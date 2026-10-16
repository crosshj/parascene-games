/* Host check: the baked span matches the old obstacle formula. */
#include <stdio.h>
#include "course.h"
#include "path.h"

int project(int x, int y, int z, XY *out) {
    (void)x;
    (void)y;
    out->x = 0;
    out->y = 0;
    return z;
}

void face(int x0, int y0, int z0, int x1, int y1, int z1,
          int x2, int y2, int z2, int x3, int y3, int z3,
          int sortz, int r, int g, int b) {
    (void)x0; (void)y0; (void)z0; (void)x1; (void)y1; (void)z1;
    (void)x2; (void)y2; (void)z2; (void)x3; (void)y3; (void)z3;
    (void)sortz; (void)r; (void)g; (void)b;
}

static int formula_has(int band) {
    return band >= 8 && (band % 8) == 5;
}

static int formula_lane(int band) {
    return ((band / 8) % 3) - 1;
}

int main(void) {
    int band;
    int x, y, z;
    if (!path_open("straight.run") || path_open("bend.flat")) {
        fprintf(stderr, "path_open gate failed\n");
        return 1;
    }
    path_begin(1000);
    path_cam(1000 + 200, 150, 40, &x, &y, &z);
    if (x != 150 || y != 40 || z != 200) {
        fprintf(stderr, "path_cam %d %d %d\n", x, y, z);
        return 1;
    }
    for (band = 0; band < 8000; band++) {
        int has = has_obstacle(band);
        if (has != formula_has(band)) {
            fprintf(stderr, "band %d has %d\n", band, has);
            return 1;
        }
        if (has && obstacle_lane(band) != formula_lane(band)) {
            fprintf(stderr, "band %d lane %d\n", band, obstacle_lane(band));
            return 1;
        }
    }
    printf("track span ok\n");
    return 0;
}
