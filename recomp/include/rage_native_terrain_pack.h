#pragma once
#include "rage_native_geometry.h"
#include "rage_scene.h"
/* Direct counterpart of rage_native_pack_vertex for immutable terrain. */
static inline void rage_native_pack_terrain(float out[16],const RageSceneVertex* scene,
    double u,double v,double r,double g,double b,unsigned material,unsigned pose,
    unsigned raw,unsigned filter,int lod,int hardware) {
    memcpy(out,scene->local,12);out[3]=(float)u;out[4]=(float)v;
    out[5]=(float)r;out[6]=(float)g;out[7]=(float)b;
    out[8]=(float)material;out[9]=(float)pose;out[10]=(float)(raw+(((scene->flags>>16)&255u)*2u));
    unsigned attributes=(filter&15u)|(lod?128u:0u);
    if(scene->flags&RAGE_SCENE_REPEAT_U)attributes|=16;
    if(scene->flags&RAGE_SCENE_REPEAT_V)attributes|=32;
    if(scene->flags&RAGE_SCENE_WALL_CULL)attributes|=512|((scene->flags&RAGE_SCENE_MIRRORED_VIEW)?1024:0);
    if(hardware)attributes|=2048;
    out[11]=(float)attributes;memcpy(out+12,scene->region,16);
}
