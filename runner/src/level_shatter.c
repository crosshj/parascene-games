/* Shatterglass: a glass desert at sunset. The ridge sits close to the sky
 * color, and the ground is a wide pale mirror, so the far road dissolves
 * into the horizon. A screen-space line of shards stands on that horizon.
 * A few near crystals and one low rise stand on the mirror. The sun
 * drops across the three stages. */

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

/* Ground-reflected fill: strongest in the two bright stages, none at night. */
static const uint8_t k_runner_ambient[3][3] = {
    {40,32,28},
    {32,24,28},
    {0,0,0}
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
    rgb(live.runner_ambient,k_runner_ambient[stage]);
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

static int wrap_shift(int yaw, int denom) {
    int s = yaw * SCREEN_W / denom;
    s %= SCREEN_W;
    if (s < 0)
        s += SCREEN_W;
    return s;
}

static void shard(int x, int w, int top, int foot, int r, int g, int b) {
    XY p0, p1, p2, p3;
    int tip = w > 4 ? w / 5 : 1;
    if (w < 2 || top >= foot)
        return;
    if (x >= SCREEN_W || x + w <= 0)
        return;
    p0.x = (int16_t)(x + w / 2 - tip);
    p0.y = (int16_t)top;
    p1.x = (int16_t)(x + w / 2 + tip);
    p1.y = (int16_t)top;
    p2.x = (int16_t)x;
    p2.y = (int16_t)foot;
    p3.x = (int16_t)(x + w);
    p3.y = (int16_t)foot;
    poly_f4(&p0, &p1, &p2, &p3, OT_LENGTH - 2, r, g, b, BLEND_NONE);
}

/* The field of glass. Screen space, slid by the heading, so the expanse
 * does not add a projection. Drawn before the ridge so the shards sit
 * on it. */
static void horizon(int stage) {
    static const int k_x[12] = { 8, 36, 58, 92, 120, 148, 176, 204, 228, 256, 284, 308 };
    int foot = path_horizon();
    int shift = wrap_shift(path_yaw(), 1536);
    int r = stage == 2 ? 220 : 255;
    int g = stage == 0 ? 236 : stage == 1 ? 190 : 150;
    int b = stage == 2 ? 230 : 200;
    int i;
    if (world_bytes_used() + 8 * 1024 > BUFFER_LENGTH)
        return;
    if (foot < 16)
        foot = 16;
    if (foot > SCREEN_H - 8)
        foot = SCREEN_H - 8;
    for (i = 0; i < 12; i++) {
        int h = 18 + ((i * 17) & 31);
        int w = 8 + (i & 3) * 3;
        int k;
        if (stage > 0)
            h += 8 * stage;
        for (k = -1; k <= 0; k++)
            shard(k_x[i] - shift + k * SCREEN_W, w, foot - h, foot + 4, r, g, b);
    }
}

/* Two crystals beside the runner, and one low rise. The horizon row is
 * the rest of the desert. */
static void desert(const LevelFrame *f) {
    int s, n;
    int base = f->scroll / 2048 * 2048;
    n = 0;
    for (s = base; n < 2 && s < f->scroll + 2600; s += 2048, n++) {
        int side = ((s / 2048) & 1) ? -1 : 1;
        int h = 260 + ((s >> 4) & 180);
        int u = side * (ROAD_HALF + 520);
        int cr = f->stage == 2 ? 230 : 255;
        int cg = f->stage == 0 ? 220 : 170;
        int cb = f->stage == 2 ? 210 : 160;
        if (!road_marker(s, u, 18, 0, h, cr, cg, cb))
            return;
        if (!road_marker(s, u, 8, h - 20, h + 70, 255, 250, 236))
            return;
    }
    s = f->scroll / 4096 * 4096 + 1600;
    if (s > f->scroll + 280 && s < f->scroll + 4200) {
        int side = ((s / 4096) & 1) ? 1 : -1;
        road_cloth(s, s + 1100, side, 700, 0, 1500, 48,
                   road.ground_c[0], road.ground_c[1], road.ground_c[2]);
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
    horizon(f->stage);
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
