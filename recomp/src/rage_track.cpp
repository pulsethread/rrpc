#include "rage_region.h"
#include "rage_track.h"
#include "rage_oval.h"
#include "native_visibility.h"
#include "rage_track_fade.h"
#include "rage_track_visibility.h"
#include "native_terrain_publication.h"
#include "rage_scene.h"
#include "rage_minimap.h"
#include "cpu_state.h"
#include "execution_profile.h"
#include "native_terrain_mirror.h"
#include "gpu_gl_renderer.h"
#include "gpu_projective_clip.h"
#include "gpu_render.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <chrono>
#include <memory>
#include <unordered_map>

namespace {
bool enabled=false;
std::atomic<int> extended_distance{0},lod_fade{1};
std::atomic<bool> replay_distance{false};
std::atomic<uint64_t> fade_starts{0};
std::atomic<uint64_t> native_views{0};
std::atomic<uint64_t> native_mirror_views{0};

struct ProjectionCache {
    std::vector<RageTrackProjection> points;
    std::vector<uint32_t> stamps;
    uint32_t epoch=0;
    void begin(size_t count){points.resize(count);stamps.resize(count);
        if(!++epoch){std::fill(stamps.begin(),stamps.end(),0);epoch=1;}}
    const RageTrackProjection& get(uint32_t id,const RageTrackCourse& mesh,const RageSceneVertex& pose){
        if(stamps[id]!=epoch){const auto& local=mesh.vertices[id];double camera[3]={};
            for(int row=0;row<3;++row)for(int col=0;col<3;++col)
                camera[row]+=pose.drawing[4*row+col]*(local[col]-pose.drawing_camera[col]);
            points[id]={camera[0]*pose.projection[0]+camera[2]*pose.projection[2],
                camera[1]*pose.projection[1]+camera[2]*pose.projection[3],camera[2]};stamps[id]=epoch;}
        return points[id];
    }
};
struct FadeView {std::vector<RageTrackFadeState> sectors;float seen=-1000;ProjectionCache current,previous;};
std::unordered_map<uint32_t,FadeView> fade_views;

struct PreparedSector {std::vector<PSXTerrainTriangle> layers[2];};
using PreparedCourse=std::vector<PreparedSector>;
std::shared_ptr<PreparedCourse> drawing_prepared;
std::shared_ptr<PreparedCourse> course_prepared;
// Like the native port's mesh templates, bake only camera-independent data.
// Called after all donor clipping, seam repair and decal conformance are done.
std::shared_ptr<PreparedCourse> prepare_course(const RageTrackCourse& mesh){
    auto result=std::make_shared<PreparedCourse>(mesh.sectors.size());
    for(size_t s=0;s<mesh.sectors.size();++s){const auto& sector=mesh.sectors[s];
        for(uint32_t q=sector.first;q<sector.first+sector.count;++q){
            const auto& face=mesh.faces[sector.draw_order.empty()?q:sector.draw_order[q-sector.first]];
            PSXTerrainTriangle t{};t.colour=face.colour;t.page=face.page;t.window=face.window;
            t.clut[0]=uint16_t(face.clut+((face.type==3 || face.type==5)?1:0));
            t.clut[1]=uint16_t(face.clut+((face.type==3 || face.type==5 || face.type<2)?1:0));
            t.flags=RAGE_SCENE_REGION;
            if(face.coverage)t.flags|=RAGE_SCENE_COVERAGE;
            if(face.decal)t.flags|=RAGE_SCENE_DECAL;
            t.flags|=uint32_t(uint8_t(face.decal?std::min(-1,int(face.depth_bias)):face.depth_bias))<<16;
            if(face.window&31)t.flags|=RAGE_SCENE_REPEAT_U;
            if(face.window&(31<<5))t.flags|=RAGE_SCENE_REPEAT_V;
            const auto& a=mesh.vertices[face.vertex[0]];const auto& b=mesh.vertices[face.vertex[1]];const auto& c=mesh.vertices[face.vertex[2]];
            const double ex=b[0]-a[0],ey=b[1]-a[1],ez=b[2]-a[2],fx=c[0]-a[0],fy=c[1]-a[1],fz=c[2]-a[2];
            const double nx=ey*fz-ez*fy,ny=ez*fx-ex*fz,nz=ex*fy-ey*fx;
            if(ny*ny<.1*(nx*nx+ny*ny+nz*nz))t.flags|=RAGE_SCENE_WALL_CULL;
            t.region[0]=t.region[1]=255;
            for(int k=0;k<4;++k){const float u=face.uv[k]&255,v=(face.uv[k]>>8)&255;
                t.region[0]=std::min(t.region[0],u);t.region[1]=std::min(t.region[1],v);
                t.region[2]=std::max(t.region[2],u);t.region[3]=std::max(t.region[3],v);}
            for(uint32_t i=face.first_triangle;i<face.first_triangle+face.triangle_count;++i){
                const auto& tri=mesh.triangles[i];memcpy(t.uv,tri.uv,sizeof t.uv);
                for(int k=0;k<3;++k)memcpy(t.local+3*k,mesh.vertices[tri.vertex[k]].data(),12);
                (*result)[s].layers[face.decal!=0].push_back(t);
            }
        }
    }
    return result;
}

std::shared_ptr<RageTrackCourse> course,drawing_course;
uint32_t course_grid=0,course_table=0,course_pool=0;
std::atomic<uint64_t> loads{0},vertex_count{0},face_count{0},draws{0},submitted{0},skipped{0},failures{0};
bool ram(uint32_t a,uint32_t bytes) {
    const uint32_t segment=a>>24,p=a&0x1fffffffu;
    return (segment==0 || segment==0x80 || segment==0xa0) && p<0x200000u && bytes<=0x200000u-p;
}
struct Point { int32_t x,y,z;bool operator==(const Point& b)const{return x==b.x&&y==b.y&&z==b.z;} };
struct PointHash { size_t operator()(const Point& p)const {
    uint64_t h=1469598103934665603ull;
    for(uint32_t v:{uint32_t(p.x),uint32_t(p.y),uint32_t(p.z)})h=(h^v)*1099511628211ull;
    return size_t(h);
} };
struct Packet {uint32_t address,count;std::array<uint32_t,12> words;};
struct View {
    uint32_t start=0,end=0,ctx=0,ret=0,sp=0,tick=0,key=0,unlit=0,texture_source=0;
    RageSceneVertex pose{};
    std::shared_ptr<RageTrackCourse> mesh;
    std::shared_ptr<PreparedCourse> prepared;
    bool drawn=false;
    bool stock_distance=false,stock_cells_valid=false;
    std::array<bool,1024> stock_cells{};
    std::vector<Packet> packets;
};
View pending;
std::deque<View> views;
std::unordered_map<uint32_t,View> history;
uint32_t capture_tick=~0u;
uint32_t (*tick_reader)(uint32_t)=nullptr;
extern "C" void gte_scene_projection(CPUState*,float[4]);
void finish(CPUState* cpu) {
    if(!pending.ret)return;
    pending.end=cpu->read_word(pending.ctx)&0x1fffffffu;
    if(pending.mesh && pending.end>pending.start && pending.end<=0x200000) {
        for(uint32_t p=pending.start;p+8<=pending.end;) {
            const unsigned count=cpu->read_word(p)>>24;
            const unsigned lengths[8]={4,7,5,9,6,9,8,12};
            std::vector<Packet> owned;
            bool valid=count>0 && count<=64 && p+4*(count+1)<=pending.end;
            for(unsigned offset=0;valid && offset<count;) {
                const uint32_t address=p+4*(offset+1);
                const unsigned op=cpu->read_word(address)>>24;
                unsigned expected=(op>=0x20 && op<=0x3f)?lengths[(op-0x20)>>2]:
                    ((op>=0x40 && op<=0x47)?3:(op>=0x50 && op<=0x57)?4:
                    ((op<=1 || (op>=0xe1 && op<=0xe6))?1:0));
                if((op>=0x48 && op<=0x4f) || (op>=0x58 && op<=0x5f)) {
                    const unsigned step=(op&16)?2:1,first=(op&16)?4:3;
                    for(unsigned n=first;n<12 && offset+n<count;n+=step)
                        if((cpu->read_word(address+4*n)&0xf000f000u)==0x50005000u){expected=n+1;break;}
                }
                if(!expected || offset+expected>count){valid=false;break;}
                if(op>=0x20 && op<=0x5f) {
                    Packet record{};record.address=address;record.count=expected;
                    for(unsigned i=0;i<expected;++i)record.words[i]=cpu->read_word(address+4*i);
                    owned.push_back(record);
                }
                offset+=expected;
            }
            if(valid) {
                pending.packets.insert(pending.packets.end(),owned.begin(),owned.end());p+=4*(count+1);
            } else p+=4;
        }
        views.push_back(pending);
        while(views.size()>16)views.pop_front();
        history[pending.key]=pending;
    }
    pending={};
}
bool visible(const RageTrackSector& sector,const RageSceneVertex& pose,int left,int top,int right,int bottom) {
    unsigned common=63;
    for(int c=0;c<8;++c) {
        double p[3]={};
        for(int row=0;row<3;++row) {
            p[row]=pose.drawing[4*row+3];
            for(int col=0;col<3;++col)p[row]+=pose.drawing[4*row+col]*((c&(1<<col))?sector.hi[col]:sector.lo[col]);
        }
        const double x=p[0]*pose.projection[0]+p[2]*pose.projection[2];
        const double y=p[1]*pose.projection[1]+p[2]*pose.projection[3];
        unsigned mask=(p[2]<1?1u:0u)|(p[2]>rage_track_far_limit()?2u:0u)|
            (x<left*p[2]?4u:0u)|(x>right*p[2]?8u:0u)|
            (y<top*p[2]?16u:0u)|(y>bottom*p[2]?32u:0u);
        common&=mask;
    }
    return !common;
}
}

