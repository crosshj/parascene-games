#include <string.h>
#ifndef RUNNER_PREVIEW
/* The SDK's own isin and icos would clash with the table below. */
#define isin sdk_isin
#define icos sdk_icos
#include <psxgte.h>
#include <inline_c.h>
#undef isin
#undef icos
#endif
#include "path.h"
#include "course.h"
#include "track_data.h"
#include "profile.h"

static int path_scroll;
static const TrackDef *active = &track_table[0];

#define MAX_BRANCHES 4
#define BAND_MAX 1024
/* 1 where a fork follows its second road. */
static uint8_t route[MAX_BRANCHES];
/* The fork each band index belongs to, or -1. */
static int8_t band_branch[BAND_MAX];
/* Per band index: some route may jump into it, some road on it holds a crate. */
#define BAND_MAY_CUT 1
#define BAND_MAY_CRATE 2
static uint8_t band_flags[BAND_MAX];
/* Per band index: how many bands from it on no route can cut, up to the
 * period's end; 0 where one may. */
static uint8_t band_clear[BAND_MAX];
static const TrackDef *flagged;
/* Each segment's deck right and up vectors at its start, so a sample blends
 * two frames instead of building one from angles. */
#define SEG_MAX 1024
static int16_t seg_frame[SEG_MAX][6];
/* Per segment, the curbs that start in it (bits 0-1) or end with it (bits
 * 2-3) where the road runs on, on any route: they taper across it. */
static uint8_t seg_taper[SEG_MAX];
static int framed;
static int cut_at(int band);
/* Band of the last path_follow and path_begin, to catch a step onto the
 * next band; -1 after a jump in s. */
static int follow_band = -1;
static int cam_band = -1;
/* The camera's offset from the centerline, eased back to 0 after a jump. */
static int cam_u;
static int end_side;
/* The last band sample read, kept until the route or the track changes. */
static int cache_band = -1;
static const PathSeg *cache_a, *cache_b;
static int cache_x, cache_y, cache_z;

/* How far into its band the last sample read, 0..STEP. */
static int sample_local;

/* Camera frame for this path_begin. path_cam reuses it every call. */
static int cam_x, cam_y, cam_z;
static int cam_yaw;
static int cam_rx, cam_ry, cam_rz;
static int cam_ux, cam_uy, cam_uz;
static int cam_fx, cam_fy, cam_fz;

typedef char path_step_ok[(STEP == 256) ? 1 : -1];

#define CAM_PITCH_NUM 3
#define CAM_PITCH_DEN 4

/* Quarter-turn sine, 0..1024 in steps of 16. 4096 is one. */
static const int16_t SIN_Q[65] = {
    0, 101, 201, 301, 401, 501, 601, 700, 799, 897, 995, 1092, 1189, 1285,
    1380, 1474, 1567, 1660, 1751, 1842, 1931, 2019, 2106, 2191, 2276, 2359,
    2440, 2520, 2598, 2675, 2751, 2824, 2896, 2967, 3035, 3102, 3166, 3229,
    3290, 3349, 3406, 3461, 3513, 3564, 3612, 3659, 3703, 3745, 3784, 3822,
    3857, 3889, 3920, 3948, 3973, 3996, 4017, 4036, 4052, 4065, 4076, 4085,
    4091, 4095, 4096
};

static int qsin(int a) {
    int i, f;
    if (a <= 0)
        return 0;
    if (a >= 1024)
        return 4096;
    i = a >> 4;
    f = a & 15;
    return (SIN_Q[i] * (16 - f) + SIN_Q[i + 1] * f) >> 4;
}

static int isin(int a) {
    a &= 4095;
    if (a < 1024)
        return qsin(a);
    if (a < 2048)
        return qsin(2048 - a);
    if (a < 3072)
        return -qsin(a - 2048);
    return -qsin(4096 - a);
}

static int icos(int a) {
    return isin(a + 1024);
}

static int mul_q(int a, int b) {
    return (int)(((int64_t)a * (int64_t)b) / 4096);
}

/* mul_q for a unit vector times a 16-bit length: the product fits 32 bits. */
static int mul_s(int a, int b) {
    return a * b >> 12;
}

static int dot_q(int dx, int dy, int dz, int x, int y, int z) {
    return (int)(((int64_t)dx * x + (int64_t)dy * y + (int64_t)dz * z) / 4096);
}

