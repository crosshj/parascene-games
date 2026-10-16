/* The course every level runs. The road is the baked span in path.h:
 * a lead, then a cycle that repeats. Levels change how it looks. */

#pragma once

#include <stdint.h>

#define STEP 256
#define CHAR_Z 200
#define LANE 150
#define ROAD_HALF 280

#define OBST_H 52
#define OBST_D 112

/* Bands of STEP along the span. A band's obstacle sits at
 * band * STEP + STEP / 2. Past the lead, bands repeat the cycle. */
int has_obstacle(int band);
int obstacle_lane(int band);

/* Face colors for an obstacle box, with dim() distance falloff. */
typedef struct {
    uint8_t back[3], left[3], right[3], top[3], front[3];
} ObstacleStyle;

/* s is the obstacle center along the road. Draw after path_begin. */
void obstacle_box(int s, int lane_i, const ObstacleStyle *style);
