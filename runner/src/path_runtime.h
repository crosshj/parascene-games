/* Immutable path preparation, compiled for every runnable track. Scene LOD
 * belongs to the world package's distance snapshots, not the path runtime. */
#pragma once
#include <stdint.h>
#define PATH_RUNTIME_VERSION 2
typedef struct {
    const uint16_t *frames;
    const int8_t *branches;
    const uint8_t *flags, *clear, *taper;
    uint16_t bands, segments;
} PathRuntime;