/* (x, y, z) into the camera frame, without the camera's position. The
 * console runs it on the GTE, with the frame in the light matrix: nothing
 * else lights with it. A vector past 16 bits goes in halved until it fits. */
static void rotate(int x, int y, int z, int *ox, int *oy, int *oz) {
    int k = 0;
    while (iabs(x) > 32767 || iabs(y) > 32767 || iabs(z) > 32767) {
        x >>= 1;
        y >>= 1;
        z >>= 1;
        k++;
    }
#ifdef RUNNER_PREVIEW
    *ox = (int)(((int64_t)x * cam_rx + (int64_t)y * cam_ry + (int64_t)z * cam_rz) >> 12);
    *oy = (int)(((int64_t)x * cam_ux + (int64_t)y * cam_uy + (int64_t)z * cam_uz) >> 12);
    *oz = (int)(((int64_t)x * cam_fx + (int64_t)y * cam_fy + (int64_t)z * cam_fz) >> 12);
#else
    {
        /* V0 straight from registers, the light-matrix product straight
         * back: two nops let V0 settle, and reading MAC waits on the GTE. */
        int xy = (x & 0xFFFF) | (y << 16), a, b, c;
        __asm__ volatile(
            "mtc2 %3, $0\n"
            "mtc2 %4, $1\n"
            "nop\n"
            "nop\n"
            "cop2 0x04A6012\n"
            "mfc2 %0, $25\n"
            "mfc2 %1, $26\n"
            "mfc2 %2, $27\n"
            "nop\n"
            : "=r"(a), "=r"(b), "=r"(c)
            : "r"(xy), "r"(z));
        *ox = a;
        *oy = b;
        *oz = c;
    }
#endif
    *ox <<= k;
    *oy <<= k;
    *oz <<= k;
}

static void basis(int yaw, int pitch, int roll,
                  int *rx, int *ry, int *rz,
                  int *ux, int *uy, int *uz,
                  int *fx, int *fy, int *fz) {
    int cp = icos(pitch);
    int sp = isin(pitch);
    int sy = isin(yaw);
    int cy = icos(yaw);
    int cr = icos(roll);
    int sr = isin(roll);
    int rx0, ry0, rz0, ux0, uy0, uz0;
    *fx = mul_q(sy, cp);
    *fy = sp;
    *fz = mul_q(cy, cp);
    rx0 = cy;
    ry0 = 0;
    rz0 = -sy;
    ux0 = mul_q(*fy, rz0) - mul_q(*fz, ry0);
    uy0 = mul_q(*fz, rx0) - mul_q(*fx, rz0);
    uz0 = mul_q(*fx, ry0) - mul_q(*fy, rx0);
    *rx = mul_q(rx0, cr) - mul_q(ux0, sr);
    *ry = mul_q(ry0, cr) - mul_q(uy0, sr);
    *rz = mul_q(rz0, cr) - mul_q(uz0, sr);
    *ux = mul_q(rx0, sr) + mul_q(ux0, cr);
    *uy = mul_q(ry0, sr) + mul_q(uy0, cr);
    *uz = mul_q(rz0, sr) + mul_q(uz0, cr);
}

/* cycle 0 is a road that ends. Past the last segment, samples stay on it. */
static int road_ends(void) {
    return active->cycle <= 0;
}

static int band_index(int band) {
    if (road_ends()) {
        if (band >= active->count)
            return active->count - 1;
        if (band < 0)
            return 0;
        return band;
    }
    if (band < active->lead)
        return band;
    return active->lead + (band - active->lead) % active->cycle;
}

static int band_lap(int band) {
    if (road_ends() || band < active->lead)
        return 0;
    return (band - active->lead) / active->cycle;
}

static int ang_delta(int a, int b) {
    int d = (b - a) & 4095;
    if (d > 2048)
        d -= 4096;
    return d;
}

static int branch_of(int idx) {
    int k;
    if (active->count <= BAND_MAX)
        return idx >= 0 && idx < active->count ? band_branch[idx] : -1;
    for (k = 0; k < active->branches; k++) {
        if (idx >= active->branch[k].band0 && idx < active->branch[k].band1)
            return k;
    }
    return -1;
}

/* The segment at a band index on the route. */
static const PathSeg *seg_at(int idx) {
    int k = branch_of(idx);
    if (k >= 0 && route[k])
        return &active->seg[active->branch[k].alt + idx - active->branch[k].band0];
    return &active->seg[idx];
}

