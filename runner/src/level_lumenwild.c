/* Lumenwild: a bioluminescent alien forest seen at speed.
 *
 * The forest is painted as an impression, not modelled. Most growths are
 * one or two projected quads, and their edge light, glowing cores, and
 * halos are copies of that same projection scaled in screen space, so the
 * layered light costs no extra GTE work. The GPU has fill to spare; the
 * CPU pays per vertex. Far away growths are silhouettes, nearer they gain
 * light, and beside the camera speed smears them into streaks.
 *
 * Full detail goes only to a hero now and then: a giant canopy with gills,
 * a glowing lip, a halo, and a light shaft, the thing the eye catches as it
 * sweeps overhead.
 *
 * Every growth comes from a hash of its cell along z, so the forest is the
 * same on every run and nothing is stored between frames. */

#include <stdint.h>
#include <string.h>
#include <psxgpu.h>
#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "level.h"
#include "profile.h"
#include "lumen_sky.h" /* LUMEN_SKY_W / LUMEN_SKY_H are texture size */

#define LW_FAR 8400
#define CELL 400
#define MID_Z 3900
#define NEAR_Z 1100
/* Growths closer than this balloon and stick on screen; cut them before that. */
#define CULL_Z 420
#define SMALL_FAR_Z 6000
/* Keep this much clear beside the asphalt so canopies never cover the road. */
#define ROAD_CLEAR (ROAD_HALF + 60)
#define SPORES 20
#define SPORE_RANGE 2600
#define STREAKS 12
#define MAX_GLOWS 20

/* One cell in HERO_EVERY carries a full-detail giant. */
#define HERO_EVERY 7

/* The forest stops adding growths here so the path, obstacles, and HUD
 * always fit in the frame's primitive buffer. */
#define FOREST_BUDGET (BUFFER_LENGTH - 12 * 1024)

#define SKY_BUCKET (OT_LENGTH - 1)
#define SKYLINE_BUCKET (OT_LENGTH - 2)
#define FAR_BUCKET (OT_LENGTH - 8)
#define STREAK_BUCKET ((CHAR_Z >> OT_SHIFT) + 2)

typedef struct { int x, y, z; } V3;
typedef struct { uint8_t r, g, b; } RGB;
typedef struct { int r, g, b; } Col;

/* A growth's dark body, the edge light that outlines it, and its glow. */
typedef struct { RGB body, rim, core; } Scheme;

enum { G_CRYSTAL, G_MOUND, G_FUNGUS, G_GIANT, G_SPIRE };
enum { LOD_NEAR, LOD_MID, LOD_FAR };

typedef struct {
    int kind;
    int hero;
    int x, z;    /* foot of the growth, camera space, once planted */
    int u;       /* lateral offset from the centerline */
    int foot;    /* camera y of the planted foot */
    int h, w, r; /* height, stalk half-width, canopy radius */
    int sway;    /* how far the stalk twists off its foot */
    int phase;
    int wz;      /* distance along the road, so pulses travel the forest */
    const Scheme *sc;
    uint32_t seed;
} Growth;

static const int8_t k_sin[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12
};

static const RGB k_floor_glow = {58, 22, 92};
static const RGB k_floor = {10, 6, 28};
static const RGB k_fog = {34, 20, 80};
static const RGB k_fade = {84, 30, 124};
static const Scheme k_schemes[5] = {
    {{54, 22, 104}, {60, 220, 255}, {190, 255, 255}},
    {{24, 28, 92}, {250, 60, 200}, {255, 170, 240}},
    {{10, 64, 80}, {170, 90, 255}, {130, 255, 220}},
    {{70, 14, 92}, {40, 210, 180}, {255, 140, 50}},
    {{30, 16, 70}, {100, 120, 255}, {80, 255, 255}},
};

/* Scheme 3 has the hot orange core, so it stays rare. */
static const uint8_t k_scheme_pick[16] = {
    0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 4, 4, 4, 3
};

static const RGB k_spore[8] = {
    {90, 240, 255}, {90, 240, 255}, {60, 200, 255},
    {255, 90, 220}, {255, 90, 220}, {200, 110, 255},
    {170, 255, 210}, {255, 150, 50}
};

static const ObstacleStyle k_obstacle = {
    {120, 26, 40}, {176, 44, 52}, {220, 70, 40}, {255, 210, 130}, {255, 118, 34}
};

static int lw_scroll, lw_speed, lw_time, lw_glows, lw_foot;
/* Set while drawing things the player must judge, which never deform. */
static int lw_rigid;

static int sn(int a) { return k_sin[a & 63]; }
static int cs(int a) { return k_sin[(a + 16) & 63]; }

