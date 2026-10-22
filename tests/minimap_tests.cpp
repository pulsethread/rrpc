#include "rage_minimap_math.h"
#include <cassert>
#include <limits>
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
}
