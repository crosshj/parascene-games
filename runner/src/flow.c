#include "flow.h"

#include <psxgpu.h>
#include <psxgte.h>
#include "level.h"
#include "world.h"

/* Icons sit this many pixels apart along the row. */
#define SPAN 104
/* The islands sit on this line. The card hangs below it, and the pair is centered. */
#define ROW_Y 84

static const Level *story_at(int slot) {
    int i, n = 0;
    for (i = 0; i < level_count; i++) {
        if (!levels[i]->story)
            continue;
        if (n == slot)
            return levels[i];
        n++;
    }
    return levels[0];
}

int flow_world_count(void) {
    int i, n = 0;
    for (i = 0; i < level_count; i++)
        if (levels[i]->story)
            n++;
    return n;
}

int flow_base(int slot) {
    int i, n = 0, base = 0;
    if (slot < 0)
        return 0;
    for (i = 0; i < level_count; i++) {
        if (!levels[i]->story)
            continue;
        if (n == slot)
            return base;
        base += levels[i]->stage_count;
        n++;
    }
    return base;
}

int flow_total(void) {
    return flow_base(flow_world_count());
}

int flow_stages(int slot) {
    return story_at(slot)->stage_count;
}

int flow_slot_for(int global) {
    int s, n = flow_world_count();
    for (s = n - 1; s > 0; s--)
        if (flow_base(s) <= global)
            return s;
    return 0;
}

/* 2 cleared, 1 open, 0 still locked. */
static int stage_look(int global, int reach) {
    if (global < reach - 1)
        return 2;
    if (global < reach)
        return 1;
    return 0;
}

static int icon_x(int slot, int pos) {
    return SCREEN_W / 2 + (((slot << 8) - pos) * SPAN >> 8);
}

/* Proportional letters. Width, then seven rows. The high bit of the width
 * is the left pixel. Drawn as flat rectangles, one run at a time. */
#define GLYPH_H 7
static const uint8_t k_glyph[] = {
    5, 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, /* A */
    5, 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e, /* B */
    5, 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e, /* C */
    5, 0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e, /* D */
    5, 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f, /* E */
    5, 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10, /* F */
    5, 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e, /* G */
    5, 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11, /* H */
    3, 0x07, 0x02, 0x02, 0x02, 0x02, 0x02, 0x07, /* I */
    5, 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c, /* J */
    5, 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, /* K */
    4, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0f, /* L */
    5, 0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x11, /* M */
    5, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11, /* N */
    5, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, /* O */
    5, 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10, /* P */
    5, 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d, /* Q */
    5, 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11, /* R */
    5, 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e, /* S */
    5, 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, /* T */
    5, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e, /* U */
    5, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04, /* V */
    5, 0x11, 0x11, 0x11, 0x15, 0x15, 0x1b, 0x11, /* W */
    5, 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11, /* X */
    5, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04, /* Y */
    5, 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f, /* Z */
    1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01  /* . */
};

static int glyph_of(int c) {
    if (c >= 'a' && c <= 'z')
        c -= 32;
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c == '.')
        return 26;
    return -1;
}

static const uint8_t *glyph_at(int i) {
    return k_glyph + i * (GLYPH_H + 1);
}

static int hud_width(const char *s, int scale) {
    int w = 0;
    for (; *s; s++) {
        int i = glyph_of(*s);
        if (*s == ' ') {
            w += 3 * scale;
            continue;
        }
        if (i >= 0)
            w += (glyph_at(i)[0] + 1) * scale;
    }
    return w;
}

static void hud_glyph(int x, int y, const uint8_t *g, int scale, int r, int gg, int b) {
    int row, col;
    int w = g[0];
    for (row = 0; row < GLYPH_H; row++) {
        int bits = g[1 + row];
        int run = -1;
        for (col = 0; col <= w; col++) {
            int on = col < w && ((bits >> (w - 1 - col)) & 1);
            if (on && run < 0)
                run = col;
            if (!on && run >= 0) {
                fill_rect(x + run * scale, y + row * scale,
                          (col - run) * scale, scale, r, gg, b);
                run = -1;
            }
        }
    }
}

static void hud_text(int x, int y, const char *s, int scale, int r, int g, int b) {
    const char *p;
    int sx = x;
    if (scale < 1)
        scale = 1;
    for (p = s; *p; p++) {
        int i;
        if (*p == ' ') {
            sx += 3 * scale;
            continue;
        }
        i = glyph_of(*p);
        if (i < 0)
            continue;
        hud_glyph(sx, y, glyph_at(i), scale, r, g, b);
        sx += (glyph_at(i)[0] + 1) * scale;
    }
}

