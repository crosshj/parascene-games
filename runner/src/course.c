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

void obstacle_box(int s, int lane_i, const ObstacleStyle *style) {
    int u, w = 64, h = OBST_H, d = OBST_D;
    int zc;
    int nltx, nlty, nltz, nrtx, nrty, nrtz;
    int nlbx, nlby, nlbz, nrbx, nrby, nrbz;
    int fltx, flty, fltz, frtx, frty, frtz;
    int flbx, flby, flbz, frbx, frby, frbz;
    int sort_side, sort_top;
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
    /* Back, then sides, then the lit top, then the front. Nearer sort values
     * draw later, so the front stays outside the other faces. */
    face(fltx, flty, fltz, frtx, frty, frtz, flbx, flby, flbz, frbx, frby, frbz,
         fltz, C(style->back));
    face(nltx, nlty, nltz, fltx, flty, fltz, nlbx, nlby, nlbz, flbx, flby, flbz,
         sort_side, C(style->left));
    face(frtx, frty, frtz, nrtx, nrty, nrtz, frbx, frby, frbz, nrbx, nrby, nrbz,
         sort_side, C(style->right));
    face(fltx, flty, fltz, frtx, frty, frtz, nltx, nlty, nltz, nrtx, nrty, nrtz,
         sort_top, C(style->top));
    face(nltx, nlty, nltz, nrtx, nrty, nrtz, nlbx, nlby, nlbz, nrbx, nrby, nrbz,
         nltz, C(style->front));
}
