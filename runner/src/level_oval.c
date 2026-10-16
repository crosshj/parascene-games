/* FLAT OVAL: the viewer's closed lap, with nothing around the road.
 * The camera follows the yaw, so this is the world that exercises a turn. */

#include "world.h"
#include "course.h"
#include "path.h"
#include "level.h"

static int edge_px(int x0, int y0, int z0, int x1, int y1, int z1) {
    XY a, b;
    int dx, dy;
    if (z0 < 8 || z1 < 8)
        return 9999;
    project(x0, y0, z0, &a);
    project(x1, y1, z1, &b);
    dx = iabs(a.x - b.x);
    dy = iabs(a.y - b.y);
    return dx > dy ? dx : dy;
}

static void patch(int s0, int s1, int u0, int u1, int v,
                  int r, int g, int b, int depth) {
    int x00, y00, z00, x01, y01, z01, x11, y11, z11, x10, y10, z10;
    int behind, ahead, wide, sort;
    path_cam(s0, u0, v, &x00, &y00, &z00);
    path_cam(s0, u1, v, &x01, &y01, &z01);
    path_cam(s1, u1, v, &x11, &y11, &z11);
    path_cam(s1, u0, v, &x10, &y10, &z10);
    behind = (z00 < 8) + (z01 < 8) + (z11 < 8) + (z10 < 8);
    ahead = (z00 >= FAR) + (z01 >= FAR) + (z11 >= FAR) + (z10 >= FAR);
    if (behind == 4 || ahead == 4)
        return;
    wide = edge_px(x00, y00, z00, x01, y01, z01) > 400
        || edge_px(x10, y10, z10, x11, y11, z11) > 400
        || edge_px(x00, y00, z00, x10, y10, z10) > 400
        || edge_px(x01, y01, z01, x11, y11, z11) > 400;
    if (depth < 5 && (behind || wide)) {
        if (iabs(u1 - u0) >= iabs(s1 - s0) / 2) {
            int um = u0 + (u1 - u0) / 2;
            patch(s0, s1, u0, um, v, r, g, b, depth + 1);
            patch(s0, s1, um, u1, v, r, g, b, depth + 1);
        } else {
            int sm = s0 + (s1 - s0) / 2;
            patch(s0, sm, u0, u1, v, r, g, b, depth + 1);
            patch(sm, s1, u0, u1, v, r, g, b, depth + 1);
        }
        return;
    }
    sort = FAR;
    if (z00 >= 8 && z00 < sort) sort = z00;
    if (z01 >= 8 && z01 < sort) sort = z01;
    if (z11 >= 8 && z11 < sort) sort = z11;
    if (z10 >= 8 && z10 < sort) sort = z10;
    if (sort >= FAR)
        return;
    if (sort < CLIP)
        sort = CLIP;
    face(x00, y00, z00, x01, y01, z01, x10, y10, z10, x11, y11, z11,
         sort, r, g, b);
}

static void oval_clear(const LevelFrame *f, int *r, int *g, int *b) {
    (void)f;
    *r = 78;
    *g = 112;
    *b = 148;
}

static void oval_draw(const LevelFrame *f) {
    int n = path_len();
    int i;
    (void)f;
    for (i = n - 1; i >= 0; i--) {
        const PathSeg *seg = path_at(i * STEP);
        int s = i * STEP;
        int half = seg->width / 2;
        int alt = i & 1;
        int rr = 96 + alt * 10;
        int gg = 92 + alt * 8;
        int bb = 84 + alt * 6;
        patch(s, s + STEP, -half, half, 0, rr, gg, bb, 0);
        patch(s, s + STEP, -LANE / 2 - 8, -LANE / 2 + 8, 2, 214, 198, 140, 0);
        patch(s, s + STEP, LANE / 2 - 8, LANE / 2 + 8, 2, 214, 198, 140, 0);
    }
}

const Level level_oval = {
    "FLAT OVAL",
    "bend.flat",
    oval_clear,
    oval_draw,
};
