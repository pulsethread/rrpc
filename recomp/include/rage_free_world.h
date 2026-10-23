#pragma once
#include "cpu_state.h"
#include "rage_track.h"
#include <array>
#include <vector>
using RageFreeRoutes=std::array<std::vector<uint8_t>,4>;
// Blobs retain the original 24-byte road points and their curvature centres.
void rage_free_world_routes(const RageFreeRoutes& routes,const std::array<std::array<float,3>,4>& shifts);
int rage_free_world_dispatch(CPUState* cpu,uint32_t target);
bool rage_free_world_build(const RageTrackCourse& active,RageTrackCourse& drawing,unsigned variant,unsigned kind);
int rage_free_world_ready();
unsigned rage_free_world_current_route();
uint64_t rage_free_world_switches();
void rage_free_world_minimap();
void rage_free_world_reset();

inline std::array<std::array<unsigned,4>,3> rage_free_tunnel_bands{};
bool rage_free_world_hide_later(CPUState* cpu);
