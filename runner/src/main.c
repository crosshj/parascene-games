/* A chase camera on a straight road. The runner is one camera-facing
 * sprite. The road and the things beside it are flat polygons. */

#include <stdio.h>
#include <stdint.h>
#include <psxgpu.h>
#include <psxgte.h>
#include <psxpad.h>
#include <psxapi.h>
#include <psxcd.h>
#include <psxspu.h>
#include <inline_c.h>
#include "sprite_sheet.h"

#define SCREEN_W 320
#define SCREEN_H 240
#define OT_LENGTH 512
#define OT_SHIFT 4
#define BUFFER_LENGTH (32 * 1024)

#define CLIP 64
#define STEP 256
#define FAR 4200
#define CAM_H 100
#define CHAR_Z 200
#define LANE 150
#define ROAD_HALF 280
#define SHOULDER 640

enum {
    ST_MENU,
    ST_RUN,
    ST_HIT
};

typedef struct {
    DISPENV disp;
    DRAWENV draw;
    uint32_t ot[OT_LENGTH];
    uint8_t buffer[BUFFER_LENGTH];
} RenderBuffer;

typedef struct {
    RenderBuffer buffers[2];
    uint8_t *next;
    int active;
} RenderContext;

typedef struct {
    int16_t x, y;
} __attribute__((aligned(4))) XY;

static RenderContext ctx;
static uint8_t pad_buff[2][34];
static uint16_t gpu_tpage[SPR_PAGE_COUNT];
static uint16_t gpu_clut[SPR_CHAR_COUNT];

static int state = ST_MENU;
static int scroll;
static int lane;
static int character;
static int anim_tick;
static int jump_wind;
static int land_hold;
static int player_x, player_y, player_vy;
static int distance, best, shown;
static int hit_timer;
static uint16_t held, prev;

static int cd_on, cd_heard, cd_cool;

static int iabs(int v) { return v < 0 ? -v : v; }

static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint16_t pad_held(void) {
    PADTYPE *pad = (PADTYPE *)pad_buff[0];
    uint16_t b = 0;
    uint16_t w;
    if (pad->stat)
        return 0;
    if (pad->type != PAD_ID_DIGITAL && pad->type != PAD_ID_ANALOG &&
        pad->type != PAD_ID_ANALOG_STICK)
        return 0;
    w = pad->btn;
    if (!(w & PAD_LEFT)) b |= PAD_LEFT;
    if (!(w & PAD_RIGHT)) b |= PAD_RIGHT;
    if (!(w & PAD_CROSS)) b |= PAD_CROSS;
    if (!(w & PAD_START)) b |= PAD_START;
    if (pad->type != PAD_ID_DIGITAL) {
        if (pad->ls_x < 64) b |= PAD_LEFT;
        if (pad->ls_x > 192) b |= PAD_RIGHT;
    }
    return b;
}

static int edge(uint16_t bit) {
    return (held & bit) && !(prev & bit);
}

static void *alloc_prim(int z, size_t size) {
    RenderBuffer *buf = &ctx.buffers[ctx.active];
    uint8_t *prim;
    if (z < 1) z = 1;
    if (z >= OT_LENGTH) z = OT_LENGTH - 1;
    if (ctx.next + size > buf->buffer + BUFFER_LENGTH)
        return 0;
    prim = ctx.next;
    addPrim(&buf->ot[z], prim);
    ctx.next += size;
    return prim;
}

static int project(int x, int y, int z, XY *out) {
    SVECTOR v __attribute__((aligned(4)));
    int sz = 0;
    v.vx = (short)clamp(x, -16000, 16000);
    v.vy = (short)clamp(CAM_H - y, -16000, 16000);
    v.vz = (short)z;
    v.pad = 0;
    gte_ldv0(&v);
    gte_rtps();
    gte_stsxy(out);
    gte_stsz(&sz);
    return sz;
}

static void dim(int *r, int *g, int *b, int z) {
    int s = 280 - z / 16;
    if (s < 72) s = 72;
    if (s > 255) s = 255;
    *r = (*r * s) >> 8;
    *g = (*g * s) >> 8;
    *b = (*b * s) >> 8;
}

