#pragma once
#include "cpu_state.h"
#include "rage_track.h"
#include <array>
#include <vector>
using RageFreeRoutes=std::array<std::vector<uint8_t>,4>;
// Blobs retain the original 24-byte road points and their curvature centres.
void rage_free_world_crest_data(const RageFreeRoutes& events);
void rage_free_world_routes(const RageFreeRoutes& routes,const std::array<std::array<float,3>,4>& shifts);
int rage_free_world_dispatch(CPUState* cpu,uint32_t target);
bool rage_free_world_build(const RageTrackCourse& active,RageTrackCourse& drawing,unsigned variant,unsigned kind);
int rage_free_world_ready();
bool rage_free_world_road_contains(double x,double y,double z,double margin);
unsigned rage_free_world_current_route();
uint64_t rage_free_world_switches();
void rage_free_world_minimap();
void rage_free_world_reset();

inline std::array<std::array<unsigned,4>,3> rage_free_tunnel_bands{};
bool rage_free_world_hide_later(CPUState* cpu);
bool rage_free_world_tunnel_routes_open(CPUState* cpu);
// The same two portal windows work in either travel direction. One segment of
// overlap opens exits before the car/camera reaches the portal plane.
inline bool rage_free_world_tunnel_window(unsigned index,const std::array<unsigned,4>& band){
    return band[3]>band[0] && ((index+1>=band[0] && index<=band[1]+1) ||
                              (index+1>=band[2] && index<=band[3]+1));
}
// Open the immediate connecting road before a tunnel index boundary changes.
// Keep distant authored upper-section backdrops concealed on the lower road.
inline bool rage_free_world_later_visible(bool hide,const float lo[3],const float hi[3],const float camera[3]){
    if(!hide)return true;
    const float range[3]={8192,2048,8192};
    for(unsigned axis=0;axis<3;++axis)
        if(camera[axis]<lo[axis]-range[axis] || camera[axis]>hi[axis]+range[axis])return false;
    return true;
}

// Distance is along the authored road, independent of travel direction. Road
// points use 32 units/metre (rendering coordinates are four times larger).
inline bool rage_free_world_portal_apron(double along,double first,double last,double radius=0){
    constexpr double apron=20.0*32.0;
    return first<=last && ((along+radius>=first && along-radius<=first+apron) ||
                          (along+radius>=last-apron && along-radius<=last));
}
// Open all route geometry through either main tunnel. Outside the portals,
// only the selected route and shared geometry remain; proximity cannot reveal
// another route as the car approaches it. Extreme Oval stays exempt.
inline bool rage_free_world_batch_visible(unsigned route,unsigned owners,bool hide,bool later,
    const float lo[3],const float hi[3],const float camera[3],bool tunnel_open=false,bool portal=false){
    // Extreme Oval (route 3), including shared faces, is always part of the world.
    if(portal || tunnel_open || (owners&(1u<<3)))return true;
    if(later && !rage_free_world_later_visible(hide,lo,hi,camera))return false;
    if(!owners || (owners&(1u<<route)))return true;
    return false;
}
