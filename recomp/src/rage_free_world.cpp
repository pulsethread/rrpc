#include "rage_free_world.h"
#include "rage_region.h"
#include "rage_free_run.h"
#include "mod_memory.h"
#include "rage_minimap.h"
#include "rage_road_math.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
struct Road {
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> prefix;
    uint32_t guest=0,count=0,length=0;
};
std::array<Road,4> roads;
unsigned current=0;
bool ready=false,installed=false;
uint32_t original_points=0,original_count=0,original_arcs=0,original_length=0;
uint64_t switches=0;
uint32_t junction_limits=0;
uint16_t half(const std::vector<uint8_t>& b,size_t p){return uint16_t(b.at(p)|(b.at(p+1)<<8));}
uint32_t word(const std::vector<uint8_t>& b,size_t p){return half(b,p)|(uint32_t(half(b,p+2))<<16);}
void put(std::vector<uint8_t>& b,size_t p,uint32_t v,unsigned size=4){for(unsigned i=0;i<size;++i)b.at(p+i)=uint8_t(v>>(8*i));}
uint32_t read(CPUState* c,uint32_t p){return c->read_word(rage_region_address(p));}
void write(CPUState* c,uint32_t p,uint32_t v){c->write_word(rage_region_address(p),v);}
struct Hit {unsigned road=0,index=0;double distance=1e30,score=1e30,t=0;};
RageRoadPoint road_point(const Road& road,unsigned i){const size_t p=4+24*(i%road.count);
    return {double(int32_t(word(road.bytes,p))),double(int32_t(word(road.bytes,p+4))),double(int16_t(half(road.bytes,p+8))),double(int16_t(half(road.bytes,p+10))),double(int16_t(half(road.bytes,p+12))),double(int16_t(half(road.bytes,p+14))),double(int16_t(half(road.bytes,p+16))),double(int16_t(half(road.bytes,p+18))),half(road.bytes,p+20),half(road.bytes,p+22)};
}
RageRoadContact contact(const Road& road,unsigned i,double x,double z){
    const auto a=road_point(road,i),b=road_point(road,i+1);double cx=0,cz=0;
    if(a.arc&3){const int index=int16_t(a.arc)>>4;const int64_t p=4+24*road.count+12*int64_t(index);
        if(p>=0 && p+8<=int64_t(road.bytes.size())){cx=int32_t(word(road.bytes,size_t(p)));cz=int32_t(word(road.bytes,size_t(p)+4));}}
    return rage_road_contact(a,b,cx,cz,x,z);
}
Hit closest(double x,double y,double z,bool all,unsigned preferred,double predict_x=0,double predict_z=0){
    Hit best;
    for(unsigned r=0;r<4;++r){if(!all && r!=preferred)continue;const auto& road=roads[r];
        for(unsigned i=0;i<road.count;++i){
            const auto h=contact(road,i,x,z),future=contact(road,i,x+predict_x*2,z+predict_z*2);
            const double height=std::abs(h.height-y);
            // Stay on the authored segment between its end planes, and select
            // an exit before the car hull reaches a closed ring's lane edge.
            const double score=h.end_distance*8+height*4+h.distance*.02+
                std::max(0.,40-h.clearance)*16+std::max(0.,40-future.clearance)*8+
                (height>192?10000:0)+(r==preferred?0:18);
            if(score<best.score)best={r,i,h.distance,score,std::clamp(h.t,0.,1.)};
        }
    }
    return best;
}
// Check each corner against the union of physical roads, never a bridge above
// or below the car. This opens junctions without opening the outside guardrail.
bool junction_footprint(CPUState* c,uint32_t car,double x,double y,double z,unsigned selected){
    constexpr double unit=6.283185307179586/4096.;
    const double angle=(c->read_word(car+0x24)&4095)*unit,cs=std::cos(angle),sn=std::sin(angle);
    const uint32_t hull=rage_region_address(0x8007dab0);
    bool crosses=false;
    for(unsigned corner=0;corner<4;++corner){
        const double hx=int16_t(c->read_half(hull+4*corner))*4.,hz=int16_t(c->read_half(hull+4*corner+2))*4.;
        const double px=x+cs*hx+sn*hz,pz=z-sn*hx+cs*hz;
        bool covered=false,on_selected=false;
        for(unsigned r=0;r<4;++r){
            for(unsigned i=0;i<roads[r].count;++i){const auto h=contact(roads[r],i,px,pz);
                if(h.end_distance<=2 && h.clearance>=0 && std::abs(h.height-y)<=64){covered=true;if(r==selected)on_selected=true;break;}
            }
        }
        if(!covered)return false;
        crosses|=!on_selected;
    }
    return crosses;
}
bool publish(CPUState* c){
    if(installed)return true;
    original_points=read(c,0x8009e688);original_count=read(c,0x8009e6a8);
    original_arcs=read(c,0x8019c7d0);original_length=read(c,0x801e40d8);
    if(!original_count || original_count>4096 || !original_length)return false;
    for(auto& road:roads){
        if(!road.guest)road.guest=psx_mod_memory_alloc(uint32_t(road.bytes.size()),16);
        if(!road.guest)return false;
        for(unsigned i=0;i<road.bytes.size();i+=4){uint32_t v=0;
            for(unsigned k=0;k<4 && i+k<road.bytes.size();++k)v|=uint32_t(road.bytes[i+k])<<(8*k);
            c->write_word(road.guest+i,v);
        }
    }
    installed=true;return true;
}
void select(CPUState* c,unsigned r){
    if(r!=current){++switches;current=r;}
    const auto& road=roads[r];
    write(c,0x8009e688,road.guest+4);write(c,0x8009e6a8,road.count);
    write(c,0x8019c7d0,road.guest+4+24*road.count);
    // Retain the original event/texture-section coordinate range. Free run
    // never completes a lap; the original event tables must remain bounded.
    write(c,0x801e40d8,original_length);
}
}

