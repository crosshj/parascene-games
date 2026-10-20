/* MAXI-P: the lane-fork lap, two forks that bulge out like wings on
 * either side of one long pad of road. This world exercises forks and joins: the
 * runner picks a road by lane, and the other road draws beside it. Blush
 * air, lavender ridges, rose curbs, mint crates. */

#include "world.h"
#include "course.h"
#include "level.h"
#include "road.h"

/* Mint crates, so they read against the pinks. */
static const ObstacleStyle k_obstacle = {
    {44, 104, 84}, {62, 140, 112}, {84, 172, 138}, {214, 255, 236}, {120, 212, 172}
};

static const Light k_light = {
    {232, 168, 196}, /* sky */
    {244, 202, 222}, /* haze */
    {150, 126, 140}, /* tint */
    {128, 72, 120},  /* shadow */
    120,             /* density */
};

static const RoadStyle k_road = {
    &k_light,
    {178, 140, 204}, /* far ridge */
    {192, 150, 212}, /* near ridge */
    {124, 100, 120}, /* asphalt */
    {160, 126, 156}, /* distant asphalt */
    {255, 96, 176},  /* rose curb glow */
    1900,
    {255, 240, 248}, /* pearl dividers */
    &k_obstacle,
    0, {0, 0, 0},
    0, {0, 0, 0},
};

#ifdef RUNNER_PROFILE
const RoadStyle *maxip_style = &k_road;
#endif

static void maxip_clear(const LevelFrame *f, int *r, int *g, int *b) {
    (void)f;
    *r = k_light.sky[0];
    *g = k_light.sky[1];
    *b = k_light.sky[2];
}

static const uint8_t k_gantry_post[3] = {244, 224, 236};
static const uint8_t k_gantry_beam[3] = {222, 160, 206};

static void maxip_draw(const LevelFrame *f) {
    road_backdrop(&k_road);
    road_draw(&k_road, f);
    road_start_gantry(f->scroll, k_gantry_post, k_gantry_beam);
}

const Level level_maxip = {
    "MAXI-P",
    "fork.lane",
    maxip_clear,
    maxip_draw,
    &k_light,
    0,
};
