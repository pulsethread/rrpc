/* Native scene submission is held until the next non-scene draw. That lets
 * the light see all captured bodies and wheels before terrain is shaded.
 * UI, copies and texture replacement drain the queue in painter order. */
#include "rage_vehicle_shadow.h"
typedef struct RageNativeState {
    int area[4],pass[5],view[4],base,terrain;
    GLuint wide;
} RageNativeState;
typedef struct RageNativeGroup {
    RageNativeBuffer* mesh;
    float* palette;
    size_t palette_capacity;
    int poses,count,semi,decal,background,caster,view_valid,player_pose,first,retained,streamed,stream_first,shadow_only;
    float inverse_view[12];
    RageNativeState state;
    RageCarLightReceipt lamps[RAGE_SCENE_PALETTE];
} RageNativeGroup;
static RageNativeGroup s_native_queue[4096];
// Diagnostic-only pressure control: production keeps the full4096 entries.
static int rage_native_queue_limit(void){static int limit=0;if(!limit){const char* value=getenv("RAGE_NATIVE_QUEUE_LIMIT");limit=value?atoi(value):4096;if(limit<128)limit=128;if(limit>4096)limit=4096;}return limit;}
static int s_native_queued,s_native_draining;
static GLuint s_terrain_stream_buffer,s_terrain_stream_vao;
static _Atomic uint64_t s_terrain_stream_bytes,s_terrain_merged_groups;
static _Atomic uint64_t s_terrain_stream_reuses,s_terrain_stream_saved_bytes;
static RageNativeStreamKey s_terrain_stream_keys[4096],s_terrain_stream_next[4096];
static size_t s_terrain_stream_key_count;
/* Coalesce only adjacent ranges with exactly the same live draw state. GPU
 * copies gather resident geometry; no vertex expansion, hash or CPU upload is
 * performed here. Orphaning the destination preserves prior submitted draws. */
