#pragma once
#include <stdint.h>
#include <math.h>
#include <stddef.h>
/* Lamp locations and lens masks adapted from the reference render/car_lamps.c.
 * Positions use its imported axes; convert Y/Z back for the PSX mesh stream. */
typedef enum RageLampKind {LAMP_HEAD,LAMP_TAIL,LAMP_STOP,LAMP_TAIL_STOP} RageLampKind;
typedef struct RageCarLamp {uint32_t material;float bounds[4];struct {float x,y,z;} position;RageLampKind kind;int round;} RageCarLamp;
typedef struct RageCarLightReceipt {uint32_t entity,asset,entry,tick;float head,tail,stop,speed;} RageCarLightReceipt;
typedef struct RageCarLightPose {RageCarLightReceipt lamps;float world[12];} RageCarLightPose;
typedef struct RageCarLightState {float head,tail,stop,dark;int automatic;} RageCarLightState;
static inline int rage_car_lights_required(uint32_t clock,int in_tunnel){
    return in_tunnel || clock<0x1154 || clock>=0x54a0;
}
static inline void rage_car_light_update(RageCarLightState* s,float shelter,int brake,float dt){
    if(!isfinite(dt)||dt<=0)return;
    if(shelter<.28f){s->dark=fminf(.6f,s->dark+dt);if(s->dark>=.6f)s->automatic=1;}
    else{s->dark=0;if(shelter>.36f)s->automatic=0;}
    const float step=dt*5;
    s->head=s->automatic?fminf(1,s->head+step):0;
    s->tail=.40f*s->head;s->stop=brake?1:0;
}
static inline float rage_car_lamp_strength(const RageCarLightReceipt* s,RageLampKind kind){
    return kind==LAMP_HEAD?s->head:kind==LAMP_TAIL?s->tail:kind==LAMP_STOP?s->stop:fmaxf(s->tail,s->stop);
}
static inline float rage_car_trail_strength(const RageCarLightReceipt* s){
    return s->head>.25f?1:0;
}
#ifdef __cplusplus
extern "C" {
#endif
unsigned rage_car_lamps(unsigned asset,int player,unsigned mesh,const RageCarLamp **lamps);
#ifdef __cplusplus
}
#endif
