/* Vesper Reach: a stone road on a ridge. The shoulder is a short lip and
 * the ground beyond it is a dark drop, with the gorge wall falling away
 * past that lip. A few stones stand on the edge. The air thickens from a
 * clear ridge, to valley haze, to a near-black purple. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"

#define EDGE_END 2000
#define SHOULDER_W 140
#define GROUND_W 480

typedef struct {
    uint8_t sky[3], haze[3], tint[3], shadow[3], top[3];
    int density;
    uint8_t ridge_far[3], ridge_near[3];
    uint8_t asphalt[3], asphalt_far[3], curb[3], dash[3];
    uint8_t shoulder_c[3], ground_c[3];
} Look;

static const ObstacleStyle k_obstacle = {
    {48, 40, 56}, {64, 54, 72}, {80, 68, 88}, {200, 170, 90}, {110, 90, 70}
};

static const Look k_look[3] = {
    {
        {72, 40, 128}, {120, 86, 160}, {140, 118, 156}, {48, 32, 72}, {20, 10, 52},
        36,
        {40, 22, 72}, {56, 32, 88},
        {78, 70, 84}, {52, 42, 64}, {186, 154, 72}, {168, 156, 140},
        {58, 48, 70}, {18, 10, 32}
    },
    {
        {40, 20, 84}, {78, 52, 108}, {130, 110, 146}, {36, 24, 56}, {12, 6, 36},
        100,
        {28, 14, 52}, {40, 22, 68},
        {64, 56, 72}, {40, 32, 52}, {196, 160, 64}, {150, 140, 128},
        {42, 32, 56}, {12, 6, 24}
    },
    {
        {14, 6, 28}, {32, 16, 44}, {120, 100, 140}, {24, 16, 40}, {4, 2, 12},
        156,
        {16, 8, 32}, {24, 12, 40},
        {40, 34, 48}, {20, 14, 30}, {214, 168, 70}, {130, 120, 110},
        {28, 18, 40}, {6, 2, 14}
    },
};

static Light live;
static RoadStyle road;
static uint8_t sky_top[3];
static int shown = -1;

static void rgb(uint8_t *d, const uint8_t *s) {
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
}

static void apply(int stage) {
    const Look *L;
    if (stage < 0)
        stage = 0;
    if (stage > 2)
        stage = 2;
    if (stage == shown)
        return;
    shown = stage;
    L = &k_look[stage];
    rgb(live.sky, L->sky);
    rgb(live.haze, L->haze);
    rgb(live.tint, L->tint);
    rgb(live.shadow, L->shadow);
    live.density = L->density;
    rgb(sky_top, L->top);
    road.light = &live;
    road.obstacle = &k_obstacle;
    road.edge_end = EDGE_END;
    road.shoulder = SHOULDER_W;
    road.ground = GROUND_W;
    rgb(road.ridge_far, L->ridge_far);
    rgb(road.ridge_near, L->ridge_near);
    rgb(road.road, L->asphalt);
    rgb(road.road_far, L->asphalt_far);
    rgb(road.edge, L->curb);
    rgb(road.dash, L->dash);
    rgb(road.shoulder_c, L->shoulder_c);
    rgb(road.ground_c, L->ground_c);
}

static void reach_clear(const LevelFrame *f, int *r, int *g, int *b) {
    apply(f->stage);
    *r = live.sky[0];
    *g = live.sky[1];
    *b = live.sky[2];
}

static void drop(const LevelFrame *f) {
    int s, base;
    base = f->scroll / 896 * 896;
    for (s = base; s < f->scroll + 5400; s += 896) {
        if (!road_cloth(s, s + 820, -1, 360, -24, 980, -760, 16, 8, 28))
            return;
        if (!road_cloth(s, s + 820, 1, 360, -24, 980, -760, 16, 8, 28))
            return;
    }
    base = f->scroll / 2688 * 2688;
    for (s = base; s < f->scroll + 5600; s += 2688) {
        int side = ((s / 2688) & 1) ? 1 : -1;
        int h = 200 + (s & 180);
        if (!road_marker(s, side * (ROAD_HALF + 210), 14, 0, h, 120, 96, 72))
            return;
    }
}

static void reach_draw(const LevelFrame *f) {
    int hy;
    apply(f->stage);
    hy = path_horizon();
    if (hy < 8)
        hy = 8;
    if (hy > SCREEN_H - 8)
        hy = SCREEN_H - 8;
    gradient(0, 0, SCREEN_W, hy, OT_LENGTH - 1,
             sky_top[0], sky_top[1], sky_top[2],
             live.sky[0], live.sky[1], live.sky[2]);
    road_backdrop(&road);
    road_draw(&road, f);
    drop(f);
    road_streaks(f, road.edge[0], road.edge[1], road.edge[2], 0, 0);
}

static const Stage k_stages[] = {
    {"THE RIDGE", "reach.ridge", 0, STAGE_FINISH},
    {"THE CLIMB", "reach.climb", 0, STAGE_FINISH},
    {"THE DROP", "reach.drop", 0, STAGE_FINISH},
};

const Level level_reach = {
    "VESPER REACH",
    "reach.ridge",
    reach_clear,
    reach_draw,
    &live,
    1,
    k_stages,
    3,
};