/* A curb this band's road has and the road before or after it lacks, with
 * no cut between: there the curb narrows to a point instead of stopping. */
static int taper_bits(int band, const PathSeg *a, const PathSeg *next) {
    int bits = 0;
    if (band > 0 && !cut_at(band))
        bits = a->curbs & ~seg_at(band_index(band - 1))->curbs;
    if (!cut_at(band + 1))
        bits |= (a->curbs & ~next->curbs) << 2;
    return bits;
}

/* The centerline point at s; leaves the band in the cache. */
static const PathSeg *sample_at_s(int s, int *x, int *y, int *z) {
    int band, local, lap, idx, nidx;
    const PathSeg *a;
    PROF_COUNT_SAMPLE();
    if (s < 0)
        s = 0;
    band = s / STEP;
    local = s - band * STEP;
    if (end_side && local == 0 && band > 0) {
        band--;
        local = STEP;
    }
    if (band != cache_band) {
        if (road_ends() && band >= active->count) {
            /* One more step of the last segment, repeated, so the deck
             * runs on instead of jumping back to the start. */
            const PathSeg *end = &active->seg[active->count - 1];
            int over = band - (active->count - 1);
            int dx = end->x1 - end->x;
            int dy = end->y1 - end->y;
            int dz = end->z1 - end->z;
            cache_a = end;
            cache_b = end;
            cache_x = end->x + (int)((long long)dx * over);
            cache_y = end->y + (int)((long long)dy * over);
            cache_z = end->z + (int)((long long)dz * over);
            cache_band = band;
        } else if (road_ends()) {
            lap = 0;
            idx = band;
            nidx = band + 1 < active->count ? band + 1 : band;
            cache_a = seg_at(idx);
            cache_b = seg_at(nidx);
            cache_x = cache_a->x;
            cache_y = cache_a->y;
            cache_z = cache_a->z;
            cache_band = band;
        } else if (band < active->lead) {
            lap = 0;
            idx = band;
            nidx = idx + 1;
            cache_a = seg_at(idx);
            cache_b = seg_at(nidx);
            cache_x = cache_a->x;
            cache_y = cache_a->y;
            cache_z = cache_a->z;
            cache_band = band;
        } else {
            lap = (band - active->lead) / active->cycle;
            idx = band - lap * active->cycle;
            nidx = idx + 1 == active->lead + active->cycle ? active->lead : idx + 1;
            cache_a = seg_at(idx);
            cache_b = seg_at(nidx);
            cache_x = cache_a->x + lap * active->rep_x;
            cache_y = cache_a->y + lap * active->rep_y;
            cache_z = cache_a->z + lap * active->rep_z;
            cache_band = band;
        }
    }
    a = cache_a;
    sample_local = local;
    *x = cache_x + (a->x1 - a->x) * local / STEP;
    *y = cache_y + (a->y1 - a->y) * local / STEP;
    *z = cache_z + (a->z1 - a->z) * local / STEP;
    return a;
}

static const PathSeg *sample(int s, int *x, int *y, int *z,
                             int *yaw, int *pitch, int *roll) {
    const PathSeg *a = sample_at_s(s, x, y, z);
    const PathSeg *b = cache_b;
    int local = sample_local;
    *yaw = a->yaw + ang_delta(a->yaw, b->yaw) * local / STEP;
    *pitch = a->pitch + ang_delta(a->pitch, b->pitch) * local / STEP;
    *roll = a->roll + ang_delta(a->roll, b->roll) * local / STEP;
    return a;
}

static void mark_taper(int band, int idx) {
    const PathSeg *a = seg_at(idx);
    int i = (int)(a - active->seg);
    if (i < SEG_MAX)
        seg_taper[i] |= (uint8_t)taper_bits(band, a, seg_at(band_index(band + 1)));
}