static void hud_center(int y, const char *s, int scale, int r, int g, int b) {
    hud_text((SCREEN_W - hud_width(s, scale)) / 2, y, s, scale, r, g, b);
}

/* Half the label face: four rows, for the names under the islands. */
#define MINI_H 4
static const uint8_t k_mini[] = {
    3, 0x02, 0x05, 0x07, 0x05, /* A */
    3, 0x06, 0x05, 0x06, 0x07, /* B */
    3, 0x07, 0x04, 0x04, 0x07, /* C */
    3, 0x06, 0x05, 0x05, 0x06, /* D */
    3, 0x07, 0x06, 0x04, 0x07, /* E */
    3, 0x07, 0x06, 0x04, 0x04, /* F */
    3, 0x06, 0x04, 0x05, 0x07, /* G */
    3, 0x05, 0x07, 0x05, 0x05, /* H */
    1, 0x01, 0x01, 0x01, 0x01, /* I */
    3, 0x03, 0x01, 0x05, 0x02, /* J */
    3, 0x05, 0x06, 0x02, 0x05, /* K */
    2, 0x02, 0x02, 0x02, 0x03, /* L */
    4, 0x09, 0x0f, 0x09, 0x09, /* M */
    3, 0x05, 0x07, 0x07, 0x05, /* N */
    3, 0x07, 0x05, 0x05, 0x07, /* O */
    3, 0x06, 0x07, 0x04, 0x04, /* P */
    3, 0x07, 0x05, 0x07, 0x01, /* Q */
    3, 0x06, 0x07, 0x05, 0x05, /* R */
    3, 0x07, 0x06, 0x03, 0x07, /* S */
    3, 0x07, 0x02, 0x02, 0x02, /* T */
    3, 0x05, 0x05, 0x05, 0x07, /* U */
    3, 0x05, 0x05, 0x05, 0x02, /* V */
    4, 0x09, 0x09, 0x0b, 0x05, /* W */
    3, 0x05, 0x02, 0x02, 0x05, /* X */
    3, 0x05, 0x02, 0x02, 0x02, /* Y */
    3, 0x07, 0x03, 0x06, 0x07  /* Z */
};

static const uint8_t *mini_at(int i) {
    return k_mini + i * (MINI_H + 1);
}

static int mini_width(const char *s) {
    int w = 0;
    for (; *s; s++) {
        int i = glyph_of(*s);
        if (*s == ' ') {
            w += 2;
            continue;
        }
        if (i >= 0 && i < 26)
            w += mini_at(i)[0] + 1;
    }
    return w;
}

static void mini_text(int x, int y, const char *s, int r, int g, int b) {
    const char *p;
    int sx = x;
    for (p = s; *p; p++) {
        int i, row, col, w;
        const uint8_t *glyph;
        if (*p == ' ') {
            sx += 2;
            continue;
        }
        i = glyph_of(*p);
        if (i < 0 || i > 25)
            continue;
        glyph = mini_at(i);
        w = glyph[0];
        for (row = 0; row < MINI_H; row++) {
            int bits = glyph[1 + row];
            int run = -1;
            for (col = 0; col <= w; col++) {
                int on = col < w && ((bits >> (w - 1 - col)) & 1);
                if (on && run < 0)
                    run = col;
                if (!on && run >= 0) {
                    fill_rect(sx + run, y + row, col - run, 1, r, g, b);
                    run = -1;
                }
            }
        }
        sx += w + 1;
    }
}

/* Bold face for the title and the card name. Eight rows, two-pixel strokes.
 * Width, then the rows. The high bit of the width is the left pixel. */
