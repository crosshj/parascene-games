/* City Run: a straight road between blocks of buildings, with gantries
 * across the road. */

#include <stdint.h>
#include "world.h"
#include "course.h"
#include "level.h"

#define SHOULDER 640

static const int k_height[12] = {
    0, 180, 320, 90, 0, 240, 70, 380, 150, 0, 260, 120
};

static const uint8_t k_bcol[6][3] = {
    {176, 72, 64},
    {64, 96, 168},
    {150, 124, 64},
    {56, 132, 112},
    {132, 76, 148},
    {96, 100, 112}
};

static const ObstacleStyle k_obstacle = {
    {110, 40, 16}, {150, 52, 20}, {186, 72, 28}, {255, 196, 96}, {232, 108, 36}
};

static void building(int band, int z, int side_sign) {
    int h = k_height[(band * (side_sign < 0 ? 1 : 5) + 3) % 12];
    int inner, outer, depth, c;
    int r, g, b;
    if (h <= 0 || z < CLIP || z >= FAR)
        return;
    inner = side_sign * (ROAD_HALF + 36);
    outer = side_sign * (ROAD_HALF + 150);
    depth = 160;
    if (z + depth >= FAR)
        depth = FAR - z - 1;
    if (depth < 24)
        return;
    c = (band + (side_sign < 0 ? 0 : 2)) % 6;
    r = k_bcol[c][0];
    g = k_bcol[c][1];
    b = k_bcol[c][2];
    /* A roof above the camera is the underside, and it fights the front face. */
    if (h < CAM_H)
        lid(inner, z + 6, outer, z + depth, h, r + 28, g + 28, b + 20);
    side(inner, z, z + depth, 0, h, r * 3 / 4, g * 3 / 4, b * 3 / 4);
    wall(inner, outer, z, 0, h, r, g, b);
    if (h > 140)
        wall(inner + side_sign * 18, outer - side_sign * 18, z, 48, h - 48, 24, 28, 36);
}

static void gantry(int z) {
    int edge = ROAD_HALF + 8;
    if (z < CLIP || z >= FAR)
        return;
    wall(-edge - 36, -edge, z, 0, 230, 168, 176, 188);
    wall(edge, edge + 36, z, 0, 230, 168, 176, 188);
    wall(-edge, edge, z, 200, 228, 140, 150, 168);
}

static void city_clear(const LevelFrame *f, int *r, int *g, int *b) {
    switch ((f->scroll / (STEP * 8)) & 3) {
    case 0: *r = 48; *g = 88; *b = 148; break;
    case 1: *r = 72; *g = 56; *b = 112; break;
    case 2: *r = 96; *g = 112; *b = 132; break;
    default: *r = 36; *g = 64; *b = 96; break;
    }
}

static void shoulder_color(int band, int *r, int *g, int *b) {
    switch ((band >> 3) & 3) {
    case 0: *r = 40; *g = 92; *b = 36; break;
    case 1: *r = 28; *g = 64; *b = 40; break;
    case 2: *r = 132; *g = 108; *b = 56; break;
    default: *r = 48; *g = 72; *b = 48; break;
    }
}

static void road_color(int band, int *r, int *g, int *b) {
    int alt = band & 1;
    switch ((band >> 3) & 3) {
    case 2: *r = 92 + alt * 10; *g = 80; *b = 64; break;
    case 1: *r = 58; *g = 52; *b = 70 + alt * 8; break;
    default: *r = 52 + alt * 12; *g = 52 + alt * 12; *b = 58 + alt * 8; break;
    }
}

static void draw_ground(int scroll) {
    int z = CLIP;
    int half = LANE / 2;
    while (z < FAR) {
        int z1 = z + z / 5;
        int band, rr, gg, bb;
        if (z1 < z + 24)
            z1 = z + 24;
        if (z1 > FAR)
            z1 = FAR;
        band = (scroll + z) / STEP;
        road_color(band, &rr, &gg, &bb);
        flat(-ROAD_HALF, z, ROAD_HALF, z1, 0, rr, gg, bb);
        shoulder_color(band, &rr, &gg, &bb);
        flat(-ROAD_HALF - SHOULDER, z, -ROAD_HALF, z1, 0, rr, gg, bb);
        flat(ROAD_HALF, z, ROAD_HALF + SHOULDER, z1, 0, rr, gg, bb);
        if (((scroll + z) / 128 & 1) == 0) {
            int zmid = z + (z1 - z) / 2;
            flat(-half - 8, z, -half + 8, zmid, 2, 220, 190, 48);
            flat(half - 8, z, half + 8, zmid, 2, 220, 190, 48);
        }
        z = z1;
    }
}

static void city_draw(const LevelFrame *f) {
    int scroll = f->scroll;
    int band0 = (scroll + CLIP) / STEP;
    int band1 = (scroll + FAR) / STEP;
    int band;
    draw_ground(scroll);
    if (band1 > band0 + 20)
        band1 = band0 + 20;
    for (band = band1; band >= band0; band--) {
        int z = band * STEP - scroll;
        if (band < 0)
            continue;
        if (z >= CLIP && z < FAR) {
            building(band, z, -1);
            building(band, z, 1);
            if ((band % 10) == 0 && band > 0)
                gantry(z + 48);
        }
        if (has_obstacle(band)) {
            int s = band * STEP + STEP / 2;
            int oz = s - scroll;
            if (oz >= CLIP && oz < FAR)
                obstacle_box(s, obstacle_u(band), &k_obstacle);
        }
    }
}

const Level level_city = {
    "CITY RUN",
    "straight.run",
    city_clear,
    city_draw,
    &light_neutral,
    0,
};
