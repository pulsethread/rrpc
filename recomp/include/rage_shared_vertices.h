#pragma once
#include <stdint.h>
#include <stdlib.h>
typedef struct RageSharedVertex {
    uint32_t frame;
    int32_t raw_x, raw_y, offset_x, offset_y, x, y;
    uint16_t z;
    uint8_t valid;
} RageSharedVertex;
typedef struct RageSharedVertices { RageSharedVertex slots[8192]; } RageSharedVertices;
/* Only reconcile depth-validated copies, inside a bounded rounding window.
 * Different cameras, frames, depths and distant coordinates remain separate. */
static inline void rage_shared_vertex(RageSharedVertices* cache, uint32_t frame,
    int32_t raw_x, int32_t raw_y, int32_t offset_x, int32_t offset_y,
    uint16_t z, int mode, int32_t* x, int32_t* y) {
    if (!mode || !z) return;
    const uint32_t bucket = ((uint32_t)z * 2654435761u) & 8191u;
    RageSharedVertex* free_slot = 0;
    for (unsigned i = 0; i < 64; ++i) {
        RageSharedVertex* v = &cache->slots[(bucket + i) & 8191u];
        if (!v->valid || v->frame != frame) { if (!free_slot) free_slot = v; continue; }
        if (v->z != z || v->offset_x != offset_x || v->offset_y != offset_y) continue;
        if (v->raw_x != raw_x || v->raw_y != raw_y) continue;
        if (llabs((int64_t)v->x - *x) > 65536 || llabs((int64_t)v->y - *y) > 65536) continue;
        *x = v->x; *y = v->y; return;
    }
    if (free_slot) {
        RageSharedVertex v = {frame, raw_x, raw_y, offset_x, offset_y, *x, *y, z, 1};
        *free_slot = v;
    }
}