void rage_free_world_routes(const RageFreeRoutes& blobs,const std::array<std::array<float,3>,4>& shifts){
    ready=false;installed=false;switches=0;
    for(unsigned r=0;r<4;++r){
        auto& road=roads[r];road.bytes=blobs[r];road.prefix.clear();road.length=0;
        if(road.bytes.size()<28)return;
        road.count=word(road.bytes,0);if(road.count<16 || road.count>4096 || road.bytes.size()<4+24*road.count)return;
        const int dx=int(std::lround(shifts[r][0]/4)),dy=int(std::lround(shifts[r][1]/4)),dz=int(std::lround(shifts[r][2]/4));
        for(unsigned i=0;i<road.count;++i){const size_t p=4+24*i;
            put(road.bytes,p,word(road.bytes,p)+dx);put(road.bytes,p+4,word(road.bytes,p+4)+dz);
            put(road.bytes,p+8,uint16_t(int16_t(half(road.bytes,p+8))+dy),2);
            road.prefix.push_back(road.length);road.length+=uint16_t(half(road.bytes,p+22));
        }
        for(size_t p=4+24*road.count;p+12<=road.bytes.size();p+=12){
            put(road.bytes,p,word(road.bytes,p)+dx);put(road.bytes,p+4,word(road.bytes,p+4)+dz);
        }
    }
    ready=true;current=0;
}
int rage_free_world_ready(){return ready;}
unsigned rage_free_world_current_route(){return current;}
uint64_t rage_free_world_switches(){return switches;}
void rage_free_world_reset(){ready=false;installed=false;}
void rage_free_world_minimap(){
    if(!ready)return;
    std::vector<float> xz;int counts[4];
    for(unsigned r=0;r<4;++r){const auto& road=roads[r];counts[r]=int(road.count);
        for(unsigned i=0;i<road.count;++i){xz.push_back(float(int32_t(word(road.bytes,4+24*i)))*4);xz.push_back(float(int32_t(word(road.bytes,8+24*i)))*4);}}
    rage_minimap_routes(xz.data(),counts,4);
}