extern "C" void rage_track_set_extended_distance(int on){extended_distance.store(on!=0,std::memory_order_relaxed);}
extern "C" int rage_track_extended_distance(){return extended_distance.load(std::memory_order_relaxed);}
extern "C" float rage_track_fade_clock(){
    static const auto epoch=std::chrono::steady_clock::now();
    return 0.001f+std::chrono::duration<float>(std::chrono::steady_clock::now()-epoch).count();
}
extern "C" void rage_track_set_lod_fade(int on){lod_fade.store(on!=0,std::memory_order_relaxed);}
extern "C" uint64_t rage_track_fade_starts(){return fade_starts.load(std::memory_order_relaxed);}
extern "C" uint64_t rage_track_native_views(){return native_views.load(std::memory_order_relaxed);}
extern "C" uint64_t rage_track_native_mirror_views(){return native_mirror_views.load(std::memory_order_relaxed);}
extern "C" int rage_track_lod_fade(){return lod_fade.load(std::memory_order_relaxed);}
extern "C" float rage_track_far_limit(){return !replay_distance.load(std::memory_order_relaxed) && rage_track_extended_distance()?196608.f:131072.f;}
extern "C" int rage_track_replay_distance(){return replay_distance.load(std::memory_order_relaxed);}

bool rage_track_decode(CPUState* cpu,uint32_t grid,uint32_t table,uint32_t pool,
                       uint32_t sector_count,RageTrackCourse& out) {
    if(!cpu || !cpu->read_word || !cpu->read_half || !sector_count || sector_count>1023 ||
       !ram(grid,2048) || !ram(table,4*sector_count) || !ram(pool,8))return false;
    RageTrackCourse mesh;
    if(ram(grid,6144)){for(unsigned i=0;i<1024;++i){mesh.cell_grid[i]=cpu->read_half(grid+2*i);mesh.cell_visibility[i]=cpu->read_word(grid+2048+4*i);}mesh.visibility_valid=true;}

    std::unordered_map<Point,uint32_t,PointHash> welded;
    for(uint32_t z=0;z<32;++z)for(uint32_t x=0;x<32;++x) {
        const uint32_t id=cpu->read_half(grid+2*((31-z)*32+x))&1023;
        if(id==1023)continue;
        if(id>=sector_count)return false;
        uint32_t cursor=cpu->read_word(table+4*id);
        RageTrackSector sector{};sector.cell_x=x;sector.cell_z=z;sector.first=(uint32_t)mesh.faces.size();
        for(int i=0;i<3;++i){sector.lo[i]=1e30f;sector.hi[i]=-1e30f;}
        bool terminated=false;
        for(unsigned group=0;group<8192;++group) {
            if(!ram(cursor,4))return false;
            const uint32_t command=cpu->read_word(cursor);cursor+=4;
            if(!command){terminated=true;break;}
            const unsigned type=command&65535,count=command>>16;
            if(type>5 || !count || count>8192)return false;
            const unsigned stride=(type==1 || type==4 || type==5)?36:32;
            if(!ram(cursor,count*stride) || mesh.faces.size()+count>65536)return false;
            for(unsigned f=0;f<count;++f,cursor+=stride) {
                RageTrackFace face{};
                for(int k=0;k<4;++k) {
                    const uint32_t vi=cpu->read_half(cursor+2*k);
                    const uint32_t p=pool+8*vi;
                    if(!ram(p,8))return false;
                    Point world{int16_t(cpu->read_half(p))+int32_t(x*8192+4096),
                        int16_t(cpu->read_half(p+2)),int16_t(cpu->read_half(p+4))+int32_t(z*8192+4096)};
                    auto found=welded.find(world);
                    if(found==welded.end()) {
                        if(mesh.vertices.size()>=262144)return false;
                        const uint32_t v=(uint32_t)mesh.vertices.size();
                        mesh.vertices.push_back({float(world.x),float(world.y),float(world.z)});
                        found=welded.emplace(world,v).first;
                    }
                    face.vertex[k]=found->second;
                    const auto& v=mesh.vertices[found->second];
                    for(int j=0;j<3;++j){sector.lo[j]=std::min(sector.lo[j],v[j]);sector.hi[j]=std::max(sector.hi[j],v[j]);}
                }
                const uint32_t uv0=cpu->read_word(cursor+8),uv1=cpu->read_word(cursor+12),uv23=cpu->read_word(cursor+16);
                face.uv[0]=uv0&65535;face.uv[1]=uv1&65535;face.uv[2]=uv23&65535;face.uv[3]=uv23>>16;
                face.clut=uint16_t(uv0>>16);face.page=uint16_t(uv1>>16);
                face.type=(uint16_t)type;
                face.depth_bias=int8_t(cpu->read_word(cursor+20)>>8);
                face.surface_flags=cpu->read_word(cursor+24);
                face.colour=cpu->read_word(cursor+28);
                face.window=stride==36?(cpu->read_word(cursor+32)&0xfffff):0;
                mesh.faces.push_back(face);
            }
        }
        if(!terminated)return false;
        sector.count=(uint32_t)mesh.faces.size()-sector.first;
        if(sector.count)mesh.sectors.push_back(sector);
    }
    if(mesh.faces.empty())return false;
    if(!rage_track_build_topology(mesh))return false;
    rage_track_classify_layers(mesh);rage_track_stitch_geometry(mesh);rage_track_conform_details(mesh);out=std::move(mesh);return true;
}

