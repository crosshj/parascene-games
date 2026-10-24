/* Vesper City: a night highway. The first stage is a skyline on the
 * horizon and a neon rail beside the curb. The last stage is a solid
 * wall of towers on each side, tall enough to leave the top of the
 * screen. The floor stops at that wall; the last stage has sky between
 * its towers, without a separate skyline. Earlier skylines are screen space. The
 * light steps from dusk, to night, to a wetter dark. */

#include <psxgpu.h>
#include "world.h"
#include "vesper_facades.h"
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

/* Dedicated free VRAM region: clear of font, sprites, art and Lumen sky. */
static int facade_ready;
static uint16_t facade_page, facade_clut;
static void facade_upload(void) {
    RECT rect;
    if (facade_ready) return;
    setRECT(&rect,960,128,64,64);
    LoadImage(&rect,(const uint32_t *)vesper_pixels);
    DrawSync(0);
    setRECT(&rect,0,483,16,1);
    LoadImage(&rect,(const uint32_t *)vesper_palette);
    DrawSync(0);
    facade_page=getTPage(0,0,960,0);
    facade_clut=getClut(0,483);
    facade_ready=1;
}
static int facade_u(int variant,int lod) {
    static const int base[4]={0,128,192,224};
    return base[lod]+variant*(64>>lod);
}

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

static int skyline_shift(int yaw) {
    /* Rotation moves both distant layers together. Different yaw scales made
     * overlapping silhouettes slide through each other during a turn.
     * Two panorama repeats per revolution also keep the 4095->0 seam exact. */
    int s = yaw * SCREEN_W / 2048;
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
            if (windows) {
                int lod=row[i].w<24 ? 2 : 1, size=64>>lod;
                int u=facade_u(row[i].win&1,lod);
                XY p[4]={{sx,top},{sx+row[i].w,top},{sx,foot+6},{sx+row[i].w,foot+6}};
                /* Crop the square tile at a uniform texel scale: stretching
                 * a complete tile onto a tall building elongated windows. */
                int h=foot+6-top, extent=h>row[i].w ? h : row[i].w;
                int uw=(size-1)*row[i].w/extent, vh=(size-1)*h/extent;
                int offset=(int)(mixb((uint32_t)i+91u) % (uint32_t)(size-uw));
                u+=offset;
                XY uv[4]={{u,128},{u+uw,128},{u,128+vh},{u+uw,128+vh}};
                world_asset("vesper_skyline",i+(row==far_row?100:0),"filtered_windows");
                poly_tex4(p,uv,OT_LENGTH-2,r<=8 ? 72 : 112,facade_page,facade_clut);
            } else block(sx,row[i].w,top,foot+6,r,g,b);
            if (row[i].h > 28 && (row[i].win & 2))
                block(sx + row[i].w / 2, 2, top - 12, top, r + 20, g + 16, b + 24);
        }
    }
}

static void skyline(int stage) {
    int foot = path_horizon();
    int shift = skyline_shift(path_yaw());
    if (foot < 8)
        foot = 8;
    if (foot > SCREEN_H - 8)
        foot = SCREEN_H - 8;
    if (rows_stage != stage)
        build_rows(stage);
    /* The final stage is enclosed by roadside towers; only sky behind them. */
    if (stage >= 2)
        return;
    /* Near row first, so it shows in front of the far row. */
    blit_row(near_row, near_n, shift, foot, 16, 8, 32, 1, 0, SCREEN_W);
    blit_row(far_row, far_n, shift, foot + 2, 8, 4, 18, 1, 0, SCREEN_W);
}

#ifdef RUNNER_PREVIEW
/* Exercise the actual skyline in isolation for temporal stability checks. */
void vesper_preview_skyline(void) {
    apply(0);
    facade_upload();
    skyline(0);
}
#endif

static void lights(const LevelFrame *f) {
    static const uint8_t gantry_post[3] = {132, 140, 148};
    static const uint8_t gantry_beam[3] = {112, 120, 128};
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
        /* Leave a full view-distance plus one interval clear so the real
         * finish gantry is the only gate ahead as the stage ends. */
        if (s > f->scroll + 240 && s < STAGE_FINISH - FAR - 7168)
            road_gantry(s, gantry_post, gantry_beam);
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

/* Open-top street canyon: shaded enclosure and inexpensive window lights. */
static void canyon(const LevelFrame *f) {
    if (f->stage>=2)
        road_city_canyon(f->scroll,CANYON_OUT,960,vesper_city_runs,VESPER_CITY_RUN_COUNT);
}

static void vesper_draw(const LevelFrame *f) {
    static const uint8_t glow[3][3]={{40,24,46},{24,18,36},{18,26,36}};
    int hy;
    apply(f->stage);
    facade_upload();
    hy = path_horizon();
    if (hy < 8)
        hy = 8;
    if (hy > SCREEN_H - 8)
        hy = SCREEN_H - 8;
    gradient(0, 0, SCREEN_W, hy, OT_LENGTH - 1,
             sky_top[0], sky_top[1], sky_top[2],
             glow[shown][0], glow[shown][1], glow[shown][2]);
    skyline(f->stage);
    world_asset("road",0,"deck");
    road_draw(&road, f);
    lights(f);
    barriers(f);
    canyon(f);
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
