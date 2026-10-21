/* Shatterglass: a glass desert at sunset. The ridge sits close to the sky
 * color, and the ground is a wide pale mirror, so the far road dissolves
 * into the horizon. Crystals and low rises stand on the mirror, outside
 * the deck. The sun drops across the three stages. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"

#define EDGE_END 2200
#define SHOULDER_W 700
#define GROUND_W 2600

typedef struct {
    uint8_t sky[3], haze[3], tint[3], shadow[3], top[3];
    int density;
    uint8_t ridge_far[3], ridge_near[3];
    uint8_t asphalt[3], asphalt_far[3], curb[3], dash[3];
    uint8_t shoulder_c[3], ground_c[3];
} Look;

static const ObstacleStyle k_obstacle = {
    {120, 72, 48}, {150, 96, 64}, {176, 120, 80}, {230, 190, 140}, {255, 160, 90}
};

static const Look k_look[3] = {
    {
        {255, 156, 104}, {255, 176, 140}, {170, 130, 110}, {140, 80, 70}, {110, 56, 140},
        28,
        {240, 130, 120}, {250, 160, 130},
        {196, 158, 146}, {230, 160, 146}, {255, 228, 180}, {255, 244, 224},
        {214, 156, 136}, {200, 124, 112}
    },
    {
        {214, 92, 124}, {236, 120, 140}, {160, 110, 120}, {120, 60, 80}, {72, 24, 100},
        56,
        {200, 90, 120}, {220, 110, 130},
        {170, 120, 128}, {200, 110, 130}, {255, 200, 170}, {255, 230, 220},
        {190, 110, 120}, {170, 80, 100}
    },
    {
        {84, 32, 104}, {140, 64, 116}, {140, 100, 130}, {60, 30, 70}, {24, 8, 44},
        104,
        {100, 40, 90}, {120, 52, 100},
        {90, 60, 84}, {110, 50, 90}, {255, 180, 150}, {240, 210, 220},
        {100, 50, 80}, {70, 28, 64}
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

static void shatter_clear(const LevelFrame *f, int *r, int *g, int *b) {
    apply(f->stage);
    *r = live.sky[0];
    *g = live.sky[1];
    *b = live.sky[2];
}

static void desert(const LevelFrame *f) {
    int s, base;
    base = f->scroll / 1408 * 1408;
    for (s = base; s < f->scroll + 5200; s += 1408) {
        int side = ((s / 1408) & 1) ? -1 : 1;
        int h = 220 + ((s >> 4) & 255);
        int u = side * (ROAD_HALF + 820 + ((s >> 3) & 127));
        if (!road_marker(s, u, 16, 0, h, 255, 214, 170))
            return;
        if (!road_marker(s, u, 8, h - 36, h + 64, 255, 244, 220))
            return;
    }
    base = f->scroll / 2048 * 2048;
    for (s = base; s < f->scroll + 6200; s += 2048) {
        int side = ((s / 2048) & 1) ? 1 : -1;
        if (!road_cloth(s, s + 900, side, 780, 0, 1480, 64,
                        road.ground_c[0], road.ground_c[1], road.ground_c[2]))
            return;
    }
}

static void shatter_draw(const LevelFrame *f) {
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
    desert(f);
    road_streaks(f, road.edge[0], road.edge[1], road.edge[2], 0, 0);
}

static const Stage k_stages[] = {
    {"THE SPLIT", "shatter.split", 0, STAGE_FINISH},
    {"THE SWITCH", "shatter.switch", 0, STAGE_FINISH},
    {"THE JOIN", "shatter.join", 0, STAGE_FINISH},
};

const Level level_shatter = {
    "SHATTERGLASS",
    "shatter.split",
    shatter_clear,
    shatter_draw,
    &live,
    1,
    k_stages,
    3,
};
