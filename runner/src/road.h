/* The road any track draws: cross-sections sampled along path space, so
 * yaw, grade, width, lane count, and missing deck all come from the baked
 * segments. A world supplies its light, its materials, and its own scenery
 * around the road. */

#pragma once

#include <stdint.h>
#include "course.h"
#include "level.h"

/* The materials of a road world. The air between them and the camera is
 * the world's Light: curbs, signs, the far deck, and the backdrop fade
 * toward its haze, and its tint lights the paint. */
typedef struct {
    const Light *light;
    uint8_t ridge_far[3];  /* backdrop silhouette, at the path horizon */
    uint8_t ridge_near[3];
    uint8_t road[3];       /* near asphalt */
    uint8_t road_far[3];   /* asphalt and crates haze toward this */
    uint8_t edge[3];       /* pulsing curb outside each edge */
    int edge_end;          /* curbs stop at this depth; 0 for none */
    uint8_t dash[3];       /* lane dividers */
    const ObstacleStyle *obstacle;
} RoadStyle;

/* Ridge silhouette by absolute heading, at path_horizon(). */
void road_backdrop(const RoadStyle *st);
/* Deck, curbs, dividers, and the obstacles on the drawn deck. */
void road_draw(const RoadStyle *st, const LevelFrame *f);
/* A flat panel across the road at one s, hazed into the air. Call after
 * road_draw in the same frame. */
void road_panel(int s, int u0, int u1, int v0, int v1, int r, int g, int b);
/* City-style overhead bar over the start line of a closed track, once a
 * lap. Nothing on a track that does not close. Call after road_draw. */
void road_start_gantry(int scroll, const uint8_t post[3], const uint8_t beam[3]);
