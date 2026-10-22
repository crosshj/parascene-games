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
    /* Ground outside each curb, in the same u as the deck. 0 draws none.
     * shoulder is the near band; ground is the wider floor under it. */
    int shoulder;
    uint8_t shoulder_c[3];
    int ground;
    uint8_t ground_c[3];
} RoadStyle;

/* Ridge silhouette by absolute heading, at path_horizon(). */
void road_backdrop(const RoadStyle *st);
/* Deck, curbs, dividers, and the obstacles on the drawn deck. */
void road_draw(const RoadStyle *st, const LevelFrame *f);
/* Infinite flat continuation into the same fog endpoint as the scenery. */
void road_to_vanishing_point(int scroll);
/* A flat panel across the road at one s, hazed into the air. Call after
 * road_draw in the same frame. */
void road_panel(int s, int u0, int u1, int v0, int v1, int r, int g, int b);
/* Overhead bar at one s: two posts and a beam. Call after road_draw. */
void road_gantry(int s, const uint8_t post[3], const uint8_t beam[3]);
/* That bar over the start line of a closed track, once a lap. Nothing on
 * a track that does not close. Call after road_draw. */
void road_start_gantry(int scroll, const uint8_t post[3], const uint8_t beam[3]);

/* Motion lines around the runner. radius and crown are the tube's
 * half-width and height; 0 is the open-road size. A set radius keeps the
 * lines inside a cave or a crack. Call after road_draw. The tint is
 * pulled toward a glassy mid color. */
/* radius < 0: default geometry as a foreground overlay; 0: depth sorted. */
void road_streaks(const LevelFrame *f, int r, int g, int b, int radius, int crown);
/* Keep the runner above foreground streaks, otherwise retain its depth. */
int road_runner_bucket(int depth_bucket);

/* A quad along the road from s0 to s1. side -1 stands off the left edge,
 * +1 off the right, and 0 uses out0 and out1 as centerline u. out is how
 * far past that edge. The quad runs from (out0, v0) to (out1, v1). A side
 * that would cross another road is skipped. A long span is split so the
 * pieces share an edge, and a piece that crosses the camera is clipped,
 * so abutting spans stay closed up to the screen. Returns 0 when the
 * frame has no room left for scenery. Call after road_draw, so a deck
 * already in the bucket stays in front of the quad. */
int road_cloth(int s0, int s1, int side, int out0, int v0, int out1, int v1,
               int r, int g, int b);
/* Ceiling from one edge to the other, `out` past each edge, at height v. */
int road_lintel(int s0, int s1, int out, int v, int r, int g, int b);
/* A card facing along the road, centered at u, `half` wide. Skipped when
 * it would sit on the deck or on the other road of a fork. */
int road_marker(int s, int u, int half, int v0, int v1, int r, int g, int b);
/* The next sheet in a run that starts at s0. step is its length beside
 * the camera; farther sheets are longer, and shorter through a bend, and
 * the run stops where the road's far deck stops. reach is how far from
 * the centerline the sheet extends. *s1 is the end. Returns 1 to draw
 * s0..*s1, 2 to advance without drawing (the stretch is outside the
 * view), or 0 when the run is finished. */
int road_span(int s0, int scroll, int step, int reach, int *s1);
