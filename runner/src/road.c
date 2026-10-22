#include <psxgpu.h>
#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "profile.h"

/* Must reach FAR even with fine near steps, or crates appear past the deck. */
#define RING_MAX 48
#define FAR_BUCKET (OT_LENGTH - 2)
#define MTN_COLS 20
/* ~90° of panorama across the screen (4096 = full turn). */
#define MTN_FOV 1024
#define EDGE_W 16
/* Dividers past this are a pixel or two; they cost more than they read. */
#define DASH_END 2800
/* Strips this short sit on a jump: deck only, no curb across the road. */
#define SLIVER 4
/* Far tier: deck and crate faces only, out to VIEW_Z. It walks up to
 * VIEW_S of track (short of a lap on a closed track) so a turn and the
 * road coming back stay in view. Spacing grows with depth and shrinks
 * where the road bends or changes grade; samples out of view are
 * skipped in long strides, so the cost stays under FAR_MAX samples. */
#define VIEW_Z 20000
#define VIEW_S 44000
#define FAR_MAX 48
#define FAR_DS_MIN STEP
#define FAR_DS_MAX (STEP * 6)
#define FAR_DS_SKIP (STEP * 4)
#define FAR_YAW 192
#define FAR_YAW_Z 8000
/* A crate past this is under 2 px wide: not worth drawing. */
#define CRATE_Z 10240
#define FAR_PITCH 48

typedef struct {
    XY l, r;
    int lz, rz;
    int s;
    int ul, w;        /* left edge and deck width in u */
    int gap;          /* 1 when off-screen road was skipped since the sample before */
    int lx, ly, rx, ry; /* the edges in camera space, with lz and rz */
    int kl, kr;       /* each curb's width, 0..4096 of EDGE_W */
    int room_l, room_r; /* how far past each edge the floor may run before another road */
    const PathSeg *seg;
} RoadSample;

static const RoadStyle *style;
/* The style's far asphalt as this world's air leaves it; set by road_draw. */
static uint8_t road_far[3];
static int road_time;

/* Cheap triangle wave in 0..4095 → -4096..4096 for soft ridges. */
static int tri_wave(int a) {
    a &= 4095;
    if (a < 1024)
        return a * 4;
    if (a < 2048)
        return (2048 - a) * 4;
    if (a < 3072)
        return -(a - 2048) * 4;
    return -(4096 - a) * 4;
}

static int ang_dist(int a, int mark) {
    int d = iabs((a - mark) & 4095);
    if (d > 2048)
        d = 4096 - d;
    return d;
}

/* World-fixed silhouette: height vs absolute yaw, so turns change the view. */
static int mountain_h(int a) {
    int h, d;
    a &= 4095;
    h = 14;
    h += (tri_wave(a * 2) + 4096) * 18 / 8192;
    h += (tri_wave(a * 3 + 700) + 4096) * 12 / 8192;
    h += (tri_wave(a * 5 + 1900) + 4096) * 8 / 8192;
    /* Landmarks so each heading reads differently. */
    d = ang_dist(a, 0);
    if (d < 280)
        h += (280 - d) / 8;
    d = ang_dist(a, 1400);
    if (d < 180)
        h += (180 - d) / 5;
    d = ang_dist(a, 2400);
    if (d < 220)
        h += (220 - d) / 6;
    d = ang_dist(a, 3300);
    if (d < 160)
        h += (160 - d) / 7;
    if (h > 52)
        h = 52;
    return h;
}

static void ridge_band(const int *h, int num, int den, int lift, int foot,
                       const uint8_t *c) {
    int i;
    for (i = 0; i < MTN_COLS; i++) {
        XY p0, p1, p2, p3;
        int x0 = i * SCREEN_W / MTN_COLS;
        int x1 = (i + 1) * SCREEN_W / MTN_COLS;
        p0.x = (int16_t)x0;
        p0.y = (int16_t)(foot - h[i] * num / den - lift);
        p1.x = (int16_t)x1;
        p1.y = (int16_t)(foot - h[i + 1] * num / den - lift);
        p2.x = (int16_t)x0;
        p2.y = (int16_t)(foot + 18);
        p3.x = (int16_t)x1;
        p3.y = (int16_t)(foot + 18);
        poly_f4(&p0, &p1, &p2, &p3, FAR_BUCKET, c[0], c[1], c[2], BLEND_NONE);
    }
}

/* A material seen through the air: amount of 256 is all haze. */
static void through_air(const Light *light, const uint8_t *c, int amount, uint8_t *out) {
    int i;
    for (i = 0; i < 3; i++)
        out[i] = (uint8_t)(c[i] + (light->haze[i] - c[i]) * amount / 256);
}

static void backdrop(const RoadStyle *st);
void road_backdrop(const RoadStyle *st) {
    PROF(PF_R_BACKDROP, backdrop(st));
}

static void backdrop(const RoadStyle *st) {
    const Light *light = st->light;
    int yaw = path_yaw();
    int base = path_horizon() + 8;
    int h[MTN_COLS + 1];
    uint8_t far_c[3], near_c[3];
    int i;
    /* The far ridge sits deepest in the air; the near wash a little less. */
    through_air(light, st->ridge_far, light->density, far_c);
    through_air(light, st->ridge_near, light->density * 3 / 4, near_c);
    /* Air at the foot of the ridge, settling into the empty world below. */
    gradient(0, base - 4, SCREEN_W, 36, FAR_BUCKET,
             (near_c[0] + light->haze[0]) / 2,
             (near_c[1] + light->haze[1]) / 2,
             (near_c[2] + light->haze[2]) / 2,
             light->sky[0], light->sky[1], light->sky[2]);
    for (i = 0; i <= MTN_COLS; i++)
        h[i] = mountain_h(yaw - MTN_FOV / 2 + MTN_FOV * i / MTN_COLS);
    ridge_band(h, 1, 1, 0, base, far_c);
    /* Nearer, lower wash — same family, barely separated. */
    ridge_band(h, 2, 3, 4, base, near_c);
}

static XY lerp_xy(XY a, XY b, int num, int den) {
    XY o;
    o.x = a.x + (b.x - a.x) * num / den;
    o.y = a.y + (b.y - a.y) * num / den;
    return o;
}

static int edge_len(XY a, XY b) {
    int dx = iabs(a.x - b.x);
    int dy = iabs(a.y - b.y);
    return dx > dy ? dx : dy;
}

/* poly_f4v drops quads wider than 280px; near road is often 500–700px.
 * Split laterally until each piece fits, using poly_f4's 480px edge limit.
 * An edge is split only by its own length, so the strip on its other side
 * splits it at the same rounded midpoint; otherwise the seam cracks open.
 * Every piece keeps al, ar on the near edge and bl, br on the far edge, so
 * cn shades the near edge and cf the far. */
static void emit_piece(XY al, XY ar, XY bl, XY br, int bucket, const int *cn, const int *cf,
                       int depth) {
    int near = depth < 3 && edge_len(al, ar) > 400;
    int far = depth < 3 && edge_len(bl, br) > 400;
    if (near && far) {
        XY am = lerp_xy(al, ar, 1, 2);
        XY bm = lerp_xy(bl, br, 1, 2);
        emit_piece(al, am, bl, bm, bucket, cn, cf, depth + 1);
        emit_piece(am, ar, bm, br, bucket, cn, cf, depth + 1);
    } else if (near) {
        XY am = lerp_xy(al, ar, 1, 2);
        emit_piece(al, am, bl, bl, bucket, cn, cf, depth + 1);
        emit_piece(am, am, bl, br, bucket, cn, cf, depth + 1);
        emit_piece(am, ar, br, br, bucket, cn, cf, depth + 1);
    } else if (far) {
        XY bm = lerp_xy(bl, br, 1, 2);
        emit_piece(al, al, bl, bm, bucket, cn, cf, depth + 1);
        emit_piece(al, ar, bm, bm, bucket, cn, cf, depth + 1);
        emit_piece(ar, ar, bm, br, bucket, cn, cf, depth + 1);
    } else {
        poly_g4(&al, &ar, &bl, &br, bucket,
                cn[0], cn[1], cn[2], cn[0], cn[1], cn[2],
                cf[0], cf[1], cf[2], cf[0], cf[1], cf[2], BLEND_NONE);
    }
}

/* dim() pulls toward black; that fights aerial perspective here. */
/* Fog that builds fast near the camera and eases toward cap at infinity:
 * cap * d / (d + FOG_K). Half of cap is reached at FOG_K, so the mid road
 * keeps its own color and only the far road settles into the air. */
