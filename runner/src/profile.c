#ifdef RUNNER_PROFILE

#include <stdio.h>
#include <stddef.h>
#include "profile.h"

#ifndef PROF_FRAMES
#define PROF_FRAMES 900
#endif
#define CYCLES_PER_FRAME (564480 * 2) /* 33.8688 MHz across two NTSC blanks */
#define CYCLES_PER_LINE 2152

uint32_t prof_ticks[PF_COUNT];
uint32_t prof_lines_used[PF_COUNT];
uint32_t prof_projections;
uint32_t prof_samples;
uint32_t prof_n[PN_COUNT];
static uint32_t n_worst[PN_COUNT];
static const char *const n_names[PN_COUNT] = {
    "samples", "clear probes", "near ring", "far ring", "fork ring",
    "strips", "strip edges", "far strips", "boxes", "far crates", "fork near", "fork off",
    "transforms", "world rings", "world spans"
};

static const char *const k_names[PF_COUNT] = {
    "game update", "level total", "runner+shadow+hud", "GPU drawing",
    "forest (cell loop)", "crystals", "mounds", "hero giants",
    "stalks near", "stalks mid", "stalks far",
    "ground streaks",
    "obstacles", "path", "spores", "speed streaks",
    "skyline", "aurora", "sky",
    "road backdrop", "road near sampling", "road near strips",
    "road dividers", "road far tier", "road crates", "road other fork",
    "road gantry",
    " far sampling", " far strips", " far crates",
    " fork sampling", " fork strips", " fork dividers", " fork crates",
    "straight tunnel",
};

static int frames;
static size_t bytes_sum;
static uint32_t level_last, level_worst, level_worst_frame;
static uint32_t ticks_last[PF_COUNT], ticks_worst[PF_COUNT];
/* Frames whose level draw passed 80, 90, and 95 % of a 60 Hz frame. */
static uint32_t over[3];

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
    {
        uint32_t c = (prof_lines_used[PF_LEVEL] - level_last) * CYCLES_PER_LINE;
        over[0] += c > CYCLES_PER_FRAME / 10 * 8;
        over[1] += c > CYCLES_PER_FRAME / 10 * 9;
        over[2] += c > CYCLES_PER_FRAME / 20 * 19;
    }
    if (prof_lines_used[PF_LEVEL] - level_last > level_worst) {
        level_worst = prof_lines_used[PF_LEVEL] - level_last;
        level_worst_frame = frames;
        for (i = 0; i < PF_COUNT; i++)
            ticks_worst[i] = prof_ticks[i] - ticks_last[i];
        for (i = 0; i < PN_COUNT; i++)
            n_worst[i] = prof_n[i];
    }
    for (i = 0; i < PN_COUNT; i++)
        prof_n[i] = 0;
    level_last = prof_lines_used[PF_LEVEL];
    for (i = 0; i < PF_COUNT; i++)
        ticks_last[i] = prof_ticks[i];
    if (++frames < PROF_FRAMES)
        return 0;
    printf("PROF   worst level frame %u cycles at frame %u; frames over 80/90/95%%: %u %u %u\n",
           (unsigned)(level_worst * CYCLES_PER_LINE), (unsigned)level_worst_frame,
           (unsigned)over[0], (unsigned)over[1], (unsigned)over[2]);
    over[0] = over[1] = over[2] = 0;
    for (i = PF_R_BACKDROP; i < PF_COUNT; i++)
        if (ticks_worst[i])
            printf("PROF     worst %-20s %7u\n", k_names[i], (unsigned)(ticks_worst[i] * 8));
    for (i = 0; i < PN_COUNT; i++)
        printf("PROF     worst count %-14s %5u\n", n_names[i], (unsigned)n_worst[i]);
    level_last = level_worst = level_worst_frame = 0;
    for (i = 0; i < PF_COUNT; i++)
        ticks_last[i] = 0;
    printf("PROF %s: %d frames, average per frame, %% of a 30 Hz frame\n", level, frames);
    for (i = PF_CRYSTAL; i <= PF_GSTREAK; i++)
        kinds += prof_ticks[i];
    for (i = 0; i < PF_COUNT; i++) {
        if (prof_lines_used[i])
            line(k_names[i], prof_lines_used[i] * CYCLES_PER_LINE / PROF_FRAMES);
        else if (prof_ticks[i])
            line(k_names[i], (i == PF_FOREST ? prof_ticks[i] - kinds : prof_ticks[i]) * 8 / PROF_FRAMES);
    }
    printf("PROF   %u projections, %u path samples, %u primitive bytes per frame\n",
           (unsigned)(prof_projections / frames), (unsigned)(prof_samples / frames),
           (unsigned)(bytes_sum / frames));
    for (i = 0; i < PF_COUNT; i++)
        prof_ticks[i] = prof_lines_used[i] = 0;
    prof_projections = 0;
    prof_samples = 0;
    bytes_sum = 0;
    frames = 0;
    return 1;
}

#endif
