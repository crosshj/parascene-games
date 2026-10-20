/* ZIPPER: the lane-count lap. The road narrows to one lane and widens back
 * to three on every straight, so this world exercises lane changes under
 * all three anchors. Dusk: an indigo sky over a coral haze, amber curbs
 * that trace each edge as it moves. */

#include "world.h"
#include "course.h"
#include "level.h"
#include "road.h"

/* Teal crates, the cool answer to the amber curbs. */
static const ObstacleStyle k_obstacle = {
    {14, 64, 72}, {20, 92, 102}, {28, 118, 130}, {150, 236, 226}, {38, 160, 170}
};

static const Light k_light = {
    {38, 30, 82},    /* sky */
    {196, 116, 108}, /* haze */
    {150, 126, 124}, /* tint */
    {70, 56, 120},   /* shadow */
    150,             /* density */
};

static const RoadStyle k_road = {
    &k_light,
    {82, 50, 98},    /* far ridge */
    {96, 58, 104},   /* near ridge */
    {82, 82, 96},    /* asphalt */
    {104, 76, 92},   /* distant asphalt */
    {255, 160, 36},  /* amber curb glow */
    1900,
    {244, 232, 204}, /* dividers */
    &k_obstacle,
    0, {0, 0, 0},
    0, {0, 0, 0},
};

static void zipper_clear(const LevelFrame *f, int *r, int *g, int *b) {
    (void)f;
    *r = k_light.sky[0];
    *g = k_light.sky[1];
    *b = k_light.sky[2];
}

static const uint8_t k_gantry_post[3] = {120, 112, 136};
static const uint8_t k_gantry_beam[3] = {200, 128, 52};

static void zipper_draw(const LevelFrame *f) {
    road_backdrop(&k_road);
    road_draw(&k_road, f);
    road_start_gantry(f->scroll, k_gantry_post, k_gantry_beam);
}

const Level level_zipper = {
    "ZIPPER",
    "lanes.count",
    zipper_clear,
    zipper_draw,
    &k_light,
    0,
};
