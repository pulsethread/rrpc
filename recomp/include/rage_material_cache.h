/* Included after the OpenGL renderer's framebuffer helpers. GPU-thread owned.
 * The original page/palette encoding is decoded once, not per scene pixel. */
enum { RAGE_MATERIAL_SLOTS=1024, RAGE_MATERIAL_EDGE=4096 };
// Full-distance A/B course banks, vehicles and window variants can exceed512 pages.
// Keep the 16-column address layout, adding rows only on capable hardware.
static int s_material_capacity=256;
#include "rage_oval.h"
static GLuint s_course_raw_tex[128]={0};
typedef struct RageMaterialSlot {
    int occupied,dirty,page_x,page_y,depth,cx,cy,accurate,twin;
    uint64_t key,touched;
} RageMaterialSlot;
static RageMaterialSlot s_material_slots[RAGE_MATERIAL_SLOTS];
static GLuint s_material_colour=0,s_material_flags=0,s_material_fbo=0,s_material_prog=0;
static float s_material_max_aniso=1;
static int s_material_applied_aniso=-1;
static void rage_material_prepare_filter(int aniso){
    if(!s_material_hw_filter || !s_material_colour || aniso==s_material_applied_aniso)return;
    s_material_applied_aniso=aniso;
    p_glActiveTexture(PSXGL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,s_material_colour);
    glTexParameterf(GL_TEXTURE_2D,0x84FE,(float)aniso<s_material_max_aniso?(float)aniso:s_material_max_aniso);
    p_glActiveTexture(PSXGL_TEXTURE0);
}

enum { RAGE_MATERIAL_BUCKETS=2048, RAGE_MATERIAL_WAYS=4 };
static int s_material_failed=0,s_material_lookup[RAGE_MATERIAL_BUCKETS*RAGE_MATERIAL_WAYS];
static uint64_t s_material_clock=0;
static _Atomic uint64_t s_material_decodes=0,s_material_triangles=0,s_material_invalidations=0;
static GLint s_material_uPage=-1,s_material_uClut=-1,s_material_uOrigin=-1,s_material_uAccurate=-1,s_material_uWindow=-1;
static const char *RAGE_MATERIAL_VS=
    "#version 330\n"
    "void main(){const vec2 p[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1));gl_Position=vec4(p[gl_VertexID],0,1);}\n";
static const char *RAGE_MATERIAL_FS=
    "#version 330\n"
    "uniform usampler2D source;uniform ivec4 page;uniform ivec2 clut;uniform ivec2 origin;uniform int accurate;uniform ivec4 window;\n"
    "layout(location=0)out vec4 frag;layout(location=1)out uint flags;\n"
    "uint at(ivec2 p){return texelFetch(source,p&ivec2(1023,511),0).r;}\n"
    "void main(){ivec2 p=ivec2(gl_FragCoord.xy)-origin;uint raw;\n"
    "p=(p & ~(window.xy*8)) | ((window.zw & window.xy)*8);\n"
    "if(page.z==0){uint word=at(page.xy+ivec2(p.x>>2,p.y));raw=at(clut+ivec2(int((word>>uint((p.x&3)*4))&15u),0));}\n"
    "else if(page.z==1){uint word=at(page.xy+ivec2(p.x>>1,p.y));raw=at(clut+ivec2(int((word>>uint((p.x&1)*8))&255u),0));}\n"
    "else raw=at(page.xy+p);\n"
    "vec3 k=vec3(raw&31u,(raw>>5u)&31u,(raw>>10u)&31u);\n"
    "frag=vec4(k*(accurate!=0?8.0/255.0:1.0/31.0),raw!=0u?1.0:0.0);flags=raw>>15u;}\n";
