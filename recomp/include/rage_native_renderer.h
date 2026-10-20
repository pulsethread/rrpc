/* GPU-thread-owned native scene backend. Mesh buffers exclude camera state,
 * screen coordinates and every PS1 raster correction. Included by the GL host. */
#include "rage_native_geometry.h"
#include "rage_track.h"
enum { RAGE_NATIVE_CACHE=128, RAGE_NATIVE_FLOATS=16 };
typedef struct RageNativeBuffer {
    GLuint buffer;
    uint64_t key, touched;
    size_t bytes;
    float* content;
} RageNativeBuffer;
static RageNativeBuffer s_native_meshes[RAGE_NATIVE_CACHE];
static GLuint s_native_program,s_native_vao;
static GLint s_native_rows,s_native_projection,s_native_camera,s_native_pass,s_native_xoff,s_native_xhalf,s_native_aniso,s_native_now;
static uint64_t s_native_clock;
static unsigned s_native_replay_cursor;
static int s_native_conservative=0;
static unsigned char s_native_replay_hints[256];
static _Atomic uint64_t s_native_uploads,s_native_hits,s_native_draws,s_native_camera_placements;
static _Atomic uint64_t s_native_view_clears;
static _Atomic uint64_t s_native_region_repairs;
uint64_t gl_renderer_native_material_region_count(void){return atomic_load(&s_native_region_repairs);}
uint64_t gl_renderer_native_view_clear_count(void){return atomic_load(&s_native_view_clears);}
static float s_native_vertices[TEXBATCH_MAXV*RAGE_NATIVE_FLOATS];
static const char* RAGE_NATIVE_VS=
    "#version 330\n"
    "layout(location=0)in vec3 position;layout(location=1)in vec2 uv;layout(location=2)in vec3 colour;\n"
    "layout(location=4)in vec4 region;flat out ivec4 bounds;layout(location=3)in vec4 material;uniform vec4 rows[144];uniform vec4 projection[48];uniform vec4 camera_origin[48];\n"
    "uniform float xoff,xhalf,fade_now;flat out float coverage;smooth out vec2 texcoord;smooth out vec3 tint;flat out ivec3 properties;\n"
    "void main(){int pose=int(material.y);vec4 p=vec4(position-camera_origin[pose].xyz,camera_origin[pose].w>0.5?0:1);\n"
    "vec3 camera=vec3(dot(rows[pose*3],p),dot(rows[pose*3+1],p),dot(rows[pose*3+2],p));\n"
    "vec4 lens=projection[pose];vec2 h=camera.xy*lens.xy+camera.z*lens.zw;\n"
    "int bias=(int(material.z)>>1)&255;if(bias>=128)bias-=256;float worldBias=float(bias);\n"
    "float legacyBias=float(bias)*camera.z/1048576.0;float bound=16.0*worldBias/max(camera.z+worldBias,1.0);\n"
    "float depth=camera.z-16.0+sign(legacyBias)*min(abs(legacyBias),abs(bound));\n"
    "gl_Position=vec4((h.x+camera.z*xoff)/xhalf-camera.z,h.y/256.0-camera.z,depth,camera.z);\n"
    "coverage=abs(camera_origin[pose].w)>2.0?clamp(fade_now-(abs(camera_origin[pose].w)-2.0),0.0,1.0):1.0;bounds=ivec4(region);texcoord=uv;tint=colour;properties=ivec3(int(material.x),int(material.z),int(material.w));}\n";
