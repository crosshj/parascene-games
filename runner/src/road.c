#include <psxgpu.h>
#include <string.h>
#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"
#include "profile.h"
#include "world_mesh.h"
#include "reach_world_data.h"
#include "road_world_data.h"
#include "meridian_world_data.h"
#ifdef RUNNER_PREVIEW
int world_mesh_preview_legacy;
int world_mesh_preview_depth;
int road_preview_far_limit_hits;
int road_preview_disable_occlusion;

static void mesh_depth_color(int z, int *near, int *far) {
    static const int colors[4][3] = {{248,0,0},{248,248,0},{0,248,0},{0,0,248}};
    int band = z < 1600 ? 0 : z < 5000 ? 1 : z < 10000 ? 2 : 3;
    int i;
    if (!world_mesh_preview_depth)
        return;
    for (i = 0; i < 3; i++)
        near[i] = far[i] = colors[band][i];
}
#endif

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
#ifdef RUNNER_LEGACY_DISTANCE
#define VIEW_Z 20000
#define VIEW_S 44000
#define FAR_MAX 48
#else
static int compiled_view;
#define VIEW_Z (compiled_view ? compiled_view : 20000)
#define VIEW_S (compiled_view ? compiled_view : 44000)
#define FAR_MAX 192
#endif
#define FOG_Z 20000
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
static int streak_overlay;
/* Conservative opaque coverage from this frame's actual near deck. Each
 * cell stores the first row guaranteed covered through the screen bottom.
 * It follows branches, banks and camera motion without a sampled PVS. */
#define OCC_CELL 8
static int deck_cover[SCREEN_W / OCC_CELL];
static int deck_cover_ready;
static void cover_deck(const RoadSample *a,const RoadSample *b) {
    int left,right,top,bottom,i,sign=0;
    const XY *p[4]={&a->l,&a->r,&b->r,&b->l};
    if(!a->seg->deck || b->gap || a->lz<CLIP || a->rz<CLIP || b->lz<CLIP || b->rz<CLIP ||
        a->lz>=FAR || a->rz>=FAR || b->lz>=FAR || b->rz>=FAR) return;
    for(i=0;i<4;i++) {
        const XY *u=p[i],*v=p[(i+1)&3],*w=p[(i+2)&3];
        int cross=(v->x-u->x)*(w->y-v->y)-(v->y-u->y)*(w->x-v->x);
        if(cross && sign && ((cross>0)!=(sign>0))) return;
        if(cross) sign=cross;
    }
    if(iabs(a->l.y-b->l.y)>400 || iabs(a->r.y-b->r.y)>400) return;
    left=a->l.x>b->l.x?a->l.x:b->l.x;
    right=a->r.x<b->r.x?a->r.x:b->r.x;
    top=b->l.y>b->r.y?b->l.y:b->r.y;
    bottom=a->l.y<a->r.y?a->l.y:a->r.y;
    if(top>=bottom || left>=right) return;
    for(i=clamp((left+OCC_CELL-1)/OCC_CELL,0,SCREEN_W/OCC_CELL);
        i<clamp(right/OCC_CELL,0,SCREEN_W/OCC_CELL);i++)
        if(bottom>=deck_cover[i] && top<deck_cover[i])
            deck_cover[i]=top;
}
static int covered_rect(int left,int right,int top) {
    int i,first,last;
#ifdef RUNNER_LEGACY_DISTANCE
    return 0;
#endif
#ifdef RUNNER_PREVIEW
    if(road_preview_disable_occlusion) return 0;
#endif
    if(!deck_cover_ready || right<left || right<0 || left>=SCREEN_W) return 0;
    first=clamp(left/OCC_CELL,0,SCREEN_W/OCC_CELL-1);
    last=clamp(right/OCC_CELL,0,SCREEN_W/OCC_CELL-1);
    for(i=first;i<=last;i++) if(top<deck_cover[i]+2) return 0;
    return 1;
}

int road_runner_bucket(int depth_bucket) {
    return streak_overlay ? (CLIP >> OT_SHIFT) - 2 : depth_bucket;
}

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

const uint8_t *road_horizon_color(const RoadStyle *st) {
    return st->horizon_enabled ? st->horizon : st->light->haze;
}

static void ridge_band(const int *h, int num, int den, int lift, int foot,
                       const uint8_t *c, const uint8_t *foot_c) {
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
        if (foot_c)
            poly_g4(&p0,&p1,&p2,&p3,FAR_BUCKET,
                    c[0],c[1],c[2], c[0],c[1],c[2],
                    foot_c[0],foot_c[1],foot_c[2], foot_c[0],foot_c[1],foot_c[2],BLEND_NONE);
        else
            poly_f4(&p0,&p1,&p2,&p3,FAR_BUCKET,c[0],c[1],c[2],BLEND_NONE);
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
    if (st->horizon_enabled) {
        const uint8_t *end = road_horizon_color(st);
        for (i=0;i<3;i++) {
            far_c[i] = end[i] + (st->ridge_far[i]-end[i])/4;
            near_c[i] = end[i] + (st->ridge_near[i]-end[i])/3;
        }
        /* The lower background is the same color as extinct geometry. */
        gradient(0,base-4,SCREEN_W,SCREEN_H,FAR_BUCKET,
                 end[0],end[1],end[2],end[0],end[1],end[2]);
    } else
    /* Air at the foot of the ridge, settling into the empty world below. */
    gradient(0, base - 4, SCREEN_W, 36, FAR_BUCKET,
             (near_c[0] + light->haze[0]) / 2,
             (near_c[1] + light->haze[1]) / 2,
             (near_c[2] + light->haze[2]) / 2,
             light->sky[0], light->sky[1], light->sky[2]);
    for (i = 0; i <= MTN_COLS; i++)
        h[i] = mountain_h(yaw - MTN_FOV / 2 + MTN_FOV * i / MTN_COLS);
    ridge_band(h, 1, 1, 0, base, far_c, st->horizon_enabled ? st->horizon : 0);
    /* Nearer, lower wash — same family, barely separated. */
    ridge_band(h, 2, 3, 4, base, near_c, st->horizon_enabled ? st->horizon : 0);
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
    if (z > FAR) {
#ifndef RUNNER_LEGACY_DISTANCE
        int t = clamp((z - FAR) * 256 / (FOG_Z - FAR), 0, 256);
        *r += (road_horizon_color(style)[0] - *r) * t / 256;
        *g += (road_horizon_color(style)[1] - *g) * t / 256;
        *b += (road_horizon_color(style)[2] - *b) * t / 256;
#endif
    }
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
    const uint8_t *haze = road_horizon_color(style);
#ifdef RUNNER_LEGACY_DISTANCE
    int k = clamp((z - FAR) * 160 / (FOG_Z - FAR), 0, 160);
#else
    int k = clamp((z - FAR) * 256 / (FOG_Z - FAR), 0, 256);
#endif
    haze_to(r, g, b, z, road_far, 210);
    *r = *r + (haze[0] - *r) * k / 256;
    *g = *g + (haze[1] - *g) * k / 256;
    *b = *b + (haze[2] - *b) * k / 256;
}

