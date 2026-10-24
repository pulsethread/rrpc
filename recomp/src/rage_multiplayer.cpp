#include "rage_multiplayer.h"
#include "rage_controller_model.h"
#include "rage_region.h"
#include "gpu.h"
#include "rage_split_select.h"
#include "rage_menu_draw.h"
#include "rage_menu_options_art.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace {
bool online=false,previous_online=false;
int slide_direction=1;
double slide=1;
uint32_t animation_tick=UINT32_MAX;
rage_controller::Mesh wifi_model(){
    using namespace rage_controller;
    Mesh m;
    // Three extruded curved bars and a raised dot form a real 3D Wi-Fi symbol.
    for(double radius:{24.,42.,60.})for(int i=0;i<24;++i){
        const double a=.25*3.141592653589793+i*3.141592653589793/48;
        const double b=a+3.141592653589793/48;
        Point p[4]={{(radius-4)*std::cos(a),-24+(radius-4)*std::sin(a),0},
                    {(radius+4)*std::cos(a),-24+(radius+4)*std::sin(a),0},
                    {(radius+4)*std::cos(b),-24+(radius+4)*std::sin(b),0},
                    {(radius-4)*std::cos(b),-24+(radius-4)*std::sin(b),0}};
        Point q[4];for(int k=0;k<4;++k)q[k]={p[k].x,p[k].y,9};
        quad(m,q[0],q[1],q[2],q[3],0xd0d3d5);
        quad(m,p[3],p[2],p[1],p[0],0x90989a);
        for(int k=0;k<4;++k){int j=(k+1)%4;quad(m,p[k],p[j],q[j],q[k],0xa4adaf);}
    }
    cylinder(m,0,-29,0,6,9,0xd0d3d5,20);
    return m;
}
}
extern "C" void rage_multiplayer_set_online(int value){online=previous_online=value!=0;slide=1;animation_tick=UINT32_MAX;}
extern "C" void rage_multiplayer_switch(int direction){
    if(slide<1)return;
    previous_online=online;online=!online;slide_direction=direction<0?-1:1;slide=0;animation_tick=UINT32_MAX;
}
extern "C" int rage_multiplayer_online(){return online;}
extern "C" void rage_multiplayer_draw(CPUState* c){
    using namespace rage_controller;
    if(!c || !c->read_word || !c->write_word)return;
    static const Mesh controllers=build(),wifi=wifi_model();
    const uint32_t tick=c->read_word(rage_region_address(0x8009e694));
    static uint32_t last_tick=UINT32_MAX;
    if(last_tick==tick)return;last_tick=tick;
    if(slide<1){
        if(animation_tick!=UINT32_MAX)slide=std::min(1.,slide+std::min(uint32_t(tick-animation_tick),4u)/22.);
        animation_tick=tick;
    }
    if(rage_split_active()){rage_split_draw(c);return;}
    const double eased=slide*slide*(3-2*slide);
    const double time=tick*.025;
    struct Draw {Point p[3];double depth;uint32_t colour;int layer;};std::vector<Draw> drawing;drawing.reserve(controllers.size()*2+wifi.size());
    for(int item=0;item<(slide<1?2:1);++item){
    const bool item_online=item==0?online:previous_online;
    const double offset=slide_direction*180*(item==0?1-eased:-eased);
    const Mesh& model=item_online?wifi:controllers;
    for(int pad=0;pad<(item_online?1:2);++pad){
        const double yaw=(pad?.32:-.27)+std::sin(time*.45+pad)*.13;
        const double roll=pad?-.25:.18,tilt=-.42;
        const double cy=std::cos(yaw),sy=std::sin(yaw),cz=std::cos(roll),sz=std::sin(roll),cx=std::cos(tilt),sx=std::sin(tilt);
        const double center_x=(item_online?200:pad?215:184)+offset,center_y=item_online?270:pad?326:224;
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
            // Clip the carousel to its showroom area, leaving the buttons clear.
            std::vector<Point> polygon(d.p,d.p+3);
            for(double edge:{124.,278.}){
                std::vector<Point> clipped;
                for(size_t k=0;k<polygon.size();++k){
                    auto a=polygon[k],b=polygon[(k+1)%polygon.size()];
                    const bool ai=edge==124.?a.x>=edge:a.x<=edge,bi=edge==124.?b.x>=edge:b.x<=edge;
                    if(ai)clipped.push_back(a);
                    if(ai!=bi){double t=(edge-a.x)/(b.x-a.x);clipped.push_back({edge,a.y+t*(b.y-a.y),a.z+t*(b.z-a.z)});}
                }
                polygon=std::move(clipped);
            }
            for(size_t k=1;k+1<polygon.size();++k){Draw clipped=d;clipped.p[0]=polygon[0];clipped.p[1]=polygon[k];clipped.p[2]=polygon[k+1];drawing.push_back(clipped);}
        }
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
    rage_menu_art::draw(c,rage_menu_art::ModeSelect,24,72,true);
    rage_menu_art::draw(c,rage_menu_art::Options,24,108,true);
    rage_menu_art::draw(c,rage_menu_art::Exit,24,144,true);
    const auto caption=online?rage_menu_art::OnlinePlay:rage_menu_art::SplitScreen;
    rage_menu_art::draw(c,caption,200-caption.w/2,392);
    rage_menu::arrow(c,124,270,false,tick);rage_menu::arrow(c,264,270,true,tick);rage_menu::environment(c);
}
