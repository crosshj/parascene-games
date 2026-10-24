/* Vesper Reach: a stone road on a ridge. The shoulder is a short lip and
 * the ground beyond it is a dark drop, with the gorge wall falling away
 * past that lip. Broken towers stand on the horizon in screen space. Gantries
 * repeat over the road, with a few stones standing on the edge.
 * The air thickens from a clear ridge, to valley haze, to a near-black
 * purple. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"
#include "profile.h"

#define EDGE_END 2000
#define SHOULDER_W 140
#define GROUND_W 480
#define GANTRY_R 150
#define GANTRY_G 124
#define GANTRY_B 78

typedef struct {
    uint8_t haze[3], tint[3], shadow[3], top[3];
    uint8_t horizon[3];
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
        {72, 48, 96}, {140, 118, 156}, {48, 32, 72}, {20, 10, 52},
        {48, 32, 72},
        36,
        {40, 22, 72}, {56, 32, 88},
        {78, 70, 84}, {52, 42, 64}, {186, 154, 72}, {168, 156, 140},
        {58, 48, 70}, {18, 10, 32}
    },
    {
        {48, 30, 64}, {130, 110, 146}, {36, 24, 56}, {12, 6, 36},
        {32, 20, 48},
        100,
        {28, 14, 52}, {40, 22, 68},
        {64, 56, 72}, {40, 32, 52}, {196, 160, 64}, {150, 140, 128},
        {42, 32, 56}, {12, 6, 24}
    },
    {
        {24, 12, 32}, {120, 100, 140}, {24, 16, 40}, {4, 2, 12},
        {16, 8, 24},
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
    rgb(live.sky, L->horizon);
    rgb(live.haze, L->haze);
    rgb(live.tint, L->tint);
    rgb(live.shadow, L->shadow);
    live.density = L->density;
    rgb(sky_top, L->top);
    road.light = &live;
    road.horizon_enabled = 1;
    rgb(road.horizon, L->horizon);
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

static int wrap_shift(int yaw, int denom) {
    int s = yaw * SCREEN_W / denom;
    s %= SCREEN_W;
    if (s < 0)
        s += SCREEN_W;
    return s;
}

static void tower(int x, int w, int top, int foot, int r, int g, int b) {
    XY p0, p1, p2, p3;
    if (w < 2 || top >= foot || x >= SCREEN_W || x + w <= 0)
        return;
    p0.x = (int16_t)x;
    p0.y = (int16_t)top;
    p1.x = (int16_t)(x + w);
    p1.y = (int16_t)top;
    p2.x = (int16_t)x;
    p2.y = (int16_t)foot;
    p3.x = (int16_t)(x + w);
    p3.y = (int16_t)foot;
    const uint8_t *end=road_horizon_color(&road);
    /* Silhouette tops remain legible; their feet dissolve into the horizon. */
    poly_g4(&p0,&p1,&p2,&p3,OT_LENGTH-2,
            r,g,b,r,g,b,end[0],end[1],end[2],end[0],end[1],end[2],BLEND_NONE);
}

/* Ruins on the horizon. Screen space, so the towers add no projections.
 * Linked before the ridge, which puts them in front of it. */
static void towers(int stage) {
    static const int k_x[8] = { 12, 48, 86, 130, 178, 214, 258, 292 };
    const Look *L = &k_look[stage < 0 ? 0 : stage > 2 ? 2 : stage];
    int foot = path_horizon();
    int shift = wrap_shift(path_yaw(), 1280);
    const uint8_t *end=road_horizon_color(&road);
    int c[3], detail[3];
    int i;
    if (world_bytes_used() + 8 * 1024 > BUFFER_LENGTH)
        return;
    if (foot < 20)
        foot = 20;
    if (foot > SCREEN_H - 8)
        foot = SCREEN_H - 8;
    /* Keep the distant silhouettes in the same air as the far ridge. */
    for (i = 0; i < 3; i++) {
        c[i] = end[i] + (L->ridge_far[i] - end[i]) / 4;
        detail[i] = c[i] + (end[i] - c[i]) / 3;
    }
    for (i = 0; i < 8; i++) {
        int h = 22 + ((i * 13) & 28);
        int w = 10 + (i & 1) * 8;
        int x = wrap_shift(k_x[i] - shift, SCREEN_W);
        int k;
        world_asset("reach_ruin", i, "haze_silhouette");
        if ((i & 1) == 0)
            h += 16;
        for (k = -1; k <= 0; k++) {
            int sx = x + k * SCREEN_W;
            tower(sx, w, foot - h, foot + 6, c[0], c[1], c[2]);
            if (h > 30)
                tower(sx + w / 2 - 1, 2, foot - h - 14, foot - h,
                      detail[0], detail[1], detail[2]);
            if (i & 2)
                tower(sx + 2, w > 6 ? 3 : 2, foot - h + 6, foot - h / 2,
                      detail[0], detail[1], detail[2]);
        }
    }
    world_asset("unclassified", 0, "mixed");
}

