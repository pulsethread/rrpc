/* GPU-thread-owned native scene backend. Mesh buffers exclude camera state,
 * screen coordinates and every PS1 raster correction. Included by the GL host. */
#include "rage_native_geometry.h"
#include "rage_native_index.h"
#include "rage_native_terrain_pack.h"
#include "rage_track.h"
enum { RAGE_NATIVE_CACHE=2048, RAGE_NATIVE_FLOATS=16, RAGE_NATIVE_LOOKUP=8192 };
typedef struct RageNativeBuffer {
    GLuint buffer, indices,shadow_indices,vao,expanded;
    int shadow_count;
    uint64_t key, touched;
    size_t bytes;
    float* content;
    int pins;
    RageNativeMeshTags tags;
    uint64_t serial;
} RageNativeBuffer;
static RageNativeBuffer s_native_meshes[RAGE_NATIVE_CACHE];
static uint16_t s_native_lookup[RAGE_NATIVE_LOOKUP][4];
/* At most 32 MiB of exact source payload. Indexed buffers plus expanded GPU
 * gather sources use at most twice that amount; the orphaned gather stream
 * is separately capped at 32 MiB (drivers may retain in-flight allocations).
 * Extra cache entries accommodate material/visibility variants, not an
 * unbounded allocation for every camera pose or frame. */
static size_t s_native_retained_bytes;
static _Atomic uint64_t s_native_live_cache_bytes;
uint64_t rage_native_cache_usage(void){return atomic_load(&s_native_live_cache_bytes);}
static GLuint s_native_program,s_native_vao;
static GLint s_native_rows,s_native_projection,s_native_camera,s_native_pass,s_native_xoff,s_native_xhalf,s_native_aniso,s_native_now;
static GLint s_native_inverse_view,s_native_shadow_rows,s_native_shadow_pass,s_native_shadow_enabled,s_native_replace_plate;
static GLint s_native_directional,s_native_far_rows,s_native_far_enabled,s_native_far_bias,s_native_near_bias,s_native_shadow_filter;
static GLint s_native_light_direction,s_native_light_ambient,s_native_light_diffuse,s_native_environment_shadow;
static GLint s_native_spot_count,s_native_spot_position,s_native_spot_direction,s_native_spot_colour;
static GLint s_native_lens_count,s_native_lens_bounds,s_native_lens_position,s_native_lens_colour;
static uint64_t s_native_clock;
static uint64_t s_native_serial;
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
/* Differentiate camera-relative positions for normals: adding the distant
 * world origin first loses precision on small building triangles. */
static const char* RAGE_NATIVE_VS=
    "#version 330\n"
    "layout(location=0)in vec3 position;layout(location=1)in vec2 uv;layout(location=2)in vec3 colour;\n"
    "layout(location=4)in vec4 region;flat out ivec4 bounds;layout(location=3)in vec4 material;uniform vec4 rows[144];uniform vec4 projection[48];uniform vec4 camera_origin[48];\n"
    "uniform float xoff,xhalf,fade_now;uniform vec4 inverse_view[3],shadow_rows[3],far_shadow_rows[3];uniform int shadow_pass;out vec3 world_position,shadow_coord,far_shadow_coord,lamp_local,relative_position;flat out int lamp_pose;flat out float coverage;smooth out vec2 texcoord;smooth out vec3 tint;flat out ivec3 properties;\n"
    "void main(){int pose=int(material.y);lamp_pose=pose;lamp_local=position;vec4 p=vec4(position-camera_origin[pose].xyz,camera_origin[pose].w>0.5?0:1);\n"
    "vec3 camera=vec3(dot(rows[pose*3],p),dot(rows[pose*3+1],p),dot(rows[pose*3+2],p));\n"
    "relative_position=vec3(dot(inverse_view[0].xyz,camera),dot(inverse_view[1].xyz,camera),dot(inverse_view[2].xyz,camera));world_position=vec3(dot(inverse_view[0],vec4(camera,1)),dot(inverse_view[1],vec4(camera,1)),dot(inverse_view[2],vec4(camera,1)));shadow_coord=vec3(dot(shadow_rows[0],vec4(world_position,1)),dot(shadow_rows[1],vec4(world_position,1)),dot(shadow_rows[2],vec4(world_position,1)));\n"
    "far_shadow_coord=vec3(dot(far_shadow_rows[0],vec4(world_position,1)),dot(far_shadow_rows[1],vec4(world_position,1)),dot(far_shadow_rows[2],vec4(world_position,1)));far_shadow_coord.xy=far_shadow_coord.xy*.5+.5;\n"
    "vec4 lens=projection[pose];vec2 h=camera.xy*lens.xy+camera.z*lens.zw;\n"
    "int bias=(int(material.z)>>1)&255;if(bias>=128)bias-=256;float worldBias=float(bias);\n"
    "float depthBias=(int(material.w)&4096)!=0?-2048.0:float(bias);worldBias=(int(material.w)&4096)!=0?depthBias*0.25:worldBias;float legacyBias=depthBias*camera.z/1048576.0;float bound=16.0*worldBias/max(camera.z+worldBias,1.0);\n"
    "float depth=camera.z-16.0+sign(legacyBias)*min(abs(legacyBias),abs(bound));\n"
    "gl_Position=vec4((h.x+camera.z*xoff)/xhalf-camera.z,h.y/256.0-camera.z,depth,camera.z);gl_ClipDistance[0]=camera.z-1.0;\n"
    "if(shadow_pass!=0){gl_Position=vec4(shadow_coord.xy,shadow_coord.z*2.0-1.0,1);gl_ClipDistance[0]=1.0;}shadow_coord.xy=shadow_coord.xy*.5+.5;\n"
    "coverage=abs(camera_origin[pose].w)>2.0?clamp(fade_now-(abs(camera_origin[pose].w)-2.0),0.0,1.0):1.0;bounds=ivec4(region);texcoord=uv;tint=colour;properties=ivec3(int(material.x),int(material.z),int(material.w));}\n";
