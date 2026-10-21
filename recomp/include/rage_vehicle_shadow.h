#pragma once
#include <math.h>
#include <string.h>
/* Adapted from the reference render_shadow.c. Coordinates here are PSX
 * vertex units (four per game-world unit), with Y down and Z forward. */
static inline float rage_shadow_dot(const float a[3],const float b[3]) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
/* A palette can begin with scenery or any rival. Only an explicitly tagged
 * player part may replace the reference's camera-centred fallback. */
static inline void rage_vehicle_shadow_center(const float inverse[12],const float* rows,
    int poses,int player,float center[3]) {
    for(int r=0;r<3;++r){center[r]=inverse[4*r+3];if(player>=0 && player<poses)
        for(int c=0;c<3;++c)center[r]+=inverse[4*r+c]*rows[12*player+4*c+3];}
}
static inline int rage_shadow_camera_inverse(const float rows[12],const float origin[3],float out[12]) {
    const float a=rows[0],b=rows[1],c=rows[2],d=rows[4],e=rows[5],f=rows[6],g=rows[8],h=rows[9],i=rows[10];
    const float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!isfinite(det)||fabsf(det)<.5f)return 0;
    const float r[9]={(e*i-f*h)/det,(c*h-b*i)/det,(b*f-c*e)/det,
        (f*g-d*i)/det,(a*i-c*g)/det,(c*d-a*f)/det,
        (d*h-e*g)/det,(b*g-a*h)/det,(a*e-b*d)/det};
    for(int y=0;y<3;++y){for(int x=0;x<3;++x)out[4*y+x]=r[3*y+x];out[4*y+3]=origin[y];}
    return 1;
}
static inline int rage_vehicle_shadow_map(const float center[3],float out[12]) {
    const float extent=16384.f,resolution=4096.f;
    const float raw[3]={-.34f,-1.f,-.42f};float light[3],right[3],vertical[3],snapped[3];
    const float length=sqrtf(rage_shadow_dot(raw,raw));
    for(int k=0;k<3;++k){if(!isfinite(center[k]))return 0;light[k]=raw[k]/length;snapped[k]=center[k];}
    right[0]=-light[2];right[1]=0;right[2]=light[0];
    const float rl=sqrtf(rage_shadow_dot(right,right));for(int k=0;k<3;++k)right[k]/=rl;
    vertical[0]=light[1]*right[2]-light[2]*right[1];
    vertical[1]=light[2]*right[0]-light[0]*right[2];
    vertical[2]=light[0]*right[1]-light[1]*right[0];
    const float texel=2*extent/resolution;
    const float dr=roundf(rage_shadow_dot(center,right)/texel)*texel-rage_shadow_dot(center,right);
    const float dv=roundf(rage_shadow_dot(center,vertical)/texel)*texel-rage_shadow_dot(center,vertical);
    for(int k=0;k<3;++k)snapped[k]+=right[k]*dr+vertical[k]*dv;
    for(int k=0;k<3;++k){out[k]=right[k]/extent;out[4+k]=vertical[k]/extent;out[8+k]=-light[k]/(4*extent);}
    out[3]=-rage_shadow_dot(right,snapped)/extent;out[7]=-rage_shadow_dot(vertical,snapped)/extent;
    out[11]=.5f+rage_shadow_dot(light,snapped)/(4*extent);
    return 1;
}
