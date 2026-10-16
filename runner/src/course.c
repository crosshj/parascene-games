#include "world.h"
#include "course.h"
#include "path.h"
#include "profile.h"

int obstacle_u(int band) {
    const PathSeg *seg;
    if (band < 0)
        return 0;
    seg = path_at(band * STEP);
    if (!seg || seg->obstacle == OBSTACLE_NONE)
        return 0;
    return seg->obstacle * LANE / 2;
}

int has_obstacle(int band) {
    const PathSeg *seg;
    if (!path_may_crate(band))
        return 0;
    seg = path_at(band * STEP);
    return seg && seg->obstacle != OBSTACLE_NONE;
}

#define C(c) (c)[0], (c)[1], (c)[2]

/* Past this, drop per-face lighting — same flat color on every side. */
#define OBST_FLAT_Z 1450

typedef struct {
    int x, y, z;
    XY p;
} Corner;

static void corner(Corner *c, int x, int y, int z) {
    c->x = x;
    c->y = y;
    c->z = z;
    if (z >= 8)
        project(x, y, z, &c->p);
}

static void face_s(const Corner *c0, const Corner *c1, const Corner *c2, const Corner *c3,
                   int sortz, const uint8_t *c, Shade shade) {
    int r = c[0], g = c[1], b = c[2];
    if (c0->z < 8 || c1->z < 8 || c2->z < 8 || c3->z < 8)
        return;
    if (shade)
        shade(&r, &g, &b, sortz);
    else
        dim(&r, &g, &b, sortz);
    poly_f4(&c0->p, &c1->p, &c2->p, &c3->p, sortz >> OT_SHIFT, r, g, b, BLEND_NONE);
}

/* Three deck samples: the near base, the near top, and the far base. The
 * far top is the far base lifted as far as the near one. Each corner is
 * projected once. */
void obstacle_box_shade(int s, int u, const ObstacleStyle *style, Shade shade) {
    int w = 64, h = OBST_H, d = OBST_D;
    int zc, flat, sort_side, sort_top;
    int nlbx, nlby, nlbz, nrbx, nrby, nrbz;
    int nltx, nlty, nltz, nrtx, nrty, nrtz;
    int flbx, flby, flbz, frbx, frby, frbz;
    Corner nlt, nrt, nlb, nrb, flt, frt, flb, frb;
    const uint8_t *back, *left, *right, *top, *front;
    PROF_N(PN_BOX);
    path_cam_lr(s - d / 2, u - w, u + w, 0, &nlbx, &nlby, &nlbz, &nrbx, &nrby, &nrbz);
    path_cam_lr(s + d / 2, u - w, u + w, 0, &flbx, &flby, &flbz, &frbx, &frby, &frbz);
    zc = (nlbz + nrbz + flbz + frbz) / 4;
    if (zc < CLIP || zc >= FAR)
        return;
    path_cam_lr(s - d / 2, u - w, u + w, h, &nltx, &nlty, &nltz, &nrtx, &nrty, &nrtz);
    corner(&nlb, nlbx, nlby, nlbz);
    corner(&nrb, nrbx, nrby, nrbz);
    corner(&nlt, nltx, nlty, nltz);
    corner(&nrt, nrtx, nrty, nrtz);
    corner(&flb, flbx, flby, flbz < FAR ? flbz : FAR - 1);
    corner(&frb, frbx, frby, frbz < FAR ? frbz : FAR - 1);
    {
        int fltz = flbz + nltz - nlbz, frtz = frbz + nrtz - nrbz;
        corner(&flt, flbx + nltx - nlbx, flby + nlty - nlby, fltz < FAR ? fltz : FAR - 1);
        corner(&frt, frbx + nrtx - nrbx, frby + nrty - nrby, frtz < FAR ? frtz : FAR - 1);
    }
    sort_side = zc;
    sort_top = zc - d / 4;
    flat = zc > OBST_FLAT_Z;
    front = style->front;
    back = flat ? front : style->back;
    left = flat ? front : style->left;
    right = flat ? front : style->right;
    top = flat ? front : style->top;
    /* Back, sides, top, front — nearer sort draws later. */
    face_s(&flt, &frt, &flb, &frb, flt.z, back, shade);
    face_s(&nlt, &flt, &nlb, &flb, sort_side, left, shade);
    face_s(&frt, &nrt, &frb, &nrb, sort_side, right, shade);
    face_s(&flt, &frt, &nlt, &nrt, sort_top, top, shade);
    face_s(&nlt, &nrt, &nlb, &nrb, nlt.z, front, shade);
}

void obstacle_box(int s, int u, const ObstacleStyle *style) {
    obstacle_box_shade(s, u, style, dim);
}
