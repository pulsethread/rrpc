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
    int poses,count,semi,decal,background,caster,view_valid,player_pose;
    float inverse_view[12];
    RageNativeState state;
    RageCarLightReceipt lamps[RAGE_SCENE_PALETTE];
} RageNativeGroup;
static RageNativeGroup s_native_queue[1024];
static int s_native_queued,s_native_draining;
static GLuint s_vehicle_shadow_texture,s_vehicle_shadow_fbo;
static float s_vehicle_shadow_rows[12];
static int s_vehicle_shadow_valid;
static _Atomic uint64_t s_vehicle_shadow_maps,s_vehicle_shadow_casters,s_vehicle_shadow_receivers;
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
    p_glBindVertexArray(g->mesh->vao);
    p_glBindBuffer(0x8893,g->mesh->indices);s_native_indexed_draw=g->mesh->indices!=0;
}
static int rage_vehicle_shadow_init(void){
    if(s_vehicle_shadow_fbo)return 1;
    glGenTextures(1,&s_vehicle_shadow_texture);glBindTexture(GL_TEXTURE_2D,s_vehicle_shadow_texture);
    glTexImage2D(GL_TEXTURE_2D,0,0x81A6 /* DEPTH_COMPONENT24 */,4096,4096,0,GL_DEPTH_COMPONENT,GL_UNSIGNED_INT,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    p_glGenFramebuffers(1,&s_vehicle_shadow_fbo);p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_vehicle_shadow_fbo);
    p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER,0x8D00 /* GL_DEPTH_ATTACHMENT */,GL_TEXTURE_2D,s_vehicle_shadow_texture,0);
    glDrawBuffer(GL_NONE);glReadBuffer(GL_NONE);
    if(p_glCheckFramebufferStatus(PSXGL_FRAMEBUFFER)==PSXGL_FRAMEBUFFER_COMPLETE)return 1;
    p_glDeleteFramebuffers(1,&s_vehicle_shadow_fbo);glDeleteTextures(1,&s_vehicle_shadow_texture);
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
    struct {int area[4],valid,group;float inverse[12];} views[1024];int view_count=0;
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
    int caster=-1;
    for(int i=0;i<s_native_queued;++i)if(s_native_queue[i].caster && s_native_queue[i].view_valid){caster=i;break;}
    static int enabled=-1;if(enabled<0){const char* value=getenv("RAGE_VEHICLE_SHADOWS");enabled=!value || value[0]!='0';}
    for(int i=0;i<s_native_queued;++i)if(s_native_queue[i].caster && s_native_queue[i].view_valid && s_native_queue[i].player_pose>=0){caster=i;break;}
    if(caster>=0 && enabled && rage_vehicle_shadow_init()){
        RageNativeGroup* first=&s_native_queue[caster];float center[3];
        // Match the reference fallback: the view camera anchors the light when
        // the player's body is absent (hood/replay). Palette zero may belong
        // to scenery or a different AI car as painter order changes.
        rage_vehicle_shadow_center(first->inverse_view,first->palette,first->poses,first->player_pose,center);
        s_vehicle_shadow_valid=rage_vehicle_shadow_map(center,s_vehicle_shadow_rows);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_vehicle_shadow_fbo);glViewport(0,0,4096,4096);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);
        glDisable(0x3000);glDisable(0x864F);glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glDepthMask(GL_TRUE);
        glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
        p_glUseProgram(s_native_program);p_glUniform1i(s_native_shadow_pass,1);
        p_glUniform1i(s_native_spot_count,0);
        p_glUniform1i(s_native_replace_plate,0);p_glUniform1i(s_native_shadow_enabled,0);
        p_glActiveTexture(PSXGL_TEXTURE0+5);glBindTexture(GL_TEXTURE_2D,0);
        p_glUniform1i(s_native_pass,0);
        p_glActiveTexture(PSXGL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,s_material_colour);
        p_glActiveTexture(PSXGL_TEXTURE0+4);glBindTexture(GL_TEXTURE_2D,s_material_flags);
        for(int i=0;i<s_native_queued;++i){RageNativeGroup* g=&s_native_queue[i];if(!g->caster || !g->view_valid)continue;
            s_native_shadow_input+=g->count;s_native_shadow_output+=g->mesh->shadow_indices?g->mesh->shadow_count:g->count;
            rage_native_group_bind(g);
            p_glUniform1i(s_native_pass,g->semi<0?0:1);
            if(g->mesh->shadow_indices){p_glBindBuffer(0x8893,g->mesh->shadow_indices);glDrawElements(GL_TRIANGLES,g->mesh->shadow_count,GL_UNSIGNED_INT,0);}
            else if(s_native_indexed_draw)glDrawElements(GL_TRIANGLES,g->count,GL_UNSIGNED_INT,0);else glDrawArrays(GL_TRIANGLES,0,g->count);
            ++s_vehicle_shadow_casters;
        }
        ++s_vehicle_shadow_maps;glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDepthMask(GL_FALSE);glDisable(GL_DEPTH_TEST);
        p_glUniform1i(s_native_shadow_pass,0);
    }
    for(int i=0;i<s_native_queued;++i){RageNativeGroup* g=&s_native_queue[i];
        rage_native_state_set(&g->state);rage_native_view_depth();
        rage_material_prepare_filter(s_tex_aniso);hr_begin(1);p_glUseProgram(s_native_program);
        rage_native_group_bind(g);
        rage_car_lights_bind(g);
        p_glUniform1i(s_native_shadow_enabled,s_vehicle_shadow_valid && g->view_valid && !g->background && !g->decal);
        p_glUniform1i(s_native_directional,g->view_valid && g->state.terrain && !g->background && !g->decal);
        // The retail plate remains a fallback only when there is no native
        // light-space map. Body and wheel geometry supplies the modern shadow.
        p_glUniform1i(s_native_replace_plate,s_vehicle_shadow_valid && g->view_valid && g->decal);
        p_glUniform1f(s_native_xoff,0);p_glUniform1f(s_native_xhalf,512);
        p_glActiveTexture(PSXGL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,s_material_colour);
        p_glActiveTexture(PSXGL_TEXTURE0+4);glBindTexture(GL_TEXTURE_2D,s_material_flags);
        p_glActiveTexture(PSXGL_TEXTURE0+5);glBindTexture(GL_TEXTURE_2D,s_vehicle_shadow_texture);p_glActiveTexture(PSXGL_TEXTURE0);
        rage_native_passes(g->count,g->semi,g->decal,g->background);
        if(g->view_valid && s_vehicle_shadow_valid)++s_vehicle_shadow_receivers;
        if(g_wide_cur && s_ws_ablate!=1){
            wide_target_begin(wide_dx(),s_native_xoff,s_native_xhalf);
            if(!view_enabled && s_area_x2-s_area_x1+1<320){int x=s_area_x1+wide_dx(),end=s_area_x2+1+wide_dx();
                if(x<view_pad_left)x=view_pad_left;if(end>g_wide_w-view_pad_right)end=g_wide_w-view_pad_right;wide_band_scissor_x(x,end>x?end-x:0);}
            if(s_ws_ablate!=2)rage_native_passes(g->count,g->semi,g->decal,g->background);
            wide_target_end(s_native_xoff,s_native_xhalf);
        }
        hr_end();--g->mesh->pins;
    }
    for(int i=0;i<view_count;++i)if(views[i].valid)rage_lamps_draw(&s_native_queue[views[i].group],lamp_now);
    s_native_indexed_draw=0;s_native_queued=0;rage_native_state_set(&saved);s_native_draining=0;
}
static int rage_native_scene_queue(RageNativeBuffer* mesh,int count,int semi,int decal,int background,int poses,
    const float* rows,const float* lens,const float* camera,int caster,int player_pose,const RageCarLightReceipt* lamps){
    if(s_native_queued==1024)rage_native_scene_flush();
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
    for(int i=0;i<1024;++i){free(s_native_queue[i].palette);s_native_queue[i].palette=NULL;s_native_queue[i].palette_capacity=0;}
    if(s_vehicle_shadow_texture)glDeleteTextures(1,&s_vehicle_shadow_texture);
    s_vehicle_shadow_fbo=s_vehicle_shadow_texture=0;s_vehicle_shadow_valid=0;
}

void gl_renderer_vehicle_shadow_stats(uint64_t out[3]){out[0]=atomic_load(&s_vehicle_shadow_maps);out[1]=atomic_load(&s_vehicle_shadow_casters);out[2]=atomic_load(&s_vehicle_shadow_receivers);}
void gl_renderer_native_queue_efficiency(uint64_t out[5]){out[0]=atomic_load(&s_native_palette_grows);out[1]=atomic_load(&s_native_view_inversions);out[2]=atomic_load(&s_native_queue_groups);out[3]=atomic_load(&s_native_shadow_input);out[4]=atomic_load(&s_native_shadow_output);}
