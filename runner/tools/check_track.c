/* Host check: the baked span matches the old obstacle formula. */
#include <stdio.h>
#include "course.h"
#include "path.h"

/* Pinhole like world.c's GTE setup: focal SCREEN_W/2, eye CAM_H up. */
int project(int x, int y, int z, XY *out) {
    int d = z < 8 ? 8 : z;
    out->x = (int16_t)(SCREEN_W / 2 + x * (SCREEN_W / 2) / d);
    out->y = (int16_t)(HORIZON_Y + (CAM_H - y) * (SCREEN_W / 2) / d);
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

void poly_f4(const XY *p0, const XY *p1, const XY *p2, const XY *p3,
             int bucket, int r, int g, int b, int blend) {
    (void)p0; (void)p1; (void)p2; (void)p3;
    (void)bucket; (void)r; (void)g; (void)b; (void)blend;
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

/* The runner's lane logic from main.c: from u0 at the start, run to s_end
 * holding the lane. Returns the lane u at the end; *mid is the lane count
 * at s_mid. */
static int run_lane(int u0, int s_mid, int s_end, int *mid) {
    int u[PATH_LANES];
    int s, lane_u = u0;
    path_reset();
    path_follow(CHAR_Z, lane_u);
    for (s = CHAR_Z + 20; s < s_end; s += 20) {
        lane_u -= path_follow(s, lane_u);
        path_lanes(s, u);
        lane_u = u[path_lane_near(s, lane_u)];
        if (s / 20 == s_mid / 20)
            *mid = path_lanes(s, u);
    }
    return lane_u;
}

/* path_first_cut against path_cut band by band, on the route and with
 * each fork swapped. */
static int first_cut_ok(const char *id) {
    int k, b0, len, b, want;
    path_open(id);
    for (k = -1; k < path_branches(); k++) {
        if (k >= 0)
            path_swap(k);
        for (b0 = 0; b0 < 900; b0++) {
            for (len = 0; len < 40; len += 7) {
                want = -1;
                for (b = b0; b <= b0 + len; b++) {
                    if (path_cut(b)) {
                        want = b;
                        break;
                    }
                }
                if (path_first_cut(b0, b0 + len) != want) {
                    fprintf(stderr, "%s fork %d: first cut %d..%d is %d, want %d\n", id, k,
                            b0, b0 + len, path_first_cut(b0, b0 + len), want);
                    return 0;
                }
            }
        }
        if (k >= 0)
            path_swap(k);
    }
    return 1;
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
        if (has && obstacle_u(band) != formula_lane(band) * LANE) {
            fprintf(stderr, "band %d u %d\n", band, obstacle_u(band));
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
    /* A flat turn never tilts the runner. */
    {
        XY feet;
        int ax, ay;
        path_begin(4096 + 4096);
        path_stand(4096 + 4096 + CHAR_Z, 0, 0, &feet, &ax, &ay);
        if (!near(ay, 0, 120)) {
            fprintf(stderr, "oval stand lean %d %d\n", ax, ay);
            return 1;
        }
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
    if (!path_open("bank.bend")) {
        fprintf(stderr, "bank.bend did not open\n");
        return 1;
    }
    /* Mid turn the deck banks right on a level horizon: the left edge rides
     * high, the right edge stays at grade, the center sits halfway. */
    path_begin(6144);
    if (path_horizon() != HORIZON_Y) {
        fprintf(stderr, "bank horizon %d\n", path_horizon());
        return 1;
    }
    {
        int lx, ly, lz, rx, ry, rz;
        path_cam_lr(6144 + CHAR_Z, -280, 280, 0, &lx, &ly, &lz, &rx, &ry, &rz);
        if (!near(ly - ry, 311, 12) || !near(ly + ry, 0, 12)) {
            fprintf(stderr, "bank turn edges %d %d\n", ly, ry);
            return 1;
        }
    }
    /* In the S the bank flips: left low through carve-left, right low
     * through carve-right. */
    {
        int lx, ly, lz, rx, ry, rz;
        path_begin(15000);
        path_cam_lr(15000 + CHAR_Z, -280, 280, 0, &lx, &ly, &lz, &rx, &ry, &rz);
        if (ly >= ry) {
            fprintf(stderr, "bank carve-left edges %d %d\n", ly, ry);
            return 1;
        }
        path_begin(19000);
        path_cam_lr(19000 + CHAR_Z, -280, 280, 0, &lx, &ly, &lz, &rx, &ry, &rz);
        if (ly <= ry) {
            fprintf(stderr, "bank carve-right edges %d %d\n", ly, ry);
            return 1;
        }
    }
    /* The runner stands square to the deck. On a right bank the deck runs
     * down to the right on screen (ay > 0), about the bank angle. */
    {
        XY feet;
        int ax, ay;
        path_begin(6144);
        path_stand(6144 + CHAR_Z, 0, 0, &feet, &ax, &ay);
        if (ay < 1800 || ay > 2600) {
            fprintf(stderr, "bank stand lean %d %d\n", ax, ay);
            return 1;
        }
    }
    path_begin(46080 + 200);
    path_cam(46080 + 400, 0, 0, &x, &y, &z);
    if (!near(x, 0, 2) || !near(y, 0, 2) || !near(z, 200, 4)) {
        fprintf(stderr, "bank lap %d %d %d\n", x, y, z);
        return 1;
    }
    if (!path_open("lanes.count")) {
        fprintf(stderr, "lanes.count did not open\n");
        return 1;
    }
    /* three-to-two holds its left edge: on the straight it stays at -280
     * from the camera while the right edge comes in from 280 to 130. */
    {
        int ul, ur, lx, ly, lz, rx, ry, rz;
        path_begin(4096);
        path_edges(4096 + CHAR_Z, &ul, &ur);
        path_cam_lr(4096 + CHAR_Z, ul, ur, 0, &lx, &ly, &lz, &rx, &ry, &rz);
        if (!near(lx, -280, 2) || !near(rx, 280 - 150 * CHAR_Z / 4096, 2)) {
            fprintf(stderr, "lanes three edges %d %d\n", lx, rx);
            return 1;
        }
        path_edges(8190, &ul, &ur);
        path_cam_lr(8190, ul, ur, 0, &lx, &ly, &lz, &rx, &ry, &rz);
        if (!near(lx, -280, 3) || !near(rx, 130, 3)) {
            fprintf(stderr, "lanes three-to-two edges %d %d\n", lx, rx);
            return 1;
        }
    }
    /* The right lane holds through 3->2 (left anchor drops nothing on the
     * left, so the right lane closes onto the middle one). */
    {
        int mid = 0;
        int u = run_lane(150, 10000, 12000, &mid);
        if (mid != 2 || !near(u, 75, 2)) {
            fprintf(stderr, "lanes right lane %d lanes %d\n", u, mid);
            return 1;
        }
    }
    if (!path_open("fork.lane")) {
        fprintf(stderr, "fork.lane did not open\n");
        return 1;
    }
    /* The left lane takes the one-lane road, which opens on its right; the
     * runner keeps its line and meets the joined road on the left lane. The
     * middle and right lanes take the two-lane road, which drops its left
     * lane; both end on the right lane. */
    {
        static const struct { int u0, mid, end; } want[] = {
            {-150, 1, -150}, {0, 2, 150}, {150, 2, 150},
        };
        int i;
        for (i = 0; i < 3; i++) {
            int mid = 0;
            int u = run_lane(want[i].u0, 6000, 19200, &mid);
            if (mid != want[i].mid || !near(u, want[i].end, 2)) {
                fprintf(stderr, "fork from %d: %d lanes mid, ends at %d\n", want[i].u0, mid, u);
                return 1;
            }
        }
    }
    if (!path_open("straight.run")) {
        fprintf(stderr, "straight.run did not reopen\n");
        return 1;
    }
    {
        static const char *const ids[] = {
            "straight.run", "bend.flat", "hill.loop", "bank.bend", "lanes.count", "fork.lane"
        };
        for (i = 0; i < 6; i++)
            if (!first_cut_ok(ids[i]))
                return 1;
    }
    printf("track span ok\n");
    return 0;
}
