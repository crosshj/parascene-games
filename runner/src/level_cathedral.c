/* Null Cathedral: a lit ribbon in a black void. No ridge, no shoulder, no
 * floor. A few shards hang off the ribbon, and a rare arch crosses above
 * it. The far deck fades into the clear color, so distance reads as a
 * break in the road. The void gets thicker, and the curb harder, each stage. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"

#define EDGE_END 2800

typedef struct {
    uint8_t sky[3], haze[3], tint[3], shadow[3], top[3];
    int density;
    uint8_t ridge_far[3], ridge_near[3];
    uint8_t asphalt[3], asphalt_far[3], curb[3], dash[3];
    uint8_t shoulder_c[3], ground_c[3];
} Look;

static const ObstacleStyle k_obstacle = {
    {20, 8, 36}, {32, 10, 48}, {48, 16, 64}, {80, 30, 120}, {180, 60, 255}
};

static const Look k_look[3] = {
    {
        {4, 0, 10}, {20, 0, 32}, {146, 100, 170}, {24, 0, 36}, {0, 0, 0},
        168,
        {0, 0, 0}, {0, 0, 0},
        {16, 6, 24}, {4, 0, 8}, {170, 48, 255}, {210, 180, 255},
        {0, 0, 0}, {0, 0, 0}
    },
    {
        {2, 0, 6}, {12, 0, 18}, {150, 90, 180}, {16, 0, 28}, {0, 0, 0},
        214,
        {0, 0, 0}, {0, 0, 0},
        {10, 4, 16}, {2, 0, 6}, {190, 36, 255}, {220, 170, 255},
        {0, 0, 0}, {0, 0, 0}
    },
    {
        {1, 0, 4}, {8, 0, 12}, {170, 80, 200}, {12, 0, 24}, {0, 0, 0},
        248,
        {0, 0, 0}, {0, 0, 0},
        {8, 2, 14}, {1, 0, 4}, {230, 80, 255}, {240, 200, 255},
        {0, 0, 0}, {0, 0, 0}
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
    road.shoulder = 0;
    road.ground = 0;
    rgb(road.ridge_far, L->ridge_far);
    rgb(road.ridge_near, L->ridge_near);
    rgb(road.road, L->asphalt);
    rgb(road.road_far, L->asphalt_far);
    rgb(road.edge, L->curb);
    rgb(road.dash, L->dash);
    rgb(road.shoulder_c, L->shoulder_c);
    rgb(road.ground_c, L->ground_c);
}

static void cathedral_clear(const LevelFrame *f, int *r, int *g, int *b) {
    apply(f->stage);
    *r = live.sky[0];
    *g = live.sky[1];
    *b = live.sky[2];
}

static void vault(const LevelFrame *f) {
    int s, base = f->scroll / 2048 * 2048;
    for (s = base; s < f->scroll + 6400; s += 2048) {
        int side = ((s / 2048) & 1) ? -1 : 1;
        int u = side * (ROAD_HALF + 190 + ((s >> 4) & 63));
        int v = 70 + ((s >> 5) & 127);
        if (!road_marker(s, u, 14, v, v + 210, road.edge[0], road.edge[1], road.edge[2]))
            return;
    }
    base = f->scroll / 8192 * 8192;
    for (s = base; s < f->scroll + 9000; s += 8192) {
        if (s < f->scroll + 480)
            continue;
        if (!road_cloth(s, s + 40, -1, 18, 48, 18, 310, 90, 24, 150))
            return;
        if (!road_cloth(s, s + 40, 1, 18, 48, 18, 310, 90, 24, 150))
            return;
        if (!road_lintel(s, s + 40, 18, 296, 150, 60, 210))
            return;
    }
}

static void cathedral_draw(const LevelFrame *f) {
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
    road_draw(&road, f);
    vault(f);
    road_streaks(f, road.edge[0], road.edge[1], road.edge[2], 0, 0);
}

static const Stage k_stages[] = {
    {"THE RIBBON", "cathedral.ribbon", 0, STAGE_FINISH},
    {"THE VAULT", "cathedral.vault", 0, STAGE_FINISH},
    {"THE CHOIR", "cathedral.choir", 0, STAGE_FINISH},
};

const Level level_cathedral = {
    "NULL CATHEDRAL",
    "cathedral.ribbon",
    cathedral_clear,
    cathedral_draw,
    &live,
    1,
    k_stages,
    3,
};