#define DISP_H 8
static const uint16_t k_disp[] = {
    8, 0x003c, 0x007e, 0x00c3, 0x00c3, 0x00ff, 0x00ff, 0x00c3, 0x00c3, /* A */
    8, 0x00fc, 0x00fe, 0x00c3, 0x00fc, 0x00fc, 0x00c3, 0x00fe, 0x00fc, /* B */
    8, 0x007e, 0x00ff, 0x00c3, 0x00c0, 0x00c0, 0x00c3, 0x00ff, 0x007e, /* C */
    8, 0x00fc, 0x00fe, 0x00c3, 0x00c3, 0x00c3, 0x00c3, 0x00fe, 0x00fc, /* D */
    8, 0x00ff, 0x00ff, 0x00c0, 0x00fc, 0x00fc, 0x00c0, 0x00ff, 0x00ff, /* E */
    8, 0x00ff, 0x00ff, 0x00c0, 0x00fc, 0x00fc, 0x00c0, 0x00c0, 0x00c0, /* F */
    8, 0x007e, 0x00ff, 0x00c0, 0x00c0, 0x00cf, 0x00c3, 0x00ff, 0x007e, /* G */
    8, 0x00c3, 0x00c3, 0x00c3, 0x00ff, 0x00ff, 0x00c3, 0x00c3, 0x00c3, /* H */
    6, 0x003f, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x000c, 0x003f, /* I */
    8, 0x003f, 0x0006, 0x0006, 0x0006, 0x00c6, 0x00c6, 0x007e, 0x003c, /* J */
    8, 0x00c3, 0x00c6, 0x00cc, 0x00f0, 0x00f0, 0x00cc, 0x00c6, 0x00c3, /* K */
    6, 0x0030, 0x0030, 0x0030, 0x0030, 0x0030, 0x0030, 0x003f, 0x003f, /* L */
    9, 0x0183, 0x01c7, 0x01ef, 0x01bb, 0x0183, 0x0183, 0x0183, 0x0183, /* M */
    8, 0x00c3, 0x00e3, 0x00f3, 0x00db, 0x00cf, 0x00c7, 0x00c3, 0x00c3, /* N */
    8, 0x007e, 0x00ff, 0x00c3, 0x00c3, 0x00c3, 0x00c3, 0x00ff, 0x007e, /* O */
    8, 0x00fe, 0x00ff, 0x00c3, 0x00ff, 0x00fe, 0x00c0, 0x00c0, 0x00c0, /* P */
    8, 0x007e, 0x00ff, 0x00c3, 0x00c3, 0x00cb, 0x00c6, 0x00ff, 0x007d, /* Q */
    8, 0x00fe, 0x00ff, 0x00c3, 0x00ff, 0x00f8, 0x00cc, 0x00c6, 0x00c3, /* R */
    8, 0x007e, 0x00ff, 0x00c0, 0x007c, 0x001e, 0x0003, 0x00ff, 0x007e, /* S */
    8, 0x00ff, 0x00ff, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018, 0x0018, /* T */
    8, 0x00c3, 0x00c3, 0x00c3, 0x00c3, 0x00c3, 0x00c3, 0x00ff, 0x007e, /* U */
    8, 0x00c3, 0x00c3, 0x00c3, 0x00c3, 0x0066, 0x0066, 0x003c, 0x0018, /* V */
    9, 0x0183, 0x0183, 0x0183, 0x0193, 0x0193, 0x01ab, 0x00ee, 0x006c, /* W */
    8, 0x00c3, 0x00c3, 0x0066, 0x003c, 0x003c, 0x0066, 0x00c3, 0x00c3, /* X */
    8, 0x00c3, 0x00c3, 0x0066, 0x003c, 0x0018, 0x0018, 0x0018, 0x0018, /* Y */
    8, 0x00ff, 0x00ff, 0x0006, 0x000c, 0x0018, 0x0030, 0x00ff, 0x00ff  /* Z */
};

static const uint16_t *disp_at(int i) {
    return k_disp + i * (DISP_H + 1);
}

/* Eight source rows drawn 10 pixels tall. Rows 0 and 4 take the extra pixel. */
static int step_32(int i) {
    return (i == 0 || i == 4) ? 2 : 1;
}

static int span_32(int n) {
    int i, w = 0;
    for (i = 0; i < n; i++)
        w += step_32(i);
    return w;
}

static int disp_width_32(const char *s) {
    int w = 0;
    int prev = 0;
    for (; *s; s++) {
        int adv;
        if (*s == ' ') {
            adv = 5;
        } else {
            int i = glyph_of(*s);
            if (i < 0 || i > 25)
                continue;
            adv = span_32(disp_at(i)[0]);
        }
        if (prev)
            w += 1;
        w += adv;
        prev = 1;
    }
    return w;
}

