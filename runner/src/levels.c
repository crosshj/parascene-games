#include "level.h"

extern const Level level_city;
extern const Level level_lumenwild;
extern const Level level_vesper;
extern const Level level_shatter;
extern const Level level_reach;
extern const Level level_sine;
extern const Level level_meridian;
extern const Level level_cathedral;
extern const Level level_oval;
extern const Level level_hill;
extern const Level level_bank;
extern const Level level_zipper;
extern const Level level_maxip;

/* Profile and the preview walk every level. Story Mode offers the ones
 * with story set; Arcade Mode offers the rest, in this order. */
const Level *const levels[] = {
    &level_lumenwild,
    &level_vesper,
    &level_shatter,
    &level_reach,
    &level_sine,
    &level_meridian,
    &level_cathedral,
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
