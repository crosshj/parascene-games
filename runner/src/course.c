#include "world.h"
#include "course.h"

int obstacle_lane(int band) {
    return ((band / 8) % 3) - 1;
}

int has_obstacle(int band) {
    return band >= 8 && (band % 8) == 5;
}

#define C(c) (c)[0], (c)[1], (c)[2]

void obstacle_box(int zc, int lane_i, const ObstacleStyle *s) {
    int x, x0, x1, z, z1, w = 64, h = OBST_H, d = OBST_D;
    if (zc < CLIP || zc >= FAR)
        return;
    x = lane_i * LANE;
    x0 = x - w;
    x1 = x + w;
    z = zc - d / 2;
    z1 = zc + d / 2;
    if (z1 >= FAR)
        z1 = FAR - 1;
    /* Back, then sides, then the lit top, then the front. Nearer sort values
     * draw later, so the front stays outside the other faces. */
    face(x0, h, z1, x1, h, z1, x0, 0, z1, x1, 0, z1, z1, C(s->back));
    face(x0, h, z, x0, h, z1, x0, 0, z, x0, 0, z1, z + d / 2, C(s->left));
    face(x1, h, z1, x1, h, z, x1, 0, z1, x1, 0, z, z + d / 2, C(s->right));
    face(x0, h, z1, x1, h, z1, x0, h, z, x1, h, z, z + d / 4, C(s->top));
    face(x0, h, z, x1, h, z, x0, 0, z, x1, 0, z, z, C(s->front));
}
