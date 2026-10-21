#include "rage_actor_clip.h"
#include <array>
#include <stdexcept>
#include <cstdio>
int main(){
    try{
        std::array<uint16_t,8> scratch={0x1234,0,0,319,239,0x5678,0x9abc,0xdef0};
        const auto original=scratch;unsigned writes=0;
        auto read=[&](uint32_t address){if(address<0x1f800078 || address>0x1f80007e)throw std::runtime_error("read outside clip rectangle");return scratch[1+(address-0x1f800078)/2];};
        auto write=[&](uint32_t address,uint16_t value){if(address<0x1f800078 || address>0x1f80007e)throw std::runtime_error("write outside clip rectangle");scratch[1+(address-0x1f800078)/2]=value;++writes;};
        RageActorClip scope;
        rage_actor_clip_begin(scope,read,write);rage_actor_clip_begin(scope,read,write);
        if(writes!=4 || int16_t(scratch[1])!=-1024 || scratch[4]!=1024)throw std::runtime_error("wrong clipping scope");
        rage_actor_clip_end(scope,write);rage_actor_clip_end(scope,write);
        if(writes!=8 || scratch!=original || scope.active)throw std::runtime_error("clip rectangle was not restored exactly");
        std::puts("Starter clip bounds retained and restored; neighbouring state preserved.");
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
