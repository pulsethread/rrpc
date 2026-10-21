#include "rage_native_index.h"
#include "rage_native_terrain_pack.h"
#include <array>
#include <vector>
#include <stdexcept>
#include <cstdio>
static void check(bool value){if(!value)throw std::runtime_error("index reconstruction contract failed");}
int main(){
    try{
        {float mixed[9*16]={};uint32_t out[9]={},packed[9]={8,7,6,5,4,3,2,1,0};
            mixed[2*16+11]=8192;mixed[3*16+11]=8192;mixed[8*16+11]=8192;
            check(rage_native_caster_indices(mixed,9,nullptr,nullptr)==6);
            check(rage_native_caster_indices(mixed,9,nullptr,out)==6);
            const uint32_t expected[6]={0,1,2,6,7,8};check(!memcmp(out,expected,sizeof expected));
            check(rage_native_caster_indices(mixed,9,packed,out)==6);
            for(int i=0;i<6;++i)check(out[i]==packed[expected[i]]);
            check(rage_native_caster_indices(mixed,2,nullptr,out)==0);}
        /* The optimized admission path must produce exactly the former
         * 32-float staging/packing result across every authored bias and
         * main/mirror winding, window, filtering and raw-colour combination. */
        for(unsigned bias=0;bias<256;++bias)for(unsigned options=0;options<512;++options){
            RageSceneVertex scene{};scene.local[0]=-123.25f;scene.local[1]=456.5f;scene.local[2]=321;
            scene.region[0]=4;scene.region[1]=8;scene.region[2]=31;scene.region[3]=63;
            scene.flags=bias<<16;
            if(options&1)scene.flags|=RAGE_SCENE_REPEAT_U;
            if(options&2)scene.flags|=RAGE_SCENE_REPEAT_V;
            if(options&4)scene.flags|=RAGE_SCENE_WALL_CULL;
            if(options&8)scene.flags|=RAGE_SCENE_MIRRORED_VIEW;
            const unsigned raw=(options>>4)&1,lod=(options>>5)&1,hardware=(options>>6)&1,filter=(options>>7)&3;
            float source[32]={},expected[16],actual[16];
            source[2]=12.5f;source[3]=27.25f;source[4]=.25f;source[5]=.5f;source[6]=.75f;source[13]=(float)raw;
            memcpy(source+27,scene.local,12);memcpy(source+14,scene.region,16);source[31]=18;
            unsigned attributes=filter|(lod?16:0)|(bias<<10);
            if(options&1)attributes|=64;if(options&2)attributes|=128;
            if(options&4)attributes|=262144|((options&8)?524288:0);
            if(hardware)attributes|=1048576;
            source[26]=(float)attributes;rage_native_pack_vertex(expected,source,3);
            rage_native_pack_terrain(actual,&scene,12.5,27.25,.25,.5,.75,17,3,raw,filter,lod,hardware);
            check(!memcmp(expected,actual,sizeof expected));
        }
        RageNativeIndex pack{};
        check(!rage_native_index_build(&pack,nullptr,0));
        std::vector<std::array<float,16>> source(600);
        for(size_t i=0;i<source.size();++i)for(int k=0;k<16;++k)source[i][k]=float((i%17)*31+k);
        /* Shared positions with different UV, pose, material, colour, bounds,
         * signed zero or NaN payloads must never be welded by position alone. */
        for(int k=0;k<16;++k)source[20+k][k]+=0.25f;
        uint32_t bits=0x80000000u;memcpy(&source[40][0],&bits,4);
        bits=0x7fc00001u;memcpy(&source[41][0],&bits,4);
        bits=0x7fc00002u;memcpy(&source[42][0],&bits,4);
        check(rage_native_index_build(&pack,source[0].data(),source.size()));
        check(pack.count<source.size());
        for(size_t i=0;i<source.size();++i){
            check(pack.indices[i]<pack.count);
            check(!memcmp(source[i].data(),pack.vertices+16*pack.indices[i],64));
        }
        rage_native_index_release(&pack);
        /* All-unique and small geometry retain the cheaper expanded path. */
        for(size_t i=0;i<source.size();++i)source[i][0]=float(i);
        check(!rage_native_index_build(&pack,source[0].data(),source.size()));
        check(!pack.vertices && !pack.indices && !pack.count);
        check(!rage_native_index_build(&pack,source[0].data(),(1u<<24)+1));
        std::puts("131,072 direct terrain payloads, exact indexed reconstruction and expanded fallback passed.");
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