extern "C" int rage_track_ground_height(float x,float z,float near_y,float* height){
    return course && height && rage_track_sample_ground(*course,x,z,near_y,*height);
}
extern "C" void rage_track_enable(int on) {
    if(enabled==(on!=0))return;
    enabled=on!=0;course.reset();drawing_course.reset();drawing_prepared.reset();views.clear();history.clear();fade_views.clear();pending={};
    capture_tick=~0u;tick_reader=nullptr;course_prepared.reset();replay_distance=false;
    loads=vertex_count=face_count=draws=submitted=skipped=failures=0;
    rage_sky_reset();
}
extern "C" void rage_track_reset(void) {
    rage_minimap_clear();
    rage_sky_reset();
    course.reset();drawing_course.reset();drawing_prepared.reset();views.clear();history.clear();fade_views.clear();pending={};
    course_grid=course_table=course_pool=0;
    capture_tick=~0u;tick_reader=nullptr;course_prepared.reset();replay_distance=false;
}
extern "C" void rage_track_dispatch(CPUState* cpu,uint32_t target) {
    if(!cpu)return;
    target=rage_region_code(target);
    if((target==0x414f0 || target==0x27ff4) && cpu->read_word)
        replay_distance.store(cpu->read_word(rage_region_address(0x801e42e4))==17,std::memory_order_relaxed);
    // The retained environment walker has its own stock near/far sector
    // limits. Extend these as well as the reconstructed terrain's range.
    if(target==0x414f0 && !replay_distance.load(std::memory_order_relaxed) && extended_distance.load(std::memory_order_relaxed)){
        const int32_t near_limit=int32_t(cpu->gpr[4]),far_limit=int32_t(cpu->gpr[5]);
        if(near_limit>=-131072 && near_limit<=0 && far_limit>0 && far_limit<=131072){
            cpu->gpr[4]=uint32_t(near_limit*3/2);cpu->gpr[5]=uint32_t(far_limit*3/2);
        }
    }
    if(!enabled)return;
    rage_sky_dispatch(cpu,target);
    if(pending.ret && (target==pending.ret || cpu->gpr[29]>pending.sp))finish(cpu);
    if(target==0x17ad0) {rage_minimap_clear();course.reset();course_prepared.reset();drawing_course.reset();drawing_prepared.reset();views.clear();history.clear();fade_views.clear();pending={};rage_sky_reset();return;}
    if(target!=0x27ff4 || cpu->gpr[4]!=0x1f800000)return;
    const uint32_t new_tick=cpu->read_word(rage_region_address(0x8009e690));
    if(capture_tick!=new_tick){views.clear();capture_tick=new_tick;pending={};}
    tick_reader=cpu->read_word;
    const uint32_t grid=cpu->read_word(rage_region_address(0x801e5020)),table=cpu->read_word(0x1f80005c),pool=cpu->read_word(0x1f800060);
    if(!course || grid!=course_grid || table!=course_table || pool!=course_pool) {
        auto loaded=std::make_shared<RageTrackCourse>();
        if(!rage_track_decode(cpu,grid,table,pool,cpu->read_word(rage_region_address(0x801e4144)),*loaded)) {
            failures.fetch_add(1,std::memory_order_relaxed);return;
        }
        // Original centreline table keeps its count immediately before the points.
        const uint32_t path=cpu->read_word(rage_region_address(0x8009e690)-8);
        if(ram(path-4,4)){const uint32_t points=cpu->read_word(path-4);
            if(points>=16 && points<=4096 && ram(path,points*24))for(uint32_t i=0;i<points;++i){const uint32_t p=path+24*i;
                loaded->route.push_back({float(int32_t(cpu->read_word(p))*4.0),float(int16_t(cpu->read_half(p+8))*4),float(int32_t(cpu->read_word(p+4))*4.0),float(cpu->read_half(p+16)*4),float(cpu->read_half(p+18)*4)});
            }}
        auto scenery=std::make_shared<RageTrackCourse>();
        drawing_course=rage_oval_extend(*loaded,*scenery)?scenery:loaded;
        course_prepared=prepare_course(*loaded);
        drawing_prepared=drawing_course==loaded?course_prepared:prepare_course(*drawing_course);
        fade_views.clear();course=std::move(loaded);course_grid=grid;course_table=table;course_pool=pool;
        std::vector<float> map_points;map_points.reserve(course->route.size()*2);
        for(const auto& p:course->route){map_points.push_back(p.x);map_points.push_back(p.z);}
        rage_minimap_route(map_points.data(),int(course->route.size()));
        loads.fetch_add(1,std::memory_order_relaxed);vertex_count=course->vertices.size();face_count=course->faces.size();
        history.clear();
    }
    rage_minimap_capture(cpu);
    finish(cpu);
    pending={};pending.stock_distance=replay_distance.load(std::memory_order_relaxed);
    pending.mesh=pending.stock_distance?course:drawing_course;
    pending.prepared=pending.stock_distance?course_prepared:drawing_prepared;
    pending.ctx=cpu->gpr[4];pending.ret=rage_region_code(cpu->gpr[31]);
    pending.sp=cpu->gpr[29];pending.start=cpu->read_word(pending.ctx)&0x1fffffffu;
    pending.tick=new_tick;pending.key=pending.ret^(cpu->read_word(pending.ctx+0x68)?0x1000000u:0);
    rage_scene_lights_capture(cpu,new_tick);
    pending.unlit=cpu->read_word(pending.ctx+0x84)!=0;
    // Capture exactly the stock renderer's visible sector identities. Native
    // interpolation reuses this selection rather than extending it off-camera.
    if(pending.stock_distance){const uint32_t cells=cpu->gpr[5],count=cpu->gpr[6];
        if(count<=64 && ram(cells,count*16)){pending.stock_cells_valid=true;
            for(uint32_t i=0;i<count;++i){const int32_t id=int32_t(cpu->read_word(cells+16*i+12));
                if(id>=0 && id<1023)pending.stock_cells[unsigned(id)]=true;}
        }
    }
    const unsigned variant=cpu->read_word(rage_region_address(0x8009e690)+0x14),kind=cpu->read_word(rage_region_address(0x801e42e4)-0x58);
    const uint32_t wanted_address[3]={0x801e6f10,0x801e669c,0x801e6c18};
    const unsigned page=cpu->read_word(wanted_address[rage_active_region]);
    if(variant<6 && kind<4 && page<2){unsigned source=2+variant*8+kind*2+page;if(rage_course_texture_pixels(source))pending.texture_source=source;}

    auto& p=pending.pose;p.node=0x70000000u|(pending.key&0xfffffu);p.flags=RAGE_SCENE_FLOAT_TRANSLATION|RAGE_SCENE_CAMERA;
    p.lamps.tick=new_tick;p.lamps.head=1;p.lamps.tail=1;p.lamps.speed=rage_active_region==0?25.f:30.f;
    const bool mirror=cpu->read_word(pending.ctx+0x68)!=0;
    if(mirror){p.flags|=RAGE_SCENE_MIRRORED_VIEW;p.node|=0x00800000u;}
    for(int row=0;row<3;++row)for(int col=0;col<3;++col) {
        int16_t r=int16_t(cpu->read_half(pending.ctx+0x28+2*(3*row+col)));
        if(mirror && row==0)r=-r;
        p.current.r[3*row+col]=r;p.drawing[4*row+col]=r/4096.0f;
    }
    for(int i=0;i<3;++i)p.camera[i]=p.camera[3+i]=p.drawing_camera[i]=(float)(int32_t(cpu->read_word(pending.ctx+8+4*i))*4.0);
    for(int row=0;row<3;++row) {
        double t=0;for(int col=0;col<3;++col)t-=p.drawing[4*row+col]*(int32_t(cpu->read_word(pending.ctx+8+4*col))*4.0);
        p.translation[3+row]=p.drawing[4*row+3]=(float)t;p.current.t[row]=(int32_t)std::lround(t);
    }
    gte_scene_projection(cpu,p.projection);
    if(p.projection[0]<=0 || p.projection[1]<=0){pending={};return;}
    p.previous=p.current;std::memcpy(p.translation,p.translation+3,12);
    auto old=history.find(pending.key);
    if(old!=history.end() && pending.tick>old->second.tick && pending.tick-old->second.tick<=4) {
        p.lamps.tail=float(pending.tick-old->second.tick);
        p.history=1;p.previous=old->second.pose.current;
        std::memcpy(p.translation,old->second.pose.translation+3,12);
        std::memcpy(p.camera,old->second.pose.camera+3,12);
        for(int i=0;i<3;++i)if(std::abs(double(p.translation[3+i])-p.translation[i])>16384)p.history=0;
        double agreement=0;for(int i=0;i<9;++i)agreement+=double(p.previous.r[i])*p.current.r[i]/(4096.0*4096.0);
        if(agreement<2)p.history=0; // A camera cut is not a camera movement.
    }

}