/* Every route at once: each fork on its main road, then each on its second. */
static void flag_bands(void) {
    int n = active->count < BAND_MAX ? active->count : BAND_MAX;
    int alt, idx;
    memset(band_flags, 0, sizeof band_flags);
    memset(seg_taper, 0, sizeof seg_taper);
    for (alt = 0; alt < 2; alt++) {
        memset(route, alt, sizeof route);
        cache_band = -1;
        for (idx = 0; idx < n; idx++) {
            int band = idx < active->lead ? idx : idx + active->cycle;
            if (seg_at(idx)->obstacle != OBSTACLE_NONE)
                band_flags[idx] |= BAND_MAY_CRATE;
            if (cut_at(band))
                band_flags[idx] |= BAND_MAY_CUT;
            mark_taper(band, idx);
            if (active->branches) {
                /* A join or fork edge: the road before on the other route. */
                int k = branch_of(idx), kp = branch_of(idx > 0 ? idx - 1 : 0);
                if (k != kp) {
                    if (kp >= 0)
                        route[kp] ^= 1;
                    else if (k >= 0)
                        route[k] ^= 1;
                    cache_band = -1;
                    if (cut_at(band))
                        band_flags[idx] |= BAND_MAY_CUT;
                    mark_taper(band, idx);
                    memset(route, alt, sizeof route);
                    cache_band = -1;
                }
            }
        }
    }
    {
        int last = active->lead + active->cycle - 1;
        for (idx = n - 1; idx >= 0; idx--) {
            if ((band_flags[idx] & BAND_MAY_CUT) || idx == active->lead)
                band_clear[idx] = 0;
            else if (idx == last || idx == n - 1)
                band_clear[idx] = 1;
            else
                band_clear[idx] = (uint8_t)(band_clear[idx + 1] < 255 ? band_clear[idx + 1] + 1 : 255);
        }
    }
    path_reset();
}

static void build_frames(void) {
    int n = active->count, k, i;
    for (k = 0; k < active->branches; k++) {
        int end = active->branch[k].alt + active->branch[k].band1 - active->branch[k].band0;
        if (end > n)
            n = end;
    }
    framed = n <= SEG_MAX;
    if (!framed)
        return;
    for (i = 0; i < n; i++) {
        const PathSeg *g = &active->seg[i];
        int rx, ry, rz, ux, uy, uz, fx, fy, fz;
        basis(g->yaw, g->pitch, g->roll, &rx, &ry, &rz, &ux, &uy, &uz, &fx, &fy, &fz);
        seg_frame[i][0] = (int16_t)rx;
        seg_frame[i][1] = (int16_t)ry;
        seg_frame[i][2] = (int16_t)rz;
        seg_frame[i][3] = (int16_t)ux;
        seg_frame[i][4] = (int16_t)uy;
        seg_frame[i][5] = (int16_t)uz;
    }
}

int path_open(const char *id) {
    int i;
    if (!id)
        return 0;
    for (i = 0; i < TRACK_TABLE_COUNT; i++) {
        if (strcmp(track_table[i].id, id) == 0) {
            if (active != &track_table[i] || flagged != active) {
                int k, j;
                active = &track_table[i];
                memset(band_branch, -1, sizeof band_branch);
                for (k = 0; k < active->branches; k++) {
                    for (j = active->branch[k].band0; j < active->branch[k].band1 && j < BAND_MAX; j++)
                        band_branch[j] = (int8_t)k;
                }
                path_reset();
                flag_bands();
                build_frames();
                flagged = active;
            }
            return 1;
        }
    }
    return 0;
}

void path_reset(void) {
    memset(route, 0, sizeof route);
    cache_band = -1;
    follow_band = -1;
    cam_band = -1;
    cam_u = 0;
}

/* How far the route's centerline steps sideways from the end of band - 1
 * to the start of band, along the new segment's right. */
static int jump_at(int band) {
    const PathSeg *a, *b;
    int la, lb, dx, dy, dz, rx, ry, rz, ux, uy, uz, fx, fy, fz;
    if (band <= 0)
        return 0;
    a = seg_at(band_index(band - 1));
    b = seg_at(band_index(band));
    la = band_lap(band - 1);
    lb = band_lap(band);
    dx = b->x - a->x1 + (lb - la) * active->rep_x;
    dy = b->y - a->y1 + (lb - la) * active->rep_y;
    dz = b->z - a->z1 + (lb - la) * active->rep_z;
    if (!dx && !dy && !dz)
        return 0;
    basis(b->yaw, b->pitch, b->roll, &rx, &ry, &rz, &ux, &uy, &uz, &fx, &fy, &fz);
    return dot_q(dx, dy, dz, rx, ry, rz);
}

