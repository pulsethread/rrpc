/* GPU-thread-owned optional effects. Included by the GL renderer. */
static int s_rage_effects[4],s_rage_dof=0,s_rage_dof_strength=2,s_rage_dof_distance=2;
static GLuint s_rage_fx_program,s_rage_fx_texture,s_rage_dof_program;
typedef struct RageDofTarget {GLuint fbo,colour,depth;int w,h;} RageDofTarget;
static RageDofTarget s_rage_dof_targets[32];
static int s_rage_fx_w,s_rage_fx_h;
static RageNativeViews s_rage_dof_views;
static const char* RAGE_EFFECT_VS=
    "#version 330\n"
    "out vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}\n";
static const char* RAGE_EFFECT_FS=
    "#version 330\n"
    "in vec2 uv;out vec4 frag;uniform sampler2D image;uniform ivec4 effects;uniform vec2 size;\n"
    "void main(){vec2 p=uv;vec2 q=p*2.0-1.0;\n"
    "if(effects.z!=0){q*=1.0+0.045*dot(q,q);p=q*0.5+0.5;}\n"
    "if(any(lessThan(p,vec2(0)))||any(greaterThan(p,vec2(1)))){frag=vec4(0,0,0,1);return;}\n"
    "vec3 c=texture(image,p).rgb;\n"
    "if(effects.x!=0){int stripe=int(mod(floor(uv.x*size.x),3.0));vec3 mask=vec3(0.82);mask[stripe]=1.0;c*=mask*1.08;}\n"
    "if(effects.y!=0)c*=1.0-0.24*smoothstep(0.15,1.5,dot(q,q));\n"
    "if(effects.w!=0){float y=dot(c,vec3(.2126,.7152,.0722));c=mix(vec3(y),c,1.10);c=(c-.5)*1.06+.5;c*=vec3(1.035,1.005,.965);}\n"
    "frag=vec4(clamp(c,0.0,1.0),1);}\n";
static const char* RAGE_DOF_FS=
    "#version 330\n"
    "in vec2 uv;out vec4 frag;uniform sampler2D image,depths;uniform vec2 pixel;uniform float strength,reach;\n"
    "float distance_at(vec2 p){return 8.0/max(1.0-texture(depths,p).r,0.000001);}\n"
    "void main(){float z=distance_at(uv);float coc=smoothstep(min(3500.0,reach*.2),reach,z);\n"
    "vec4 center=texture(image,uv);if(coc<.01){frag=center;return;}\n"
    "vec3 sum=center.rgb;float weight=1.0;float radius=coc*strength*1.6;\n"
    "for(int i=0;i<12;++i){float a=float(i)*2.39996323;vec2 p=clamp(uv+vec2(cos(a),sin(a))*sqrt((float(i)+.5)/12.0)*radius*pixel,vec2(0),vec2(1));\n"
    "float nz=distance_at(p);float w=smoothstep(z*.45,z*.8,nz);sum+=texture(image,p).rgb*w;weight+=w;}\n"
    "frag=vec4(sum/weight,center.a);}\n";