static void disp_glyph_32(int x, int y, const uint16_t *g, int r, int gg, int b) {
    int row, col;
    int w = g[0];
    for (row = 0; row < DISP_H; row++) {
        int bits = g[1 + row];
        int run = -1;
        for (col = 0; col <= w; col++) {
            int on = col < w && ((bits >> (w - 1 - col)) & 1);
            if (on && run < 0)
                run = col;
            if (!on && run >= 0) {
                fill_rect(x + span_32(run), y + span_32(row),
                          span_32(col) - span_32(run), step_32(row), r, gg, b);
                run = -1;
            }
        }
    }
}

static void disp_text_32(int x, int y, const char *s, int r, int g, int b) {
    const char *p;
    int sx = x;
    int prev = 0;
    for (p = s; *p; p++) {
        int adv;
        if (prev)
            sx += 1;
        if (*p == ' ') {
            adv = 5;
        } else {
            int i = glyph_of(*p);
            if (i < 0 || i > 25)
                continue;
            disp_glyph_32(sx, y, disp_at(i), r, g, b);
            adv = span_32(disp_at(i)[0]);
        }
        sx += adv;
        prev = 1;
    }
}

static int tint(int c, int shade) {
    return (c * shade + 96 * (256 - shade)) >> 8;
}

typedef struct {
    int x, y, z;
} P3;

typedef struct {
    int cx, cy, sc, yaw, bucket, shade;
} Icon;

static void spin(int yaw, P3 p, P3 *o) {
    int c = icos(yaw);
    int s = isin(yaw);
    o->x = (int)(((long)p.x * c - (long)p.z * s) >> 12);
    o->y = p.y;
    o->z = (int)(((long)p.x * s + (long)p.z * c) >> 12);
}

/* A little pitch, so the lid of a box reads as a top and not a second wall. */
static void project_icon(const Icon *ic, P3 p, XY *o) {
    int depth;
    int py;
    spin(ic->yaw, p, &p);
    depth = 78 + p.z;
    if (depth < 32)
        depth = 32;
    py = p.y - p.z / 2;
    o->x = (short)(ic->cx + p.x * ic->sc / depth);
    o->y = (short)(ic->cy - py * ic->sc / depth);
}

static void quad_face(const Icon *ic, int shade,
                      int x0, int y0, int z0, int x1, int y1, int z1,
                      int x2, int y2, int z2, int x3, int y3, int z3,
                      int r, int g, int b) {
    P3 in[4];
    XY p[4];
    int i;
    in[0].x = x0; in[0].y = y0; in[0].z = z0;
    in[1].x = x1; in[1].y = y1; in[1].z = z1;
    in[2].x = x2; in[2].y = y2; in[2].z = z2;
    in[3].x = x3; in[3].y = y3; in[3].z = z3;
    for (i = 0; i < 4; i++)
        project_icon(ic, in[i], &p[i]);
    poly_f4(&p[0], &p[1], &p[2], &p[3], ic->bucket,
            tint(r, shade), tint(g, shade), tint(b, shade), BLEND_NONE);
}

/* A box. (x, y, z) is the near-left-bottom corner. */
static void blot(const Icon *ic, int x, int y, int z, int w, int h, int d,
                 int r, int g, int b) {
    int x1 = x + w;
    int y1 = y + h;
    int z1 = z + d;
    int s = ic->shade;
    quad_face(ic, s, x, y1, z, x1, y1, z, x, y1, z1, x1, y1, z1, r, g, b);
    quad_face(ic, s, x, y, z, x1, y, z, x, y1, z, x1, y1, z, r * 3 / 4, g * 3 / 4, b * 3 / 4);
    quad_face(ic, s, x1, y, z, x1, y, z1, x1, y1, z, x1, y1, z1, r / 2, g / 2, b / 2);
}

static void icon_lumen(const Icon *ic) {
    blot(ic, -22, -3, -12, 44, 5, 24, 28, 16, 58);
    blot(ic, -16, 2, -8, 30, 6, 16, 52, 22, 92);
    blot(ic, -12, 8, -2, 5, 14, 5, 24, 70, 88);
    blot(ic, -18, 20, -6, 16, 6, 12, 250, 60, 200);
    blot(ic, 4, 8, -1, 4, 10, 4, 10, 64, 80);
    blot(ic, 0, 16, -5, 12, 5, 10, 60, 220, 255);
}

