#pragma once
#include "cpu_state.h"
#include "gpu.h"
#include <cstdint>
#include <cmath>
namespace rage_menu {
inline uint32_t xy(int x,int y){return uint16_t(x)|(uint32_t(uint16_t(y))<<16);}
inline void packet(CPUState* c,const uint32_t* data,unsigned count,int depth=0){
 uint32_t p=c->read_word(0x1f800000),ot=c->read_word(0x1f800004)+depth;
 auto valid=[](uint32_t a,unsigned n){a&=0x1fffffff;return a>=0x10000 && a<=0x200000-n;};
 if(!valid(p,(count+1)*4)||!valid(ot,4))return;
 c->write_word(p,(count<<24)|(c->read_word(ot)&0xffffff));for(unsigned i=0;i<count;++i)c->write_word(p+4+i*4,data[i]);
 c->write_word(ot,(c->read_word(ot)&0xff000000)|(p&0xffffff));c->write_word(0x1f800000,p+(count+1)*4);
}
inline void quad(CPUState* c,int x,int y,int w,int h,int u,int v,int uw,int vh,int clut,int page,uint32_t command=0x2d808080){
 const uint32_t p[]={command,xy(x,y),uint32_t(u|(v<<8)|(clut<<16)),xy(x+w,y),uint32_t((u+uw)|(v<<8)|(page<<16)),xy(x,y+h),uint32_t(u|((v+vh)<<8)),xy(x+w,y+h),uint32_t((u+uw)|((v+vh)<<8))};packet(c,p,9);
}
inline void environment(CPUState* c,int depth=0){const uint32_t words[]={0xe3000000u,0xe4077d3fu,0xe5000000u,0xe2000000u};packet(c,words,4,depth);}
inline void white_palette(){gpu_write_gp0(0xa0000000);gpu_write_gp0(608|(494u<<16));gpu_write_gp0(16|(1u<<16));gpu_write_gp0(0x80000000);for(int i=1;i<8;++i)gpu_write_gp0(0x80008000);gpu_write_gp0(0xa0000000);gpu_write_gp0(608|(495u<<16));gpu_write_gp0(16|(1u<<16));gpu_write_gp0(0x7fff0000);for(int i=1;i<8;++i)gpu_write_gp0(0x7fff7fff);}
inline void text(CPUState* c,int x,int y,const char* str,bool white=true){for(;*str;++str,x+=8){int glyph=*str>='A'&&*str<='Z'?*str-'7':*str>='0'&&*str<='9'?*str-'0':-1;if(glyph<0)continue;int u=glyph%32*8,v=glyph/32*16+24;quad(c,x,y,8,16,u,v,8,16,white?0x7be6:0x7ba6,0x3b,0x2d808080);}}
inline void arrow(CPUState* c,int x,int y,bool right,uint32_t tick){
 quad(c,x,y,12,28,right?0x58:0x48,0xb8,16,32,0x7f82,0x19);
 // Retail browse arrows: rsin(phase)/64 - 65, wrapping to an unsigned byte.
 const uint8_t green=uint8_t(int(4096*std::sin(((tick*0x60u)&0xfffu)*6.283185307179586/4096))/64-65);
 // OT prepends: the green plate must be queued after its transparent arrow.
 const uint32_t background[]={0x28000000u|(uint32_t(green)<<8),xy(x,y),xy(x+12,y),xy(x,y+28),xy(x+12,y+28)};
 packet(c,background,5);
}
inline void plate(CPUState* c,int x,int y,int w,uint32_t colour){uint32_t p[]={0x28000000|colour,xy(x,y),xy(x+w,y),xy(x,y+30),xy(x+w,y+30)};packet(c,p,5);}
}