void gl_renderer_set_screen_effects(int texture,int shading,int distortion,int grading,int dof,int strength,int distance){
    GL_RT_SYNC("screen_effects");
    s_rage_effects[0]=!!texture;s_rage_effects[1]=!!shading;s_rage_effects[2]=!!distortion;s_rage_effects[3]=!!grading;
    s_rage_dof=!!dof;s_rage_dof_strength=strength<1?1:strength>5?5:strength;
    s_rage_dof_distance=distance<0?0:distance>4?4:distance;
}
static void rage_effect_texture(GLuint* texture,int w,int h,int depth){
    if(!*texture)glGenTextures(1,texture);glBindTexture(GL_TEXTURE_2D,*texture);
    glTexImage2D(GL_TEXTURE_2D,0,depth?PSXGL_DEPTH24_STENCIL8:GL_RGBA8,w,h,0,
        depth?0x84F9 /* DEPTH_STENCIL */:GL_RGBA,depth?0x84FA /* UNSIGNED_INT_24_8 */:GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,depth?GL_NEAREST:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,depth?GL_NEAREST:GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
static void rage_screen_effects_apply(int x,int y,int w,int h){
    if(!(s_rage_effects[0]||s_rage_effects[1]||s_rage_effects[2]||s_rage_effects[3])||w<1||h<1)return;
    if(!s_rage_fx_program)s_rage_fx_program=build_program(RAGE_EFFECT_VS,RAGE_EFFECT_FS);
    if(!s_rage_fx_program)return;
    p_glActiveTexture(PSXGL_TEXTURE0);
    if(w!=s_rage_fx_w||h!=s_rage_fx_h||!s_rage_fx_texture){rage_effect_texture(&s_rage_fx_texture,w,h,0);s_rage_fx_w=w;s_rage_fx_h=h;}
    glBindTexture(GL_TEXTURE_2D,s_rage_fx_texture);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,0);glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,x,y,w,h);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glViewport(x,y,w,h);
    p_glUseProgram(s_rage_fx_program);p_glUniform1i(p_glGetUniformLocation(s_rage_fx_program,"image"),0);
    p_glUniform4i(p_glGetUniformLocation(s_rage_fx_program,"effects"),s_rage_effects[0],s_rage_effects[1],s_rage_effects[2],s_rage_effects[3]);
    p_glUniform2f(p_glGetUniformLocation(s_rage_fx_program,"size"),(float)w,(float)h);
    p_glBindVertexArray(s_present_vao);glDrawArrays(GL_TRIANGLES,0,3);p_glBindVertexArray(0);p_glUseProgram(0);
}
static void rage_dof_target(GLuint target,int x,int y,int w,int h,int slot){
    if(w<1||h<1)return;
    RageDofTarget* scratch=&s_rage_dof_targets[slot];
    p_glActiveTexture(PSXGL_TEXTURE0);
    if(w!=scratch->w||h!=scratch->h||!scratch->fbo){
        rage_effect_texture(&scratch->colour,w,h,0);rage_effect_texture(&scratch->depth,w,h,1);
        if(!scratch->fbo)p_glGenFramebuffers(1,&scratch->fbo);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER,scratch->fbo);
        p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER,PSXGL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,scratch->colour,0);
        p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER,PSXGL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,scratch->depth,0);
        if(p_glCheckFramebufferStatus(PSXGL_FRAMEBUFFER)!=PSXGL_FRAMEBUFFER_COMPLETE)return;
        scratch->w=w;scratch->h=h;
    }
    glDisable(GL_SCISSOR_TEST);
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,target);p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER,scratch->fbo);
    p_glBlitFramebuffer(x,y,x+w,y+h,0,0,w,h,GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT,GL_NEAREST);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,target);glViewport(x,y,w,h);
    glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    p_glUseProgram(s_rage_dof_program);
    p_glUniform1i(p_glGetUniformLocation(s_rage_dof_program,"image"),0);
    p_glUniform1i(p_glGetUniformLocation(s_rage_dof_program,"depths"),1);
    p_glUniform2f(p_glGetUniformLocation(s_rage_dof_program,"pixel"),1.f/w,1.f/h);
    p_glUniform1f(p_glGetUniformLocation(s_rage_dof_program,"strength"),s_rage_dof_strength*(float)w/640.f);
    const float reaches[]={8000,16000,30000,60000,100000};
    p_glUniform1f(p_glGetUniformLocation(s_rage_dof_program,"reach"),reaches[s_rage_dof_distance]);
    p_glActiveTexture(PSXGL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,scratch->colour);
    p_glActiveTexture(PSXGL_TEXTURE0+1);glBindTexture(GL_TEXTURE_2D,scratch->depth);
    p_glBindVertexArray(s_present_vao);glDrawArrays(GL_TRIANGLES,0,3);
    p_glActiveTexture(PSXGL_TEXTURE0);p_glBindVertexArray(0);p_glUseProgram(0);
}
/* Resolve completed 3D views before the first interface batch. */
static void rage_dof_before_interface(void){
    rage_native_scene_flush();
    if(!s_rage_dof||!s_scene_depth_owned||s_hiw)return;
    if(!s_rage_dof_program)s_rage_dof_program=build_program(RAGE_EFFECT_VS,RAGE_DOF_FS);
    if(!s_rage_dof_program)return;
    for(int i=s_rage_dof_views.count;i<s_scene_views.count;++i){
        const int* rect=s_scene_views.rect[i];const int w=rect[2]-rect[0]+1,h=rect[3]-rect[1]+1;
        rage_dof_target(s_hr_fbo,rect[0]*s_hr_scale,rect[1]*s_hr_scale,w*s_hr_scale,h*s_hr_scale,2*i);
        if(g_wide_cur){int x=rect[0]+wide_dx(),width=w;
            if(w>=320){x=view_pad_left;width=g_wide_w-view_pad_left-view_pad_right;}
            if(x<0){width+=x;x=0;}if(x+width>g_wide_w)width=g_wide_w-x;
            rage_dof_target(g_wide_cur,x*s_out_scale,rect[1]*s_out_scale,width*s_out_scale,h*s_out_scale,2*i+1);}
    }
    s_rage_dof_views.count=s_scene_views.count;
}
static void rage_screen_effects_release(void){
    glDeleteTextures(1,&s_rage_fx_texture);
    for(int i=0;i<32;++i){RageDofTarget* scratch=&s_rage_dof_targets[i];
        glDeleteTextures(1,&scratch->colour);glDeleteTextures(1,&scratch->depth);
        if(scratch->fbo)p_glDeleteFramebuffers(1,&scratch->fbo);memset(scratch,0,sizeof *scratch);}
    if(s_rage_fx_program)p_glDeleteProgram(s_rage_fx_program);if(s_rage_dof_program)p_glDeleteProgram(s_rage_dof_program);
    s_rage_fx_texture=s_rage_fx_program=s_rage_dof_program=0;
    s_rage_fx_w=s_rage_fx_h=0;s_rage_dof_views.count=0;
}