static int rage_terrain_groups_equal(const RageNativeGroup* a,const RageNativeGroup* b){
    return a->streamed && b->streamed && a->poses==b->poses && a->semi==b->semi &&
        a->decal==b->decal && a->background==b->background && a->view_valid==b->view_valid && a->shadow_only==b->shadow_only &&
        !memcmp(&a->state,&b->state,sizeof a->state) &&
        !memcmp(a->palette,b->palette,(size_t)a->poses*80) &&
        !memcmp(a->inverse_view,b->inverse_view,sizeof a->inverse_view) &&
        !memcmp(a->lamps,b->lamps,(size_t)a->poses*sizeof(*a->lamps));
}
static void rage_terrain_stream_prepare(void){
    size_t count=0,key_count=0;
    for(int i=0;i<s_native_queued;++i)if(s_native_queue[i].retained &&
        (!s_native_queue[i].mesh->indices || s_native_queue[i].mesh->expanded)){
        const RageNativeGroup* g=&s_native_queue[i];count+=g->count;
        s_terrain_stream_next[key_count++]=(RageNativeStreamKey){g->mesh->serial,g->mesh->expanded?g->mesh->expanded:g->mesh->buffer,(uint32_t)g->first,(uint32_t)g->count,0};
    }
    if(!count || count>(32u<<20)/64 || !p_glCopyBufferSubData)return;
    if(!s_terrain_stream_buffer){
        p_glGenBuffers(1,&s_terrain_stream_buffer);p_glGenVertexArrays(1,&s_terrain_stream_vao);
        p_glBindVertexArray(s_terrain_stream_vao);p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_terrain_stream_buffer);
        const int sizes[5]={3,2,3,4,4},offsets[5]={0,3,5,8,12};
        for(int i=0;i<5;++i){p_glEnableVertexAttribArray(i);p_glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,64,(void*)(size_t)(offsets[i]*4));}
    }
    const int reuse=rage_native_stream_layout_equal(s_terrain_stream_next,key_count,s_terrain_stream_keys,s_terrain_stream_key_count);
    p_glBindBuffer(0x8F37 /* COPY_WRITE_BUFFER */,s_terrain_stream_buffer);
    if(!reuse)p_glBufferData(0x8F37,(ptrdiff_t)(count*64),NULL,PSXGL_STREAM_DRAW);
    if(glGetError()!=GL_NO_ERROR){s_terrain_stream_key_count=0;return;}
    size_t first=0;
    for(int i=0;i<s_native_queued;++i){RageNativeGroup* g=&s_native_queue[i];
        if(!g->retained || (g->mesh->indices && !g->mesh->expanded))continue;
        if(!reuse){p_glBindBuffer(0x8F36 /* COPY_READ_BUFFER */,g->mesh->expanded?g->mesh->expanded:g->mesh->buffer);
            p_glCopyBufferSubData(0x8F36,0x8F37,(ptrdiff_t)g->first*64,(ptrdiff_t)first*64,(ptrdiff_t)g->count*64);}
        g->streamed=1;g->stream_first=(int)first;first+=g->count;
    }
    if(glGetError()!=GL_NO_ERROR){
        for(int i=0;i<s_native_queued;++i)s_native_queue[i].streamed=0;s_terrain_stream_key_count=0;
    }else if(reuse){++s_terrain_stream_reuses;atomic_fetch_add(&s_terrain_stream_saved_bytes,count*64);}
    else{atomic_fetch_add(&s_terrain_stream_bytes,count*64);memcpy(s_terrain_stream_keys,s_terrain_stream_next,key_count*sizeof(*s_terrain_stream_keys));s_terrain_stream_key_count=key_count;}
    p_glBindBuffer(0x8F36,0);p_glBindBuffer(0x8F37,0);
}
static GLuint s_vehicle_shadow_texture,s_vehicle_shadow_fbo,s_far_shadow_texture;
static float s_far_shadow_rows[12];static int s_far_shadow_valid,s_shadow_resolution;
static float s_vehicle_shadow_rows[12];
static int s_vehicle_shadow_valid;
static RageEnvironmentLight s_shadow_frame_light;
static _Atomic uint64_t s_shadow_frame_clears,s_shadow_frame_appends,s_shadow_frame_reuses;
static _Atomic uint64_t s_vehicle_shadow_maps,s_vehicle_shadow_casters,s_vehicle_shadow_receivers;
static _Atomic uint64_t s_environment_shadow_maps,s_environment_shadow_casters,s_environment_lit_groups;
static _Atomic uint64_t s_native_palette_grows,s_native_view_inversions,s_native_queue_groups,s_native_shadow_input,s_native_shadow_output;
static void rage_native_state_get(RageNativeState* s){
    s->area[0]=s_area_x1;s->area[1]=s_area_y1;s->area[2]=s_area_x2;s->area[3]=s_area_y2;
    s->pass[0]=s_pass_active;s->pass[1]=s_pass_x;s->pass[2]=s_pass_y;s->pass[3]=s_pass_w;s->pass[4]=s_pass_h;
    s->view[0]=view_enabled;s->view[1]=view_shift;s->view[2]=view_pad_left;s->view[3]=view_pad_right;
    s->wide=g_wide_cur;s->base=g_wide_cur_base;s->terrain=s_tb_scene_terrain;
}
static void rage_native_state_set(const RageNativeState* s){
    s_area_x1=s->area[0];s_area_y1=s->area[1];s_area_x2=s->area[2];s_area_y2=s->area[3];
    s_pass_active=s->pass[0];s_pass_x=s->pass[1];s_pass_y=s->pass[2];s_pass_w=s->pass[3];s_pass_h=s->pass[4];
    view_enabled=s->view[0];view_shift=s->view[1];view_pad_left=s->view[2];view_pad_right=s->view[3];
    g_wide_cur=s->wide;g_wide_cur_base=s->base;s_tb_scene_terrain=s->terrain;
}
static void rage_native_group_bind(const RageNativeGroup* g){
    p_glUniform4fv(s_native_rows,3*g->poses,g->palette);
    p_glUniform4fv(s_native_projection,g->poses,g->palette+12*g->poses);
    p_glUniform4fv(s_native_camera,g->poses,g->palette+16*g->poses);
    p_glUniform4fv(s_native_inverse_view,3,g->inverse_view);
    p_glUniform4fv(s_native_shadow_rows,3,s_vehicle_shadow_rows);
    p_glUniform4fv(s_native_far_rows,3,s_far_shadow_rows);
    p_glBindVertexArray(g->streamed?s_terrain_stream_vao:g->mesh->vao);
    p_glBindBuffer(0x8893,g->streamed?0:g->mesh->indices);s_native_indexed_draw=!g->streamed && g->mesh->indices!=0;
}
static int rage_vehicle_shadow_init(void){
    int resolution=rage_shadow_resolution(rage_track_shadow_quality());GLint maximum=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    if(resolution>maximum)resolution=maximum;
    if(s_vehicle_shadow_fbo && resolution==s_shadow_resolution)return 1;
    if(s_vehicle_shadow_fbo)p_glDeleteFramebuffers(1,&s_vehicle_shadow_fbo);
    if(s_vehicle_shadow_texture)glDeleteTextures(1,&s_vehicle_shadow_texture);
    if(s_far_shadow_texture)glDeleteTextures(1,&s_far_shadow_texture);
    s_vehicle_shadow_fbo=s_vehicle_shadow_texture=s_far_shadow_texture=0;s_vehicle_shadow_valid=s_far_shadow_valid=0;
    s_shadow_resolution=resolution;if(resolution<=0)return 0;
    glGenTextures(1,&s_vehicle_shadow_texture);glBindTexture(GL_TEXTURE_2D,s_vehicle_shadow_texture);
    glTexImage2D(GL_TEXTURE_2D,0,0x81A6 /* DEPTH_COMPONENT24 */,resolution,resolution,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glGenTextures(1,&s_far_shadow_texture);glBindTexture(GL_TEXTURE_2D,s_far_shadow_texture);
    glTexImage2D(GL_TEXTURE_2D,0,0x81A6,resolution,resolution,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    p_glGenFramebuffers(1,&s_vehicle_shadow_fbo);p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_vehicle_shadow_fbo);
    p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER,0x8D00 /* GL_DEPTH_ATTACHMENT */,GL_TEXTURE_2D,s_vehicle_shadow_texture,0);
    glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
    if(p_glCheckFramebufferStatus(PSXGL_FRAMEBUFFER)==PSXGL_FRAMEBUFFER_COMPLETE)return 1;
    p_glDeleteFramebuffers(1,&s_vehicle_shadow_fbo);glDeleteTextures(1,&s_vehicle_shadow_texture);
    glDeleteTextures(1,&s_far_shadow_texture);s_far_shadow_texture=0;
    s_vehicle_shadow_fbo=s_vehicle_shadow_texture=0;return 0;
}
#include "rage_native_lights.h"
static void rage_native_scene_flush(void){
    if(!s_native_queued || s_native_draining)return;
    s_native_draining=1;RageNativeState saved;rage_native_state_get(&saved);
    /* A view comes from a terrain camera receipt, never from a car's local
     * rotation. Main and mirror cameras keep independent inverse matrices. */
    /* Resolve each distinct viewport once. Most scenes contain hundreds of
     * groups but only a main view and a mirror; do not invert/search per mesh. */
    struct {int area[4],valid,group;float inverse[12];} views[4096];int view_count=0;
    for(int i=0;i<s_native_queued;++i){const RageNativeGroup* t=&s_native_queue[i];
        if(!t->state.terrain || t->palette[16*t->poses+3]<.5f)continue;
        int j=0;for(;j<view_count;++j)if(!memcmp(views[j].area,t->state.area,sizeof t->state.area))break;
        if(j<view_count)continue;
        memcpy(views[j].area,t->state.area,sizeof t->state.area);
        views[j].group=i;
        memset(views[j].inverse,0,sizeof views[j].inverse);
        ++s_native_view_inversions;
        views[j].valid=rage_shadow_camera_inverse(t->palette,t->palette+16*t->poses,views[j].inverse);++view_count;
    }
    for(int i=0;i<s_native_queued;++i){RageNativeGroup* g=&s_native_queue[i];
        for(int j=0;j<view_count;++j)if(!memcmp(g->state.area,views[j].area,sizeof g->state.area)){
            g->view_valid=views[j].valid;memcpy(g->inverse_view,views[j].inverse,sizeof g->inverse_view);break;
        }
    }
    rage_car_lights_collect();const double lamp_now=SDL_GetTicksNS()/1000000000.0;
    RageEnvironmentLight light={{-.34f,-1,-.42f,0},{.35f,.35f,.38f,0},{.85f,.85f,.80f,0}};
    const char* sky_option=getenv("RAGE_SKY_LIGHTING");
    if(view_count && (!sky_option || sky_option[0]!='0')){
        const RageNativeGroup* view=&s_native_queue[views[0].group];
        rage_scene_environment_light(view->lamps[0].tick,view->lamps[0].head,&light);
    }
    p_glUseProgram(s_native_program);p_glUniform4fv(s_native_light_direction,1,light.direction);
    p_glUniform4fv(s_native_light_ambient,1,light.ambient);p_glUniform4fv(s_native_light_diffuse,1,light.diffuse);
    const char* env_option=getenv("RAGE_ENVIRONMENT_SHADOWS");const int environment_enabled=rage_track_sky_light() && rage_track_shadow_quality()>0 && (!env_option || env_option[0]!='0');
    for(int i=0;i<s_native_queued;++i){RageNativeGroup* g=&s_native_queue[i];
        if(g->shadow_only && !environment_enabled)g->caster=0;
        // Only retained world terrain and explicitly tagged vehicles have a
        // proven world transform. Goal banners and other camera-space effects
        // must not be promoted to environmental shadow geometry.
        if(g->caster==2 && g->semi>=0)g->caster=0;
    }
    // Gather resident terrain once for both passes. Environment casters with
    // identical palettes can then share a light draw, as colour ranges do.
    rage_terrain_stream_prepare();
    int caster=-1,environment_casters=0;
    for(int i=0;i<s_native_queued;++i)if(s_native_queue[i].caster && s_native_queue[i].view_valid){caster=i;break;}

    static int enabled=-1;if(enabled<0){const char* value=getenv("RAGE_VEHICLE_SHADOWS");enabled=!value || value[0]!='0';}
    for(int i=0;i<s_native_queued;++i)if(s_native_queue[i].caster && s_native_queue[i].view_valid && s_native_queue[i].player_pose>=0){caster=i;break;}
    if(s_vehicle_shadow_valid && s_far_shadow_valid){
        light=s_shadow_frame_light;
        p_glUniform4fv(s_native_light_direction,1,light.direction);p_glUniform4fv(s_native_light_ambient,1,light.ambient);p_glUniform4fv(s_native_light_diffuse,1,light.diffuse);
        if(caster<0)++s_shadow_frame_reuses;
    }
    if(caster>=0 && rage_track_sky_light() && rage_track_shadow_quality()>0 && enabled && rage_vehicle_shadow_init()){
        RageNativeGroup* first=&s_native_queue[caster];float center[3];
        // Always anchor to the camera. Player meshes may appear in a different
        // queue flush or disappear in cockpit views; switching anchors causes
        // the scenery shadow grid to jump between otherwise identical frames.
        rage_vehicle_shadow_center(first->inverse_view,first->palette,first->poses,-1,center);
        float near_rows[12];
        const int plan=rage_shadow_update_plan(s_vehicle_shadow_valid && s_far_shadow_valid,1);
        const int fresh=plan==1;
        if(fresh){
            s_shadow_frame_light=light;
            s_vehicle_shadow_valid=rage_directional_shadow_map_grid(center,light.direction,16384.f,(float)s_shadow_resolution,near_rows);
            s_far_shadow_valid=rage_directional_shadow_map_grid(center,light.direction,rage_track_far_limit()*1.5f,(float)s_shadow_resolution,s_far_shadow_rows);
            ++s_shadow_frame_clears;
        }else{memcpy(near_rows,s_vehicle_shadow_rows,sizeof near_rows);++s_shadow_frame_appends;}
        // Independent near detail and full draw-distance coverage. Both use
        // stable texel grids; blend the overlap instead of revealing map edges.
        for(int cascade=0;cascade<2;++cascade){
        memcpy(s_vehicle_shadow_rows,cascade?s_far_shadow_rows:near_rows,sizeof near_rows);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_vehicle_shadow_fbo);
        p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER,0x8D00,GL_TEXTURE_2D,cascade?s_far_shadow_texture:s_vehicle_shadow_texture,0);glViewport(0,0,s_shadow_resolution,s_shadow_resolution);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glDisable(0x3000);glDisable(0x864F);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(2.f,4.f);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);if(fresh){glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);}
        p_glUseProgram(s_native_program);p_glUniform1i(s_native_shadow_pass,1);
        p_glUniform1i(s_native_spot_count,0);
        p_glUniform1i(s_native_replace_plate,0);p_glUniform1i(s_native_shadow_enabled,0);
        if(s_native_conservative)glDisable(0x9346);
        p_glActiveTexture(PSXGL_TEXTURE0+6);glBindTexture(GL_TEXTURE_2D,0);
        p_glActiveTexture(PSXGL_TEXTURE0+5);glBindTexture(GL_TEXTURE_2D,0);
        p_glUniform1i(s_native_pass,0);
        p_glActiveTexture(PSXGL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,s_material_colour);
        p_glActiveTexture(PSXGL_TEXTURE0+4);glBindTexture(GL_TEXTURE_2D,s_material_flags);
        for(int i=0;i<s_native_queued;++i){RageNativeGroup combined=s_native_queue[i];if(!combined.caster || !combined.view_valid)continue;
            int end=i+1;
            if(combined.caster==2)while(end<s_native_queued && s_native_queue[end].caster==2 && rage_terrain_groups_equal(&s_native_queue[i],&s_native_queue[end])){
                combined.count+=s_native_queue[end].count;++end;}
            RageNativeGroup* g=&combined;if(g->streamed)g->first=g->stream_first;
            if(g->caster==2){++environment_casters;++s_environment_shadow_casters;}
            s_native_shadow_input+=g->count;s_native_shadow_output+=g->mesh->shadow_indices?g->mesh->shadow_count:g->count;
            rage_native_group_bind(g);
            p_glUniform1i(s_native_pass,g->semi<0?0:1);
            p_glUniform1i(s_native_environment_shadow,g->caster==2);
            if(g->caster==1 && g->mesh->shadow_indices){p_glBindBuffer(0x8893,g->mesh->shadow_indices);glDrawElements(GL_TRIANGLES,g->mesh->shadow_count,GL_UNSIGNED_INT,0);}
            else if(s_native_indexed_draw)glDrawElements(GL_TRIANGLES,g->count,GL_UNSIGNED_INT,(void*)(size_t)(g->first*4));else glDrawArrays(GL_TRIANGLES,g->first,g->count);
            if(g->caster==1)++s_vehicle_shadow_casters;
            i=end-1;
        }
        glDisable(GL_POLYGON_OFFSET_FILL);++s_vehicle_shadow_maps;glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_FALSE);glDisable(GL_DEPTH_TEST);
        if(environment_casters)++s_environment_shadow_maps;
        }
        memcpy(s_vehicle_shadow_rows,near_rows,sizeof near_rows);
        p_glUniform1i(s_native_shadow_pass,0);
        p_glUniform1i(s_native_environment_shadow,0);
    }
    for(int i=0;i<s_native_queued;++i){RageNativeGroup combined=s_native_queue[i];
        if(combined.shadow_only){--s_native_queue[i].mesh->pins;continue;}
        int end=i+1;
        while(end<s_native_queued && rage_terrain_groups_equal(&s_native_queue[i],&s_native_queue[end])){
            combined.count+=s_native_queue[end].count;++end;
        }
        atomic_fetch_add(&s_terrain_merged_groups,end-i-1);
        RageNativeGroup* g=&combined;if(g->streamed)g->first=g->stream_first;
        rage_native_state_set(&g->state);rage_native_view_depth();
        rage_material_prepare_filter(s_tex_aniso);hr_begin(1);p_glUseProgram(s_native_program);
        rage_native_group_bind(g);
        rage_car_lights_bind(g);
        p_glUniform1i(s_native_shadow_filter,rage_track_shadow_quality()==3);
        p_glUniform1i(s_native_far_enabled,rage_track_sky_light() && rage_track_shadow_quality()>0 && s_far_shadow_valid);
        // Bias is measured in each map's texels, not a fixed number of world
        // units. Scaling by near/far extent under-biased the distant map.
        const float shadow_bias=s_shadow_resolution>0?4096.f/s_shadow_resolution:1.f;
        p_glUniform1f(s_native_far_bias,shadow_bias);
        p_glUniform1f(s_native_near_bias,shadow_bias);
        const int world_lit=rage_native_world_lit(g->state.terrain,g->mesh->tags.caster,g->background);
        const int shadow_receiver=rage_native_shadow_receiver(g->state.terrain,g->mesh->tags.caster,g->background,g->semi);
        if(!world_lit){p_glUniform1i(s_native_spot_count,0);p_glUniform1i(s_native_lens_count,0);}
        p_glUniform1i(s_native_shadow_enabled,shadow_receiver && rage_track_sky_light() && rage_track_shadow_quality()>0 && s_vehicle_shadow_valid && g->view_valid && !g->background);
        p_glUniform1i(s_native_directional,world_lit && rage_track_sky_light() && g->view_valid && !g->background);
        // The retail plate remains a fallback only when there is no native
        // light-space map. Body and wheel geometry supplies the modern shadow.
        p_glUniform1i(s_native_replace_plate,rage_track_sky_light() && rage_track_shadow_quality()>0 && s_vehicle_shadow_valid && g->view_valid && g->decal);
        p_glUniform1f(s_native_xoff,0);p_glUniform1f(s_native_xhalf,512);
        p_glActiveTexture(PSXGL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,s_material_colour);
        p_glActiveTexture(PSXGL_TEXTURE0+4);glBindTexture(GL_TEXTURE_2D,s_material_flags);
        p_glActiveTexture(PSXGL_TEXTURE0+5);glBindTexture(GL_TEXTURE_2D,s_vehicle_shadow_texture);p_glActiveTexture(PSXGL_TEXTURE0+6);glBindTexture(GL_TEXTURE_2D,s_far_shadow_texture);p_glActiveTexture(PSXGL_TEXTURE0);
        rage_native_range_passes(g->first,g->count,g->semi,g->decal,g->background);
        if(g->view_valid && s_vehicle_shadow_valid)++s_vehicle_shadow_receivers;
        if(g->view_valid && !g->background && s_vehicle_shadow_valid && environment_casters)++s_environment_lit_groups;
        if(g_wide_cur && s_ws_ablate!=1){
            wide_target_begin(wide_dx(),s_native_xoff,s_native_xhalf);
            if(!view_enabled && s_area_x2-s_area_x1+1<320){int x=s_area_x1+wide_dx(),end=s_area_x2+1+wide_dx();
                if(x<view_pad_left)x=view_pad_left;if(end>g_wide_w-view_pad_right)end=g_wide_w-view_pad_right;wide_band_scissor_x(x,end>x?end-x:0);}
            if(s_ws_ablate!=2)rage_native_range_passes(g->first,g->count,g->semi,g->decal,g->background);
            wide_target_end(s_native_xoff,s_native_xhalf);
        }
        hr_end();for(int j=i;j<end;++j)--s_native_queue[j].mesh->pins;i=end-1;
    }
    for(int i=0;i<view_count;++i)if(views[i].valid)rage_lamps_draw(&s_native_queue[views[i].group],lamp_now);
    s_native_indexed_draw=0;s_native_queued=0;rage_native_state_set(&saved);s_native_draining=0;
}
static int rage_native_scene_queue(RageNativeBuffer* mesh,int count,int semi,int decal,int background,int poses,
    const float* rows,const float* lens,const float* camera,int caster,int player_pose,const RageCarLightReceipt* lamps){
    if(s_native_queued>=rage_native_queue_limit())rage_native_scene_flush();
    if(poses<=0 || poses>RAGE_SCENE_PALETTE)return 0;
    RageNativeGroup* g=&s_native_queue[s_native_queued];
    const size_t bytes=(size_t)poses*80;
    if(g->palette_capacity<bytes){float* grown=(float*)realloc(g->palette,bytes);if(!grown)return 0;
        g->palette=grown;g->palette_capacity=bytes;++s_native_palette_grows;}
    float* palette=g->palette;const size_t capacity=g->palette_capacity;
    memcpy(palette,rows,(size_t)poses*48);memcpy(palette+12*poses,lens,(size_t)poses*16);memcpy(palette+16*poses,camera,(size_t)poses*16);
    ++s_native_queued;++s_native_queue_groups;memset(g,0,offsetof(RageNativeGroup,lamps));g->palette_capacity=capacity;
    memcpy(g->lamps,lamps,(size_t)poses*sizeof(*lamps));
    g->mesh=mesh;++mesh->pins;g->palette=palette;g->poses=poses;g->count=count;g->semi=semi;g->decal=decal;g->background=background;g->caster=caster;g->player_pose=player_pose;
    rage_native_state_get(&g->state);return 1;
}
static void rage_vehicle_shadow_release(void){
    rage_native_scene_flush();if(s_vehicle_shadow_fbo)p_glDeleteFramebuffers(1,&s_vehicle_shadow_fbo);
    rage_car_lights_release();
    if(s_terrain_stream_buffer)p_glDeleteBuffers(1,&s_terrain_stream_buffer);
    if(s_terrain_stream_vao)p_glDeleteVertexArrays(1,&s_terrain_stream_vao);
    s_terrain_stream_buffer=s_terrain_stream_vao=0;s_terrain_stream_key_count=0;
    for(int i=0;i<4096;++i){free(s_native_queue[i].palette);s_native_queue[i].palette=NULL;s_native_queue[i].palette_capacity=0;}
    if(s_vehicle_shadow_texture)glDeleteTextures(1,&s_vehicle_shadow_texture);
    if(s_far_shadow_texture)glDeleteTextures(1,&s_far_shadow_texture);s_far_shadow_texture=0;s_far_shadow_valid=0;
    s_vehicle_shadow_fbo=s_vehicle_shadow_texture=0;s_vehicle_shadow_valid=0;
}

