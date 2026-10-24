#pragma once
#include <math.h>
#include <string.h>
/* Adapted from the reference RenderDirectionalLightFromSky. Native world
 * directions use PSX Y-down/Z-forward; colours are the displayed sky bands. */
typedef struct RageEnvironmentLight {float direction[4],ambient[4],diffuse[4];} RageEnvironmentLight;
static inline float rage_light_clamp(float v,float lo,float hi){return v<lo?lo:v>hi?hi:v;}
static inline void rage_environment_light(const float top[3],const float horizon[3],RageEnvironmentLight* out){
    const float sky=top[0]*.2126f+top[1]*.7152f+top[2]*.0722f;
    const float edge=horizon[0]*.2126f+horizon[1]*.7152f+horizon[2]*.0722f;
    const float day=rage_light_clamp(fmaxf(sky,edge)*1.35f,.08f,1.f);
    const float elevation=.28f+day*.58f,horizontal=sqrtf(fmaxf(0,1-elevation*elevation));
    const float maximum=fmaxf(horizon[0],fmaxf(horizon[1],horizon[2]));
    const float fallback[3]={.72f,.78f,1.f};memset(out,0,sizeof(*out));
    out->direction[0]=-horizontal*.63f;out->direction[1]=-elevation;out->direction[2]=-horizontal*.7766f;
    for(int k=0;k<3;++k){const float tint=maximum>.001f?rage_light_clamp(horizon[k]/maximum,.38f,1):fallback[k];
        out->ambient[k]=(.34f+day*.30f)*(.65f+tint*.35f);out->diffuse[k]=(.65f+day*.45f)*tint;}
}