#define FOG_K 3000
static void haze_to(int *r, int *g, int *b, int z, const uint8_t *to, int cap) {
    int d = z - CLIP;
    int t = d > 0 ? cap * d / (d + FOG_K) : 0;
    *r = *r + (to[0] - *r) * t / 256;
    *g = *g + (to[1] - *g) * t / 256;
    *b = *b + (to[2] - *b) * t / 256;
}

static void haze_air(int *r, int *g, int *b, int z) {
    haze_to(r, g, b, z, style->light->haze, 220);
}

static void haze_road(int *r, int *g, int *b, int z) {
    haze_to(r, g, b, z, road_far, 210);
}

/* Paint is lit like the runner, by the world's tint. Then it is the
 * asphalt's haze at this depth plus a share of the paint's own contrast that
 * thins with distance, so dividers sink into the road instead of glowing. */
static void haze_paint(int *r, int *g, int *b, int z) {
    const uint8_t *tint = style->light->tint;
    int ar = style->road[0], ag = style->road[1], ab = style->road[2];
    int k = clamp(256 - (z - CLIP) * 216 / (DASH_END - CLIP), 40, 256);
    *r = *r * tint[0] / 128;
    *g = *g * tint[1] / 128;
    *b = *b * tint[2] / 128;
    haze_road(&ar, &ag, &ab, z);
    *r = ar + (*r - style->road[0]) * k / 256;
    *g = ag + (*g - style->road[1]) * k / 256;
    *b = ab + (*b - style->road[2]) * k / 256;
}

/* Depth FAR..VIEW_Z squeezed into the buckets between the near road and the
 * backdrop, so painter's order still hides what a crest covers. */
static int view_bucket(int z) {
    const int lo = (FAR >> OT_SHIFT) + 1;
    const int hi = FAR_BUCKET - 1;
    if (z < FAR)
        return z >> OT_SHIFT;
    if (z >= VIEW_Z)
        return hi;
    return lo + (z - FAR) * (hi - lo) / (VIEW_Z - FAR);
}

/* Past FAR: settle on the distant asphalt, then fade into the air. */
static void haze_far(int *r, int *g, int *b, int z) {
    const uint8_t *haze = style->light->haze;
    int k = clamp((z - FAR) * 160 / (VIEW_Z - FAR), 0, 160);
    haze_to(r, g, b, z, road_far, 210);
    *r = *r + (haze[0] - *r) * k / 256;
    *g = *g + (haze[1] - *g) * k / 256;
    *b = *b + (haze[2] - *b) * k / 256;
}

/* haze_far for the far deck and its edge lines, by depth past FAR in
 * steps of 1 << HAZE_SHIFT. The style fixes them, so they are built once
 * a style. */
#define HAZE_SHIFT 9
#define HAZE_N (((VIEW_Z - FAR) >> HAZE_SHIFT) + 2)
static const RoadStyle *haze_style;
static uint8_t haze_deck[HAZE_N][3], haze_line[HAZE_N][3], haze_crate[HAZE_N][3];

static void haze_tables(void) {
    int i;
    if (haze_style == style)
        return;
    haze_style = style;
    for (i = 0; i < HAZE_N; i++) {
        int z = FAR + (i << HAZE_SHIFT);
        int r = style->road[0], g = style->road[1], b = style->road[2];
        haze_far(&r, &g, &b, z);
        haze_deck[i][0] = (uint8_t)clamp(r, 0, 255);
        haze_deck[i][1] = (uint8_t)clamp(g, 0, 255);
        haze_deck[i][2] = (uint8_t)clamp(b, 0, 255);
        r = (style->edge[0] + road_far[0] * 3) / 4;
        g = (style->edge[1] + road_far[1] * 3) / 4;
        b = (style->edge[2] + road_far[2] * 3) / 4;
        haze_far(&r, &g, &b, z);
        haze_line[i][0] = (uint8_t)clamp(r, 0, 255);
        haze_line[i][1] = (uint8_t)clamp(g, 0, 255);
        haze_line[i][2] = (uint8_t)clamp(b, 0, 255);
        if (style->obstacle) {
            r = style->obstacle->front[0];
            g = style->obstacle->front[1];
            b = style->obstacle->front[2];
            haze_far(&r, &g, &b, z);
            haze_crate[i][0] = (uint8_t)clamp(r, 0, 255);
            haze_crate[i][1] = (uint8_t)clamp(g, 0, 255);
            haze_crate[i][2] = (uint8_t)clamp(b, 0, 255);
        }
    }
}

static const uint8_t *haze_at(uint8_t table[][3], int z) {
    return table[clamp((z - FAR) >> HAZE_SHIFT, 0, HAZE_N - 1)];
}

/* 1 when the quad between two samples is wholly off one side of the screen. */
static int off_screen(const RoadSample *a, const RoadSample *b) {
    int x0 = a->l.x, x1 = a->l.x, y0 = a->l.y, y1 = a->l.y;
    const XY *p[3];
    int i;
    p[0] = &a->r;
    p[1] = &b->l;
    p[2] = &b->r;
    for (i = 0; i < 3; i++) {
        if (p[i]->x < x0) x0 = p[i]->x;
        if (p[i]->x > x1) x1 = p[i]->x;
        if (p[i]->y < y0) y0 = p[i]->y;
        if (p[i]->y > y1) y1 = p[i]->y;
    }
    return x1 < -EDGE_W || x0 > SCREEN_W + EDGE_W || y1 < 0 || y0 > SCREEN_H;
}

/* A point `extra` units of u outside one edge. side is -1 for the left. */
static void edge_out(const RoadSample *a, int side, int extra, int *x, int *y, int *z) {
    int sx, sy, sz, ox, oy, oz;
    int w = a->w > 16 ? a->w : 16;
    if (side < 0) {
        sx = a->lx; sy = a->ly; sz = a->lz;
        ox = a->rx; oy = a->ry; oz = a->rz;
    } else {
        sx = a->rx; sy = a->ry; sz = a->rz;
        ox = a->lx; oy = a->ly; oz = a->lz;
    }
    *x = sx + (sx - ox) * extra / w;
    *y = sy + (sy - oy) * extra / w;
    *z = sz + (sz - oz) * extra / w;
}

/* A corner is drawable when it is in front of the camera and inside the
 * guard band. Past that, the GPU drops the whole quad, which opens a hole
 * in a wall. */
static int on_guard(int x, int y, int z) {
    int lim;
    if (z < 28)
        return 0;
    lim = z * 5;
    return x <= lim && x >= -lim && y <= lim && y >= -lim;
}

static int lerp4096(int a, int b, int t) {
    int d = b - a;
    return a + d / 4096 * t + (d % 4096) * t / 4096;
}

/* Slide a corner along the edge toward one that is already drawable,
 * stopping on the guard so the visible part of the sheet remains. */
static void pull_guard(int *x, int *y, int *z, int x1, int y1, int z1) {
    int x0, y0, z0, lo, hi, n;
    if (on_guard(*x, *y, *z) || !on_guard(x1, y1, z1))
        return;
    x0 = *x;
    y0 = *y;
    z0 = *z;
    lo = 0;
    hi = 4096;
    for (n = 0; n < 8; n++) {
        int t = (lo + hi) >> 1;
        if (on_guard(lerp4096(x0, x1, t), lerp4096(y0, y1, t), lerp4096(z0, z1, t)))
            hi = t;
        else
            lo = t;
    }
    *x = lerp4096(x0, x1, hi);
    *y = lerp4096(y0, y1, hi);
    *z = lerp4096(z0, z1, hi);
}

static int scr128(int v, int z) {
    return v / z * 128 + (v % z) * 128 / z;
}

/* 1 when this edge would be too long on screen to draw in one quad. */
static int span_long(int x0, int y0, int z0, int x1, int y1, int z1) {
    int dx, dy;
    if (z0 < 28 || z1 < 28)
        return 1;
    dx = scr128(x0, z0) - scr128(x1, z1);
    dy = scr128(y0, z0) - scr128(y1, z1);
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    return (dx > dy ? dx : dy) > 240;
}

/* 1 when a sheet from s0 to s1 should be cut in half. Pieces share the
 * cut, so the sheet stays closed. */