static const char* RAGE_NATIVE_FS=
    "#version 330\n"
    "uniform sampler2D colours;uniform usampler2D flags;uniform int pass,aniso;\n"
    "flat in float coverage;smooth in vec2 texcoord;smooth in vec3 tint;flat in ivec3 properties;flat in ivec4 bounds;out vec4 frag;\n"
    "ivec2 origin(){return ivec2(properties.x&15,properties.x>>4)*256;}\n"
    "ivec2 address(ivec2 p){p.x=(properties.z&16)!=0?(p.x&255):clamp(p.x,bounds.x,bounds.z);p.y=(properties.z&32)!=0?(p.y&255):clamp(p.y,bounds.y,bounds.w);return p;}\n"
    "vec4 texel(ivec2 p){return texelFetch(colours,origin()+address(p),0);}\n"
    "vec4 class_texel(ivec2 p,int stp){p=address(p);vec4 c=texelFetch(colours,origin()+p,0);if(c.a<0.5 || int(texelFetch(flags,origin()+p,0).r)!=stp)return vec4(0);return c;}\n"
    "vec4 class_linear(vec2 uv,int stp){vec2 q=uv-0.5,f=fract(q);ivec2 b=ivec2(floor(q));return mix(mix(class_texel(b,stp),class_texel(b+ivec2(1,0),stp),f.x),mix(class_texel(b+ivec2(0,1),stp),class_texel(b+ivec2(1,1),stp),f.x),f.y);}\n"
    "vec3 anisotropic_sample(vec2 uv,vec2 dx,vec2 dy,int stp,vec3 base){\n"
    " float a=dx.x*dx.x+dy.x*dy.x,b=dx.x*dx.y+dy.x*dy.y,c=dx.y*dx.y+dy.y*dy.y;\n"
    " float eigen=0.5*(a+c+sqrt(max(0.0,(a-c)*(a-c)+4.0*b*b))),major=sqrt(max(eigen,0.0));\n"
    " if(major<=1.0){vec4 col=class_linear(uv,stp);return col.a>0.00001?col.rgb/col.a:base;}\n"
    " float det=dx.x*dy.y-dx.y*dy.x,minor=max(1.0,sqrt(max(0.0,det*det/max(eigen,0.000001))));\n"
    " vec2 axis=abs(b)>0.000001?normalize(vec2(b,eigen-a)):(a>=c?vec2(1,0):vec2(0,1));\n"
    " int n=clamp(int(ceil(major/minor)),1,clamp(aniso,1,16));float box=max(minor,major/float(n));\n"
    " int k=clamp(int(ceil(box)),1,8);while(k>1 && n*k*k>64)k--;\n"
    " vec2 across=vec2(-axis.y,axis.x);vec4 sum=vec4(0);\n"
    " for(int t=0;t<n;++t){vec2 center=uv+axis*major*((float(t)+0.5)/float(n)-0.5);\n"
    "  if(k==1)sum+=class_linear(center,stp);\n"
    "  else for(int y=0;y<k;++y)for(int x=0;x<k;++x){vec2 p=center+box*(axis*((float(x)+0.5)/float(k)-0.5)+across*((float(y)+0.5)/float(k)-0.5));sum+=class_texel(ivec2(floor(p)),stp);}}\n"
    " return sum.a>0.00001?sum.rgb/sum.a:base;\n"
    "}\n"
    "void main(){vec2 dx=dFdx(texcoord),dy=dFdy(texcoord);ivec2 p=address(ivec2(floor(texcoord)));\n"
    "if((properties.z&512)!=0 && gl_FrontFacing==((properties.z&1024)!=0))discard;\n"
    "// Stable coverage transparency preserves depth and ordering across road/decal layers.\n"
    "if(coverage<1.0 && (coverage<=0.0 || fract(52.9829189*fract(dot(floor(gl_FragCoord.xy),vec2(0.06711056,0.00583715))))>=coverage))discard;\n"
    "bool solid=(properties.z&64)!=0;vec4 c=solid?vec4(1):texel(p);if(c.a<0.5)discard;uint stp=solid?1u:texelFetch(flags,origin()+p,0).r;\n"
    "if((pass==1 && stp!=0u)||(pass==2 && stp==0u))discard;\n"
    "if(!solid && (properties.z&128)!=0 && (properties.z&2048)!=0){\n"
    " vec2 size=vec2(textureSize(colours,0));float edge=0.5;\n"
    " vec2 q=clamp(texcoord,vec2(bounds.xy)+0.5,vec2(bounds.zw)+0.5);if((properties.z&16)!=0)q.x=mod(texcoord.x,256.0);if((properties.z&32)!=0)q.y=mod(texcoord.y,256.0);q=clamp(q,vec2(edge),vec2(256.0-edge));\n"
    " vec4 filtered=textureGrad(colours,(vec2(origin())+q)/size,dx/size,dy/size);if(filtered.a>0.00001)c.rgb=filtered.rgb/filtered.a;\n"
    "}else if(!solid && (properties.z&128)!=0)c.rgb=anisotropic_sample(texcoord,dx,dy,int(stp),c.rgb);\n"
    "else if(!solid && (properties.z&15)!=0){vec2 q=texcoord-0.5,f=fract(q);ivec2 b=ivec2(floor(q));\n"
    "vec4 filtered=mix(mix(texel(b),texel(b+ivec2(1,0)),f.x),mix(texel(b+ivec2(0,1)),texel(b+ivec2(1,1)),f.x),f.y);\n"
    "if(filtered.a>0.00001)c.rgb=filtered.rgb/filtered.a;}\n"
    "vec3 rgb=solid?tint:((properties.y&1)!=0?c.rgb:clamp(c.rgb*tint*2.0,0.0,1.0));frag=vec4(rgb,float(stp));}\n";
