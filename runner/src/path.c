#include <string.h>
#include "path.h"
#include "course.h"
#include "track_data.h"

static int path_scroll;
static const TrackDef *active = &track_table[0];

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

static int dot_q(int dx, int dy, int dz, int x, int y, int z) {
    return (int)(((int64_t)dx * x + (int64_t)dy * y + (int64_t)dz * z) / 4096);
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

static int band_index(int band) {
    if (band < active->lead)
        return band;
    return active->lead + (band - active->lead) % active->cycle;
}

static int band_lap(int band) {
    if (band < active->lead)
        return 0;
    return (band - active->lead) / active->cycle;
}

static int ang_delta(int a, int b) {
    int d = (b - a) & 4095;
    if (d > 2048)
        d -= 4096;
    return d;
}

static const PathSeg *sample(int s, int *x, int *y, int *z,
                             int *yaw, int *pitch, int *roll) {
    int band, local, lap, idx;
    const PathSeg *a;
    const PathSeg *b;
    if (s < 0)
        s = 0;
    band = s / STEP;
    local = s - band * STEP;
    lap = band_lap(band);
    idx = band_index(band);
    a = &active->seg[idx];
    b = &active->seg[band_index(band + 1)];
    *x = a->x + lap * active->rep_x + (a->x1 - a->x) * local / STEP;
    *y = a->y + lap * active->rep_y + (a->y1 - a->y) * local / STEP;
    *z = a->z + lap * active->rep_z + (a->z1 - a->z) * local / STEP;
    *yaw = a->yaw + ang_delta(a->yaw, b->yaw) * local / STEP;
    *pitch = a->pitch + ang_delta(a->pitch, b->pitch) * local / STEP;
    *roll = a->roll + ang_delta(a->roll, b->roll) * local / STEP;
    return a;
}

int path_open(const char *id) {
    int i;
    if (!id)
        return 0;
    for (i = 0; i < TRACK_TABLE_COUNT; i++) {
        if (strcmp(track_table[i].id, id) == 0) {
            active = &track_table[i];
            return 1;
        }
    }
    return 0;
}

void path_begin(int scroll) {
    int cyaw, cpitch, croll;
    path_scroll = scroll;
    sample(path_scroll, &cam_x, &cam_y, &cam_z, &cyaw, &cpitch, &croll);
    cam_yaw = cyaw & 4095;
    /* The camera takes most of the grade, so a hill ahead still reads as one. */
    cpitch = ang_delta(0, cpitch) * CAM_PITCH_NUM / CAM_PITCH_DEN;
    basis(cyaw, cpitch, croll,
          &cam_rx, &cam_ry, &cam_rz,
          &cam_ux, &cam_uy, &cam_uz,
          &cam_fx, &cam_fy, &cam_fz);
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

/* Flat deck (pitch=roll=0): right = (cy,0,-sy), up = (0,1,0). */
static void to_cam(int px, int py, int pz, int *x, int *y, int *z) {
    int dx = px - cam_x;
    int dy = py - cam_y;
    int dz = pz - cam_z;
    *x = dot_q(dx, dy, dz, cam_rx, cam_ry, cam_rz);
    *y = dot_q(dx, dy, dz, cam_ux, cam_uy, cam_uz);
    *z = dot_q(dx, dy, dz, cam_fx, cam_fy, cam_fz);
}

void path_cam(int s, int u, int v, int *x, int *y, int *z) {
    int px, py, pz, yaw, pitch, roll;
    sample(s, &px, &py, &pz, &yaw, &pitch, &roll);
    if (pitch == 0 && roll == 0) {
        int sy = isin(yaw);
        int cy = icos(yaw);
        px += mul_q(cy, u);
        py += v;
        pz += mul_q(-sy, u);
    } else {
        int prx, pry, prz, pux, puy, puz, pfx, pfy, pfz;
        basis(yaw, pitch, roll, &prx, &pry, &prz, &pux, &puy, &puz, &pfx, &pfy, &pfz);
        px += mul_q(prx, u) + mul_q(pux, v);
        py += mul_q(pry, u) + mul_q(puy, v);
        pz += mul_q(prz, u) + mul_q(puz, v);
    }
    to_cam(px, py, pz, x, y, z);
}

void path_cam_lr(int s, int half, int v,
                 int *lx, int *ly, int *lz,
                 int *rx, int *ry, int *rz) {
    int px, py, pz, yaw, pitch, roll;
    sample(s, &px, &py, &pz, &yaw, &pitch, &roll);
    if (pitch == 0 && roll == 0) {
        int sy = isin(yaw);
        int cy = icos(yaw);
        int dx = mul_q(cy, half);
        int dz = mul_q(-sy, half);
        to_cam(px - dx, py + v, pz - dz, lx, ly, lz);
        to_cam(px + dx, py + v, pz + dz, rx, ry, rz);
    } else {
        int prx, pry, prz, pux, puy, puz, pfx, pfy, pfz;
        basis(yaw, pitch, roll, &prx, &pry, &prz, &pux, &puy, &puz, &pfx, &pfy, &pfz);
        to_cam(px - mul_q(prx, half) + mul_q(pux, v),
               py - mul_q(pry, half) + mul_q(puy, v),
               pz - mul_q(prz, half) + mul_q(puz, v),
               lx, ly, lz);
        to_cam(px + mul_q(prx, half) + mul_q(pux, v),
               py + mul_q(pry, half) + mul_q(puy, v),
               pz + mul_q(prz, half) + mul_q(puz, v),
               rx, ry, rz);
    }
}

int path_project(int s, int u, int v, XY *out) {
    int x, y, z;
    path_cam(s, u, v, &x, &y, &z);
    return project(x, y, z, out);
}

const PathSeg *path_at(int s) {
    if (s < 0)
        return 0;
    return &active->seg[band_index(s / STEP)];
}

int path_len(void) {
    return active->count;
}

int path_lap(void) {
    if (active->rep_x || active->rep_y || active->rep_z)
        return 0;
    return active->cycle * STEP;
}
