#include "rage_vehicle_shadow.h"
#include "rage_environment_light.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <initializer_list>
static bool close(float a,float b,float e=.00001f){return std::abs(a-b)<e;}
int main(){
    assert(rage_shadow_update_plan(0,0)==0);
    assert(rage_shadow_update_plan(0,1)==1);
    assert(rage_shadow_update_plan(1,0)==0);
    assert(rage_shadow_update_plan(1,1)==2);
    // Simulate a material upload splitting a frame into caster, receiver-only,
    // and additional caster batches. Only the first may clear accumulated depth.
    int valid=0,clears=0,appends=0;
    for(int casters:{1,0,1,0,0,1}){int plan=rage_shadow_update_plan(valid,casters);clears+=plan==1;appends+=plan==2;if(plan)valid=1;}
    assert(clears==1 && appends==2 && valid);

    {const float inverse[12]={1,0,0,100,0,1,0,200,0,0,1,300};
        float arbitrary[24]={},center[3];arbitrary[3]=90000;arbitrary[7]=-1000;arbitrary[11]=50000;
        rage_vehicle_shadow_center(inverse,arbitrary,2,-1,center);
        assert(center[0]==100 && center[1]==200 && center[2]==300);
        arbitrary[3]=-80000;arbitrary[11]=-60000;
        rage_vehicle_shadow_center(inverse,arbitrary,2,-1,center);
        assert(center[0]==100 && center[1]==200 && center[2]==300);
        arbitrary[15]=10;arbitrary[19]=20;arbitrary[23]=30;
        rage_vehicle_shadow_center(inverse,arbitrary,2,1,center);
        assert(center[0]==110 && center[1]==220 && center[2]==330);}
    assert(rage_shadow_resolution(0)==0);assert(rage_shadow_resolution(1)==2048);assert(rage_shadow_resolution(2)==4096);assert(rage_shadow_resolution(3)==8192);
    float center[3]={0,0,0},map[12];assert(rage_vehicle_shadow_map(center,map));
    assert(close(map[3],0)&&close(map[7],0)&&close(map[11],.5));
    // A point nearer the sun must win the shadow depth test, regardless of
    // camera or aspect ratio. There are four vertex units per game-world unit.
    const float sun[3]={-.34f,-1.f,-.42f};
    float toward=map[11];for(int k=0;k<3;++k)toward+=map[8+k]*sun[k]*400;
    assert(toward<map[11]);
    // Sub-texel lateral movement leaves the snapped light camera unchanged.
    float shifted[3];for(int k=0;k<3;++k)shifted[k]=map[k]*16384*.5f;
    float second[12];assert(rage_vehicle_shadow_map(shifted,second));
    for(int k=0;k<12;++k)assert(close(map[k],second[k]));
    center[0]=std::numeric_limits<float>::infinity();assert(!rage_vehicle_shadow_map(center,second));
    // Mirror cameras need a true inverse, including their reflected basis.
    const float rows[12]={0,0,1,0,0,1,0,0,1,0,0,0},origin[3]={100,200,300};float inverse[12];
    assert(rage_shadow_camera_inverse(rows,origin,inverse));
    const float point[3]={15,-20,40};float camera[3];
    for(int r=0;r<3;++r){camera[r]=0;for(int c=0;c<3;++c)camera[r]+=rows[4*r+c]*(point[c]-origin[c]);}
    for(int r=0;r<3;++r){float restored=inverse[4*r+3];for(int c=0;c<3;++c)restored+=inverse[4*r+c]*camera[c];assert(close(restored,point[r]));}
    const float singular[12]={};assert(!rage_shadow_camera_inverse(singular,origin,inverse));
    // The reference's sky colours drive a warm sun and longer shadows at
    // sunset. The sun stays fixed in the world when either camera rotates.
    RageEnvironmentLight day,sunset,night;
    const float dayTop[3]={.3f,.6f,1},dayEdge[3]={.8f,.9f,1},warmTop[3]={.12f,.08f,.2f},warmEdge[3]={.6f,.2f,.08f},black[3]={};
    rage_environment_light(dayTop,dayEdge,&day);rage_environment_light(warmTop,warmEdge,&sunset);rage_environment_light(black,black,&night);
    assert(day.ambient[0]>.5f);
    assert(day.direction[1]<sunset.direction[1] && sunset.direction[1]<0);
    assert(sunset.diffuse[0]>sunset.diffuse[1] && sunset.diffuse[1]>=sunset.diffuse[2]);
    assert(night.ambient[0]>0 && night.diffuse[2]>night.diffuse[0]);
    center[0]=center[1]=center[2]=0;
    assert(rage_directional_shadow_map(center,sunset.direction,map));
    float a[3]={-100,-100,-100},b[3]={100,100,100};assert(rage_shadow_bounds_visible(map,a,b,0));
    // An off-camera caster inside the light volume stays selected. A remote
    // sector outside it cannot darken the local course, including Free Run.
    a[0]=-2000;b[0]=-1000;assert(rage_shadow_bounds_visible(map,a,b,0));
    a[0]=1000000;b[0]=1000100;assert(!rage_shadow_bounds_visible(map,a,b,4096));
    // The distant cascade covers every direction out to either geometry
    // setting, including reverse views. It must retain texel-grid stability.
    for(float distance:{131072.f,196608.f}){float farMap[12];
        assert(rage_directional_shadow_map_extent(center,day.direction,distance*1.5f,farMap));
        for(int axis=0;axis<3;++axis)for(int sign:{-1,1}){
            float point[3]={};point[axis]=distance*sign;
            for(int r=0;r<3;++r){float v=farMap[4*r+3];for(int k=0;k<3;++k)v+=farMap[4*r+k]*point[k];
                assert(r==2?(v>.08f && v<.92f):std::abs(v)<.84f);}
        }
        float moved[3];for(int k=0;k<3;++k)moved[k]=farMap[k]*distance*1.5f*.25f;
        assert(rage_directional_shadow_map_extent(moved,day.direction,distance*1.5f,second));
        for(int k=0;k<12;++k)assert(close(farMap[k],second[k]));
    }
    const float zero[3]={};assert(!rage_directional_shadow_map(center,zero,map));
}
