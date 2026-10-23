#include "rage_minimap.h"
#include "rage_minimap_math.h"
#include "rage_region.h"
#include "rage_free_run.h"
#include "cpu_state.h"
#include <array>
#include <mutex>
#include <cstring>

namespace {
constexpr int side=RAGE_MINIMAP_SIZE;
using Image=std::array<uint32_t,side*side>;
std::mutex mutex;
Image track{},image{};
RageMinimapFit fit;
bool ready=false,visible=false;
uint32_t last_tick=~0u;
void pixel(Image& dst,int x,int y,uint32_t rgb,float alpha){
    if(x<0||y<0||x>=side||y>=side||alpha<=0)return;
    uint32_t& p=dst[y*side+x];const float a=std::min(1.f,alpha),old=(p>>24)/255.f,out=a+old*(1-a);uint32_t value=uint32_t(std::lround(out*255))<<24;
    for(int shift=0;shift<24;shift+=8){const float c=(((rgb>>shift)&255)*a+((p>>shift)&255)*old*(1-a))/out;value|=uint32_t(std::lround(c))<<shift;}p=value;
}
void line(Image& dst,float ax,float ay,float bx,float by,float radius,uint32_t colour){
    const float dx=bx-ax,dy=by-ay,length=dx*dx+dy*dy;
    const int x0=std::max(0,int(std::floor(std::min(ax,bx)-radius-1))),x1=std::min(side-1,int(std::ceil(std::max(ax,bx)+radius+1)));
    const int y0=std::max(0,int(std::floor(std::min(ay,by)-radius-1))),y1=std::min(side-1,int(std::ceil(std::max(ay,by)+radius+1)));
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){float t=length>0?std::clamp(((x+.5f-ax)*dx+(y+.5f-ay)*dy)/length,0.f,1.f):0;
        float ex=x+.5f-ax-t*dx,ey=y+.5f-ay-t*dy;pixel(dst,x,y,colour,std::clamp(radius+.5f-std::sqrt(ex*ex+ey*ey),0.f,1.f));}
}
void point(float x,float z,float& px,float& py){fit.point(x,z,px,py);px+=side*.5f;py+=side*.5f;}
void arrow(float x,float y,float dx,float dy,float scale,uint32_t colour){
    const float rx=-dy,ry=dx;
    // Filled, outlined triangles use opaque cores and antialiased edges.
    for(int pass=0;pass<2;++pass){
        const float pad=pass?0.f:1.2f;
        const float tip=8*scale+pad,back=5*scale+pad,half=4.5f*scale+pad;
        const float vx[3]={x+dx*tip,x-dx*back+rx*half,x-dx*back-rx*half};
        const float vy[3]={y+dy*tip,y-dy*back+ry*half,y-dy*back-ry*half};
        const float sign=((vx[1]-vx[0])*(vy[2]-vy[0])-(vy[1]-vy[0])*(vx[2]-vx[0]))>0?1.f:-1.f;
        const int x0=std::max(0,int(std::floor(std::min({vx[0],vx[1],vx[2]})-1))),x1=std::min(side-1,int(std::ceil(std::max({vx[0],vx[1],vx[2]})+1)));
        const int y0=std::max(0,int(std::floor(std::min({vy[0],vy[1],vy[2]})-1))),y1=std::min(side-1,int(std::ceil(std::max({vy[0],vy[1],vy[2]})+1)));
        for(int py=y0;py<=y1;++py)for(int px=x0;px<=x1;++px){float coverage=1;
            for(int i=0;i<3;++i){const int j=(i+1)%3;const float ex=vx[j]-vx[i],ey=vy[j]-vy[i];
                const float distance=sign*(ex*(py+.5f-vy[i])-ey*(px+.5f-vx[i]))/std::sqrt(ex*ex+ey*ey);
                coverage=std::min(coverage,std::clamp(distance+.5f,0.f,1.f));}
            pixel(image,px,py,pass?colour:0x080b12,coverage);
        }
    }

}
}
extern "C" void rage_minimap_clear(void){std::lock_guard<std::mutex> lock(mutex);ready=visible=false;last_tick=~0u;}
extern "C" void rage_minimap_route(const float* xz,int count){
    rage_minimap_routes(xz,&count,1);
}
extern "C" void rage_minimap_routes(const float* xz,const int* counts,int roads){
    std::lock_guard<std::mutex> lock(mutex);ready=visible=false;last_tick=~0u;
    if(!counts || roads<1 || roads>4)return;
    int count=0;for(int r=0;r<roads;++r){if(counts[r]<2)return;count+=counts[r];}
    if(!fit.build(xz,count,side-32.f))return;
    track.fill(0);
    for(int pass=0;pass<2;++pass){int start=0;for(int r=0;r<roads;++r){for(int n=0;n<counts[r];++n){int i=start+n,next=start+(n+1)%counts[r];float ax,ay,bx,by;point(xz[2*i],xz[2*i+1],ax,ay);point(xz[2*next],xz[2*next+1],bx,by);
        line(track,ax,ay,bx,by,pass?1.f:2.3f,pass?0xffffff:0x080b12);}
        start+=counts[r];}}
    // Fade the completed road layer once, so overlapping segments stay equally translucent.
    for(auto& p:track)p=(p&0x00ffffffu)|((((p>>24)+1)/2)<<24);
    ready=true;
}
extern "C" void rage_minimap_capture(CPUState* cpu){
    if(!cpu||!cpu->read_word||!cpu->read_half)return;
    std::lock_guard<std::mutex> lock(mutex);
    visible=ready && cpu->read_word(rage_region_address(0x801e42e4))==12;
    if(!visible)return;
    const uint32_t tick=cpu->read_word(rage_region_address(0x8009e690));if(tick==last_tick)return;last_tick=tick;
    image=track;
    const uint32_t player=rage_region_address(0x8009e6d4);
    const uint32_t rivals=rage_region_address(0x801f1854);
    // Retail publishes a place only for the player. Rank opponents by the
    // same total lap progress used by the game's race-position calculation.
    auto progress=[&](uint32_t car){return int32_t(cpu->read_word(car+0x68)+cpu->read_word(car+0x6c));};
    uint32_t leader=player;int32_t best=progress(player);
    for(int i=0;!rage_free_run_active() && i<11;++i){const uint32_t car=rivals+0x19c*i;
        if(int16_t(cpu->read_half(car+0xac))==-1 || cpu->read_word(car+0xbc)!=1)continue;
        const int32_t distance=progress(car);if(distance>best){best=distance;leader=car;}
    }
    for(int i=0;!rage_free_run_active() && i<11;++i){const uint32_t car=rivals+0x19c*i;
        if(int16_t(cpu->read_half(car+0xac))==-1 || cpu->read_word(car+0xbc)!=1)continue;
        float x,y,dx,dy;point(float(int32_t(cpu->read_word(car)))*4,float(int32_t(cpu->read_word(car+8)))*4,x,y);
        rage_minimap_heading(int32_t(cpu->read_word(car+0x24)),dx,dy);
        const bool first=car==leader;
        arrow(x,y,dx,dy,.65f,first?0x35df64:0xff3d43);
    }
    float x,y,dx,dy;
    point(float(int32_t(cpu->read_word(player)))*4,float(int32_t(cpu->read_word(player+8)))*4,x,y);
    rage_minimap_heading(int32_t(cpu->read_word(player+0x24)),dx,dy);arrow(x,y,dx,dy,1.f,0x308dff);
}
extern "C" int rage_minimap_image(uint32_t* argb,int capacity){
    if(!argb || capacity<side*side)return 0;
    std::lock_guard<std::mutex> lock(mutex);if(!visible)return 0;
    std::memcpy(argb,image.data(),sizeof image);return 1;
}
