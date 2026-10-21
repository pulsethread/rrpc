#pragma once
#include <stdint.h>
inline bool rage_vehicle_shadow_entry(bool player,bool front,uint32_t entry){
    return !front && (player?entry==1:entry%5==1);
}
/* Opaque flat body and tyre-shell materials in a body entry. The adjacent
 * shadow entry retains its original winding. Rotating wheels are handled
 * separately below, without treating their stream as a body material. */
inline bool rage_vehicle_tread_material(uint32_t entry,uint32_t handler,uint32_t colour){
    if(entry%5!=0 && entry%5!=4)return false;
    // Both GT4 handlers emit opaque 0x3c packets; +8 is a normal index,
    // not a colour, for these streams.
    if(handler==0x29230 || handler==0x295bc)return true;
    if(handler!=0x29064 && handler!=0x293f0)return false;
    // All opaque flat body panels must remain an occluder when the host
    // camera moves between guest frames, including black front tyre strips.
    return (colour>>24)==0x28;
}

/* Native wheels are complete meshes, as in reference Render World. Integer
 * screen-space winding fluctuates for small or edge-on GT4 caps as they spin.
 * Keep opaque wheel faces and let the native body/wheel depth test occlude
 * them. Player wheel/blur banks use pairs after entries 0/1; rival banks use
 * five-entry assemblies. Never admit the flat shadow or a rival far body. */
inline bool rage_vehicle_wheel_material(bool player,bool front,uint32_t entry,uint32_t handler,uint32_t colour){
    const bool stream=front || (player?entry>=2:(entry%5==2 || entry%5==3));
    if(!stream)return false;
    if(handler==0x29230 || handler==0x295bc)return true;
    return (handler==0x29064 || handler==0x293f0) && (colour>>24)==0x28;
}
