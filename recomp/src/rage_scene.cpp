#include "rage_region.h"
#include "rage_scene.h"
#include "cpu_state.h"
#include "rage_track.h"
#include "rage_track_fade.h"
#include "rage_music.h"
#include "rage_vehicle_material.h"
#include <array>
#include <cstdio>
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {
struct Node {
    uint32_t id, tick=0; bool captured=false, history=false;
    PSXMotionMatrix previous{}, current{};
    float projection[4]{};
    float ground_shift[3]{};
    float fade_start=0;
    bool shadow=false,shadow_projection_valid=false;
    float camera_inverse[9]{},camera_rotation[9]{},camera_origin[3]{};
    float model_inverse[9]{};
};
bool enabled=false;
uint32_t (*scene_read_word)(uint32_t)=nullptr;
uint16_t (*scene_read_half)(uint32_t)=nullptr;
std::atomic<int> original_lod{0};
std::atomic<uint64_t> high_lod_draws{0},actor_groundings{0};
std::atomic<int> actor_lift_milli{0};
uint32_t actor_return=0,actor_sp=0;float actor_shift[3]{};
uint32_t owner=0, owner_return=0, owner_sp=0, draw_return=0, draw_sp=0;
uint32_t tick=0, side=0, part=0, resource=0;
uint32_t draw_entry=0;
std::atomic<uint64_t> pose_count{0}, wheel_count{0}, pair_count{0};
std::atomic<uint64_t> tread_faces{0};
std::unordered_map<uint64_t, Node> nodes;
std::unordered_map<uint32_t,Node*> by_id;
uint32_t node_sequence=0;
std::array<Node,4096> world_nodes{};
std::unordered_map<uint64_t,uint32_t> world_poses;
uint32_t world_epoch=~0u,world_sequence=0;
extern "C" uint64_t s_frame_count;
uint64_t hash(uint64_t h, uint32_t v) { return (h^v)*1099511628211ull; }
bool wheel=false;
uint32_t scenery_return=0,scenery_sp=0;float scenery_fade=0;
struct SceneryView {float seen=-1000;uint32_t tick=~0u;bool warm=true;float rotation[9]{},origin[3]{};};
std::unordered_map<uint64_t,RageTrackFadeState> scenery_fades;
SceneryView scenery_views[2];

void quaternion(const double m[9],double q[4]) {
    const double trace=m[0]+m[4]+m[8];
    if(trace>0) { double s=std::sqrt(trace+1)*2; q[0]=s/4;q[1]=(m[7]-m[5])/s;q[2]=(m[2]-m[6])/s;q[3]=(m[3]-m[1])/s; }
    else {
        int i=m[4]>m[0]?1:0; if(m[8]>m[3*i+i])i=2;
        int j=(i+1)%3,k=(i+2)%3;
        double s=std::sqrt(std::max(0.0,1+m[3*i+i]-m[3*j+j]-m[3*k+k]))*2;
        q[0]=(m[3*k+j]-m[3*j+k])/s; q[i+1]=s/4;
        q[j+1]=(m[3*j+i]+m[3*i+j])/s; q[k+1]=(m[3*k+i]+m[3*i+k])/s;
    }
    double length=0; for(int i=0;i<4;++i)length+=q[i]*q[i];length=std::sqrt(length);
    for(int i=0;i<4;++i)q[i]/=length;
}
double determinant(const double m[9]) {
    return m[0]*(m[4]*m[8]-m[5]*m[7])-m[1]*(m[3]*m[8]-m[5]*m[6])+m[2]*(m[3]*m[7]-m[4]*m[6]);
}
void native_matrix(const RageSceneVertex& v,double t,float out[12]) {
    if(!v.history)t=1; t=std::max(0.0,std::min(t,1.0));
    double a[9],b[9];
    for(int i=0;i<9;++i){a[i]=v.previous.r[i]/4096.0;b[i]=v.current.r[i]/4096.0;}
    if(!v.history)std::memcpy(a,b,sizeof a);
    // Reflection belongs to the mesh basis, not the quaternion rotation.
    const bool mirror=determinant(b)<0;
    const bool same_basis=(determinant(a)<0)==mirror;
    if(mirror)for(int i=6;i<9;++i){a[i]=-a[i];b[i]=-b[i];}
    double scale_a[3],scale_b[3]; bool regular=true;
    for(int col=0;col<3;++col) {
        scale_a[col]=std::sqrt(a[col]*a[col]+a[3+col]*a[3+col]+a[6+col]*a[6+col]);
        scale_b[col]=std::sqrt(b[col]*b[col]+b[3+col]*b[3+col]+b[6+col]*b[6+col]);
        regular&=scale_a[col]>1e-8 && scale_b[col]>1e-8;
    }
    if(!regular) {
        for(int row=0;row<3;++row){for(int col=0;col<3;++col)out[4*row+col]=v.current.r[3*row+col]/4096.0f;out[4*row+3]=(float)v.current.t[row];}
        return;
    }
    for(int row=0;row<3;++row)for(int col=0;col<3;++col){a[3*row+col]/=scale_a[col];b[3*row+col]/=scale_b[col];}
    double qa[4],qb[4],q[4]; quaternion(a,qa);quaternion(b,qb);
    double dot=0;for(int i=0;i<4;++i)dot+=qa[i]*qb[i];
    if(dot<0){dot=-dot;for(int i=0;i<4;++i)qb[i]=-qb[i];}
    dot=std::min(1.0,dot);const double angle=std::acos(dot);
    double wa=1-t,wb=t;if(angle>1e-8){wa=std::sin((1-t)*angle)/std::sin(angle);wb=std::sin(t*angle)/std::sin(angle);}
    if(!same_basis){wa=0;wb=1;t=1;}
    for(int i=0;i<4;++i)q[i]=wa*qa[i]+wb*qb[i];
    const double w=q[0],x=q[1],y=q[2],z=q[3];
    const double r[9]={1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w),
        2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w),2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)};
    for(int row=0;row<3;++row) {
        for(int col=0;col<3;++col)out[4*row+col]=(float)(r[3*row+col]*(mirror&&row==2?-1:1)*(scale_a[col]+(scale_b[col]-scale_a[col])*t));
        const double ta=(v.flags&RAGE_SCENE_FLOAT_TRANSLATION)?v.translation[row]:v.previous.t[row];
        const double tb=(v.flags&RAGE_SCENE_FLOAT_TRANSLATION)?v.translation[3+row]:v.current.t[row];
        out[4*row+3]=(float)(ta+(tb-ta)*t);
    }
    if((v.flags&RAGE_SCENE_CAMERA) && t>0 && t<1) {
        // Interpolate camera position in world space. Interpolating -R*C
        // independently of R rotates the scene around the world's origin.
        for(int row=0;row<3;++row){double tr=0;
            for(int col=0;col<3;++col)tr-=out[4*row+col]*(v.camera[col]+(v.camera[3+col]-v.camera[col])*t);
            out[4*row+3]=(float)tr;
        }
    }
    // Exact authored endpoint retains legitimate fixed-point scale/shear.
    if(t==1||t==0)for(int row=0;row<3;++row)for(int col=0;col<3;++col)
        out[4*row+col]=(t==1?v.current:v.previous).r[3*row+col]/4096.0f;
}
}
extern "C" void rage_scene_set_original_lod(int on){original_lod.store(0,std::memory_order_relaxed);}
extern "C" int rage_scene_original_lod(){return original_lod.load(std::memory_order_relaxed);}
extern "C" uint64_t rage_scene_high_lod_draws(){return high_lod_draws.load(std::memory_order_relaxed);}
extern "C" uint64_t rage_scene_actor_groundings(){return actor_groundings.load(std::memory_order_relaxed);}
extern "C" int rage_scene_actor_lift_milli(){return actor_lift_milli.load(std::memory_order_relaxed);}
extern "C" void rage_scene_reset() {
    nodes.clear(); by_id.clear();scenery_fades.clear();scenery_views[0]={};scenery_views[1]={};scenery_return=0;scenery_fade=0;
    world_poses.clear(); world_epoch=~0u;
    owner=owner_return=owner_sp=draw_return=draw_sp=0;actor_return=actor_sp=0;std::memset(actor_shift,0,12);
    actor_groundings=0;actor_lift_milli=0;
    pose_count=wheel_count=pair_count=0;
}
extern "C" void rage_scene_enable(int on) { if(enabled==(on!=0))return;enabled=on!=0;rage_scene_reset(); }
extern "C" void rage_scene_dispatch(CPUState* cpu, uint32_t target) {
    if (!cpu) return;
    scene_read_word=cpu->read_word;scene_read_half=cpu->read_half;
    target = rage_region_code(target);
    // Music callbacks run only on the short pre-race screen; avoid another
    // function call at every guest dispatch throughout a live race.
    if(target==0x1cfb4 || rage_music_dispatch_pending)rage_music_dispatch(cpu,target);
    // SCES00650: ApplyMatrixLV has returned to the other-car draw caller.
    // These two stack inputs are subsequently used only for Manhattan LOD
    // distance. Preserve the independent behind-camera result at SP+0x150,
    // car world position, matrices, physics and speed-based wheel animation.
    if(target==0x1e098 && !original_lod.load(std::memory_order_relaxed) &&
       cpu->read_word && cpu->write_word &&
       cpu->read_word(rage_region_address(0x8001e098))==0x8fa20128 && cpu->read_word(rage_region_address(0x8001e0d8))==0x28620d00){
        const uint32_t sp=cpu->gpr[29]&0x1fffffffu;
        if(sp<0x200000u-0x154 && (cpu->gpr[29]>>24)==0x80){
            cpu->write_word(cpu->gpr[29]+0x128,0);cpu->write_word(cpu->gpr[29]+0x130,0);
            high_lod_draws.fetch_add(1,std::memory_order_relaxed);
        }
    }
    if (!enabled) return;
    const uint32_t sp=cpu->gpr[29], ra=rage_region_code(cpu->gpr[31]);
    if(scenery_return && (target==scenery_return || sp>scenery_sp)){scenery_return=0;scenery_fade=0;}
    if(actor_return && (target==actor_return || sp>actor_sp)){actor_return=0;std::memset(actor_shift,0,12);}
    // SCES00650's race-start walking actor uses the preprojected model family.
    // Ground only this caller's render receipt; leave simulation and GTE state
    // untouched, and retain ordinary world depth testing against nearer cars.
    if(target==0x296b4 && ra==0x3d6d0 && cpu->gpr[4]==0x1f800000 && cpu->read_half && cpu->read_word){
        actor_return=ra;actor_sp=sp;std::memset(actor_shift,0,12);
        const uint32_t table=cpu->read_word(0x1f800048),index=cpu->gpr[5];
        if(index>=40 && index<80 && (table&0x1fffffffu)<0x1ffc00){
            const uint32_t entry=table+12*index,pool=cpu->read_word(entry),count=cpu->read_word(entry+4);
            if(count>=3 && count<=512 && (pool&0x1fffffffu)<0x200000-8*count){
                double camera_r[9],model_r[9],tr[3],origin[3];
                for(int i=0;i<9;++i){camera_r[i]=int16_t(cpu->read_half(0x1f800028+2*i))/4096.;model_r[i]=int16_t(cpu->gte_ctrl[i/2]>>(16*(i&1)))/4096.;}
                for(int i=0;i<3;++i){tr[i]=int32_t(cpu->gte_ctrl[5+i]);origin[i]=int32_t(cpu->read_word(0x1f800008+4*i))*4.;}
                const double* m=camera_r;const double det=determinant(m);
                if(std::abs(det)>.5){
                    const double inv[9]={(m[4]*m[8]-m[5]*m[7])/det,(m[2]*m[7]-m[1]*m[8])/det,(m[1]*m[5]-m[2]*m[4])/det,
                        (m[5]*m[6]-m[3]*m[8])/det,(m[0]*m[8]-m[2]*m[6])/det,(m[2]*m[3]-m[0]*m[5])/det,
                        (m[3]*m[7]-m[4]*m[6])/det,(m[1]*m[6]-m[0]*m[7])/det,(m[0]*m[4]-m[1]*m[3])/det};
                    std::vector<std::array<double,3>> points;points.reserve(count);double bottom=-1e30;
                    for(uint32_t v=0;v<count;++v){double p[3],cam[3];std::array<double,3> world;
                        for(int i=0;i<3;++i)p[i]=int16_t(cpu->read_half(pool+8*v+2*i));
                        for(int i=0;i<3;++i){cam[i]=tr[i];for(int j=0;j<3;++j)cam[i]+=model_r[3*i+j]*p[j];}
                        for(int i=0;i<3;++i){world[i]=origin[i];for(int j=0;j<3;++j)world[i]+=inv[3*i+j]*cam[j];}
                        bottom=std::max(bottom,world[1]);points.push_back(world);
                    }
                    double lift=0;bool supported=false;
                    for(const auto& p:points)if(p[1]>=bottom-2){float height;
                        if(rage_track_ground_height(float(p[0]),float(p[2]),float(p[1]),&height)){
                            supported=true;lift=std::max(lift,p[1]-height+.25);
                        }}
                    if(supported && lift>0 && lift<=64){
                        for(int i=0;i<3;++i)actor_shift[i]=float(-camera_r[3*i+1]*lift);
                        actor_lift_milli.store(int(std::lround(lift*1000)),std::memory_order_relaxed);
                        actor_groundings.fetch_add(1,std::memory_order_relaxed);
                    }
                }
            }
        }
    }

    if (draw_return && (target==draw_return || sp>draw_sp)) draw_return=0;
    if (owner && (target==owner_return || sp>owner_sp)) { owner=0; draw_return=0; }
    // These SCES-00650 entries receive the actual player/AI car record.
    if (target==0x1dab0 || target==0x1dfc0) {
        owner=cpu->gpr[4]&0x1fffffffu; owner_return=ra; owner_sp=sp;
        draw_return=0;
    }
    // Retained static models have an independent sector visibility list.
    // Key their appearance by authored stream/entry and camera, never packet
    // address or the changing camera transform. Cars and the walking actor
    // use separate paths and must not fade with distant scenery.
    if(target==0x28dec && !owner && cpu->gpr[4]==0x1f800000 &&
       cpu->read_word && cpu->read_word(rage_region_address(0x801e42e4))==5){
        const uint32_t table=cpu->read_word(0x1f800050),entry=cpu->gpr[5];
        if((table&0x1fffffffu)<0x200000-16384 && entry<4096){
            const unsigned mirror=cpu->read_word(0x1f800068)!=0;
            auto& view=scenery_views[mirror];const float now=rage_track_fade_clock();
            const uint32_t frame=cpu->read_word(rage_region_address(0x8009e690));
            if(view.tick!=frame){
                view.warm=now-view.seen>0.25f;
                if(cpu->read_half){double agreement=0;
                    for(int i=0;i<9;++i){float r=int16_t(cpu->read_half(0x1f800028+2*i))/4096.f;agreement+=r*view.rotation[i];view.rotation[i]=r;}
                    if(agreement<2)view.warm=true;
                    for(int i=0;i<3;++i){float p=float(int32_t(cpu->read_word(0x1f800008+4*i))*4.0);
                        if(std::abs(p-view.origin[i])>16384)view.warm=true;view.origin[i]=p;}
                }
                view.seen=now;view.tick=frame;
            }
            uint64_t key=hash(hash(hash(1469598103934665603ull,table),entry),mirror);
            key=hash(key,cpu->read_word(table+4*entry));
            if(scenery_fades.size()>16384)scenery_fades.clear();
            auto& state=scenery_fades[key];
            rage_track_fade_visit(state,now,view.warm,rage_track_lod_fade()!=0);
            scenery_fade=state.start;scenery_return=ra;scenery_sp=sp;
        }
    }
    if (target!=0x28dec || !owner) return;
    tick=cpu->read_word(rage_region_address(0x8009e690));
    part=ra; side=0; wheel=ra==0x1df44 || ra==0x1e538;
    if (wheel) side=cpu->gpr[16]; // s0 is the wheel-side loop index at both sites.
    if (side>1) return;
    const uint32_t ctx=cpu->gpr[4], entry=cpu->gpr[5];
    draw_entry=entry;
    const uint32_t table=cpu->read_word(ctx+0x50);
    if ((table&0x1fffffffu)>=0x200000u || entry>4096) return;
    const uint32_t stream=cpu->read_word(table+4*entry);
    resource=uint32_t(hash(hash(1469598103934665603ull,table),stream));
    draw_return=ra; draw_sp=sp;
}
extern "C" int rage_scene_racing(void){
    return scene_read_word && scene_read_word(rage_region_address(0x801e42e4))==12;
}
extern "C" int rage_scene_full_width(void){
    if(!scene_read_word)return 0;
    const uint32_t state=scene_read_word(rage_region_address(0x801e42e4));
    return state==10 || state==12 || state==17 || state==30 || state==32; // Race loading, race, replay, intro and prologue.
}
extern "C" uint32_t rage_scene_pose(const int16_t r[9], const int32_t tr[3],
                                     float hx,float hy,float ox,float oy) {
    if (!enabled) return 0;
    if (!owner || !draw_return) {
        const uint32_t epoch=(uint32_t)s_frame_count;
        if(epoch!=world_epoch){world_poses.clear();world_epoch=epoch;}
        uint64_t key=1469598103934665603ull;
        for(int i=0;i<9;++i)key=hash(key,(uint16_t)r[i]);
        for(int i=0;i<3;++i)key=hash(key,(uint32_t)tr[i]);
        for(float v:{hx,hy,ox,oy,actor_return?actor_shift[0]:0,actor_return?actor_shift[1]:0,actor_return?actor_shift[2]:0}){uint32_t bits;std::memcpy(&bits,&v,4);key=hash(key,bits);}
        auto found=world_poses.find(key);if(found!=world_poses.end())return found->second;
        if(++world_sequence>=0x7fffffffu)world_sequence=1;
        const uint32_t id=0x80000000u|world_sequence;
        Node& n=world_nodes[world_sequence%world_nodes.size()]; n={};n.id=id;
        if(actor_return)std::memcpy(n.ground_shift,actor_shift,12);
        if(scenery_return && !actor_return && tr[2]>32768 && rage_track_lod_fade())n.fade_start=scenery_fade;
        std::memcpy(n.current.r,r,18);std::memcpy(n.current.t,tr,12);
        n.projection[0]=hx;n.projection[1]=hy;n.projection[2]=ox;n.projection[3]=oy;
        world_poses.emplace(key,id);pose_count.fetch_add(1,std::memory_order_relaxed);
        return id;
    }
    uint64_t key=1469598103934665603ull;
    for (uint32_t v:{owner,owner_return,part,side,resource}) key=hash(key,v);
    const float projection[4]={hx,hy,ox,oy};
    for (float v:projection) { uint32_t bits; std::memcpy(&bits,&v,4); key=hash(key,bits); }
    auto it=nodes.find(key);
    if (it==nodes.end()) {
        if (nodes.size()>=8192) return 0;
        Node n{}; if(++node_sequence>=0x7fffffffu)node_sequence=1;n.id=node_sequence;
        it=nodes.emplace(key,n).first; by_id.emplace(n.id,&it->second);
    }
    Node& n=it->second;
    if (!n.captured || n.tick!=tick) {
        n.history=n.captured && tick>n.tick && tick-n.tick<=4;
        n.previous=n.current;
        std::memcpy(n.current.r,r,18); std::memcpy(n.current.t,tr,12);
        if (n.history) for (int i=0;i<3;++i)
            if (std::abs(double(n.current.t[i])-n.previous.t[i])>16384) n.history=false;
        n.tick=tick; n.captured=true;
        n.shadow=!wheel && draw_entry%5==1;n.shadow_projection_valid=false;
        if(n.shadow && scene_read_half && scene_read_word){
            double camera[9],model[9];for(int i=0;i<9;++i){camera[i]=int16_t(scene_read_half(0x1f800028+2*i))/4096.;model[i]=r[i]/4096.;n.camera_rotation[i]=float(camera[i]);}
            auto inverse=[](const double m[9],float out[9]){double d=determinant(m);if(std::abs(d)<.5)return false;
                for(int row=0;row<3;++row)for(int col=0;col<3;++col)out[3*row+col]=float((m[3*((col+1)%3)+(row+1)%3]*m[3*((col+2)%3)+(row+2)%3]-m[3*((col+1)%3)+(row+2)%3]*m[3*((col+2)%3)+(row+1)%3])/d);return true;};
            
            n.shadow_projection_valid=inverse(camera,n.camera_inverse) && inverse(model,n.model_inverse);
            for(int i=0;i<3;++i)n.camera_origin[i]=float(int32_t(scene_read_word(0x1f800008+4*i))*4.);
        }

        std::memcpy(n.projection,projection,sizeof projection);
        pose_count.fetch_add(1,std::memory_order_relaxed);
        if(wheel)wheel_count.fetch_add(1,std::memory_order_relaxed);
        if(n.history)pair_count.fetch_add(1,std::memory_order_relaxed);
    } else if (std::memcmp(n.current.r,r,18) || std::memcmp(n.current.t,tr,12)) {
        // A reused draw key with two transforms is not one scene instance.
        n.history=false;
        // Keep the actual projected vertices for the ambiguous submission.
        // Using this node's first transform would move the second mesh to it.
        return 0;
    }
    return n.id;
}
extern "C" int rage_scene_vertex(uint32_t id,const float local[3],RageSceneVertex* out) {
    if (!id || !out) return 0;
    const Node* node=nullptr;
    if(id&0x80000000u) { node=&world_nodes[(id&0x7fffffffu)%world_nodes.size()];if(node->id!=id)return 0; }
    else {auto found=by_id.find(id);if(found==by_id.end())return 0;node=found->second;}
    const Node& n=*node;
    *out={}; out->node=id; out->history=n.history;out->fade_start=n.fade_start;
    out->previous=n.previous; out->current=n.current;
    if(n.shadow)out->flags|=RAGE_SCENE_DECAL|(255u<<16);
    std::memcpy(out->local,local,12); std::memcpy(out->projection,n.projection,16);
    if(n.shadow && n.shadow_projection_valid){
        double camera[3],world[3];for(int row=0;row<3;++row){camera[row]=n.current.t[row];for(int col=0;col<3;++col)camera[row]+=n.current.r[3*row+col]*local[col]/4096.;}
        for(int row=0;row<3;++row){world[row]=n.camera_origin[row];for(int col=0;col<3;++col)world[row]+=n.camera_inverse[3*row+col]*camera[col];}
        float height;if(rage_track_ground_height(float(world[0]),float(world[2]),float(world[1]),&height)){
            // PSX Y points down. Keep contact plates above the road in world
            // space; a tiny clip-depth bias vanishes near the camera.
            const double delta=(height-3.0)-world[1];
            if(std::abs(delta)<=128)for(int row=0;row<3;++row)for(int col=0;col<3;++col)out->local[row]+=float(n.model_inverse[3*row+col]*n.camera_rotation[3*col+1]*delta);
        }
    }

    if(n.ground_shift[0] || n.ground_shift[1] || n.ground_shift[2]){
        out->flags|=RAGE_SCENE_FLOAT_TRANSLATION;
        for(int i=0;i<3;++i)out->translation[i]=out->translation[3+i]=n.current.t[i]+n.ground_shift[i];
    }
    rage_scene_prepare(out,1);
    return 1;
}
extern "C" int rage_scene_tread_winding(CPUState* cpu){
    // Retain both sides of opaque tyre shells for native depth rendering.
    // Culling these thin strips at the guest camera leaves holes when an
    // intermediate camera reveals the other side. Spinning wheel faces use
    // their existing independent capture path.
    if(!enabled || !owner || !draw_return || wheel || !cpu || !cpu->read_word)return 0;
    const uint32_t handler=rage_region_code(cpu->gpr[10]);
    if(handler!=0x29064 && handler!=0x293f0 && handler!=0x29230 && handler!=0x295bc)return 0;
    const uint32_t face=cpu->gpr[8]&0x1fffffffu;
    if(face>0x1ffff0)return 0;
    const uint32_t colour=cpu->read_word(face+8);
    if(!rage_vehicle_tread_material(draw_entry,handler,colour))return 0;
    tread_faces.fetch_add(1,std::memory_order_relaxed);
    return cpu->gpr[3]?-1:1;
}
extern "C" uint64_t rage_scene_tread_faces(void){return tread_faces.load(std::memory_order_relaxed);}
extern "C" int rage_scene_evaluate(const RageSceneVertex* v,double phase,double camera[3]) {
    if (!v || !v->node) return 0;
    float pose[12]; native_matrix(*v,phase,pose);
    for (int i=0;i<3;++i) {
        camera[i]=(v->flags&RAGE_SCENE_CAMERA)?0:pose[4*i+3];
        const double t=v->history?std::max(0.0,std::min(phase,1.0)):1;
        for (int j=0;j<3;++j) camera[i]+=pose[4*i+j]*(v->local[j]-
            ((v->flags&RAGE_SCENE_CAMERA)?v->camera[j]+(v->camera[3+j]-v->camera[j])*t:0));
    }
    return 1;
}
extern "C" int rage_scene_camera_affine(const RageSceneVertex* v,double phase,float rotation[9],float translation[3]) {
    if(!v || !(v->flags&RAGE_SCENE_CAMERA))return 0;
    double m[9];for(int i=0;i<9;++i)m[i]=v->current.r[i]/4096.0;
    const double det=determinant(m);if(std::abs(det)<1e-8)return 0;
    const double inverse[9]={
        (m[4]*m[8]-m[5]*m[7])/det,(m[2]*m[7]-m[1]*m[8])/det,(m[1]*m[5]-m[2]*m[4])/det,
        (m[5]*m[6]-m[3]*m[8])/det,(m[0]*m[8]-m[2]*m[6])/det,(m[2]*m[3]-m[0]*m[5])/det,
        (m[3]*m[7]-m[4]*m[6])/det,(m[1]*m[6]-m[0]*m[7])/det,(m[0]*m[4]-m[1]*m[3])/det};
    float pose[12];native_matrix(*v,phase,pose);
    const double t=v->history?std::max(0.0,std::min(phase,1.0)):1;
    for(int row=0;row<3;++row){
        for(int col=0;col<3;++col){double a=0;
            for(int k=0;k<3;++k)a+=pose[4*row+k]*inverse[3*k+col];
            rotation[3*row+col]=(float)a;
        }
        double b=0;for(int k=0;k<3;++k)b+=pose[4*row+k]*(v->camera[3+k]-
            (v->camera[k]+(v->camera[3+k]-v->camera[k])*t));
        translation[row]=(float)b;
    }
    return 1;
}
extern "C" void rage_scene_prepare(RageSceneVertex* v,double phase) {
    if(v && v->node) {
        native_matrix(*v,phase,v->drawing);
        const double t=v->history?std::max(0.0,std::min(phase,1.0)):1;
        for(int i=0;i<3;++i)v->drawing_camera[i]=(float)(v->camera[i]+(v->camera[3+i]-v->camera[i])*t);
    }
}
extern "C" int rage_scene_camera_continuous(const RageSceneVertex* a,const RageSceneVertex* b) {
    if(!a || !b || !a->node || a->node!=b->node || !b->history ||
       !(a->flags&RAGE_SCENE_CAMERA) || !(b->flags&RAGE_SCENE_CAMERA))return 0;
    if(std::memcmp(a->current.r,b->previous.r,sizeof a->current.r))return 0;
    for(int i=0;i<2;++i)if(std::abs(a->projection[i]-b->projection[i])>0.01f)return 0;
    for(int i=0;i<3;++i)if(std::abs(a->camera[3+i]-b->camera[i])>0.01f)return 0;
    return 1;
}
extern "C" void rage_scene_stats(uint64_t* poses,uint64_t* wheels,uint64_t* paired) {
    if(poses)*poses=pose_count.load(std::memory_order_relaxed);
    if(wheels)*wheels=wheel_count.load(std::memory_order_relaxed);
    if(paired)*paired=pair_count.load(std::memory_order_relaxed);
}
