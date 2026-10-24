/* Frame cost measurement. Configure with -DRUNNER_PROFILE=ON and the disc
 * skips the menus, runs each level for a while with collisions off, and
 * prints where the frame went to the TTY. Otherwise PROF() is just its
 * statement. */

#pragma once

#include <stdint.h>

enum {
    PF_UPDATE, PF_LEVEL, PF_SPRITES, PF_GPU,
    PF_FOREST, PF_CRYSTAL, PF_MOUND, PF_HERO,
    PF_STALK_NEAR, PF_STALK_MID, PF_STALK_FAR,
    PF_GSTREAK,
    PF_OBSTACLES, PF_PATH, PF_SPORES, PF_STREAKS,
    PF_SKYLINE, PF_AURORA, PF_SKY,
    PF_R_BACKDROP, PF_R_SAMPLE, PF_R_STRIPS, PF_R_DASHES, PF_R_FAR,
    PF_R_CRATES, PF_R_BRANCH, PF_R_GANTRY,
    PF_REACH_CANYON, PF_REACH_MARKERS, PF_REACH_GANTRIES,
    PF_F_SAMPLE, PF_F_STRIPS, PF_F_CRATES,
    PF_B_SAMPLE, PF_B_STRIPS, PF_B_DASHES, PF_B_CRATES,
    PF_WORLD_MESH, PF_COUNT
};

/* Events counted per frame, reported for the worst frame. */
enum {
    PN_SAMPLE, PN_CLEAR, PN_NEAR, PN_FAR, PN_FORK, PN_STRIP, PN_EDGES, PN_FAR_STRIP,
    PN_BOX, PN_FAR_CRATE, PN_FORK_NEAR, PN_FORK_OFF,
    PN_TRANSFORM, PN_WORLD_RING, PN_WORLD_SPAN, PN_COUNT
};

#ifdef RUNNER_PROFILE
#include <hwregs_c.h>

extern uint32_t prof_ticks[PF_COUNT];
extern uint32_t prof_projections;
extern uint32_t prof_samples;
extern uint32_t prof_n[PN_COUNT];
#define PROF_N(i) (prof_n[i]++)

/* Root counter 2 at the system clock / 8 wraps after 524288 cycles, so it
 * times the parts. Root counter 1 counts scanlines, 2152 cycles each, and
 * times whole phases that can run longer than a frame. */
static inline uint32_t prof_now(void) { return TIMER_VALUE(2); }
static inline uint32_t prof_lines(void) { return TIMER_VALUE(1); }

#define PROF(id, stmt) do { \
        uint32_t prof_t0_ = prof_now(); \
        stmt; \
        prof_ticks[id] += (prof_now() - prof_t0_) & 0xFFFF; \
    } while (0)
#define PROF_LONG(id, stmt) do { \
        uint32_t prof_t0_ = prof_lines(); \
        stmt; \
        prof_lines_used[id] += (prof_lines() - prof_t0_) & 0xFFFF; \
    } while (0)

extern uint32_t prof_lines_used[PF_COUNT];
#define PROF_COUNT_PROJECTION() (prof_projections++)
#define PROF_COUNT_SAMPLE() (prof_samples++, prof_n[PN_SAMPLE]++)

void prof_start(void);
void prof_present(unsigned blanks);
/* Returns 1 when the current level has run long enough and was reported. */
int prof_frame(const char *level, size_t bytes);
#else
#define PROF(id, stmt) stmt
#define PROF_LONG(id, stmt) stmt
#define PROF_COUNT_PROJECTION() ((void)0)
#define PROF_COUNT_SAMPLE() ((void)0)
#define PROF_N(i) ((void)0)
#endif
