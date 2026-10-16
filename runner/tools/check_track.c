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

void quad(int x0, int y0, int z0, int x1, int y1, int z1,
          int x2, int y2, int z2, int x3, int y3, int z3,
          int sortz, int r, int g, int b) {
    face(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, sortz, r, g, b);
}

void dim(int *r, int *g, int *b, int z) {
    (void)r; (void)g; (void)b; (void)z;
}

static int formula_has(int band) {
    return band >= 8 && (band % 8) == 5;
}

static int formula_lane(int band) {
    return ((band / 8) % 3) - 1;
}

static int near(int got, int want, int slop) {
    int d = got - want;
    if (d < 0)
        d = -d;
    return d <= slop;
}

int main(void) {
    int band;
    int x, y, z;
    int i, turned;
    if (!path_open("straight.run") || path_open("nope")) {
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
    /* A later lap of the straight cycle still runs forward. */
    path_begin(10000);
    path_cam(10200, 0, 0, &x, &y, &z);
    if (!near(x, 0, 1) || !near(y, 0, 1) || !near(z, 200, 1)) {
        fprintf(stderr, "straight lap %d %d %d\n", x, y, z);
        return 1;
    }
    if (!path_open("bend.flat")) {
        fprintf(stderr, "bend.flat did not open\n");
        return 1;
    }
    path_begin(0);
    path_cam(200, 150, 40, &x, &y, &z);
    if (!near(x, 150, 1) || !near(y, 40, 1) || !near(z, 200, 1)) {
        fprintf(stderr, "oval start %d %d %d\n", x, y, z);
        return 1;
    }
    /* One lap later the road is back in the same place. */
    path_begin(24576);
    path_cam(24576 + 200, 0, 0, &x, &y, &z);
    if (!near(x, 0, 1) || !near(y, 0, 1) || !near(z, 200, 1)) {
        fprintf(stderr, "oval lap %d %d %d\n", x, y, z);
        return 1;
    }
    turned = 0;
    for (i = 0; i < path_len(); i++) {
        const PathSeg *seg = path_at(i * 256);
        if (!seg || seg->yaw < 1000 || seg->yaw > 1100)
            continue;
        path_begin(i * 256);
        path_cam(i * 256, 150, 0, &x, &y, &z);
        if (!near(x, 150, 8) || !near(z, 0, 8)) {
            fprintf(stderr, "oval right %d %d %d yaw %d\n", x, y, z, seg->yaw);
            return 1;
        }
        path_cam(i * 256 + 256, 0, 0, &x, &y, &z);
        if (z < 200 || !near(x, 0, 40)) {
            fprintf(stderr, "oval ahead %d %d %d\n", x, y, z);
            return 1;
        }
        turned = 1;
        break;
    }
    if (!turned) {
        fprintf(stderr, "oval has no quarter turn\n");
        return 1;
    }
    if (!path_open("hill.loop")) {
        fprintf(stderr, "hill.loop did not open\n");
        return 1;
    }
    path_begin(0);
    if (path_horizon() != HORIZON_Y) {
        fprintf(stderr, "hill flat horizon %d\n", path_horizon());
        return 1;
    }
    /* From the foot of the climb, the road ahead rises. */
    path_begin(1800);
    path_cam(1800 + 2000, 0, 0, &x, &y, &z);
    if (y < 100 || z < 1500) {
        fprintf(stderr, "hill climb ahead %d %d %d\n", x, y, z);
        return 1;
    }
    /* Nose up on the climb drops the horizon on screen. */
    path_begin(6144);
    if (path_horizon() < HORIZON_Y + 30) {
        fprintf(stderr, "hill climb horizon %d\n", path_horizon());
        return 1;
    }
    /* The near deck stays under the camera whatever the grade. */
    path_cam(6144 + CHAR_Z, 0, 0, &x, &y, &z);
    if (!near(x, 0, 2) || !near(y, 0, 24) || !near(z, CHAR_Z, 24)) {
        fprintf(stderr, "hill near deck %d %d %d\n", x, y, z);
        return 1;
    }
    /* One lap later the road is back in the same place, at the same height. */
    path_begin(65536 + 200);
    path_cam(65536 + 400, 0, 0, &x, &y, &z);
    if (!near(x, 0, 2) || !near(y, 0, 2) || !near(z, 200, 4)) {
        fprintf(stderr, "hill lap %d %d %d\n", x, y, z);
        return 1;
    }
    if (!path_open("straight.run")) {
        fprintf(stderr, "straight.run did not reopen\n");
        return 1;
    }
    printf("track span ok\n");
    return 0;
}
