/* FLAT OVAL: the viewer's closed lap, with nothing around the road.
 * The camera follows the yaw, so this is the world that exercises a turn. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "level.h"

/* Must reach FAR even with fine near steps, or crates appear past the deck. */
#define RING_MAX 48
#define FAR_BUCKET (OT_LENGTH - 2)
#define MTN_COLS 20
/* ~90° of panorama across the screen (4096 = full turn). */
#define MTN_FOV 1024

/* Same orange crate look as City Run; positions come from track catalog data. */
static const ObstacleStyle k_obstacle = {
    {110, 40, 16}, {150, 52, 20}, {186, 72, 28}, {255, 196, 96}, {232, 108, 36}
};

/* bend.flat cycle is 96 bands; start/repeat piece "north" is at s = 0, lap, … */
#define OVAL_LAP (96 * STEP)

typedef struct {
    XY l, r;
    int lz, rz;
    int s;
} RoadSample;

static void oval_clear(const LevelFrame *f, int *r, int *g, int *b) {
    (void)f;
    *r = 78;
    *g = 112;
    *b = 148;
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

static void draw_mountains(void) {
    int yaw = path_yaw();
    int base = HORIZON_Y + 8;
    int i;
    int h0[MTN_COLS + 1];

    /* Soft haze under the ridge — almost sky, grounds the silhouette. */
    gradient(0, base - 4, SCREEN_W, 36, FAR_BUCKET,
             74, 106, 140, 78, 112, 148);

    for (i = 0; i <= MTN_COLS; i++) {
        int ang = (yaw - MTN_FOV / 2 + MTN_FOV * i / MTN_COLS) & 4095;
        h0[i] = mountain_h(ang);
    }
    for (i = 0; i < MTN_COLS; i++) {
        XY p0, p1, p2, p3;
        int x0 = i * SCREEN_W / MTN_COLS;
        int x1 = (i + 1) * SCREEN_W / MTN_COLS;
        int y0 = base - h0[i];
        int y1 = base - h0[i + 1];
        p0.x = (int16_t)x0;
        p0.y = (int16_t)y0;
        p1.x = (int16_t)x1;
        p1.y = (int16_t)y1;
        p2.x = (int16_t)x0;
        p2.y = (int16_t)(base + 18);
        p3.x = (int16_t)x1;
        p3.y = (int16_t)(base + 18);
        /* Far ridge: a touch cooler/darker than clear color. */
        poly_f4(&p0, &p1, &p2, &p3, FAR_BUCKET, 66, 96, 128, BLEND_NONE);
    }
    /* Nearer, lower wash — same family, barely separated. */
    for (i = 0; i < MTN_COLS; i++) {
        XY p0, p1, p2, p3;
        int x0 = i * SCREEN_W / MTN_COLS;
        int x1 = (i + 1) * SCREEN_W / MTN_COLS;
        int y0 = base - h0[i] * 2 / 3 - 4;
        int y1 = base - h0[i + 1] * 2 / 3 - 4;
        p0.x = (int16_t)x0;
        p0.y = (int16_t)y0;
        p1.x = (int16_t)x1;
        p1.y = (int16_t)y1;
        p2.x = (int16_t)x0;
        p2.y = (int16_t)(base + 10);
        p3.x = (int16_t)x1;
        p3.y = (int16_t)(base + 10);
        poly_f4(&p0, &p1, &p2, &p3, FAR_BUCKET, 72, 104, 136, BLEND_NONE);
    }
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

/* Near asphalt → sky haze with distance (same idea as the mountains).
 * dim() pulls toward black; that fights aerial perspective here. */
#define SKY_R 78
#define SKY_G 112
#define SKY_B 148
#define ROAD_R 100
#define ROAD_G 96
#define ROAD_B 88
/* Distant asphalt: a touch darker/cooler than the near deck. */
#define ROAD_FAR_R 62
#define ROAD_FAR_G 66
#define ROAD_FAR_B 74
/* Teal edge glow outside ±half (Lumenwild pulse, cooler hue). Near only. */
#define EDGE_W 16
#define EDGE_END 1900
#define EDGE_R 110
#define EDGE_G 245
#define EDGE_B 230

static int oval_time;

static void haze_toward_sky(int *r, int *g, int *b, int z) {
    int t = (z - CLIP) * 256 / (FAR - CLIP);
    if (t < 0)
        t = 0;
    if (t > 220)
        t = 220;
    *r = *r + (SKY_R - *r) * t / 256;
    *g = *g + (SKY_G - *g) * t / 256;
    *b = *b + (SKY_B - *b) * t / 256;
}

static void haze_road(int *r, int *g, int *b, int z) {
    int t = (z - CLIP) * 256 / (FAR - CLIP);
    if (t < 0)
        t = 0;
    if (t > 210)
        t = 210;
    *r = *r + (ROAD_FAR_R - *r) * t / 256;
    *g = *g + (ROAD_FAR_G - *g) * t / 256;
    *b = *b + (ROAD_FAR_B - *b) * t / 256;
}

static void strip(const RoadSample *a, const RoadSample *b, int r, int g, int bcol) {
    int sort, bucket;
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

/* Outset EDGE_W past a road edge in screen space (flat deck: u is linear in x). */
static XY curb_out(XY edge, XY other, int half) {
    XY o;
    int span = half * 2;
    o.x = (int16_t)(edge.x + (edge.x - other.x) * EDGE_W / span);
    o.y = (int16_t)(edge.y + (edge.y - other.y) * EDGE_W / span);
    return o;
}

static void strip_edges(const RoadSample *a, const RoadSample *b, int half) {
    int er, eg, eb, sort, bucket, band, pulse, e;
    XY alo, aro, blo, bro;
    if (half < EDGE_W * 2)
        return;
    if (a->lz < 8 || a->rz < 8 || b->lz < 8 || b->rz < 8)
        return;
    sort = (b->lz + b->rz) / 2;
    if (sort < CLIP)
        sort = CLIP;
    if (sort >= EDGE_END)
        return;
    /* Same breath as Lumenwild's path rim: bright band, soft along-track pulse. */
    band = a->s / STEP;
    pulse = tri_wave(band * 48 - oval_time * 16);
    e = 170 + pulse * 80 / 4096;
    er = (EDGE_R * e) >> 8;
    eg = (EDGE_G * e) >> 8;
    eb = (EDGE_B * e) >> 8;
    haze_toward_sky(&er, &eg, &eb, sort);
    bucket = sort >> OT_SHIFT;
    alo = curb_out(a->l, a->r, half);
    blo = curb_out(b->l, b->r, half);
    aro = curb_out(a->r, a->l, half);
    bro = curb_out(b->r, b->l, half);
    poly_f4(&alo, &a->l, &blo, &b->l, bucket, er, eg, eb, BLEND_NONE);
    poly_f4(&a->r, &aro, &b->r, &bro, bucket, er, eg, eb, BLEND_NONE);
}

static void shade_road(int midz, int *rr, int *gg, int *bb) {
    (void)midz;
    *rr = ROAD_R;
    *gg = ROAD_G;
    *bb = ROAD_B;
}

/* Local deck under a crate so it never floats if the main ring skips a span. */
static void deck_under(int s) {
    const PathSeg *seg = path_at(s);
    int half = seg->width / 2;
    int d = OBST_D + 24;
    RoadSample a, b;
    int lx, ly, lz, rx, ry, rz;
    int rr, gg, bb, midz;
    path_cam_lr(s - d / 2, half, 0, &lx, &ly, &lz, &rx, &ry, &rz);
    if (lz < 8 || rz < 8)
        return;
    project(lx, ly, lz, &a.l);
    project(rx, ry, rz, &a.r);
    a.lz = lz;
    a.rz = rz;
    a.s = s - d / 2;
    path_cam_lr(s + d / 2, half, 0, &lx, &ly, &lz, &rx, &ry, &rz);
    if (lz < 8 || rz < 8)
        return;
    project(lx, ly, lz, &b.l);
    project(rx, ry, rz, &b.r);
    b.lz = lz;
    b.rz = rz;
    b.s = s + d / 2;
    midz = (a.lz + a.rz + b.lz + b.rz) / 4;
    shade_road(midz, &rr, &gg, &bb);
    strip(&a, &b, rr, gg, bb);
}

/* City-style overhead bar, built in path space at one s. */
static void gantry_panel(int s, int u0, int u1, int v0, int v1,
                         int r, int g, int b) {
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
    haze_toward_sky(&r, &g, &b, sortz);
    quad(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, sortz, r, g, b);
}

static void start_gantry(int s) {
    int edge = ROAD_HALF + 8;
    gantry_panel(s, -edge - 36, -edge, 0, 230, 148, 160, 176);
    gantry_panel(s, edge, edge + 36, 0, 230, 148, 160, 176);
    gantry_panel(s, -edge, edge, 200, 228, 120, 140, 160);
}

/* Only the north start/finish — one per lap, never elsewhere on the oval. */
static void draw_start_gantry(int scroll) {
    int s = (scroll / OVAL_LAP) * OVAL_LAP;
    if (s < scroll + CLIP)
        s += OVAL_LAP;
    if (s >= scroll + CLIP && s < scroll + FAR)
        start_gantry(s);
}

/* Path-space dashes so lanes stay correct through bends. */
static void dash(int s0, int s1, int u) {
    int x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3;
    int sort;
    path_cam(s0, u - 6, 3, &x0, &y0, &z0);
    path_cam(s0, u + 6, 3, &x1, &y1, &z1);
    path_cam(s1, u + 6, 3, &x2, &y2, &z2);
    path_cam(s1, u - 6, 3, &x3, &y3, &z3);
    if (z0 < 8 || z1 < 8 || z2 < 8 || z3 < 8)
        return;
    if (z0 < 40 || z1 < 40)
        return;
    sort = (z0 + z1) / 2;
    if (sort < 40)
        sort = 40;
    if (sort >= FAR)
        return;
    {
        int rr = 214, gg = 198, bb = 140;
        XY p0, p1, p2, p3;
        haze_toward_sky(&rr, &gg, &bb, sort);
        project(x0, y0, z0, &p0);
        project(x1, y1, z1, &p1);
        project(x3, y3, z3, &p2);
        project(x2, y2, z2, &p3);
        poly_f4(&p0, &p1, &p2, &p3, sort >> OT_SHIFT, rr, gg, bb, BLEND_NONE);
    }
}

static int push_sample(RoadSample *ring, int n, int s) {
    const PathSeg *seg = path_at(s);
    int half = seg->width / 2;
    int lx, ly, lz, rx, ry, rz, cz;
    path_cam_lr(s, half, 0, &lx, &ly, &lz, &rx, &ry, &rz);
    cz = (lz + rz) / 2;
    if (cz < 8 || cz >= FAR)
        return -1;
    project(lx, ly, lz, &ring[n].l);
    project(rx, ry, rz, &ring[n].r);
    ring[n].lz = lz;
    ring[n].rz = rz;
    ring[n].s = s;
    return cz;
}

static void oval_draw(const LevelFrame *f) {
    RoadSample ring[RING_MAX];
    int scroll = f->scroll;
    int s_end = scroll + FAR;
    int s = scroll + 32;
    int n = 0;
    int i;
    int road_s0, road_s1;

    oval_time = f->time;
    draw_mountains();

    while (n < RING_MAX && s < s_end) {
        int cz = push_sample(ring, n, s);
        int remain_n, remain_s, ds;
        if (cz < 0) {
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
        if (ds < 28)
            ds = 28;
        if (ds > STEP)
            ds = STEP;
        if (remain_n > 0 && ds * remain_n < remain_s)
            ds = (remain_s + remain_n - 1) / remain_n;
        if (ds < 28)
            ds = 28;
        if (ds > STEP)
            ds = STEP;
        s += ds;
    }

    for (i = n - 2; i >= 0; i--) {
        int midz = (ring[i].lz + ring[i].rz + ring[i + 1].lz + ring[i + 1].rz) / 4;
        int rr, gg, bb;
        shade_road(midz, &rr, &gg, &bb);
        strip(&ring[i], &ring[i + 1], rr, gg, bb);
        if (midz < EDGE_END)
            strip_edges(&ring[i], &ring[i + 1], path_at(ring[i].s)->width / 2);
    }

    for (i = 0; i < n - 1; i++) {
        int s0 = ring[i].s;
        int s1 = ring[i + 1].s;
        int gap;
        if (((s0 / 96) & 1) == 0)
            continue;
        gap = (s1 - s0) / 8;
        if (gap < 4)
            gap = 4;
        if (s1 - s0 <= gap * 2)
            continue;
        dash(s0 + gap, s1 - gap, -LANE / 2);
        dash(s0 + gap, s1 - gap, LANE / 2);
    }

    draw_start_gantry(scroll);

    /* Only crates that sit on the meshed deck; each gets a local pad too. */
    if (n < 2)
        return;
    road_s0 = ring[0].s;
    road_s1 = ring[n - 1].s;
    {
        int band0 = road_s0 / STEP;
        int band1 = road_s1 / STEP;
        int band;
        for (band = band1; band >= band0; band--) {
            int os;
            if (band < 0 || !has_obstacle(band))
                continue;
            os = band * STEP + STEP / 2;
            if (os < road_s0 || os > road_s1)
                continue;
            deck_under(os);
            obstacle_box_shade(os, obstacle_lane(band), &k_obstacle, haze_road);
        }
    }
}

const Level level_oval = {
    "FLAT OVAL",
    "bend.flat",
    oval_clear,
    oval_draw,
};
