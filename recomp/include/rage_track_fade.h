#pragma once
/* A sector remains known while it is in distance range, even off-screen.
 * Short absences retain progress to avoid restarting at visibility boundaries. */
struct RageTrackFadeState {float seen=-1000,start=0;};
inline float rage_track_fade_distance2(const float lo[3],const float hi[3],const float camera[3]){
    float distance=0;
    for(int i=0;i<3;++i){const float d=camera[i]<lo[i]?lo[i]-camera[i]:camera[i]>hi[i]?camera[i]-hi[i]:0;distance+=d*d;}
    return distance;
}
inline void rage_track_fade_visit(RageTrackFadeState& state,float now,bool warm,bool enabled){
    if(warm || !enabled)state.start=0;
    else if(now-state.seen>0.25f)state.start=now;
    state.seen=now;
}
inline float rage_track_fade_coverage(float start,float now){
    return start<=0?1:now<=start?0:now>=start+1?1:now-start;
}