/* haze_far for the far deck and its edge lines, by depth past FAR in
 * steps of 1 << HAZE_SHIFT. The style fixes them, so they are built once
 * a style. */
#define HAZE_SHIFT 9
#define HAZE_N (((FOG_Z - FAR) >> HAZE_SHIFT) + 2)
static const RoadStyle *haze_style;
static RoadStyle haze_style_value;
static Light haze_light_value;
static uint8_t haze_deck[HAZE_N][3], haze_line[HAZE_N][3], haze_crate[HAZE_N][3];
#ifdef RUNNER_PREVIEW
void road_preview_reset_haze(void) { haze_style = 0; }
#endif

static void haze_tables(void) {
    int i;
    if (haze_style == style && !memcmp(&haze_style_value, style, sizeof *style) &&
        !memcmp(&haze_light_value, style->light, sizeof *style->light))
        return;
    haze_style = style;
    haze_style_value = *style;
    haze_light_value = *style->light;
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
    if (style->horizon_enabled && z >= FOG_Z)
        return road_horizon_color(style);
    return table[clamp((z - FAR) >> HAZE_SHIFT, 0, HAZE_N - 1)];
}

#ifdef RUNNER_PREVIEW
/* Exercise the real material shading paths, including cached table endpoints.
 * Called after drawing a stage so a changed stage must invalidate its cache. */
int road_preview_atmosphere_check(const uint8_t *expected) {
    static const int depths[]={20000,20001,24000,64000};
    static const int colors[][3]={{0,0,0},{255,255,255},{18,10,32},{78,70,84}};
    int i,j,k,failures=0;
    if (!style->horizon_enabled || memcmp(road_horizon_color(style),expected,3)) return 1;
    for(i=0;i<4;i++) {
        failures += memcmp(haze_at(haze_deck,depths[i]),expected,3)!=0;
        failures += memcmp(haze_at(haze_line,depths[i]),expected,3)!=0;
        for(j=0;j<4;j++) for(k=0;k<2;k++) {
            int r=colors[j][0],g=colors[j][1],b=colors[j][2];
            if(k) haze_far(&r,&g,&b,depths[i]); else haze_air(&r,&g,&b,depths[i]);
            failures += r!=expected[0] || g!=expected[1] || b!=expected[2];
        }
    }
    return failures;
}
#endif

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
static int pull_guard(int *x, int *y, int *z, int x1, int y1, int z1) {
    int x0, y0, z0, lo, hi, n;
    if (on_guard(*x, *y, *z) || !on_guard(x1, y1, z1))
        return 0;
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
    return hi;
}

