/* GPU-thread-owned native scene backend. Mesh buffers exclude camera state,
 * screen coordinates and every PS1 raster correction. Included by the GL host. */
#include "rage_native_geometry.h"
#include "rage_native_index.h"
#include "rage_native_terrain_pack.h"
#include "rage_track.h"
enum { RAGE_NATIVE_CACHE=512, RAGE_NATIVE_FLOATS=16, RAGE_NATIVE_LOOKUP=2048 };
typedef struct RageNativeBuffer {
    GLuint buffer, indices;
    uint64_t key, touched;
    size_t bytes;
    float* content;
    int pins;
} RageNativeBuffer;
static RageNativeBuffer s_native_meshes[RAGE_NATIVE_CACHE];
static uint16_t s_native_lookup[RAGE_NATIVE_LOOKUP][4];
/* At most 32 MiB of exact source payload plus at most the same GPU storage.
 * Extra cache entries accommodate material/visibility variants, not an
 * unbounded allocation for every camera pose or frame. */
static size_t s_native_retained_bytes;
static GLuint s_native_program,s_native_vao;
static GLint s_native_rows,s_native_projection,s_native_camera,s_native_pass,s_native_xoff,s_native_xhalf,s_native_aniso,s_native_now;
static GLint s_native_inverse_view,s_native_shadow_rows,s_native_shadow_pass,s_native_shadow_enabled,s_native_replace_plate;
static uint64_t s_native_clock;
static unsigned s_native_replay_cursor;
static int s_native_conservative=0;
static int s_native_indexed_draw=0;
static uint16_t s_native_replay_hints[256];
static _Atomic uint64_t s_native_uploads,s_native_hits,s_native_draws,s_native_camera_placements;
static _Atomic uint64_t s_native_view_clears;
static _Atomic uint64_t s_native_region_repairs;
static _Atomic uint64_t s_native_source_bytes,s_native_gpu_bytes,s_native_index_uploads;
static _Atomic uint64_t s_native_terrain_records,s_native_terrain_triangles;
void gl_renderer_native_efficiency(uint64_t out[5]) {
    out[0]=atomic_load(&s_native_source_bytes);out[1]=atomic_load(&s_native_gpu_bytes);
    out[2]=atomic_load(&s_native_index_uploads);out[3]=atomic_load(&s_native_terrain_records);
    out[4]=atomic_load(&s_native_terrain_triangles);
}
uint64_t gl_renderer_native_material_region_count(void){return atomic_load(&s_native_region_repairs);}
uint64_t gl_renderer_native_view_clear_count(void){return atomic_load(&s_native_view_clears);}
static float s_native_vertices[TEXBATCH_MAXV*RAGE_NATIVE_FLOATS];
static int s_native_terrain_poses,s_native_terrain_original[RAGE_SCENE_PALETTE];
static const char* RAGE_NATIVE_VS=
    "#version 330\n"
    "layout(location=0)in vec3 position;layout(location=1)in vec2 uv;layout(location=2)in vec3 colour;\n"
    "layout(location=4)in vec4 region;flat out ivec4 bounds;layout(location=3)in vec4 material;uniform vec4 rows[144];uniform vec4 projection[48];uniform vec4 camera_origin[48];\n"
    "uniform float xoff,xhalf,fade_now;uniform vec4 inverse_view[3],shadow_rows[3];uniform int shadow_pass;out vec3 world_position,shadow_coord;flat out float coverage;smooth out vec2 texcoord;smooth out vec3 tint;flat out ivec3 properties;\n"
    "void main(){int pose=int(material.y);vec4 p=vec4(position-camera_origin[pose].xyz,camera_origin[pose].w>0.5?0:1);\n"
    "vec3 camera=vec3(dot(rows[pose*3],p),dot(rows[pose*3+1],p),dot(rows[pose*3+2],p));\n"
    "world_position=vec3(dot(inverse_view[0],vec4(camera,1)),dot(inverse_view[1],vec4(camera,1)),dot(inverse_view[2],vec4(camera,1)));shadow_coord=vec3(dot(shadow_rows[0],vec4(world_position,1)),dot(shadow_rows[1],vec4(world_position,1)),dot(shadow_rows[2],vec4(world_position,1)));\n"
    "vec4 lens=projection[pose];vec2 h=camera.xy*lens.xy+camera.z*lens.zw;\n"
    "int bias=(int(material.z)>>1)&255;if(bias>=128)bias-=256;float worldBias=float(bias);\n"
    "float depthBias=(int(material.w)&4096)!=0?-2048.0:float(bias);worldBias=(int(material.w)&4096)!=0?depthBias*0.25:worldBias;float legacyBias=depthBias*camera.z/1048576.0;float bound=16.0*worldBias/max(camera.z+worldBias,1.0);\n"
    "float depth=camera.z-16.0+sign(legacyBias)*min(abs(legacyBias),abs(bound));\n"
    "gl_Position=vec4((h.x+camera.z*xoff)/xhalf-camera.z,h.y/256.0-camera.z,depth,camera.z);gl_ClipDistance[0]=camera.z-1.0;\n"
    "if(shadow_pass!=0){gl_Position=vec4(shadow_coord.xy,shadow_coord.z*2.0-1.0,1);gl_ClipDistance[0]=1.0;}shadow_coord.xy=shadow_coord.xy*.5+.5;\n"
    "coverage=abs(camera_origin[pose].w)>2.0?clamp(fade_now-(abs(camera_origin[pose].w)-2.0),0.0,1.0):1.0;bounds=ivec4(region);texcoord=uv;tint=colour;properties=ivec3(int(material.x),int(material.z),int(material.w));}\n";