void path_begin(int scroll) {
    int cyaw, cpitch, croll, band;
    path_scroll = scroll;
    band = scroll < 0 ? 0 : scroll / STEP;
    if (band == cam_band + 1)
        cam_u -= jump_at(band);
    else if (band != cam_band)
        cam_u = 0;
    cam_band = band;
    cam_u -= cam_u / 16 + (cam_u > 0) - (cam_u < 0);
    sample(path_scroll, &cam_x, &cam_y, &cam_z, &cyaw, &cpitch, &croll);
    cam_yaw = cyaw & 4095;
    if (cam_u) {
        cam_x += mul_q(icos(cyaw), cam_u);
        cam_z -= mul_q(isin(cyaw), cam_u);
    }
    /* The camera takes most of the grade, so a hill ahead still reads as one.
     * It never rolls: the horizon stays level and the bank shows in the deck. */
    cpitch = ang_delta(0, cpitch) * CAM_PITCH_NUM / CAM_PITCH_DEN;
    basis(cyaw, cpitch, 0,
          &cam_rx, &cam_ry, &cam_rz,
          &cam_ux, &cam_uy, &cam_uz,
          &cam_fx, &cam_fy, &cam_fz);
#ifndef RUNNER_PREVIEW
    {
        MATRIX m;
        m.m[0][0] = (int16_t)cam_rx; m.m[0][1] = (int16_t)cam_ry; m.m[0][2] = (int16_t)cam_rz;
        m.m[1][0] = (int16_t)cam_ux; m.m[1][1] = (int16_t)cam_uy; m.m[1][2] = (int16_t)cam_uz;
        m.m[2][0] = (int16_t)cam_fx; m.m[2][1] = (int16_t)cam_fy; m.m[2][2] = (int16_t)cam_fz;
        __asm__ volatile("" ::: "memory");
        gte_SetLightMatrix(&m);
    }
#endif
}

int path_yaw(void) {
    return cam_yaw;
}

int path_horizon(void) {
    int sy = isin(cam_yaw);
    int cy = icos(cam_yaw);
    int up = dot_q(sy, 0, cy, cam_ux, cam_uy, cam_uz);
    int fwd = dot_q(sy, 0, cy, cam_fx, cam_fy, cam_fz);
    if (fwd < 64)
        fwd = 64;
    return HORIZON_Y - up * (SCREEN_W / 2) / fwd;
}

/* The deck point at (px, py, pz) lifted v, relative to the camera, and the
 * deck's right vector, both still in world axes. Flat deck
 * (pitch=roll=0): right = (cy,0,-sy), up = (0,1,0). */
static void deck_from(int px, int py, int pz, int yaw, int pitch, int roll, int v,
                      int *dx, int *dy, int *dz, int *rx, int *ry, int *rz) {
    if (pitch == 0 && roll == 0) {
        *rx = icos(yaw);
        *ry = 0;
        *rz = -isin(yaw);
        py += v;
    } else {
        int ux, uy, uz, fx, fy, fz;
        basis(yaw, pitch, roll, rx, ry, rz, &ux, &uy, &uz, &fx, &fy, &fz);
        px += mul_q(ux, v);
        py += mul_q(uy, v);
        pz += mul_q(uz, v);
    }
    *dx = px - cam_x;
    *dy = py - cam_y;
    *dz = pz - cam_z;
}

/* deck_from, with the frame blended from the band's two segment frames. */
static const PathSeg *deck_at(int s, int v, int *dx, int *dy, int *dz,
                              int *rx, int *ry, int *rz) {
    int px, py, pz, yaw, pitch, roll, local;
    const PathSeg *a;
    const int16_t *fa, *fb;
    if (!framed) {
        a = sample(s, &px, &py, &pz, &yaw, &pitch, &roll);
        deck_from(px, py, pz, yaw, pitch, roll, v, dx, dy, dz, rx, ry, rz);
        return a;
    }
    a = sample_at_s(s, &px, &py, &pz);
    local = sample_local;
    fa = seg_frame[a - active->seg];
    fb = seg_frame[cache_b - active->seg];
    *rx = fa[0] + (fb[0] - fa[0]) * local / STEP;
    *ry = fa[1] + (fb[1] - fa[1]) * local / STEP;
    *rz = fa[2] + (fb[2] - fa[2]) * local / STEP;
    if (v) {
        px += (fa[3] + (fb[3] - fa[3]) * local / STEP) * v >> 12;
        py += (fa[4] + (fb[4] - fa[4]) * local / STEP) * v >> 12;
        pz += (fa[5] + (fb[5] - fa[5]) * local / STEP) * v >> 12;
    }
    *dx = px - cam_x;
    *dy = py - cam_y;
    *dz = pz - cam_z;
    return a;
}