/* Vertices past this screen range make the GPU drop a stray triangle. */
#define SAFE_X0 (-200)
#define SAFE_X1 (SCREEN_W + 200)

static int xy_ok(const XY *p) {
    return p->x >= -1000 && p->x <= 1000 && p->y >= -1000 && p->y <= 1000;
}

static int edge_ok(XY a, XY b) {
    int dx = iabs(a.x - b.x);
    int dy = iabs(a.y - b.y);
    return (dx > dy ? dx : dy) <= 480;
}

static void quad(int x0, int y0, int z0, int x1, int y1, int z1,
                 int x2, int y2, int z2, int x3, int y3, int z3,
                 int sortz, int r, int g, int b) {
    XY p0, p1, p2, p3;
    int bucket;
    POLY_F4 *poly;
    if (z0 < 8 || z1 < 8 || z2 < 8 || z3 < 8)
        return;
    project(x0, y0, z0, &p0);
    project(x1, y1, z1, &p1);
    project(x2, y2, z2, &p2);
    project(x3, y3, z3, &p3);
    if (!xy_ok(&p0) || !xy_ok(&p1) || !xy_ok(&p2) || !xy_ok(&p3))
        return;
    if (!edge_ok(p0, p1) || !edge_ok(p2, p3) || !edge_ok(p0, p2) || !edge_ok(p1, p3))
        return;
    bucket = sortz >> OT_SHIFT;
    poly = alloc_prim(bucket, sizeof(POLY_F4));
    if (!poly)
        return;
    setPolyF4(poly);
    setRGB0(poly, clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255));
    setXY4(poly, p0.x, p0.y, p1.x, p1.y, p2.x, p2.y, p3.x, p3.y);
}

static void x_limits(int z, int *lo, int *hi) {
    if (z < 1)
        z = 1;
    *lo = (SAFE_X0 - SCREEN_W / 2) * z / (SCREEN_W / 2);
    *hi = (SAFE_X1 - SCREEN_W / 2) * z / (SCREEN_W / 2);
}

/* A horizontal quad. sort_far keeps a ground strip behind objects standing on it. */
static void slab(int xa, int z0, int xb, int z1, int y, int r, int g, int b, int sort_far) {
    int znear, zfar, left, right;
    int nlo, nhi, flo, fhi, nl, nr, fl, fr;
    int span, step, slices, i;
    if (z0 < z1) { znear = z0; zfar = z1; }
    else { znear = z1; zfar = z0; }
    if (xa < xb) { left = xa; right = xb; }
    else { left = xb; right = xa; }
    if (znear < 8 || left >= right)
        return;
    x_limits(znear, &nlo, &nhi);
    x_limits(zfar, &flo, &fhi);
    nl = clamp(left, nlo, nhi);
    nr = clamp(right, nlo, nhi);
    fl = clamp(left, flo, fhi);
    fr = clamp(right, flo, fhi);
    if (nl >= nr || fl >= fr)
        return;
    dim(&r, &g, &b, znear);
    span = nr - nl;
    step = znear / 2;
    if (step < 16)
        step = 16;
    slices = span / step;
    if (slices < 1)
        slices = 1;
    if (slices > 16)
        slices = 16;
    for (i = 0; i < slices; i++) {
        int x0 = nl + span * i / slices;
        int x1 = nl + span * (i + 1) / slices;
        int u0 = fl + (fr - fl) * i / slices;
        int u1 = fl + (fr - fl) * (i + 1) / slices;
        quad(x0, y, znear, x1, y, znear, u0, y, zfar, u1, y, zfar,
             sort_far ? zfar : znear, r, g, b);
    }
}

static void flat(int xa, int z0, int xb, int z1, int y, int r, int g, int b) {
    slab(xa, z0, xb, z1, y, r, g, b, 1);
}

static void lid(int xa, int z0, int xb, int z1, int y, int r, int g, int b) {
    slab(xa, z0, xb, z1, y, r, g, b, 0);
}