static int rage_native_init(void) {
    if(s_native_program)return 1;
    s_native_program=build_program(RAGE_NATIVE_VS,RAGE_NATIVE_FS);
    if(!s_native_program)return 0;
    s_native_conservative=SDL_GL_ExtensionSupported("GL_NV_conservative_raster");
    fprintf(stdout,"Native terrain subpixel coverage: %s\n",s_native_conservative?"GPU conservative raster":"shared-edge geometry");
    p_glGenVertexArrays(1,&s_native_vao);
    s_native_rows=p_glGetUniformLocation(s_native_program,"rows");
    s_native_projection=p_glGetUniformLocation(s_native_program,"projection");
    s_native_camera=p_glGetUniformLocation(s_native_program,"camera_origin");
    s_native_pass=p_glGetUniformLocation(s_native_program,"pass");
    s_native_now=p_glGetUniformLocation(s_native_program,"fade_now");
    s_native_aniso=p_glGetUniformLocation(s_native_program,"aniso");
    s_native_xoff=p_glGetUniformLocation(s_native_program,"xoff");
    s_native_xhalf=p_glGetUniformLocation(s_native_program,"xhalf");
    p_glUseProgram(s_native_program);
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"colours"),3);
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"flags"),4);
    return 1;
}
static GLuint rage_native_buffer(float* vertices,size_t bytes) {

    ++s_native_clock;
    // Replay order is usually identical across intermediate frames. Verify
    // the expected retained mesh directly with the vectorized byte compare,
    // avoiding even a fast full-mesh hash. An eviction, changed clipping or
    // different ordering falls back to the normal content-addressed lookup.
    const unsigned hint=s_fg_drawing?s_native_replay_cursor++:256;
    if(hint<256 && s_native_replay_hints[hint]){
        RageNativeBuffer* mesh=&s_native_meshes[s_native_replay_hints[hint]-1];
        if(mesh->buffer && mesh->bytes==bytes && !memcmp(mesh->content,vertices,bytes)){
            mesh->touched=s_native_clock;atomic_fetch_add(&s_native_hits,1);return mesh->buffer;
        }
    }
    const uint64_t key=rage_native_geometry_key(vertices,bytes);
    int victim=0;
    for(int i=0;i<RAGE_NATIVE_CACHE;++i) {
        RageNativeBuffer* mesh=&s_native_meshes[i];
        if(mesh->buffer && mesh->key==key && mesh->bytes==bytes && !memcmp(mesh->content,vertices,bytes)) {
            if(hint<256)s_native_replay_hints[hint]=(unsigned char)(i+1);
            mesh->touched=s_native_clock;atomic_fetch_add(&s_native_hits,1);return mesh->buffer;
        }
        if(!mesh->buffer || mesh->touched<s_native_meshes[victim].touched)victim=i;
    }
    RageNativeBuffer* mesh=&s_native_meshes[victim];
    float* content=(float*)realloc(mesh->content,bytes);
    if(!content)return 0;
    mesh->content=content;memcpy(content,vertices,bytes);
    // Keep the exact input as cache identity; repair filtering bounds only
    // on upload, so intermediate frames reuse the already prepared mesh.
    atomic_fetch_add(&s_native_region_repairs,rage_native_material_regions(vertices,bytes/(RAGE_NATIVE_FLOATS*sizeof(float))));
    if(!mesh->buffer)p_glGenBuffers(1,&mesh->buffer);
    p_glBindBuffer(PSXGL_ARRAY_BUFFER,mesh->buffer);
    p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)bytes,vertices,GL_STATIC_DRAW);
    mesh->bytes=bytes;mesh->key=key;mesh->touched=s_native_clock;
    if(hint<256)s_native_replay_hints[hint]=(unsigned char)(victim+1);
    atomic_fetch_add(&s_native_uploads,1);return mesh->buffer;
}
static void rage_native_clear_depth_rect(GLuint fbo,int scale,const int rect[4]) {
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,fbo);glEnable(GL_SCISSOR_TEST);
    glScissor(rect[0]*scale,rect[1]*scale,(rect[2]-rect[0]+1)*scale,(rect[3]-rect[1]+1)*scale);
    glDepthMask(GL_TRUE);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);glDepthMask(GL_FALSE);
}
static int rage_native_has_inset(void){
    if(s_area_x2-s_area_x1+1<320 || s_area_y2-s_area_y1+1<200)return 0;
    for(int i=0;i<s_scene_views.count;++i){const int* r=s_scene_views.rect[i];if(r[2]-r[0]<200 && r[3]-r[1]<100)return 1;}
    return 0;
}
static void rage_native_mask_subviews(GLuint fbo,int scale,int offset,const int rect[4]){
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,fbo);glEnable(GL_SCISSOR_TEST);glStencilMask(0x80);glClearStencil(0);
    glScissor((rect[0]+offset)*scale,rect[1]*scale,(rect[2]-rect[0]+1)*scale,(rect[3]-rect[1]+1)*scale);glClear(GL_STENCIL_BUFFER_BIT);
    if(rage_native_has_inset())for(int i=0;i<s_scene_views.count;++i){const int* r=s_scene_views.rect[i];if(r[2]-r[0]>=200 || r[3]-r[1]>=100)continue;
        glScissor((r[0]+offset)*scale,r[1]*scale,(r[2]-r[0]+1)*scale,(r[3]-r[1]+1)*scale);glClearStencil(0x80);glClear(GL_STENCIL_BUFFER_BIT);
    }
    glClearStencil(0);glStencilMask(0x01);
}
static void rage_native_view_depth(void) {
    if(!s_scene_depth_owned)return;
    int rect[4]={s_area_x1,s_area_y1,s_area_x2,s_area_y2};
    if(s_pass_active){if(rect[0]<s_pass_x)rect[0]=s_pass_x;if(rect[1]<s_pass_y)rect[1]=s_pass_y;
        if(rect[2]>=s_pass_x+s_pass_w)rect[2]=s_pass_x+s_pass_w-1;if(rect[3]>=s_pass_y+s_pass_h)rect[3]=s_pass_y+s_pass_h-1;}
    if(!rage_native_view_enter(&s_scene_views,rect))return;
    atomic_fetch_add(&s_native_view_clears,1);
    // A rear/inset camera has an independent depth domain. Clear only its
    // rectangle on its first 3D draw, preserving the surrounding main view.
    rage_native_clear_depth_rect(s_hr_fbo,s_hr_scale,rect);
    if(g_wide_cur){
        int x=rect[0]+wide_dx(),w=rect[2]-rect[0]+1;
        if(w>=320){x=view_pad_left;w=g_wide_w-view_pad_left-view_pad_right;}
        if(x<0){w+=x;x=0;}if(x+w>g_wide_w)w=g_wide_w-x;
        const int wide_rect[4]={x,rect[1],x+w-1,rect[3]};
        rage_native_clear_depth_rect(g_wide_cur,s_out_scale,wide_rect);
    }
    rage_native_mask_subviews(s_hr_fbo,s_hr_scale,0,rect);
    if(g_wide_cur)rage_native_mask_subviews(g_wide_cur,s_out_scale,wide_dx(),rect);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_hr_fbo);glDepthMask(GL_FALSE);
    glScissor(rect[0]*s_hr_scale,rect[1]*s_hr_scale,(rect[2]-rect[0]+1)*s_hr_scale,(rect[3]-rect[1]+1)*s_hr_scale);
}
static void rage_native_passes(int count,int semi,int decal,int background) {
    p_glUniform1i(s_native_aniso,s_tex_aniso);
    p_glUniform1f(s_native_now,rage_track_fade_clock());
    if(rage_native_has_inset()){glEnable(GL_STENCIL_TEST);glStencilMask(0);glStencilFunc(GL_EQUAL,0,0x80);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);}else glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    if(s_native_conservative){if(s_tb_scene_terrain==2 && semi<0 && !decal && !background)glEnable(0x9346);else glDisable(0x9346);}
    if(background)glDisable(GL_DEPTH_TEST);else glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);
    // Face-specific bounded bias is applied by the native vertex shader.
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);glDepthMask(decal || background?GL_FALSE:GL_TRUE);
    p_glUniform1i(s_native_pass,semi<0?0:1);glDrawArrays(GL_TRIANGLES,0,count);
    if(semi>=0){
        apply_psx_blend(semi);glDepthMask(GL_FALSE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        p_glUniform1i(s_native_pass,2);glDrawArrays(GL_TRIANGLES,0,count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    if(s_native_conservative)glDisable(0x9346);
    glDisable(GL_STENCIL_TEST);glStencilMask(0x01);
    atomic_fetch_add(&s_native_draws,1);
}
static int rage_native_draw_batch(int count,int semi,int decal,int background) {
    if(!rage_native_init())return 0;
    rage_native_view_depth();
    int mapping[RAGE_SCENE_PALETTE],poses=0;
    float rows[RAGE_SCENE_PALETTE*12],lens[RAGE_SCENE_PALETTE*4],camera[RAGE_SCENE_PALETTE*4];
    for(int i=0;i<RAGE_SCENE_PALETTE;++i)mapping[i]=-1;
    for(int i=0;i<count;++i) {
        const float* source=s_tb+i*TEXV;float* vertex=s_native_vertices+i*RAGE_NATIVE_FLOATS;
        const int original=(int)source[30]-1;
        if(original<0 || original>=s_scene_palette_count || source[31]<1)return 0;
        if(mapping[original]<0){
            mapping[original]=poses;
            memcpy(rows+12*poses,s_scene_batch_rows+12*original,48);
            memcpy(lens+4*poses,s_scene_batch_projection+4*original,16);
            memcpy(camera+4*poses,s_scene_batch_camera+4*original,16);++poses;
        }
        rage_native_pack_vertex(vertex,source,(unsigned)mapping[original]);
    }
    const GLuint buffer=rage_native_buffer(s_native_vertices,(size_t)count*RAGE_NATIVE_FLOATS*sizeof(float));
    if(!buffer)return 0;
    rage_material_prepare_filter(s_tex_aniso);
    hr_begin(1);p_glUseProgram(s_native_program);
    p_glUniform4fv(s_native_rows,3*poses,rows);p_glUniform4fv(s_native_projection,poses,lens);
    p_glUniform4fv(s_native_camera,poses,camera);
    p_glUniform1f(s_native_xoff,0);p_glUniform1f(s_native_xhalf,512);
    p_glActiveTexture(PSXGL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,s_material_colour);
    p_glActiveTexture(PSXGL_TEXTURE0+4);glBindTexture(GL_TEXTURE_2D,s_material_flags);
    p_glActiveTexture(PSXGL_TEXTURE0);
    p_glBindVertexArray(s_native_vao);p_glBindBuffer(PSXGL_ARRAY_BUFFER,buffer);
    const int sizes[5]={3,2,3,4,4},offsets[5]={0,3,5,8,12};
    for(int i=0;i<5;++i){p_glEnableVertexAttribArray(i);p_glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,RAGE_NATIVE_FLOATS*sizeof(float),(void*)(size_t)(offsets[i]*sizeof(float)));}
    rage_native_passes(count,semi,decal,background);
    if(g_wide_cur && s_ws_ablate!=1){
        wide_target_begin(wide_dx(),s_native_xoff,s_native_xhalf);
        // Full scene views reveal wide margins; a literal inset/rear view
        // must stay inside its own horizontal viewport on the wide surface.
        if(!view_enabled && s_area_x2-s_area_x1+1<320){
            int x=s_area_x1+wide_dx(),end=s_area_x2+1+wide_dx();
            if(x<view_pad_left)x=view_pad_left;if(end>g_wide_w-view_pad_right)end=g_wide_w-view_pad_right;
            wide_band_scissor_x(x,end>x?end-x:0);
        }
        if(s_ws_ablate!=2)rage_native_passes(count,semi,decal,background);
        wide_target_end(s_native_xoff,s_native_xhalf);
    }
    hr_end();return 1;
}
static void rage_native_release(void) {
    for(int i=0;i<RAGE_NATIVE_CACHE;++i){
        if(s_native_meshes[i].buffer)p_glDeleteBuffers(1,&s_native_meshes[i].buffer);
        free(s_native_meshes[i].content);
    }
    memset(s_native_meshes,0,sizeof s_native_meshes);s_native_clock=0;
    memset(s_native_replay_hints,0,sizeof s_native_replay_hints);s_native_replay_cursor=0;
    if(s_native_program)p_glDeleteProgram(s_native_program);
    if(s_native_vao)p_glDeleteVertexArrays(1,&s_native_vao);
    s_native_program=s_native_vao=0;
}
void gl_renderer_native_scene_stats(uint64_t* uploads,uint64_t* hits,uint64_t* draws,uint64_t* camera_placements) {
    if(uploads)*uploads=atomic_load(&s_native_uploads);
    if(hits)*hits=atomic_load(&s_native_hits);
    if(draws)*draws=atomic_load(&s_native_draws);
    if(camera_placements)*camera_placements=atomic_load(&s_native_camera_placements);
}
/* Actual-driver regression: a green subimage surrounded by opaque black/white
 * artwork must remain green at every edge and across the quad diagonal. */
int gl_renderer_native_material_probe(void) {
    GL_RT_SYNC("native_material_probe");
    if(!s_ctx || !s_raster_ok)return -1;
    // The render queue may have been consumed without submitting its current
    // batches. Finish those before replacing any live texture/FBO bindings.
    flush_cpu_upload();flush_flat_batch();flush_tex_batch();
    if(!rage_native_init())return -1;
    unsigned char* pixels=(unsigned char*)calloc(256*256,4);
    unsigned char* flags=(unsigned char*)calloc(256*256,1);
    if(!pixels || !flags){free(pixels);free(flags);return -1;}
    const int keep_anisotropy=s_tex_aniso;
    const RageNativeViews keep_views=s_scene_views;s_scene_views.count=0;
    const int keep_terrain=s_tb_scene_terrain;s_tb_scene_terrain=0;
    GLint keep_textures[5],keep_viewport[4];glGetIntegerv(GL_VIEWPORT,keep_viewport);
    for(int u=0;u<5;++u){p_glActiveTexture(PSXGL_TEXTURE0+u);glGetIntegerv(GL_TEXTURE_BINDING_2D,keep_textures+u);}
    for(int y=0;y<256;++y)for(int x=0;x<256;++x){
        unsigned char* p=pixels+4*(y*256+x);
        const int inside=x>=16 && x<=31 && y>=16 && y<=31;
        const int white=(x+y)&1;
        p[0]=inside?0:white?255:0;p[1]=inside?255:white?255:0;
        p[2]=inside?0:white?255:0;p[3]=255;
    }
    GLuint colour=0,stp=0,target=0,fbo=0,vao=0,vbo=0,depth=0;
    p_glActiveTexture(PSXGL_TEXTURE0);
    colour=make_tex(GL_RGBA8,256,256,GL_RGBA,GL_UNSIGNED_BYTE);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    stp=make_tex(0x8232,256,256,PSXGL_RED_INTEGER,GL_UNSIGNED_BYTE);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,256,256,PSXGL_RED_INTEGER,GL_UNSIGNED_BYTE,flags);
    free(pixels);free(flags);
    target=make_tex(GL_RGBA8,1024,512,GL_RGBA,GL_UNSIGNED_BYTE);
    int failures=-1;
    p_glGenRenderbuffers(1,&depth);p_glBindRenderbuffer(PSXGL_RENDERBUFFER,depth);
    p_glRenderbufferStorage(PSXGL_RENDERBUFFER,PSXGL_DEPTH24_STENCIL8,1024,512);
    if(!make_fbo(&fbo,target,depth))goto finish;
    p_glGenVertexArrays(1,&vao);p_glGenBuffers(1,&vbo);
    p_glBindVertexArray(vao);p_glBindBuffer(PSXGL_ARRAY_BUFFER,vbo);
    float vertices[6*16]={0};const int corners[6]={0,1,2,2,1,3};
    for(int i=0;i<6;++i){const int c=corners[i];float* p=vertices+i*16;
        p[0]=(c&1)?128:-128;p[1]=(c&2)?128:-128;p[2]=256;
        p[3]=(c&1)?31:16;p[4]=(c&2)?31:16;p[5]=p[6]=p[7]=0.5f;
        p[8]=p[9]=p[10]=0;p[11]=1;p[12]=p[13]=16;p[14]=p[15]=31;
    }
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    const int sizes[5]={3,2,3,4,4},offsets[5]={0,3,5,8,12};
    for(int i=0;i<5;++i){p_glEnableVertexAttribArray(i);p_glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,64,(void*)(size_t)(offsets[i]*4));}
    p_glUseProgram(s_native_program);
    const float rows[12]={1,0,0,0,0,1,0,0,0,0,1,0},lens[4]={256,256,256,128},camera[4]={0,0,0,0};
    p_glUniform4fv(s_native_rows,3,rows);p_glUniform4fv(s_native_projection,1,lens);p_glUniform4fv(s_native_camera,1,camera);
    p_glUniform1f(s_native_xoff,0);p_glUniform1f(s_native_xhalf,512);p_glUniform1i(s_native_pass,0);
    p_glActiveTexture(PSXGL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,colour);
    p_glActiveTexture(PSXGL_TEXTURE0+4);glBindTexture(GL_TEXTURE_2D,stp);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,fbo);glViewport(0,0,1024,512);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDrawArrays(GL_TRIANGLES,0,6);
    const int samples[9][2]={{128,0},{255,0},{383,0},{128,128},{383,128},{128,255},{255,255},{383,255},{255,128}};
    failures=0;
    for(int i=0;i<9;++i){unsigned char p[4];glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        if(p[0]>1 || p[1]<254 || p[2]>1)++failures;
    }
    // Negative control: whole-page bounds must expose the surrounding artwork.
    // This proves the edge test can detect the old unrestricted sampling path.
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[12]=p[13]=0;p[14]=p[15]=255;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    glDrawArrays(GL_TRIANGLES,0,6);int contaminated=0;
    for(int i=0;i<9;++i){unsigned char p[4];glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        if(p[0]>1 || p[1]<254 || p[2]>1)++contaminated;}
    if(!contaminated)++failures;
    // Solid materials preserve vertex colour even over opaque unrelated art;
    // their STP classification enters the blended pass for every fragment.
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[5]=0.25f;p[6]=0.5f;p[7]=0.75f;p[11]=64;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    p_glUniform1i(s_native_pass,2);glDrawArrays(GL_TRIANGLES,0,6);
    for(int i=0;i<9;++i){unsigned char p[4];glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        if(abs((int)p[0]-64)>1 || abs((int)p[1]-128)>1 || abs((int)p[2]-191)>1)++failures;}
    // A near opaque road must hide a farther surface drawn afterwards.
    glDepthMask(GL_TRUE);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[5]=p[7]=0;p[6]=1;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    rage_native_passes(6,-1,0,0);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[0]*=2;p[1]*=2;p[2]*=2;p[5]=1;p[6]=0;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    rage_native_passes(6,-1,0,0);
    for(int i=0;i<9;++i){unsigned char p[4];glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        if(p[0]>1 || p[1]<254 || p[2]>1)++failures;}
    // Negative control: suppressing road depth writes must expose the red.
    glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[0]*=0.5f;p[1]*=0.5f;p[2]*=0.5f;p[5]=0;p[6]=1;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    rage_native_passes(6,-1,1,0);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[0]*=2;p[1]*=2;p[2]*=2;p[5]=1;p[6]=0;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    rage_native_passes(6,-1,0,0);
    unsigned char exposed[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,exposed);
    if(exposed[0]<254 || exposed[1]>1)++failures;
    // Coplanar paint stays visible without removing its road's occlusion.
    glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[0]*=0.5f;p[1]*=0.5f;p[2]*=0.5f;p[5]=p[7]=0;p[6]=1;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,0);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[6]=0;p[7]=1;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,1,0);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[0]*=2;p[1]*=2;p[2]*=2;p[5]=1;p[7]=0;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,0);
    for(int i=0;i<9;++i){unsigned char p[4];glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        if(p[0]>1 || p[1]>1 || p[2]<254)++failures;}
    // A coplanar subtractive shadow darkens the road consistently.
    glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[0]*=0.5f;p[1]*=0.5f;p[2]*=0.5f;p[5]=0;p[6]=1;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,0);
    for(int i=0;i<6;++i)vertices[i*16+6]=0.25f;
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,2,0,0);
    for(int i=0;i<9;++i){unsigned char p[4];glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        if(p[0]>1 || abs((int)p[1]-191)>2 || p[2]>1)++failures;}
    // An inset camera's far geometry passes inside its own depth rectangle,
    // while the main camera's near road still occludes outside that inset.
    for(int i=0;i<6;++i)vertices[i*16+6]=1;
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,0);
    const int inset[4]={240,112,271,143};rage_native_clear_depth_rect(fbo,1,inset);glDisable(GL_SCISSOR_TEST);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[0]*=2;p[1]*=2;p[2]*=2;p[5]=1;p[6]=0;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,0);
    unsigned char inside[4],outside[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,inside);glReadPixels(128,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,outside);
    if(inside[0]<254 || inside[1]>1)++failures;if(outside[0]>1 || outside[1]<254)++failures;
    // Background tiles never become occluders, even though their own native
    // sky mesh is closer than a distant world surface in this fixture.
    glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
    for(int i=0;i<6;++i){vertices[i*16+5]=0;vertices[i*16+6]=1;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,1);
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[0]*=2;p[1]*=2;p[2]*=2;p[5]=1;p[6]=0;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,0);
    for(int i=0;i<9;++i){unsigned char p[4];glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        if(p[0]<254 || p[1]>1 || p[2]>1)++failures;}
    // A floor triangle can cross behind the eye. Clip-space depth must stay
    // linear across its vertices; clamping a divided depth per vertex changes
    // the depth of the visible near-road portion.
    glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);
    for(int i=0;i<6;++i){const int c=corners[i];float* p=vertices+i*16;
        p[0]=(c&1)?128:-128;p[1]=-32;p[2]=(c&2)?256:-64;p[5]=p[7]=0;p[6]=1;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,0);
    for(int i=0;i<3;++i){unsigned char p[4];glReadPixels(255,8+24*i,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
        if(p[0]>1 || p[1]<254 || p[2]>1)++failures;}
    // Anisotropic minification keeps samples inside an atlas material at
    // all five menu levels, including footprints much larger than its tile.
    for(int i=0;i<6;++i){const int c=corners[i];float* p=vertices+i*16;
        p[0]=(c&1)?128:-128;p[1]=(c&2)?128:-128;p[2]=256;
        p[3]=(c&1)?2064:16;p[4]=(c&2)?31:16;p[5]=p[6]=p[7]=.5f;p[11]=130;p[12]=p[13]=16;p[14]=p[15]=31;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    for(int level=1;level<=16;level*=2){s_tex_aniso=level;glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);rage_native_passes(6,-1,0,0);
        for(int i=0;i<9;++i){unsigned char p[4];glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
            if(p[0]>1 || p[1]<254 || p[2]>1)++failures;}}
    // At a grazing angle, 1x averages a thin transverse line while 16x
    // preserves it. This catches a menu/uniform that is a visual no-op.
    unsigned char band[16*16*4];for(int y=0;y<16;++y)for(int x=0;x<16;++x){unsigned char* p=band+4*(y*16+x);p[0]=p[2]=0;p[1]=y==7?255:0;p[3]=255;}
    p_glActiveTexture(PSXGL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,colour);glTexSubImage2D(GL_TEXTURE_2D,0,16,16,16,16,GL_RGBA,GL_UNSIGNED_BYTE,band);p_glActiveTexture(PSXGL_TEXTURE0);
    for(int i=0;i<6;++i)vertices[i*16+4]=23.5f;
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    int line[5];for(int level=0;level<5;++level){s_tex_aniso=1<<level;glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);rage_native_passes(6,-1,0,0);
        unsigned char p[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);line[level]=p[1];if(p[0]>1 || p[2]>1 || (level && line[level]<line[level-1]))++failures;}
    if(line[0]>=128 || line[4]<254 || line[4]-line[0]<100)++failures;
    // One-second coverage fade: 0/250/500/750/1000ms must produce
    // 0/25/50/75/100% visibility with no depth-writing transparent pixels.
    for(int i=0;i<6;++i){vertices[i*16+5]=vertices[i*16+7]=0;vertices[i*16+6]=1;vertices[i*16+11]=64;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    const float fading_camera[4]={0,0,0,12};p_glUniform4fv(s_native_camera,1,fading_camera);
    p_glUniform1i(s_native_pass,0);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
    float old_clear[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,old_clear);glClearColor(0,0,0,0);
    for(int step=0;step<=4;++step){
        p_glUniform1f(s_native_now,10+step*0.25f);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,6);
        unsigned char block[64*64*4];glReadPixels(224,96,64,64,GL_RGBA,GL_UNSIGNED_BYTE,block);
        int covered=0;for(int i=0;i<64*64;++i)if(block[i*4+1]>254)++covered;
        if(abs(covered-step*1024)>32)++failures;
    }
    p_glUniform4fv(s_native_camera,1,camera); // Finish the independent fade fixture.
    // Blended wheel pixels just behind an opaque body must remain hidden.
    // A blanket polygon offset would pull the wheel through the body here.
    glDepthMask(GL_TRUE);glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);
    for(int i=0;i<6;++i){const int c=corners[i];float* p=vertices+i*16;memset(p,0,64);
        p[0]=(c&1)?500:-500;p[1]=(c&2)?500:-500;p[2]=1000;p[6]=1;p[11]=64;
        p[14]=p[15]=255;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,0);
    for(int i=0;i<6;++i){float* p=vertices+i*16;const float scale=1000.008f/1000.f;p[0]*=scale;p[1]*=scale;p[2]=1000.008f;p[5]=1;p[6]=0;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,0,0,0);
    for(int i=0;i<9;++i){unsigned char pixel[4];glReadPixels(samples[i][0],samples[i][1],1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        if(pixel[0]>1 || pixel[1]<254 || pixel[2]>1)++failures;}
    // Negative control proves the overlap probe can expose depth pull-through.
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-1,-16);
    apply_psx_blend(0);p_glUniform1i(s_native_pass,2);glDrawArrays(GL_TRIANGLES,0,6);
    {unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);if(pixel[0]<64)++failures;}
    glDisable(GL_POLYGON_OFFSET_FILL);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);
    glClearColor(old_clear[0],old_clear[1],old_clear[2],old_clear[3]);

finish:
    s_scene_views=keep_views;s_tb_scene_terrain=keep_terrain;
    s_tex_aniso=keep_anisotropy;
    if(vbo)p_glDeleteBuffers(1,&vbo);if(vao)p_glDeleteVertexArrays(1,&vao);
    if(fbo)p_glDeleteFramebuffers(1,&fbo);
    if(depth)p_glDeleteRenderbuffers(1,&depth);
    if(colour)glDeleteTextures(1,&colour);if(stp)glDeleteTextures(1,&stp);if(target)glDeleteTextures(1,&target);
    glDepthMask(GL_FALSE);hr_end();
    for(int u=0;u<5;++u){p_glActiveTexture(PSXGL_TEXTURE0+u);glBindTexture(GL_TEXTURE_2D,(GLuint)keep_textures[u]);}
    p_glActiveTexture(PSXGL_TEXTURE0);glViewport(keep_viewport[0],keep_viewport[1],keep_viewport[2],keep_viewport[3]);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_hr_fbo);return failures;
}
