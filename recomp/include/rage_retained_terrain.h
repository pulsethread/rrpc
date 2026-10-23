/* Included on the GL consumer after terrain commands and material ownership
 * are defined. Commands retain their source payload; only this GPU-thread
 * cache borrows native buffers, checked by a never-reused allocation serial. */
enum { RAGE_RETAINED_TERRAIN_SLOTS=1024 };
typedef struct RageTerrainRun {int first,count,semi,decal,terrain;} RageTerrainRun;
typedef struct RageRetainedTerrain {
    uint64_t identity,serial,layout;
    uint32_t first,count,variant[6];
    RageNativeBuffer* mesh;
    unsigned runs,materials;
    uint16_t slots[128];
    RageTerrainRun run[128];
} RageRetainedTerrain;
static RageRetainedTerrain s_retained_terrain[RAGE_RETAINED_TERRAIN_SLOTS];
static _Atomic uint64_t s_retained_terrain_hits,s_retained_terrain_builds,s_retained_terrain_vertices;
void gl_renderer_retained_terrain_stats(uint64_t out[5]){
    out[0]=atomic_load(&s_retained_terrain_hits);
    out[1]=atomic_load(&s_retained_terrain_builds);
    out[2]=atomic_load(&s_retained_terrain_vertices);
    out[3]=atomic_load(&s_terrain_stream_bytes);out[4]=atomic_load(&s_terrain_merged_groups);
}
static int rage_retained_terrain_draw(const RageTerrainBatchHeader* h,const PSXTerrainTriangle* triangles){
    static int enabled=-1;
    if(enabled<0){const char* v=getenv("RAGE_RETAINED_TERRAIN");enabled=!v || v[0]!='0';}
    if(!enabled || !h->identity || !h->count || h->count>128 || !rage_compact_terrain() ||
       !s_scene_depth_owned || s_selected_bank_tex || s_hd_native_authority || !rage_native_init())return 0;
    flush_cpu_upload();flush_flat_batch();flush_tex_batch();
    const uint32_t variant[6]={h->pose.flags,h->source,h->unlit,(uint32_t)s_tex_filter,
        (uint32_t)s_tex_lod,(uint32_t)gl_renderer_accurate_blending()};
    const uint64_t key=(h->identity*0x9e3779b185ebca87ull)^((uint64_t)h->first*0xc2b2ae3d27d4eb4full)^rage_native_geometry_key(variant,sizeof variant);
    RageRetainedTerrain* entry=&s_retained_terrain[(key^(key>>32))&(RAGE_RETAINED_TERRAIN_SLOTS-1)];
    int valid=entry->identity==h->identity && entry->first==h->first && entry->count==h->count &&
        !memcmp(entry->variant,variant,sizeof variant) && entry->layout==s_material_layout_generation &&
        entry->mesh && entry->mesh->buffer && entry->mesh->serial==entry->serial;
    if(valid)for(unsigned i=0;i<entry->materials;++i){
        if(s_material_slots[entry->slots[i]].dirty){valid=0;break;}
    }
    if(!valid){
        RageRetainedTerrain next={0};
        next.identity=h->identity;next.first=h->first;next.count=h->count;
        memcpy(next.variant,variant,sizeof variant);
        float vertices[128*3*16];
        for(unsigned i=0;i<h->count;++i){
            const PSXTerrainTriangle* t=triangles+i;
            glb_set_texture_window(t->window);
            const uint16_t clut=t->clut[h->unlit!=0];
            const unsigned page=(t->page&0xfe00)?t->page:(t->page&0x1ff)|(h->source<<9);
            const unsigned depth=(page>>7)&3;
            const int material=rage_material_acquire((page&15)*64+(((page>>9)&127)*1024),
                ((page>>4)&1)*256,depth>2?2:depth,(clut&63)*16,(clut>>6)&511);
            if(material<0){glb_set_texture_window(h->restore_window);return 0;}
            unsigned m=0;while(m<next.materials && next.slots[m]!=material)++m;
            if(m==next.materials)next.slots[next.materials++]=(uint16_t)material;
            const int semi=((t->colour>>24)&2)?((t->page>>5)&3):-1;
            const int decal=((t->flags|h->pose.flags)&RAGE_SCENE_DECAL)!=0;
            const int terrain=((t->flags|h->pose.flags)&RAGE_SCENE_COVERAGE)?2:1;
            RageTerrainRun* run=next.runs?&next.run[next.runs-1]:NULL;
            if(!run || run->semi!=semi || run->decal!=decal || run->terrain!=terrain){
                run=&next.run[next.runs++];*run=(RageTerrainRun){(int)(3*i),0,semi,decal,terrain};
            }
            run->count+=3;
            RageSceneVertex scene=h->pose;scene.flags|=t->flags|RAGE_SCENE_GPU_CLIP;
            memcpy(scene.region,t->region,16);
            const double r=(t->colour&255)/255.0,g=((t->colour>>8)&255)/255.0,b=((t->colour>>16)&255)/255.0;
            for(unsigned k=0;k<3;++k){memcpy(scene.local,t->local+3*k,12);
                rage_native_pack_terrain(vertices+(i*3+k)*16,&scene,t->uv[2*k],t->uv[2*k+1],r,g,b,
                    material,0,(t->colour>>24)&1,s_tex_filter,s_tex_lod,s_material_hw_filter && ((page>>9)&127)>=2);
            }
        }
        glb_set_texture_window(h->restore_window);
        next.mesh=rage_native_buffer(vertices,(size_t)h->count*3*64);
        if(!next.mesh)return 0;
        if(next.mesh->indices && !next.mesh->expanded){
            p_glGenBuffers(1,&next.mesh->expanded);p_glBindBuffer(PSXGL_ARRAY_BUFFER,next.mesh->expanded);
            p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)h->count*3*64,vertices,GL_STATIC_DRAW);
            if(glGetError()!=GL_NO_ERROR){p_glDeleteBuffers(1,&next.mesh->expanded);next.mesh->expanded=0;}
            else atomic_fetch_add(&s_native_gpu_bytes,(uint64_t)h->count*3*64);
        }
        next.serial=next.mesh->serial;next.layout=s_material_layout_generation;
        *entry=next;
        atomic_fetch_add(&s_retained_terrain_builds,1);
    }else{
        entry->mesh->touched=++s_native_clock;
        for(unsigned i=0;i<entry->materials;++i)s_material_slots[entry->slots[i]].touched=++s_material_clock;
        atomic_fetch_add(&s_retained_terrain_hits,1);
        atomic_fetch_add(&s_retained_terrain_vertices,(uint64_t)h->count*3);
    }
    // Keep material-driven flushes and the original painter order. Geometry
    // is fixed; camera, mirror winding, fade time and viewport remain live.
    // A requested depth clear may drain the queue; do it before reserving
    // the records so an allocation failure still has a complete fallback.
    s_pz[0]=s_pz[1]=s_pz[2]=256;depth_before_tri();
    s_depth_tris+=h->count-1;
    if(s_native_queued+(int)entry->runs>1024)rage_native_scene_flush();
    // Reserve every queue record before submission. On allocation failure no
    // partial range has escaped, so the complete copied command can fall back.
    for(unsigned i=0;i<entry->runs;++i){RageNativeGroup* g=&s_native_queue[s_native_queued+i];
        if(g->palette_capacity<80){float* grown=(float*)realloc(g->palette,80);
            if(!grown)return 0;g->palette=grown;g->palette_capacity=80;++s_native_palette_grows;}
    }
    const int xs[3]={s_area_x1,s_area_x2,s_area_x1},ys[3]={s_area_y1,s_area_y1,s_area_y2};
    mark_prim_dirty(xs,ys,3,1);
    float camera[4]={h->pose.drawing_camera[0],h->pose.drawing_camera[1],h->pose.drawing_camera[2],1};
    if(h->pose.fade_start>0)camera[3]=2+h->pose.fade_start;
    ++entry->mesh->pins;
    for(unsigned i=0;i<entry->runs;++i){const RageTerrainRun* run=&entry->run[i];
        s_tb_scene_terrain=run->terrain;
        rage_native_scene_queue(entry->mesh,run->count,run->semi,run->decal,0,1,
            h->pose.drawing,h->pose.projection,camera,0,-1,&h->pose.lamps);
        s_native_queue[s_native_queued-1].first=run->first;
        s_native_queue[s_native_queued-1].retained=1;
    }
    --entry->mesh->pins;s_scene_prims_tex+=h->count;
    return 1;
}
