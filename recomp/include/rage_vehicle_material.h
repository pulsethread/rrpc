#pragma once
#include <stdint.h>
/* Opaque flat body and tyre-shell materials in a body entry. The adjacent
 * shadow entry and rotating wheel entries retain their original winding. */
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