static int cloth_coarse(int s0, int s1,
                        int x0, int y0, int z0, int x1, int y1, int z1,
                        int x2, int y2, int z2, int x3, int y3, int z3) {
    if (s1 - s0 <= 24)
        return 0;
    if (z0 < 28 && z1 < 28 && z2 < 28 && z3 < 28)
        return 0;
    if (z0 < 28 || z1 < 28 || z2 < 28 || z3 < 28)
        return 1;
    if (!on_guard(x0, y0, z0) || !on_guard(x1, y1, z1) ||
        !on_guard(x2, y2, z2) || !on_guard(x3, y3, z3))
        return 1;
    if (span_long(x0, y0, z0, x2, y2, z2) || span_long(x1, y1, z1, x3, y3, z3))
        return 1;
    if (span_long(x0, y0, z0, x1, y1, z1) || span_long(x2, y2, z2, x3, y3, z3))
        return 1;
    return 0;
}

/* One band beside the deck, from `inner` to `outer` u past the curb. */
static void verge_band(const RoadSample *a, const RoadSample *b, int side,
                       int inner, int outer, const uint8_t *rgb) {
    int x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3;
    int cn[3], cf[3], za, zb, sort, bucket;
    XY p0, p1, p2, p3;
    if (outer <= inner)
        return;
    edge_out(a, side, inner, &x0, &y0, &z0);
    edge_out(a, side, outer, &x1, &y1, &z1);
    edge_out(b, side, inner, &x2, &y2, &z2);
    edge_out(b, side, outer, &x3, &y3, &z3);
    pull_guard(&x0, &y0, &z0, x2, y2, z2);
    pull_guard(&x1, &y1, &z1, x3, y3, z3);
    pull_guard(&x2, &y2, &z2, x0, y0, z0);
    pull_guard(&x3, &y3, &z3, x1, y1, z1);
    if (!on_guard(x0, y0, z0) || !on_guard(x1, y1, z1) ||
        !on_guard(x2, y2, z2) || !on_guard(x3, y3, z3))
        return;
    za = (z0 + z1) / 2;
    zb = (z2 + z3) / 2;
    /* The outer corner on a bend swings toward the camera. Sorting by it
     * puts the floor in a nearer slice than the curb, and the floor wins. */
    sort = (b->lz + b->rz) / 2;
    if (sort < CLIP)
        sort = CLIP;
    if (sort >= VIEW_Z)
        return;
    cn[0] = rgb[0]; cn[1] = rgb[1]; cn[2] = rgb[2];
    cf[0] = rgb[0]; cf[1] = rgb[1]; cf[2] = rgb[2];
    haze_road(&cn[0], &cn[1], &cn[2], za);
    haze_road(&cf[0], &cf[1], &cf[2], zb);
    project(x0, y0, z0, &p0);
    project(x1, y1, z1, &p1);
    project(x2, y2, z2, &p2);
    project(x3, y3, z3, &p3);
    bucket = sort >= FAR ? view_bucket(sort) : (sort >> OT_SHIFT);
    emit_piece(p0, p1, p2, p3, bucket, cn, cf, 0);
}

/* Shoulder over the floor. Link this after the deck and curb: the first
 * primitive in a bucket is the one that shows, so the floor has to be last
 * or it paints over the curb. On a fork the floor stops short of the other
 * deck. The route is drawn first, and a floor linked before the other road
 * would cover it. */
static void verge(const RoadSample *a, const RoadSample *b) {
    int side;
    if (!style->ground && !style->shoulder)
        return;
    if (a->gap || b->s - a->s < SLIVER)
        return;
    for (side = -1; side <= 1; side += 2) {
        int room = side < 0 ? a->room_l : a->room_r;
        int room_b = side < 0 ? b->room_l : b->room_r;
        int sh = style->shoulder;
        int gr = style->ground;
        if (room_b < room)
            room = room_b;
        if (room < 16)
            continue;
        if (sh > room)
            sh = room;
        if (gr > room)
            gr = room;
        if (sh > 0)
            verge_band(a, b, side, 0, sh, style->shoulder_c);
        if (gr > 0)
            verge_band(a, b, side, 0, gr, style->ground_c);
    }
}

/* The deck shaded from its near edge to its far edge, so the haze ramps
 * across the strip instead of stepping at each seam. */
static void strip(const RoadSample *a, const RoadSample *b) {
    int sort, bucket;
    int cn[3], cf[3];
    PROF_N(PN_STRIP);
    if (a->lz < 8 || a->rz < 8 || b->lz < 8 || b->rz < 8)
        return;
    if (off_screen(a, b))
        return;
    sort = (b->lz + b->rz) / 2;
    if (sort < CLIP)
        sort = CLIP;
    if (sort >= FAR)
        return;
    cn[0] = cf[0] = style->road[0];
    cn[1] = cf[1] = style->road[1];
    cn[2] = cf[2] = style->road[2];
    haze_road(&cn[0], &cn[1], &cn[2], (a->lz + a->rz) / 2);
    haze_road(&cf[0], &cf[1], &cf[2], sort);
    /* Nudge near deck back a hair so crate sides win at the screen bottom. */
    if (sort < 500)
        sort += 48;
    bucket = sort >> OT_SHIFT;
    emit_piece(a->l, a->r, b->l, b->r, bucket, cn, cf, 0);
}

/* Outset k / 4096 of EDGE_W past a road edge in screen space (u is linear
 * across the deck). */
static XY curb_out(XY edge, XY other, int half, int k) {
    XY o;
    int span = half * 2;
    o.x = (int16_t)(edge.x + (edge.x - other.x) * EDGE_W * (k >> 4) / (span << 8));
    o.y = (int16_t)(edge.y + (edge.y - other.y) * EDGE_W * (k >> 4) / (span << 8));
    return o;
}

/* Bright band with a soft along-track pulse. */
static void curb_color(int s, int z, int *r, int *g, int *b) {
    int pulse = tri_wave((s / STEP) * 48 - road_time * 16);
    int e = 170 + pulse * 80 / 4096;
    *r = (style->edge[0] * e) >> 8;
    *g = (style->edge[1] * e) >> 8;
    *b = (style->edge[2] * e) >> 8;
    haze_air(r, g, b, z);
}

static void strip_edges(const RoadSample *a, const RoadSample *b) {
    int er, eg, eb, sort, bucket, half = a->w / 2, half_b = b->w / 2;
    XY alo, aro, blo, bro;
    PROF_N(PN_EDGES);
    if (half < EDGE_W * 2 || half_b < EDGE_W * 2 || b->s - a->s < SLIVER)
        return;
    if (a->lz < 8 || a->rz < 8 || b->lz < 8 || b->rz < 8)
        return;
    if (off_screen(a, b))
        return;
    sort = (b->lz + b->rz) / 2;
    if (sort < CLIP)
        sort = CLIP;
    if (sort >= style->edge_end)
        return;
    curb_color(a->s, sort, &er, &eg, &eb);
    bucket = sort >> OT_SHIFT;
    /* A curb tapering to nothing at one end is drawn as a triangle. */
    if (a->kl || b->kl) {
        alo = curb_out(a->l, a->r, half, a->kl);
        blo = curb_out(b->l, b->r, half_b, b->kl);
        poly_f4(&alo, &a->l, &blo, &b->l, bucket, er, eg, eb, BLEND_NONE);
    }
    if (a->kr || b->kr) {
        aro = curb_out(a->r, a->l, half, a->kr);
        bro = curb_out(b->r, b->l, half_b, b->kr);
        poly_f4(&a->r, &aro, &b->r, &bro, bucket, er, eg, eb, BLEND_NONE);
    }
}

/* s + ds, unless that crosses a band edge where the road does not run on:
 * a centerline jump at a fork or join, or edges that do not meet. Then the
 * edge, with *end set: sample it once as the end of the road before
 * (path_end_side) and again as the start of the road after, so no strip
 * stretches one road into the other. */
static int next_s(int s, int ds, int *end) {
    int b = path_first_cut(s / STEP + 1, (s + ds) / STEP);
    *end = b >= 0;
    return b >= 0 ? b * STEP : s + ds;
}

/* The other road's edge, in this sample's u. *ok is 0 when the edge is
 * too small to read, and the caller leaves the floor alone. */
static int other_u(const RoadSample *a, int x, int y, int z, int *ok) {
    int vx = (a->rx - a->lx) >> 4;
    int vy = (a->ry - a->ly) >> 4;
    int vz = (a->rz - a->lz) >> 4;
    int dx = (x - a->lx) >> 4;
    int dy = (y - a->ly) >> 4;
    int dz = (z - a->lz) >> 4;
    int vv = vx * vx + vy * vy + vz * vz;
    int num;
    if (vv < 8 || a->w < 8) {
        *ok = 0;
        return 0;
    }
    num = dx * vx + dy * vy + dz * vz;
    *ok = 1;
    return a->ul + num * a->w / vv;
}