extern "C" int rage_track_native_try(CPUState* cpu,uint32_t target){
    // Native publication boundary. The captured view owns one
    // inert ordering-table marker instead of thousands of stock terrain
    // packets. Its camera, mesh, materials and interpolation history remain
    // the same as the established replacement path.
    const char* option=std::getenv("RAGE_NATIVE_TERRAIN");
    if(!PSX_EXECUTION_ENHANCED || (option && option[0]=='0') || !enabled || !cpu ||
       rage_region_code(target)!=0x27ff4 || !pending.ret ||
       pending.ctx!=cpu->gpr[4] || pending.ctx!=0x1f800000 ||
       !pending.mesh || !pending.prepared ||
       gr_backend()!=GR_BACKEND_OPENGL || !gl_renderer_projective_supported())return 0;
    const bool mirror=(pending.pose.flags&RAGE_SCENE_MIRRORED_VIEW)!=0;
    const char* mirror_option=std::getenv("RAGE_NATIVE_MIRROR");
    if(mirror && mirror_option && mirror_option[0]=='0')return 0;
    if(!cpu->write_word || cpu->read_word(target)!=0xac9f008cu ||
       cpu->read_word(target+0xf0)!=0x03e00008u)return 0;
    uint32_t last_cell=0;
    if(mirror){
        const uint32_t cells=cpu->gpr[5],count=cpu->gpr[6];
        if(count>64 || !ram(cells,count*16))return 0;
        for(uint32_t i=0;i<count;++i){const uint32_t cell=cells+i*16;
            const int32_t id=int32_t(cpu->read_word(cell+12));
            if(id<0)continue;
            if(id>=1023 || !ram(course_table+4*uint32_t(id),4))return 0;
            last_cell=cell;
        }
    }
    const uint32_t cursor=cpu->read_word(pending.ctx),ot=cpu->read_word(pending.ctx+4);
    if((cursor&0x1fffffffu)!=pending.start || !ram(cursor,20) || !ram(ot,704*4))return 0;
    // Terrain occupies OT entries128..575; sky is702/703. Publish at the far
    // terrain boundary, after sky and before nearer native depth-tested work.
    if(!rage_native_terrain_publish(cursor,ot,cpu->read_word,cpu->write_word))return 0;
    if(mirror){
        rage_native_mirror_state(cpu,last_cell);
        native_mirror_views.fetch_add(1,std::memory_order_relaxed);
    }
    cpu->write_word(pending.ctx,cursor+20);
    native_views.fetch_add(1,std::memory_order_relaxed);
    cpu->pc=cpu->gpr[31];
    return 1;
}

