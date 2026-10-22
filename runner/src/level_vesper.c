/* Vesper City: a night highway. The first stage is a skyline on the
 * horizon and a neon rail beside the curb. The last stage is a solid
 * wall of towers on each side, tall enough to leave the top of the
 * screen. The floor stops at that wall, and the skyline is only the
 * slot of street between the walls. The skyline is screen space. The
 * light steps from dusk, to night, to a wetter dark. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"

#define EDGE_END 2400
#define SHOULDER_W 200
#define GROUND_W 1800
#define CANYON_OUT 64

typedef struct {
    uint8_t sky[3], haze[3], tint[3], shadow[3], top[3];
    int density;
    uint8_t ridge_far[3], ridge_near[3];
    uint8_t asphalt[3], asphalt_far[3], curb[3], dash[3];
    uint8_t shoulder_c[3], ground_c[3];
} Look;

static const ObstacleStyle k_obstacle = {
    {40, 48, 58}, {56, 64, 76}, {72, 84, 98}, {180, 200, 210}, {40, 200, 220}
};

static const Look k_look[3] = {
    {
        {22, 16, 42}, {84, 36, 76}, {150, 124, 156}, {48, 32, 72}, {8, 6, 22},
        72,
        {28, 18, 52}, {40, 24, 64},
        {32, 32, 40}, {18, 16, 30}, {40, 210, 255}, {255, 196, 72},
        {18, 16, 28}, {6, 4, 14}
    },
    {
        {6, 8, 20}, {36, 14, 52}, {140, 128, 168}, {32, 24, 64}, {2, 2, 10},
        124,
        {14, 10, 32}, {22, 14, 44},
        {24, 24, 32}, {12, 10, 22}, {64, 230, 255}, {255, 210, 96},
        {12, 14, 24}, {4, 2, 10}
    },
    {
        {4, 8, 16}, {18, 28, 36}, {120, 132, 150}, {24, 32, 48}, {2, 6, 12},
        176,
        {10, 16, 28}, {14, 24, 36},
        {20, 26, 32}, {10, 16, 24}, {72, 190, 210}, {220, 200, 120},
        {16, 32, 40}, {4, 8, 14}
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
    road.shoulder = stage >= 2 ? 40 : SHOULDER_W;
    road.ground = stage >= 2 ? CANYON_OUT : GROUND_W;
    rgb(road.ridge_far, L->ridge_far);
    rgb(road.ridge_near, L->ridge_near);
    rgb(road.road, L->asphalt);
    rgb(road.road_far, L->asphalt_far);
    rgb(road.edge, L->curb);
    rgb(road.dash, L->dash);
    rgb(road.shoulder_c, L->shoulder_c);
    rgb(road.ground_c, L->ground_c);
}

static void vesper_clear(const LevelFrame *f, int *r, int *g, int *b) {
    apply(f->stage);
    *r = live.sky[0];
    *g = live.sky[1];
    *b = live.sky[2];
}

/* One screen of skyline, rebuilt when the stage changes. Drawn each frame
 * in screen space and slid by the heading, so a bend pans the city. */
#define ROW 26

typedef struct {
    int x, w, h, win;
} Block;

static Block far_row[ROW];
static Block near_row[ROW];
static int far_n, near_n;
static int rows_stage = -1;

static uint32_t mixb(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    return h;
}

static int fill_row(Block *row, uint32_t seed, int lift, int span) {
    int x = 0, n = 0;
    while (x < SCREEN_W && n < ROW) {
        uint32_t h = mixb(seed += 0x9E3779B9u);
        int w = 14 + (int)(h & 15);
        int ht = lift + (int)((h >> 4) % (uint32_t)span);
        if (((h >> 12) & 7) == 0)
            ht += span / 2 + 8;
        row[n].x = x;
        row[n].w = w;
        row[n].h = ht;
        row[n].win = (int)((h >> 16) & 3);
        n++;
        x += w + 2 + (int)((h >> 20) & 3);
    }
    return n;
}

static void build_rows(int stage) {
    int lift = stage == 1 ? 20 : stage == 0 ? 12 : 8;
    int span = stage == 1 ? 34 : stage == 0 ? 24 : 14;
    far_n = fill_row(far_row, 0xA11C500u + (uint32_t)stage * 3, lift / 2 + 6, span / 2 + 8);
    near_n = stage == 2 ? 0 : fill_row(near_row, 0xBEEF000u + (uint32_t)stage * 9, lift, span);
    rows_stage = stage;
}

static int wrap_shift(int yaw, int denom) {
    int s = yaw * SCREEN_W / denom;
    s %= SCREEN_W;
    if (s < 0)
        s += SCREEN_W;
    return s;
}

static void block(int x, int w, int top, int foot, int r, int g, int b) {
    XY p0, p1, p2, p3;
    if (w < 2 || x >= SCREEN_W || x + w <= 0)
        return;
    if (top < 1)
        top = 1;
    p0.x = (int16_t)x;
    p0.y = (int16_t)top;
    p1.x = (int16_t)(x + w);
    p1.y = (int16_t)top;
    p2.x = (int16_t)x;
    p2.y = (int16_t)foot;
    p3.x = (int16_t)(x + w);
    p3.y = (int16_t)foot;
    poly_f4(&p0, &p1, &p2, &p3, OT_LENGTH - 2, r, g, b, BLEND_NONE);
}