/* How far past this edge the floor can go before it meets another road.
 * A missing curb is the seam, where the decks already touch. */
static int measure_room(const RoadSample *a, int side) {
    int n = path_branches();
    int k, room = 8000, edge;
    if (n <= 0)
        return room;
    if (side < 0) {
        if (!a->kl)
            return 0;
        edge = a->ul;
    } else {
        if (!a->kr)
            return 0;
        edge = a->ul + a->w;
    }
    for (k = 0; k < n; k++) {
        int s0, s1, ul, ur, lx, ly, lz, rx, ry, rz;
        int u0, u1, ok0, ok1, near, gap;
        if (!path_branch(k, a->s, &s0, &s1) || a->s < s0 || a->s >= s1)
            continue;
        path_swap(k);
        path_edges(a->s, &ul, &ur);
        path_cam_lr(a->s, ul, ur, 0, &lx, &ly, &lz, &rx, &ry, &rz);
        path_swap(k);
        if (lz < 32 && rz < 32)
            continue;
        u0 = other_u(a, lx, ly, lz, &ok0);
        u1 = other_u(a, rx, ry, rz, &ok1);
        if (!ok0 || !ok1)
            continue;
        near = side < 0 ? (u0 > u1 ? u0 : u1) : (u0 < u1 ? u0 : u1);
        if (side < 0) {
            if (near >= edge)
                continue;
            gap = edge - near - 48;
        } else {
            if (near <= edge)
                continue;
            gap = near - edge - 48;
        }
        if (gap < room)
            room = gap;
    }
    if (room < 0)
        room = 0;
    return room;
}

static void fill_room(RoadSample *a) {
    a->room_l = 8000;
    a->room_r = 8000;
    if (path_branches() <= 0 || !style || (!style->ground && !style->shoulder))
        return;
    a->room_l = measure_room(a, -1);
    a->room_r = measure_room(a, 1);
}

static int sample_at(RoadSample *out, int s);

static int sample_side(RoadSample *out, int s, int end) {
    int cz;
    path_end_side(end);
    cz = sample_at(out, s);
    path_end_side(0);
    return cz;
}

static int sample_at(RoadSample *out, int s) {
    int ul, ur, lx, ly, lz, rx, ry, rz;
    out->seg = path_deck_lr(s, &ul, &ur, &lx, &ly, &lz, &rx, &ry, &rz, &out->kl, &out->kr);
    out->ul = ul;
    out->w = ur - ul;
    project(lx, ly, lz, &out->l);
    project(rx, ry, rz, &out->r);
    out->lx = lx;
    out->ly = ly;
    out->lz = lz;
    out->rx = rx;
    out->ry = ry;
    out->rz = rz;
    out->s = s;
    out->gap = 0;
    fill_room(out);
    return (lz + rz) / 2;
}

/* Local deck under a crate so it never floats if the main ring skips a span. */
static void deck_under(int s) {
    int d = OBST_D + 24;
    RoadSample a, b;
    if (sample_at(&a, s - d / 2) < 8 || a.lz < 8 || a.rz < 8)
        return;
    if (sample_at(&b, s + d / 2) < 8 || b.lz < 8 || b.rz < 8)
        return;
    strip(&a, &b);
}

void road_panel(int s, int u0, int u1, int v0, int v1, int r, int g, int b) {
    int x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, sortz;
    path_cam_lr(s, u0, u1, v1, &x0, &y0, &z0, &x1, &y1, &z1);
    path_cam_lr(s, u0, u1, v0, &x2, &y2, &z2, &x3, &y3, &z3);
    if (z0 < 8 || z1 < 8 || z2 < 8 || z3 < 8)
        return;
    sortz = (z0 + z1) / 2;
    if (sortz < CLIP || sortz >= FAR)
        return;
    haze_air(&r, &g, &b, sortz);
    quad(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, sortz, r, g, b);
}

void road_gantry(int s, const uint8_t post[3], const uint8_t beam[3]) {
    int edge = ROAD_HALF + 8;
    road_panel(s, -edge - 36, -edge, 0, 230, post[0], post[1], post[2]);
    road_panel(s, edge, edge + 36, 0, 230, post[0], post[1], post[2]);
    road_panel(s, -edge, edge, 200, 228, beam[0], beam[1], beam[2]);
}

void road_start_gantry(int scroll, const uint8_t post[3], const uint8_t beam[3]) {
    int lap = path_lap();
    int s;
    if (!lap)
        return;
#ifdef RUNNER_PROFILE
    uint32_t prof_t0_ = prof_now();
#endif
    s = (scroll / lap) * lap;
    if (s < scroll + CLIP)
        s += lap;
    if (s >= scroll + FAR)
        return;
    road_gantry(s, post, beam);
#ifdef RUNNER_PROFILE
    prof_ticks[PF_R_GANTRY] += (prof_now() - prof_t0_) & 0xFFFF;
#endif
}

/* Scenery stops while this much of the primitive buffer is still free, so
 * the speed lines and the HUD have somewhere to land. */
#define SCENE_SLACK (10 * 1024)

static int scene_room(void) {
    return world_bytes_used() + SCENE_SLACK <= BUFFER_LENGTH;
}

/* 1 when a sheet `out` past this edge stays off the other road. */
static int clear_of_fork(int s, int side, int out) {
    RoadSample a;
    int room;
    if (out <= 0 || path_branches() <= 0)
        return 1;
    sample_at(&a, s);
    if (style && (style->ground || style->shoulder))
        room = side < 0 ? a.room_l : a.room_r;
    else
        room = measure_room(&a, side);
    return room >= out;
}

static int paint_span(int x0, int y0, int z0, int x1, int y1, int z1,
                      int x2, int y2, int z2, int x3, int y3, int z3,
                      int r, int g, int b) {
    int nz, fz, nr, ng, nb, fr, fg, fb, sort;
    int cn[3], cf[3];
    XY p0, p1, p2, p3;
    pull_guard(&x0, &y0, &z0, x2, y2, z2);
    pull_guard(&x1, &y1, &z1, x3, y3, z3);
    pull_guard(&x2, &y2, &z2, x0, y0, z0);
    pull_guard(&x3, &y3, &z3, x1, y1, z1);
    if (!on_guard(x0, y0, z0) || !on_guard(x1, y1, z1) ||
        !on_guard(x2, y2, z2) || !on_guard(x3, y3, z3))
        return 1;
    nz = (z0 + z1) / 2;
    fz = (z2 + z3) / 2;
    sort = fz;
    if (sort < CLIP)
        sort = CLIP;
    if (sort >= VIEW_Z)
        return 1;
    nr = fr = r;
    ng = fg = g;
    nb = fb = b;
    haze_air(&nr, &ng, &nb, nz);
    haze_air(&fr, &fg, &fb, fz);
    cn[0] = nr;
    cn[1] = ng;
    cn[2] = nb;
    cf[0] = fr;
    cf[1] = fg;
    cf[2] = fb;
    project(x0, y0, z0, &p0);
    project(x1, y1, z1, &p1);
    project(x2, y2, z2, &p2);
    project(x3, y3, z3, &p3);
    emit_piece(p0, p1, p2, p3, view_bucket(sort), cn, cf, 0);
    return 1;
}

static void cloth_u(int s, int side, int out, int *u) {
    int ul, ur;
    if (!side) {
        *u = out;
        return;
    }
    path_edges(s, &ul, &ur);
    *u = side < 0 ? ul - out : ur + out;
}

int road_cloth(int s0, int s1, int side, int out0, int v0, int out1, int v1,
               int r, int g, int b) {
    int u0, u1, reach;
    int x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3;
    if (!scene_room())
        return 0;
    if (s1 < s0 + 8)
        return 1;
    reach = out0 > out1 ? out0 : out1;
    if (side && (!clear_of_fork(s0, side, reach) || !clear_of_fork(s1, side, reach)))
        return 1;
    cloth_u(s0, side, out0, &u0);
    cloth_u(s0, side, out1, &u1);
    path_cam(s0, u0, v0, &x0, &y0, &z0);
    path_cam(s0, u1, v1, &x1, &y1, &z1);
    cloth_u(s1, side, out0, &u0);
    cloth_u(s1, side, out1, &u1);
    path_cam(s1, u0, v0, &x2, &y2, &z2);
    path_cam(s1, u1, v1, &x3, &y3, &z3);
    if (cloth_coarse(s0, s1, x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3)) {
        int mid = (s0 + s1) / 2;
        if (!road_cloth(s0, mid, side, out0, v0, out1, v1, r, g, b))
            return 0;
        return road_cloth(mid, s1, side, out0, v0, out1, v1, r, g, b);
    }
    return paint_span(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, r, g, b);
}