static void wall(int xa, int xb, int z, int ya, int yb, int r, int g, int b) {
    int left, right, y0, y1, lo, hi, step, x;
    if (z < 8)
        return;
    if (xa < xb) { left = xa; right = xb; }
    else { left = xb; right = xa; }
    if (ya < yb) { y0 = ya; y1 = yb; }
    else { y0 = yb; y1 = ya; }
    x_limits(z, &lo, &hi);
    left = clamp(left, lo, hi);
    right = clamp(right, lo, hi);
    if (left >= right || y0 == y1)
        return;
    dim(&r, &g, &b, z);
    step = z / 2;
    if (step < 16)
        step = 16;
    for (x = left; x < right; ) {
        int x2 = x + step;
        if (x2 > right)
            x2 = right;
        quad(x, y0, z, x2, y0, z, x, y1, z, x2, y1, z, z, r, g, b);
        x = x2;
    }
}

static void side(int x, int z0, int z1, int ya, int yb, int r, int g, int b) {
    int znear, zfar, y0, y1, z, step;
    if (z0 < z1) { znear = z0; zfar = z1; }
    else { znear = z1; zfar = z0; }
    if (ya < yb) { y0 = ya; y1 = yb; }
    else { y0 = yb; y1 = ya; }
    while (znear < zfar) {
        int lo, hi;
        x_limits(znear, &lo, &hi);
        if (x >= lo && x <= hi)
            break;
        znear += 16;
    }
    if (znear >= zfar)
        return;
    dim(&r, &g, &b, znear);
    step = 80;
    for (z = znear; z < zfar; ) {
        int z2 = z + step;
        int lo, hi;
        if (z2 > zfar)
            z2 = zfar;
        x_limits(z2, &lo, &hi);
        if (x < lo || x > hi)
            break;
        quad(x, y0, z, x, y0, z2, x, y1, z, x, y1, z2, z, r, g, b);
        z = z2;
    }
}

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

static void obstacle(int z, int lane_i) {
    int x, w = 70, h = 76, d = 64;
    if (z < CLIP || z >= FAR)
        return;
    x = lane_i * LANE;
    lid(x - w + 6, z + 6, x + w - 6, z + d, h, 255, 176, 72);
    side(x - w, z, z + d, 0, h, 160, 48, 24);
    side(x + w, z, z + d, 0, h, 160, 48, 24);
    wall(x - w, x + w, z, 0, h, 220, 72, 36);
}

static int obstacle_lane(int band) {
    return ((band / 8) % 3) - 1;
}

static int has_obstacle(int band) {
    return band >= 8 && (band % 8) == 5;
}

static void sky_color(int *r, int *g, int *b) {
    switch ((scroll / (STEP * 8)) & 3) {
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

static void draw_ground(void) {
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
        flat(-ROAD_HALF, z, -ROAD_HALF + 10, z1, 1, 180, 180, 170);
        flat(ROAD_HALF - 10, z, ROAD_HALF, z1, 1, 180, 180, 170);
        if (((scroll + z) / 128 & 1) == 0) {
            int zmid = z + (z1 - z) / 2;
            flat(-half - 8, z, -half + 8, zmid, 2, 220, 190, 48);
            flat(half - 8, z, half + 8, zmid, 2, 220, 190, 48);
        }
        z = z1;
    }
}

static void draw_world(void) {
    int band0 = (scroll + CLIP) / STEP;
    int band1 = (scroll + FAR) / STEP;
    int band;
    draw_ground();
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
            int oz = band * STEP + STEP / 2 - scroll;
            if (oz >= CLIP && oz < FAR)
                obstacle(oz, obstacle_lane(band));
        }
    }
}

static void draw_shadow(void) {
    int w = 34 - player_y / 8;
    int z0 = CHAR_Z - 18;
    int z1 = CHAR_Z + 18;
    if (w < 8)
        w = 8;
    flat(player_x - w, z0, player_x + w, z1, 3, 16, 16, 20);
}

static int anim_count(int anim) {
    return spr_base_anim_count[spr_char_base[character]][anim];
}

static int play_frame(int anim, int index) {
    int base = spr_char_base[character];
    int n = spr_base_anim_count[base][anim];
    if (n < 1)
        return 0;
    if (index < 0)
        index = 0;
    if (index >= n)
        index = n - 1;
    return spr_anim_frames[spr_base_list_off[base] + spr_base_anim_begin[base][anim] + index];
}

