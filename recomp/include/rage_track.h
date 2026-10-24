#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct CPUState;
void rage_track_enable(int on);
void rage_track_set_sky_light(int on);
int rage_track_sky_light(void);
void rage_track_set_shadow_quality(int quality);
int rage_track_shadow_quality(void);
void rage_track_set_extended_distance(int on);
int rage_track_extended_distance(void);
int rage_track_replay_distance(void);
float rage_track_far_limit(void);
void rage_track_set_lod_fade(int on);
int rage_track_lod_fade(void);
float rage_track_fade_clock(void);
void rage_track_reset(void);
int rage_track_ground_height(float x,float z,float near_y,float* height);
int rage_track_in_tunnel(float x,float y,float z);
void rage_track_dispatch(struct CPUState* cpu,uint32_t target);
int rage_track_native_try(struct CPUState* cpu,uint32_t target);
void rage_sky_reset(void);
void rage_sky_dispatch(struct CPUState* cpu,uint32_t target);
int rage_sky_submit(uint32_t packet,int dx,int dy,uint32_t window,const uint32_t* words,unsigned count);
void rage_sky_stats(uint64_t* packets,uint64_t* triangles);
int rage_track_submit(uint32_t packet,int dx,int dy,uint32_t texture_window,
                      int left,int top,int right,int bottom,const uint32_t* words,unsigned count);
void rage_track_stats(uint64_t* loads,uint64_t* vertices,uint64_t* faces,
                      uint64_t* draws,uint64_t* submitted,uint64_t* skipped,uint64_t* failures);
#ifdef __cplusplus
}
#include <array>
#include <vector>
struct RageTrackFace {
    uint32_t vertex[4], uv[4], colour, window, surface_flags;
    uint16_t page,clut,type;
    int8_t depth_bias=0;
    uint32_t first_triangle=0,triangle_count=0;
    uint32_t decal=0; /* Geometrically contained, coplanar ground detail. */
    uint32_t free_run_routes=0; /* Zero: unrestricted; otherwise owning route bits. */
    uint32_t free_run_portal=0; /* Persistent entrance/exit apron on hidden routes. */
    uint32_t free_run_later=0; /* Hidden behind the main tunnels from the lower stretch. */
    uint32_t coverage=0; /* Open boundary needs conservative subpixel coverage. */
};
struct RageTrackTriangle {
    uint32_t vertex[3];
    float uv[6];
};
struct RageTrackSector {
    uint32_t first,count;
    int cell_x=-1,cell_z=-1;
    float lo[3],hi[3];
    std::vector<uint32_t> draw_order;
};
struct RageTrackRoutePoint { float x,y,z,left,right; };
struct RageTrackCourse {
    std::vector<RageTrackRoutePoint> route;
    bool visibility_valid=false;
    std::array<uint16_t,1024> cell_grid{};
    std::array<uint32_t,1024> cell_visibility{};
    std::vector<std::array<float,3>> vertices;
    std::vector<RageTrackFace> faces;
    std::vector<RageTrackSector> sectors;
    std::vector<RageTrackTriangle> triangles;
    uint32_t source_vertices=0,tjunctions=0;
};
bool rage_track_has_roof(const RageTrackCourse& course,float x,float y,float z);
std::vector<uint8_t> rage_track_tunnel_points(const RageTrackCourse& course);
bool rage_track_build_topology(RageTrackCourse& course);
uint32_t rage_track_stitch_geometry(RageTrackCourse& course);
uint32_t rage_track_classify_layers(RageTrackCourse& course);
uint32_t rage_track_conform_details(RageTrackCourse& course);
bool rage_track_sample_ground(const RageTrackCourse& course,float x,float z,float near_y,float& height);
bool rage_track_decode(CPUState* cpu,uint32_t grid,uint32_t table,uint32_t pool,
                       uint32_t sector_count,RageTrackCourse& course);
#endif