static int lerp_u(int a, int b, int local);

const PathSeg *path_deck_lr(int s, int *ul, int *ur,
                            int *lx, int *ly, int *lz,
                            int *rx, int *ry, int *rz, int *kl, int *kr) {
    int dx, dy, dz, wx, wy, wz, cx, cy, cz, ax, ay, az;
    const PathSeg *a = deck_at(s, 0, &dx, &dy, &dz, &wx, &wy, &wz);
    int local = sample_local;
    *ul = lerp_u(a->el, a->el1, local);
    *ur = lerp_u(a->er, a->er1, local);
    rotate(dx, dy, dz, &cx, &cy, &cz);
    rotate(wx, wy, wz, &ax, &ay, &az);
    *lx = cx + mul_s(ax, *ul);
    *ly = cy + mul_s(ay, *ul);
    *lz = cz + mul_s(az, *ul);
    *rx = cx + mul_s(ax, *ur);
    *ry = cy + mul_s(ay, *ur);
    *rz = cz + mul_s(az, *ur);
    *kl = a->curbs & PATH_CURB_L ? 4096 : 0;
    *kr = a->curbs & PATH_CURB_R ? 4096 : 0;
    if (framed && seg_taper[a - active->seg]) {
        int t = seg_taper[a - active->seg];
        int up = local * (4096 / STEP), down = (STEP - local) * (4096 / STEP);
        if ((t & 1) && *kl > up) *kl = up;
        if ((t & 4) && *kl > down) *kl = down;
        if ((t & 2) && *kr > up) *kr = up;
        if ((t & 8) && *kr > down) *kr = down;
    }
    return local == STEP ? cache_b : a;
}

void path_cam(int s, int u, int v, int *x, int *y, int *z) {
    int dx, dy, dz, rx, ry, rz;
    deck_at(s, v, &dx, &dy, &dz, &rx, &ry, &rz);
    rotate(dx + mul_q(rx, u), dy + mul_q(ry, u), dz + mul_q(rz, u), x, y, z);
}

void path_cam_lr(int s, int ul, int ur, int v,
                 int *lx, int *ly, int *lz,
                 int *rx, int *ry, int *rz) {
    int dx, dy, dz, wx, wy, wz, cx, cy, cz, ax, ay, az;
    deck_at(s, v, &dx, &dy, &dz, &wx, &wy, &wz);
    rotate(dx, dy, dz, &cx, &cy, &cz);
    rotate(wx, wy, wz, &ax, &ay, &az);
    *lx = cx + mul_s(ax, ul);
    *ly = cy + mul_s(ay, ul);
    *lz = cz + mul_s(az, ul);
    *rx = cx + mul_s(ax, ur);
    *ry = cy + mul_s(ay, ur);
    *rz = cz + mul_s(az, ur);
}

int path_view_clear(int s, int margin, int zmax) {
    int px, py, pz, x, y, z, c = 0, d;
    PROF_N(PN_CLEAR);
    sample_at_s(s, &px, &py, &pz);
    rotate(px - cam_x, py - cam_y, pz - cam_z, &x, &y, &z);
    /* Behind the camera, past zmax, and outside either side of the view:
     * each is a half-space holding the view, so the farthest bounds it. */
    d = -z - margin;
    if (d > c) c = d;
    d = z - zmax - margin;
    if (d > c) c = d;
    d = (iabs(x) - margin - z - z / 4) * 5 / 8;
    if (d > c) c = d;
    return c;
}

int path_project(int s, int u, int v, XY *out) {
    int x, y, z;
    path_cam(s, u, v, &x, &y, &z);
    return project(x, y, z, out);
}

