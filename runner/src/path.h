/* Path space for every track. s is distance along the road, u is lateral
 * offset from the centerline, v is height above the deck. The open span is
 * one period; past the lead, s repeats the cycle.
 *
 * path_cam follows the segment yaw, pitch, and roll. On straight.run all
 * are zero, so the frame stays (u, v, s - scroll). The camera takes 3/4 of
 * the grade under it and none of the roll, so a banked deck tilts on a
 * level horizon. The bake keeps a banked deck's low edge at grade. */

#pragma once

#include <stdint.h>
#include "world.h"

#define OBSTACLE_NONE (-128)

typedef struct {
    int32_t x, y, z;
    int32_t x1, y1, z1;
    int16_t yaw, pitch, roll;
    int16_t lanes;
    int16_t width;
    int16_t next;
    int16_t fork;
    int8_t deck;
    int8_t obstacle;
} PathSeg;

/* 1 when this build can run the named track. */
int path_open(const char *id);
/* Call once a frame before any path_cam or path_project. */
void path_begin(int scroll);
/* Camera yaw at the current path_begin scroll (0..4095). */
int path_yaw(void);
/* Screen y of the level horizon for this frame. HORIZON_Y when level. */
int path_horizon(void);
void path_cam(int s, int u, int v, int *x, int *y, int *z);
/* One sample: both road edges at ±half. Cheaper than two path_cam calls. */
void path_cam_lr(int s, int half, int v,
                 int *lx, int *ly, int *lz,
                 int *rx, int *ry, int *rz);
int path_project(int s, int u, int v, XY *out);
/* Feet at (s, u, v) and the deck's screen direction under them, a unit
 * vector in 4096ths: (4096, 0) on a level deck. A figure stands square to
 * the deck by building its quad along (ax, ay) and up along (ay, -ax). */
int path_stand(int s, int u, int v, XY *feet, int *ax, int *ay);
/* Segment containing s, or 0 when s is negative. */
const PathSeg *path_at(int s);
/* Segments in the open period, lead plus one cycle. */
int path_len(void);
/* Length of one lap when the track closes, else 0. */
int path_lap(void);
