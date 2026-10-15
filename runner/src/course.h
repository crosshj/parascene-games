/* The course every level runs: three lanes and the obstacles in them.
 * Levels change how the course looks, never where things are. */

#pragma once

#include <stdint.h>

#define STEP 256
#define CHAR_Z 200
#define LANE 150
#define ROAD_HALF 280

#define OBST_H 52
#define OBST_D 112

/* The world is cut into bands of STEP along z. A band's obstacle sits at
 * band * STEP + STEP / 2. */
int has_obstacle(int band);
int obstacle_lane(int band);

/* Face colors for an obstacle box, with dim() distance falloff. */
typedef struct {
    uint8_t back[3], left[3], right[3], top[3], front[3];
} ObstacleStyle;

void obstacle_box(int zc, int lane_i, const ObstacleStyle *style);