static int material_span_intersects(int start,int width,int lo,int hi) {
    const int end=start+width-1;
    return (hi>=start && lo<=end) || (end>=1024 && lo<=end-1024);
}
static void rage_material_notify(int kind,int x0,int y0,int x1,int y1) {
    // Native materials are assets, not aliases of the modern render target.
    // Explicit source uploads/CPU writes/copies update them. Drawing a scene
    // or packing its framebuffer must not redecode its authored textures.
    if(s_fg_drawing || (kind!=GL_COH_FLUSH && kind!=GL_COH_COPY && kind!=GL_COH_UPLOAD)) return;
    for(int i=0;i<RAGE_MATERIAL_SLOTS;++i) {
        RageMaterialSlot* m=&s_material_slots[i];if(!m->occupied || m->dirty || m->page_x>=1024)continue;
        const int page_width=m->depth==0?64:m->depth==1?128:256;
        const int palette_width=m->depth==0?16:256;
        if((y1>=m->page_y && y0<=m->page_y+255 && material_span_intersects(m->page_x,page_width,x0,x1)) ||
            (m->depth<2 && y0<=m->cy && y1>=m->cy && material_span_intersects(m->cx,palette_width,x0,x1))) {
            m->dirty=1;atomic_fetch_add_explicit(&s_material_invalidations,1,memory_order_relaxed);
        }
    }
}
static int rage_material_init(void) {
    if(s_material_fbo)return 1;if(s_material_failed)return 0;
    s_material_prog=build_program(RAGE_MATERIAL_VS,RAGE_MATERIAL_FS);
    if(!s_material_prog){s_material_failed=1;return 0;}
    GLint max_edge=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&max_edge);
    s_material_capacity=max_edge>=16384?RAGE_MATERIAL_SLOTS:max_edge>=8192?512:256;
    const int height=(s_material_capacity/16)*256;
    s_material_colour=make_tex(GL_RGBA8,RAGE_MATERIAL_EDGE,height,GL_RGBA,GL_UNSIGNED_BYTE);
    glBindTexture(GL_TEXTURE_2D,s_material_colour);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    if(SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic") || SDL_GL_ExtensionSupported("GL_ARB_texture_filter_anisotropic")){
        glGetFloatv(0x84FF,&s_material_max_aniso);s_material_hw_filter=s_material_max_aniso>=1;
        // Retain full-resolution level zero: no texture LOD reduction.
        glTexParameteri(GL_TEXTURE_2D,0x813D /* MAX_LEVEL */,0);
    }
    s_material_flags=make_tex(0x8232 /* R8UI */,RAGE_MATERIAL_EDGE,height,PSXGL_RED_INTEGER,GL_UNSIGNED_BYTE);
    p_glGenFramebuffers(1,&s_material_fbo);p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_material_fbo);
    p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER,PSXGL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,s_material_colour,0);
    p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER,PSXGL_COLOR_ATTACHMENT0+1,GL_TEXTURE_2D,s_material_flags,0);
    const GLenum attachments[2]={PSXGL_COLOR_ATTACHMENT0,PSXGL_COLOR_ATTACHMENT0+1};p_glDrawBuffers(2,attachments);
    if(p_glCheckFramebufferStatus(PSXGL_FRAMEBUFFER)!=PSXGL_FRAMEBUFFER_COMPLETE){s_material_failed=1;return 0;}
    s_material_uPage=p_glGetUniformLocation(s_material_prog,"page");s_material_uClut=p_glGetUniformLocation(s_material_prog,"clut");
    s_material_uOrigin=p_glGetUniformLocation(s_material_prog,"origin");s_material_uAccurate=p_glGetUniformLocation(s_material_prog,"accurate");
    s_material_uWindow=p_glGetUniformLocation(s_material_prog,"window");
    p_glUseProgram(s_material_prog);p_glUniform1i(p_glGetUniformLocation(s_material_prog,"source"),0);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_hr_fbo);
    return 1;
}
static int rage_material_acquire(int px,int py,int depth,int cx,int cy) {
    if(s_material_failed)return -1;
    const int external=px>>10;px&=1023;
    const int accurate=gl_renderer_accurate_blending();
    const int twin=s_tw_mask_x|(s_tw_mask_y<<5)|((s_tw_off_x&s_tw_mask_x)<<10)|((s_tw_off_y&s_tw_mask_y)<<15);
    if(depth>=2)cx=cy=0;
    const uint64_t key=(uint64_t)(px>>6)|((uint64_t)(py>>8)<<4)|((uint64_t)depth<<5)|
        ((uint64_t)(cx>>4)<<7)|((uint64_t)cy<<13)|((uint64_t)twin<<22)|((uint64_t)accurate<<42)|((uint64_t)external<<50);
    const unsigned bucket=((unsigned)((key^(key>>17)^(key>>31))*0x9e3779b97f4a7c15ull)&(RAGE_MATERIAL_BUCKETS-1u))*RAGE_MATERIAL_WAYS;
    ++s_material_clock;
    int slot=0,lookup_way=RAGE_MATERIAL_WAYS-1;
    // Colliding palette/window keys must not force a linear atlas search
    // on every triangle. Validate identities, then promote this set's hit.
    for(int way=0;way<RAGE_MATERIAL_WAYS;++way){
        const int candidate=s_material_lookup[bucket+way]-1;
        if(candidate>=0 && s_material_slots[candidate].occupied && s_material_slots[candidate].key==key){
            slot=candidate;lookup_way=way;goto material_found;
        }
    }
    while(slot<s_material_capacity) {
        const RageMaterialSlot* m=&s_material_slots[slot];
        if(m->occupied && m->key==key)break;
        ++slot;
    }
    if(slot==s_material_capacity) {
        slot=0;
        for(int i=0;i<s_material_capacity;++i) {
            if(!s_material_slots[i].occupied){slot=i;break;}
            if(s_material_slots[i].touched<s_material_slots[slot].touched)slot=i;
        }
        if(s_material_slots[slot].occupied)rage_native_scene_flush();
        // Queued draws must finish before an atlas tile is replaced.
        flush_flat_batch();flush_tex_batch();
        s_material_slots[slot]=(RageMaterialSlot){1,1,px+external*1024,py,depth,cx,cy,accurate,twin};
        s_material_slots[slot].key=key;
    }
material_found:
    for(int way=lookup_way;way>0;--way)s_material_lookup[bucket+way]=s_material_lookup[bucket+way-1];
    s_material_lookup[bucket]=slot+1;s_material_slots[slot].touched=s_material_clock;
    RageMaterialSlot* m=&s_material_slots[slot];
    if(m->dirty) {
        rage_native_scene_flush();
        flush_flat_batch();flush_tex_batch();
        if(!rage_material_init())return -1;
        const int x=(slot%16)*256,y=(slot/16)*256;
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_material_fbo);glViewport(x,y,256,256);
        glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
        p_glUseProgram(s_material_prog);p_glUniform4i(s_material_uPage,px,py,depth,0);
        p_glUniform2i(s_material_uClut,cx,cy);p_glUniform2i(s_material_uOrigin,x,y);p_glUniform1i(s_material_uAccurate,accurate);
        p_glUniform4i(s_material_uWindow,s_tw_mask_x,s_tw_mask_y,s_tw_off_x,s_tw_off_y);
        p_glActiveTexture(PSXGL_TEXTURE0);
        if(external && !s_course_raw_tex[external]){
            const uint16_t* source=rage_course_texture_pixels(external);if(!source){m->dirty=1;return -1;}
            s_course_raw_tex[external]=make_tex(PSXGL_R16UI,1024,512,PSXGL_RED_INTEGER,GL_UNSIGNED_SHORT);
            glBindTexture(GL_TEXTURE_2D,s_course_raw_tex[external]);
            GLint alignment,row;glGetIntegerv(GL_UNPACK_ALIGNMENT,&alignment);glGetIntegerv(PSXGL_UNPACK_ROW_LENGTH,&row);
            glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(PSXGL_UNPACK_ROW_LENGTH,0);
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1024,512,PSXGL_RED_INTEGER,GL_UNSIGNED_SHORT,source);
            glPixelStorei(GL_UNPACK_ALIGNMENT,alignment);glPixelStorei(PSXGL_UNPACK_ROW_LENGTH,row);
        }
        glBindTexture(GL_TEXTURE_2D,external?s_course_raw_tex[external]:s_raw_tex);
        p_glBindVertexArray(s_empty_vao);glDrawArrays(GL_TRIANGLES,0,6);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_hr_fbo);
        m->dirty=0;atomic_fetch_add_explicit(&s_material_decodes,1,memory_order_relaxed);
    }
    atomic_fetch_add_explicit(&s_material_triangles,1,memory_order_relaxed);return slot;
}
static void rage_material_release(void) {
    for(int i=0;i<128;++i)if(s_course_raw_tex[i])glDeleteTextures(1,&s_course_raw_tex[i]);memset(s_course_raw_tex,0,sizeof s_course_raw_tex);
    if(s_material_colour)glDeleteTextures(1,&s_material_colour);if(s_material_flags)glDeleteTextures(1,&s_material_flags);
    if(s_material_fbo)p_glDeleteFramebuffers(1,&s_material_fbo);if(s_material_prog)p_glDeleteProgram(s_material_prog);
    s_material_colour=s_material_flags=s_material_fbo=s_material_prog=0;s_material_failed=0;
    memset(s_material_lookup,0,sizeof s_material_lookup);s_material_clock=0;
    memset(s_material_slots,0,sizeof s_material_slots);
    s_material_capacity=256;s_material_hw_filter=0;s_material_applied_aniso=-1;
}
void gl_renderer_scene_material_stats(uint64_t* decoded,uint64_t* triangles,uint64_t* invalidated) {
    if(decoded)*decoded=atomic_load_explicit(&s_material_decodes,memory_order_relaxed);
    if(triangles)*triangles=atomic_load_explicit(&s_material_triangles,memory_order_relaxed);
    if(invalidated)*invalidated=atomic_load_explicit(&s_material_invalidations,memory_order_relaxed);
}
