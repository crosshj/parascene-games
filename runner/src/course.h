/* The course every level runs. The road is the baked span in path.h:
 * a lead, then a cycle that repeats. Levels change how it looks. */

#pragma once

#include <stdint.h>
#include "world.h"

#define STEP 256
#define CHAR_Z 200
#define LANE 150
#define ROAD_HALF 280

#define OBST_H 52
#define OBST_D 112

/* Bands of STEP along the span. A band's obstacle sits at
 * band * STEP + STEP / 2. Past the lead, bands repeat the cycle. */
int has_obstacle(int band);
/* The obstacle's u across the road. */
int obstacle_u(int band);

/* Face colors for an obstacle box. */
typedef struct {
    uint8_t back[3], left[3], right[3], top[3], front[3];
} ObstacleStyle;

/* s is the obstacle center along the road. Draw after path_begin.
 * obstacle_box uses dim(); pass shade for aerial perspective, etc. */
void obstacle_box(int s, int u, const ObstacleStyle *style);
void obstacle_box_shade(int s, int u, const ObstacleStyle *style, Shade shade);

/* Marks on the boxes of one stage. A jumped box draws bright, a hit box
 * draws in the negative of that color. Bands are absolute; base_band is
 * the stage's first band, and the table covers one 3-minute run. */
enum { BOX_NONE = 0, BOX_JUMP = 1, BOX_HIT = 2 };

void course_boxes_reset(int base_band);
/* Obstacle centers in [s0, s1) are absent. s0 >= s1 clears the span. */
void course_quiet(int s0, int s1);
void course_box_set(int band, int kind);
int course_box_kind(int band);
/* base when the band has no mark. */
const ObstacleStyle *course_box_style(int band, const ObstacleStyle *base);