static uint32_t mix(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

/* A step of a linear congruential stream, scaled into lo..hi with a
 * multiply; the console's divide is far slower. */
static int rnd(uint32_t *s, int lo, int hi) {
    *s = *s * 1664525u + 1013904223u;
    return lo + (int)(((uint64_t)*s * (uint32_t)(hi - lo + 1)) >> 32);
}

static int glow_ok(void) {
    return lw_glows++ < MAX_GLOWS;
}

/* Colors */

static Col col(RGB c, int k) {
    Col o;
    o.r = (c.r * k) >> 8;
    o.g = (c.g * k) >> 8;
    o.b = (c.b * k) >> 8;
    return o;
}

static void tint(Col *c, RGB to, int t) {
    c->r += ((to.r - c->r) * t) >> 8;
    c->g += ((to.g - c->g) * t) >> 8;
    c->b += ((to.b - c->b) * t) >> 8;
}

static int fog_t(int z) {
    if (z <= 400)
        return 0;
    return clamp((z - 400) * 256 / (LW_FAR - 400), 0, 256);
}

/* Near things get more saturated, far things sink into the horizon. */
static void depth_cue(Col *c, int z) {
    if (z < 900) {
        int avg = (c->r + c->g + c->b) / 3;
        int k = 900 - (z < 0 ? 0 : z);
        c->r += (c->r - avg) * k / 2400;
        c->g += (c->g - avg) * k / 2400;
        c->b += (c->b - avg) * k / 2400;
    }
    if (z > LW_FAR - 2500)
        tint(c, k_fade, clamp((z - (LW_FAR - 2500)) * 256 / 2500, 0, 256));
}

/* A surface that only reflects: distance pulls it into the cool fog. */
static Col surface(RGB c, int k, int z) {
    Col o = col(c, k);
    tint(&o, k_fog, fog_t(z));
    depth_cue(&o, z);
    return o;
}

/* A light source keeps its color farther into the fog. */
static Col light(RGB c, int k, int z) {
    Col o = col(c, k);
    tint(&o, k_fog, fog_t(z) / 2);
    depth_cue(&o, z);
    return o;
}

/* An additive glow fades toward black, so distance never brightens it. */
static Col glow(RGB c, int k, int z) {
    return col(c, (k * (256 - fog_t(z))) >> 8);
}

/* Illumination that ripples along the forest, 96 to 256. */
static int pulse(int wz, int phase) {
    return 176 + sn(lw_time * 2 - (wz >> 6) + phase) * 80 / 127;
}

/* Geometry */

static int depth_bucket(int z) {
    int b;
    if (z < FAR)
        b = z >> OT_SHIFT;
    else
        b = (FAR >> OT_SHIFT) + (z - FAR) / 24;
    return clamp(b, 4, FAR_BUCKET);
}

/* Speed leans tall things back and flings near things outward. */
static void warp(V3 *v) {
    if (lw_rigid)
        return;
    if (v->y > 0)
        v->z += (v->y * lw_speed) >> 7;
    if (v->z < NEAR_Z) {
        int k = (NEAR_Z - v->z) * lw_speed / NEAR_Z;
        v->x += (v->x * k) >> 5;
    }
}

/* Mild speed streak for near side growths that are well clear of the road.
 * Kept small on purpose: large smears were covering the asphalt and then
 * hanging on screen as the growth left. */
static int smear(int z, int x) {
    int k, sm;
    if (iabs(x) < ROAD_CLEAR + 400)
        return 0;
    k = clamp(NEAR_Z - z, 0, NEAR_Z);
    sm = (20 + lw_speed * 8) * k / NEAR_Z;
    return sm > 120 ? 120 : sm;
}

static void screen_quad(const XY *p, int bucket, Col c, int blend) {
    poly_f4v(p, bucket, c.r, c.g, c.b, blend);
}

/* Warps and projects one point. Returns its depth after the warp, or 0 when
 * it lands behind the camera. */
static int pv(int x, int y, int z, XY *out) {
    V3 v = {x, y, z};
    warp(&v);
    v.y += lw_foot;
    if (v.z < 8)
        return 0;
    project(v.x, v.y, v.z, out);
    return v.z;
}

static int project4(const V3 *q, XY *p) {
    int i;
    for (i = 0; i < 4; i++) {
        if (!pv(q[i].x, q[i].y, q[i].z, &p[i]))
            return 0;
    }
    return 1;
}

/* Four projected points in setXY4 order. */
static void emit(const XY *a, const XY *b, const XY *c, const XY *d, int bucket, Col col, int blend) {
    XY p[4];
    p[0] = *a; p[1] = *b; p[2] = *c; p[3] = *d;
    screen_quad(p, bucket, col, blend);
}

/* q is in setXY4 order: q[0], q[1] one edge, q[2], q[3] the opposite one. */
static void lw_quad(const V3 *q, int bucket, Col c, int blend) {
    XY p[4];
    if (project4(q, p))
        screen_quad(p, bucket, c, blend);
}

static XY lerp_xy(XY a, XY b, int t) {
    XY o;
    o.x = (int16_t)(a.x + (((b.x - a.x) * t) >> 8));
    o.y = (int16_t)(a.y + (((b.y - a.y) * t) >> 8));
    return o;
}

static XY centroid(const XY *p) {
    XY o;
    o.x = (int16_t)((p[0].x + p[1].x + p[2].x + p[3].x) >> 2);
    o.y = (int16_t)((p[0].y + p[1].y + p[2].y + p[3].y) >> 2);
    return o;
}

/* A projected quad scaled by k / 256 about a pivot: a core inside it, or a
 * halo around it, for the cost of a few adds. */
static void layer(const XY *p, XY pivot, int k, int bucket, Col c, int blend) {
    XY s[4];
    int i;
    for (i = 0; i < 4; i++) {
        s[i].x = (int16_t)(pivot.x + (((p[i].x - pivot.x) * k) >> 8));
        s[i].y = (int16_t)(pivot.y + (((p[i].y - pivot.y) * k) >> 8));
    }
    screen_quad(s, bucket, c, blend);
}

/* A vertical diamond from foot to tip, widest at waist_y. smear pushes the
 * upper points away from the camera. */
static void kite_points(V3 *q, int xt, int yt, int xf, int yf, int yw, int hw, int z, int sm) {
    int span = yt - yf ? yt - yf : 1;
    int xw = xf + (xt - xf) * (yw - yf) / span;
    q[0] = (V3){xt, yt, z + sm};
    q[1] = (V3){xw + hw, yw, z + sm / 2};
    q[2] = (V3){xw - hw, yw, z + sm / 2};
    q[3] = (V3){xf, yf, z};
}

static void kite(int xt, int yt, int xf, int yf, int yw, int hw, int z, int sm,
                 int bucket, Col c, int blend) {
    V3 q[4];
    kite_points(q, xt, yt, xf, yf, yw, hw, z, sm);
    lw_quad(q, bucket, c, blend);
}

/* A horizontal diamond, seen from below when y is above the camera. */
static void under(int x, int y, int z, int rx, int rz, int bucket, Col c) {
    V3 q[4];
    q[0] = (V3){x, y, z + rz};
    q[1] = (V3){x + rx, y, z};
    q[2] = (V3){x - rx, y, z};
    q[3] = (V3){x, y, z - rz};
    lw_quad(q, bucket, c, BLEND_NONE);
}

/* A screen-space streak from a thin far end to a wider near end. */
static void streak(XY a, XY b, int w, int bucket, Col c) {
    XY p[4];
    int dx = b.x - a.x, dy = b.y - a.y;
    if (iabs(dx) + iabs(dy) < 2) {
        p[0].x = b.x; p[0].y = (int16_t)(b.y - w);
        p[1].x = (int16_t)(b.x + w); p[1].y = b.y;
        p[2].x = (int16_t)(b.x - w); p[2].y = b.y;
        p[3].x = b.x; p[3].y = (int16_t)(b.y + w);
    } else {
        int ox = 0, oy = 0;
        if (iabs(dx) > iabs(dy)) oy = w;
        else ox = w;
        p[0].x = (int16_t)(a.x - ox / 2); p[0].y = (int16_t)(a.y - oy / 2);
        p[1].x = (int16_t)(a.x + ox / 2); p[1].y = (int16_t)(a.y + oy / 2);
        p[2].x = (int16_t)(b.x - ox); p[2].y = (int16_t)(b.y - oy);
        p[3].x = (int16_t)(b.x + ox); p[3].y = (int16_t)(b.y + oy);
    }
    screen_quad(p, bucket, c, BLEND_ADD);
}

/* Growths */

static int lod(const Growth *g) {
    int j = (int)(g->seed >> 24);
    if (g->z < NEAR_Z + j)
        return LOD_NEAR;
    if (g->z > MID_Z + j * 4)
        return LOD_FAR;
    return LOD_MID;
}

static void make_growth(Growth *g, uint32_t s, int zbase, int side, int hero) {
    int roll = rnd(&s, 0, 99);
    int ax, clear;
    g->hero = hero;
    if (hero)
        roll = 90;
    g->z = zbase + rnd(&s, 0, CELL - 1);
    g->wz = g->z;
    g->phase = rnd(&s, 0, 63);
    g->sc = &k_schemes[k_scheme_pick[rnd(&s, 0, 15)]];
    g->w = g->r = g->sway = 0;
    if (roll < 30) {
        g->kind = G_CRYSTAL;
        g->h = rnd(&s, 160, 440);
        clear = ROAD_CLEAR + 40;
        ax = clear + rnd(&s, 40, 280);
    } else if (roll < 52) {
        g->kind = G_MOUND;
        g->h = rnd(&s, 150, 420);
        g->w = rnd(&s, 220, 560);
        clear = ROAD_CLEAR + g->w / 2;
        ax = clear + rnd(&s, 40, 900);
    } else if (roll < 84) {
        g->kind = G_FUNGUS;
        g->h = rnd(&s, 560, 1500);
        g->w = rnd(&s, 36, 80);
        g->r = rnd(&s, 240, 600);
        g->sway = rnd(&s, 40, 160);
        clear = ROAD_CLEAR + g->r + g->sway;
        ax = clear + rnd(&s, 40, 700);
    } else if (roll < 95) {
        g->kind = G_GIANT;
        g->h = rnd(&s, 1700, 3000);
        g->w = rnd(&s, 110, 220);
        g->r = rnd(&s, 700, 1300);
        g->sway = rnd(&s, 120, 360);
        clear = ROAD_CLEAR + g->r + g->sway;
        ax = clear + rnd(&s, 80, 1200);
    } else {
        g->kind = G_SPIRE;
        g->h = rnd(&s, 1400, 2800);
        g->w = rnd(&s, 70, 150);
        g->sway = rnd(&s, 160, 420);
        clear = ROAD_CLEAR + g->w + g->sway;
        ax = clear + rnd(&s, 80, 1400);
    }
    g->x = side * ax;
    g->u = g->x;
    g->seed = mix(s);
}

/* The foot follows the road. The stalk then stands in camera space, so a
 * tree stays upright while its base takes the bend and the grade. */
static void plant(Growth *g) {
    int cx, cy, cz;
    path_cam(g->wz, g->u, 0, &cx, &cy, &cz);
    g->x = cx;
    g->z = cz;
    g->foot = cy;
}

/* A twisting stalk of stacked bands. Each ring is one projected center,
 * widened on screen by its depth, and shared by the bands above and below
 * it. The edge light on the side facing the path is cut from the band in
 * screen space. sm smears the top away from the camera. Reports where the
 * top ended up. */
static void stalk(const Growth *g, int segs, int lit, int sm, int *top_x, int *top_z) {
    uint32_t s = g->seed;
    int taper = g->kind == G_SPIRE ? 2 : 9;
    int z0 = g->z;
    int bk = depth_bucket(z0);
    int lit_left = g->x > 0;
    XY l0 = {0, 0}, r0 = {0, 0}, l1 = {0, 0}, r1 = {0, 0};
    int k, ok0 = 0;
    for (k = 0; k <= segs; k++) {
        int a = g->phase + k * 9;
        /* Foot stays on the ground at the growth's x,z — no sway/twist — so
         * mid mushrooms don't look like they hover above the floor. */
        int y = k == 0 ? 0 : g->h * k / segs;
        int cx = k == 0 ? g->x : g->x + g->sway * sn(a) / 127;
        int z = k == 0 ? z0 : z0 + g->w * cs(a) / 48 + sm * k / segs;
        int half = g->w * (16 * segs - (16 - taper) * k) / (16 * segs);
        int zc, ok1;
        if (k == 0)
            half += g->w;
        half += rnd(&s, -g->w / 5, g->w / 5);
        if (half < 4)
            half = 4;
        /* At distance the stem would go sub-pixel and leave a floating cap —
         * keep enough world width that the foot stays a few pixels wide. */
        if (k == 0 && half * (SCREEN_W / 2) / (z0 > 8 ? z0 : 8) < 3)
            half = 3 * z0 / (SCREEN_W / 2) + 2;
        zc = pv(cx, y, z, &l1);
        ok1 = zc != 0;
        if (ok1) {
            int hs = half * (SCREEN_W / 2) / zc + 1;
            if (hs < 2)
                hs = 2;
            /* Cap screen width so a near stalk can't become a wall. */
            if (hs > 48)
                hs = 48;
            r1 = l1;
            l1.x = (int16_t)(l1.x - hs);
            r1.x = (int16_t)(r1.x + hs);
        }
        if (k > 0 && ok0 && ok1) {
            emit(&l1, &r1, &l0, &r0, bk, surface(g->sc->body, (k & 1) ? 256 : 196, z0), BLEND_NONE);
            if (lit) {
                Col c = light(g->sc->rim, pulse(g->wz + y, g->phase), z0);
                int t = lit_left ? 64 : 192;
                XY e1 = lerp_xy(l1, r1, t), e0 = lerp_xy(l0, r0, t);
                if (lit_left)
                    emit(&l1, &e1, &l0, &e0, bk - 1, c, BLEND_NONE);
                else
                    emit(&e1, &r1, &e0, &r0, bk - 1, c, BLEND_NONE);
            }
        }
        l0 = l1; r0 = r1; ok0 = ok1;
        if (k == segs) {
            *top_x = cx;
            *top_z = z;
        }
    }
}

/* A canopy in one stroke: a tilted diamond whose near point dips toward
 * the camera, with glowing gills scaled from that same point. */
static void canopy(const Growth *g, int lv, int cx, int cz, int sm) {
    int R = g->r + g->r * sn(lw_time * 2 + g->phase) / (127 * 16);
    int ch = R * 9 / 16;
    int y = g->h;
    int bk = depth_bucket(cz);
    int j = (int)((g->seed >> 8) & 63) - 32;
    V3 q[4];
    XY p[4];
    q[0] = (V3){cx + j * R / 128, y + ch, cz + sm};
    q[1] = (V3){cx + R, y + ch / 6, cz + sm / 2};
    q[2] = (V3){cx - R, y, cz + sm / 2};
    q[3] = (V3){cx - j * R / 256, y - ch / 3, cz - R / 3};
    if (!project4(q, p))
        return;
    screen_quad(p, bk, surface(g->sc->body, lv == LOD_FAR ? 230 : 280, cz), BLEND_NONE);
    layer(p, p[3], lv == LOD_FAR ? 60 : 130, bk - 1, light(g->sc->core, pulse(g->wz, g->phase), cz), BLEND_NONE);
}

/* The hero's canopy: a two-band dome, a glowing lip, gills seen from below,
 * an accent stroke, a halo, and a light shaft to the floor. */
static void hero_canopy(const Growth *g, int cx, int cz) {
    uint32_t s = g->seed ^ 0x51515151u;
    int R = g->r + g->r * sn(lw_time * 2 + g->phase) / (127 * 16);
    int ch = R * 9 / 16;
    int y = g->h;
    int bk = depth_bucket(cz);
    int p = pulse(g->wz, g->phase);
    const Scheme *sc = g->sc;
    int j1 = rnd(&s, -R / 8, R / 8);
    int j2 = rnd(&s, -R / 8, R / 8);
    int j3 = rnd(&s, -ch / 6, ch / 6);
    int j4 = rnd(&s, -R / 8, R / 8);
    int j5 = rnd(&s, -R / 8, R / 8);
    int j6 = rnd(&s, -ch / 6, ch / 6);
    XY bl, br, ml, mr, tl, tr;
    V3 lip[4] = {
        {cx - R * 17 / 16, y + ch / 10, cz}, {cx + R * 17 / 16, y + ch / 10, cz},
        {cx - R, y - ch / 10, cz}, {cx + R, y - ch / 10, cz}
    };
    if (pv(cx - R, y, cz, &bl) && pv(cx + R, y, cz, &br) &&
        pv(cx - R * 7 / 8 + j1, y + ch / 2 + j3, cz, &ml) &&
        pv(cx + R * 7 / 8 + j2, y + ch / 2 - j3, cz, &mr) &&
        pv(cx - R / 3 + j4, y + ch + j6, cz, &tl) &&
        pv(cx + R / 3 + j5, y + ch - j6, cz, &tr)) {
        XY dome[4] = {tl, tr, bl, br};
        emit(&ml, &mr, &bl, &br, bk, surface(sc->body, 300, cz), BLEND_NONE);
        emit(&tl, &tr, &ml, &mr, bk, surface(sc->body, 210, cz), BLEND_NONE);
        if (glow_ok())
            layer(dome, centroid(dome), 400, bk + 1, glow(sc->core, p / 5, cz), BLEND_ADD);
    }
    kite(cx + j2, y + ch * 7 / 8, cx + j1 / 2, y + ch / 3, y + ch * 5 / 8, R / 6, cz,
         0, bk - 1, light(sc->rim, p * 3 / 4, cz), BLEND_NONE);
    under(cx, y, cz, R * 15 / 16, R * 5 / 8, bk - 1, light(sc->core, 80, cz));
    under(cx, y - 10, cz, R * 9 / 16, R * 3 / 8, bk - 2, light(sc->core, p, cz));
    lw_quad(lip, bk - 3, light(sc->rim, p, cz), BLEND_NONE);
    if (cz > NEAR_Z && cz < 3600 && glow_ok()) {
        int lean = (g->x > 0 ? -1 : 1) * R / 2;
        int fade = clamp((cz - NEAR_Z) < (3600 - cz) ? cz - NEAR_Z : 3600 - cz, 0, 600);
        V3 q[4] = {
            {cx - R / 4, y, cz - 4}, {cx + R / 4, y, cz - 4},
            {cx + lean - R * 2 / 3, 0, cz - 4}, {cx + lean + R * 2 / 3, 0, cz - 4}
        };
        lw_quad(q, bk - 1, glow(sc->core, fade * p / (600 * 6), cz), BLEND_ADD);
    }
}

static void stalk_growth(const Growth *g, int lv) {
    int cx, cz;
    int sm = lv == LOD_NEAR ? smear(g->z, g->x) : 0;
    int hero = g->hero && lv == LOD_MID;
    stalk(g, hero ? 4 : lv == LOD_MID ? 2 : 1, lv != LOD_FAR, sm, &cx, &cz);
    if (!g->r)
        return;
    if (hero)
        hero_canopy(g, cx, cz);
    else
        canopy(g, lv, cx, cz, sm);
}

/* Leaning shards with bright cores. */
static void crystal(const Growth *g, int lv) {
    uint32_t s = g->seed ^ 0x77777777u;
    int n = lv == LOD_FAR || g->z > 2400 ? 1 : 2;
    int p = pulse(g->wz, g->phase);
    int out = g->x > 0 ? 30 : -30;
    int j;
    for (j = 0; j < n; j++) {
        int xb = g->x + rnd(&s, -90, 90);
        int zb = g->z + rnd(&s, -70, 70);
        int hs = g->h * rnd(&s, 45, 100) / 100;
        int ws = rnd(&s, 18, 40);
        int lean = rnd(&s, -50, 50) + out;
        int sm = 0, bk;
        V3 q[4];
        XY pt[4];
        if (zb < CULL_Z)
            continue;
        if (lv == LOD_NEAR)
            sm = smear(zb, xb);
        bk = depth_bucket(zb);
        kite_points(q, xb + lean, hs, xb, -12, hs * 2 / 7, ws, zb, sm);
        if (!project4(q, pt))
            continue;
        screen_quad(pt, bk, light(g->sc->rim, 130, zb), BLEND_NONE);
        if (lv == LOD_FAR)
            continue;
        layer(pt, centroid(pt), 110, bk - 1, light(g->sc->core, p, zb), BLEND_NONE);
        if (j == 0 && lv == LOD_MID && zb < 3000 && glow_ok())
            layer(pt, centroid(pt), 600, bk + 1, glow(g->sc->core, p / 5, zb), BLEND_ADD);
    }
}

/* Overlapping blobs of undergrowth, the painted masses between stalks. */
static void mound(const Growth *g, int lv) {
    uint32_t s = g->seed ^ 0x42424242u;
    int n = lv == LOD_FAR ? 1 : 2;
    int bk0 = depth_bucket(g->z);
    int j;
    for (j = 0; j < n; j++) {
        int dx = rnd(&s, -g->w / 2, g->w / 2);
        int hh = g->h * rnd(&s, 55, 100) / 100;
        int ww = g->w * rnd(&s, 55, 100) / 100;
        int top = g->x + dx + rnd(&s, -ww / 3, ww / 3);
        int foot = g->x + dx + rnd(&s, -ww / 4, ww / 4);
        int waist = hh * rnd(&s, 25, 45) / 100;
        int z = g->z - j * 24;
        int sm = 0;
        V3 q[4];
        XY pt[4];
        if (z < CULL_Z)
            continue;
        if (lv == LOD_NEAR)
            sm = smear(z, g->x + dx);
        kite_points(q, top, hh, foot, -30, waist, ww, z, sm);
        if (!project4(q, pt))
            continue;
        screen_quad(pt, bk0 - j,
                    j ? surface(k_schemes[(g->seed >> 8) % 5].body, 330, z) : surface(g->sc->body, 280, z),
                    BLEND_NONE);
        if (j == 0 && lv != LOD_FAR)
            layer(pt, pt[0], 90, bk0 - 2, light(g->sc->rim, 120, z), BLEND_NONE);
    }
}

static void draw_growth(const Growth *g) {
    int lv = lod(g);
    int reach = g->r + g->sway + g->w * 2 + 200;
    int min_z;
    /* Drop early so near growths don't freeze or cover the road. Larger
     * canopies are culled sooner because they fill the screen first. */
    min_z = CULL_Z + (g->r + g->sway + g->w) / 10;
    if (g->z < min_z)
        return;
    if (iabs(g->x) - reach > (SCREEN_W / 2 + 60) * g->z / (SCREEN_W / 2))
        return;
    /* Canopy must stay off the asphalt. u is the path lateral, so a bend
     * does not pull an off-road growth back onto the deck. */
    if (iabs(g->u) - g->r - g->sway < ROAD_CLEAR && g->z < NEAR_Z + 800)
        return;
    /* Near the horizon the small growths are a pixel or two; the skyline
     * and the tall stalks carry the distance. */
    if (g->z > SMALL_FAR_Z && (g->kind == G_CRYSTAL || g->kind == G_MOUND))
        return;
    lw_foot = g->foot;
    switch (g->kind) {
    case G_CRYSTAL: PROF(PF_CRYSTAL, crystal(g, lv)); break;
    case G_MOUND: PROF(PF_MOUND, mound(g, lv)); break;
    default:
        if (g->hero)
            PROF(PF_HERO, stalk_growth(g, lv));
        else
            PROF(PF_STALK_NEAR + lv, stalk_growth(g, lv));
        break;
    }
    lw_foot = 0;
}

/* Light trails across the forest floor, longer the faster the run. */
static void ground_streak(uint32_t s, int sbase) {
    int along = sbase + rnd(&s, 0, CELL - 1);
    int side = rnd(&s, 0, 1) ? 1 : -1;
    int u = side * (ROAD_HALF + rnd(&s, 30, 560));
    int w = rnd(&s, 5, 14);
    int len = 50 + lw_speed * 16 + rnd(&s, 0, 120);
    int x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3;
    XY q[4];
    const Scheme *sc = &k_schemes[rnd(&s, 0, 4)];
    Col c;
    path_cam(along + len, u - w, 1, &x0, &y0, &z0);
    path_cam(along + len, u + w, 1, &x1, &y1, &z1);
    path_cam(along, u - w, 1, &x2, &y2, &z2);
    path_cam(along, u + w, 1, &x3, &y3, &z3);
    if (z2 > 2600 || z2 < CULL_Z || z0 < CULL_Z)
        return;
    if (!project(x0, y0, z0, &q[0]) || !project(x1, y1, z1, &q[1]) ||
        !project(x2, y2, z2, &q[2]) || !project(x3, y3, z3, &q[3]))
        return;
    c = light(sc->rim, 170, z2);
    screen_quad(q, depth_bucket(z2) - 1, c, BLEND_NONE);
}

static void cell(int c) {
    uint32_t s = mix((uint32_t)c * 0x9E3779B1u + 0x2545F491u);
    int base = c * CELL;
    int side = (s & 4) ? 1 : -1;
    int hero = (s >> 20) % HERO_EVERY == 0;
    Growth g;
    make_growth(&g, mix(s + 0x632BE5ABu), base, side, hero);
    plant(&g);
    draw_growth(&g);
    make_growth(&g, mix(s + 2 * 0x632BE5ABu), base, -side, 0);
    plant(&g);
    draw_growth(&g);
    PROF(PF_GSTREAK, ground_streak(mix(s ^ 0xBEEFBEEFu), base));
}

static void forest(void) {
    int c0 = (lw_scroll - 800) / CELL;
    int c1 = (lw_scroll + LW_FAR) / CELL;
    int c;
    if (c0 < 0)
        c0 = 0;
    /* Near cells first, so a full buffer drops the far end. */
    for (c = c0; c <= c1; c++) {
        if (world_bytes_used() > FOREST_BUDGET)
            break;
        cell(c);
    }
}

/* Backdrop: one soft sky texture, uploaded once. Animation is low priority:
 * a slow UV crawl, and a CLUT pulse only every few frames. */

#ifndef RUNNER_PREVIEW
static uint16_t sky_tpage;
static uint16_t sky_tpage2;
static uint16_t sky_clutid;
#endif
static uint16_t sky_clut_live[256] __attribute__((aligned(4)));
static int sky_ready;
static int sky_clut_tick = -1;

static void sky_upload(void) {
    RECT rect;
    memcpy(sky_clut_live, lumen_sky_clut, sizeof sky_clut_live);
#ifndef RUNNER_PREVIEW
    /* 320 8-bit texels = 160 halfwords; spans two texture pages. */
    setRECT(&rect, LUMEN_SKY_VRAM_X, LUMEN_SKY_VRAM_Y, LUMEN_SKY_W / 2, LUMEN_SKY_H);
    LoadImage(&rect, (const uint32_t *)lumen_sky_pix);
    DrawSync(0);
    sky_tpage = getTPage(1, 0, LUMEN_SKY_VRAM_X, LUMEN_SKY_VRAM_Y);
    sky_tpage2 = getTPage(1, 0, LUMEN_SKY_VRAM_X2, LUMEN_SKY_VRAM_Y);
    setRECT(&rect, LUMEN_SKY_CLUT_X, LUMEN_SKY_CLUT_Y, 256, 1);
    LoadImage(&rect, (const uint32_t *)sky_clut_live);
    DrawSync(0);
    sky_clutid = getClut(LUMEN_SKY_CLUT_X, LUMEN_SKY_CLUT_Y);
#else
    (void)rect;
#endif
    sky_ready = 1;
}

/* Rewrites only the spire and star palette slots. The sky gradient stays put.
 * Each brightness band keeps its dark→bright level; only hue rotates, so a
 * spire's own vertical gradient holds while its colours chase. */
static void sky_cycle_clut(void) {
    /* One hue step every 4 frames. */
    int shift = (lw_time >> 2) % LUMEN_SKY_BEAM_RING;
    int band, i;
    RECT rect;
    for (i = 1; i <= LUMEN_SKY_GRAD_LAST; i++)
        sky_clut_live[i] = lumen_sky_clut[i];
    for (band = 0; band < LUMEN_SKY_BEAM_BANDS; band++) {
        int base = LUMEN_SKY_CYCLE_FIRST + band * LUMEN_SKY_BEAM_RING;
        for (i = 0; i < LUMEN_SKY_BEAM_RING; i++)
            sky_clut_live[base + i] =
                lumen_sky_clut[base + ((i + shift) % LUMEN_SKY_BEAM_RING)];
    }
    /* Stars: dense in the bitmap, but only LUMEN_SKY_STAR_LIT CLUT slots
     * are bright. Epoch advances slowly so they don't strobe. */
    {
        uint32_t epoch = (uint32_t)(lw_time >> 6); /* ~1s per change at 60 Hz */
        uint32_t h = epoch * 1103515245u + 12345u;
        int lit[LUMEN_SKY_STAR_LIT];
        int n;
        for (n = 0; n < LUMEN_SKY_STAR_LIT; n++) {
            lit[n] = (int)((h >> (n * 7)) % LUMEN_SKY_STAR_COUNT);
        }
        for (i = 0; i < LUMEN_SKY_STAR_COUNT; i++) {
            uint16_t c = lumen_sky_clut[LUMEN_SKY_STAR_FIRST + i];
            int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
            int on = 0, pulse;
            for (n = 0; n < LUMEN_SKY_STAR_LIT; n++) {
                if (i == lit[n])
                    on = 1;
            }
            /* Off stars stay barely there so the field reads; few flare. */
            pulse = on ? 255 : 14;
            r = (r * pulse) >> 8;
            g = (g * pulse) >> 8;
            b = (b * pulse) >> 8;
            sky_clut_live[LUMEN_SKY_STAR_FIRST + i] =
                (uint16_t)(r | (g << 5) | (b << 10) | (c & 0x8000));
        }
    }
    setRECT(&rect, LUMEN_SKY_CLUT_X, LUMEN_SKY_CLUT_Y, 256, 1);
    /* No DrawSync: this is low priority; the next frame can wait. */
    LoadImage(&rect, (const uint32_t *)sky_clut_live);
}

#ifdef RUNNER_PREVIEW
/* The host preview has no textured fill, so approximate the sky from the
 * same bitmap with a few gouraud bands. */
static void sky_draw(int hy) {
    int bands = 8, i;
    for (i = 0; i < bands; i++) {
        int y0 = hy * i / bands, y1 = hy * (i + 1) / bands;
        int ty0 = (LUMEN_SKY_H - 1) * i / bands;
        int ty1 = (LUMEN_SKY_H - 1) * (i + 1) / bands;
        int mid = LUMEN_SKY_W / 2;
        uint16_t c0 = sky_clut_live[lumen_sky_pix[ty0 * LUMEN_SKY_W + mid]];
        uint16_t c1 = sky_clut_live[lumen_sky_pix[ty1 * LUMEN_SKY_W + mid]];
        gradient(0, y0, SCREEN_W, y1 - y0, SKY_BUCKET,
                 (c0 & 31) << 3, ((c0 >> 5) & 31) << 3, ((c0 >> 10) & 31) << 3,
                 (c1 & 31) << 3, ((c1 >> 5) & 31) << 3, ((c1 >> 10) & 31) << 3);
    }
}
#else
/* Sky stays still, 1:1 horizontally: left 256px from page 0, right 64px from
 * page 1. No horizontal stretch, so a one-texel spire is one screen pixel. */
static void sky_panel(int x0, int x1, int hy, int u0, int u1, uint16_t tpage) {
    POLY_FT4 *p = alloc_prim(SKY_BUCKET, sizeof(POLY_FT4));
    int v0 = LUMEN_SKY_V0, v1 = LUMEN_SKY_V0 + LUMEN_SKY_H - 1;
    if (!p)
        return;
    setPolyFT4(p);
    /* Raw texels: a tinted texture would be dithered. */
    setShadeTex(p, 1);
    setRGB0(p, 128, 128, 128);
    setXY4(p, x0, 0, x1, 0, x0, hy, x1, hy);
    setUV4(p, u0, v0, u1, v0, u0, v1, u1, v1);
    p->tpage = tpage;
    p->clut = sky_clutid;
}

static void sky_draw(int hy) {
    sky_panel(0, LUMEN_SKY_PAGE_W, hy, 0, LUMEN_SKY_PAGE_W - 1, sky_tpage);
    sky_panel(LUMEN_SKY_PAGE_W, SCREEN_W, hy, 0, SCREEN_W - LUMEN_SKY_PAGE_W - 1, sky_tpage2);
}
#endif

static void sky(void) {
    int hy = path_horizon() + 6;
    if (!sky_ready)
        sky_upload();
    /* CLUT work is cheap but not free; do it every 4 frames. */
    if ((lw_time >> 2) != sky_clut_tick) {
        sky_clut_tick = lw_time >> 2;
        sky_cycle_clut();
    }
    sky_draw(hy);
    gradient(0, hy, SCREEN_W, SCREEN_H - hy, SKY_BUCKET,
             k_floor_glow.r, k_floor_glow.g, k_floor_glow.b, k_floor.r, k_floor.g, k_floor.b);
}

static void aurora(void) {
    /* Aurora is baked into the sky texture. */
}

/* Forest shapes too far to approach: two rows of canopies and spires on the
 * horizon. They hold still, like distant hills, so their primitives are
 * built once and only relinked each frame. */
#define SKYLINE_MAX 96

static POLY_F4 sky_prims[2][SKYLINE_MAX];
static uint8_t sky_layer[SKYLINE_MAX];
static int sky_count;

static void set_f4(POLY_F4 *p, const XY *q, Col c) {
    setPolyF4(p);
    setRGB0(p, clamp(c.r, 0, 255), clamp(c.g, 0, 255), clamp(c.b, 0, 255));
    setXY4(p, q[0].x, q[0].y, q[1].x, q[1].y, q[2].x, q[2].y, q[3].x, q[3].y);
}

static void sky_add(const XY *p, Col c, int layer_i) {
    if (sky_count == SKYLINE_MAX)
        return;
    set_f4(&sky_prims[0][sky_count], p, c);
    set_f4(&sky_prims[1][sky_count], p, c);
    sky_layer[sky_count] = (uint8_t)layer_i;
    sky_count++;
}

static void build_skyline(int base) {
    uint32_t s = 0x51ED270Bu;
    int layer_i;
    for (layer_i = 0; layer_i < 2; layer_i++) {
        RGB tone = layer_i ? (RGB){20, 10, 46} : (RGB){52, 22, 92};
        int x = -30 + layer_i * 13;
        while (x < SCREEN_W + 30) {
            int w = rnd(&s, 8, 22) * (layer_i + 2) / 2;
            int th = rnd(&s, 6, 24) * (layer_i + 2) / 2;
            int stem = 1 + rnd(&s, 0, 2) + layer_i;
            int cx = x + w, cy = base - th;
            Col c = col(tone, rnd(&s, 220, 290));
            if (rnd(&s, 0, 3) == 0) {
                int tip = cy - rnd(&s, 10, 30) * (layer_i + 2) / 2;
                XY spire[4] = {{cx, tip}, {cx + 1, tip}, {cx - stem * 2, base}, {cx + stem * 2, base}};
                sky_add(spire, c, layer_i);
            } else {
                int ch = w * rnd(&s, 30, 60) / 100;
                XY stalk_q[4] = {{cx - stem, cy}, {cx + stem, cy}, {cx - stem * 2, base}, {cx + stem * 2, base}};
                XY cap_q[4] = {{cx - w / 3, cy - ch}, {cx + w / 3, cy - ch}, {cx - w, cy}, {cx + w, cy}};
                sky_add(stalk_q, c, layer_i);
                sky_add(cap_q, c, layer_i);
            }
            x += w * rnd(&s, 80, 170) / 100;
        }
    }
}

static int sky_base = -1;

static void skyline(void) {
    int buf = world_buffer();
    int hy = path_horizon() + 6;
    int i;
    if (!sky_count || sky_base != hy) {
        sky_count = 0;
        sky_base = hy;
        build_skyline(hy);
    }
    for (i = 0; i < sky_count; i++)
        world_link(SKYLINE_BUCKET - sky_layer[i], &sky_prims[buf][i]);
}

/* Motion */

typedef struct { int16_t x, y, z; uint8_t color; } Spore;

static Spore spore_table[SPORES];
static int spores_ready;

static void make_spores(void) {
    int i;
    for (i = 0; i < SPORES; i++) {
        uint32_t s = mix((uint32_t)i * 0x27D4EB2Fu + 0x165667B1u);
        int side = (s & 1) ? 1 : -1;
        spore_table[i].x = (int16_t)(side * (220 + (int)((s >> 1) % 1200)));
        spore_table[i].y = (int16_t)(50 + (int)((s >> 12) % 640));
        spore_table[i].z = (int16_t)((s >> 4) % SPORE_RANGE);
        spore_table[i].color = (uint8_t)((s >> 20) & 7);
    }
}

/* Each spore is projected once; its tail lies on the line back to the
 * vanishing point, scaled by how much farther away the tail is. */
static void spores(void) {
    int wrap = lw_scroll % SPORE_RANGE;
    int i;
    if (!spores_ready) {
        make_spores();
        spores_ready = 1;
    }
    for (i = 0; i < SPORES; i++) {
        const Spore *sp = &spore_table[i];
        V3 v;
        XY head, tail;
        int z = sp->z - wrap, d;
        if (z < 0)
            z += SPORE_RANGE;
        z += 40;
        v.x = sp->x + sn(lw_time + i * 13) * 24 / 127;
        v.y = sp->y + sn(lw_time * 2 + i * 7) * 30 / 127;
        v.z = z;
        warp(&v);
        project(v.x, v.y, v.z, &head);
        if (!xy_ok(&head))
            continue;
        d = 16 + lw_speed * 10;
        tail.x = (int16_t)(SCREEN_W / 2 + (head.x - SCREEN_W / 2) * v.z / (v.z + d));
        tail.y = (int16_t)(HORIZON_Y + (head.y - HORIZON_Y) * v.z / (v.z + d));
        streak(tail, head, clamp(560 / z, 1, 4), depth_bucket(z),
               glow(k_spore[sp->color], 256 - z / 12, z));
    }
}

/* Thin streaks racing out of the vanishing point along the screen edges. */
static void speed_streaks(void) {
    int i;
    if (lw_speed <= 0)
        return;
    for (i = 0; i < STREAKS; i++) {
        int t = lw_time * 5 + i * 23;
        int ph = t & 63;
        uint32_t s = mix((uint32_t)(t >> 6) * 0x9E3779B1u + (uint32_t)i * 0x85EBCA77u);
        int a = (int)(s % 26) - 4;
        int r0 = 130 + ph * 6;
        int len = 12 + lw_speed * 4 + ph;
        int k = ph < 8 ? ph * 32 : 255 - ph * 2;
        XY p, q;
        if (a >= 10)
            a += 13; /* keeps clear of the runner below the vanishing point */
        p.x = (int16_t)(SCREEN_W / 2 + cs(a) * r0 / 127);
        p.y = (int16_t)(path_horizon() + sn(a) * r0 / 127);
        q.x = (int16_t)(SCREEN_W / 2 + cs(a) * (r0 + len) / 127);
        q.y = (int16_t)(path_horizon() + sn(a) * (r0 + len) / 127);
        streak(p, q, 2 + (ph >> 4), STREAK_BUCKET,
               col(k_spore[(s >> 8) & 7], k * lw_speed / 32));
    }
}

static const Light k_light = {
    {10, 6, 28},     /* sky */
    {34, 20, 80},    /* haze */
    {148, 128, 168}, /* tint */
    {70, 48, 96},    /* shadow */
    96,              /* density */
};

static const RoadStyle k_road = {
    &k_light,
    {20, 10, 46},    /* far ridge */
    {52, 22, 92},    /* near ridge */
    {28, 16, 58},    /* asphalt */
    {14, 8, 32},     /* distant asphalt */
    {240, 60, 210},  /* curb */
    2400,
    {90, 240, 255},  /* dividers */
    &k_obstacle,
    900,             /* moss shoulder */
    {24, 70, 88},
    2400,            /* forest floor */
    {12, 6, 28},
};

static void lumen_clear(const LevelFrame *f, int *r, int *g, int *b) {
    (void)f;
    *r = k_light.sky[0];
    *g = k_light.sky[1];
    *b = k_light.sky[2];
}

static void lumen_draw(const LevelFrame *f) {
    lw_scroll = f->scroll;
    lw_speed = f->speed;
    lw_time = f->time;
    lw_glows = 0;
    PROF(PF_SKY, sky());
    PROF(PF_AURORA, aurora());
    PROF(PF_SKYLINE, skyline());
    PROF(PF_FOREST, forest());
    road_draw(&k_road, f);
    PROF(PF_SPORES, spores());
    PROF(PF_STREAKS, speed_streaks());
}

const Level level_lumenwild = {
    "LUMENWILD",
    "lumenwild.way",
    lumen_clear,
    lumen_draw,
    &k_light,
    1,
};
