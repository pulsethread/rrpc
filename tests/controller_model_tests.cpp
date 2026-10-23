#include "rage_controller_model.h"
#include "rage_multiplayer.h"
#include "rage_region.h"
#include <cassert>
#include <cstring>
#include <iostream>
static std::vector<uint8_t> memory(0x200000,0xcd),scratch(1024);
static uint32_t read(uint32_t p){p&=0x1fffffffu;uint32_t v;std::memcpy(&v,p>=0x1f800000?scratch.data()+p-0x1f800000:memory.data()+p,4);return v;}
static void write(uint32_t p,uint32_t v){p&=0x1fffffffu;std::memcpy(p>=0x1f800000?scratch.data()+p-0x1f800000:memory.data()+p,&v,4);}
int main(){
    const auto mesh=rage_controller::build();assert(mesh.size()>500 && mesh.size()<2000);
    double lo[3]={1e9,1e9,1e9},hi[3]={-1e9,-1e9,-1e9};
    for(const auto& face:mesh){auto n=rage_controller::cross(rage_controller::sub(face.p[1],face.p[0]),rage_controller::sub(face.p[2],face.p[0]));
        assert(n.x*n.x+n.y*n.y+n.z*n.z>.00001);
        for(auto p:face.p){double v[3]={p.x,p.y,p.z};for(int k=0;k<3;++k){assert(std::isfinite(v[k]));lo[k]=std::min(lo[k],v[k]);hi[k]=std::max(hi[k],v[k]);}}}
    assert(hi[0]-lo[0]>130 && hi[1]-lo[1]>120 && hi[2]-lo[2]>24 && hi[2]<=17);
    size_t shoulders=0;
    for(const auto& face:mesh)if(face.colour==0x292724 || face.colour==0x1e1c1a){
        assert(face.layer<0);
        for(auto p:face.p)assert(p.z<=6 && p.y>=34);
        ++shoulders;
    }
    assert(shoulders==48); // Four complete shoulder buttons, behind the face.
    CPUState cpu{};cpu.read_word=read;cpu.write_word=write;
    for(unsigned region=0;region<3;++region){rage_active_region=region;
        std::vector<uint8_t> first;
        for(uint32_t tick:{0u,100u}){
            write(0x1f800000,0x80110000);write(rage_region_address(0x8019c900),0x80100000);
            write(0x1f800004,0x80100000);
            write(0x80100020,0xffffff);write(rage_region_address(0x8009e694),tick);
            rage_multiplayer_draw(&cpu);const auto end=read(0x1f800000);
            assert(end>0x80110000+400*20 && end<0x80110000+3000*20 && (end-0x80110000)%20==0);
            size_t visited=0;uint32_t p=read(0x80100020)&0xffffff;
            while(p!=0xffffff){assert(p>=0x110000 && p<(end&0x1fffffff));const auto head=read(p),colour=read(p+4);
                assert((head>>24)==4 && (colour>>24)==0x20);
                for(int k=0;k<3;++k){uint32_t xy=read(p+8+4*k);int x=int16_t(xy),y=int16_t(xy>>16);assert(x>=150 && x<=320 && y>=50 && y<=460);}
                p=head&0xffffff;assert(++visited<3000);
            }
            assert(visited==(end-0x80110000)/20);
            std::vector<uint8_t> bytes(memory.begin()+0x110000,memory.begin()+(end&0x1fffffff));
            if(!tick)first=bytes;else assert(first!=bytes); // Genuine moving 3D geometry.
        }
    }
    std::cout<<"Controller mesh topology, projection, animation and regional packet ownership passed\n";
}