int road_lintel(int s0, int s1, int out, int v, int r, int g, int b) {
    int ul, ur;
    int x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3;
    if (!scene_room())
        return 0;
    if (s1 < s0 + 8)
        return 1;
    path_edges(s0, &ul, &ur);
    path_cam(s0, ul - out, v, &x0, &y0, &z0);
    path_cam(s0, ur + out, v, &x1, &y1, &z1);
    path_edges(s1, &ul, &ur);
    path_cam(s1, ul - out, v, &x2, &y2, &z2);
    path_cam(s1, ur + out, v, &x3, &y3, &z3);
    if (cloth_coarse(s0, s1, x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3)) {
        int mid = (s0 + s1) / 2;
        if (!road_lintel(s0, mid, out, v, r, g, b))
            return 0;
        return road_lintel(mid, s1, out, v, r, g, b);
    }
    return paint_span(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, r, g, b);
}

int road_marker(int s, int u, int half, int v0, int v1, int r, int g, int b) {
    int ul, ur, side, inner, out;
    if (!scene_room())
        return 0;
    if (half < 1)
        half = 1;
    path_edges(s, &ul, &ur);
    if (u + half > ul + 8 && u - half < ur - 8)
        return 1;
    side = (u + half < ul) ? -1 : 1;
    inner = side < 0 ? ul - (u + half) : (u - half) - ur;
    if (inner < 24)
        return 1;
    out = side < 0 ? ul - (u - half) : (u + half) - ur;
    if (!clear_of_fork(s, side, out))
        return 1;
    road_panel(s, u - half, u + half, v0, v1, r, g, b);
    return 1;
}

/* Averaged with the scene, so the line stays see-through. */
static void glow_line(XY a, XY b, int bucket, int r, int g, int bl) {
    LINE_F2 *line;
    DR_TPAGE *mode;
    if (!xy_ok(&a) || !xy_ok(&b) || !edge_ok(a, b))
        return;
    if (world_bytes_used() + sizeof(LINE_F2) + sizeof(DR_TPAGE) > BUFFER_LENGTH)
        return;
    line = alloc_prim(bucket, sizeof(LINE_F2));
    if (!line)
        return;
    setLineF2(line);
    setSemiTrans(line, 1);
    setRGB0(line, clamp(r, 0, 255), clamp(g, 0, 255), clamp(bl, 0, 255));
    setXY2(line, a.x, a.y, b.x, b.y);
    mode = alloc_prim(bucket, sizeof(DR_TPAGE));
    if (mode)
        setDrawTPage(mode, 0, 1, getTPage(0, BLEND_AVG, 0, 0));
}

static const int8_t k_sin[64] = {
    0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126,
    127, 126, 125, 122, 117, 112, 106, 98, 90, 81, 71, 60, 49, 37, 25, 12,
    0, -12, -25, -37, -49, -60, -71, -81, -90, -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106, -98, -90, -81, -71, -60, -49, -37, -25, -12
};

static int sn64(int a) { return k_sin[a & 63]; }
static int cs64(int a) { return k_sin[(a + 16) & 63]; }

static uint32_t mix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

#define STREAKS 48
#define TUBE_R 460
#define TUBE_CROWN (CAM_H + HORIZON_Y * CHAR_Z / (SCREEN_W / 2))

/* Carry the lower end to just past the bottom of the screen. The two
 * projected ends already give the line's direction. */
static void extend_bottom(XY *far, XY *near) {
    int dy, dx, reach, nx, ny, span;
    if (near->y < 0 || near->y >= SCREEN_H || far->y >= near->y)
        return;
    dy = near->y - far->y;
    if (dy < 1)
        return;
    dx = near->x - far->x;
    if (dx > 2000)
        dx = 2000;
    if (dx < -2000)
        dx = -2000;
    reach = SCREEN_H + 12 - near->y;
    nx = near->x + dx * reach / dy;
    ny = SCREEN_H + 12;
    span = iabs(nx - far->x);
    if (iabs(ny - far->y) > span)
        span = iabs(ny - far->y);
    if (span > 480) {
        nx = far->x + (nx - far->x) * 480 / span;
        ny = far->y + (ny - far->y) * 480 / span;
    }
    if (nx < -1000)
        nx = -1000;
    if (nx > 1000)
        nx = 1000;
    near->x = nx;
    near->y = ny;
}

void road_streaks(const LevelFrame *f, int r, int g, int b, int radius, int crown) {
    int picture = f->time >> 1;
    int tube = radius > 0 ? radius : TUBE_R;
    int top = crown > 0 ? crown : TUBE_CROWN;
    int i;
    r = r / 2 + 40;
    g = g / 2 + 40;
    b = b / 2 + 40;
    if (f->speed <= 0)
        return;
    for (i = 0; i < STREAKS; i++) {
        uint32_t slot = mix32((uint32_t)i * 0x9E3779B9u);
        int span = 480;
        int cycle = span * 5;
        int speed = 128 + (int)((slot >> 8) & 63);
        uint32_t clock = (uint32_t)picture * (uint32_t)speed + (slot >> 16);
        int travel = (int)(clock % (uint32_t)cycle);
        int a = (int)(mix32(slot ^ (clock / (uint32_t)cycle)) & 63);
        int ahead = CHAR_Z + span - travel % span;
        int step = 48 + (int)((slot >> 24) & 63);
        int cax = cs64(a), sax = sn64(a);
        int u = f->x + tube * cax / 127;
        int h = top * (sax + 127) / 254;
        int bucket;
        XY far, near;
        int zf, zn;
        if (travel >= span)
            continue;
        zf = path_project(f->scroll + ahead + step, u, h, &far);
        zn = path_project(f->scroll + ahead, u, h, &near);
        if (zf < 48 || zn < 48)
            continue;
        if (far.y < 0 || near.y < 0)
            continue;
        if (radius <= 0)
            extend_bottom(&far, &near);
        bucket = ahead >> OT_SHIFT;
        if (bucket < (CHAR_Z >> OT_SHIFT))
            bucket = CHAR_Z >> OT_SHIFT;
        glow_line(far, near, bucket, r, g, b);
    }
}

/* The deck at u across a sample, in camera space: path_cam_lr is linear
 * in u, so this is exact. */
static void across(const RoadSample *a, int u, int *x, int *y, int *z) {
    int f = a->w ? (u - a->ul) * 4096 / a->w : 2048;
    *x = a->lx + (a->rx - a->lx) * f / 4096;
    *y = a->ly + (a->ry - a->ly) * f / 4096;
    *z = a->lz + (a->rz - a->lz) * f / 4096;
}

/* The deck at u, t/4096 of the way from sample a to sample b: on the strip
 * between them, so a dash never floats off the chord the deck draws. */
static void deck_point(const RoadSample *a, const RoadSample *b, int t, int u,
                       int *x, int *y, int *z) {
    int ax, ay, az, bx, by, bz;
    across(a, u, &ax, &ay, &az);
    across(b, u, &bx, &by, &bz);
    *x = ax + (bx - ax) * t / 4096;
    *y = ay + (by - ay) * t / 4096;
    *z = az + (bz - az) * t / 4096;
}

/* Past this a dash is under two pixels wide, and draws as a line. */
#define DASH_LINE_Z 1400