void gl_renderer_vehicle_shadow_stats(uint64_t out[3]){out[0]=atomic_load(&s_vehicle_shadow_maps);out[1]=atomic_load(&s_vehicle_shadow_casters);out[2]=atomic_load(&s_vehicle_shadow_receivers);}
void gl_renderer_environment_shadow_stats(uint64_t out[3]){out[0]=atomic_load(&s_environment_shadow_maps);out[1]=atomic_load(&s_environment_shadow_casters);out[2]=atomic_load(&s_environment_lit_groups);}
void gl_renderer_native_queue_efficiency(uint64_t out[5]){out[0]=atomic_load(&s_native_palette_grows);out[1]=atomic_load(&s_native_view_inversions);out[2]=atomic_load(&s_native_queue_groups);out[3]=atomic_load(&s_native_shadow_input);out[4]=atomic_load(&s_native_shadow_output);}

void gl_renderer_shadow_lifetime_stats(uint64_t out[3]){out[0]=atomic_load(&s_shadow_frame_clears);out[1]=atomic_load(&s_shadow_frame_appends);out[2]=atomic_load(&s_shadow_frame_reuses);}

void gl_renderer_stream_reuse_stats(uint64_t out[2]){out[0]=atomic_load(&s_terrain_stream_reuses);out[1]=atomic_load(&s_terrain_stream_saved_bytes);}
