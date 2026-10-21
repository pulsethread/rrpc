#pragma once
#include <stdint.h>
struct RageActorClip {
    bool active=false;
    uint16_t bounds[4]{};
};
/* Retain all projected actor faces; actual clipping belongs to the native
 * camera. Only the model builder's temporary reject rectangle is changed. */
template<class Read,class Write>
inline void rage_actor_clip_begin(RageActorClip& clip,Read read,Write write){
    if(clip.active)return;
    for(unsigned i=0;i<4;++i){clip.bounds[i]=read(0x1f800078u+2*i);
        write(0x1f800078u+2*i,uint16_t(i<2?-1024:1024));}
    clip.active=true;
}
template<class Write>
inline void rage_actor_clip_end(RageActorClip& clip,Write write){
    if(!clip.active)return;
    for(unsigned i=0;i<4;++i)write(0x1f800078u+2*i,clip.bounds[i]);
    clip.active=false;
}