static void icon_vesper(const Icon *ic) {
    blot(ic, -20, -3, -10, 40, 4, 20, 90, 64, 150);
    blot(ic, -16, 1, -4, 8, 22, 8, 70, 40, 130);
    blot(ic, -6, 1, -6, 10, 32, 10, 110, 72, 180);
    blot(ic, 6, 1, -3, 8, 16, 8, 56, 32, 100);
    blot(ic, -4, 28, -4, 6, 8, 6, 120, 230, 255);
}

static void icon_shatter(const Icon *ic) {
    blot(ic, -18, -3, -10, 36, 4, 20, 214, 156, 136);
    blot(ic, -14, 1, -2, 8, 20, 6, 255, 214, 170);
    blot(ic, -4, 1, -6, 7, 28, 7, 255, 244, 220);
    blot(ic, 5, 1, -1, 8, 16, 6, 255, 176, 140);
    blot(ic, -8, 18, -4, 5, 10, 5, 255, 228, 180);
}

static void icon_reach(const Icon *ic) {
    blot(ic, -22, -3, -12, 44, 4, 24, 56, 32, 88);
    blot(ic, -16, 1, -6, 28, 8, 14, 78, 70, 84);
    blot(ic, -8, 9, -4, 16, 12, 10, 120, 96, 72);
    blot(ic, -3, 21, -2, 8, 14, 6, 186, 154, 72);
}

static void icon_sine(const Icon *ic) {
    blot(ic, -20, -3, -10, 40, 4, 20, 8, 22, 26);
    blot(ic, -16, 1, -3, 6, 18, 6, 14, 56, 60);
    blot(ic, 10, 1, -3, 6, 18, 6, 14, 56, 60);
    blot(ic, -16, 16, -3, 32, 5, 6, 32, 220, 210);
    blot(ic, -4, 6, -2, 8, 4, 4, 70, 140, 146);
}

static void icon_meridian(const Icon *ic) {
    blot(ic, -20, -3, -12, 40, 4, 24, 170, 206, 216);
    blot(ic, -14, 1, -8, 28, 5, 16, 186, 214, 222);
    blot(ic, -8, 6, -5, 16, 6, 10, 206, 226, 232);
    blot(ic, -3, 12, -2, 6, 16, 4, 246, 252, 255);
}

static void icon_cathedral(const Icon *ic) {
    blot(ic, -18, -3, -10, 36, 4, 20, 16, 6, 24);
    blot(ic, -14, 1, -3, 6, 20, 6, 90, 24, 150);
    blot(ic, -3, 1, -4, 8, 34, 8, 170, 48, 255);
    blot(ic, 8, 1, -2, 5, 14, 5, 80, 30, 120);
    blot(ic, -1, 35, -2, 4, 8, 4, 210, 180, 255);
}

extern const Level level_lumenwild;
extern const Level level_vesper;
extern const Level level_shatter;
extern const Level level_reach;
extern const Level level_sine;
extern const Level level_meridian;
extern const Level level_cathedral;

static void icon_fn(const Icon *ic, int slot) {
    const Level *lv = story_at(slot);
    if (lv == &level_vesper)
        icon_vesper(ic);
    else if (lv == &level_shatter)
        icon_shatter(ic);
    else if (lv == &level_reach)
        icon_reach(ic);
    else if (lv == &level_sine)
        icon_sine(ic);
    else if (lv == &level_meridian)
        icon_meridian(ic);
    else if (lv == &level_cathedral)
        icon_cathedral(ic);
    else
        icon_lumen(ic);
}

static void diamond(int x, int y, int rad, int r, int g, int b) {
    XY top, right, left, bottom;
    top.x = (short)x;
    top.y = (short)(y - rad);
    right.x = (short)(x + rad);
    right.y = (short)y;
    left.x = (short)(x - rad);
    left.y = (short)y;
    bottom.x = (short)x;
    bottom.y = (short)(y + rad);
    /* Upper triangle is top-right-left, lower is right-left-bottom. */
    poly_f4(&top, &right, &left, &bottom, 2, r, g, b, BLEND_NONE);
}

static void diamond_open(int x, int y, int rad, int r, int g, int bl) {
    XY top, right, left, bottom;
    top.x = (short)x;
    top.y = (short)(y - rad);
    right.x = (short)(x + rad);
    right.y = (short)y;
    left.x = (short)(x - rad);
    left.y = (short)y;
    bottom.x = (short)x;
    bottom.y = (short)(y + rad);
    line_f2(top, right, 2, r, g, bl);
    line_f2(right, bottom, 2, r, g, bl);
    line_f2(bottom, left, 2, r, g, bl);
    line_f2(left, top, 2, r, g, bl);
}

