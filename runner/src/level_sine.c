/* The Sineworks: the inside of a machine. There is no ridge and nothing
 * outside the tube: the walls and the ceiling are the world, and the lip
 * stops at the wall. The clear color is the far end of the tunnel. The
 * air thickens each stage. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"

#define EDGE_END 2400
#define SHOULDER_W 16
#define TUBE_OUT 36
#define GROUND_W TUBE_OUT

typedef struct {
    uint8_t sky[3], haze[3], tint[3], shadow[3], top[3];
    int density;
    uint8_t ridge_far[3], ridge_near[3];
    uint8_t asphalt[3], asphalt_far[3], curb[3], dash[3];
    uint8_t shoulder_c[3], ground_c[3];
} Look;

static const ObstacleStyle k_obstacle = {
    {16, 28, 30}, {22, 40, 42}, {30, 56, 58}, {40, 80, 84}, {20, 200, 190}
};

static const Look k_look[3] = {
    {
        {8, 22, 26}, {14, 56, 60}, {110, 150, 148}, {16, 36, 40}, {2, 6, 8},
        136,
        {0, 0, 0}, {0, 0, 0},
        {26, 30, 34}, {8, 16, 18}, {32, 220, 210}, {70, 140, 146},
        {16, 24, 28}, {6, 10, 12}
    },
    {
        {6, 14, 18}, {10, 40, 48}, {100, 160, 150}, {12, 28, 36}, {1, 4, 6},
        180,
        {0, 0, 0}, {0, 0, 0},
        {20, 26, 30}, {6, 12, 16}, {48, 255, 230}, {80, 160, 156},
        {12, 22, 26}, {4, 8, 10}
    },
    {
        {2, 8, 10}, {6, 22, 26}, {90, 140, 136}, {8, 20, 24}, {0, 2, 4},
        220,
        {0, 0, 0}, {0, 0, 0},
        {14, 18, 22}, {4, 8, 10}, {20, 170, 180}, {50, 100, 110},
        {8, 14, 16}, {2, 4, 6}
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

static void sine_clear(const LevelFrame *f, int *r, int *g, int *b) {
    apply(f->stage);
    *r = live.sky[0];
    *g = live.sky[1];
    *b = live.sky[2];
}

/* Walls and ceiling share their edges, so the tube is closed and the
 * clear color shows only at the far mouth. The sheets run as far as the
 * road's far deck. Ribs stay near the camera. */
static void tube(const LevelFrame *f) {
    int step = 640;
    int reach = ROAD_HALF + TUBE_OUT;
    int s = f->scroll / step * step;
    int s1, draw;
    int ribs = f->scroll + 7040;
    for (; (draw = road_span(s, f->scroll, step, reach, &s1)) != 0; s = s1) {
        if (draw == 2)
            continue;
        if (!road_cloth(s, s1, -1, TUBE_OUT, 0, TUBE_OUT, 420, 6, 12, 14))
            return;
        if (!road_cloth(s, s1, 1, TUBE_OUT, 0, TUBE_OUT, 420, 6, 12, 14))
            return;
        if (!road_lintel(s, s1, TUBE_OUT, 408, 5, 10, 12))
            return;
    }
    for (s = f->scroll / step * step; s < ribs; s += step) {
        int rib = ((s / step) & 3) == ((f->time >> 3) & 3);
        int wr, wg, wb;
        if (!rib)
            continue;
        wr = road.edge[0];
        wg = road.edge[1];
        wb = road.edge[2];
        if (!road_cloth(s, s + 40, -1, 22, 0, 22, 440, wr, wg, wb))
            return;
        if (!road_cloth(s, s + 40, 1, 22, 0, 22, 440, wr, wg, wb))
            return;
        if (!road_lintel(s, s + 40, 22, 432, wr, wg, wb))
            return;
    }
}

static void sine_draw(const LevelFrame *f) {
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
    tube(f);
    road_streaks(f, road.edge[0], road.edge[1], road.edge[2], 120, 380);
}

static const Stage k_stages[] = {
    {"THE GATE", "sine.gate", 0, STAGE_FINISH},
    {"THE BEAT", "sine.beat", 0, STAGE_FINISH},
    {"THE CRUSH", "sine.crush", 0, STAGE_FINISH},
};

const Level level_sine = {
    "THE SINEWORKS",
    "sine.gate",
    sine_clear,
    sine_draw,
    &live,
    1,
    k_stages,
    3,
};
