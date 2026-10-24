/* Path space for every track. s is distance along the road, u is lateral
 * offset from the centerline, v is height above the deck. The open span is
 * one period; past the lead, s repeats the cycle.
 *
 * A road with cycle 0 ends. Past its last segment the deck runs straight
 * on the heading it finished with, and it does not come back to the start.
 *
 * path_cam follows the segment yaw, pitch, and roll. On straight.run all
 * are zero, so the frame stays (u, v, s - scroll). The camera takes 3/4 of
 * the grade under it and none of the roll, so a banked deck tilts on a
 * level horizon. The bake keeps a banked deck's low edge at grade.
 *
 * A fork is a second road beside the main one for a run of bands; both
 * share s. The route is which road each fork follows. The runner picks it
 * at the fork with path_follow; the camera and every sample follow the
 * route. The centerline can jump sideways where a road starts beside
 * another (a fork, a join); path_follow reports the jump so the runner
 * stays put, and the camera eases across it. */

#pragma once

#include <stdint.h>
#include "world.h"

#define OBSTACLE_NONE (-128)
#define PATH_LANES 3

typedef struct {
    int32_t x, y, z;
    int32_t x1, y1, z1;
    int16_t yaw, pitch, roll;
    int16_t el, er, el1, er1;               /* road edges, u at each end */
    int16_t lu[PATH_LANES], lu1[PATH_LANES]; /* lane centers at each end */
    int16_t mu[2], mu1[2];                   /* lane dividers at each end */
    int16_t next;
    int16_t fork;
    int8_t lanes;    /* entries in lu; a lane change lists its widest count */
    int8_t marks;    /* entries in mu */
    int8_t deck;
    int8_t obstacle; /* u in half lanes, or OBSTACLE_NONE */
    int8_t curbs;    /* PATH_CURB_L | PATH_CURB_R: off where another road meets this edge */
} PathSeg;

#define PATH_CURB_L 1
#define PATH_CURB_R 2

/* 1 when this build can run the named track. Reopening the open track
 * keeps its route. */
int path_open(const char *id);
/* Every fork back on the main road. */
void path_reset(void);
/* Call once a frame before any path_cam or path_project. */
void path_begin(int scroll);
/* Camera yaw at the current path_begin scroll (0..4095). */
int path_yaw(void);
/* Screen y of the level horizon for this frame. HORIZON_Y when level. */
int path_horizon(void);
void path_cam(int s, int u, int v, int *x, int *y, int *z);
void path_cam_pair(int s, int u0, int v0, int u1, int v1,
                   int *x0,int *y0,int *z0,int *x1,int *y1,int *z1);
/* Prepared static scenery uses the same camera/GTE without resampling the path. */
void path_transform(int wx, int wy, int wz, int *x, int *y, int *z);
/* Camera rotation only, for a baked mesh's shared basis vectors. */
void path_rotate(int wx, int wy, int wz, int *x, int *y, int *z);
const char *path_id(void);
uint32_t path_view_revision(void);
int path_is_straight(void);
#ifdef RUNNER_PREVIEW
extern unsigned long path_preview_samples, path_preview_transforms;
#endif
/* One sample: two points across the deck at ul and ur. Cheaper than two
 * path_cam calls. */
void path_cam_lr(int s, int ul, int ur, int v,
                 int *lx, int *ly, int *lz,
                 int *rx, int *ry, int *rz);
int path_project(int s, int u, int v, XY *out);
/* path_edges and path_cam_lr at once: the deck's edges at s and both in
 * camera space, and each curb's width there, 0..4096 of full: 0 where the
 * road has none, and narrowing to a point across a band where a curb
 * starts or stops while the road runs on. Returns path_at(s). */
const PathSeg *path_deck_lr(int s, int *ul, int *ur,
                            int *lx, int *ly, int *lz,
                            int *rx, int *ry, int *rz, int *kl, int *kr);
/* How far the centerline at s is outside the view, less margin, or past
 * zmax: 0 when it may be in view. The road moves no faster than s, so
 * road within that distance of s stays out of view too. A cheap test that
 * skips sampling road nobody can see. The view reaches 40 px past each
 * screen edge. */
int path_view_clear(int s, int margin, int zmax);
/* Feet at (s, u, v) and the deck's screen direction under them, a unit
 * vector in 4096ths: (4096, 0) on a level deck. A figure stands square to
 * the deck by building its quad along (ax, ay) and up along (ay, -ax). */
int path_stand(int s, int u, int v, XY *feet, int *ax, int *ay);
/* Segment containing s on the route, or 0 when s is negative. */
const PathSeg *path_at(int s);
/* While on, a sample exactly on a band edge reads the end of the band
 * before it rather than the start of the band after: the last point of a
 * road that jumps there. path_cam_lr and path_edges honor it. */
void path_end_side(int on);
/* 1 where the road does not run on from the band before into this one:
 * the centerline jumps or the edges do not meet. */
int path_cut(int band);
/* The first band from band0 to band1 where path_cut holds, or -1. */
int path_first_cut(int band0, int band1);
/* 0 when no road at this band holds a crate on any route; cheap, for
 * skipping bands. */
int path_may_crate(int band);
/* Road edges at s. */
void path_edges(int s, int *ul, int *ur);
/* Lane centers at s; returns how many. Lanes that merge share a u. */
int path_lanes(int s, int *u);
/* Divider lines at s; returns how many. */
int path_marks(int s, int *u);
/* The lane a runner at u belongs to at s. Where a lane opens beside it,
 * the one that holds still. */
int path_lane_near(int s, int u);
/* The runner at s with lateral offset u, once a frame. Picks the road at a
 * fork by the lane u is in. Returns how far the centerline moved sideways
 * since the last call; subtract it from u to stay put. */
int path_follow(int s, int u);
/* Segments in the open period, lead plus one cycle. */
int path_len(void);
/* Length of one lap when the track closes, else 0. */
int path_lap(void);
/* Forks of the open track. path_branch gives the next run of the fork at
 * or past s, as absolute s; 0 when none. path_swap flips the fork onto its
 * other road for drawing; call it again to restore the route. */
int path_branches(void);
int path_branch(int k, int s, int *s0, int *s1);
void path_swap(int k);
