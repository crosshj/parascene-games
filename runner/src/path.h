/* Path space for every track. s is distance along the road, u is lateral
 * offset from the centerline, v is height above the deck. The open span is
 * one period; past the lead, s repeats the cycle.
 *
 * path_cam follows the segment yaw. On straight.run that yaw is zero, so
 * the frame stays (u, v, s - scroll). Pitch and roll stay level until a
 * track that needs them is runnable. */

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
void path_cam(int s, int u, int v, int *x, int *y, int *z);
int path_project(int s, int u, int v, XY *out);
/* Segment containing s, or 0 when s is negative. */
const PathSeg *path_at(int s);
/* Segments in the open period, lead plus one cycle. */
int path_len(void);
