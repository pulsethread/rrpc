#include "rage_multiplayer.h"
#include "rage_controller_model.h"
#include "rage_region.h"
#include <vector>
#include <cmath>
#include <algorithm>

extern "C" void rage_multiplayer_draw(CPUState* c){
    using namespace rage_controller;
    if(!c || !c->read_word || !c->write_word)return;
    static const Mesh model=build();
    const uint32_t tick=c->read_word(rage_region_address(0x8009e694));
    static uint32_t last_tick=UINT32_MAX;
    if(last_tick==tick)return;last_tick=tick;
    const double time=tick*.025;
    struct Draw {Point p[3];double depth;uint32_t colour;int layer;};std::vector<Draw> drawing;drawing.reserve(model.size()*2);
    for(int pad=0;pad<2;++pad){
        const double yaw=(pad?.32:-.27)+std::sin(time*.45+pad)*.13;
        const double roll=pad?-.25:.18,tilt=-.42;
        const double cy=std::cos(yaw),sy=std::sin(yaw),cz=std::cos(roll),sz=std::sin(roll),cx=std::cos(tilt),sx=std::sin(tilt);
        const double center_x=pad?249:218,center_y=pad?326:224;
        for(const auto& face:model){Draw d{};d.layer=face.layer;Point transformed[3];
            for(int i=0;i<3;++i){auto p=face.p[i];const double y=p.y*cx-p.z*sx,z=p.y*sx+p.z*cx;
                const double x=p.x*cy+z*sy,zz=-p.x*sy+z*cy;
                transformed[i]={x*cz-y*sz,x*sz+y*cz,zz};}
            auto n=cross(sub(transformed[1],transformed[0]),sub(transformed[2],transformed[0]));
            const double length=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);if(length<.0001 || n.z<=0)continue;
            const double light=.48+.52*std::max(0.,(-n.x*.35+n.y*.45+n.z*.82)/length);
            for(int k=0;k<3;++k){const auto p=transformed[k];const double perspective=340/(340-p.z);
                d.p[k]={center_x+p.x*.66*perspective,center_y-p.y*1.32*perspective,p.z};d.depth+=p.z;}
            for(int channel=0;channel<3;++channel){const auto value=(face.colour>>(8*channel))&255;
                d.colour|=uint32_t(std::clamp(int(value*light),0,255))<<(8*channel);}
            drawing.push_back(d);
        }
    }
    // OT nodes prepend, so inserting near-to-far produces a far-to-near walk.
    std::stable_sort(drawing.begin(),drawing.end(),[](const Draw& a,const Draw& b){return a.layer==b.layer?a.depth>b.depth:a.layer>b.layer;});
    uint32_t cursor=c->read_word(0x1f800000),ot=c->read_word(0x1f800004)+32;
    auto ram=[](uint32_t p,size_t n){p&=0x1fffffffu;return p>=0x10000 && p<0x200000 && n<=0x200000-p;};
    if(!ram(cursor,drawing.size()*20) || !ram(ot,4))return;
    for(const auto& d:drawing){uint32_t words[5]={0x04000000u|(c->read_word(ot)&0xffffffu),0x20000000u|d.colour};
        for(int i=0;i<3;++i)words[i+2]=uint16_t(int(std::lround(d.p[i].x)))|(uint32_t(uint16_t(int(std::lround(d.p[i].y))))<<16);
        for(int i=0;i<5;++i)c->write_word(cursor+4*i,words[i]);
        c->write_word(ot,(c->read_word(ot)&0xff000000u)|(cursor&0xffffffu));cursor+=20;
    }
    c->write_word(0x1f800000,cursor);
}