static int scr128(int v, int z) {
    /* Calls follow on_guard: coordinates are bounded by five depths,
     * within this renderer's lookahead. The product fits signed 32 bits. */
    return v * 128 / z;
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
#ifdef RUNNER_LEGACY_SURFACES
    int reuse=0;
#else
    int reuse=1;
#endif
#ifdef RUNNER_PREVIEW
    reuse=!road_preview_legacy_surfaces;
#endif
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
#ifndef RUNNER_LEGACY_DISTANCE
    if (za > FAR) haze_far(&cn[0], &cn[1], &cn[2], za);
    else haze_road(&cn[0], &cn[1], &cn[2], za);
    if (zb > FAR) haze_far(&cf[0], &cf[1], &cf[2], zb);
    else haze_road(&cf[0], &cf[1], &cf[2], zb);
#else
    haze_road(&cn[0], &cn[1], &cn[2], za);
    haze_road(&cf[0], &cf[1], &cf[2], zb);
#endif
    if(reuse && inner==0 && x0==(side<0?a->lx:a->rx) && y0==(side<0?a->ly:a->ry) && z0==(side<0?a->lz:a->rz))
        p0=side<0?a->l:a->r;
    else project(x0,y0,z0,&p0);
    project(x1, y1, z1, &p1);
    if(reuse && inner==0 && x2==(side<0?b->lx:b->rx) && y2==(side<0?b->ly:b->ry) && z2==(side<0?b->lz:b->rz))
        p2=side<0?b->l:b->r;
    else project(x2,y2,z2,&p2);
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

/* Resolve the authored finish once per track change, including previews,
 * reverse seeks and stage transitions. Endless arcade tracks have no limit. */
static int gate_finish(void) {
    static const char *last_track;
    static int finish;
    const char *track=path_id();
    int i,j;
    if (track==last_track) return finish;
    last_track=track;finish=0;
    for(i=0;i<level_count;i++) for(j=0;j<levels[i]->stage_count;j++) {
        const Stage *stage=&levels[i]->stages[j];
        if(!strcmp(track,stage->track)) {
            finish=stage->finish;
            return finish;
        }
    }
    return 0;
}

int road_scenery_gate_allowed(int s) {
    int finish=gate_finish();
    return !finish || s < finish-FAR;
}

static void gantry_mesh(int s, const uint8_t post[3], const uint8_t beam[3]) {
    int edge = ROAD_HALF + 8;
    road_panel(s, -edge - 36, -edge, 0, 230, post[0], post[1], post[2]);
    road_panel(s, edge, edge + 36, 0, 230, post[0], post[1], post[2]);
    road_panel(s, -edge, edge, 200, 228, beam[0], beam[1], beam[2]);
}

void road_gantry(int s, const uint8_t post[3], const uint8_t beam[3]) {
    if (!road_scenery_gate_allowed(s)) return;
    world_asset("scenery_gate",s,"gantry");
    gantry_mesh(s,post,beam);
    world_asset("unclassified",0,"mixed");
}

void road_finish_gantry(int s, const uint8_t post[3], const uint8_t beam[3]) {
    if (!gate_finish() || s!=gate_finish()) return;
    world_asset("finish_gate",s,"gantry");
    gantry_mesh(s,post,beam);
    world_asset("unclassified",0,"mixed");
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
#ifdef RUNNER_PREVIEW
    mesh_depth_color(sort, cn, cf);
#endif
    emit_piece(p0, p1, p2, p3, view_bucket(sort), cn, cf, 0);
    return 1;
}

/* Screen position of a camera-space corner, matching project().
 * 0 when the corner is too near to trust: the sheet may still cut
 * the view. */
static int corner_screen(int x, int y, int z, int *sx, int *sy) {
    int h = SCREEN_W / 2;
    if (z < 48)
        return 0;
    *sx = h + x * h / z;
    *sy = HORIZON_Y + (CAM_H - y) * h / z;
    return 1;
}

/* 1 when all four corners sit on the same side of the screen, far
 * enough out that the sheet cannot cross it. Those sheets used to be
 * split and projected anyway. */
static int sheet_hidden(int x0, int y0, int z0, int x1, int y1, int z1,
                        int x2, int y2, int z2, int x3, int y3, int z3) {
    int xs[4], ys[4], zs[4], sx, sy, i;
    int left = 1, right = 1, above = 1, below = 1;
    int minx=0x7fffffff,maxx=-0x7fffffff,miny=0x7fffffff,deep=1;
    xs[0] = x0; ys[0] = y0; zs[0] = z0;
    xs[1] = x1; ys[1] = y1; zs[1] = z1;
    xs[2] = x2; ys[2] = y2; zs[2] = z2;
    xs[3] = x3; ys[3] = y3; zs[3] = z3;
    for (i = 0; i < 4; i++) {
        if (!corner_screen(xs[i], ys[i], zs[i], &sx, &sy))
            return 0;
        if(sx<minx) minx=sx;
        if(sx>maxx) maxx=sx;
        if(sy<miny) miny=sy;
        if(zs[i]<FAR+256) deep=0;
        if (sx > -96)
            left = 0;
        if (sx < SCREEN_W + 96)
            right = 0;
        if (sy > -96)
            above = 0;
        if (sy < SCREEN_H + 96)
            below = 0;
    }
    return left || right || above || below || (deep && covered_rect(minx-2,maxx+2,miny-2));
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

#if defined(RUNNER_LEGACY_SURFACES) || defined(RUNNER_PREVIEW)
static int cloth_legacy(int s0, int s1, int side, int out0, int v0, int out1, int v1,
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
    if (sheet_hidden(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3))
        return 1;
    if (cloth_coarse(s0, s1, x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3)) {
        int mid = (s0 + s1) / 2;
        if (!cloth_legacy(s0, mid, side, out0, v0, out1, v1, r, g, b))
            return 0;
        return cloth_legacy(mid, s1, side, out0, v0, out1, v1, r, g, b);
    }
    return paint_span(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, r, g, b);
}
#endif

typedef struct {
    int x,y,z, x1,y1,z1;
    int sx,sy,sx1,sy1; /* exact 160-scale visibility coordinates */
    int tx,ty,tx1,ty1; /* exact 128-scale split coordinates */
    int guard;
} SheetEdge;
typedef struct { int side,out0,v0,out1,v1,r,g,b; } SheetProfile;
#ifdef RUNNER_PREVIEW
int road_preview_legacy_surfaces;
#endif

static void sheet_edge(int s,const SheetProfile *p,SheetEdge *e) {
    int l=0,r=0,u0=p->out0,u1=p->out1;
    e->sx=e->sy=e->sx1=e->sy1=0;
    if(p->side) {
        path_edges(s,&l,&r);
        u0=p->side<0?l-p->out0:r+p->out0;
        u1=p->side<0?l-p->out1:r+p->out1;
    }
    path_cam_pair(s,u0,p->v0,u1,p->v1,&e->x,&e->y,&e->z,&e->x1,&e->y1,&e->z1);
    e->guard=on_guard(e->x,e->y,e->z)&&on_guard(e->x1,e->y1,e->z1);
    if(e->z>=28) { e->tx=scr128(e->x,e->z);e->ty=scr128(e->y,e->z); }
    if(e->z1>=28) { e->tx1=scr128(e->x1,e->z1);e->ty1=scr128(e->y1,e->z1); }
    corner_screen(e->x,e->y,e->z,&e->sx,&e->sy);
    corner_screen(e->x1,e->y1,e->z1,&e->sx1,&e->sy1);
}

static int sheet_cached_hidden(const SheetEdge *a,const SheetEdge *b) {
    int left,right,top,bottom,i;
    int x[4]={a->sx,a->sx1,b->sx,b->sx1}, y[4]={a->sy,a->sy1,b->sy,b->sy1};
    if(a->z<48||a->z1<48||b->z<48||b->z1<48) return 0;
    left=right=x[0];top=bottom=y[0];
    for(i=1;i<4;i++) {
        if(x[i]<left) left=x[i]; if(x[i]>right) right=x[i];
        if(y[i]<top) top=y[i]; if(y[i]>bottom) bottom=y[i];
    }
    if(right<=-96||left>=SCREEN_W+96||bottom<=-96||top>=SCREEN_H+96) return 1;
    return a->z>=FAR+256&&a->z1>=FAR+256&&b->z>=FAR+256&&b->z1>=FAR+256&&
        covered_rect(left-2,right+2,top-2);
}

static int sheet_cached_coarse(int s0,int s1,const SheetEdge *a,const SheetEdge *b) {
    if(s1-s0<=24) return 0;
    if(a->z<28&&a->z1<28&&b->z<28&&b->z1<28) return 0;
    if(a->z<28||a->z1<28||b->z<28||b->z1<28||!a->guard||!b->guard) return 1;
    return iabs(a->tx-b->tx)>240||iabs(a->ty-b->ty)>240||
        iabs(a->tx1-b->tx1)>240||iabs(a->ty1-b->ty1)>240||
        iabs(a->tx-a->tx1)>240||iabs(a->ty-a->ty1)>240||
        iabs(b->tx-b->tx1)>240||iabs(b->ty-b->ty1)>240;
}

/* Preserve the exact split/clip/sort decisions, but compute each new edge
 * once. The recursive renderer previously resampled both endpoints in both
 * children, including large amounts of geometry that was then clipped. */
/* Homogeneous clip planes also work when a corner is behind the camera.
 * Testing projected coordinates cannot reject those near-camera pieces and
 * used to recurse down to tiny invisible spans before guard clipping them. */
static int sheet_halfspace_hidden(const SheetEdge *a,const SheetEdge *b) {
    int x[4]={a->x,a->x1,b->x,b->x1},y[4]={a->y,a->y1,b->y,b->y1};
    int z[4]={a->z,a->z1,b->z,b->z1};
    int i,left=1,right=1,top=1,bottom=1,behind=1;
    for(i=0;i<4;i++) {
        left &= x[i]*160 < -256*z[i];
        right &= x[i]*160 > 256*z[i];
        top &= (CAM_H-y[i])*160 < -(HORIZON_Y+96)*z[i];
        bottom &= (CAM_H-y[i])*160 > (SCREEN_H-HORIZON_Y+96)*z[i];
        behind &= z[i]<28;
    }
    return left||right||top||bottom||behind;
}
static int sheet_tree(int s0,int s1,const SheetProfile *p,const SheetEdge *a,const SheetEdge *b) {
    int reach=p->out0>p->out1?p->out0:p->out1;
    if(!scene_room()) return 0;
    if(s1<s0+8) return 1;
    if(p->side && (!clear_of_fork(s0,p->side,reach)||!clear_of_fork(s1,p->side,reach))) return 1;
    if(s1-s0<=STEP && sheet_halfspace_hidden(a,b)) return 1;
    if(sheet_cached_hidden(a,b)) return 1;
    if(sheet_cached_coarse(s0,s1,a,b)) {
        int mid=(s0+s1)/2;
        SheetEdge m;
        sheet_edge(mid,p,&m);
        if(!sheet_tree(s0,mid,p,a,&m)) return 0;
        return sheet_tree(mid,s1,p,&m,b);
    }
    return paint_span(a->x,a->y,a->z,a->x1,a->y1,a->z1,b->x,b->y,b->z,b->x1,b->y1,b->z1,p->r,p->g,p->b);
}

int road_cloth(int s0,int s1,int side,int out0,int v0,int out1,int v1,int r,int g,int b) {
#ifdef RUNNER_LEGACY_SURFACES
    return cloth_legacy(s0,s1,side,out0,v0,out1,v1,r,g,b);
#else
    SheetProfile p={side,out0,v0,out1,v1,r,g,b};
    SheetEdge a,z;
    static struct { uint32_t revision; int s; SheetProfile profile; SheetEdge edge; } cache[3];
    int key=side<0?0:side>0?2:1;
    uint32_t revision=path_view_revision();
#ifdef RUNNER_PREVIEW
    if(road_preview_legacy_surfaces) return cloth_legacy(s0,s1,side,out0,v0,out1,v1,r,g,b);
#endif
    if(!scene_room()) return 0;
    if(s1<s0+8) return 1;
    if(cache[key].revision==revision && cache[key].s==s0 &&
       cache[key].profile.side==side && cache[key].profile.out0==out0 && cache[key].profile.v0==v0 &&
       cache[key].profile.out1==out1 && cache[key].profile.v1==v1)
        a=cache[key].edge;
    else sheet_edge(s0,&p,&a);
    sheet_edge(s1,&p,&z);
    cache[key].revision=revision; cache[key].s=s1; cache[key].profile=p; cache[key].edge=z;
    return sheet_tree(s0,s1,&p,&a,&z);
#endif
}

/* Resident meshes use distance snapshots: no per-frame path sampling or
 * LOD decisions for their far geometry. Near sheets retain normal clipping. */
#ifdef RUNNER_PREVIEW
int road_preview_legacy_mesh;
#endif
static const WorldMesh *compiled_wall_package(void) {
#ifndef RUNNER_LEGACY_SURFACES
    unsigned i,route=0;
    int k;
    for(k=0;k<path_branches() && k<3;k++)
        route|=(unsigned)path_route(k)<<k;
    for(i=0;i<WORLD_MESH_COUNT;i++)
        if(!strcmp(path_id(),world_meshes[i].track) && world_meshes[i].route_mask==route) return &world_meshes[i];
    for(i=0;i<MERIDIAN_WORLD_MESH_COUNT;i++)
        if(!strcmp(path_id(),meridian_world_meshes[i].track) && meridian_world_meshes[i].route_mask==route)
            return &meridian_world_meshes[i];
#endif
    return 0;
}
static const WorldMesh *compiled_walls(void) {
#ifdef RUNNER_PREVIEW
    if(road_preview_legacy_mesh) return 0;
#endif
    return compiled_wall_package();
}
static unsigned mesh_stride(const WorldMesh *m,uint32_t bit) {
    unsigned byte=bit>>3,shift=bit&7;
    unsigned bits=m->indices[byte]|((unsigned)m->indices[byte+1]<<8);
    return ((bits>>shift)&((1u<<m->stride_bits)-1))+1;
}
int road_compiled_walls_start(int scroll) {
    const WorldMesh *m=compiled_walls();
    const WorldSchedule *q;
    int band=scroll/STEP,base,i;
    if(!m || scroll<0 || band>=m->schedule_count) return -1;
    q=&m->schedules[band];base=band?band-1:0;
    {
        int relative=q->first_offset;uint32_t bit=q->first;
        for(i=0;i<q->count;i++) {
            int s;
            if(i) { relative+=(int)mesh_stride(m,bit);bit+=m->stride_bits; }
            s=(base+relative)*STEP;
            if(s>=scroll+m->near_distance) return s;
        }
    }
    return -1;
}
typedef struct { int x,y,z; XY screen; } MeshPoint;
typedef struct { MeshPoint p[4]; } MeshWallRing;
static void mesh_wall_ring(const WorldRing *r,MeshWallRing *out) {
    static const int corners[4]={0,1,5,4};
    int k;
    PROF_N(PN_WORLD_RING);
    for(k=0;k<4;k++) {
        int j=corners[k];
        MeshPoint *p=&out->p[k];
        path_transform(r->origin.x+(j?r->offset[j-1][0]:0),
                       r->origin.y+(j?r->offset[j-1][1]:0),
                       r->origin.z+(j?r->offset[j-1][2]:0),&p->x,&p->y,&p->z);
        if(on_guard(p->x,p->y,p->z)) project(p->x,p->y,p->z,&p->screen);
    }
}
static void compiled_walls_range(int scroll,int start,int end,int r,int g,int b) {
    const WorldMesh *m=compiled_walls();
    const WorldSchedule *q;
    MeshWallRing a,z;
    int band=scroll/STEP,base,i,have=0,previous=-1;
    if(!m || start<0 || scroll<0 || band>=m->schedule_count) return;
    q=&m->schedules[band];base=band?band-1:0;
    {
    int relative=q->first_offset;uint32_t bit=q->first;
    for(i=0;i<q->count;i++) {
        int index,side;
        if(i) { relative+=(int)mesh_stride(m,bit);bit+=m->stride_bits; }
        index=base+relative;
        if(end<0 && index*STEP<start) continue;
        mesh_wall_ring(&m->rings[index],&z);
        if(m->rings[index].flags&WORLD_RING_CUT_BEFORE) have=0;
        if(have && (end<0 || (previous*STEP>=start && index*STEP<=end))) {
        if(!scene_room()) return;
        for(side=0;side<4;side+=2) {
            const MeshPoint *p=&a.p[side],*t=&a.p[side+1],*u=&z.p[side],*v=&z.p[side+1];
            int sort=(u->z+v->z)/2;
            int cn[3]={r,g,b},cf[3]={r,g,b};
            if(sort>=VIEW_Z) continue;
            if(!on_guard(p->x,p->y,p->z)||!on_guard(t->x,t->y,t->z)||
               !on_guard(u->x,u->y,u->z)||!on_guard(v->x,v->y,v->z)) {
                paint_span(p->x,p->y,p->z,t->x,t->y,t->z,u->x,u->y,u->z,v->x,v->y,v->z,r,g,b);
                continue;
            }
            haze_air(&cn[0],&cn[1],&cn[2],(p->z+t->z)/2);
            haze_air(&cf[0],&cf[1],&cf[2],sort);
#ifdef RUNNER_PREVIEW
            mesh_depth_color(sort,cn,cf);
#endif
            PROF_N(PN_WORLD_SPAN);
            emit_piece(p->screen,t->screen,u->screen,v->screen,view_bucket(sort),cn,cf,0);
        }
        }
        a=z;previous=index;have=1;
    }
    }
}
void road_compiled_walls(int scroll,int start,int r,int g,int b) {
    compiled_walls_range(scroll,start,-1,r,g,b);
}
void road_compiled_walls_range(int scroll,int start,int end,int r,int g,int b) {
    if(end>=start) compiled_walls_range(scroll,start,end,r,g,b);
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
    if (sheet_hidden(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3))
        return 1;
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

static void road_streaks(const LevelFrame *f, int r, int g, int b) {
    int picture = f->time >> 1;
    int overlay = 1;
    int tube = TUBE_R;
    int top = TUBE_CROWN;
    int i;
    streak_overlay = overlay;
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
        extend_bottom(&far, &near);
        bucket = ahead >> OT_SHIFT;
        if (bucket < (CHAR_Z >> OT_SHIFT))
            bucket = CHAR_Z >> OT_SHIFT;
        if (overlay)
            bucket = (CLIP >> OT_SHIFT) - 1;
        glow_line(far, near, bucket, r, g, b);
    }
}

void road_speed_lines(const LevelFrame *f) {
    if (!style || !f || f->speed <= 0)
        return;
    world_asset("speed_lines", 0, "overlay");
    road_streaks(f, style->edge[0], style->edge[1], style->edge[2]);
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
    if(a->lz>FAR+256 && a->rz>FAR+256 && b->lz>FAR+256 && b->rz>FAR+256) {
        int outside=style->ground>style->shoulder?style->ground:style->shoulder;
        XY p[4]={a->l,a->r,b->l,b->r};
        int left,right,top,j,deep=1;
        if(outside>0) for(j=0;j<4;j++) {
            const RoadSample *ring=j<2?a:b;
            int side=(j&1)?1:-1,room=side<0?ring->room_l:ring->room_r;
            int x,y,z;
            edge_out(ring,side,room<outside?room:outside,&x,&y,&z);
            if(z<FAR+256) deep=0;
            project(x,y,z,&p[j]);
        }
        left=right=p[0].x;top=p[0].y;
        for(j=1;j<4;j++) {
            if(p[j].y<top) top=p[j].y;
            if(p[j].x<left) left=p[j].x;
            if(p[j].x>right) right=p[j].x;
        }
        if(deep && covered_rect(left-2,right+2,top-2)) return;
    }
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
    /* Once every road material has reached the common fog endpoint,
     * preserve the corridor silhouette with one face. Dividers, curbs,
     * shoulder and floor have no remaining color contrast to represent. */
    if (za >= FOG_Z && zb >= FOG_Z) {
        int outside=style->ground>style->shoulder?style->ground:style->shoulder;
        int l=outside, r=outside, x,y,z;
        XY al=a->l,ar=a->r,bl=b->l,br=b->r;
        const uint8_t *fog=road_horizon_color(style);
        if(a->room_l<l) l=a->room_l;
        if(b->room_l<l) l=b->room_l;
        if(a->room_r<r) r=a->room_r;
        if(b->room_r<r) r=b->room_r;
        if(l>0) {
            edge_out(a,-1,l,&x,&y,&z); project(x,y,z,&al);
            edge_out(b,-1,l,&x,&y,&z); project(x,y,z,&bl);
        }
        if(r>0) {
            edge_out(a,1,r,&x,&y,&z); project(x,y,z,&ar);
            edge_out(b,1,r,&x,&y,&z); project(x,y,z,&br);
        }
        poly_f4(&al,&ar,&bl,&br,view_bucket(sort),fog[0],fog[1],fog[2],BLEND_NONE);
        return;
    }
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
int road_span(int s0, int scroll, int step, int reach, int height, int *s1) {
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
    /* Test the entire cross-section before the near/far split. Negative
     * depth used to enter the detailed near loop, so a long view spent
     * most of its CPU rebuilding sheets behind the camera. The envelope
     * includes wall height as well as width. Stop skips at topology cuts. */
    clear = path_view_clear(s0, reach + height, VIEW_Z);
    if (clear >= step * 2) {
        int cut;
        jump = clear / 2;
        if (jump > SKIP_MAX) jump = SKIP_MAX;
        if (s0 + jump > end) jump = end - s0;
        cut = path_first_cut(s0 / STEP + 1, (s0 + jump) / STEP);
        if (cut >= 0) jump = cut * STEP - s0;
        if (jump >= 8) { *s1 = s0 + jump; return 2; }
    }
    /* Beside the camera, including a sheet that starts just behind it.
     * Skipping here would drop the wall next to the runner. Further
     * along, the sheet grows. A bend keeps the short step, and a sheet
     * that fills the screen is still cut by cloth_coarse. */
    if (z < FAR) {
        const PathSeg *bend;
        int guard = 0;
        ds = step;
        if (z > 900) {
            int grown = z / 2;
            if (grown > ds)
                ds = grown;
            if (ds > step * 4)
                ds = step * 4;
            bend = path_at(s0);
            if (bend) {
                while (ds > step && too_bent(bend, s0 + ds, z) && guard < 6) {
                    ds /= 2;
                    guard++;
                }
            }
        }
        if (s0 + ds > end)
            ds = end - s0;
        if (ds < 8)
            return 0;
        *s1 = s0 + ds;
        return 1;
    }
    ds = z / 5;
    if (ds < step)
        ds = step;
    if (ds > step * 8)
        ds = step * 8;
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

typedef struct { MeshPoint p[6]; RoadSample road; } MeshRoadRing;
static void mesh_road_ring(const WorldRing *r,int s,MeshRoadRing *out) {
    int k,ul,ur;
    RoadSample *q=&out->road;
    for(k=0;k<6;k++) {
        MeshPoint *p=&out->p[k];
        path_transform(r->origin.x+(k?r->offset[k-1][0]:0),
                       r->origin.y+(k?r->offset[k-1][1]:0),
                       r->origin.z+(k?r->offset[k-1][2]:0),&p->x,&p->y,&p->z);
        project(p->x,p->y,p->z,&p->screen);
    }
    path_edges(s,&ul,&ur);
    q->s=s;q->seg=path_at(s);q->ul=ul;q->w=ur-ul;q->gap=0;
    q->l=out->p[2].screen;q->r=out->p[3].screen;
    q->lx=out->p[2].x;q->ly=out->p[2].y;q->lz=out->p[2].z;
    q->rx=out->p[3].x;q->ry=out->p[3].y;q->rz=out->p[3].z;
    q->kl=q->kr=4096;q->room_l=q->room_r=8000;
}
static void mesh_road_strip(const MeshRoadRing *a,const MeshRoadRing *b) {
    int strip,za=(a->road.lz+a->road.rz)/2,zb=(b->road.lz+b->road.rz)/2;
    int sort=za>zb?za:zb;
    if(sort>=VIEW_Z || sort<CLIP) return;
    /* At the common fog endpoint the five material strips are identical.
     * Submit their outer silhouette once, as the procedural far tier does. */
    if(za>=FOG_Z && zb>=FOG_Z) {
        const uint8_t *fog=road_horizon_color(style);
        poly_f4(&a->p[0].screen,&a->p[5].screen,&b->p[0].screen,&b->p[5].screen,
                view_bucket(sort),fog[0],fog[1],fog[2],BLEND_NONE);
        return;
    }
    for(strip=0;strip<5;strip++) {
        const MeshPoint *p=&a->p[strip],*q=&a->p[strip+1],*u=&b->p[strip],*v=&b->p[strip+1];
        int cn[3],cf[3],k;
        const uint8_t *color=strip==2?style->road:(strip==1||strip==3)?style->shoulder_c:style->ground_c;
        if(p->z<CLIP||q->z<CLIP||u->z<CLIP||v->z<CLIP) {
            far_strip(&a->road,&b->road);return;
        }
        if(strip==2) {
            const uint8_t *n=haze_at(haze_deck,za),*f=haze_at(haze_deck,zb);
            for(k=0;k<3;k++) {cn[k]=n[k];cf[k]=f[k];}
        } else {
            for(k=0;k<3;k++) cn[k]=cf[k]=color[k];
            haze_far(&cn[0],&cn[1],&cn[2],(p->z+q->z)/2);
            haze_far(&cf[0],&cf[1],&cf[2],(u->z+v->z)/2);
        }
        emit_piece(p->screen,q->screen,u->screen,v->screen,view_bucket(sort),cn,cf,0);
    }
    edge_lines(&a->road,&b->road,sort);
}
static int compiled_far_road(const RoadSample *seam,int scroll) {
#ifndef RUNNER_LEGACY_SURFACES
    const WorldMesh *m=0;const WorldSchedule *q;
    MeshRoadRing ring[128];
    int band=scroll/STEP,base,i,n=0,k;
#ifdef RUNNER_PREVIEW
    if(road_preview_legacy_mesh) return 0;
#endif
    if(scroll<0 || style->ground!=road_world_profile[0][1] || style->shoulder!=road_world_profile[1][1]) return 0;
    for(i=0;i<(int)ROAD_WORLD_MESH_COUNT;i++) if(!strcmp(path_id(),road_world_meshes[i].track)) m=&road_world_meshes[i];
    if(!m||band>=m->schedule_count) return 0;
    q=&m->schedules[band];base=band?band-1:0;
    if(q->count>128) return 0;
    {
    int relative=q->first_offset;uint32_t bit=q->first;
    for(i=0;i<q->count;i++) {
        int index;
        if(i) { relative+=(int)mesh_stride(m,bit);bit+=m->stride_bits; }
        index=base+relative;
        if(index*STEP<=seam->s) continue;
        mesh_road_ring(&m->rings[index],index*STEP,&ring[n++]);
    }
    }
    if(!n) return 0;
    for(i=n-2;i>=0;i--) mesh_road_strip(&ring[i],&ring[i+1]);
    far_strip(seam,&ring[0].road);
    if(!style->obstacle) return 1;
    k=0;
    for(band=(seam->s+STEP/2)/STEP;band<=ring[n-1].road.s/STEP;band++) {
        int os=band*STEP+STEP/2;
        const RoadSample *a,*b;
        if(os<=seam->s||!path_may_crate(band)) continue;
        while(k<n-1 && ring[k].road.s<os) k++;
        a=k?&ring[k-1].road:seam;b=&ring[k].road;
        if((a->lz+a->rz)/2>=CRATE_Z) continue;
        if(has_obstacle(band)) far_crate(os,obstacle_u(band),a,b);
    }
    return 1;
#else
    return 0;
#endif
}

/* Beyond the near ring: coarse deck out to VIEW_Z, and its crates. */
static void far_tier(const RoadSample *seam, int scroll) {
    if(compiled_far_road(seam,scroll)) return;
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
            if(style->analytic_continuation && path_is_straight() && cz>=FOG_Z) break;
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
#ifdef RUNNER_PREVIEW
    if (s < s_end && on_screen(&ring[n-1], (ring[n-1].lz+ring[n-1].rz)/2) &&
        (ring[n-1].lz+ring[n-1].rz)/2 < FOG_Z)
        road_preview_far_limit_hits++;
#endif
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
    int i;
    deck_cover_ready=0;
    for(i=0;i<SCREEN_W/OCC_CELL;i++) deck_cover[i]=SCREEN_H;
    streak_overlay = 0;
    style = st;
#ifndef RUNNER_LEGACY_DISTANCE
    {
        const WorldMesh *mesh=compiled_wall_package();
        compiled_view=mesh?(int)mesh->lookahead:0;
    }
#else
    compiled_view=0;
#endif
    road_time = f->time;
    through_air(st->light, st->road_far, st->light->density, road_far);
    haze_tables();
    route_draw(st, f);
    PROF(PF_R_BRANCH, branches_draw(f->scroll));
}

void road_to_vanishing_point(int scroll) {
    int ul, ur;
    XY a, b, vp = {SCREEN_W/2,HORIZON_Y}, pixel_end = {SCREEN_W/2+1,HORIZON_Y};
    const uint8_t *near, *end;
    if (!path_is_straight()) return;
    path_edges(scroll+FAR,&ul,&ur);
    a.x = SCREEN_W/2+ul*(SCREEN_W/2)/FAR;
    b.x = SCREEN_W/2+ur*(SCREEN_W/2)/FAR;
    a.y = b.y = HORIZON_Y+CAM_H*(SCREEN_W/2)/FAR;
    near = haze_at(haze_deck,FAR);
    end = road_horizon_color(style);
    /* At subpixel width the continuation becomes one native pixel, rather
     * than a zero-area tip that the rasterizer cannot cover. */
    poly_g4(&a,&b,&vp,&pixel_end,OT_LENGTH-3,
            near[0],near[1],near[2],near[0],near[1],near[2],
            end[0],end[1],end[2],end[0],end[1],end[2],BLEND_NONE);
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

    /* Compiled meshes already have their visibility lists. Their near
     * remainder cannot use the deep-sheet occlusion mask. */
    if(!compiled_walls()) {
        for (i=0;i<n-1;i++) cover_deck(&ring[i],&ring[i+1]);
        deck_cover_ready=1;
    }
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
    PROF_LONG(PF_R_FAR, far_tier(&ring[n - 1], scroll));
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

/* Keep the mass cheap. Cross-sections are shared by both walls and windows;
 * only the distant step grows. Curves/banks still come from the real path. */
typedef struct { int x,y,z; } CityPoint;
typedef struct { CityPoint p[4]; } CityRing;
static void city_ring(int s,int outside,int height,CityRing *a) {
    int l,r;
    path_edges(s,&l,&r);
    path_cam_lr(s,l-outside,r+outside,0,&a->p[0].x,&a->p[0].y,&a->p[0].z,
                &a->p[2].x,&a->p[2].y,&a->p[2].z);
    path_cam_lr(s,l-outside,r+outside,height,&a->p[1].x,&a->p[1].y,&a->p[1].z,
                &a->p[3].x,&a->p[3].y,&a->p[3].z);
}
static CityPoint city_lerp(CityPoint a,CityPoint b,int t) {
    CityPoint p={lerp4096(a.x,b.x,t),lerp4096(a.y,b.y,t),lerp4096(a.z,b.z,t)};
    return p;
}
static unsigned city_hash(unsigned n) {
    n^=n>>16; n*=0x7feb352du; n^=n>>15; n*=0x846ca68bu; return n^(n>>16);
}
static void city_windows(const CityRing *a,const CityRing *b,int side,int s,int next,int scroll,int bucket) {
    int col,row;
    for(col=s;col<next;col+=128) {
        int ahead=col-scroll;
        /* Keep stable world-space samples, with fewer columns/floors once
         * many windows share a pixel. No additional path sampling. */
        int stride=ahead<3600?128:ahead<8000?256:512;
        int row_step=ahead<8000?1:2;
        if(col%stride) continue;
        int t=(col+64-s)*4096/(next-s);
        CityPoint foot=city_lerp(a->p[side],b->p[side],t);
        CityPoint top=city_lerp(a->p[side+1],b->p[side+1],t);
        for(row=0;row<16;row+=row_step) {
            unsigned h=city_hash((unsigned)(col/128)*97u+(unsigned)row*7919u+(unsigned)side*1013u);
            CityPoint p; XY c,q[4]; int size,r,g,bl,k;
            if(h%10>=3) continue;
            p=city_lerp(foot,top,192+row*240);
            if(p.z<CLIP || p.z>VIEW_Z) continue;
            /* Reject subpixel/offscreen lights before GTE work. Window lights
             * are small square impostors, never stretched affine textures. */
            size=clamp(3200/p.z,1,5);
            if(p.x*160 < (-164)*p.z || p.x*160 > 164*p.z ||
               (CAM_H-p.y)*160 < -76*p.z || (CAM_H-p.y)*160 > 172*p.z) continue;
            project(p.x,p.y,p.z,&c);
            q[0]=(XY){c.x-size/2,c.y-size/2}; q[1]=(XY){q[0].x+size,q[0].y};
            q[2]=(XY){q[0].x,q[0].y+size}; q[3]=(XY){q[1].x,q[2].y};
            k=p.z<3600 ? clamp(256-p.z*192/3600,64,256) : 64*6000/(p.z+2400);
            r=((h&16)?216:64)*k/256; g=((h&16)?168:184)*k/256; bl=((h&16)?72:216)*k/256;
            if(p.z>=2400) {
                /* Emission fades into the hazed wall, not below its brightness. */
                int wr=12,wg=16,wb=28;
                haze_air(&wr,&wg,&wb,p.z);
                int ambient=clamp((p.z-2400)*256/1200,0,256);
                r+=wr*ambient/256;g+=wg*ambient/256;bl+=wb*ambient/256;
            }
            world_asset("city_windows",side+(p.z<3600?0:p.z<8000?4:8),p.z<3600?"near":p.z<8000?"mid":"far");
            if(size==1) point_f1(c,bucket,r,g,bl);
            else poly_f4v(q,bucket,r,g,bl,BLEND_NONE);
        }
    }
}
/* Two-pixel guard band avoids a clear scanline from fixed-point rounding. */
static int city_plane(CityPoint p,int plane) {
    switch(plane) {
    case 0:return p.z-CLIP;
    case 1:return 80*p.x+81*p.z;
    case 2:return 81*p.z-80*p.x;
    case 3:return 37*p.z-80*(p.y-CAM_H);
    default:return 17*p.z+16*(p.y-CAM_H);
    }
}
/* Clip the enclosure before projection. Large near walls must not be dropped
 * by the GPU's polygon-size limits: they are the city's opaque shell. */
static void city_shell(CityPoint p,CityPoint q,CityPoint r,CityPoint t,int bucket,int tint) {
    CityPoint a[12]={p,q,t,r},b[12];
    XY xy[12]; int colors[12][3];
    int n=4,plane,i;
    for(plane=0;plane<5 && n;plane++) {
        int count=0;
        CityPoint prev=a[n-1]; int dp=city_plane(prev,plane);
        for(i=0;i<n;i++) {
            CityPoint cur=a[i]; int dc=city_plane(cur,plane);
            if((dp<0)!=(dc<0)) {
                int k=(int)((int64_t)dp*4096/(dp-dc));
                b[count++]=city_lerp(prev,cur,k);
            }
            if(dc>=0) b[count++]=cur;
            prev=cur;dp=dc;
        }
        n=count;
        for(i=0;i<n;i++) a[i]=b[i];
    }
    if(n<3) return;
    for(i=0;i<n;i++) {
        colors[i][0]=10+tint;colors[i][1]=14+tint;colors[i][2]=24+tint;
        haze_air(&colors[i][0],&colors[i][1],&colors[i][2],a[i].z);
        project(a[i].x,a[i].y,a[i].z,&xy[i]);
    }
    for(i=1;i<n-1;i++) {
        int *c=colors[0],*d=colors[i],*e=colors[i+1];
        poly_g4(&xy[0],&xy[i],&xy[i+1],&xy[i+1],bucket,
            c[0],c[1],c[2],d[0],d[1],d[2],e[0],e[1],e[2],e[0],e[1],e[2],BLEND_NONE);
    }
}
void road_city_canyon(int scroll,int outside,int height,const uint32_t *runs,int count) {
    CityRing a,b;
    int s=scroll/256*256, end=scroll+20000;
    city_ring(s,outside,height,&a);
    for(;s<end;) {
        int ahead=s-scroll, step=ahead<4096?256:ahead<8192?512:1024;
        int next=(s/step+1)*step,side;
        int band=s/256;
        if(band>=0 && band<count && runs[band]>(unsigned)next) {
            int limit=ahead<4096 ? 2048 : 8192;
            next=(s/limit+1)*limit;
            if(next>(int)runs[band]) next=runs[band];
        }
        if(next>end) next=end;
        city_ring(next,outside,height,&b);
        for(side=0;side<4;side+=2) {
            CityRing wa=a,wb=b;
            unsigned building=city_hash((unsigned)(s/2048)+(unsigned)side*101u);
            int scale=ahead<4096 ? 3276+(building%3)*820 : 4096;
            CityPoint p=a.p[side],q=city_lerp(p,a.p[side+1],scale);
            CityPoint r=b.p[side],t=city_lerp(r,b.p[side+1],scale);
            int sort=(r.z+t.z)/2, bucket=view_bucket(sort);
            if(sort<CLIP || sheet_hidden(p.x,p.y,p.z,q.x,q.y,q.z,r.x,r.y,r.z,t.x,t.y,t.z)) continue;
            world_asset("shell_city",side,"shaded_shell");
            wa.p[side+1]=q;wb.p[side+1]=t;
            /* In the same bucket, submit lights first so the opaque mass is
             * drawn behind them. Later foreground spans occlude both. */
            city_windows(&wa,&wb,side,s,next,scroll,bucket);
            world_asset("shell_city",side,"shaded_shell");
            city_shell(p,q,r,t,bucket,1+(building&3));
        }
        a=b; s=next;
        if(!scene_room()) break;
    }
}
