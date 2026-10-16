#include "level.h"

extern const Level level_city;
extern const Level level_lumenwild;
extern const Level level_oval;
extern const Level level_hill;
extern const Level level_bank;
extern const Level level_zipper;
extern const Level level_maxip;

/* The select screen lists levels in this order; the first is the default. */
const Level *const levels[] = {
    &level_lumenwild,
    &level_city,
    &level_oval,
    &level_hill,
    &level_bank,
    &level_zipper,
    &level_maxip,
};

const int level_count = sizeof levels / sizeof levels[0];

const Light light_neutral = {
    {0, 0, 0},
    {0, 0, 0},
    {128, 128, 128},
    {128, 128, 128},
    0,
};

const Light *level_light(const Level *lv) {
    return lv->light ? lv->light : &light_neutral;
}
