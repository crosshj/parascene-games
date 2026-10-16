/* HILL: the viewer's hill loop, with nothing around the road. The camera
 * takes the grade and the turns, so this world exercises pitch and yaw. */

#include "world.h"
#include "course.h"
#include "level.h"
#include "road.h"

static const ObstacleStyle k_obstacle = {
    {40, 70, 96}, {52, 92, 124}, {68, 116, 150}, {150, 210, 236}, {92, 150, 186}
};

/* Red air: a deep red sky, thick red haze that takes the far road and the
 * ridges, red light on the runner and the paint, a cool shadow under it. */
static const Light k_light = {
    {148, 56, 52},   /* sky */
    {196, 52, 40},   /* haze */
    {156, 100, 92},  /* tint */
    {72, 108, 100},  /* shadow */
    176,             /* density */
};

static const RoadStyle k_road = {
    &k_light,
    {128, 96, 104},  /* far ridge */
    {146, 110, 112}, /* near ridge */
    {96, 86, 88},    /* asphalt */
    {74, 62, 70},    /* distant asphalt */
    {170, 255, 60},  /* lime curb glow */
    1900,
    {232, 214, 168}, /* dividers */
    &k_obstacle,
};

static void hill_clear(const LevelFrame *f, int *r, int *g, int *b) {
    (void)f;
    *r = k_light.sky[0];
    *g = k_light.sky[1];
    *b = k_light.sky[2];
}

/* Warm steel, so the bar sits in the dusk instead of reading blue. */
static const uint8_t k_gantry_post[3] = {168, 150, 144};
static const uint8_t k_gantry_beam[3] = {138, 118, 116};

static void hill_draw(const LevelFrame *f) {
    road_backdrop(&k_road);
    road_draw(&k_road, f);
    road_start_gantry(f->scroll, k_gantry_post, k_gantry_beam);
}

const Level level_hill = {
    "HILL",
    "hill.loop",
    hill_clear,
    hill_draw,
    &k_light,
};
