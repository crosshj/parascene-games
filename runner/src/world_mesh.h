/* Version 1 resident static scenery. No per-frame allocation or event cursor:
 * distance-indexed snapshots also support restart, pause and arbitrary seeks. */
#pragma once
#include <stdint.h>

typedef struct { int32_t x, y, z; } WorldVertex;
typedef struct { WorldVertex origin; int16_t offset[5][3]; } WorldRing;
typedef struct { uint32_t first; uint16_t count, reserved; } WorldSchedule;
typedef struct {
    const char *track;
    const WorldRing *rings;
    const WorldSchedule *schedules;
    const uint8_t *indices; /* ring offsets relative to max(0, band - 1) */
    uint16_t schedule_count, version;
    uint16_t axis_aligned, reserved;
} WorldMesh;
typedef char world_ring_size_check[sizeof(WorldRing) == 44 ? 1 : -1];
typedef char world_schedule_size_check[sizeof(WorldSchedule) == 8 ? 1 : -1];

#ifdef RUNNER_PREVIEW
extern int world_mesh_preview_legacy;
extern int world_mesh_preview_depth;
#endif
