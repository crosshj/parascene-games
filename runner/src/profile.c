#ifdef RUNNER_PROFILE

#include <stdio.h>
#include <stddef.h>
#include "profile.h"

#define PROF_FRAMES 900
#define CYCLES_PER_FRAME 564480 /* 33.8688 MHz at 60 Hz */
#define CYCLES_PER_LINE 2152

uint32_t prof_ticks[PF_COUNT];
uint32_t prof_lines_used[PF_COUNT];
uint32_t prof_projections;

static const char *const k_names[PF_COUNT] = {
    "game update", "level total", "runner+shadow+hud", "GPU drawing",
    "forest (cell loop)", "crystals", "mounds", "hero giants",
    "stalks near", "stalks mid", "stalks far",
    "ground streaks",
    "obstacles", "path", "spores", "speed streaks",
    "skyline", "aurora", "sky",
};

static int frames;
static size_t bytes_sum;

void prof_start(void) {
    TIMER_CTRL(1) = 0x0100;
    TIMER_CTRL(2) = 0x0200;
}

static void line(const char *name, uint32_t cycles) {
    uint32_t tenths = cycles * 1000 / CYCLES_PER_FRAME;
    printf("PROF   %-20s %7u cycles %3u.%u%%\n", name, (unsigned)cycles,
           (unsigned)(tenths / 10), (unsigned)(tenths % 10));
}

int prof_frame(const char *level, size_t bytes) {
    int i;
    uint32_t kinds = 0;
    bytes_sum += bytes;
    if (++frames < PROF_FRAMES)
        return 0;
    printf("PROF %s: %d frames, average per frame, %% of a 60 Hz frame\n", level, frames);
    for (i = PF_CRYSTAL; i <= PF_GSTREAK; i++)
        kinds += prof_ticks[i];
    for (i = 0; i < PF_COUNT; i++) {
        if (prof_lines_used[i])
            line(k_names[i], prof_lines_used[i] * CYCLES_PER_LINE / PROF_FRAMES);
        else if (prof_ticks[i])
            line(k_names[i], (i == PF_FOREST ? prof_ticks[i] - kinds : prof_ticks[i]) * 8 / PROF_FRAMES);
    }
    printf("PROF   %u projections, %u primitive bytes per frame\n",
           (unsigned)(prof_projections / frames), (unsigned)(bytes_sum / frames));
    for (i = 0; i < PF_COUNT; i++)
        prof_ticks[i] = prof_lines_used[i] = 0;
    prof_projections = 0;
    bytes_sum = 0;
    frames = 0;
    return 1;
}

#endif