/* Path-space dashes so lanes stay correct through bends and over crests. */
static void dash(const RoadSample *a, const RoadSample *b, int t0, int t1, int u0, int u1) {
    int x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3;
    int sort, rr, gg, bb;
    XY p0, p1, p2, p3;
    deck_point(a, b, t0, u0, &x0, &y0, &z0);
    if (z0 >= DASH_LINE_Z) {
        if (z0 >= FAR)
            return;
        deck_point(a, b, t1, u1, &x1, &y1, &z1);
        if (z1 < 8)
            return;
        rr = style->dash[0];
        gg = style->dash[1];
        bb = style->dash[2];
        haze_paint(&rr, &gg, &bb, z0);
        project(x0, y0, z0, &p0);
        project(x1, y1, z1, &p1);
        line_f2(p0, p1, z0 >> OT_SHIFT, rr, gg, bb);
        return;
    }
    deck_point(a, b, t0, u0 - 6, &x0, &y0, &z0);
    deck_point(a, b, t0, u0 + 6, &x1, &y1, &z1);
    deck_point(a, b, t1, u1 + 6, &x2, &y2, &z2);
    deck_point(a, b, t1, u1 - 6, &x3, &y3, &z3);
    if (z0 < 40 || z1 < 40 || z2 < 8 || z3 < 8)
        return;
    sort = (z0 + z1) / 2;
    if (sort >= FAR)
        return;
    rr = style->dash[0];
    gg = style->dash[1];
    bb = style->dash[2];
    haze_paint(&rr, &gg, &bb, sort);
    project(x0, y0, z0, &p0);
    project(x1, y1, z1, &p1);
    project(x3, y3, z3, &p2);
    project(x2, y2, z2, &p3);
    poly_f4(&p0, &p1, &p2, &p3, sort >> OT_SHIFT, rr, gg, bb, BLEND_NONE);
}

static int marks_join(const PathSeg *a, const PathSeg *b) {
    int i;
    if (b->marks != a->marks || b->x != a->x1 || b->y != a->y1 || b->z != a->z1)
        return 0;
    for (i = 0; i < a->marks; i++)
        if (b->mu[i] != a->mu1[i])
            return 0;
    return 1;
}

/* Dividers from the baked marks, so they follow a lane change. Where the
 * next segment has other dividers (a lane added or dropped, or a jump), a
 * dash stops at the segment's end instead of running to one of them. */
static void dashes(const RoadSample *a, const RoadSample *b) {
    const PathSeg *seg = a->seg;
    const PathSeg *far;
    int s0 = a->s, s1 = b->s;
    int u0[2], u1[2];
    int n0, n1, gap, t0, t1, i, edge;
    if (!seg->deck || b->gap || ((s0 / 96) & 1) == 0)
        return;
    if ((a->lz + a->rz) / 2 > DASH_END)
        return;
    gap = (s1 - s0) / 8;
    if (gap < 4)
        gap = 4;
    if (s1 - s0 <= gap * 2)
        return;
    n0 = path_marks(s0 + gap, u0);
    t0 = gap * 4096 / (s1 - s0);
    t1 = 4096 - t0;
    far = seg;
    for (edge = (s0 / STEP + 1) * STEP; edge <= s1 - gap; edge += STEP) {
        const PathSeg *next = path_at(edge);
        if (!marks_join(far, next))
            break;
        far = next;
    }
    if (edge <= s1 - gap) {
        if (edge - s0 <= gap)
            return;
        t1 = (edge - s0) * 4096 / (s1 - s0);
        n1 = path_marks(edge - 1, u1);
    } else {
        n1 = path_marks(s1 - gap, u1);
    }
    for (i = 0; i < n0; i++)
        dash(a, b, t0, t1, u0[i], u1[i < n1 ? i : n1 - 1]);
}

/* Past edge_end the curb becomes a one-pixel line on each edge. It starts as
 * the near curb and eases to a hint of curb color over the asphalt by FAR.
 * Link before the deck so it draws over the deck in the same bucket. */
static void edge_lines(const RoadSample *a, const RoadSample *b, int sort) {
    const uint8_t *c = haze_at(haze_line, sort);
    int r = c[0], g = c[1], bl = c[2];
    int k = sort < FAR ? clamp((FAR - sort) * 256 / (FAR - style->edge_end), 0, 256) : 0;
    if (b->s - a->s < SLIVER)
        return;
    if (k) {
        r = (style->edge[0] + road_far[0] * 3) / 4;
        g = (style->edge[1] + road_far[1] * 3) / 4;
        bl = (style->edge[2] + road_far[2] * 3) / 4;
        haze_far(&r, &g, &bl, sort);
    }
    if (k) {
        int cr, cg, cb;
        curb_color(a->s, sort, &cr, &cg, &cb);
        r += (cr - r) * k / 256;
        g += (cg - g) * k / 256;
        bl += (cb - bl) * k / 256;
    }
    if (a->kl || b->kl)
        line_f2(a->l, b->l, view_bucket(sort), r, g, bl);
    if (a->kr || b->kr)
        line_f2(a->r, b->r, view_bucket(sort), r, g, bl);
}

/* Shaded from its near edge to its far edge, so long far pieces read as
 * receding road instead of flat blocks. */
static void far_strip(const RoadSample *a, const RoadSample *b) {
    int za, zb, sort;
    const uint8_t *c0, *c1;
    PROF_N(PN_FAR_STRIP);
    if (a->lz < 8 || a->rz < 8 || b->lz < 8 || b->rz < 8)
        return;
    if (off_screen(a, b))
        return;
    za = (a->lz + a->rz) / 2;
    zb = (b->lz + b->rz) / 2;
    sort = za > zb ? za : zb;
    if (sort < FAR) {
        edge_lines(a, b, zb);
        strip(a, b);
        verge(a, b);
        return;
    }
    if (za >= VIEW_Z && zb >= VIEW_Z)
        return;
    edge_lines(a, b, sort);
    c0 = haze_at(haze_deck, za);
    c1 = haze_at(haze_deck, zb);
    poly_g4(&a->l, &a->r, &b->l, &b->r, view_bucket(sort),
            c0[0], c0[1], c0[2], c0[0], c0[1], c0[2],
            c1[0], c1[1], c1[2], c1[0], c1[1], c1[2], BLEND_NONE);
    verge(a, b);
}

static int on_screen(const RoadSample *p, int cz) {
    if (cz < CLIP || cz >= VIEW_Z)
        return 0;
    if (p->l.x < -40 && p->r.x < -40)
        return 0;
    if (p->l.x > SCREEN_W + 40 && p->r.x > SCREEN_W + 40)
        return 0;
    return 1;
}

static int ang_abs(int d) {
    return iabs(((d + 2048) & 4095) - 2048);
}

/* A chord may turn more than it may pitch or roll: a far bend reads as a
 * curve at coarse steps, but a crest or a bank change needs fine ones to
 * keep its outline. */
/* A turn's chord strays from the arc by the same distance at any depth,
 * which shrinks on screen with depth, so the allowed turn grows with it.
 * Grade keeps a fixed limit: a crest must still hide the road beyond. */
static int too_bent(const PathSeg *a, int s1, int z) {
    const PathSeg *b = path_at(s1);
    return ang_abs(b->yaw - a->yaw) > (z > FAR_YAW_Z ? FAR_YAW * (z / 64) / (FAR_YAW_Z / 64) : FAR_YAW) ||
           ang_abs(b->pitch - a->pitch) > FAR_PITCH ||
           ang_abs(b->roll - a->roll) > FAR_PITCH;
}

/* Next far sample: depth / per while in view, halved through bends and
 * grade changes, and a long stride while out of view. */
static int far_step(int s, const RoadSample *p, int cz, int per) {
    int ds;
    if (!on_screen(p, cz))
        return FAR_DS_SKIP;
    ds = clamp(cz / per, FAR_DS_MIN, FAR_DS_MAX);
    while (ds > FAR_DS_MIN && too_bent(p->seg, s + ds, cz))
        ds /= 2;
    return ds;
}

/* Longest hop over road out of view, so next_s walks few bands. */
#define SKIP_MAX (STEP * 32)

/* How far a sheet beside the road runs. Same stop as the far deck:
 * VIEW_S of track, short of a lap, and not past VIEW_Z. Steps stay
 * `step` beside the camera and grow with depth, so a wall meets the
 * road at the horizon without a quad per near-sized slice. */