static int posed_frame(void) {
    int dist, n, phase;
    if (jump_wind > 4)
        return play_frame(SPR_ANIM_JUMP, 0);
    if (jump_wind > 0)
        return play_frame(SPR_ANIM_JUMP, 1);
    if (player_y > 0) {
        if (player_vy > 11)
            phase = 2;
        else if (player_vy > 6)
            phase = 3;
        else if (player_vy > 1)
            phase = 4;
        else if (player_vy > -4)
            phase = 5;
        else if (player_y > 24)
            phase = 6;
        else
            phase = 7;
        return play_frame(SPR_ANIM_JUMP, phase);
    }
    if (land_hold > 0)
        return play_frame(SPR_ANIM_JUMP, anim_count(SPR_ANIM_JUMP) - 1);
    dist = lane * LANE - player_x;
    if (dist > 8) {
        n = anim_count(SPR_ANIM_RIGHT);
        return play_frame(SPR_ANIM_RIGHT, (LANE - dist) * n / LANE);
    }
    if (dist < -8) {
        n = anim_count(SPR_ANIM_LEFT);
        return play_frame(SPR_ANIM_LEFT, (LANE + dist) * n / LANE);
    }
    return play_frame(SPR_ANIM_RUN, (anim_tick / 4) % anim_count(SPR_ANIM_RUN));
}

static void draw_runner(void) {
    XY feet;
    int sz, h, w, frame, bucket;
    POLY_FT4 *spr;
    sz = project(player_x, player_y, CHAR_Z, &feet);
    if (sz < 8)
        return;
    h = (78 * (SCREEN_W / 2)) / sz;
    if (h < 8)
        h = 8;
    w = h * spr_base_fw[spr_char_base[character]] / spr_base_fh[spr_char_base[character]];
    frame = posed_frame();
    bucket = CHAR_Z >> OT_SHIFT;
    spr = alloc_prim(bucket, sizeof(POLY_FT4));
    if (!spr)
        return;
    setPolyFT4(spr);
    setRGB0(spr, 128, 128, 128);
    setXY4(spr,
        feet.x - w / 2, feet.y - h,
        feet.x + w / 2, feet.y - h,
        feet.x - w / 2, feet.y,
        feet.x + w / 2, feet.y);
    setUVWH(spr,
        spr_frame_u[spr_base_uv_off[spr_char_base[character]] + frame],
        spr_char_v[character] + spr_frame_v[spr_base_uv_off[spr_char_base[character]] + frame],
        spr_base_fw[spr_char_base[character]] - 1,
        spr_base_fh[spr_char_base[character]] - 1);
    spr->tpage = gpu_tpage[spr_char_page[character]];
    spr->clut = gpu_clut[character];
}

static void hud(int x, int y, const char *s) {
    RenderBuffer *buf = &ctx.buffers[ctx.active];
    int glyphs = 0;
    const char *p;
    for (p = s; *p; p++) {
        if (*p != ' ' && *p != '\n')
            glyphs++;
    }
    if (ctx.next + glyphs * sizeof(SPRT_8) + sizeof(DR_TPAGE) > buf->buffer + BUFFER_LENGTH)
        return;
    ctx.next = FntSort(&buf->ot[0], ctx.next, x, y, s);
}

static void hud_center(int y, const char *s) {
    int n = 0;
    while (s[n])
        n++;
    hud((SCREEN_W - n * 8) / 2, y, s);
}

static void draw_ui(void) {
    char line[80];
    TILE *bar;
    if (state == ST_RUN) {
        bar = alloc_prim(1, sizeof(TILE));
        if (bar) {
            setTile(bar);
            setXY0(bar, 0, 0);
            setWH(bar, SCREEN_W, 22);
            setRGB0(bar, 8, 12, 24);
        }
        snprintf(line, sizeof line, "RUN %d     X JUMP   START MENU", distance);
        hud(8, 8, line);
        return;
    }
    bar = alloc_prim(1, sizeof(TILE));
    if (bar) {
        setTile(bar);
        setXY0(bar, 28, 24);
        setWH(bar, 264, shown ? 112 : 104);
        setRGB0(bar, 8, 12, 28);
    }
    snprintf(line, sizeof line, "< %s >", spr_char_name[character]);
    if (shown) {
        hud_center(36, "WIPED OUT");
        snprintf(line, sizeof line, "DISTANCE %d   BEST %d", shown, best);
        hud_center(52, line);
        snprintf(line, sizeof line, "< %s >", spr_char_name[character]);
        hud_center(72, line);
        hud_center(92, "START OR X TO RUN");
    } else {
        hud_center(32, "RUNNER");
        hud_center(48, line);
        hud(56, 68, "LEFT RIGHT  CHARACTER");
        hud(56, 84, "START OR X   RUN");
        hud(56, 100, "X            JUMP");
    }
}

