#pragma once
#include <stdint.h>

/* A real ordering-table node, consumed only by its captured native view.
 * Keep guest DMA ordering instead of drawing immediately from the CPU hook. */
inline bool rage_native_terrain_publish(uint32_t cursor,uint32_t ot,
    uint32_t (*read)(uint32_t),void (*write)(uint32_t,uint32_t)){
    auto ram=[](uint32_t a,uint32_t n){
        const uint32_t segment=a>>24,p=a&0x1fffffffu;
        return !(a&3) && (segment==0 || segment==0x80 || segment==0xa0) &&
            p<0x200000u && n<=0x200000u-p;
    };
    if(!read || !write || !ram(cursor,20) || !ram(ot,704*4))return false;
    const uint32_t p=cursor&0x1fffffffu,t=ot&0x1fffffffu;
    if(p<t+704*4 && t<p+20)return false;
    const uint32_t slot=ot+575*4,link=read(slot);
    if((link&0xffffffu)==p)return false;
    write(cursor,0x04000000u|(link&0xffffffu));
    write(cursor+4,0x20000000u);
    for(unsigned i=8;i<20;i+=4)write(cursor+i,0);
    write(slot,(link&0xff000000u)|(cursor&0xffffffu));
    return true;
}