static void gem(int x, int y, int look, int pop, int r, int g, int b) {
    int rad = 3 + pop;
    if (rad < 2)
        rad = 2;
    if (look == 2)
        diamond(x, y, rad, r, g, b);
    else
        diamond_open(x, y, rad, r, g, b);
}

static void padlock(int x, int y, int r, int g, int bl) {
    int left = x - 4;
    /* 8x8. y is the top row. Only the lit pixels are drawn. */
    fill_rect(left + 2, y, 4, 1, r, g, bl);
    fill_rect(left + 1, y + 1, 2, 1, r, g, bl);
    fill_rect(left + 5, y + 1, 2, 1, r, g, bl);
    fill_rect(left, y + 2, 2, 2, r, g, bl);
    fill_rect(left + 6, y + 2, 2, 2, r, g, bl);
    fill_rect(left, y + 4, 8, 1, r, g, bl);
    fill_rect(left, y + 5, 3, 2, r, g, bl);
    fill_rect(left + 5, y + 5, 3, 2, r, g, bl);
    fill_rect(left, y + 7, 8, 1, r, g, bl);
}

static void gem_of(int x, int y, int global, int reach, int glow, int glow_at, int big) {
    int look = stage_look(global, reach);
    int pop = 0;
    int r = 50, g = 58, b = 66;
    if (glow > 0 && global == glow_at && look == 1) {
        int t = 256 - glow * 256 / FLOW_GLOW;
        int half = FLOW_GLOW / 2;
        r = 50 + (170 - 50) * t / 256;
        g = 58 + (236 - 58) * t / 256;
        b = 66 + (220 - 66) * t / 256;
        pop = (glow > half ? FLOW_GLOW - glow : glow) / (big ? 10 : 16);
        look = 1;
    } else if (look == 2) {
        r = 72;
        g = 220;
        b = 110;
    } else if (look == 1) {
        r = 170;
        g = 236;
        b = 220;
    }
    gem(x, y, look, pop, r, g, b);
}

static int world_shade(int slot, int reach, int glow, int glow_at) {
    int base = flow_base(slot);
    /* Color as soon as the world is open. Clearing it is the diamonds. */
    int shade = base < reach ? 256 : 78;
    if (glow > 0 && glow_at == base) {
        int t = 256 - glow * 256 / FLOW_GLOW;
        shade = 78 + (256 - 78) * t / 256;
    }
    return shade;
}

static int world_cleared(int slot, int reach) {
    return flow_base(slot) + flow_stages(slot) < reach;
}

static void hline(int x0, int x1, int y, int bucket, int r, int g, int b) {
    XY a, c;
    if (x1 < x0) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    a.x = (short)x0;
    a.y = (short)y;
    c.x = (short)x1;
    c.y = (short)y;
    line_f2(a, c, bucket, r, g, b);
}

static void vline(int x, int y0, int y1, int bucket, int r, int g, int b) {
    XY a, c;
    a.x = c.x = (short)x;
    a.y = (short)y0;
    c.y = (short)y1;
    line_f2(a, c, bucket, r, g, b);
}

static void path_seg(int x0, int x1, int y, int kind) {
    XY top, right, left, bottom;
    int r = 22, g = 26, b = 28;
    if (x1 < x0) {
        int t = x0;
        x0 = x1;
        x1 = t;
    }
    if (x1 - x0 < 4)
        return;
    if (kind == 2) {
        r = 48;
        g = 190;
        b = 160;
    } else if (kind == 1) {
        r = 32;
        g = 70;
        b = 68;
    }
    top.x = (short)(x0 + 5);
    top.y = (short)y;
    right.x = (short)(x1 - 5);
    right.y = (short)y;
    left.x = top.x;
    left.y = (short)(y + 1);
    bottom.x = right.x;
    bottom.y = left.y;
    poly_f4(&top, &right, &left, &bottom, 28, r, g, b, BLEND_NONE);
}

