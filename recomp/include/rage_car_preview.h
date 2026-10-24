#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
namespace rage_preview {
struct Vec {double x,y,z;};
struct Face {Vec p[4];uint8_t uv[8]{};uint16_t page=0,clut=0;uint32_t colour=0;bool textured=false;int part=0;};
struct Car {std::vector<Face> faces;std::vector<uint8_t> image;bool loaded=false;};
inline uint16_t h(const std::vector<uint8_t>& b,size_t o){if(o+2>b.size())throw std::runtime_error("car bounds");return b[o]|(b[o+1]<<8);}
inline uint32_t w(const std::vector<uint8_t>& b,size_t o){return h(b,o)|(uint32_t(h(b,o+2))<<16);}
inline Car decode(const std::vector<uint8_t>& b){Car car;
 try {if(b.size()<40)return {};size_t base=w(b,32),image=w(b,36);if(base!=40 || image+32768>b.size())return {};
 car.image.assign(b.begin()+image,b.begin()+image+32768);size_t pool=base+w(b,base+4);unsigned count=w(b,base);if(count<4||count>256)return {};
 auto vertex=[&](unsigned i){size_t o=pool+i*8;return Vec{double(int16_t(h(b,o))),double(int16_t(h(b,o+2))),double(int16_t(h(b,o+4)))};};
 // Object translations are in quarter mesh units (SetGteObjectMatrix multiplies by four).
 // Assemble the same close-detail body, shell, rear axle and two front wheels.
 for(int part=0;part<5;++part){int model=part==4?2:part;size_t stream=base+w(b,base+12+model*4);unsigned limit=0;
  while(auto code=w(b,stream)){stream+=4;unsigned type=code&65535,n=code>>16;const unsigned strides[]={16,24,24,32};if(type>3||n>4096)throw std::runtime_error("car stream");
   for(unsigned j=0;j<n;++j){size_t f=stream+j*strides[type];Face face;face.part=part;for(int k=0;k<4;++k){face.p[k]=vertex(h(b,f+k*2));if(part==2 || part==4){double sign=part==2?1:-1;face.p[k].x=face.p[k].x*sign+sign*4*int16_t(h(b,0));face.p[k].y+=4*int16_t(h(b,2));face.p[k].z=face.p[k].z*sign+4*int16_t(h(b,4));}}
    face.textured=type==1||type==3;face.colour=type==0?w(b,f+8)&0xffffff:type==2?w(b,f+16)&0xffffff:0x808080;
    if(face.textured){size_t uv=f+(type==1?8:16);face.uv[0]=b.at(uv);face.uv[1]=b.at(uv+1);face.clut=h(b,uv+2);face.uv[2]=b.at(uv+4);face.uv[3]=b.at(uv+5);face.page=h(b,uv+6);for(int k=0;k<4;++k)face.uv[4+k]=b.at(uv+8+k);}
    car.faces.push_back(face);
   }stream+=n*strides[type];if(++limit>1000)throw std::runtime_error("car stream loop");
  }
 }
 }catch(...){return {};}
 return car;
}
}
