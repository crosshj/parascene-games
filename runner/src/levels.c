#include "level.h"

extern const Level level_city;
extern const Level level_lumenwild;

/* The select screen lists levels in this order; the first is the default. */
const Level *const levels[] = {
    &level_lumenwild,
    &level_city,
};

const int level_count = sizeof levels / sizeof levels[0];