int rage_free_world_dispatch(CPUState* c,uint32_t target){
    const bool collision=target==0x31298;
    if(target!=0x2c168 && !collision)return 0;
    if(!ready || !rage_free_run_active() || read(c,0x801e42e4)!=12)return 0;
    const uint32_t car=c->gpr[4],physical=car&0x1fffffffu;
    if(physical<0x10000 || physical>0x200000-0x19c || !publish(c))return 0;
    const bool player=physical==(rage_region_address(0x8009e6d4)&0x1fffffffu);
    if(!player)return 0;
    const double x=int32_t(c->read_word(car)),z=int32_t(c->read_word(car+8));
    // The primary height word is reused as contact-work scratch. The rendered
    // height remains stable throughout physics and preserves bridge separation.
    double y=int32_t(c->read_word(car+0x60));
    if(std::abs(y)>32768){const auto& road=roads[current];const unsigned i=c->read_word(car+0x30)%road.count;y=int16_t(half(road.bytes,4+24*i+8));}
    const double mx=std::clamp(double(int32_t(c->read_word(car+16))),-160.,160.);
    const double mz=std::clamp(double(int32_t(c->read_word(car+24))),-160.,160.);
    const Hit hit=closest(x,y,z,player,current,mx,mz);
    if(hit.score>=1e29)return 0;
    if(player)select(c,hit.road);
    const auto& road=roads[current];
    {
        c->write_word(car+0x30,hit.index);
        const uint32_t progress=uint32_t(uint64_t(road.prefix[hit.index])*original_length/std::max(1u,road.length));
        c->write_word(car+0x68,progress);
    }
    if(collision){
        c->gpr[5]=hit.index;
        const uint32_t limits=c->gpr[6],lp=limits&0x1fffffffu;
        const auto h=contact(road,hit.index,x,z);
        if(lp>=0x10000 && lp<=0x200000-8 && h.clearance>=0 && h.clearance<128 && junction_footprint(c,car,x,y,z,current)){
            if(!junction_limits)junction_limits=psx_mod_memory_alloc(8,4);
            if(junction_limits){
                const auto a=road_point(road,hit.index),b=road_point(road,hit.index+1);
                const double t=std::clamp(h.along/std::max(1u,a.length),0.,1.);
                const double left=a.left*(1-t)+b.left*t,right=a.right*(1-t)+b.right*t;
                const int leftInset=int16_t(c->read_half(limits+2)),rightInset=int16_t(c->read_half(limits));
                c->write_half(junction_limits,uint16_t(std::min(rightInset,int(std::floor(right-h.lateral-2)))));
                c->write_half(junction_limits+2,uint16_t(std::max(leftInset,int(std::ceil(-left-h.lateral+2)))));
                c->write_word(junction_limits+4,c->read_word(limits+4));
                c->gpr[6]=junction_limits;
            }
        }
        // Let the original contact routine compute height, banking, surface
        // response and collisions with the remaining outside road boundaries.
        return 0;
    }
    c->pc=c->gpr[31];return 1;
}

bool rage_free_world_hide_later(CPUState* cpu){
    if(!ready || !rage_free_run_active() || !cpu)return false;
    const uint32_t car=rage_region_address(0x8009e6d4);
    auto lower=[](unsigned r,unsigned i){const auto& b=rage_free_tunnel_bands[r];return b[3]>b[0] && (i<=b[1] || i>=b[2]);};
    if(current<3)return lower(current,cpu->read_word(car+0x30)%roads[current].count);
    // The oval shares part of the lower road but has no upper section to hide.
    const double x=int32_t(cpu->read_word(car)),z=int32_t(cpu->read_word(car+8)),y=int32_t(cpu->read_word(car+0x60));
    for(unsigned r=0;r<3;++r){const auto h=closest(x,y,z,false,r);const auto c=contact(roads[r],h.index,x,z);
        if(c.clearance>=0 && std::abs(c.height-y)<128 && lower(r,h.index))return true;}
    return false;
}
