/* Version 1 resident static scenery. No per-frame allocation or event cursor:
 * distance-indexed snapshots also support restart, pause and arbitrary seeks. */
#pragma once
#include <stdint.h>

typedef struct { int32_t x, y, z; } WorldVertex;
typedef struct { WorldVertex origin; int16_t offset[5][3]; uint16_t flags; } WorldRing;
/* Paired boundary faces in world coordinates, shared by both route packages.
 * flags bit 0 marks the inner island; the upper bits identify the fork.
 * Outer boundaries also supply the continuous floor beneath both roads. */
typedef struct {
    int32_t start, end;
    WorldVertex origin;
    int16_t offset[7][3];
    uint16_t flags;
} WorldForkWall;
#define WORLD_FORK_ISLAND 1
static inline WorldVertex world_fork_vertex(const WorldForkWall *w,int i) {
    WorldVertex p=w->origin;
    if(i) { p.x+=w->offset[i-1][0];p.y+=w->offset[i-1][1];p.z+=w->offset[i-1][2]; }
    return p;
}
typedef char world_fork_size_check[sizeof(WorldForkWall) == 64 ? 1 : -1];
#define WORLD_RING_CUT_BEFORE 1
typedef struct { uint32_t first; uint16_t count, first_offset; } WorldSchedule;
typedef struct {
    const char *track;
    const WorldRing *rings;
    const WorldSchedule *schedules;
    const uint8_t *indices; /* packed 6-bit forward ring strides; first ring is band-1 */
    const WorldForkWall *fork_walls;
    uint16_t fork_wall_count;
    uint16_t schedule_count, version;
    uint16_t axis_aligned, near_distance;
    uint8_t route_mask, stride_bits; /* route variant and packed LOD gap width */
    uint32_t lookahead;
    const uint16_t *ring_ids; /* optional exact shared-vertex pool mapping */
} WorldMesh;
#ifndef RUNNER_PREVIEW
typedef char world_mesh_size_check[sizeof(WorldMesh) == 40 ? 1 : -1];
#endif
static inline const WorldRing *world_mesh_ring(const WorldMesh *m,int i) {
    return &m->rings[m->ring_ids?m->ring_ids[i]:i];
}
typedef char world_ring_size_check[sizeof(WorldRing) == 44 ? 1 : -1];
typedef char world_schedule_size_check[sizeof(WorldSchedule) == 8 ? 1 : -1];

#ifdef RUNNER_PREVIEW
extern int world_mesh_preview_legacy;
extern int world_mesh_preview_depth;
#endif