static void collide(void) {
    int pz = scroll + CHAR_Z;
    int b0 = (pz - STEP) / STEP;
    int b;
    if (b0 < 0)
        b0 = 0;
    for (b = b0; b <= b0 + 2; b++) {
        int oz, x;
        if (!has_obstacle(b))
            continue;
        oz = b * STEP + STEP / 2;
        if (iabs(oz - pz) > 60)
            continue;
        x = obstacle_lane(b) * LANE;
        if (iabs(player_x - x) < 80 && player_y < 72) {
            state = ST_HIT;
            hit_timer = 50;
            shown = distance;
            if (distance > best)
                best = distance;
            return;
        }
    }
}

static void update(void) {
    int speed = 0;
    int target;
    int dx;
    held = pad_held();
    if (state != ST_RUN && SPR_CHAR_COUNT > 1) {
        if (edge(PAD_LEFT))
            character = (character + SPR_CHAR_COUNT - 1) % SPR_CHAR_COUNT;
        if (edge(PAD_RIGHT))
            character = (character + 1) % SPR_CHAR_COUNT;
    }
    if (state == ST_MENU) {
        speed = 8;
        if (edge(PAD_START) || edge(PAD_CROSS)) {
            state = ST_RUN;
            scroll = 0;
            lane = 0;
            player_x = 0;
            player_y = 0;
            player_vy = 0;
            jump_wind = 0;
            land_hold = 0;
            distance = 0;
        }
    } else if (state == ST_RUN) {
        speed = 20;
        if (edge(PAD_START)) {
            state = ST_MENU;
            shown = 0;
            jump_wind = 0;
            land_hold = 0;
        }
        if (edge(PAD_LEFT) && !edge(PAD_RIGHT) && lane > -1)
            lane--;
        if (edge(PAD_RIGHT) && !edge(PAD_LEFT) && lane < 1)
            lane++;
        if (edge(PAD_CROSS) && player_y == 0 && jump_wind == 0) {
            jump_wind = 8;
            land_hold = 0;
        } else if (jump_wind > 0) {
            jump_wind--;
            if (jump_wind == 0)
                player_vy = 15;
        }
        distance = scroll / 48;
        collide();
    } else {
        if (hit_timer > 0)
            hit_timer--;
        else {
            state = ST_MENU;
            jump_wind = 0;
            land_hold = 0;
        }
    }
    target = lane * LANE;
    dx = target - player_x;
    if (dx > 12) player_x += 12;
    else if (dx < -12) player_x -= 12;
    else player_x = target;
    if (state == ST_RUN && player_y == 0 && land_hold > 0)
        land_hold--;
    {
        int was_up = player_y > 0;
        player_y += player_vy;
        if (player_vy > -20)
            player_vy--;
        if (player_y <= 0) {
            if (was_up && state == ST_RUN)
                land_hold = 8;
            player_y = 0;
            player_vy = 0;
        }
    }
    anim_tick++;
    scroll += speed;
    prev = held;
}

static void upload_sprite(void) {
    int i;
    RECT rect;
    for (i = 0; i < SPR_PAGE_COUNT; i++) {
        setRECT(&rect, spr_page_x[i], spr_page_y[i], SPR_PAGE_W / 4, spr_page_h[i]);
        LoadImage(&rect, (const uint32_t *)(spr_tex + spr_page_off[i]));
        DrawSync(0);
        gpu_tpage[i] = getTPage(0, 0, spr_page_x[i], spr_page_y[i]);
    }
    for (i = 0; i < SPR_CHAR_COUNT; i++) {
        setRECT(&rect, spr_clut_x[i], spr_clut_y[i], 16, 1);
        LoadImage(&rect, (const uint32_t *)spr_clut[i]);
        DrawSync(0);
        gpu_clut[i] = getClut(spr_clut_x[i], spr_clut_y[i]);
    }
}

