/* GPU-thread-owned lamp receipts, reference spot lights and depth-tested halos.
 * No reads of mutable guest RAM or CPU-side car history occur here. */
typedef struct RageNativeLamp {float position[4],direction[4],colour[4],trail;uint32_t entity,asset,tick,index;} RageNativeLamp;
static RageNativeLamp s_car_lamps[72];static int s_car_lamp_count;
/* Store projected positions per viewport so camera motion also leaves a trail. */
typedef struct RageLampHistory {uint32_t entity,asset,index,tick;int area[4],origin[2],count,moving;double seen,time[64];float point[64][4],weight[64];} RageLampHistory;
static RageLampHistory s_lamp_history[216];
static _Atomic uint64_t s_car_lamp_frames,s_car_lamp_draws,s_car_trail_segments;
static _Atomic uint64_t s_player_lamp_fallbacks;
static _Atomic uint64_t s_player_head_percent,s_live_headlights,s_live_trails;
static GLuint s_lamp_program,s_lamp_vao,s_lamp_vbo;
static GLuint s_lamp_visibility_queries[144];
static GLint s_lamp_xoff,s_lamp_xhalf;
static void rage_lamp_blend(void){glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE);p_glBlendEquationSeparate(PSXGL_FUNC_ADD,PSXGL_FUNC_ADD);}
static void rage_lamp_world(const RageNativeGroup* g,int pose,const float local[3],int direction,float world[3]){
    float camera[3];const float* rows=g->palette+12*pose;
    for(int r=0;r<3;++r){camera[r]=direction?0:rows[4*r+3];for(int c=0;c<3;++c)camera[r]+=rows[4*r+c]*local[c];}
    for(int r=0;r<3;++r){world[r]=direction?0:g->inverse_view[4*r+3];for(int c=0;c<3;++c)world[r]+=g->inverse_view[4*r+c]*camera[c];}
}
static void rage_car_lights_collect(void){
    s_car_lamp_count=0;
    for(int i=0;i<s_native_queued;++i){const RageNativeGroup* g=&s_native_queue[i];if(!g->view_valid || g->decal || g->background)continue;
        for(int p=0;p<g->poses;++p){const RageCarLightReceipt* state=&g->lamps[p];if(!state->entity)continue;
            const RageCarLamp* lamps=NULL;const unsigned count=rage_car_lamps(state->asset&0x7fffffffu,(state->asset>>31)!=0,state->entry,&lamps);
            for(unsigned k=0;k<count && s_car_lamp_count<72;++k){int duplicate=0;for(int j=0;j<s_car_lamp_count;++j)
                if(s_car_lamps[j].entity==state->entity && s_car_lamps[j].index==k){duplicate=1;break;}
                if(duplicate)continue;const float strength=rage_car_lamp_strength(state,lamps[k].kind);if(strength<=.001f)continue;
                RageNativeLamp* out=&s_car_lamps[s_car_lamp_count++];memset(out,0,sizeof(*out));
                const int front=lamps[k].kind==LAMP_HEAD;
                const float local[3]={lamps[k].position.x,-lamps[k].position.y,-lamps[k].position.z};
                /* Aim 24 degrees downward in the mesh's Y-down coordinates. */
                const float direction[3]={0,front?.40673664f:.06f,front?-.91354546f:1};
                rage_lamp_world(g,p,local,0,out->position);rage_lamp_world(g,p,direction,1,out->direction);
                float length=sqrtf(rage_shadow_dot(out->direction,out->direction));if(length>.00001f)for(int c=0;c<3;++c)out->direction[c]/=length;
                out->position[3]=front?4800:640;out->direction[3]=front?.80f:.25f;
                out->colour[0]=strength*(front?12:1.5f);out->colour[1]=strength*(front?11.28f:.025f);out->colour[2]=strength*(front?9.6f:.01f);out->colour[3]=front?.96f:.75f;
                out->entity=state->entity;out->asset=state->asset;out->index=k;out->tick=state->tick;
                out->trail=front?0:rage_car_trail_strength(state);
            }
        }
    }
    /* The retail cockpit can omit the player body entirely. Resolve the
     * immutable player pose for this camera tick, as reference lampsOnly does. */
    for(int i=0;i<s_native_queued;++i){const RageNativeGroup* g=&s_native_queue[i];if(!g->state.terrain || !g->view_valid)continue;
        RageCarLightPose player;if(!rage_scene_player_light(g->lamps[0].tick,g->lamps[0].head,&player))break;
        int found=0;for(int j=0;j<s_car_lamp_count;++j)if(s_car_lamps[j].entity==player.lamps.entity){found=1;break;}if(found)break;
        const RageCarLamp* lamps=NULL;const unsigned count=rage_car_lamps(player.lamps.asset&0x7fffffffu,1,0,&lamps);
        for(unsigned k=0;k<count && s_car_lamp_count<72;++k){const float strength=rage_car_lamp_strength(&player.lamps,lamps[k].kind);if(strength<=.001f)continue;
            const int front=lamps[k].kind==LAMP_HEAD;RageNativeLamp* out=&s_car_lamps[s_car_lamp_count++];memset(out,0,sizeof(*out));
            const float local[3]={lamps[k].position.x,-lamps[k].position.y,-lamps[k].position.z},direction[3]={0,front?.40673664f:.06f,front?-.91354546f:1};
            for(int r=0;r<3;++r){out->position[r]=player.world[4*r+3];for(int c=0;c<3;++c){out->position[r]+=player.world[4*r+c]*local[c];out->direction[r]+=player.world[4*r+c]*direction[c];}}
            float length=sqrtf(rage_shadow_dot(out->direction,out->direction));if(length>.00001f)for(int c=0;c<3;++c)out->direction[c]/=length;
            out->position[3]=front?4800:640;out->direction[3]=front?.80f:.25f;out->colour[0]=strength*(front?12:1.5f);out->colour[1]=strength*(front?11.28f:.025f);out->colour[2]=strength*(front?9.6f:.01f);out->colour[3]=front?.96f:.75f;
            out->entity=player.lamps.entity;out->asset=player.lamps.asset;out->index=k;out->tick=player.lamps.tick;out->trail=front?0:rage_car_trail_strength(&player.lamps);
        }++s_player_lamp_fallbacks;break;
    }
    uint64_t heads=0,trails=0,player_head=0;for(int i=0;i<s_car_lamp_count;++i){const RageNativeLamp* lamp=&s_car_lamps[i];
        if(lamp->position[3]>1000){++heads;if(lamp->asset>>31)player_head=(uint64_t)lroundf(lamp->colour[0]*100/12);}
        if(lamp->trail>0)++trails;}
    atomic_store(&s_player_head_percent,player_head);atomic_store(&s_live_headlights,heads);atomic_store(&s_live_trails,trails);
    if(s_car_lamp_count)++s_car_lamp_frames;
}
static void rage_car_lights_bind(const RageNativeGroup* g){
    float positions[64],directions[64],colours[64],score[16];int count=0;
    if(g->view_valid && !g->background && !g->decal)for(int i=0;i<s_car_lamp_count;++i){const RageNativeLamp* light=&s_car_lamps[i];
        float distance=0;for(int c=0;c<3;++c){float d=light->position[c]-g->inverse_view[4*c+3];distance+=d*d;}
        /* Long-range headlights and nearby rear lights have priority. */
        const float rank=distance/(light->position[3]*light->position[3]);if(rank>9)continue;
        int slot;if(count<16)slot=count++;else{if(rank>=score[15])continue;slot=15;}
        while(slot>0 && score[slot-1]>rank){if(slot<16){score[slot]=score[slot-1];memcpy(positions+4*slot,positions+4*(slot-1),16);memcpy(directions+4*slot,directions+4*(slot-1),16);memcpy(colours+4*slot,colours+4*(slot-1),16);}--slot;}
        score[slot]=rank;memcpy(positions+4*slot,light->position,16);memcpy(directions+4*slot,light->direction,16);memcpy(colours+4*slot,light->colour,16);
        /* Reduce headlight surface spill without dimming the physical lenses. */
        if(light->position[3]>1000)for(int c=0;c<3;++c)colours[4*slot+c]*=.35f;
    }
    p_glUniform1i(s_native_spot_count,count);if(count){p_glUniform4fv(s_native_spot_position,count,positions);p_glUniform4fv(s_native_spot_direction,count,directions);p_glUniform4fv(s_native_spot_colour,count,colours);}
    float bounds[256],local[256],emission[256];int patches=0;
    if(!g->decal && !g->background)for(int p=0;p<g->poses;++p){const RageCarLightReceipt* state=&g->lamps[p];if(!state->entity)continue;
        const RageCarLamp* lamps=NULL;const unsigned total=rage_car_lamps(state->asset&0x7fffffffu,state->asset>>31,state->entry,&lamps);
        for(unsigned i=0;i<total && patches<64;++i){const float strength=rage_car_lamp_strength(state,lamps[i].kind);if(strength<=.001f)continue;
            const int offset=patches++*4,front=lamps[i].kind==LAMP_HEAD;memcpy(bounds+offset,lamps[i].bounds,16);
            local[offset]=lamps[i].position.x;local[offset+1]=-lamps[i].position.y;local[offset+2]=-lamps[i].position.z;local[offset+3]=p+(lamps[i].round?.5f:0);
            emission[offset]=strength*2.5f;emission[offset+1]=strength*(front?2.35f:.025f);emission[offset+2]=strength*(front?2:.01f);emission[offset+3]=0;
        }
    }
    p_glUniform1i(s_native_lens_count,patches);if(patches){p_glUniform4fv(s_native_lens_bounds,patches,bounds);p_glUniform4fv(s_native_lens_position,patches,local);p_glUniform4fv(s_native_lens_colour,patches,emission);}
}
static int rage_lamp_init(void){
    if(s_lamp_program)return 1;
    s_lamp_program=build_program("#version 330\nlayout(location=0)in vec4 position;layout(location=1)in vec2 uv_in;layout(location=2)in vec3 colour_in;uniform float xoff,xhalf;out vec2 uv;out vec3 colour;void main(){uv=uv_in;colour=colour_in;gl_Position=vec4((position.x+position.w*xoff)/xhalf-position.w,position.y/256.0-position.w,position.z,position.w);gl_ClipDistance[0]=1.0;}",
        "#version 330\nin vec2 uv;in vec3 colour;out vec4 frag;void main(){float r2=dot(uv,uv);float halo=exp(-4.0*r2)*(1.0-smoothstep(.3,1.0,r2));frag=vec4(colour*halo,0);}");
    if(!s_lamp_program)return 0;
    s_lamp_xoff=p_glGetUniformLocation(s_lamp_program,"xoff");s_lamp_xhalf=p_glGetUniformLocation(s_lamp_program,"xhalf");
    p_glGenVertexArrays(1,&s_lamp_vao);p_glGenBuffers(1,&s_lamp_vbo);p_glBindVertexArray(s_lamp_vao);p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_lamp_vbo);
    const int sizes[3]={4,2,3},offset[3]={0,4,6};for(int i=0;i<3;++i){p_glEnableVertexAttribArray(i);p_glVertexAttribPointer(i,sizes[i],GL_FLOAT,GL_FALSE,36,(void*)(size_t)(offset[i]*4));}return 1;
}
typedef struct RageLampVertex {float clip[4],uv[2],colour[3];} RageLampVertex;
static RageLampVertex s_lamp_vertices[72*65*6];static int s_lamp_vertices_count;
static int rage_lamp_project(const RageNativeGroup* view,const float world[3],float clip[4]){
    const float* origin=view->palette+16*view->poses;const float* lens=view->palette+12*view->poses;
    float camera[3]={0};for(int r=0;r<3;++r)for(int c=0;c<3;++c)camera[r]+=view->palette[4*r+c]*(world[c]-origin[c]);
    if(!isfinite(camera[2]) || camera[2]<=1)return 0;
    clip[0]=camera[0]*lens[0]+camera[2]*lens[2];clip[1]=camera[1]*lens[1]+camera[2]*lens[3];clip[2]=camera[2]-16;clip[3]=camera[2];return 1;
}
static void rage_lamp_quad(const float a[4],const float b[4],float ax,float ay,float bx,float by,const float colour[3],float fade_a,float fade_b,int ribbon){
    if(s_lamp_vertices_count+6>(int)(sizeof s_lamp_vertices/sizeof *s_lamp_vertices))return;
    const int corners[6]={0,1,2,2,1,3};for(int i=0;i<6;++i){const int c=corners[i],end=c>>1,side=(c&1)?1:-1;
        RageLampVertex* v=&s_lamp_vertices[s_lamp_vertices_count++];memcpy(v->clip,end?b:a,16);v->clip[0]+=side*(end?bx:ax);v->clip[1]+=side*(end?by:ay);
        v->uv[0]=(float)side;v->uv[1]=ribbon?0:(end?1:-1);for(int k=0;k<3;++k)v->colour[k]=colour[k]*(end?fade_b:fade_a);}
}
/* Test lamp centres against this view's completed scene depth. Issue the whole
 * query batch before reading results, rather than synchronizing per source. */