static void draw_icon(int slot, int pos, int cy, int reach, int glow, int glow_at, int time, int selected) {
    Icon ic;
    int dx, ad, idle;
    if (slot < 0 || slot >= flow_world_count())
        return;
    ic.cx = icon_x(slot, pos);
    if (ic.cx < -70 || ic.cx > SCREEN_W + 70)
        return;
    dx = ic.cx - SCREEN_W / 2;
    ad = dx < 0 ? -dx : dx;
    ic.cy = cy;
    ic.shade = world_shade(slot, reach, glow, glow_at);
    /* Neighbors sit a step back in brightness. The hue stays the world's. */
    if (!selected && ic.shade > 220)
        ic.shade = 200;
    idle = isin(time * 3) / 40;
    ic.yaw = -dx * 2 + idle;
    if (ic.yaw > 500)
        ic.yaw = 500;
    if (ic.yaw < -500)
        ic.yaw = -500;
    if (selected)
        ic.sc = 112 + isin(time * 2) / 512;
    else if (ad < SPAN + 16)
        ic.sc = 88;
    else
        ic.sc = 68;
    ic.bucket = 8 + ad / 20;
    if (ic.bucket > 22)
        ic.bucket = 22;
    icon_fn(&ic, slot);
}

static void draw_row(int slot, int pos, int cy, int reach, int glow, int glow_at, int time) {
    int n = flow_world_count();
    int order[12];
    int i, a, b;
    if (n > 12)
        n = 12;
    for (i = 0; i < n; i++)
        order[i] = i;
    for (a = 0; a < n; a++) {
        for (b = a + 1; b < n; b++) {
            int da = icon_x(order[a], pos) - SCREEN_W / 2;
            int db = icon_x(order[b], pos) - SCREEN_W / 2;
            if (da < 0)
                da = -da;
            if (db < 0)
                db = -db;
            if (da < db) {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }
    for (i = 0; i < n - 1; i++) {
        int x0 = icon_x(i, pos);
        int x1 = icon_x(i + 1, pos);
        int kind = 0;
        if (x1 < -20 || x0 > SCREEN_W + 20)
            continue;
        if (world_cleared(i, reach))
            kind = 2;
        else if (flow_base(i) < reach)
            kind = 1;
        path_seg(x0, x1, cy + 2, kind);
    }
    for (i = 0; i < n; i++)
        draw_icon(order[i], pos, cy, reach, glow, glow_at, time, order[i] == slot);
    for (i = 0; i < n; i++) {
        const char *name = story_at(i)->name;
        int x = icon_x(i, pos);
        int base, s, count, tw, nx;
        int here = i == slot;
        if (x < -20 || x > SCREEN_W + 20)
            continue;
        diamond(x, cy + 6, here ? 3 : 2,
                here ? 80 : 36, here ? 210 : 64, here ? 180 : 66);
        base = flow_base(i);
        count = flow_stages(i);
        if (x < 24 || x > SCREEN_W - 24)
            continue;
        tw = mini_width(name);
        nx = x - tw / 2;
        if (nx >= 2 && nx + tw <= SCREEN_W - 2) {
            mini_text(nx, cy + 16, name,
                      here ? 220 : 78, here ? 236 : 96, here ? 228 : 94);
            if (here)
                fill_rect(nx, cy + 21, tw > 1 ? tw - 1 : 1, 1, 48, 190, 170);
        }
        for (s = 0; s < count && s < 3; s++)
            gem_of(x - 10 + s * 10, cy + 28, base + s, reach, glow, glow_at, 0);
        if (world_shade(i, reach, glow, glow_at) < 210)
            padlock(x, cy + 38, 50, 58, 66);
    }
}

static void frame_box(int x, int y, int w, int h, int r, int g, int b) {
    int cr = r + 40, cg = g + 30, cb = b + 20;
    if (cr > 255)
        cr = 255;
    if (cg > 255)
        cg = 255;
    if (cb > 255)
        cb = 255;
    hline(x, x + w, y, 3, r, g, b);
    hline(x, x + w, y + h, 3, r, g, b);
    vline(x, y, y + h, 3, r, g, b);
    vline(x + w, y, y + h, 3, r, g, b);
    hline(x, x + 10, y, 3, cr, cg, cb);
    hline(x + w - 10, x + w, y, 3, cr, cg, cb);
    hline(x, x + 10, y + h, 3, cr, cg, cb);
    hline(x + w - 10, x + w, y + h, 3, cr, cg, cb);
    vline(x, y, y + 8, 3, cr, cg, cb);
    vline(x + w, y, y + 8, 3, cr, cg, cb);
    vline(x, y + h - 8, y + h, 3, cr, cg, cb);
    vline(x + w, y + h - 8, y + h, 3, cr, cg, cb);
}

static void draw_card(int slot, int focus, int stage, int reach, int glow, int glow_at) {
    const Level *lv = story_at(slot);
    int base = flow_base(slot);
    int n = lv->stage_count;
    int i;
    int top = 136;
    int title_w = disp_width_32(lv->name);
    int list_w = 0;
    int shown = n < 3 ? n : 3;
    int tab;
    frame_box(20, top, 280, 70, 32, 110, 104);
    disp_text_32((SCREEN_W - title_w) / 2, top + 5, lv->name, 236, 244, 242);
    for (i = 0; i < shown; i++) {
        int tw = hud_width(lv->stages[i].name, 1);
        if (tw > list_w)
            list_w = tw;
    }
    tab = (SCREEN_W - list_w) / 2;
    for (i = 0; i < shown; i++) {
        const char *name = lv->stages[i].name;
        int y = top + 20 + i * 14;
        int open = stage_look(base + i, reach) > 0;
        int here = focus == FLOW_STAGES && i == stage;
        int tw = hud_width(name, 1);
        /* The bar is linked after the name, so the name draws in front of it. */
        gem_of(tab - 12, y + 3, base + i, reach, glow, glow_at, 1);
        hud_text(tab, y, name, 1,
                 here ? 236 : (open ? 170 : 70),
                 here ? 248 : (open ? 220 : 82),
                 here ? 244 : (open ? 200 : 80));
        if (here)
            fill_rect(tab - 4, y - 2, tw + 8, 11, 16, 48, 52);
    }
}

void flow_draw(int slot, int pos_q8, int focus, int stage, int reach,
               int glow, int glow_at, int time) {
    int nworld = flow_world_count();
    if (slot < 0)
        slot = 0;
    if (slot >= nworld)
        slot = nworld - 1;
    draw_row(slot, pos_q8, ROW_Y, reach, glow, glow_at, time);
    draw_card(slot, focus, stage, reach, glow, glow_at);
    front_plain();
}

static int text_width(const char *s, int scale) {
    int n = 0;
    while (s[n])
        n++;
    return n * 8 * scale;
}

static void slide_name(const char *s, int y, int tick, int from_left, int scale, int r, int g, int b) {
    int span = CARD_SLIDE;
    int t = tick < span ? tick : span;
    int slide = (span - t) * SCREEN_W / span;
    int w = text_width(s, scale);
    int x = (SCREEN_W - w) / 2;
    int fade = 255;
    if (tick >= CARD_HOLD) {
        int u = tick - CARD_HOLD;
        if (u >= CARD_FADE)
            return;
        fade = 255 - u * 255 / CARD_FADE;
    }
    if (from_left)
        x -= slide;
    else
        x += slide;
    if (x > SCREEN_W || x + w < 0)
        return;
    r = r * fade / 255;
    g = g * fade / 255;
    b = b * fade / 255;
    /* The offset copy is linked later, so it sits behind the name. */
    text_scale(x, y, s, r, g, b, scale);
    if (scale > 1)
        text_scale(x + 1, y, s, r / 3, g / 2, b / 2, scale);
}

int card_black(int tick) {
    return tick < CARD_HOLD;
}

int card_finished(int tick) {
    return tick >= CARD_GO;
}

void card_overlay(const char *world, const char *stage, int tick) {
    if (tick < CARD_ROAD) {
        slide_name(world, 88, tick, 1, 2, 240, 248, 244);
        slide_name(stage, 116, tick, 0, 1, 170, 210, 206);
    }
    if (tick >= CARD_ROAD && tick < CARD_GO) {
        int step = (tick - CARD_ROAD) / CARD_BEAT;
        int local = (tick - CARD_ROAD) % CARD_BEAT;
        char num[2];
        int scale = 4;
        if (step > 2)
            step = 2;
        /* A short gap between digits, then the number holds still. */
        if (local >= 8) {
            num[0] = (char)('3' - step);
            num[1] = 0;
            text_scale((SCREEN_W - 8 * scale) / 2, 100, num, 240, 248, 244, scale);
        }
    }
    if (tick >= CARD_HOLD && tick < CARD_ROAD) {
        int u = tick - CARD_HOLD;
        if (u < CARD_FADE * 2 / 3)
            screen_cover(0, 0, 0, BLEND_AVG);
        if (u < CARD_FADE / 3)
            screen_cover(0, 0, 0, BLEND_AVG);
    }
}
