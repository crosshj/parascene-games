/* BANK: the viewer's banked lap, with nothing around the road. The camera
 * stays level, so this world exercises roll: the deck tilts on a flat
 * horizon through both turn-arounds and the S. */

#include "world.h"
#include "course.h"
#include "level.h"
#include "road.h"

/* Warm amber crates, so they stand off both the green air and the curbs. */
static const ObstacleStyle k_obstacle = {
    {104, 64, 16}, {144, 90, 22}, {182, 118, 30}, {255, 220, 120}, {226, 150, 44}
};

/* Green air: a deep green sky, a lighter green haze that takes the far road
 * and the ridges, green light on the runner and the paint, a violet shadow
 * that answers the curbs. */
static const Light k_light = {
    {44, 104, 72},   /* sky */
    {76, 156, 96},   /* haze */
    {104, 150, 112}, /* tint */
    {88, 64, 112},   /* shadow */
    144,             /* density */
};

static const RoadStyle k_road = {
    &k_light,
    {52, 112, 82},   /* far ridge */
    {60, 124, 90},   /* near ridge */
    {88, 92, 90},    /* asphalt */
    {60, 72, 66},    /* distant asphalt */
    {255, 40, 200},  /* fuchsia curb glow */
    1900,
    {226, 236, 206}, /* dividers */
    &k_obstacle,
    0, {0, 0, 0},
    0, {0, 0, 0},
};

static void bank_clear(const LevelFrame *f, int *r, int *g, int *b) {
    (void)f;
    *r = k_light.sky[0];
    *g = k_light.sky[1];
    *b = k_light.sky[2];
}

static const uint8_t k_gantry_post[3] = {150, 164, 154};
static const uint8_t k_gantry_beam[3] = {116, 134, 124};

static void bank_draw(const LevelFrame *f) {
    road_backdrop(&k_road);
    road_draw(&k_road, f);
    road_start_gantry(f->scroll, k_gantry_post, k_gantry_beam);
}

const Level level_bank = {
    "BANK",
    "bank.bend",
    bank_clear,
    bank_draw,
    &k_light,
    0,
};