static void rage_lamp_visibility(const RageNativeGroup* view,unsigned char visible[72]){
    memset(visible,0,72);if(!s_car_lamp_count || !p_glGenQueries || !p_glGetQueryObjectui64v)return;
    if(!s_lamp_visibility_queries[0])p_glGenQueries(144,s_lamp_visibility_queries);
    int first[72];s_lamp_vertices_count=0;
    const float white[3]={1,1,1};
    for(int i=0;i<s_car_lamp_count;++i){const RageNativeLamp* light=&s_car_lamps[i];float world[3],clip[4];first[i]=-1;
        for(int c=0;c<3;++c)world[c]=light->position[c]+light->direction[c]*12;
        if(!rage_lamp_project(view,world,clip))continue;
        float a[4],b[4];memcpy(a,clip,16);memcpy(b,clip,16);a[1]-=.35f*clip[3];b[1]+=.35f*clip[3];
        first[i]=s_lamp_vertices_count;rage_lamp_quad(a,b,.35f*clip[3],0,.35f*clip[3],0,white,1,1,0);
    }
    rage_native_state_set(&view->state);rage_native_view_depth();hr_begin(1);p_glUseProgram(s_lamp_program);
    p_glBindVertexArray(s_lamp_vao);p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_lamp_vbo);
    p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)(s_lamp_vertices_count*sizeof(RageLampVertex)),s_lamp_vertices,GL_STREAM_DRAW);
    p_glUniform1f(s_lamp_xoff,0);p_glUniform1f(s_lamp_xhalf,512);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
    glDisable(GL_BLEND);glDisable(GL_CULL_FACE);glDisable(GL_POLYGON_OFFSET_FILL);glEnable(0x864F);
    if(rage_native_has_inset()){glEnable(GL_STENCIL_TEST);glStencilMask(0);glStencilFunc(GL_EQUAL,0,0x80);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);}else glDisable(GL_STENCIL_TEST);
    for(int i=0;i<s_car_lamp_count;++i)if(first[i]>=0){p_glBeginQuery(0x8C2F,s_lamp_visibility_queries[i]);glDrawArrays(GL_TRIANGLES,first[i],6);p_glEndQuery(0x8C2F);}
    const int wide=g_wide_cur && s_ws_ablate!=1 && s_ws_ablate!=2;
    if(wide){wide_target_begin(wide_dx(),s_lamp_xoff,s_lamp_xhalf);
        if(!view_enabled && s_area_x2-s_area_x1+1<320){int x=s_area_x1+wide_dx(),end=s_area_x2+1+wide_dx();if(x<view_pad_left)x=view_pad_left;if(end>g_wide_w-view_pad_right)end=g_wide_w-view_pad_right;wide_band_scissor_x(x,end>x?end-x:0);}
        for(int i=0;i<s_car_lamp_count;++i)if(first[i]>=0){p_glBeginQuery(0x8C2F,s_lamp_visibility_queries[72+i]);glDrawArrays(GL_TRIANGLES,first[i],6);p_glEndQuery(0x8C2F);}
        wide_target_end(s_lamp_xoff,s_lamp_xhalf);
    }
    for(int i=0;i<s_car_lamp_count;++i)if(first[i]>=0){GLuint64 passed=0;p_glGetQueryObjectui64v(s_lamp_visibility_queries[i],GL_QUERY_RESULT,&passed);visible[i]=passed!=0;
        if(wide){p_glGetQueryObjectui64v(s_lamp_visibility_queries[72+i],GL_QUERY_RESULT,&passed);visible[i]|=passed!=0;}
    }
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(0x864F);glDisable(GL_DEPTH_TEST);glDisable(GL_STENCIL_TEST);glStencilMask(1);hr_end();s_lamp_vertices_count=0;
}
static RageLampHistory* rage_lamp_history(const RageNativeLamp* light,const RageNativeGroup* view,double now){
    /* Draw pages alternate in VRAM; they are the same camera, not two histories. */
    const int origin[2]={(view->state.area[0]/320)*320,(view->state.area[1]/240)*240};
    const int area[4]={view->state.area[0]-origin[0],view->state.area[1]-origin[1],view->state.area[2]-origin[0],view->state.area[3]-origin[1]};
    RageLampHistory* oldest=&s_lamp_history[0];for(int i=0;i<216;++i){RageLampHistory* h=&s_lamp_history[i];
        if(h->entity==light->entity && h->asset==light->asset && h->index==light->index && !memcmp(h->area,area,sizeof h->area)){memcpy(h->origin,origin,sizeof origin);return h;}
        if(h->seen<oldest->seen)oldest=h;}
    memset(oldest,0,sizeof(*oldest));oldest->entity=light->entity;oldest->asset=light->asset;oldest->index=light->index;oldest->seen=now;memcpy(oldest->area,area,sizeof area);memcpy(oldest->origin,origin,sizeof origin);return oldest;
}
static void rage_lamp_history_sample(RageLampHistory* h,const RageNativeLamp* light,const RageNativeGroup* view,const float clip[4],double now){
    float point[4]={clip[0]/clip[3]-h->origin[0],clip[1]/clip[3]-h->origin[1],clip[2]/clip[3],1};
    /* Scene render order includes future real frames before older interpolated
     * frames. Keep a sorted animation-time path, never a wall-clock path. */
    if(now-h->seen>.25 || now-h->seen<-.5)h->count=0;
    h->seen=fmax(h->seen,now);h->tick=light->tick;
    int slot=0;while(slot<h->count && h->time[slot]<now-.00001)++slot;
    if(h->count){int nearest=slot?slot-1:0;float dx=point[0]-h->point[nearest][0],dy=point[1]-h->point[nearest][1];
        if(dx*dx+dy*dy>128.f*128){h->count=0;slot=0;}
    }
    if(slot<h->count && fabs(h->time[slot]-now)<.00001){memcpy(h->point[slot],point,16);}
    else{
        if(h->count==64){if(slot==0)return;memmove(h->point,h->point+1,63*sizeof h->point[0]);memmove(h->time,h->time+1,63*sizeof h->time[0]);memmove(h->weight,h->weight+1,63*sizeof h->weight[0]);--h->count;--slot;}
        memmove(h->point+slot+1,h->point+slot,(h->count-slot)*sizeof h->point[0]);
        memmove(h->time+slot+1,h->time+slot,(h->count-slot)*sizeof h->time[0]);
        memmove(h->weight+slot+1,h->weight+slot,(h->count-slot)*sizeof h->weight[0]);
        memcpy(h->point[slot],point,16);h->time[slot]=now;++h->count;
    }
    /* Measure net screen displacement over a short window, rejecting jitter.
     * Recompute subsequent exposure after inserting an older frame. */
    for(int i=slot;i<h->count;++i){int previous=i;
        while(previous>0 && h->time[i]-h->time[previous]<.04)--previous;
        const double dt=h->time[i]-h->time[previous];float speed=0;
        if(dt>.005){float dx=h->point[i][0]-h->point[previous][0],dy=h->point[i][1]-h->point[previous][1];speed=sqrtf(dx*dx+dy*dy)/(float)dt;}
        h->weight[i]=speed>(i && h->weight[i-1]>0?60.f:90.f)?1.f:0.f;
    }
    h->moving=h->weight[slot]>0;
    (void)view;
}
static float rage_lamp_fade(double age){
    const float t=fminf(1,fmaxf(0,1-(float)(age/.18)));return t*t*(3-2*t);
}
/* A single joined ribbon: no independently rotated, overlapping segment ends. */
static void rage_lamp_ribbon(const RageLampHistory* h,const float clip[4],double now,const float colour[3],float strength){
    float points[65][4],fade[65],join[65][2];int count=0;
    const float depth=clip?clip[2]/clip[3]-.00001f:0;
    for(int k=0;k<h->count;++k){double age=now-h->time[k];
        if(age<-.00001)continue; /* Future real-frame samples must not leak backward. */
        if(age>.18){
            if(k+1==h->count || now-h->time[k+1]>=.18)continue;
            const double span=h->time[k+1]-h->time[k];if(span<=0 || h->time[k+1]>now+.00001)continue;
            const float t=(float)((now-.18-h->time[k])/span);
            for(int c=0;c<2;++c)points[count][c]=h->point[k][c]+t*(h->point[k+1][c]-h->point[k][c]);
            fade[count]=0;
        }else{memcpy(points[count],h->point[k],16);fade[count]=rage_lamp_fade(age)*strength*h->weight[k];}
        points[count][2]=depth;points[count][3]=1;++count;
    }
    /* Connect every rendered frame to the real lamp, without refreshing history. */
    if(clip && h->moving){const float x=clip[0]/clip[3]-h->origin[0],y=clip[1]/clip[3]-h->origin[1];
        if(count && fabsf(points[count-1][0]-x)+fabsf(points[count-1][1]-y)<.001f)fade[count-1]=strength;
        else{points[count][0]=x;points[count][1]=y;points[count][2]=depth;points[count][3]=1;fade[count++]=strength;}
    }
    /* Remove duplicate positions before deriving tangents. */
    int unique=0;for(int k=0;k<count;++k){
        if(unique){float dx=points[k][0]-points[unique-1][0],dy=points[k][1]-points[unique-1][1];if(dx*dx+dy*dy<.0001f){fade[unique-1]=fade[k];continue;}}
        if(unique!=k){memcpy(points[unique],points[k],16);fade[unique]=fade[k];}++unique;
    }count=unique;if(count<2)return;
    for(int k=0;k<count;++k){
        const int prev=k?k-1:0,next=k+1<count?k+1:k;
        float ax=points[k][0]-points[prev][0],ay=points[k][1]-points[prev][1];
        float bx=points[next][0]-points[k][0],by=points[next][1]-points[k][1];
        float al=sqrtf(ax*ax+ay*ay),bl=sqrtf(bx*bx+by*by);
        if(al<.001f){ax=bx;ay=by;al=bl;}if(bl<.001f){bx=ax;by=ay;bl=al;}
        ax/=al;ay/=al;bx/=bl;by/=bl;
        float nx=-ay-by,ny=ax+bx,nl=sqrtf(nx*nx+ny*ny);
        if(nl<.001f){nx=-by;ny=bx;}else{nx/=nl;ny/=nl;}
        const float width=2.5f/fmaxf(.5f,nx*(-by)+ny*bx);join[k][0]=nx*width;join[k][1]=ny*width;
    }
    for(int k=0;k<count;++k){points[k][0]+=h->origin[0];points[k][1]+=h->origin[1];}
    for(int k=1;k<count;++k){rage_lamp_quad(points[k-1],points[k],join[k-1][0],join[k-1][1],join[k][0],join[k][1],colour,fade[k-1],fade[k],1);++s_car_trail_segments;}
}
static void rage_lamps_draw(const RageNativeGroup* view,double now){
    if(!view->view_valid || !rage_lamp_init())return;s_lamp_vertices_count=0;
    const RageCarLightReceipt* clock=&view->lamps[0];
    if(clock->speed>=25 && clock->speed<=30)now=((double)clock->tick-fmaxf(1,clock->tail)*(1-fminf(1,fmaxf(0,clock->head))))/clock->speed;
    unsigned char visible[72];rage_lamp_visibility(view,visible);
    const float* lens=view->palette+12*view->poses;
    for(int i=0;i<s_car_lamp_count;++i){const RageNativeLamp* light=&s_car_lamps[i];float delta[3],distance=0;
        if(!visible[i]){if(light->trail>0){RageLampHistory* h=rage_lamp_history(light,view,now);h->count=0;h->moving=0;}continue;}
        for(int c=0;c<3;++c){delta[c]=view->inverse_view[4*c+3]-light->position[c];distance+=delta[c]*delta[c];}
        distance=sqrtf(distance);if(distance<=4)continue;const float facing=rage_shadow_dot(delta,light->direction)/distance;
        float lifted[3];for(int c=0;c<3;++c)lifted[c]=light->position[c]+light->direction[c]*12;
        float clip[4];if(!rage_lamp_project(view,lifted,clip))continue;
        const float strength=fmaxf(light->colour[0],fmaxf(light->colour[1],light->colour[2]));
        const float intensity=fminf(strength,1)*fminf(fmaxf((facing-.1f)*2,0),1)*.65f;
        const float radius=fminf(48,fmaxf(24,distance*.04f));float colour[3];for(int c=0;c<3;++c)colour[c]=light->colour[c]/strength*intensity;
        float top[4],bottom[4];memcpy(top,clip,16);memcpy(bottom,clip,16);top[1]-=radius*lens[1];bottom[1]+=radius*lens[1];
        if(intensity>0){rage_lamp_quad(top,bottom,radius*lens[0],0,radius*lens[0],0,colour,1,1,0);++s_car_lamp_draws;}
        /* Motion ribbons scatter light in every viewing direction, including chase views. */
        if(light->trail<=0)continue;for(int c=0;c<3;++c)colour[c]=light->colour[c]/strength*.65f;RageLampHistory* h=rage_lamp_history(light,view,now);
        rage_lamp_history_sample(h,light,view,clip,now);
        rage_lamp_ribbon(h,clip,now,colour,light->trail);
    }
    if(!s_lamp_vertices_count)return;
    rage_native_state_set(&view->state);rage_native_view_depth();hr_begin(1);p_glUseProgram(s_lamp_program);
    p_glBindVertexArray(s_lamp_vao);p_glBindBuffer(PSXGL_ARRAY_BUFFER,s_lamp_vbo);p_glBufferData(PSXGL_ARRAY_BUFFER,(ptrdiff_t)(s_lamp_vertices_count*sizeof(RageLampVertex)),s_lamp_vertices,GL_STREAM_DRAW);
    p_glUniform1f(s_lamp_xoff,0);p_glUniform1f(s_lamp_xhalf,512);
    if(rage_native_has_inset()){glEnable(GL_STENCIL_TEST);glStencilMask(0);glStencilFunc(GL_EQUAL,0,0x80);glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);}else glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LEQUAL);glDepthMask(GL_FALSE);rage_lamp_blend();glDisable(GL_CULL_FACE);glDisable(GL_POLYGON_OFFSET_FILL);glEnable(0x864F);
    glDrawArrays(GL_TRIANGLES,0,s_lamp_vertices_count);
    if(g_wide_cur && s_ws_ablate!=1){wide_target_begin(wide_dx(),s_lamp_xoff,s_lamp_xhalf);
        if(!view_enabled && s_area_x2-s_area_x1+1<320){int x=s_area_x1+wide_dx(),end=s_area_x2+1+wide_dx();if(x<view_pad_left)x=view_pad_left;if(end>g_wide_w-view_pad_right)end=g_wide_w-view_pad_right;wide_band_scissor_x(x,end>x?end-x:0);}
        if(s_ws_ablate!=2)glDrawArrays(GL_TRIANGLES,0,s_lamp_vertices_count);wide_target_end(s_lamp_xoff,s_lamp_xhalf);}
    glDisable(0x864F);glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glDisable(GL_STENCIL_TEST);glStencilMask(1);hr_end();
}
static void rage_car_lights_release(void){
    if(s_lamp_visibility_queries[0])p_glDeleteQueries(144,s_lamp_visibility_queries);memset(s_lamp_visibility_queries,0,sizeof s_lamp_visibility_queries);
    if(s_lamp_program)p_glDeleteProgram(s_lamp_program);if(s_lamp_vao)p_glDeleteVertexArrays(1,&s_lamp_vao);if(s_lamp_vbo)p_glDeleteBuffers(1,&s_lamp_vbo);
    s_lamp_program=s_lamp_vao=s_lamp_vbo=0;s_car_lamp_count=0;memset(s_lamp_history,0,sizeof s_lamp_history);
}
void gl_renderer_car_light_stats(uint64_t out[3]){out[0]=atomic_load(&s_car_lamp_frames);out[1]=atomic_load(&s_car_lamp_draws);out[2]=atomic_load(&s_car_trail_segments);}
uint64_t gl_renderer_player_lamp_fallbacks(void){return atomic_load(&s_player_lamp_fallbacks);}
void gl_renderer_car_lights_live(uint64_t out[3]){out[0]=atomic_load(&s_player_head_percent);out[1]=atomic_load(&s_live_headlights);out[2]=atomic_load(&s_live_trails);}