int road_span(int s0, int scroll, int step, int reach, int *s1) {
    int lap, end, x, y, z, ds, clear, jump;
    const PathSeg *seg;
    if (step < 8)
        step = 8;
    if (reach < 0)
        reach = 0;
    lap = path_lap();
    end = scroll + VIEW_S;
    if (lap && lap - STEP < VIEW_S)
        end = scroll + lap - STEP;
    if (s0 >= end)
        return 0;
    path_cam(s0, 0, 0, &x, &y, &z);
    /* Beside the camera, including a sheet that starts just behind it.
     * Skipping here would drop the wall next to the runner. */
    if (z < FAR) {
        ds = step;
        if (s0 + ds > end)
            ds = end - s0;
        if (ds < 8)
            return 0;
        *s1 = s0 + ds;
        return 1;
    }
    if (z >= VIEW_Z)
        return 0;
    clear = path_view_clear(s0, reach, VIEW_Z);
    if (clear > 0) {
        jump = clear < step ? step : clear;
        if (jump > SKIP_MAX)
            jump = SKIP_MAX;
        if (s0 + jump > end)
            jump = end - s0;
        if (jump >= 8 && (clear >= jump || path_view_clear(s0 + jump, reach, VIEW_Z) > 0)) {
            *s1 = s0 + jump;
            return 2;
        }
    }
    ds = z / 5;
    if (ds < step)
        ds = step;
    if (ds > step * 8)
        ds = step * 8;
    if (VIEW_Z - z < ds)
        ds = VIEW_Z - z;
    seg = path_at(s0);
    if (seg) {
        int guard = 0;
        while (ds > step && too_bent(seg, s0 + ds, z) && guard < 6) {
            ds /= 2;
            guard++;
        }
    }
    if (s0 + ds > end)
        ds = end - s0;
    if (ds < 8)
        return 0;
    *s1 = s0 + ds;
    return 1;
}

/* Past the widest deck and its curb, with room to spare. */
#define OFF_MARGIN 400

/* From s, out of view: hop on while the road stays out of view, and return
 * where it may come into view, or a jump. A hop within the road's
 * clearance cannot be seen; a shorter one is taken only when both ends are
 * out of view. *gap is 1 when road was skipped; nothing draws across it. */
static int skip_off(int s, int ds, int s_end, int *end, int *gap) {
    int t = next_s(s, ds, end), c;
    *gap = 0;
    while (!*end && t < s_end && (c = path_view_clear(t, OFF_MARGIN, VIEW_Z)) > 0) {
        int e2, t2 = next_s(t, clamp(c, ds, SKIP_MAX), &e2);
        if (t2 >= s_end)
            break;
        if (c < ds && (e2 || !path_view_clear(t2, OFF_MARGIN, VIEW_Z)))
            break;
        t = t2;
        *end = e2;
        *gap = 1;
    }
    return t;
}

/* A far crate is its front face: a few pixels, one quad, raised straight
 * up the screen from its base, which sits on the strip from a to b. */
static void far_crate(int s, int u, const RoadSample *a, const RoadSample *b) {
    int w = 64, s0 = s - OBST_D / 2, t;
    int x0, y0, z0, x1, y1, z1, h;
    const uint8_t *c;
    XY p0, p1, p2, p3;
    PROF_N(PN_FAR_CRATE);
    t = b->s > a->s ? clamp((s0 - a->s) * 4096 / (b->s - a->s), -2048, 4096) : 0;
    deck_point(a, b, t, u - w, &x0, &y0, &z0);
    if (z0 + OBST_D / 2 < FAR) {
        obstacle_box_shade(s, u, course_box_style(s / STEP, style->obstacle), haze_road);
        return;
    }
    if (z0 >= CRATE_Z)
        return;
    deck_point(a, b, t, u + w, &x1, &y1, &z1);
    if (z1 < 8)
        return;
    project(x0, y0, z0, &p2);
    project(x1, y1, z1, &p3);
    h = OBST_H * (SCREEN_W / 2) / z0;
    p0 = p2;
    p1 = p3;
    p0.y = (int16_t)(p0.y - h);
    p1.y = (int16_t)(p1.y - h);
    c = haze_at(haze_crate, z0);
    poly_f4(&p0, &p1, &p2, &p3, view_bucket(z0) - 1, c[0], c[1], c[2], BLEND_NONE);
}

/* Beyond the near ring: coarse deck out to VIEW_Z, and its crates. */
static void far_tier(const RoadSample *seam, int scroll) {
    RoadSample ring[FAR_MAX + 1];
    int lap = path_lap();
    int s_end = scroll + VIEW_S;
    int s = seam->s;
    int n = 1, end = 0, i, band, band0, band1;
#ifdef RUNNER_PROFILE
    uint32_t prof_t0_ = prof_now();
#endif
    if (lap && s_end > scroll + lap - STEP)
        s_end = scroll + lap - STEP;
    ring[0] = *seam;
    while (n <= FAR_MAX && s < s_end) {
        const RoadSample *p = &ring[n - 1];
        int gap = 0;
        if (end) {
            end = 0;
        } else {
            int cz = (p->lz + p->rz) / 2;
            if (on_screen(p, cz))
                s = next_s(s, far_step(p->s, p, cz, 5), &end);
            else
                s = skip_off(s, FAR_DS_SKIP, s_end, &end, &gap);
            if (s >= s_end) {
                s = s_end;
                end = 0;
            }
        }
        sample_side(&ring[n], s, end);
        PROF_N(PN_FAR);
        ring[n].gap = gap;
        n++;
    }
#ifdef RUNNER_PROFILE
    prof_ticks[PF_F_SAMPLE] += (prof_now() - prof_t0_) & 0xFFFF;
    prof_t0_ = prof_now();
#endif
    for (i = n - 2; i >= 0; i--) {
        if (ring[i].seg->deck && !ring[i + 1].gap)
            far_strip(&ring[i], &ring[i + 1]);
    }
#ifdef RUNNER_PROFILE
    prof_ticks[PF_F_STRIPS] += (prof_now() - prof_t0_) & 0xFFFF;
    prof_t0_ = prof_now();
#endif
    if (!style->obstacle)
        return;
    band0 = (seam->s + STEP / 2) / STEP;
    band1 = ring[n - 1].s / STEP;
    i = 0;
    for (band = band0; band <= band1; band++) {
        int os = band * STEP + STEP / 2;
        const RoadSample *a, *b;
        if (os <= seam->s || os > ring[n - 1].s || !path_may_crate(band))
            continue;
        while (i < n - 2 && ring[i + 1].s < os)
            i++;
        a = &ring[i];
        b = &ring[i + 1];
        /* A crate on road out of view, too far to see, or on road skipped as
         * out of view. */
        if (b->gap || (a->lz + a->rz) / 2 >= CRATE_Z ||
            (!on_screen(a, (a->lz + a->rz) / 2) && !on_screen(b, (b->lz + b->rz) / 2)))
            continue;
        if (has_obstacle(band))
            far_crate(os, obstacle_u(band), a, b);
    }
#ifdef RUNNER_PROFILE
    prof_ticks[PF_F_CRATES] += (prof_now() - prof_t0_) & 0xFFFF;
#endif
}

#define BRANCH_MAX 64
/* The road off the route is a glance away: dividers stop nearer. */
#define BRANCH_DASH_END (DASH_END / 2)

/* The road of a fork the route is not on, from s0 to s1: near deck with
 * curbs and dividers, the far deck past FAR, and its crates. Call with the
 * fork swapped onto that road. */
