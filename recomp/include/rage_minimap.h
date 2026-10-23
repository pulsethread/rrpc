#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { RAGE_MINIMAP_SIZE=192 };
struct CPUState;
/* CPU publishes immutable race data; the presenter only copies a bitmap. */
void rage_minimap_route(const float* xz,int count);
void rage_minimap_routes(const float* xz,const int* counts,int roads);
void rage_minimap_capture(struct CPUState* cpu);
void rage_minimap_clear(void);
int rage_minimap_image(uint32_t* argb,int capacity);
#ifdef __cplusplus
}
#endif
