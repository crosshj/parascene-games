#include "world.h"
#include "course.h"
#include "path.h"

int obstacle_lane(int band) {
    const PathSeg *seg;
    if (band < 0)
        return 0;
    seg = path_at(band * STEP);
    if (!seg || seg->obstacle == OBSTACLE_NONE)
        return 0;
    return seg->obstacle;
}

int has_obstacle(int band) {
    const PathSeg *seg;
    if (band < 0)
        return 0;
    seg = path_at(band * STEP);
    return seg && seg->obstacle != OBSTACLE_NONE;
}

#define C(c) (c)[0], (c)[1], (c)[2]

/* Past this, drop per-face lighting — same flat color on every side. */
#define OBST_FLAT_Z 1450

static void face_s(int x0, int y0, int z0, int x1, int y1, int z1,
                   int x2, int y2, int z2, int x3, int y3, int z3,
                   int sortz, int r, int g, int b, Shade shade) {
    if (shade)
        shade(&r, &g, &b, sortz);
    else
        dim(&r, &g, &b, sortz);
    quad(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, sortz, r, g, b);
}

void obstacle_box_shade(int s, int lane_i, const ObstacleStyle *style, Shade shade) {
    int u, w = 64, h = OBST_H, d = OBST_D;
    int zc, flat;
    int nltx, nlty, nltz, nrtx, nrty, nrtz;
    int nlbx, nlby, nlbz, nrbx, nrby, nrbz;
    int fltx, flty, fltz, frtx, frty, frtz;
    int flbx, flby, flbz, frbx, frby, frbz;
    int sort_side, sort_top;
    const uint8_t *back, *left, *right, *top, *front;
    u = lane_i * LANE;
    path_cam(s, u, 0, &nltx, &nlty, &zc);
    if (zc < CLIP || zc >= FAR)
        return;
    path_cam(s - d / 2, u - w, h, &nltx, &nlty, &nltz);
    path_cam(s - d / 2, u + w, h, &nrtx, &nrty, &nrtz);
    path_cam(s - d / 2, u - w, 0, &nlbx, &nlby, &nlbz);
    path_cam(s - d / 2, u + w, 0, &nrbx, &nrby, &nrbz);
    path_cam(s + d / 2, u - w, h, &fltx, &flty, &fltz);
    path_cam(s + d / 2, u + w, h, &frtx, &frty, &frtz);
    path_cam(s + d / 2, u - w, 0, &flbx, &flby, &flbz);
    path_cam(s + d / 2, u + w, 0, &frbx, &frby, &frbz);
    if (fltz >= FAR) fltz = FAR - 1;
    if (frtz >= FAR) frtz = FAR - 1;
    if (flbz >= FAR) flbz = FAR - 1;
    if (frbz >= FAR) frbz = FAR - 1;
    sort_side = zc;
    sort_top = zc - d / 4;
    flat = zc > OBST_FLAT_Z;
    front = style->front;
    back = flat ? front : style->back;
    left = flat ? front : style->left;
    right = flat ? front : style->right;
    top = flat ? front : style->top;
    /* Back, sides, top, front — nearer sort draws later. */
    face_s(fltx, flty, fltz, frtx, frty, frtz, flbx, flby, flbz, frbx, frby, frbz,
           fltz, C(back), shade);
    face_s(nltx, nlty, nltz, fltx, flty, fltz, nlbx, nlby, nlbz, flbx, flby, flbz,
           sort_side, C(left), shade);
    face_s(frtx, frty, frtz, nrtx, nrty, nrtz, frbx, frby, frbz, nrbx, nrby, nrbz,
           sort_side, C(right), shade);
    face_s(fltx, flty, fltz, frtx, frty, frtz, nltx, nlty, nltz, nrtx, nrty, nrtz,
           sort_top, C(top), shade);
    face_s(nltx, nlty, nltz, nrtx, nrty, nrtz, nlbx, nlby, nlbz, nrbx, nrby, nrbz,
           nltz, C(front), shade);
}

void obstacle_box(int s, int lane_i, const ObstacleStyle *style) {
    obstacle_box_shade(s, lane_i, style, dim);
}