static void branch_draw(int s0, int s1) {
    RoadSample ring[BRANCH_MAX];
    int s = s0, n = 0, end = 0, i, band;
#ifdef RUNNER_PROFILE
    uint32_t prof_t0_ = prof_now();
#endif
    int gap = 0;
    while (n < BRANCH_MAX) {
        int cz = sample_side(&ring[n], s, end || s >= s1);
        int ds;
        PROF_N(PN_FORK);
        ring[n].gap = gap;
        gap = 0;
        n++;
        if (s >= s1)
            break;
        if (end) {
            end = 0;
            continue;
        }
        if (cz < -OFF_MARGIN)
            ds = 0;
        else if (cz < CLIP)
            ds = 96;
        else if (!on_screen(&ring[n - 1], cz))
            ds = 0;
        else if (cz < BRANCH_DASH_END)
            ds = clamp(cz / 3, 64, STEP);
        else
            ds = far_step(s, &ring[n - 1], cz, 3);
#ifdef RUNNER_PROFILE
        if (cz < FAR && ds)
            PROF_N(PN_FORK_NEAR);
        if (!ds)
            PROF_N(PN_FORK_OFF);
#endif
        if (ds)
            s = next_s(s, ds, &end);
        else
            s = skip_off(s, cz < FAR && cz > 0 ? clamp(cz / 3, 64, STEP) : FAR_DS_SKIP,
                         s1, &end, &gap);
        if (s >= s1) {
            s = s1;
            end = 0;
        }
    }
#ifdef RUNNER_PROFILE
    prof_ticks[PF_B_SAMPLE] += (prof_now() - prof_t0_) & 0xFFFF;
    prof_t0_ = prof_now();
#endif
    for (i = n - 2; i >= 0; i--) {
        int za = (ring[i].lz + ring[i].rz) / 2;
        int zb = (ring[i + 1].lz + ring[i + 1].rz) / 2;
        if (!ring[i].seg->deck || ring[i + 1].gap)
            continue;
        if (zb < style->edge_end && za < FAR) {
            strip(&ring[i], &ring[i + 1]);
            strip_edges(&ring[i], &ring[i + 1]);
            verge(&ring[i], &ring[i + 1]);
        } else {
            far_strip(&ring[i], &ring[i + 1]);
        }
    }
#ifdef RUNNER_PROFILE
    prof_ticks[PF_B_STRIPS] += (prof_now() - prof_t0_) & 0xFFFF;
#endif
    PROF(PF_B_DASHES, for (i = 0; i < n - 1; i++)
                          if ((ring[i].lz + ring[i].rz) / 2 < BRANCH_DASH_END)
                              dashes(&ring[i], &ring[i + 1]));
    if (!style->obstacle)
        return;
#ifdef RUNNER_PROFILE
    prof_t0_ = prof_now();
#endif
    i = 0;
    for (band = s0 / STEP; band * STEP < s1; band++) {
        int os = band * STEP + STEP / 2, za, zb;
        const RoadSample *a, *b;
        if (os < ring[0].s || os > ring[n - 1].s || n < 2 || !path_may_crate(band))
            continue;
        while (i < n - 2 && ring[i + 1].s < os)
            i++;
        a = &ring[i];
        b = &ring[i + 1];
        za = (a->lz + a->rz) / 2;
        zb = (b->lz + b->rz) / 2;
        if (b->gap || (za >= CRATE_Z && zb >= CRATE_Z) || (za < CLIP && zb < CLIP) ||
            (!on_screen(a, za) && !on_screen(b, zb)))
            continue;
        if (!has_obstacle(band))
            continue;
        if (za < FAR && b->s - a->s > STEP)
            deck_under(os);
        far_crate(os, obstacle_u(band), a, b);
    }
#ifdef RUNNER_PROFILE
    prof_ticks[PF_B_CRATES] += (prof_now() - prof_t0_) & 0xFFFF;
#endif
}

/* Each fork's other road, where it is in view. */
static void branches_draw(int scroll) {
    int lap = path_lap();
    int view = scroll + (lap && lap - STEP < VIEW_S ? lap - STEP : VIEW_S);
    int k;
    for (k = 0; k < path_branches(); k++) {
        int s0, s1;
        if (!path_branch(k, scroll, &s0, &s1))
            continue;
        if (s0 < scroll + 32)
            s0 = scroll + 32;
        if (s1 > view)
            s1 = view;
        if (s0 >= s1)
            continue;
        path_swap(k);
        branch_draw(s0, s1);
        path_swap(k);
    }
}

static void route_draw(const RoadStyle *st, const LevelFrame *f);

void road_draw(const RoadStyle *st, const LevelFrame *f) {
    style = st;
    road_time = f->time;
    through_air(st->light, st->road_far, st->light->density, road_far);
    haze_tables();
    route_draw(st, f);
    PROF(PF_R_BRANCH, branches_draw(f->scroll));
}

static void route_draw(const RoadStyle *st, const LevelFrame *f) {
    RoadSample ring[RING_MAX];
    int scroll = f->scroll;
    int s_end = scroll + FAR;
    int s = scroll + 32;
    int n = 0, end = 0;
    int i, band, band0, band1, road_s0, road_s1;
#ifdef RUNNER_PROFILE
    uint32_t prof_t0_ = prof_now();
#endif

    while (n < RING_MAX && s < s_end) {
        int cz = sample_side(&ring[n], s, end);
        PROF_N(PN_NEAR);
        int remain_n, remain_s, ds;
        if (cz < 8 || cz >= FAR) {
            if (end) {
                end = 0;
                continue;
            }
            s += 32;
            continue;
        }
        n++;
        if (end) {
            end = 0;
            continue;
        }
        remain_n = RING_MAX - n;
        remain_s = s_end - s;
        if (remain_n <= 0)
            break;
        /* Prefer fine steps near the camera, but always leave enough slots
         * to reach FAR so crates never sit past the deck. */
        ds = cz / 8;
        if (ds * remain_n < remain_s)
            ds = (remain_s + remain_n - 1) / remain_n;
        s = next_s(s, clamp(ds, 28, STEP), &end);
    }
#ifdef RUNNER_PROFILE
    prof_ticks[PF_R_SAMPLE] += (prof_now() - prof_t0_) & 0xFFFF;
    prof_t0_ = prof_now();
#endif

    for (i = n - 2; i >= 0; i--) {
        const PathSeg *seg = ring[i].seg;
        int zb = (ring[i + 1].lz + ring[i + 1].rz) / 2;
        if (!seg->deck)
            continue;
        if (zb >= st->edge_end) {
            edge_lines(&ring[i], &ring[i + 1], zb);
            strip(&ring[i], &ring[i + 1]);
        } else {
            strip(&ring[i], &ring[i + 1]);
            strip_edges(&ring[i], &ring[i + 1]);
        }
        verge(&ring[i], &ring[i + 1]);
    }

#ifdef RUNNER_PROFILE
    prof_ticks[PF_R_STRIPS] += (prof_now() - prof_t0_) & 0xFFFF;
#endif
    PROF(PF_R_DASHES, for (i = 0; i < n - 1; i++) dashes(&ring[i], &ring[i + 1]));

    if (n < 2)
        return;
    PROF(PF_R_FAR, far_tier(&ring[n - 1], scroll));
#ifdef RUNNER_PROFILE
    prof_t0_ = prof_now();
#endif

    /* Only crates that sit on the drawn deck; each gets a local pad too. */
    if (!st->obstacle)
        return;
    road_s0 = ring[0].s;
    road_s1 = ring[n - 1].s;
    band0 = road_s0 / STEP;
    band1 = road_s1 / STEP;
    for (band = band1; band >= band0; band--) {
        int os;
        if (band < 0 || !has_obstacle(band))
            continue;
        os = band * STEP + STEP / 2;
        if (os < road_s0 || os > road_s1)
            continue;
        deck_under(os);
        obstacle_box_shade(os, obstacle_u(band), course_box_style(band, st->obstacle), haze_road);
    }
#ifdef RUNNER_PROFILE
    prof_ticks[PF_R_CRATES] += (prof_now() - prof_t0_) & 0xFFFF;
#endif
}

#ifdef RUNNER_PROFILE
#include <stdio.h>
void road_bench(const RoadStyle *st) {
    int i, r, g, b, acc = 0;
    uint32_t t;
    XY p0 = {100, 150}, p1 = {200, 150}, p2 = {110, 140}, p3 = {190, 140};
    RoadSample a, c;
    style = st;
    through_air(st->light, st->road_far, st->light->density, road_far);
    haze_tables();
    sample_at(&a, 2000);
    sample_at(&c, 2100);
#define RB(name, stmt) do { t = prof_now(); for (i = 0; i < 100; i++) { stmt; } \
        printf("PROF bench %-14s %5u cycles\n", name, (unsigned)((((prof_now() - t) & 0xFFFF) * 8) / 100)); } while (0)
    RB("poly_f4", poly_f4(&p0, &p1, &p2, &p3, 100, 10, 20, 30, BLEND_NONE));
    RB("poly_g4", poly_g4(&p0, &p1, &p2, &p3, 100, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, BLEND_NONE));
    RB("line_f2", line_f2(p0, p1, 100, 1, 2, 3));
    RB("haze_road", (r = 100, g = 100, b = 100, haze_road(&r, &g, &b, 1000 + i), acc += r));
    RB("haze_far", (r = 100, g = 100, b = 100, haze_far(&r, &g, &b, 5000 + i), acc += r));
    RB("curb_color", (curb_color(i * 256, 1000, &r, &g, &b), acc += r));
    RB("view_bucket", acc += view_bucket(5000 + i * 10));
    RB("sample_at", acc += sample_at(&a, 2000 + i * 13));
    sample_at(&a, 2000);
    RB("strip", strip(&a, &c));
    RB("strip_edges", strip_edges(&a, &c));
    RB("far_strip", far_strip(&a, &c));
    RB("on_screen", acc += on_screen(&a, 1000));
    RB("next_s", { int e; acc += next_s(2000 + i, 700, &e); });
    RB("view_clear", acc += path_view_clear(2000 + i * 50, 400, VIEW_Z));
    printf("PROF bench acc %d\n", acc);
}
#endif
