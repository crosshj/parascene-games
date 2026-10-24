/* FLAT OVAL: the viewer's closed lap, with nothing around the road.
 * The camera follows the yaw, so this is the world that exercises a turn. */

#include "world.h"
#include "course.h"
#include "level.h"
#include "road.h"

/* Same orange crate look as City Run; positions come from track catalog data. */
static const ObstacleStyle k_obstacle = {
    {110, 40, 16}, {150, 52, 20}, {186, 72, 28}, {255, 196, 96}, {232, 108, 36}
};

/* Cool, clear air: the runner and the paint go a little blue and gray. */
static const Light k_light = {
    {78, 112, 148},  /* sky */
    {78, 112, 148},  /* haze */
    {104, 114, 132}, /* tint */
    {70, 82, 104},   /* shadow */
    0,               /* density */
    {0,0,0}, /* Automatic ambient fill. */
};

static const RoadStyle k_road = {
    &k_light,
    {66, 96, 128},   /* far ridge */
    {72, 104, 136},  /* near ridge */
    {100, 96, 88},   /* asphalt */
    {62, 66, 74},    /* distant asphalt */
    {110, 245, 230}, /* teal curb glow */
    1900,
    {214, 198, 140}, /* dividers */
    &k_obstacle,
    0, {0, 0, 0},
    0, {0, 0, 0},
    0, /* No separately drawn analytic continuation. */
    0, {0,0,0}, /* Legacy single-color atmosphere. */
};

static void oval_clear(const LevelFrame *f, int *r, int *g, int *b) {
    (void)f;
    *r = k_light.sky[0];
    *g = k_light.sky[1];
    *b = k_light.sky[2];
}

static const uint8_t k_gantry_post[3] = {132, 140, 148};
static const uint8_t k_gantry_beam[3] = {112, 120, 128};

static void oval_draw(const LevelFrame *f) {
    road_backdrop(&k_road);
    road_draw(&k_road, f);
    road_start_gantry(f->scroll, k_gantry_post, k_gantry_beam);
}

const Level level_oval = {
    "FLAT OVAL",
    "bend.flat",
    oval_clear,
    oval_draw,
    &k_light,
    0,
    0,
    0,
};
