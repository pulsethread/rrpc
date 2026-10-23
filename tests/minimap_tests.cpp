#include "rage_minimap_math.h"
#include <cassert>
#include <limits>
#include "rage_minimap.h"
#include "cpu_state.h"
#include <array>
#include <algorithm>
#include <cstring>
namespace {
std::array<uint8_t,2*1024*1024> ram{};
int free_run=0;
uint16_t rh(uint32_t p){p&=0x1fffff;return ram[p]|(uint16_t(ram[p+1])<<8);}
uint32_t rw(uint32_t p){return rh(p)|(uint32_t(rh(p+2))<<16);}
void ww(uint32_t p,uint32_t v){p&=0x1fffff;for(int i=0;i<4;++i)ram[p+i]=uint8_t(v>>(i*8));}
}
extern "C" int rage_free_run_active(){return free_run;}

int main(){
    RageMinimapFit fit;const float route[]={-100,-50,100,-50,100,50,-100,50};
    assert(fit.build(route,4,160));float x,y;fit.point(-100,-50,x,y);assert(std::abs(x+80)<.001f&&std::abs(y-40)<.001f);
    fit.point(100,50,x,y);assert(std::abs(x-80)<.001f&&std::abs(y+40)<.001f);
    // Coordinates use one uniform scale, preserving track shape and car alignment.
    const float shifted[]={9900,19950,10100,19950,10100,20050,9900,20050};
    assert(fit.build(shifted,4,160));fit.point(10000,20000,x,y);assert(std::abs(x)<.001f&&std::abs(y)<.001f);
    float dx,dy;rage_minimap_heading(0,dx,dy);assert(std::abs(dx)<.001f&&std::abs(dy+1)<.001f);
    rage_minimap_heading(1024,dx,dy);assert(std::abs(dx-1)<.001f&&std::abs(dy)<.001f);
    rage_minimap_heading(2048,dx,dy);assert(std::abs(dx)<.001f&&std::abs(dy-1)<.001f);
    rage_minimap_heading(3072,dx,dy);assert(std::abs(dx+1)<.001f&&std::abs(dy)<.001f);
    const float invalid[]={0,0,1,0,std::numeric_limits<float>::quiet_NaN(),1};assert(!fit.build(invalid,3,160));
    const float flat[]={1,1,1,1,1,1};assert(!fit.build(flat,3,160));assert(!fit.build(nullptr,0,160));
    CPUState cpu{};cpu.read_word=rw;cpu.read_half=rh;
    const uint32_t player=0x8009e6d4,rivals=0x801f1854,tick=0x8009e690;
    const float map[]={-400,-400,400,-400,400,400,-400,400};
    rage_minimap_route(map,4);ww(0x801e42e4,12);
    // Publish only the road first: its opacity must never exceed 50%.
    free_run=1;ww(player,100000);ww(tick,1);rage_minimap_capture(&cpu);
    std::array<uint32_t,RAGE_MINIMAP_SIZE*RAGE_MINIMAP_SIZE> pixels{};
    assert(rage_minimap_image(pixels.data(),int(pixels.size())));
    unsigned road=0;for(auto p:pixels){assert((p>>24)<=128);if(p>>24)++road;}assert(road>100);
    free_run=0;ww(player,0);ww(player+8,0);ww(player+0x160,2);
    for(int i=0;i<11;++i)ww(rivals+0x19c*i+0xac,0xffff);
    for(int i=0;i<2;++i){const auto car=rivals+0x19c*i;ww(car,i?50:uint32_t(-50));ww(car+0xac,0);ww(car+0xbc,1);ww(car+0x68,i?100:200);}
    auto capture=[&](uint32_t frame){ww(tick,frame);rage_minimap_capture(&cpu);assert(rage_minimap_image(pixels.data(),int(pixels.size())));};
    capture(2);
    auto count=[&](uint32_t colour){return std::count(pixels.begin(),pixels.end(),0xff000000u|colour);};
    assert(count(0x308dff)>10 && count(0x35df64)>5 && count(0xff3d43)>5);
    // The opponent triangle follows yaw rather than remaining a fixed dot.
    const auto north=pixels;ww(rivals+0x24,1024);capture(3);assert(pixels!=north);
    ww(player+0x68,300);capture(4);assert(count(0x35df64)==0 && count(0xff3d43)>10);
    rage_minimap_clear();assert(!rage_minimap_image(pixels.data(),int(pixels.size())));
}