extern "C" int rage_track_submit(uint32_t packet,int dx,int dy,uint32_t texture_window,
                                  int left,int top,int right,int bottom,const uint32_t* words,unsigned count) {
    if(!enabled || packet==UINT32_MAX || !words || count>12 || !tick_reader || tick_reader(rage_region_address(0x8009e690))!=capture_tick)return 0;
    packet&=0x1fffffffu;
    for(auto it=views.rbegin();it!=views.rend();++it) {
        View& view=*it;if(packet<view.start || packet>=view.end)continue;
        auto owned=std::lower_bound(view.packets.begin(),view.packets.end(),packet,
            [](const Packet& p,uint32_t address){return p.address<address;});
        if(owned==view.packets.end() || owned->address!=packet)continue;
        const unsigned op=words[0]>>24;
        const bool polyline=count==1 && ((op>=0x48 && op<=0x4f)||(op>=0x58 && op<=0x5f));
        if(polyline) {
            bool exact=owned->words[0]==words[0];
            for(unsigned i=0;i<owned->count && exact;++i)exact=tick_reader(packet+4*i)==owned->words[i];
            if(!exact)continue;
        } else if(owned->count!=count || std::memcmp(owned->words.data(),words,4*count))continue;
        skipped.fetch_add(1,std::memory_order_relaxed);
        if(view.drawn)return 1;view.drawn=true;
        auto pose=view.pose;pose.projection[2]+=dx;pose.projection[3]+=dy;
        auto previous=pose;rage_scene_prepare(&previous,0);
        // A reflected camera changes the view matrix and winding, not the
        // immutable terrain. Share the prepared/GPU-clipped mesh path with
        // the main view instead of rebuilding/projecting every mirror face.
        const char* gpu_mirror=std::getenv("RAGE_GPU_MIRROR");
        const bool gpu_clip=(pose.flags&RAGE_SCENE_MIRRORED_VIEW)==0 || !gpu_mirror || gpu_mirror[0]!='0';
        const bool prepared=gpu_clip && view.prepared && !std::getenv("RAGE_TRACK_LEGACY_SUBMIT");
        // Retain a guard band and both camera endpoints. Interpolated views
        // can expose a sector outside the newer real frame's exact rectangle.
        const int guard=std::min(128,std::max(32,std::max(right-left+1,bottom-top+1)/10));
        uint32_t window=texture_window;uint64_t count=0;
        const float now=rage_track_fade_clock();
        auto& fade=fade_views[view.key];
        if(!gpu_clip)fade.current.begin(view.mesh->vertices.size());
        const bool warm=fade.sectors.size()!=view.mesh->sectors.size() || now-fade.seen>0.25f || !pose.history;
        fade.sectors.resize(view.mesh->sectors.size());fade.seen=now;
        // Visibility history must be independent of camera orientation. The
        // old positive-Z test forgot nearby road behind the camera, then
        // faded it through the sky in mirrors and rotating replay views.
        for(size_t i=0;i<view.mesh->sectors.size();++i){
            const auto& sector=view.mesh->sectors[i];
            const float distance2=rage_track_fade_distance2(sector.lo,sector.hi,pose.drawing_camera);
            auto& state=fade.sectors[i];
            if(distance2<=float(rage_track_far_limit())*rage_track_far_limit()){
                const float old_start=state.start;
                // Contact road must always establish opaque depth. Keep the
                // optional appearance fade only for distant scenery.
                rage_track_fade_visit(state,now,warm || distance2<=32768.f*32768.f,rage_track_lod_fade()!=0);
                if(state.start>0 && state.start!=old_start)fade_starts.fetch_add(1,std::memory_order_relaxed);
            }
        }
        // Record +24 is the original edge-line command/colour, not a decal
        // classification. Every opaque course face must establish scene depth.
        for(int layer=0;layer<2;++layer)for(const auto& sector:view.mesh->sectors) {
            if(view.stock_distance && view.stock_cells_valid){
                if(sector.cell_x<0 || sector.cell_z<0)continue;
                const unsigned id=view.mesh->cell_grid[(31-sector.cell_z)*32+sector.cell_x]&1023u;
                if(!view.stock_cells[id])continue;
            }
            if(!visible(sector,pose,left-guard,top-guard,right+guard,bottom+guard) &&
               !(pose.history && visible(sector,previous,left-guard,top-guard,right+guard,bottom+guard)))continue;
            if(prepared){
                const size_t index=&sector-view.mesh->sectors.data();
                const auto& triangles=(*view.prepared)[index].layers[layer];
                auto camera=pose;camera.fade_start=rage_track_lod_fade()?fade.sectors[index].start:0;
                gl_renderer_draw_terrain_batch(&camera,triangles.data(),(unsigned)triangles.size(),view.texture_source,view.unlit,texture_window);
                count+=triangles.size();continue;
            }
            for(uint32_t q=sector.first;q<sector.first+sector.count;++q) {
                const auto& face=view.mesh->faces[sector.draw_order.empty()?q:sector.draw_order[q-sector.first]];
                if(face.decal!=unsigned(layer))continue;
                // Unlit stream handlers select the adjacent authored palette.
                // The stored CLUT is the lit variant, not always the final material.
                const uint16_t clut=(uint16_t)(face.clut+
                    ((face.type==3 || face.type==5 || (face.type<2 && view.unlit))?1:0));
                const auto& a=view.mesh->vertices[face.vertex[0]];const auto& b=view.mesh->vertices[face.vertex[1]];const auto& c=view.mesh->vertices[face.vertex[2]];
                const double ex=b[0]-a[0],ey=b[1]-a[1],ez=b[2]-a[2],fx=c[0]-a[0],fy=c[1]-a[1],fz=c[2]-a[2];
                const double nx=ey*fz-ez*fy,ny=ez*fx-ex*fz,nz=ex*fy-ey*fx;
                const bool wall=ny*ny<.1*(nx*nx+ny*ny+nz*nz);
                float region[4]={255,255,0,0};
                for(int corner=0;corner<4;++corner){const float u=face.uv[corner]&255,w=(face.uv[corner]>>8)&255;
                    region[0]=std::min(region[0],u);region[1]=std::min(region[1],w);
                    region[2]=std::max(region[2],u);region[3]=std::max(region[3],w);}
                for(uint32_t triangle=face.first_triangle;triangle<face.first_triangle+face.triangle_count;++triangle) {
                    const auto& tri=view.mesh->triangles[triangle];
                    PSXProjectedVertex vertices[3]{};
                    for(int k=0;k<3;++k) {
                        auto& vertex=vertices[k];vertex.scene=pose;
                        vertex.scene.fade_start=rage_track_lod_fade()?fade.sectors[&sector-view.mesh->sectors.data()].start:0;
                        vertex.scene.flags|=RAGE_SCENE_REGION;
                        if(gpu_clip)vertex.scene.flags|=RAGE_SCENE_GPU_CLIP;
                        if(face.coverage)vertex.scene.flags|=RAGE_SCENE_COVERAGE;
                        if(face.decal)vertex.scene.flags|=RAGE_SCENE_DECAL;
                        if(wall)vertex.scene.flags|=RAGE_SCENE_WALL_CULL;
                        vertex.scene.flags|=uint32_t(uint8_t(face.decal?std::min(-1,int(face.depth_bias)):face.depth_bias))<<16;
                        if(face.window&31)vertex.scene.flags|=RAGE_SCENE_REPEAT_U;
                        if(face.window&(31<<5))vertex.scene.flags|=RAGE_SCENE_REPEAT_V;
                        std::memcpy(vertex.scene.region,region,sizeof region);
                        const auto& local=view.mesh->vertices[tri.vertex[k]];
                        std::memcpy(vertex.scene.local,local.data(),12);
                        // Native shader transforms and clips world positions on the GPU.
                        if(gpu_clip){vertex.x=0;vertex.y=0;vertex.z=256;}
                        else {const auto& projected=fade.current.get(tri.vertex[k],*view.mesh,pose);vertex.x=projected.x;vertex.y=projected.y;vertex.z=projected.z;}
                        vertex.u=tri.uv[2*k];vertex.v=tri.uv[2*k+1];
                        vertex.r=(face.colour&255)/255.0;vertex.g=((face.colour>>8)&255)/255.0;vertex.b=((face.colour>>16)&255)/255.0;
                    }
                    // Course sheets and details are authored for painter rendering.
                    // Render both sides, as the standalone course viewer does; depth
                    // resolves occlusion without dropping twisted or reversed quads.
                    if(!gpu_clip){
                        uint32_t ids[3];int32_t pc[9],h[3],xs[3],ys[3];bool proven=true;
                        for(int k=0;k<3;++k){const auto& v=vertices[k];ids[k]=0x40000000u+tri.vertex[k];h[k]=(int32_t)pose.projection[1];
                            proven&=v.z>=8;const double depth=std::abs(v.z)<1?1:v.z;
                            xs[k]=(int32_t)std::clamp(v.x/depth,-1000000.,1000000.);ys[k]=(int32_t)std::clamp(v.y/depth,-1000000.,1000000.);
                            pc[3*k]=(int32_t)((v.x-v.z*pose.projection[2])/pose.projection[0]);pc[3*k+1]=(int32_t)((v.y-v.z*pose.projection[3])/pose.projection[1]);pc[3*k+2]=(int32_t)v.z;}
                        if(proven)gl_renderer_fg_source(ids,pc,h,xs,ys);
                    }
                    const unsigned op=face.colour>>24;
                    if(window!=face.window){gr_set_texture_window(face.window);window=face.window;}
                    gl_renderer_draw_projected_triangle(vertices,uint16_t((face.page&0x8000)?face.page:(face.page&0x1ff)|(view.texture_source<<9)),(clut&63)*16,
                        (clut>>6)&511,op&1,(op&2)?((face.page>>5)&3):-1,1);
                    ++count;
                }
            }
        }
        if(window!=texture_window)gr_set_texture_window(texture_window);
        draws.fetch_add(1,std::memory_order_relaxed);submitted.fetch_add(count,std::memory_order_relaxed);
        return 1;
    }
    return 0;
}
extern "C" void rage_track_stats(uint64_t* a,uint64_t* b,uint64_t* c,uint64_t* d,uint64_t* e,uint64_t* f,uint64_t* g) {
    if(a)*a=loads.load();if(b)*b=vertex_count.load();if(c)*c=face_count.load();
    if(d)*d=draws.load();if(e)*e=submitted.load();if(f)*f=skipped.load();if(g)*g=failures.load();
}
#include "rage_sky_scene.h"