static const char* RAGE_NATIVE_FS=
    "#version 330\n"
    "uniform sampler2D colours,shadow_map;uniform usampler2D flags;uniform int pass,aniso,shadow_pass,shadow_enabled,replace_plate;in vec3 world_position,shadow_coord;\n"
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
    "float vehicle_visibility(){vec3 dx=dFdx(shadow_coord),dy=dFdy(shadow_coord);vec3 normal=cross(dFdx(world_position),dFdy(world_position));vec3 n=normal*inversesqrt(max(dot(normal,normal),.00000001));float facing=max(dot(n,normalize(vec3(-.34,-1,-.42))),0.0);float bias=mix(.00025,.00008,facing);\n"
    "if(any(lessThanEqual(shadow_coord,vec3(0)))||any(greaterThanEqual(shadow_coord,vec3(1))))return 1.0;float det=dx.x*dy.y-dx.y*dy.x;vec2 slope=abs(det)>.000000000001?vec2(dx.z*dy.y-dy.z*dx.y,dy.z*dx.x-dx.z*dy.x)/det:vec2(0);vec2 pixel=shadow_coord.xy*4096.0-.5,f=fract(pixel),first=(floor(pixel)+.5)/4096.0;float v=0;\n"
    "for(int y=0;y<2;++y)for(int x=0;x<2;++x){vec2 uv=first+vec2(x,y)/4096.0;float depth=texture(shadow_map,uv).r;float receiver=shadow_coord.z+clamp(dot(slope,uv-shadow_coord.xy),-.01,.01);v+=(receiver-bias<=depth?1.0:0.0)*(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);}return v;}\n"
    "void main(){vec2 dx=dFdx(texcoord),dy=dFdy(texcoord);ivec2 p=address(ivec2(floor(texcoord)));\n"
    "if(replace_plate!=0 && (properties.z&8192)!=0)discard;if(shadow_pass!=0 && (properties.z&8192)==0)discard;\n"
    "if((properties.z&512)!=0 && gl_FrontFacing==((properties.z&1024)!=0))discard;\n"
    "// Stable coverage transparency preserves depth and ordering across road/decal layers.\n"
    "if(coverage<1.0 && (coverage<=0.0 || fract(52.9829189*fract(dot(floor(gl_FragCoord.xy),vec2(0.06711056,0.00583715))))>=coverage))discard;\n"
    "bool solid=(properties.z&64)!=0;vec4 c=solid?vec4(1):texel(p);if(c.a<0.5)discard;uint stp=solid?1u:texelFetch(flags,origin()+p,0).r;\n"
    "if((pass==1 && stp!=0u)||(pass==2 && stp==0u))discard;\n"
    "if(shadow_pass!=0){frag=vec4(1);return;}\n"
    "if(!solid && (properties.z&128)!=0 && (properties.z&2048)!=0){\n"
    " vec2 size=vec2(textureSize(colours,0));float edge=0.5;\n"
    " vec2 q=clamp(texcoord,vec2(bounds.xy)+0.5,vec2(bounds.zw)+0.5);if((properties.z&16)!=0)q.x=mod(texcoord.x,256.0);if((properties.z&32)!=0)q.y=mod(texcoord.y,256.0);q=clamp(q,vec2(edge),vec2(256.0-edge));\n"
    " vec4 filtered=textureGrad(colours,(vec2(origin())+q)/size,dx/size,dy/size);if(filtered.a>0.00001)c.rgb=filtered.rgb/filtered.a;\n"
    "}else if(!solid && (properties.z&128)!=0)c.rgb=anisotropic_sample(texcoord,dx,dy,int(stp),c.rgb);\n"
    "else if(!solid && (properties.z&15)!=0){vec2 q=texcoord-0.5,f=fract(q);ivec2 b=ivec2(floor(q));\n"
    "vec4 filtered=mix(mix(texel(b),texel(b+ivec2(1,0)),f.x),mix(texel(b+ivec2(0,1)),texel(b+ivec2(1,1)),f.x),f.y);\n"
    "if(filtered.a>0.00001)c.rgb=filtered.rgb/filtered.a;}\n"
    "vec3 rgb=solid?tint:((properties.y&1)!=0?c.rgb:clamp(c.rgb*tint*2.0,0.0,1.0));if(shadow_enabled!=0 && (properties.z&8192)==0)rgb*=mix(.55,1.0,vehicle_visibility());frag=vec4(rgb,float(stp));}\n";
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
    s_native_inverse_view=p_glGetUniformLocation(s_native_program,"inverse_view");s_native_shadow_rows=p_glGetUniformLocation(s_native_program,"shadow_rows");
    s_native_shadow_pass=p_glGetUniformLocation(s_native_program,"shadow_pass");s_native_shadow_enabled=p_glGetUniformLocation(s_native_program,"shadow_enabled");s_native_replace_plate=p_glGetUniformLocation(s_native_program,"replace_plate");
    p_glUseProgram(s_native_program);
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"colours"),3);
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"flags"),4);
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"shadow_map"),5);
    return 1;
}
static void rage_native_forget(int i) {
    RageNativeBuffer* mesh=&s_native_meshes[i];
    if(mesh->buffer)p_glDeleteBuffers(1,&mesh->buffer);
    if(mesh->indices)p_glDeleteBuffers(1,&mesh->indices);
    s_native_retained_bytes-=mesh->bytes;
    free(mesh->content);memset(mesh,0,sizeof(*mesh));
}
static RageNativeBuffer* rage_native_buffer(float* vertices,size_t bytes) {

    ++s_native_clock;
    // Replay order is usually identical across intermediate frames. Verify
    // the expected retained mesh directly with the vectorized byte compare,
    // avoiding even a fast full-mesh hash. An eviction, changed clipping or
    // different ordering falls back to the normal content-addressed lookup.
    const unsigned hint=s_fg_drawing?s_native_replay_cursor++:256;
    if(hint<256 && s_native_replay_hints[hint]){
        RageNativeBuffer* mesh=&s_native_meshes[s_native_replay_hints[hint]-1];
        if(mesh->buffer && mesh->bytes==bytes && !memcmp(mesh->content,vertices,bytes)){
            mesh->touched=s_native_clock;atomic_fetch_add(&s_native_hits,1);return mesh;
        }
    }
    const uint64_t key=rage_native_geometry_key(vertices,bytes);
    const size_t bucket=(size_t)(key^(key>>32))&(RAGE_NATIVE_LOOKUP-1);
    for(int way=0;way<4;++way)if(s_native_lookup[bucket][way]){
        const int i=s_native_lookup[bucket][way]-1;
        RageNativeBuffer* mesh=&s_native_meshes[i];
        if(mesh->buffer && mesh->key==key && mesh->bytes==bytes && !memcmp(mesh->content,vertices,bytes)){
            if(hint<256)s_native_replay_hints[hint]=(uint16_t)(i+1);
            mesh->touched=s_native_clock;atomic_fetch_add(&s_native_hits,1);return mesh;
        }
    }
    int victim=-1;
    for(int i=0;i<RAGE_NATIVE_CACHE;++i) {
        RageNativeBuffer* mesh=&s_native_meshes[i];
        if(mesh->buffer && mesh->key==key && mesh->bytes==bytes && !memcmp(mesh->content,vertices,bytes)) {
            if(hint<256)s_native_replay_hints[hint]=(uint16_t)(i+1);
            memmove(s_native_lookup[bucket]+1,s_native_lookup[bucket],3*sizeof(uint16_t));s_native_lookup[bucket][0]=(uint16_t)(i+1);
            mesh->touched=s_native_clock;atomic_fetch_add(&s_native_hits,1);return mesh;
        }
        if(!mesh->pins && (victim<0 || !mesh->buffer || mesh->touched<s_native_meshes[victim].touched))victim=i;
    }
    if(victim<0){rage_native_scene_flush();return rage_native_buffer(vertices,bytes);}
    if(bytes>(32u<<20))return NULL;
    rage_native_forget(victim);
    while(s_native_retained_bytes+bytes>(32u<<20)){
        int oldest=-1;
        for(int i=0;i<RAGE_NATIVE_CACHE;++i)if(s_native_meshes[i].buffer && !s_native_meshes[i].pins &&
            (oldest<0 || s_native_meshes[i].touched<s_native_meshes[oldest].touched))oldest=i;
        if(oldest<0){rage_native_scene_flush();return rage_native_buffer(vertices,bytes);}
        rage_native_forget(oldest);
    }
    RageNativeBuffer* mesh=&s_native_meshes[victim];
    float* content=(float*)realloc(mesh->content,bytes);
    if(!content)return 0;
    mesh->content=content;memcpy(content,vertices,bytes);
    // Keep the exact input as cache identity; repair filtering bounds only
    // on upload, so intermediate frames reuse the already prepared mesh.
    atomic_fetch_add(&s_native_region_repairs,rage_native_material_regions(vertices,bytes/(RAGE_NATIVE_FLOATS*sizeof(float))));
    if(!mesh->buffer)p_glGenBuffers(1,&mesh->buffer);
    /* Bind the native VAO before changing its element buffer. Other paths
     * own separate VAOs, including the compatibility renderer and probes. */
    p_glBindVertexArray(s_native_vao);
    p_glBindBuffer(PSXGL_ARRAY_BUFFER,mesh->buffer);
    RageNativeIndex pack;
    const char* indexed=getenv("RAGE_INDEXED_GEOMETRY");
    if((!indexed || indexed[0]!='0') && rage_native_index_build(&pack,vertices,bytes/64)){
        atomic_fetch_add(&s_native_gpu_bytes,pack.count*64+bytes/16);
        atomic_fetch_add(&s_native_index_uploads,1);
        p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)(pack.count*64),pack.vertices,GL_STATIC_DRAW);
        if(!mesh->indices)p_glGenBuffers(1,&mesh->indices);
        p_glBindBuffer(0x8893 /* GL_ELEMENT_ARRAY_BUFFER */,mesh->indices);
        p_glBufferData(0x8893,(ptrdiff_t)(bytes/16),pack.indices,GL_STATIC_DRAW);
        rage_native_index_release(&pack);
    }else{
        atomic_fetch_add(&s_native_gpu_bytes,bytes);
        p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)bytes,vertices,GL_STATIC_DRAW);
        if(mesh->indices){p_glDeleteBuffers(1,&mesh->indices);mesh->indices=0;}
    }
    atomic_fetch_add(&s_native_source_bytes,bytes);
    mesh->bytes=bytes;mesh->key=key;mesh->touched=s_native_clock;
    s_native_retained_bytes+=bytes;
    memmove(s_native_lookup[bucket]+1,s_native_lookup[bucket],3*sizeof(uint16_t));s_native_lookup[bucket][0]=(uint16_t)(victim+1);
    if(hint<256)s_native_replay_hints[hint]=(uint16_t)(victim+1);
    atomic_fetch_add(&s_native_uploads,1);return mesh;
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
    // Separate the one-unit near clip from the established depth mapping.
    // Depth clamp preserves distant precision and coplanar-layer behaviour.
    glEnable(0x864F /* GL_DEPTH_CLAMP */);glEnable(0x3000 /* GL_CLIP_DISTANCE0 */);
    if(s_native_conservative){if(s_tb_scene_terrain==2 && semi<0 && !decal && !background)glEnable(0x9346);else glDisable(0x9346);}
    if(background)glDisable(GL_DEPTH_TEST);else glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);
    // Face-specific bounded bias is applied by the native vertex shader.
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_BLEND);glDepthMask(decal || background?GL_FALSE:GL_TRUE);
    p_glUniform1i(s_native_pass,semi<0?0:1);
    if(s_native_indexed_draw)glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,0);else glDrawArrays(GL_TRIANGLES,0,count);
    if(semi>=0){
        apply_psx_blend(semi);glDepthMask(GL_FALSE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        p_glUniform1i(s_native_pass,2);
        if(s_native_indexed_draw)glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,0);else glDrawArrays(GL_TRIANGLES,0,count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glDisable(0x3000);glDisable(0x864F);
    if(s_native_conservative)glDisable(0x9346);
    glDisable(GL_STENCIL_TEST);glStencilMask(0x01);
    atomic_fetch_add(&s_native_draws,1);
}
#include "rage_native_scene_queue.h"
static int rage_native_draw_batch(int count,int semi,int decal,int background) {
    if(!rage_native_init())return 0;
    int mapping[RAGE_SCENE_PALETTE],poses=0;
    float rows[RAGE_SCENE_PALETTE*12],lens[RAGE_SCENE_PALETTE*4],camera[RAGE_SCENE_PALETTE*4];
    for(int i=0;i<RAGE_SCENE_PALETTE;++i)mapping[i]=-1;
    if(s_tb_scene_terrain){
        /* Terrain was packed directly at admission. Only its small camera
         * palette changes between generated frames; avoid staging 32 floats
         * per vertex and immediately converting them to sixteen again. */
        poses=s_native_terrain_poses;
        for(int p=0;p<poses;++p){const int original=s_native_terrain_original[p];
            memcpy(rows+12*p,s_scene_batch_rows+12*original,48);
            memcpy(lens+4*p,s_scene_batch_projection+4*original,16);
            memcpy(camera+4*p,s_scene_batch_camera+4*original,16);
        }
    }else for(int i=0;i<count;++i) {
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
    RageNativeBuffer* mesh=rage_native_buffer(s_native_vertices,(size_t)count*RAGE_NATIVE_FLOATS*sizeof(float));
    if(!mesh){
        /* Allocation failure retains the original compatibility fallback.
         * Reconstruct its staging payload only on this exceptional path. */
        if(s_tb_scene_terrain)for(int i=0;i<count;++i){
            const float* in=s_native_vertices+16*i;float* out=s_tb+TEXV*i;memset(out,0,TEXV*sizeof(float));
            memcpy(out+27,in,12);memcpy(out+2,in+3,8);memcpy(out+4,in+5,12);memcpy(out+14,in+12,16);
            out[13]=(float)((int)in[10]&1);out[30]=(float)(s_native_terrain_original[(int)in[9]]+1);out[31]=in[8]+1;
            const unsigned flags=(unsigned)in[11];out[26]=(float)((flags&15)|((flags&128)?16:0)|((flags&16)?64:0)|((flags&32)?128:0)|
                ((flags&16384)?8388608:0)|((flags&8192)?4194304:0)|((flags&4096)?2097152:0)|((flags&512)?262144:0)|((flags&1024)?524288:0)|((flags&2048)?1048576:0)|(((unsigned)in[10]>>1)<<10));
        }
        return 0;
    }
    int caster=0,player_pose=-1;
    if(!decal && !background)for(int i=0;i<count;++i)if((int)s_native_vertices[16*i+11]&8192){caster=1;break;}
    if(caster)for(int i=0;i<count;++i)if((int)s_native_vertices[16*i+11]&16384){player_pose=(int)s_native_vertices[16*i+9];break;}
    return rage_native_scene_queue(mesh,count,semi,decal,background,poses,rows,lens,camera,caster,player_pose);
}

static void rage_native_release(void) {
    rage_vehicle_shadow_release();
    for(int i=0;i<RAGE_NATIVE_CACHE;++i){
        if(s_native_meshes[i].buffer)p_glDeleteBuffers(1,&s_native_meshes[i].buffer);
        if(s_native_meshes[i].indices)p_glDeleteBuffers(1,&s_native_meshes[i].indices);
        free(s_native_meshes[i].content);
    }
    memset(s_native_meshes,0,sizeof s_native_meshes);s_native_clock=0;
    memset(s_native_lookup,0,sizeof s_native_lookup);s_native_retained_bytes=0;
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
    flush_cpu_upload();flush_flat_batch();flush_tex_batch();rage_native_scene_flush();
    if(!rage_native_init())return -1;
    unsigned char* pixels=(unsigned char*)calloc(256*256,4);
    unsigned char* flags=(unsigned char*)calloc(256*256,1);
    if(!pixels || !flags){free(pixels);free(flags);return -1;}
    const int keep_anisotropy=s_tex_aniso;
    const RageNativeViews keep_views=s_scene_views;s_scene_views.count=0;
    const int keep_terrain=s_tb_scene_terrain;s_tb_scene_terrain=0;
    GLint keep_textures[6],keep_viewport[4];glGetIntegerv(GL_VIEWPORT,keep_viewport);
    for(int u=0;u<6;++u){p_glActiveTexture(PSXGL_TEXTURE0+u);glGetIntegerv(GL_TEXTURE_BINDING_2D,keep_textures+u);}
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
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"shadow_enabled"),0);p_glUniform1i(p_glGetUniformLocation(s_native_program,"replace_plate"),0);p_glUniform1i(p_glGetUniformLocation(s_native_program,"shadow_pass"),0);
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
    /* Exercise actual indexed GPU submission, not only CPU reconstruction.
     * Compare every pixel in the drawn region with the expanded draw. */
    {RageNativeIndex pack;
        if(!rage_native_index_build(&pack,vertices,6))++failures;
        else{
            unsigned char before[256*256*4],after[256*256*4];GLuint indices=0;
            glReadPixels(128,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,before);
            p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)(pack.count*64),pack.vertices,GL_STATIC_DRAW);
            p_glGenBuffers(1,&indices);p_glBindBuffer(0x8893,indices);
            p_glBufferData(0x8893,6*sizeof(uint32_t),pack.indices,GL_STATIC_DRAW);
            float clear_colour[4];glGetFloatv(GL_COLOR_CLEAR_VALUE,clear_colour);
            glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
            glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,0);
            glReadPixels(128,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,after);
            glClearColor(clear_colour[0],clear_colour[1],clear_colour[2],clear_colour[3]);
            if(memcmp(before,after,sizeof before))++failures;
            p_glDeleteBuffers(1,&indices);rage_native_index_release(&pack);
            p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
        }
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
    // Geometry between the new one-unit near clip and the former eight-unit
    // plane must draw, while geometry behind the new plane remains clipped.
    for(int i=0;i<6;++i){const int c=corners[i];float* p=vertices+i*16;memset(p,0,64);
        p[0]=(c&1)?1:-1;p[1]=(c&2)?1:-1;p[2]=2;p[6]=1;p[11]=64;p[14]=p[15]=255;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
    glDepthMask(GL_TRUE);glClear(GL_DEPTH_BUFFER_BIT);rage_native_passes(6,-1,0,0);
    {unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);if(pixel[0]>1 || pixel[1]<254 || pixel[2]>1)++failures;}
    for(int i=0;i<6;++i){float* p=vertices+i*16;p[0]*=.25f;p[1]*=.25f;p[2]=.5f;p[5]=1;p[6]=0;}
    p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);rage_native_passes(6,-1,0,0);
    {unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);if(pixel[0]>1 || pixel[1]<254 || pixel[2]>1)++failures;}
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
    // Actual-driver vehicle light pass: car geometry writes light-space depth,
    // non-casters do not, and receivers distinguish shadow, sun and contact bias.
    {float shadow_rows[12]={1.f/128,0,0,0,0,1.f/128,0,0,0,0,0,.25f};
        const GLint shadow_pass=p_glGetUniformLocation(s_native_program,"shadow_pass");
        const GLint enabled=p_glGetUniformLocation(s_native_program,"shadow_enabled");
        p_glUniform4fv(p_glGetUniformLocation(s_native_program,"inverse_view"),3,rows);
        p_glUniform4fv(p_glGetUniformLocation(s_native_program,"shadow_rows"),3,shadow_rows);
        for(int i=0;i<6;++i){const int c=corners[i];float* p=vertices+i*16;memset(p,0,64);
            p[0]=(c&1)?128:-128;p[1]=(c&2)?128:-128;p[2]=256;p[6]=1;p[11]=64|8192;p[14]=p[15]=255;}
        p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
        p_glUniform1i(shadow_pass,1);p_glUniform1i(s_native_pass,0);glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);
        glClearDepth(1);glClear(GL_DEPTH_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,6);
        float light_depth=0;glReadPixels(255,128,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&light_depth);if(fabsf(light_depth-.25f)>.00001f)++failures;
        for(int i=0;i<6;++i)vertices[i*16+11]=64;
        p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
        glClear(GL_DEPTH_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,6);glReadPixels(255,128,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&light_depth);
        if(light_depth<.99999f)++failures;
        p_glUniform1i(shadow_pass,0);p_glUniform1i(enabled,1);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
        memset(shadow_rows,0,sizeof shadow_rows);shadow_rows[11]=.5f;
        p_glUniform4fv(p_glGetUniformLocation(s_native_program,"shadow_rows"),3,shadow_rows);
        GLuint probe_depth=0;p_glActiveTexture(PSXGL_TEXTURE0+5);glGenTextures(1,&probe_depth);glBindTexture(GL_TEXTURE_2D,probe_depth);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        const float depths[3]={.25f,.75f,.49999f};
        for(int step=0;step<3;++step){float block[4]={depths[step],depths[step],depths[step],depths[step]};
            glTexImage2D(GL_TEXTURE_2D,0,0x81A6,2,2,0,GL_DEPTH_COMPONENT,GL_FLOAT,block);glDrawArrays(GL_TRIANGLES,0,6);
            unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            if(pixel[0]>1 || pixel[2]>1 || abs((int)pixel[1]-(step?255:140))>2)++failures;}
        glDeleteTextures(1,&probe_depth);p_glActiveTexture(PSXGL_TEXTURE0);p_glUniform1i(enabled,0);
    }
    glClearColor(old_clear[0],old_clear[1],old_clear[2],old_clear[3]);

finish:
    s_scene_views=keep_views;s_tb_scene_terrain=keep_terrain;
    s_tex_aniso=keep_anisotropy;
    if(vbo)p_glDeleteBuffers(1,&vbo);if(vao)p_glDeleteVertexArrays(1,&vao);
    if(fbo)p_glDeleteFramebuffers(1,&fbo);
    if(depth)p_glDeleteRenderbuffers(1,&depth);
    if(colour)glDeleteTextures(1,&colour);if(stp)glDeleteTextures(1,&stp);if(target)glDeleteTextures(1,&target);
    glDepthMask(GL_FALSE);hr_end();
    for(int u=0;u<6;++u){p_glActiveTexture(PSXGL_TEXTURE0+u);glBindTexture(GL_TEXTURE_2D,(GLuint)keep_textures[u]);}
    p_glActiveTexture(PSXGL_TEXTURE0);glViewport(keep_viewport[0],keep_viewport[1],keep_viewport[2],keep_viewport[3]);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_hr_fbo);return failures;
}
