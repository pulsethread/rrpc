#include "rage_region.h"
#pragma once
/* Title-owned sky receipts. The original asset tiles/colours are retained;
 * their camera pan and roll are rendered as a smooth native background. */
namespace RageSky {
struct Packet {uint32_t address=0,count=0;std::array<uint32_t,12> words{};};
struct View {uint32_t start=0,end=0,ret=0,sp=0,tick=0,key=0;RageSceneVertex pose{};std::vector<Packet> packets;std::vector<uint64_t> rows;};
static View pending;
static std::deque<View> views;
static std::unordered_map<uint32_t,View> history;
static uint32_t tick=~0u;
static uint32_t (*read)(uint32_t)=nullptr;
static std::atomic<uint64_t> packets{0},triangles{0};
static void finish(CPUState* cpu) {
    pending.end=cpu->read_word(0x1f800000)&0x1fffffffu;
    if(pending.end>pending.start && pending.end<=0x200000 && pending.end-pending.start<=0x40000) {
        const unsigned lengths[8]={4,7,5,9,6,9,8,12};
        for(uint32_t p=pending.start;p+8<=pending.end;) {
            unsigned count=cpu->read_word(p)>>24;std::vector<Packet> found;
            bool valid=count && count<=64 && p+4*(count+1)<=pending.end;
            for(unsigned n=0;valid && n<count;) {
                uint32_t address=p+4*(n+1),op=cpu->read_word(address)>>24;
                unsigned words=op>=0x20 && op<=0x3f?lengths[(op-0x20)>>2]:
                    ((op<=1 || (op>=0xe1 && op<=0xe6))?1:0);
                if(!words || n+words>count){valid=false;break;}
                if(op>=0x20 && op<=0x3f){Packet record;record.address=address;record.count=words;
                    for(unsigned i=0;i<words;++i)record.words[i]=cpu->read_word(address+4*i);found.push_back(record);}
                n+=words;
            }
            if(valid){pending.packets.insert(pending.packets.end(),found.begin(),found.end());p+=4*(count+1);}else p+=4;
        }
        views.push_back(pending);while(views.size()>8)views.pop_front();history[pending.key]=pending;
    }
    pending={};
}
}
extern "C" void rage_sky_reset(void) {
    RageSky::pending={};RageSky::views.clear();RageSky::history.clear();RageSky::read=nullptr;
    RageSky::tick=~0u;RageSky::packets=0;RageSky::triangles=0;
}
namespace RageSky {
static void dispatch(CPUState* cpu,uint32_t target) {
    if(pending.ret && (target==pending.ret || cpu->gpr[29]>pending.sp))finish(cpu);
    if(target!=0x418d4)return;
    const uint32_t now=cpu->read_word(rage_region_address(0x8009e690));
    if(tick!=now){views.clear();tick=now;}
    read=cpu->read_word;
    const bool mirror=cpu->read_word(0x1f800068)!=0,mode=cpu->read_word(rage_region_address(0x8019cacc))!=0;
    const uint32_t key=(rage_region_code(cpu->gpr[31]))^(mirror?0x800000u:0u)^(mode?0x400000u:0u);
    pending={};pending.key=key;pending.ret=rage_region_code(cpu->gpr[31]);pending.sp=cpu->gpr[29];pending.tick=now;
    pending.start=cpu->read_word(0x1f800000)&0x1fffffffu;
    auto& p=pending.pose;p.node=0x60000000u|(key&0xffffffu);
    p.flags=RAGE_SCENE_CAMERA|RAGE_SCENE_FLOAT_TRANSLATION|RAGE_SCENE_BACKGROUND;
    double yaw=int32_t(cpu->read_word(0x1f80001c))*(mirror?-1.0:1.0);
    auto old=history.find(key);
    if(old!=history.end())yaw=old->second.pose.camera[3]*2+std::remainder(yaw-old->second.pose.camera[3]*2,4096.0);
    int pitch=int32_t(cpu->read_word(0x1f800018))*(mirror!=mode?-1:1);pitch=((pitch+2048)&4095)-2048;
    const double roll=int32_t(cpu->read_word(0x1f800020))*(mode?1.0:-1.0)*6.283185307179586/4096;
    const int16_t co=int16_t(std::lround(std::cos(roll)*4096)),si=int16_t(std::lround(std::sin(roll)*4096));
    p.current.r[0]=p.current.r[4]=co;p.current.r[1]=si;p.current.r[3]=-si;p.current.r[8]=4096;
    p.camera[3]=float(yaw/2);p.camera[4]=float(pitch/2.0+1+(int32_t(cpu->read_word(0x1f80000c))-6000)/64.0);
    for(int r=0;r<3;++r){double tr=0;for(int c=0;c<3;++c)tr-=p.current.r[r*3+c]/4096.0*p.camera[3+c];
        p.current.t[r]=int32_t(std::lround(tr));p.translation[3+r]=float(tr);}
    float lens[4];gte_scene_projection(cpu,lens);
    p.projection[1]=256;p.projection[0]=lens[1]>0?256*lens[0]/lens[1]:256;
    p.projection[2]=160;p.projection[3]=mirror!=mode?36:120;
    p.previous=p.current;std::memcpy(p.camera,p.camera+3,12);std::memcpy(p.translation,p.translation+3,12);
    if(old!=history.end() && now>old->second.tick && now-old->second.tick<=4){
        const auto& a=old->second.pose;p.history=1;p.previous=a.current;
        std::memcpy(p.camera,a.camera+3,12);std::memcpy(p.translation,a.translation+3,12);
        double agreement=0;for(int i=0;i<9;++i)agreement+=double(a.current.r[i])*p.current.r[i]/(4096.0*4096.0);
        if(agreement<2 || std::abs(p.camera[4]-p.camera[1])>128)p.history=0;
    }
    rage_scene_prepare(&p,1);
}
static int submit(uint32_t address,int dx,int dy,uint32_t window,const uint32_t* words,unsigned count) {
    if(!read || address==UINT32_MAX || !words || count>12 || read(rage_region_address(0x8009e690))!=tick)return 0;
    address&=0x1fffffffu;
    for(auto it=views.rbegin();it!=views.rend();++it){
        View& v=*it;if(address<v.start || address>=v.end)continue;
        auto found=std::lower_bound(v.packets.begin(),v.packets.end(),address,[](const Packet& p,uint32_t a){return p.address<a;});
        if(found==v.packets.end() || found->address!=address || found->count!=count || std::memcmp(found->words.data(),words,count*4))continue;
        const unsigned op=words[0]>>24,n=(op&8)?4:3;const bool textured=(op&4)!=0,gouraud=(op&16)!=0;
        const unsigned stride=(textured?2:1)+(gouraud?1:0);
        const uint16_t clut=textured?uint16_t(words[2]>>16):0,page=textured?uint16_t(words[2+stride]>>16):0;
        PSXProjectedVertex vertices[4]{};float region[4]={255,255,0,0};
        for(unsigned i=0;i<n;++i){const unsigned index=1+stride*i;const uint32_t xy=words[index],uv=textured?words[index+1]:0;
            const uint32_t colour=words[gouraud?index-1:0];auto& vertex=vertices[i];vertex.scene=v.pose;
            const double x=int16_t(xy)-v.pose.projection[2],y=int16_t(xy>>16)-v.pose.projection[3];
            const double a=v.pose.current.r[0]/4096.0,b=v.pose.current.r[1]/4096.0,det=a*a+b*b;
            if(det<0.5)return 0;
            double wx=(a*x-b*y)/det+v.pose.camera[3],wy=(b*x+a*y)/det+v.pose.camera[4];
            // The authored cloud strip is a 64 by 128 pixel lattice. Restore
            // its shared vertices before integer screen rounding can wobble it.
            if(textured && (page&0x1ff)==0x18 && clut==0x798e){
                const double sx=std::round(wx/64)*64,sy=std::round((wy+208)/128)*128-208;
                if(std::abs(wx-sx)<2)wx=sx;if(std::abs(wy-sy)<2)wy=sy;
            }
            // Colour bands are screen-filling backgrounds, while cloud tiles
            // have an angular width. Extend only the bands after FOV scaling.
            if(!textured && v.pose.projection[0]>0 && v.pose.projection[0]<256)
                wx=v.pose.camera[3]+(wx-v.pose.camera[3])*256/v.pose.projection[0];
            vertex.scene.local[0]=float(wx);vertex.scene.local[1]=float(wy);vertex.scene.local[2]=256;
            vertex.scene.projection[2]+=dx;vertex.scene.projection[3]+=dy;
            vertex.scene.flags|=RAGE_SCENE_REGION;if(!textured)vertex.scene.flags|=RAGE_SCENE_SOLID;
            vertex.u=uv&255;vertex.v=(uv>>8)&255;
            region[0]=std::min(region[0],float(vertex.u));region[1]=std::min(region[1],float(vertex.v));
            region[2]=std::max(region[2],float(vertex.u));region[3]=std::max(region[3],float(vertex.v));
            // The flat lower sky quad fills undefined space behind the track.
            // Keep gradient/cloud bands authored; use charcoal only for this fallback.
            if(!textured && !gouraud && n==4)vertex.r=vertex.g=vertex.b=12.f/255.f;
            else{vertex.r=(colour&255)/255.0;vertex.g=((colour>>8)&255)/255.0;vertex.b=((colour>>16)&255)/255.0;}
            double camera[3];rage_scene_evaluate(&vertex.scene,1,camera);vertex.z=camera[2];
            vertex.x=(camera[0]*vertex.scene.projection[0]/256+vertex.scene.projection[2])*vertex.z;
            vertex.y=(camera[1]+vertex.scene.projection[3])*vertex.z;
        }
        for(unsigned i=0;i<n;++i){std::memcpy(vertices[i].scene.region,region,sizeof region);
            if(window&31)vertices[i].scene.flags|=RAGE_SCENE_REPEAT_U;if(window&(31<<5))vertices[i].scene.flags|=RAGE_SCENE_REPEAT_V;}
        const int semi=(op&2)?((page>>5)&3):-1;
        if(n==4 && textured && !gouraud && (page&0x1ff)==0x18 && clut==0x798e &&
           std::abs(vertices[1].scene.local[0]-vertices[0].scene.local[0]-64)<0.01 &&
           std::abs(vertices[2].scene.local[1]-vertices[0].scene.local[1]-128)<0.01) {
            // Reconstruct the authored repeating cloud strip, including the
            // columns the original 4:3 CPU visibility test never emitted.
            // Tile UVs come from the game's own eight-tile ROM table.
            const int cell=int(std::lround(vertices[0].scene.local[0]/64));
            const int source_tile=(unsigned(vertices[0].u)/64)+4*(vertices[0].v>=128);
            const unsigned shift=unsigned(source_tile-cell)&7u;
            const int row=int(std::lround(vertices[0].scene.local[1]));
            const uint64_t key=(uint64_t(uint32_t(row))<<32)|(uint64_t(shift)<<24)|(words[0]&0xffffffu);
            packets.fetch_add(1,std::memory_order_relaxed);
            if(std::find(v.rows.begin(),v.rows.end(),key)!=v.rows.end())return 1;
            if(v.rows.size()>=16)return 0;v.rows.push_back(key);
            const int centre=int(std::floor(v.pose.camera[3]/64));
            for(int column=centre-8;column<centre+8;++column){
                PSXProjectedVertex tile[4];std::memcpy(tile,vertices,sizeof tile);
                float bounds[4]={255,255,0,0};const unsigned id=(unsigned(column)+shift)&7;
                for(int corner=0;corner<4;++corner){
                    const uint32_t uv_pair=read(rage_region_address(0x8007f510)+8*id+4*(corner/2));const uint16_t uv=uint16_t(uv_pair>>(16*(corner%2)));
                    tile[corner].u=uv&255;tile[corner].v=uv>>8;
                    tile[corner].scene.local[0]=float(column*64+((corner&1)?64:0));
                    tile[corner].scene.local[1]=float(row+((corner&2)?128:0));
                    bounds[0]=std::min(bounds[0],float(tile[corner].u));bounds[1]=std::min(bounds[1],float(tile[corner].v));
                    bounds[2]=std::max(bounds[2],float(tile[corner].u));bounds[3]=std::max(bounds[3],float(tile[corner].v));
                    double camera[3];rage_scene_evaluate(&tile[corner].scene,1,camera);
                    tile[corner].x=(camera[0]*tile[corner].scene.projection[0]/256+tile[corner].scene.projection[2])*256;
                    tile[corner].y=(camera[1]+tile[corner].scene.projection[3])*256;
                }
                for(auto& vertex:tile)std::memcpy(vertex.scene.region,bounds,sizeof bounds);
                gl_renderer_draw_projected_triangle(tile,page&0x1ff,(clut&63)*16,(clut>>6)&511,op&1,semi,1);
                const PSXProjectedVertex second[3]={tile[2],tile[1],tile[3]};
                gl_renderer_draw_projected_triangle(second,page&0x1ff,(clut&63)*16,(clut>>6)&511,op&1,semi,1);
            }
            triangles.fetch_add(32,std::memory_order_relaxed);return 1;
        }
        gl_renderer_draw_projected_triangle(vertices,page&0x1ff,(clut&63)*16,(clut>>6)&511,textured?(op&1):0,semi,1);
        if(n==4){const PSXProjectedVertex second[3]={vertices[2],vertices[1],vertices[3]};
            gl_renderer_draw_projected_triangle(second,page&0x1ff,(clut&63)*16,(clut>>6)&511,textured?(op&1):0,semi,1);}
        packets.fetch_add(1,std::memory_order_relaxed);triangles.fetch_add(n-2,std::memory_order_relaxed);return 1;
    }
    return 0;
}
}
extern "C" void rage_sky_dispatch(CPUState* cpu,uint32_t target){RageSky::dispatch(cpu,target);}
extern "C" int rage_sky_submit(uint32_t address,int dx,int dy,uint32_t window,const uint32_t* words,unsigned count){return RageSky::submit(address,dx,dy,window,words,count);}
extern "C" void rage_sky_stats(uint64_t* p,uint64_t* t){if(p)*p=RageSky::packets.load();if(t)*t=RageSky::triangles.load();}