static void blit_row(const Block *row, int n, int shift, int foot,
                     int r, int g, int b, int windows, int x0, int x1) {
    int i;
    for (i = 0; i < n; i++) {
        int x = row[i].x - shift;
        int top = foot - row[i].h;
        int k;
        for (k = -1; k <= 1; k++) {
            int sx = x + k * SCREEN_W;
            if (sx >= x1 || sx + row[i].w <= x0)
                continue;
            if (sx >= SCREEN_W || sx + row[i].w <= 0)
                continue;
            if (windows && row[i].win && row[i].h > 16) {
                int wx = sx + row[i].w / 4;
                int ww = row[i].w / 5;
                int amber = row[i].win & 1;
                if (ww < 2)
                    ww = 2;
                block(wx, ww, top + 4, top + row[i].h / 2,
                      amber ? 255 : 48, amber ? 176 : 210, amber ? 64 : 230);
            }
            block(sx, row[i].w, top, foot + 6, r, g, b);
            if (windows && row[i].h > 18) {
                int sign = row[i].win & 1;
                block(sx + 2, row[i].w > 6 ? row[i].w - 4 : row[i].w,
                      top + row[i].h / 3, top + row[i].h / 3 + 3,
                      sign ? 255 : 64, sign ? 64 : 220, sign ? 140 : 255);
            }
            if (row[i].h > 28 && (row[i].win & 2))
                block(sx + row[i].w / 2, 2, top - 12, top, r + 20, g + 16, b + 24);
        }
    }
}

static void skyline(int stage) {
    int foot = path_horizon();
    int yaw = path_yaw();
    if (foot < 8)
        foot = 8;
    if (foot > SCREEN_H - 8)
        foot = SCREEN_H - 8;
    if (rows_stage != stage)
        build_rows(stage);
    /* The last stage's walls cover the sides, so the skyline is only the
     * slot down the street. */
    if (stage >= 2) {
        int x0 = SCREEN_W / 2 - 36;
        int x1 = SCREEN_W / 2 + 36;
        blit_row(far_row, far_n, wrap_shift(yaw, 2048), foot + 2, 8, 4, 18, 0, x0, x1);
        return;
    }
    /* Near row first, so it shows in front of the far row. */
    blit_row(near_row, near_n, wrap_shift(yaw, 1024), foot, 16, 8, 32, 1, 0, SCREEN_W);
    blit_row(far_row, far_n, wrap_shift(yaw, 2048), foot + 2, 8, 4, 18, 0, 0, SCREEN_W);
}

static void lights(const LevelFrame *f) {
    int s, base;
    if (f->stage < 2) {
        base = f->scroll / 1792 * 1792;
        for (s = base; s < f->scroll + 4800; s += 1792) {
            int side = ((s / 1792) & 1) ? 1 : -1;
            int u = side * (ROAD_HALF + 86);
            if (!road_marker(s, u, 8, 0, 250, road.edge[0], road.edge[1], road.edge[2]))
                return;
            if (!road_marker(s, u, 14, 232, 268, road.dash[0], road.dash[1], road.dash[2]))
                return;
        }
    }
    base = f->scroll / 7168 * 7168;
    for (s = base; s < f->scroll + 8000; s += 7168) {
        if (s > f->scroll + 240)
            road_gantry(s, road.edge, road.dash);
    }
}

/* A low rail just outside the curb, only beside the runner. The skyline
 * is the rest of the city, and it is not projected. */
static void barriers(const LevelFrame *f) {
    int step = 420;
    int s = f->scroll / step * step;
    if (f->stage >= 2)
        return;
    for (; s < f->scroll + 1300; s += step) {
        int side;
        for (side = -1; side <= 1; side += 2) {
            if (!road_cloth(s, s + step - 24, side, 18, 0, 18, 28,
                            road.edge[0], road.edge[1], road.edge[2]))
                return;
        }
    }
}

/* Last stage: one solid wall on each side, so the sheets meet and the
 * street is a slot. Height still changes along the wall. Windows and the
 * crown sit on the near wall only; the far wall is the mass. */
static void canyon(const LevelFrame *f) {
    int step = 640;
    int s = f->scroll / step * step;
    int limit = f->scroll + 4200;
    if (f->stage < 2)
        return;
    /* Fixed sheets, not the grown span. A tall wall on a long sheet is
     * cut into dozens of pieces when a corner passes the camera. */
    for (; s < limit; s += step) {
        int side;
        for (side = -1; side <= 1; side += 2) {
            uint32_t n = mixb((uint32_t)(s + (side > 0 ? 409 : 0)) * 0x9E3779B9u);
            int near = s < f->scroll + 1280;
            int tall = near ? 480 : 640 + (int)(n % 200u);
            int amber = n & 1u;
            if (!road_cloth(s, s + step, side, CANYON_OUT, 0, CANYON_OUT, tall, 12, 14, 28))
                return;
            if (!near)
                continue;
            if (!road_cloth(s, s + step, side, CANYON_OUT - 4, 150, CANYON_OUT - 4, 186,
                            amber ? 255 : 64, amber ? 186 : 220, amber ? 72 : 255))
                return;
        }
    }
}

static void vesper_draw(const LevelFrame *f) {
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
    skyline(f->stage);
    road_draw(&road, f);
    lights(f);
    barriers(f);
    canyon(f);
    road_streaks(f, road.edge[0], road.edge[1], road.edge[2],
                 -1, 0);
}

static const Stage k_stages[] = {
    {"THE SWEEP", "vesper.sweep", 0, STAGE_FINISH},
    {"THE ESSE", "vesper.esse", 0, STAGE_FINISH},
    {"THE HOOK", "vesper.hook", 0, STAGE_FINISH},
};

const Level level_vesper = {
    "VESPER CITY",
    "vesper.sweep",
    vesper_clear,
    vesper_draw,
    &live,
    1,
    k_stages,
    3,
};
