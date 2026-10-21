#include "rage_vehicle_shadow.h"
#include <cassert>
#include <cmath>
#include <limits>
static bool close(float a,float b,float e=.00001f){return std::abs(a-b)<e;}
int main(){
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
}