static void setup_video(void) {
    int i;
    ResetGraph(0);
    for (i = 0; i < 2; i++) {
        SetDefDrawEnv(&ctx.buffers[i].draw, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        SetDefDispEnv(&ctx.buffers[i].disp, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        setRGB0(&ctx.buffers[i].draw, 48, 88, 148);
        ctx.buffers[i].draw.isbg = 1;
        ctx.buffers[i].draw.dtd = 0;
    }
    ctx.active = 0;
    ctx.next = ctx.buffers[0].buffer;
    ClearOTagR(ctx.buffers[0].ot, OT_LENGTH);
    FntLoad(960, 0);
    InitGeom();
    gte_SetGeomOffset(SCREEN_W / 2, SCREEN_H / 2);
    gte_SetGeomScreen(SCREEN_W / 2);
    {
        static SVECTOR rot __attribute__((aligned(4)));
        static VECTOR pos;
        MATRIX mtx;
        RotMatrix(&rot, &mtx);
        TransMatrix(&mtx, &pos);
        gte_SetRotMatrix(&mtx);
        gte_SetTransMatrix(&mtx);
    }
    upload_sprite();
    SetDispMask(1);
}

static int cd_play(void) {
    uint8_t mode = CdlModeDA;
    uint8_t track = itob(2);
    uint8_t result[8];
    CdlLOC loc;
    CdlATV mix;
    if (!CdControlB(CdlSetmode, &mode, 0))
        return 0;
    if (!CdCommand(CdlGetTD, &track, 1, result))
        return 0;
    if (CdSync(1, 0) != CdlComplete)
        return 0;
    loc.minute = result[1];
    loc.second = result[2];
    loc.sector = 0;
    loc.track = track;
    if (!CdControlB(CdlSetloc, &loc, 0))
        return 0;
    if (!CdControlB(CdlPlay, 0, 0))
        return 0;
    CdControl(CdlDemute, 0, 0);
    mix.val0 = 128;
    mix.val1 = 0;
    mix.val2 = 128;
    mix.val3 = 0;
    CdMix(&mix);
    SpuSetCommonMasterVolume(0x3fff, 0x3fff);
    SpuSetCommonCDVolume(0x3fff, 0x3fff);
    return 1;
}

static void cd_service(void) {
    int st;
    if (!cd_on)
        return;
    if (cd_cool > 0) {
        cd_cool--;
        return;
    }
    st = CdStatus();
    if (st & CdlStatPlay) {
        cd_heard = 1;
        return;
    }
    if (st & CdlStatSeek)
        return;
    if (!cd_heard)
        return;
    cd_heard = 0;
    if (cd_play())
        cd_cool = 90;
}

static void flip(void) {
    RenderBuffer *draw = &ctx.buffers[ctx.active];
    RenderBuffer *disp = &ctx.buffers[ctx.active ^ 1];
    DrawSync(0);
    VSync(0);
    PutDispEnv(&disp->disp);
    DrawOTagEnv(&draw->ot[OT_LENGTH - 1], &draw->draw);
    ctx.active ^= 1;
    ctx.next = disp->buffer;
    ClearOTagR(disp->ot, OT_LENGTH);
}

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;
    setup_video();
    SpuInit();
    InitPAD(pad_buff[0], 34, pad_buff[1], 34);
    StartPAD();
    ChangeClearPAD(0);
    cd_on = CdInit();
    if (cd_on && cd_play())
        cd_cool = 90;

    for (;;) {
        int sr, sg, sb;
        update();
        cd_service();
        sky_color(&sr, &sg, &sb);
        setRGB0(&ctx.buffers[ctx.active].draw, sr, sg, sb);
        draw_world();
        draw_shadow();
        draw_runner();
        draw_ui();
        flip();
    }
    return 0;
}
