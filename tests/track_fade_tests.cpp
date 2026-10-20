#include "rage_track_fade.h"
#include <stdexcept>
#include <iostream>
void check(bool ok){if(!ok)throw std::runtime_error("Nearby terrain fade regression");}
int main(){try{
    const float lo[3]={-100,-10,-100},hi[3]={100,10,100};
    float camera[3]={0,0,0};check(rage_track_fade_distance2(lo,hi,camera)==0);
    camera[0]=103;camera[2]=104;check(rage_track_fade_distance2(lo,hi,camera)==25);
    camera[0]=-103;camera[2]=-104;check(rage_track_fade_distance2(lo,hi,camera)==25);
    RageTrackFadeState state;
    rage_track_fade_visit(state,1,true,true);check(rage_track_fade_coverage(state.start,1)==1);
    // Nearby contact road remains fully opaque after absence/camera reversal.
    rage_track_fade_visit(state,3,rage_track_fade_distance2(lo,hi,camera)<=32768.f*32768.f,true);
    check(rage_track_fade_coverage(state.start,3)==1);
    // Optional distant appearance fades retain their one-second progression.
    rage_track_fade_visit(state,5,false,true);check(rage_track_fade_coverage(state.start,5)==0);
    check(rage_track_fade_coverage(state.start,5.5f)==.5f);
    check(rage_track_fade_coverage(state.start,6)==1);
    std::cout<<"Orientation-independent distance, opaque nearby road and distant fade passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
