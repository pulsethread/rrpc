#pragma once
#include <algorithm>
#include <cmath>
struct RageRoadPoint {double x,z,y,angle,pitch,camber,left,right;unsigned arc,length;};
struct RageRoadContact {double along,t,lateral,height,distance,end_distance,clearance;};
inline double rage_angle_delta(double a,double b){return std::remainder(b-a,6.283185307179586);}
inline RageRoadContact rage_road_contact(const RageRoadPoint& a,const RageRoadPoint& b,double cx,double cz,double x,double z){
    constexpr double unit=6.283185307179586/4096.;
    double heading=a.angle*unit,along=(x-a.x)*std::cos(heading)+(z-a.z)*std::sin(heading);
    double t=along/std::max(1u,a.length),lateral=-(x-a.x)*std::sin(heading)+(z-a.z)*std::cos(heading);
    if(a.arc&3){
        const double start=std::atan2(a.z-cz,a.x-cx),end=std::atan2(b.z-cz,b.x-cx);
        const double span=rage_angle_delta(start,end);
        if(std::abs(span)>1e-7){
            t=rage_angle_delta(start,std::atan2(z-cz,x-cx))/span;
            const double fraction=std::clamp(t,0.,1.);
            const double radius=std::hypot(a.x-cx,a.z-cz)*(1-fraction)+std::hypot(b.x-cx,b.z-cz)*fraction;
            lateral=std::hypot(x-cx,z-cz)-radius;if((a.arc&3)==2)lateral=-lateral;
            heading+=rage_angle_delta(heading,b.angle*unit)*fraction;
            along=(x-a.x)*std::cos(heading)+(z-a.z)*std::sin(heading);
        }
    }
    const double fraction=std::clamp(along/std::max(1u,a.length),0.,1.);
    const double y=a.y*(1-fraction)+b.y*fraction+(a.camber*(1-fraction)+b.camber*fraction)*lateral/128.;
    const double left=a.left*(1-fraction)+b.left*fraction,right=a.right*(1-fraction)+b.right*fraction;
    const double outside=std::max({0.,-t,t-1})*a.length;
    const double distance=outside>0?std::min(std::hypot(x-a.x,z-a.z),std::hypot(x-b.x,z-b.z)):std::abs(lateral);
    return {along,t,lateral,y,distance,outside,std::min(lateral+left,right-lateral)};
}