static int isqrt(int n) {
    int r = 0, bit = 1 << 30;
    while (bit > n)
        bit >>= 2;
    while (bit) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* The deck's up vector on screen, not its lateral line: across a turn the
 * lateral line foreshortens and would tilt the figure on flat road. */
int path_stand(int s, int u, int v, XY *feet, int *ax, int *ay) {
    XY head;
    int sz = path_project(s, u, v, feet);
    int dx, dy, len;
    *ax = 4096;
    *ay = 0;
    if (sz < 8)
        return sz;
    path_project(s, u, v + 64, &head);
    dx = head.x - feet->x;
    dy = head.y - feet->y;
    len = isqrt(dx * dx + dy * dy);
    if (dy < 0 && len > 0) {
        *ax = -dy * 4096 / len;
        *ay = dx * 4096 / len;
    }
    return sz;
}

const PathSeg *path_at(int s) {
    if (s < 0)
        return 0;
    return seg_at(band_index(s / STEP));
}

/* Segment under s and how far into it, 0..STEP-1. */
static const PathSeg *seg_local(int s, int *local) {
    if (s < 0)
        s = 0;
    *local = s % STEP;
    if (end_side && *local == 0 && s > 0) {
        *local = STEP;
        return seg_at(band_index(s / STEP - 1));
    }
    return seg_at(band_index(s / STEP));
}

static int cut_at(int band) {
    const PathSeg *a, *b;
    int idx, prev, rx = 0, ry = 0, rz = 0;
    if (band <= 0)
        return 0;
    idx = band_index(band);
    prev = idx - 1;
    if (idx == active->lead && band > active->lead) {
        prev = active->lead + active->cycle - 1;
        rx = active->rep_x;
        ry = active->rep_y;
        rz = active->rep_z;
    }
    a = seg_at(prev);
    b = seg_at(idx);
    return a->x1 + rx != b->x || a->y1 + ry != b->y || a->z1 + rz != b->z ||
           a->el1 != b->el || a->er1 != b->er;
}

int path_cut(int band) {
    int idx;
    if (band <= 0)
        return 0;
    idx = band_index(band);
    if (active->count <= BAND_MAX && idx != active->lead && !(band_flags[idx] & BAND_MAY_CUT))
        return 0;
    return cut_at(band);
}

int path_first_cut(int band0, int band1) {
    int b = band0 > 1 ? band0 : 1;
    while (b <= band1) {
        if (active->count <= BAND_MAX) {
            int c = band_clear[band_index(b)];
            if (c) {
                b += c;
                continue;
            }
        }
        if (cut_at(b))
            return b;
        b++;
    }
    return -1;
}

int path_may_crate(int band) {
    int idx;
    if (band < 0)
        return 0;
    if (road_ends() && band >= active->count)
        return 0;
    idx = band_index(band);
    return active->count > BAND_MAX || (band_flags[idx] & BAND_MAY_CRATE);
}

void path_end_side(int on) {
    end_side = on;
}

static int lerp_u(int a, int b, int local) {
    return a + (b - a) * local / STEP;
}

void path_edges(int s, int *ul, int *ur) {
    int local;
    const PathSeg *seg = seg_local(s, &local);
    *ul = lerp_u(seg->el, seg->el1, local);
    *ur = lerp_u(seg->er, seg->er1, local);
}

int path_lanes(int s, int *u) {
    int local, i;
    const PathSeg *seg = seg_local(s, &local);
    for (i = 0; i < seg->lanes; i++)
        u[i] = lerp_u(seg->lu[i], seg->lu1[i], local);
    return seg->lanes;
}

int path_marks(int s, int *u) {
    int local, i;
    const PathSeg *seg = seg_local(s, &local);
    for (i = 0; i < seg->marks; i++)
        u[i] = lerp_u(seg->mu[i], seg->mu1[i], local);
    return seg->marks;
}

/* How far lane i moves across its segment, centerline slide included: 0
 * for a lane an edge anchor holds. */
static int lane_drift(const PathSeg *seg, int i) {
    int rx, ry, rz, ux, uy, uz, fx, fy, fz;
    basis(seg->yaw, seg->pitch, seg->roll, &rx, &ry, &rz, &ux, &uy, &uz, &fx, &fy, &fz);
    return iabs(seg->lu1[i] - seg->lu[i] +
                dot_q(seg->x1 - seg->x, seg->y1 - seg->y, seg->z1 - seg->z, rx, ry, rz));
}

int path_lane_near(int s, int u) {
    int lanes[PATH_LANES];
    int local, n, i, best = 0;
    const PathSeg *seg = seg_local(s, &local);
    n = path_lanes(s, lanes);
    for (i = 1; i < n; i++) {
        int d = iabs(lanes[i] - u) - iabs(lanes[best] - u);
        if (d < -2 || (d <= 2 && lane_drift(seg, i) < lane_drift(seg, best)))
            best = i;
    }
    return best;
}

/* Which road of fork k a runner at u takes on entering it at band: the
 * one with a lane nearest u, the main road on a tie. */
static int pick_road(int band, int k, int u) {
    int best = 0, best_d = 0, alt;
    for (alt = 0; alt < 2; alt++) {
        const PathSeg *seg;
        int at, i, d = 0x7fff;
        route[k] = (uint8_t)alt;
        cache_band = -1;
        seg = seg_at(band_index(band));
        at = u - jump_at(band);
        for (i = 0; i < seg->lanes; i++) {
            if (iabs(seg->lu[i] - at) < d)
                d = iabs(seg->lu[i] - at);
        }
        if (alt == 0 || d < best_d) {
            best = alt;
            best_d = d;
        }
    }
    return best;
}

int path_follow(int s, int u) {
    int band = s < 0 ? 0 : s / STEP;
    int j = 0;
    if (band == follow_band + 1) {
        int idx = band_index(band);
        int k = branch_of(idx);
        if (k >= 0 && idx == active->branch[k].band0)
            route[k] = (uint8_t)pick_road(band, k, u);
        cache_band = -1;
        j = jump_at(band);
    }
    follow_band = band;
    return j;
}

int path_branches(void) {
    return active->branches;
}

int path_branch(int k, int s, int *s0, int *s1) {
    const TrackBranch *b = &active->branch[k];
    int band = s < 0 ? 0 : s / STEP;
    int lap = 0;
    if (band >= b->band1) {
        if (road_ends() || b->band0 < active->lead)
            return 0;
        lap = (band - b->band1) / active->cycle + 1;
    }
    *s0 = (b->band0 + lap * active->cycle) * STEP;
    *s1 = (b->band1 + lap * active->cycle) * STEP;
    return 1;
}

void path_swap(int k) {
    route[k] ^= 1;
    cache_band = -1;
}

int path_len(void) {
    return active->count;
}

int path_lap(void) {
    if (active->rep_x || active->rep_y || active->rep_z)
        return 0;
    return active->cycle * STEP;
}

#ifdef RUNNER_PROFILE
#include <stdio.h>
void path_bench(void) {
    int i, x, y, z, a, b, c, acc = 0;
    uint32_t t;
#define PB(name, stmt) do { t = prof_now(); for (i = 0; i < 200; i++) { stmt; } \
        printf("PROF bench %-14s %5u cycles\n", name, (unsigned)((((prof_now() - t) & 0xFFFF) * 8) / 200)); } while (0)
    PB("sample", (sample(2000 + i * 7, &x, &y, &z, &a, &b, &c), acc += x));
    PB("isin", acc += isin(i * 13));
    PB("band_index", acc += band_index(i * 3));
    PB("band_lap", acc += band_lap(i * 3));
    PB("seg_at", acc += seg_at(i & 127)->lanes);
    PB("mul_q", acc += mul_q(i * 1000, 3000 - i));
    PB("dot_q", acc += dot_q(i * 1000, i, 3000 - i, 4000, -i, 200));
    PB("rotate", (rotate(i * 100, 50, 3000 - i, &x, &y, &z), acc += x));
    PB("path_cut", acc += path_cut(i));
    PB("path_cam_lr", (path_cam_lr(2000 + i * 7, -50, 50, 0, &x, &y, &z, &a, &b, &c), acc += z));
    printf("PROF bench acc %d\n", acc);
    printf("PROF cam r %d %d %d u %d %d %d f %d %d %d\n", cam_rx, cam_ry, cam_rz, cam_ux, cam_uy, cam_uz, cam_fx, cam_fy, cam_fz);
    for (i = 0; i < 3; i++) {
        int vx = 1000 * i - 700, vy = 300 - i * 50, vz = 5000 + i * 3000;
        rotate(vx, vy, vz, &x, &y, &z);
        printf("PROF rot %d %d %d gte %d %d %d c %d %d %d\n", vx, vy, vz, x, y, z,
               dot_q(vx, vy, vz, cam_rx, cam_ry, cam_rz), dot_q(vx, vy, vz, cam_ux, cam_uy, cam_uz),
               dot_q(vx, vy, vz, cam_fx, cam_fy, cam_fz));
    }
}
#endif
