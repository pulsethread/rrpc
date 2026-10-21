#pragma once
#include "rage_native_geometry.h"

/* Adapted from the reference renderer's exact geometry pack: all sixteen
 * attributes participate in identity, and triangle/index order is retained.
 * The caller keeps expanded rendering when packing cannot reduce storage. */
typedef struct RageNativeIndex {
    float* vertices;
    uint32_t* indices;
    size_t count;
} RageNativeIndex;
static inline void rage_native_index_release(RageNativeIndex* pack) {
    free(pack->vertices);free(pack->indices);memset(pack,0,sizeof(*pack));
}
static inline int rage_native_index_build(RageNativeIndex* pack,const float* vertices,size_t count) {
    memset(pack,0,sizeof(*pack));
    if(!vertices || !count || count>(1u<<24))return 0;
    size_t capacity=64;while(capacity<count*2)capacity*=2;
    uint32_t* slots=(uint32_t*)calloc(capacity,sizeof(uint32_t));
    pack->vertices=(float*)malloc(count*64);pack->indices=(uint32_t*)malloc(count*4);
    if(!slots || !pack->vertices || !pack->indices){free(slots);rage_native_index_release(pack);return 0;}
    for(size_t i=0;i<count;++i){
        const float* vertex=vertices+16*i;
        const uint64_t key=rage_native_geometry_key(vertex,64);
        size_t slot=(size_t)(key^(key>>32))&(capacity-1);
        while(slots[slot] && memcmp(vertex,pack->vertices+16*(slots[slot]-1),64))slot=(slot+1)&(capacity-1);
        if(!slots[slot]){memcpy(pack->vertices+16*pack->count,vertex,64);slots[slot]=(uint32_t)++pack->count;}
        pack->indices[i]=slots[slot]-1;
    }
    free(slots);
    if(pack->count*64+count*4>=count*64){rage_native_index_release(pack);return 0;}
    return 1;
}