/* Reach uses the shared gantry geometry, tinted to match its warm haze. */
static void gantries(const LevelFrame *f) {
    static const uint8_t color[3] = {GANTRY_R, GANTRY_G, GANTRY_B};
    int s = (f->scroll / 5120) * 5120 + 2000;
    for (; s < f->scroll + FAR; s += 5120) {
        if (s <= f->scroll + 240)
            continue;
        world_asset("reach_gantry", s / 5120, "road_gantry");
        road_gantry(s, color, color);
    }
    world_asset("unclassified", 0, "mixed");
}

static void canyon_walls(const LevelFrame *f) {
    int step = 768;
    int reach = ROAD_HALF + 980;
    int s = f->scroll / step * step;
    int s1, draw;
    int compiled_start=road_compiled_walls_start(f->scroll);
    for (; (compiled_start<0 || s<compiled_start) && (draw = road_span(s, f->scroll, step, reach, 760, &s1)) != 0; s = s1) {
        if(compiled_start>=0 && s1>compiled_start) s1=compiled_start;
        if (draw == 2)
            continue;
        world_asset("reach_canyon", -1, "far_sheet");
        if (!road_cloth(s, s1, -1, 360, -24, 980, -760, 16, 8, 28))
            return;
        world_asset("reach_canyon", 1, "far_sheet");
        if (!road_cloth(s, s1, 1, 360, -24, 980, -760, 16, 8, 28))
            return;
    }
    world_asset("reach_canyon", 0, "compiled_mesh");
    road_compiled_walls(f->scroll,compiled_start,16,8,28);
    world_asset("unclassified", 0, "mixed");
}

static void edge_markers(const LevelFrame *f) {
    int n = 0, s;
    s = (f->scroll / 3200 + (f->scroll % 3200 != 0)) * 3200;
    for (; n < 2 && s < f->scroll + 3600; s += 3200, n++) {
        int side = ((s / 3200) & 1) ? 1 : -1;
        int h = 180 + (s & 140);
        world_asset("reach_marker", s / 3200, "edge_stone");
        if (!road_marker(s, side * (ROAD_HALF + 210), 14, 0, h, 120, 96, 72))
            return;
    }
    world_asset("unclassified", 0, "mixed");
}

static void drop(const LevelFrame *f) {
    PROF_LONG(PF_REACH_CANYON, canyon_walls(f));
    PROF(PF_REACH_MARKERS, edge_markers(f));
}

static void reach_draw(const LevelFrame *f) {
    int hy;
    apply(f->stage);
    hy = path_horizon();
    if (hy < 8)
        hy = 8;
    if (hy > SCREEN_H - 8)
        hy = SCREEN_H - 8;
    world_asset("reach_sky",0,"horizon_gradient");
    gradient(0, 0, SCREEN_W, hy, OT_LENGTH - 1,
             sky_top[0], sky_top[1], sky_top[2],
             live.sky[0], live.sky[1], live.sky[2]);
    towers(f->stage);
    world_asset("reach_backdrop",0,"horizon_gradient");
    road_backdrop(&road);
    world_asset("unclassified",0,"mixed");
    road_draw(&road, f);
    drop(f);
    PROF(PF_REACH_GANTRIES, gantries(f));
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
