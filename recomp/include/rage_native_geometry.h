#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
typedef struct RageNativeViews {int count,rect[16][4];} RageNativeViews;
/* Only proven 3D draws enter a view. UI clipping changes never call this. */
static inline int rage_native_view_enter(RageNativeViews* views,const int rect[4]) {
    if(rect[2]<rect[0] || rect[3]<rect[1])return 0;
    for(int i=0;i<views->count;++i)if(!memcmp(views->rect[i],rect,4*sizeof(int)))return 0;
    const int clear=views->count!=0;
    if(views->count<16)memcpy(views->rect[views->count++],rect,4*sizeof(int));
    return clear;
}
/* Immutable mesh attributes. Screen/depth bookkeeping and camera matrices
 * must never enter the retained GPU resource identity. Pose indices are dense
 * within a batch so changing the transform palette does not change a mesh. */
static inline void rage_native_pack_vertex(float out[16],const float source[32],unsigned pose) {
    memcpy(out,source+27,12);memcpy(out+3,source+2,8);memcpy(out+5,source+4,12);
    out[8]=source[31]-1;out[9]=(float)pose;
    out[10]=source[13]+(float)((((unsigned)source[26]>>10)&255u)*2u);out[11]=(float)(((int)source[26]&15)|(((int)source[26]&64)?16:0)|(((int)source[26]&128)?32:0)|(((int)source[26]&256)?64:0)|(((int)source[26]&16)?128:0)|(((int)source[26]&512)?256:0));
    if((unsigned)source[26]&1048576u)out[11]+=2048;
    if((unsigned)source[26]&262144u)out[11]+=512;
    if((unsigned)source[26]&524288u)out[11]+=1024;
    memcpy(out+12,source+14,16);
}

typedef struct RageMaterialEdge {uint32_t key[13];unsigned triangle;} RageMaterialEdge;
static inline int rage_material_edge_compare(const void* a,const void* b){
    return memcmp(((const RageMaterialEdge*)a)->key,((const RageMaterialEdge*)b)->key,sizeof(((const RageMaterialEdge*)a)->key));
}
static inline unsigned rage_material_root(unsigned* parents,unsigned i){
    while(parents[i]!=i){parents[i]=parents[parents[i]];i=parents[i];}return i;
}
/* A fan is one painted material, not one atlas tile per wedge. Join only
 * complete edges with identical local position, UV, pose and material. UV
 * seams, separate instances, and course-sheet bounds remain independent. */
static inline unsigned rage_native_material_regions(float* vertices,size_t count){
    const unsigned triangles=(unsigned)(count/3);unsigned candidates=0;
    for(unsigned i=0;i<triangles;++i)if((int)vertices[i*48+11]&256)++candidates;
    if(candidates<2)return 0;
    RageMaterialEdge* edges=(RageMaterialEdge*)malloc((size_t)candidates*3*sizeof(RageMaterialEdge));
    unsigned* parents=(unsigned*)malloc((size_t)triangles*sizeof(unsigned));
    float* bounds=(float*)malloc((size_t)triangles*4*sizeof(float));
    if(!edges || !parents || !bounds){free(edges);free(parents);free(bounds);return 0;}
    unsigned n=0;
    for(unsigned i=0;i<triangles;++i){float* v=vertices+i*48;parents[i]=i;memcpy(bounds+4*i,v+12,16);
        if(!((int)v[11]&256))continue;
        for(unsigned edge=0;edge<3;++edge){const float* a=v+16*edge;const float* b=v+16*((edge+1)%3);
            if(!memcmp(a,b,3*sizeof(float)))continue;
            if(memcmp(a,b,5*sizeof(float))>0){const float* swap=a;a=b;b=swap;}
            RageMaterialEdge* e=&edges[n++];memcpy(e->key,a,20);memcpy(e->key+5,b,20);
            memcpy(e->key+10,v+8,8);memcpy(e->key+12,v+11,4);e->triangle=i;
        }
    }
    qsort(edges,n,sizeof(*edges),rage_material_edge_compare);
    for(unsigned i=1;i<n;++i)if(!rage_material_edge_compare(edges+i-1,edges+i)){
        unsigned a=rage_material_root(parents,edges[i-1].triangle),b=rage_material_root(parents,edges[i].triangle);
        if(a==b)continue;parents[b]=a;
        for(int k=0;k<4;++k){float* x=bounds+4*a+k;const float y=bounds[4*b+k];if(k<2?y<*x:y>*x)*x=y;}
    }
    unsigned changed=0;
    for(unsigned i=0;i<triangles;++i)if((int)vertices[i*48+11]&256){
        const float* region=bounds+4*rage_material_root(parents,i);
        if(memcmp(vertices+i*48+12,region,16)){++changed;for(unsigned k=0;k<3;++k)memcpy(vertices+i*48+16*k+12,region,16);}
    }
    free(edges);free(parents);free(bounds);return changed;
}
static inline uint64_t rage_native_geometry_key(const void* geometry,size_t bytes) {
    /* Four independent word lanes avoid a dependent multiply for every byte
     * of a multi-megabyte course batch on every interpolated frame. This is
     * only a lookup accelerator: cache hits still compare every source byte. */
    uint64_t lanes[4]={0x9e3779b185ebca87ull,0xc2b2ae3d27d4eb4full,
                       0x165667b19e3779f9ull,0x85ebca77c2b2ae63ull};
    const unsigned char* data=(const unsigned char*)geometry;
    size_t i=0;
    for(;i+32<=bytes;i+=32)for(int lane=0;lane<4;++lane){
        uint64_t word;memcpy(&word,data+i+8*lane,8);
        lanes[lane]=(lanes[lane]^word)*0x9e3779b185ebca87ull;
        lanes[lane]=(lanes[lane]<<27)|(lanes[lane]>>37);
    }
    uint64_t key=bytes;
    for(int lane=0;lane<4;++lane)key=(key^lanes[lane])*0xc2b2ae3d27d4eb4full;
    for(;i<bytes;++i)key=(key^data[i])*1099511628211ull;
    key^=key>>33;key*=0xff51afd7ed558ccdull;key^=key>>33;
    return key;
}

/* Proven scene surfaces always test depth. Blended surfaces test without
 * establishing an opaque occluder; interface polygons remain outside depth. */
static inline int rage_native_depth_mode(int enabled,int proven,int semi) {
    return !enabled || !proven?0:semi<0?1:3;
}

static inline int rage_native_texel_address(int coordinate,int low,int high,int repeat) {
    return repeat?(coordinate&255):coordinate<low?low:coordinate>high?high:coordinate;
}