static const char* RAGE_NATIVE_FS=
    "#version 330\n"
    "uniform sampler2D colours,shadow_map,far_shadow_map;uniform int far_shadow_enabled,shadow_filter;uniform float far_shadow_bias,near_shadow_bias;uniform usampler2D flags;uniform int pass,aniso,shadow_pass,shadow_enabled,replace_plate,directional;in vec3 world_position,shadow_coord,far_shadow_coord,relative_position;\n"
    "uniform int spot_count,environment_shadow;uniform vec4 light_direction,light_ambient,light_diffuse;uniform vec4 spot_position[16],spot_direction[16],spot_colour[16];\n"
    "in vec3 lamp_local;flat in int lamp_pose;uniform int lens_count;uniform vec4 lens_bounds[64],lens_position[64],lens_colour[64];\n"
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
    "vec3 local_lights(vec3 n){vec3 sum=vec3(0);for(int i=0;i<spot_count;++i){vec3 delta=world_position-spot_position[i].xyz;float d2=dot(delta,delta),range=spot_position[i].w; if(d2>=range*range || d2<.000001)continue;vec3 d=delta*inversesqrt(d2);float cone=dot(d,spot_direction[i].xyz);if(cone<=spot_direction[i].w)continue;float cutoff=1.0;if(range>2000.0){if(delta.y<0.0)continue;cutoff=smoothstep(0.0,32.0,delta.y);}float edge=cutoff*smoothstep(spot_direction[i].w,spot_colour[i].w,cone),falloff=1.0-d2/(range*range);sum+=spot_colour[i].rgb*edge*falloff*falloff*abs(dot(n,-d));}return sum/(vec3(1)+sum);}\n"
    "vec3 lamp_emission(){vec3 emission=vec3(0);for(int i=0;i<lens_count;++i){if(int(lens_position[i].w)!=lamp_pose)continue;vec3 delta=lamp_local-lens_position[i].xyz;if(dot(delta,delta)>6400.0)continue;vec4 b=lens_bounds[i];if(any(lessThan(texcoord,b.xy))||any(greaterThan(texcoord,b.zw)))continue;if(fract(lens_position[i].w)>.25){vec2 q=(texcoord-(b.xy+b.zw)*.5)/max((b.zw-b.xy)*.5,vec2(.00001));if(dot(q,q)>1.0)continue;}emission=max(emission,lens_colour[i].rgb);}return emission;}\n"
    "float cascade_visibility(sampler2D map,vec3 coord,vec3 dx,vec3 dy,float facing,float scale){float bias=mix(.00025,.00008,facing)*scale;\n"
    "if(any(lessThanEqual(coord,vec3(0)))||any(greaterThanEqual(coord,vec3(1))))return 1.0;float det=dx.x*dy.y-dx.y*dy.x;vec2 slope=abs(det)>.000000000001?vec2(dx.z*dy.y-dy.z*dx.y,dy.z*dx.x-dx.z*dy.x)/det:vec2(0);vec2 size=vec2(textureSize(map,0));float v=0;int taps=shadow_filter>0?4:1;\n"
    "for(int tap=0;tap<taps;++tap){vec2 offset=taps>1?vec2((tap&1)!=0?.5:-.5,(tap&2)!=0?.5:-.5):vec2(0);vec2 pixel=coord.xy*size-.5+offset,f=fract(pixel),first=(floor(pixel)+.5)/size;for(int y=0;y<2;++y)for(int x=0;x<2;++x){vec2 uv=first+vec2(x,y)/size;float depth=texture(map,uv).r;float receiver=coord.z+clamp(dot(slope,uv-coord.xy),-.01,.01);v+=(receiver-bias<=depth?1.0:0.0)*(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);}}v/=float(taps);float edge=min(min(coord.x,1.0-coord.x),min(coord.y,1.0-coord.y));return mix(1.0,v,smoothstep(0.0,.08,edge));}\n"
    "float vehicle_visibility(vec3 dx,vec3 dy,float facing){float nearValue=cascade_visibility(shadow_map,shadow_coord,dx,dy,facing,near_shadow_bias);if(far_shadow_enabled==0)return nearValue;float farValue=cascade_visibility(far_shadow_map,far_shadow_coord,dFdx(far_shadow_coord),dFdy(far_shadow_coord),facing,far_shadow_bias);float edge=min(min(shadow_coord.x,1.0-shadow_coord.x),min(shadow_coord.y,1.0-shadow_coord.y));float weight=smoothstep(.05,.18,edge);if(shadow_coord.z<=0.0 || shadow_coord.z>=1.0)weight=0.0;return mix(farValue,nearValue,weight); }\n"
    "void main(){if(shadow_pass!=0 && (properties.z&32768)!=0)discard;vec2 dx=dFdx(texcoord),dy=dFdy(texcoord);vec3 sx=dFdx(shadow_coord),sy=dFdy(shadow_coord);vec3 normal=cross(dFdx(relative_position),dFdy(relative_position));vec3 n=normal*inversesqrt(max(dot(normal,normal),.00000001));float facing=abs(dot(n,normalize(light_direction.xyz)));ivec2 p=address(ivec2(floor(texcoord)));\n"
    "if(replace_plate!=0 && (properties.z&8192)!=0)discard;if(shadow_pass!=0 && environment_shadow==0 && (properties.z&8192)==0)discard;\n"
    "if(shadow_pass==0 && (properties.z&512)!=0 && gl_FrontFacing==((properties.z&1024)!=0))discard;\n"
    "// Stable coverage transparency preserves depth and ordering across road/decal layers.\n"
    "if(shadow_pass==0 && coverage<1.0 && (coverage<=0.0 || fract(52.9829189*fract(dot(floor(gl_FragCoord.xy),vec2(0.06711056,0.00583715))))>=coverage))discard;\n"
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
    "vec3 rgb=solid?tint:((properties.y&1)!=0?c.rgb:clamp(c.rgb*tint*2.0,0.0,1.0));float visibility=1.0;if(shadow_enabled!=0 && ((properties.z&8192)!=0 || abs(n.y)>.55))visibility=vehicle_visibility(sx,sy,facing);\n"
    "if((properties.z&32768)!=0){frag=vec4(rgb,float(stp));return;}vec3 base=rgb;if(directional!=0){vec3 ambient=light_ambient.rgb,diffuse=light_diffuse.rgb*facing;vec3 lit=ambient+diffuse;vec3 reference=vec3(.35,.35,.38)+vec3(.85,.85,.80)*inversesqrt(1.292);vec3 shade=(ambient+diffuse*mix(.16,1.0,visibility))/max(lit,vec3(.0001));rgb*=mix(vec3(1),clamp(lit/reference,vec3(.18),vec3(1.15)),.7)*shade;}else rgb*=mix(.55,1.0,visibility);if(spot_count>0)rgb+=base*local_lights(n);if(lens_count>0 && (properties.z&8192)!=0){vec3 emission=lamp_emission();rgb+=(vec3(1)-clamp(rgb,0.0,1.0))*emission/(vec3(1)+emission);}frag=vec4(rgb,float(stp));}\n";
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
    s_native_shadow_filter=p_glGetUniformLocation(s_native_program,"shadow_filter");
    s_native_far_rows=p_glGetUniformLocation(s_native_program,"far_shadow_rows");
    s_native_far_enabled=p_glGetUniformLocation(s_native_program,"far_shadow_enabled");
    s_native_near_bias=p_glGetUniformLocation(s_native_program,"near_shadow_bias");
    s_native_far_bias=p_glGetUniformLocation(s_native_program,"far_shadow_bias");
    s_native_directional=p_glGetUniformLocation(s_native_program,"directional");
    s_native_light_direction=p_glGetUniformLocation(s_native_program,"light_direction");
    s_native_light_ambient=p_glGetUniformLocation(s_native_program,"light_ambient");s_native_light_diffuse=p_glGetUniformLocation(s_native_program,"light_diffuse");
    s_native_environment_shadow=p_glGetUniformLocation(s_native_program,"environment_shadow");
    p_glUseProgram(s_native_program);
    p_glUniform1f(s_native_near_bias,1.f);
    {const float direction[4]={-.34f,-1,-.42f,0},ambient[4]={.35f,.35f,.38f,0},diffuse[4]={.85f,.85f,.80f,0};
        p_glUniform4fv(s_native_light_direction,1,direction);p_glUniform4fv(s_native_light_ambient,1,ambient);p_glUniform4fv(s_native_light_diffuse,1,diffuse);}
    s_native_spot_count=p_glGetUniformLocation(s_native_program,"spot_count");s_native_spot_position=p_glGetUniformLocation(s_native_program,"spot_position");s_native_spot_direction=p_glGetUniformLocation(s_native_program,"spot_direction");s_native_spot_colour=p_glGetUniformLocation(s_native_program,"spot_colour");
    s_native_lens_count=p_glGetUniformLocation(s_native_program,"lens_count");s_native_lens_bounds=p_glGetUniformLocation(s_native_program,"lens_bounds");s_native_lens_position=p_glGetUniformLocation(s_native_program,"lens_position");s_native_lens_colour=p_glGetUniformLocation(s_native_program,"lens_colour");
    p_glUseProgram(s_native_program);
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"colours"),3);
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"flags"),4);
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"shadow_map"),5);
    p_glUniform1i(p_glGetUniformLocation(s_native_program,"far_shadow_map"),6);
    return 1;
}
static void rage_native_forget(int i) {
    RageNativeBuffer* mesh=&s_native_meshes[i];
    if(mesh->buffer)p_glDeleteBuffers(1,&mesh->buffer);
    if(mesh->indices)p_glDeleteBuffers(1,&mesh->indices);
    if(mesh->shadow_indices)p_glDeleteBuffers(1,&mesh->shadow_indices);
    if(mesh->vao)p_glDeleteVertexArrays(1,&mesh->vao);
    if(mesh->expanded)p_glDeleteBuffers(1,&mesh->expanded);
    s_native_retained_bytes-=mesh->bytes;atomic_store(&s_native_live_cache_bytes,s_native_retained_bytes);
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
    mesh->tags=rage_native_mesh_tags(vertices,bytes/(RAGE_NATIVE_FLOATS*sizeof(float)));
    // Keep the exact input as cache identity; repair filtering bounds only
    // on upload, so intermediate frames reuse the already prepared mesh.
    atomic_fetch_add(&s_native_region_repairs,rage_native_material_regions(vertices,bytes/(RAGE_NATIVE_FLOATS*sizeof(float))));
    if(!mesh->buffer)p_glGenBuffers(1,&mesh->buffer);
    /* Bind the native VAO before changing its element buffer. Other paths
     * own separate VAOs, including the compatibility renderer and probes. */
    p_glBindVertexArray(s_native_vao);
    p_glBindBuffer(PSXGL_ARRAY_BUFFER,mesh->buffer);
    RageNativeIndex pack;
    memset(&pack,0,sizeof pack);
    const char* indexed=getenv("RAGE_INDEXED_GEOMETRY");
    if((!indexed || indexed[0]!='0') && rage_native_index_build(&pack,vertices,bytes/64)){
        atomic_fetch_add(&s_native_gpu_bytes,pack.count*64+bytes/16);
        atomic_fetch_add(&s_native_index_uploads,1);
        p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)(pack.count*64),pack.vertices,GL_STATIC_DRAW);
        if(!mesh->indices)p_glGenBuffers(1,&mesh->indices);
        p_glBindBuffer(0x8893 /* GL_ELEMENT_ARRAY_BUFFER */,mesh->indices);
        p_glBufferData(0x8893,(ptrdiff_t)(bytes/16),pack.indices,GL_STATIC_DRAW);
    }else{
        atomic_fetch_add(&s_native_gpu_bytes,bytes);
        p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)bytes,vertices,GL_STATIC_DRAW);
        if(mesh->indices){p_glDeleteBuffers(1,&mesh->indices);mesh->indices=0;}
    }
    /* Like the reference's caster draw list, reject non-casters before vertex
     * processing rather than rasterising an entire mixed batch and discarding
     * its terrain fragments in the 4096-square light pass. */
    const size_t caster_count=rage_native_caster_indices(vertices,bytes/64,NULL,NULL);
    uint32_t* casters=caster_count && caster_count<bytes/64?(uint32_t*)malloc(caster_count*4):NULL;
    if(casters){const size_t count=rage_native_caster_indices(vertices,bytes/64,pack.indices,casters);
        p_glGenBuffers(1,&mesh->shadow_indices);
        p_glBindBuffer(0x8893,mesh->shadow_indices);p_glBufferData(0x8893,(ptrdiff_t)(count*4),casters,GL_STATIC_DRAW);
        mesh->shadow_count=(int)count;atomic_fetch_add(&s_native_gpu_bytes,count*4);
        free(casters);}
    rage_native_index_release(&pack);
    /* Retain the vertex input description with the GPU mesh, as in the
     * reference native mesh backend. Camera/palette changes do not change
     * this layout; rebuilding ten attribute states per draw is unnecessary. */
    p_glGenVertexArrays(1,&mesh->vao);p_glBindVertexArray(mesh->vao);
    p_glBindBuffer(PSXGL_ARRAY_BUFFER,mesh->buffer);p_glBindBuffer(0x8893,mesh->indices);
    const int sizes[5]={3,2,3,4,4},offsets[5]={0,3,5,8,12};
    for(int i=0;i<5;++i){p_glEnableVertexAttribArray(i);p_glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,64,(void*)(size_t)(offsets[i]*4));}
    atomic_fetch_add(&s_native_source_bytes,bytes);
    mesh->bytes=bytes;mesh->key=key;mesh->touched=s_native_clock;
    mesh->serial=++s_native_serial;
    s_native_retained_bytes+=bytes;atomic_store(&s_native_live_cache_bytes,s_native_retained_bytes);
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
static void rage_native_range_passes(int first,int count,int semi,int decal,int background) {
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
    if(s_native_indexed_draw)glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,(void*)(size_t)(first*4));else glDrawArrays(GL_TRIANGLES,first,count);
    if(semi>=0){
        apply_psx_blend(semi);glDepthMask(GL_FALSE);
        glDisable(GL_POLYGON_OFFSET_FILL);
        p_glUniform1i(s_native_pass,2);
        if(s_native_indexed_draw)glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,(void*)(size_t)(first*4));else glDrawArrays(GL_TRIANGLES,first,count);
    }
    glDisable(GL_POLYGON_OFFSET_FILL);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    glDisable(0x3000);glDisable(0x864F);
    if(s_native_conservative)glDisable(0x9346);
    glDisable(GL_STENCIL_TEST);glStencilMask(0x01);
    atomic_fetch_add(&s_native_draws,1);
}
static void rage_native_passes(int count,int semi,int decal,int background){
    rage_native_range_passes(0,count,semi,decal,background);
}
#include "rage_native_scene_queue.h"
static int rage_native_draw_batch(int count,int semi,int decal,int background) {
    if(!rage_native_init())return 0;
    int mapping[RAGE_SCENE_PALETTE],poses=0;
    float rows[RAGE_SCENE_PALETTE*12],lens[RAGE_SCENE_PALETTE*4],camera[RAGE_SCENE_PALETTE*4];
    RageCarLightReceipt lamps[RAGE_SCENE_PALETTE]={0};
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
            lamps[p]=s_scene_batch_lamps[original];
        }
    }else for(int i=0;i<count;++i) {
        const float* source=s_tb+i*TEXV;float* vertex=s_native_vertices+i*RAGE_NATIVE_FLOATS;
        const int original=(int)source[30]-1;
        if(original<0 || original>=s_scene_palette_count || source[31]<1)return 0;
        if(mapping[original]<0){
            mapping[original]=poses;
            memcpy(rows+12*poses,s_scene_batch_rows+12*original,48);
            memcpy(lens+4*poses,s_scene_batch_projection+4*original,16);
            lamps[poses]=s_scene_batch_lamps[original];
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
    const int caster=!decal && !background && mesh->tags.caster;
    const int player_pose=caster?mesh->tags.player_pose:-1;
    return rage_native_scene_queue(mesh,count,semi,decal,background,poses,rows,lens,camera,caster,player_pose,lamps);
}

static void rage_native_release(void) {
    rage_vehicle_shadow_release();
    for(int i=0;i<RAGE_NATIVE_CACHE;++i){
        if(s_native_meshes[i].buffer)p_glDeleteBuffers(1,&s_native_meshes[i].buffer);
        if(s_native_meshes[i].indices)p_glDeleteBuffers(1,&s_native_meshes[i].indices);
        if(s_native_meshes[i].shadow_indices)p_glDeleteBuffers(1,&s_native_meshes[i].shadow_indices);
        if(s_native_meshes[i].vao)p_glDeleteVertexArrays(1,&s_native_meshes[i].vao);
        if(s_native_meshes[i].expanded)p_glDeleteBuffers(1,&s_native_meshes[i].expanded);
        free(s_native_meshes[i].content);
    }
    memset(s_native_meshes,0,sizeof s_native_meshes);s_native_clock=0;
    memset(s_native_lookup,0,sizeof s_native_lookup);s_native_retained_bytes=0;atomic_store(&s_native_live_cache_bytes,0);
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
    GLint keep_textures[7],keep_viewport[4];glGetIntegerv(GL_VIEWPORT,keep_viewport);
    for(int u=0;u<7;++u){p_glActiveTexture(PSXGL_TEXTURE0+u);glGetIntegerv(GL_TEXTURE_BINDING_2D,keep_textures+u);}
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
    {const float direction[4]={-.34f,-1,-.42f,0},ambient[4]={.35f,.35f,.38f,0},diffuse[4]={.85f,.85f,.80f,0};
        p_glUniform4fv(s_native_light_direction,1,direction);p_glUniform4fv(s_native_light_ambient,1,ambient);p_glUniform4fv(s_native_light_diffuse,1,diffuse);p_glUniform1i(s_native_environment_shadow,0);}
    p_glUniform1f(s_native_near_bias,1.f);
    p_glUniform1i(s_native_shadow_filter,0);
    p_glUniform1i(s_native_far_enabled,0);
    p_glUniform1i(s_native_directional,0);
    p_glUniform1i(s_native_spot_count,0);
    p_glUniform1i(s_native_lens_count,0);
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
            if(memcmp(before,after,sizeof before))++failures;
            /* A nonzero index range must select the same six indices. */
            uint32_t ranges[12]={0};memcpy(ranges+6,pack.indices,6*sizeof(uint32_t));
            p_glBufferData(0x8893,sizeof ranges,ranges,GL_STATIC_DRAW);
            glDepthMask(GL_TRUE);glClearDepth(1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);s_native_indexed_draw=1;
            rage_native_range_passes(6,6,-1,0,0);s_native_indexed_draw=0;
            glReadPixels(128,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,after);
            if(memcmp(before,after,sizeof before))++failures;
            /* Gather an immutable GPU range into a stream without a CPU
             * vertex upload. Prefix vertices intentionally draw nothing. */
            float source[12*16]={};memcpy(source+6*16,vertices,sizeof vertices);
            GLuint gpu_source=0;p_glGenBuffers(1,&gpu_source);
            p_glBindBuffer(0x8F36,gpu_source);p_glBufferData(0x8F36,sizeof source,source,GL_STATIC_DRAW);
            p_glBindBuffer(0x8F37,vbo);p_glBufferData(0x8F37,sizeof source,NULL,PSXGL_STREAM_DRAW);
            p_glCopyBufferSubData(0x8F36,0x8F37,6*64,6*64,6*64);
            p_glBindBuffer(0x8F36,0);p_glBindBuffer(0x8F37,0);
            p_glBindBuffer(0x8893,0);glDepthMask(GL_TRUE);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            rage_native_range_passes(6,6,-1,0,0);
            glReadPixels(128,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,after);
            p_glDeleteBuffers(1,&gpu_source);
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
        // A scene-frame map survives a receiver-only flush and accumulates a
        // subsequent, more distant caster rather than clearing the first one.
        for(int has_casters=0;has_casters<2;++has_casters){
            const int plan=rage_shadow_update_plan(1,has_casters);
            if(plan==1)glClear(GL_DEPTH_BUFFER_BIT);
            if(plan){shadow_rows[11]=.75f;p_glUniform4fv(s_native_shadow_rows,3,shadow_rows);glDrawArrays(GL_TRIANGLES,0,6);}
            glReadPixels(255,128,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&light_depth);
            if(fabsf(light_depth-.25f)>.00001f)++failures;
        }
        shadow_rows[11]=.25f;p_glUniform4fv(s_native_shadow_rows,3,shadow_rows);
        for(int i=0;i<6;++i)vertices[i*16+11]=64;
        p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
        glClear(GL_DEPTH_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,6);glReadPixels(255,128,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&light_depth);
        if(light_depth<.99999f)++failures;
        // The same production depth shader admits environment geometry only
        // during its own caster pass. Main colour/depth never sees it here.
        p_glUniform1i(s_native_environment_shadow,1);glClear(GL_DEPTH_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,6);
        glReadPixels(255,128,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&light_depth);if(fabsf(light_depth-.25f)>.00001f)++failures;
        // A leaf/sign cutout must leave light-space depth empty, while its
        // opaque neighbour casts normally with the same texture and mesh.
        for(int step=0;step<2;++step){const unsigned char pixel[4]={0,0,0,step?255:0};
            for(int i=0;i<6;++i)vertices[i*16+11]=0;
            p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
            p_glActiveTexture(PSXGL_TEXTURE0+3);glBindTexture(GL_TEXTURE_2D,colour);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            glClear(GL_DEPTH_BUFFER_BIT);glDrawArrays(GL_TRIANGLES,0,6);glReadPixels(255,128,1,1,GL_DEPTH_COMPONENT,GL_FLOAT,&light_depth);
            if(fabsf(light_depth-(step?.25f:1.f))>.00001f)++failures;
        }
        for(int i=0;i<6;++i)vertices[i*16+11]=64|8192;
        p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
        p_glUniform1i(s_native_environment_shadow,0);
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
        // An upright decorative sheet retains its authored texture rather
        // than showing its own overlapping faces as changing dark patches.
        for(int i=0;i<6;++i)vertices[i*16+11]=64;
        p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
        {float block[4]={.25f,.25f,.25f,.25f};glTexImage2D(GL_TEXTURE_2D,0,0x81A6,2,2,0,GL_DEPTH_COMPONENT,GL_FLOAT,block);
            glDrawArrays(GL_TRIANGLES,0,6);unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);if(pixel[1]<254)++failures;}
        for(int i=0;i<6;++i)vertices[i*16+11]=64|8192;
        p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
        // The local light volume fades out before its boundary rather than
        // exposing a moving rectangular shadow edge on distant terrain.
        {float block[4]={.25f,.25f,.25f,.25f};glTexImage2D(GL_TEXTURE_2D,0,0x81A6,2,2,0,GL_DEPTH_COMPONENT,GL_FLOAT,block);
            shadow_rows[3]=.98f;p_glUniform4fv(s_native_shadow_rows,3,shadow_rows);glDrawArrays(GL_TRIANGLES,0,6);
            unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);if(pixel[1]<240)++failures;
            shadow_rows[3]=0;p_glUniform4fv(s_native_shadow_rows,3,shadow_rows);}
        /* Reference ambient/diffuse split, including a reflected camera.
         * Derivatives are evaluated before alpha/coverage rejection, and
         * two-sided retail terrain must receive identical mirror lighting. */
        p_glUniform1i(s_native_directional,1);
        for(int mirror=0;mirror<2;++mirror){float inverse[12]={1,0,0,0,0,1,0,0,0,0,1,0};inverse[0]=mirror?-1.f:1.f;
            p_glUniform4fv(s_native_inverse_view,3,inverse);
            const float facing=.42f/sqrtf(1.292f),diffuse=.85f*facing,lit=.35f+diffuse;
            const float modulation=.3f+.7f*lit/(.35f+.85f/sqrtf(1.292f));
            for(int step=0;step<3;++step){float block[4]={depths[step],depths[step],depths[step],depths[step]};
                glTexImage2D(GL_TEXTURE_2D,0,0x81A6,2,2,0,GL_DEPTH_COMPONENT,GL_FLOAT,block);glDrawArrays(GL_TRIANGLES,0,6);
                unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                const int expected=(int)lroundf(255*modulation*(step?1.f:(.35f+diffuse*.16f)/lit));
                if(pixel[0]>1 || pixel[2]>1 || abs((int)pixel[1]-expected)>2)++failures;
            }
        }
        p_glUniform1i(s_native_directional,0);
        // A receiver beyond the near grid still gets the far cascade's shadow.
        {GLuint far_depth=0;glGenTextures(1,&far_depth);p_glActiveTexture(PSXGL_TEXTURE0+6);glBindTexture(GL_TEXTURE_2D,far_depth);
            float block[4]={.25f,.25f,.25f,.25f};glTexImage2D(GL_TEXTURE_2D,0,0x81A6,2,2,0,GL_DEPTH_COMPONENT,GL_FLOAT,block);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            float outside[12];memcpy(outside,shadow_rows,sizeof outside);outside[3]=3.f;
            p_glUniform4fv(s_native_shadow_rows,3,outside);p_glUniform4fv(s_native_far_rows,3,shadow_rows);p_glUniform1f(s_native_far_bias,1);p_glUniform1i(s_native_far_enabled,1);p_glUniform1i(s_native_shadow_filter,1);
            glDrawArrays(GL_TRIANGLES,0,6);unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
            if(pixel[0]>1 || pixel[2]>1 || abs((int)pixel[1]-140)>2)++failures;
            // Distant planar receivers need a texel-scaled tolerance. The
            // previous near/far-extent scaling falsely shadowed this surface.
            for(int i=0;i<4;++i)block[i]=.49995f;
            glTexImage2D(GL_TEXTURE_2D,0,0x81A6,2,2,0,GL_DEPTH_COMPONENT,GL_FLOAT,block);
            for(int control=0;control<2;++control){p_glUniform1f(s_native_far_bias,control?1.f/12.f:.5f);
                glDrawArrays(GL_TRIANGLES,0,6);glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                if(abs((int)pixel[1]-(control?140:255))>2)++failures;
            }
            p_glUniform1i(s_native_shadow_filter,0);p_glUniform1i(s_native_far_enabled,0);p_glUniform4fv(s_native_shadow_rows,3,shadow_rows);glDeleteTextures(1,&far_depth);p_glActiveTexture(PSXGL_TEXTURE0+5);
        }
        glDeleteTextures(1,&probe_depth);p_glActiveTexture(PSXGL_TEXTURE0);p_glUniform1i(enabled,0);
        // Sky hue reaches actual fragments, rather than only the background.
        {const float top[3]={.1f,.1f,.2f},horizons[2][3]={{.8f,.25f,.08f},{.1f,.25f,.8f}};
            for(int i=0;i<6;++i)vertices[i*16+5]=vertices[i*16+6]=vertices[i*16+7]=.5f;
            p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);p_glUniform1i(s_native_directional,1);
            for(int step=0;step<2;++step){RageEnvironmentLight light;rage_environment_light(top,horizons[step],&light);
                p_glUniform4fv(s_native_light_direction,1,light.direction);p_glUniform4fv(s_native_light_ambient,1,light.ambient);p_glUniform4fv(s_native_light_diffuse,1,light.diffuse);
                glDrawArrays(GL_TRIANGLES,0,6);unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                if(step?pixel[2]<pixel[0]+10:pixel[0]<pixel[2]+10)++failures;
            }
            // UI retains its authored colours under either sky palette.
            unsigned char ui_reference[4]={0};
            for(int i=0;i<6;++i)vertices[i*16+11]=(float)((unsigned)vertices[i*16+11]|32768u);
            p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
            for(int step=0;step<2;++step){RageEnvironmentLight light;rage_environment_light(top,horizons[step],&light);
                p_glUniform4fv(s_native_light_direction,1,light.direction);p_glUniform4fv(s_native_light_ambient,1,light.ambient);p_glUniform4fv(s_native_light_diffuse,1,light.diffuse);
                glDrawArrays(GL_TRIANGLES,0,6);unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                if(!step)memcpy(ui_reference,pixel,4);else if(memcmp(ui_reference,pixel,4))++failures;
            }p_glUniform1i(s_native_directional,0);
        }
        /* A neutral receiver brightens only inside a local light's range/cone. */
        {float inverse[12]={1,0,0,0,0,1,0,0,0,0,1,0};p_glUniform4fv(s_native_inverse_view,3,inverse);
            for(int i=0;i<6;++i){vertices[16*i+5]=vertices[16*i+6]=vertices[16*i+7]=.25f;vertices[16*i+11]=64;}
            p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
            const float colour[4]={2,0,0,.96f};p_glUniform1i(s_native_spot_count,1);p_glUniform4fv(s_native_spot_colour,1,colour);
            for(int step=0;step<3;++step){float position[4]={0,0,128,step==2?64.f:1024.f},direction[4]={0,0,step==1?-1.f:1.f,.8f};
                p_glUniform4fv(s_native_spot_position,1,position);p_glUniform4fv(s_native_spot_direction,1,direction);glDrawArrays(GL_TRIANGLES,0,6);
                unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                if(step==0?(pixel[0]<pixel[1]+20):(abs((int)pixel[0]-pixel[1])>2))++failures;}
            p_glUniform1i(s_native_spot_count,0);
        }
        /* Headlights illuminate below their height, never above (world Y is down). */
        {const float colour[4]={4.2f,3.948f,3.36f,.96f},direction[4]={0,0,1,.8f};
            p_glUniform1i(s_native_spot_count,1);p_glUniform4fv(s_native_spot_colour,1,colour);p_glUniform4fv(s_native_spot_direction,1,direction);
            for(int step=0;step<2;++step){const float position[4]={0,step?48.f:-48.f,128,4800};
                p_glUniform4fv(s_native_spot_position,1,position);glDrawArrays(GL_TRIANGLES,0,6);
                unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                if(step?abs((int)pixel[0]-64)>2:pixel[0]<90)++failures;
            }p_glUniform1i(s_native_spot_count,0);
        }
        /* The downward beam reaches near road that the former upward aim missed. */
        {const float position[4]={0,-96,128,4800},colour[4]={4.2f,3.948f,3.36f,.96f};
            p_glUniform1i(s_native_spot_count,1);p_glUniform4fv(s_native_spot_position,1,position);p_glUniform4fv(s_native_spot_colour,1,colour);
            for(int step=0;step<2;++step){const float direction[4]={0,step?.40673664f:-.20791169f,step?.91354546f:.97814760f,.8f};
                p_glUniform4fv(s_native_spot_direction,1,direction);glDrawArrays(GL_TRIANGLES,0,6);
                unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
                if(step?pixel[0]<90:abs((int)pixel[0]-64)>2)++failures;
            }p_glUniform1i(s_native_spot_count,0);
        }
        /* Slow/subpixel motion is rejected; fast screen motion deposits a stain. */
        {RageLampHistory history={0};RageNativeLamp lamp={0};RageNativeGroup view={0};
            const float a[4]={100,100,200,256},b[4]={110,100,200,256};
            rage_lamp_history_sample(&history,&lamp,&view,a,1.0);rage_lamp_history_sample(&history,&lamp,&view,b,1.02);
            if(history.moving || history.weight[history.count-1]!=0)++failures;
            for(int k=0;k<6;++k){const float fast[4]={20.f*k,100,0,1};rage_lamp_history_sample(&history,&lamp,&view,fast,2.0+.02*k);}
            if(history.count<2 || !history.moving)++failures;
            rage_lamp_history_sample(&history,&lamp,&view,b,2.5);if(history.count!=1 || history.weight[0]!=0)++failures;
        }
        /* Ribbon joins share vertices, age clips continuously and depth stays current. */
        {RageLampHistory history={0};history.count=3;history.weight[0]=history.weight[1]=history.weight[2]=1;
            const float path[3][4]={{100,100,.7f,1},{110,100,.8f,1},{120,110,.9f,1}},head[4]={130,110,.8f,1},red[3]={1,0,0};
            memcpy(history.point,path,sizeof path);history.time[0]=1;history.time[1]=1.1;history.time[2]=1.2;
            s_lamp_vertices_count=0;rage_lamp_ribbon(&history,NULL,1.2,red,1);
            if(s_lamp_vertices_count!=12)++failures;
            if(memcmp(s_lamp_vertices[2].clip,s_lamp_vertices[6].clip,16) || memcmp(s_lamp_vertices[5].clip,s_lamp_vertices[7].clip,16))++failures;
            if(s_lamp_vertices[0].colour[0]!=0 || s_lamp_vertices[11].colour[0]!=1 || fabsf(s_lamp_vertices[0].clip[0]-102)> .001f)++failures;
            int bad_depth=0;for(int k=0;k<s_lamp_vertices_count;++k)if(s_lamp_vertices[k].clip[2]!=0)bad_depth=1;
            if(bad_depth)++failures;
            float old_end[4];memcpy(old_end,s_lamp_vertices[11].clip,16);s_lamp_vertices_count=0;rage_lamp_ribbon(&history,NULL,1.22,red,1);
            if(memcmp(old_end,s_lamp_vertices[11].clip,16) || s_lamp_vertices[11].colour[0]>=1)++failures;
            history.moving=1;s_lamp_vertices_count=0;rage_lamp_ribbon(&history,head,1.2,red,1);
            if(s_lamp_vertices_count!=18 || fabsf((s_lamp_vertices[14].clip[0]+s_lamp_vertices[17].clip[0])*.5f-130)>.001f)++failures;
        }
        /* The source UV mask illuminates the lens, not a nearby body panel. */
        {RageLampHistory history={0};RageNativeLamp lamp={0};RageNativeGroup view={0};
            const float a[4]={100,100,.8f,1},future[4]={140,100,.8f,1},b[4]={110,100,.8f,1},c[4]={120,100,.8f,1},red[3]={1,0,0};
            rage_lamp_history_sample(&history,&lamp,&view,a,3);rage_lamp_history_sample(&history,&lamp,&view,future,3.08);
            rage_lamp_history_sample(&history,&lamp,&view,b,3.02);rage_lamp_history_sample(&history,&lamp,&view,c,3.04);
            int bad=history.count!=4;for(int k=1;k<history.count;++k)if(history.time[k]<=history.time[k-1] || history.point[k][0]<=history.point[k-1][0])bad=1;
            if(bad)++failures;
            s_lamp_vertices_count=0;rage_lamp_ribbon(&history,c,3.04,red,1);bad=0;for(int k=0;k<s_lamp_vertices_count;++k)if(s_lamp_vertices[k].clip[0]>120.001f)bad=1;
            if(bad || s_lamp_vertices_count!=12)++failures;
            lamp.entity=0x7ffffffe;lamp.asset=88;view.state.area[2]=319;view.state.area[3]=239;
            RageLampHistory* first=rage_lamp_history(&lamp,&view,3);rage_lamp_history_sample(first,&lamp,&view,a,3);
            view.state.area[1]=240;view.state.area[3]=479;const float flipped[4]={110,340,.8f,1};
            RageLampHistory* second=rage_lamp_history(&lamp,&view,3.02);rage_lamp_history_sample(second,&lamp,&view,flipped,3.02);
            if(first!=second || second->count!=2 || fabsf(second->point[1][1]-100)>.001f)++failures;
        }
        {const float bounds[4]={-1,-1,1,1},emission[4]={2,0,0,0};
            for(int i=0;i<6;++i)vertices[16*i+11]=64|8192;p_glBufferData(PSXGL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
            p_glUniform1i(s_native_lens_count,1);p_glUniform4fv(s_native_lens_bounds,1,bounds);p_glUniform4fv(s_native_lens_colour,1,emission);
            for(int step=0;step<2;++step){float local[4]={0,0,step?-256.f:256.f,0};p_glUniform4fv(s_native_lens_position,1,local);glDrawArrays(GL_TRIANGLES,0,6);
                unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);if(step?(abs((int)pixel[0]-pixel[1])>2):(pixel[0]<pixel[1]+100))++failures;}
            p_glUniform1i(s_native_lens_count,0);
        }
        /* Use the actual halo shader to prove opaque depth hides a lamp. */
        if(!rage_lamp_init())++failures;else{
            const float top[4]={65536,16384,0,256},bottom[4]={65536,49152,0,256},red[3]={1,0,0};
            s_lamp_vertices_count=0;rage_lamp_quad(top,bottom,16384,0,16384,0,red,1,1,0);
            p_glUseProgram(s_lamp_program);p_glUniform1f(s_lamp_xoff,0);p_glUniform1f(s_lamp_xhalf,512);p_glBindVertexArray(s_lamp_vao);p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_lamp_vbo);
            p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)(s_lamp_vertices_count*sizeof(RageLampVertex)),s_lamp_vertices,GL_STREAM_DRAW);
            glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDisable(GL_BLEND);glClearColor(0,0,0,0);glEnable(0x864F);
            if(!s_lamp_visibility_queries[0])p_glGenQueries(144,s_lamp_visibility_queries);
            for(int step=0;step<2;++step){glDepthMask(GL_TRUE);glClearDepth(step?1:.25);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);glDepthMask(GL_FALSE);glDrawArrays(GL_TRIANGLES,0,s_lamp_vertices_count);
                unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);if(step?(pixel[0]<240):(pixel[0]>1))++failures;}
            for(int step=0;step<2;++step){glDepthMask(GL_TRUE);glClearDepth(step?1:.25);glClear(GL_DEPTH_BUFFER_BIT);glDepthMask(GL_FALSE);
                p_glBeginQuery(0x8C2F,s_lamp_visibility_queries[0]);glDrawArrays(GL_TRIANGLES,0,s_lamp_vertices_count);p_glEndQuery(0x8C2F);
                GLuint64 passed=0;p_glGetQueryObjectui64v(s_lamp_visibility_queries[0],GL_QUERY_RESULT,&passed);if((passed!=0)!=step)++failures;
            }
            glDisable(0x864F);
            glClearColor(.1f,.2f,.3f,1);glClear(GL_COLOR_BUFFER_BIT);p_glBlendEquationSeparate(PSXGL_FUNC_REVERSE_SUBTRACT,PSXGL_FUNC_ADD);
            rage_lamp_blend();glDrawArrays(GL_TRIANGLES,0,s_lamp_vertices_count);
            {unsigned char pixel[4];glReadPixels(255,128,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);if(pixel[0]<250 || abs((int)pixel[1]-51)>2 || abs((int)pixel[2]-77)>2)++failures;}
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);glClearDepth(1);p_glUseProgram(s_native_program);
        }
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
    for(int u=0;u<7;++u){p_glActiveTexture(PSXGL_TEXTURE0+u);glBindTexture(GL_TEXTURE_2D,(GLuint)keep_textures[u]);}
    p_glActiveTexture(PSXGL_TEXTURE0);glViewport(keep_viewport[0],keep_viewport[1],keep_viewport[2],keep_viewport[3]);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_hr_fbo);return failures;
}
