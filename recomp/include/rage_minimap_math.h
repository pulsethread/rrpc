#pragma once
#include <cmath>
#include <algorithm>
struct RageMinimapFit {
    float x=0,z=0,scale=0;
    bool build(const float* points,int count,float extent){
        if(!points || count<3 || count>4096 || extent<=0)return false;
        float lo_x=points[0],hi_x=lo_x,lo_z=points[1],hi_z=lo_z;
        for(int i=0;i<count;++i){float a=points[2*i],b=points[2*i+1];if(!std::isfinite(a)||!std::isfinite(b))return false;
            lo_x=std::min(lo_x,a);hi_x=std::max(hi_x,a);lo_z=std::min(lo_z,b);hi_z=std::max(hi_z,b);}
        const float span=std::max(hi_x-lo_x,hi_z-lo_z);if(span<1)return false;
        x=(lo_x+hi_x)*.5f;z=(lo_z+hi_z)*.5f;scale=extent/span;return true;
    }
    void point(float wx,float wz,float& px,float& py)const{px=(wx-x)*scale;py=-(wz-z)*scale;}
};
static inline void rage_minimap_heading(int yaw,float& dx,float& dy){
    const float angle=(yaw&4095)*6.28318530718f/4096.f;
    dx=std::sin(angle);dy=-std::cos(angle);
}
