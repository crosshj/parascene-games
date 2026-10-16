#include "world.h"
#include "course.h"
#include "path.h"
#include "road.h"

/* Must reach FAR even with fine near steps, or crates appear past the deck. */
#define RING_MAX 48
#define FAR_BUCKET (OT_LENGTH - 2)
#define MTN_COLS 20
/* ~90° of panorama across the screen (4096 = full turn). */
#define MTN_FOV 1024
#define EDGE_W 16
/* Dividers past this are a pixel or two; they cost more than they read. */
#define DASH_END 2800
/* Far tier: deck and crate faces only, out to VIEW_Z. It walks up to
 * VIEW_S of track (short of a lap on a closed track) so a turn and the
 * road coming back stay in view. Spacing grows with depth and shrinks
 * where the road bends or changes grade; samples out of view are
 * skipped in long strides, so the cost stays under FAR_MAX samples. */
#define VIEW_Z 20000
#define VIEW_S 44000
#define FAR_MAX 48
#define FAR_DS_MIN STEP
#define FAR_DS_MAX (STEP * 3)
#define FAR_DS_SKIP (STEP * 4)
#define FAR_YAW 192
#define FAR_PITCH 48

typedef struct {
    XY l, r;
    int lz, rz;
    int s;
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

void road_backdrop(const RoadStyle *st) {
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
 * Split laterally until each piece fits, using poly_f4's 480px edge limit. */
static void emit_piece(XY al, XY ar, XY bl, XY br, int bucket, int r, int g, int bcol, int depth) {
    int near = edge_len(al, ar);
    int far = edge_len(bl, br);
    int span = near > far ? near : far;
    if (span > 400 && depth < 3) {
        XY am = lerp_xy(al, ar, 1, 2);
        XY bm = lerp_xy(bl, br, 1, 2);
        emit_piece(al, am, bl, bm, bucket, r, g, bcol, depth + 1);
        emit_piece(am, ar, bm, br, bucket, r, g, bcol, depth + 1);
        return;
    }
    poly_f4(&al, &ar, &bl, &br, bucket, r, g, bcol, BLEND_NONE);
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

static void strip(const RoadSample *a, const RoadSample *b) {
    int sort, bucket;
    int r = style->road[0], g = style->road[1], bcol = style->road[2];
    if (a->lz < 8 || a->rz < 8 || b->lz < 8 || b->rz < 8)
        return;
    sort = (b->lz + b->rz) / 2;
    if (sort < CLIP)
        sort = CLIP;
    if (sort >= FAR)
        return;
    /* Nudge near deck back a hair so crate sides win at the screen bottom. */
    if (sort < 500)
        sort += 48;
    haze_road(&r, &g, &bcol, sort);
    bucket = sort >> OT_SHIFT;
    emit_piece(a->l, a->r, b->l, b->r, bucket, r, g, bcol, 0);
}

/* Outset EDGE_W past a road edge in screen space (u is linear across the deck). */
static XY curb_out(XY edge, XY other, int half) {
    XY o;
    int span = half * 2;
    o.x = (int16_t)(edge.x + (edge.x - other.x) * EDGE_W / span);
    o.y = (int16_t)(edge.y + (edge.y - other.y) * EDGE_W / span);
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

static void strip_edges(const RoadSample *a, const RoadSample *b, int half) {
    int er, eg, eb, sort, bucket;
    XY alo, aro, blo, bro;
    if (half < EDGE_W * 2)
        return;
    if (a->lz < 8 || a->rz < 8 || b->lz < 8 || b->rz < 8)
        return;
    sort = (b->lz + b->rz) / 2;
    if (sort < CLIP)
        sort = CLIP;
    if (sort >= style->edge_end)
        return;
    curb_color(a->s, sort, &er, &eg, &eb);
    bucket = sort >> OT_SHIFT;
    alo = curb_out(a->l, a->r, half);
    blo = curb_out(b->l, b->r, half);
    aro = curb_out(a->r, a->l, half);
    bro = curb_out(b->r, b->l, half);
    poly_f4(&alo, &a->l, &blo, &b->l, bucket, er, eg, eb, BLEND_NONE);
    poly_f4(&a->r, &aro, &b->r, &bro, bucket, er, eg, eb, BLEND_NONE);
}

static int sample_at(RoadSample *out, int s) {
    const PathSeg *seg = path_at(s);
    int half = seg->width / 2;
    int lx, ly, lz, rx, ry, rz;
    path_cam_lr(s, half, 0, &lx, &ly, &lz, &rx, &ry, &rz);
    project(lx, ly, lz, &out->l);
    project(rx, ry, rz, &out->r);
    out->lz = lz;
    out->rz = rz;
    out->s = s;
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
    path_cam(s, u0, v1, &x0, &y0, &z0);
    path_cam(s, u1, v1, &x1, &y1, &z1);
    path_cam(s, u0, v0, &x2, &y2, &z2);
    path_cam(s, u1, v0, &x3, &y3, &z3);
    if (z0 < 8 || z1 < 8 || z2 < 8 || z3 < 8)
        return;
    sortz = (z0 + z1) / 2;
    if (sortz < CLIP || sortz >= FAR)
        return;
    haze_air(&r, &g, &b, sortz);
    quad(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, sortz, r, g, b);
}

void road_start_gantry(int scroll, const uint8_t post[3], const uint8_t beam[3]) {
    int lap = path_lap();
    int edge = ROAD_HALF + 8;
    int s;
    if (!lap)
        return;
    s = (scroll / lap) * lap;
    if (s < scroll + CLIP)
        s += lap;
    if (s >= scroll + FAR)
        return;
    road_panel(s, -edge - 36, -edge, 0, 230, post[0], post[1], post[2]);
    road_panel(s, edge, edge + 36, 0, 230, post[0], post[1], post[2]);
    road_panel(s, -edge, edge, 200, 228, beam[0], beam[1], beam[2]);
}

/* Path-space dashes so lanes stay correct through bends and over crests. */
static void dash(int s0, int s1, int u) {
    int x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3;
    int sort, rr, gg, bb;
    XY p0, p1, p2, p3;
    path_cam(s0, u - 6, 3, &x0, &y0, &z0);
    path_cam(s0, u + 6, 3, &x1, &y1, &z1);
    path_cam(s1, u + 6, 3, &x2, &y2, &z2);
    path_cam(s1, u - 6, 3, &x3, &y3, &z3);
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

/* Dividers sit between lane centers, LANE apart and centered on u = 0. */
static void dashes(const RoadSample *a, int s1) {
    const PathSeg *seg = path_at(a->s);
    int s0 = a->s;
    int n = seg->lanes;
    int gap, i;
    if (n < 2 || !seg->deck || ((s0 / 96) & 1) == 0)
        return;
    if ((a->lz + a->rz) / 2 > DASH_END)
        return;
    gap = (s1 - s0) / 8;
    if (gap < 4)
        gap = 4;
    if (s1 - s0 <= gap * 2)
        return;
    for (i = 0; i < n - 1; i++)
        dash(s0 + gap, s1 - gap, i * LANE - (n - 2) * LANE / 2);
}

/* Past edge_end the curb becomes a one-pixel line on each edge. It starts as
 * the near curb and eases to a hint of curb color over the asphalt by FAR.
 * Link before the deck so it draws over the deck in the same bucket. */
static void edge_lines(const RoadSample *a, const RoadSample *b, int sort) {
    int r = (style->edge[0] + road_far[0] * 3) / 4;
    int g = (style->edge[1] + road_far[1] * 3) / 4;
    int bl = (style->edge[2] + road_far[2] * 3) / 4;
    int k = clamp((FAR - sort) * 256 / (FAR - style->edge_end), 0, 256);
    haze_far(&r, &g, &bl, sort);
    if (k) {
        int cr, cg, cb;
        curb_color(a->s, sort, &cr, &cg, &cb);
        r += (cr - r) * k / 256;
        g += (cg - g) * k / 256;
        bl += (cb - bl) * k / 256;
    }
    line_f2(a->l, b->l, view_bucket(sort), r, g, bl);
    line_f2(a->r, b->r, view_bucket(sort), r, g, bl);
}

/* Shaded from its near edge to its far edge, so long far pieces read as
 * receding road instead of flat blocks. */
static void far_strip(const RoadSample *a, const RoadSample *b) {
    int za, zb, sort, r0, g0, b0, r1, g1, b1;
    if (a->lz < 8 || a->rz < 8 || b->lz < 8 || b->rz < 8)
        return;
    za = (a->lz + a->rz) / 2;
    zb = (b->lz + b->rz) / 2;
    sort = za > zb ? za : zb;
    if (sort < FAR) {
        edge_lines(a, b, zb);
        strip(a, b);
        return;
    }
    if (za >= VIEW_Z && zb >= VIEW_Z)
        return;
    edge_lines(a, b, sort);
    r0 = r1 = style->road[0];
    g0 = g1 = style->road[1];
    b0 = b1 = style->road[2];
    haze_far(&r0, &g0, &b0, za);
    haze_far(&r1, &g1, &b1, zb);
    poly_g4(&a->l, &a->r, &b->l, &b->r, view_bucket(sort),
            r0, g0, b0, r0, g0, b0, r1, g1, b1, r1, g1, b1, BLEND_NONE);
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
static int too_bent(int s0, int s1) {
    const PathSeg *a = path_at(s0);
    const PathSeg *b = path_at(s1);
    return ang_abs(b->yaw - a->yaw) > FAR_YAW ||
           ang_abs(b->pitch - a->pitch) > FAR_PITCH ||
           ang_abs(b->roll - a->roll) > FAR_PITCH;
}

/* Next far sample: by depth while in view, halved through bends and
 * grade changes, and a long stride while out of view. */
static int far_step(int s, const RoadSample *p, int cz) {
    int ds;
    if (!on_screen(p, cz))
        return FAR_DS_SKIP;
    ds = clamp(cz / 8, FAR_DS_MIN, FAR_DS_MAX);
    while (ds > FAR_DS_MIN && too_bent(s, s + ds))
        ds /= 2;
    return ds;
}

/* A far crate is its front face: a few pixels, one quad. */
static void far_crate(int s, int lane_i) {
    const uint8_t *c = style->obstacle->front;
    int u = lane_i * LANE, w = 64, s0 = s - OBST_D / 2;
    int x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, r, g, b;
    XY p0, p1, p2, p3;
    path_cam(s, u, 0, &x0, &y0, &z0);
    if (z0 < FAR) {
        obstacle_box_shade(s, lane_i, style->obstacle, haze_road);
        return;
    }
    if (z0 >= VIEW_Z)
        return;
    path_cam(s0, u - w, OBST_H, &x0, &y0, &z0);
    path_cam(s0, u + w, OBST_H, &x1, &y1, &z1);
    path_cam(s0, u - w, 0, &x2, &y2, &z2);
    path_cam(s0, u + w, 0, &x3, &y3, &z3);
    if (z0 < 8 || z1 < 8 || z2 < 8 || z3 < 8)
        return;
    project(x0, y0, z0, &p0);
    project(x1, y1, z1, &p1);
    project(x2, y2, z2, &p2);
    project(x3, y3, z3, &p3);
    r = c[0];
    g = c[1];
    b = c[2];
    haze_far(&r, &g, &b, z2);
    poly_f4(&p0, &p1, &p2, &p3, view_bucket(z2) - 1, r, g, b, BLEND_NONE);
}

/* Beyond the near ring: coarse deck out to VIEW_Z, and its crates. */
static void far_tier(const RoadSample *seam, int scroll) {
    RoadSample ring[FAR_MAX + 1];
    int lap = path_lap();
    int s_end = scroll + VIEW_S;
    int s = seam->s;
    int n = 1, i, band, band0, band1;
    if (lap && s_end > scroll + lap - STEP)
        s_end = scroll + lap - STEP;
    ring[0] = *seam;
    while (n <= FAR_MAX && s < s_end) {
        const RoadSample *p = &ring[n - 1];
        s += far_step(p->s, p, (p->lz + p->rz) / 2);
        if (s > s_end)
            s = s_end;
        sample_at(&ring[n], s);
        n++;
    }
    for (i = n - 2; i >= 0; i--) {
        if (path_at(ring[i].s)->deck)
            far_strip(&ring[i], &ring[i + 1]);
    }
    if (!style->obstacle)
        return;
    band0 = (seam->s + STEP / 2) / STEP;
    band1 = ring[n - 1].s / STEP;
    for (band = band0; band <= band1; band++) {
        int os = band * STEP + STEP / 2;
        if (os <= seam->s || os > ring[n - 1].s || !has_obstacle(band))
            continue;
        far_crate(os, obstacle_lane(band));
    }
}

void road_draw(const RoadStyle *st, const LevelFrame *f) {
    RoadSample ring[RING_MAX];
    int scroll = f->scroll;
    int s_end = scroll + FAR;
    int s = scroll + 32;
    int n = 0;
    int i, band, band0, band1, road_s0, road_s1;

    style = st;
    road_time = f->time;
    through_air(st->light, st->road_far, st->light->density, road_far);

    while (n < RING_MAX && s < s_end) {
        int cz = sample_at(&ring[n], s);
        int remain_n, remain_s, ds;
        if (cz < 8 || cz >= FAR) {
            s += 32;
            continue;
        }
        n++;
        remain_n = RING_MAX - n;
        remain_s = s_end - s;
        if (remain_n <= 0)
            break;
        /* Prefer fine steps near the camera, but always leave enough slots
         * to reach FAR so crates never sit past the deck. */
        ds = cz / 8;
        if (ds * remain_n < remain_s)
            ds = (remain_s + remain_n - 1) / remain_n;
        s += clamp(ds, 28, STEP);
    }

    for (i = n - 2; i >= 0; i--) {
        const PathSeg *seg = path_at(ring[i].s);
        int zb = (ring[i + 1].lz + ring[i + 1].rz) / 2;
        if (!seg->deck)
            continue;
        if (zb >= st->edge_end) {
            edge_lines(&ring[i], &ring[i + 1], zb);
            strip(&ring[i], &ring[i + 1]);
        } else {
            strip(&ring[i], &ring[i + 1]);
            strip_edges(&ring[i], &ring[i + 1], seg->width / 2);
        }
    }

    for (i = 0; i < n - 1; i++)
        dashes(&ring[i], ring[i + 1].s);

    if (n < 2)
        return;
    far_tier(&ring[n - 1], scroll);

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
        obstacle_box_shade(os, obstacle_lane(band), st->obstacle, haze_road);
    }
}
