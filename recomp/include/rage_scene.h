#pragma once
#include <stdint.h>
#include "render_pass_motion.h"
#ifdef __cplusplus
extern "C" {
#endif
struct CPUState;
/* Immutable pose receipt carried with a triangle to the render thread. */
typedef struct RageSceneVertex {
    uint32_t node, history, flags;
    float fade_start; /* Host clock seconds; zero means fully visible. */
    PSXMotionMatrix previous, current;
    float region[4]; /* Shared authored face bounds: min U/V, max U/V. */
    float local[3], projection[4]; /* Hx, Hy, centre X, centre Y */
    float drawing[12]; /* Native floating-point model/view rows for the GPU. */
    float camera[6], drawing_camera[3]; /* Previous/current world-space camera origins. */
    float translation[6]; /* Optional unrounded previous/current camera translations. */
} RageSceneVertex;
enum { RAGE_SCENE_FLOAT_TRANSLATION=1, RAGE_SCENE_MIRRORED_VIEW=2, RAGE_SCENE_DECAL=4, RAGE_SCENE_CAMERA=8, RAGE_SCENE_REGION=16, RAGE_SCENE_REPEAT_U=32, RAGE_SCENE_REPEAT_V=64, RAGE_SCENE_SOLID=128, RAGE_SCENE_BACKGROUND=256, RAGE_SCENE_MATERIAL_REGION=512, RAGE_SCENE_UI=1024, RAGE_SCENE_GPU_CLIP=2048, RAGE_SCENE_WALL_CULL=4096, RAGE_SCENE_COVERAGE=8192 };
void rage_scene_enable(int enabled);
int rage_scene_racing(void);
int rage_scene_full_width(void);
void rage_scene_set_original_lod(int enabled);
int rage_scene_original_lod(void);
uint64_t rage_scene_high_lod_draws(void);
uint64_t rage_scene_actor_groundings(void);
int rage_scene_actor_lift_milli(void);
void rage_scene_reset(void);
void rage_scene_dispatch(struct CPUState* cpu, uint32_t target);
int rage_scene_tread_winding(struct CPUState* cpu);
uint64_t rage_scene_tread_faces(void);
uint32_t rage_scene_pose(const int16_t rotation[9], const int32_t translation[3],
                         float hx, float hy, float ox, float oy);
int rage_scene_vertex(uint32_t node, const float local[3], RageSceneVertex* out);
int rage_scene_evaluate(const RageSceneVertex* vertex, double phase, double camera[3]);
int rage_scene_camera_affine(const RageSceneVertex* camera, double phase, float rotation[9], float translation[3]);
void rage_scene_prepare(RageSceneVertex* vertex, double phase);
int rage_scene_camera_continuous(const RageSceneVertex* older,const RageSceneVertex* newer);
void rage_scene_stats(uint64_t* poses, uint64_t* wheels, uint64_t* paired);
#ifdef __cplusplus
}
#endif
