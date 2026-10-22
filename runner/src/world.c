#include <psxgpu.h>
#include <psxgte.h>
#include <inline_c.h>
#include "world.h"
#include "profile.h"

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

/* Gouraud and texture-blended prims dither down to 15-bit color. Each
 * DR_TPAGE rewrites the whole draw mode, so they set it too. */
#define DITHER 1

static RenderContext ctx;
static const uint16_t *stamp_px;
static int frame_mark;

void world_mark(void) {
    frame_mark = VSync(-1);
}

void world_stamp_row(const uint16_t *px) {
    stamp_px = px;
}

void world_init(void) {
    int i;
    ResetGraph(0);
    for (i = 0; i < 2; i++) {
        SetDefDrawEnv(&ctx.buffers[i].draw, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        SetDefDispEnv(&ctx.buffers[i].disp, 0, i * SCREEN_H, SCREEN_W, SCREEN_H);
        setRGB0(&ctx.buffers[i].draw, 48, 88, 148);
        ctx.buffers[i].draw.isbg = 1;
        ctx.buffers[i].draw.dtd = DITHER;
    }
    ctx.active = 0;
    ctx.next = ctx.buffers[0].buffer;
    ClearOTagR(ctx.buffers[0].ot, OT_LENGTH);
    FntLoad(960, 0);
    InitGeom();
    /* A raised horizon looks down on the road without tilting the camera. */
    gte_SetGeomOffset(SCREEN_W / 2, HORIZON_Y);
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
}

void world_flip(void) {
    RenderBuffer *draw = &ctx.buffers[ctx.active];
    RenderBuffer *disp = &ctx.buffers[ctx.active ^ 1];
    DrawSync(0);
    if (stamp_px) {
        RECT rect;
        /* disp is the buffer whose draw just finished, and the one PutDispEnv
         * is about to show. The clear already happened inside that draw. */
        setRECT(&rect, 0, (ctx.active ^ 1) * SCREEN_H + (SCREEN_H - 1), SCREEN_W, 1);
        LoadImage(&rect, (const uint32_t *)stamp_px);
        DrawSync(0);
    }
    /* A picture that finished inside its first scan waits out the second,
     * so a light frame stays at 30. A picture that already used that scan
     * presents on the next blank and does not wait for a third. */
    if (VSync(-1) - frame_mark < 1)
        VSync(0);
    VSync(0);
    PutDispEnv(&disp->disp);
    DrawOTagEnv(&draw->ot[OT_LENGTH - 1], &draw->draw);
#ifdef RUNNER_PROFILE
    /* Waiting here keeps the GPU's time apart from the CPU's. */
    PROF_LONG(PF_GPU, DrawSync(0));
#endif
    ctx.active ^= 1;
    ctx.next = disp->buffer;
    ClearOTagR(disp->ot, OT_LENGTH);
}

void world_set_clear(int r, int g, int b) {
    setRGB0(&ctx.buffers[ctx.active].draw, r, g, b);
}

size_t world_bytes_used(void) {
    return (size_t)(ctx.next - ctx.buffers[ctx.active].buffer);
}

void *alloc_prim(int z, size_t size) {
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

void world_link(int z, void *prim) {
    if (z < 1) z = 1;
    if (z >= OT_LENGTH) z = OT_LENGTH - 1;
    addPrim(&ctx.buffers[ctx.active].ot[z], prim);
}

int world_buffer(void) {
    return ctx.active;
}

extern uint16_t _font_tpage;
extern uint16_t _font_clut;

static int text_glyphs(const char *s) {
    int glyphs = 0;
    for (; *s; s++) {
        if (*s != ' ' && *s != '\n')
            glyphs++;
    }
    return glyphs;
}

void text(int x, int y, const char *s) {
    RenderBuffer *buf = &ctx.buffers[ctx.active];
    if (ctx.next + text_glyphs(s) * sizeof(SPRT_8) + sizeof(DR_TPAGE) > buf->buffer + BUFFER_LENGTH)
        return;
    ctx.next = FntSort(&buf->ot[0], ctx.next, x, y, s);
}

void text_rgb(int x, int y, const char *s, int r, int g, int b) {
#ifdef RUNNER_PREVIEW
    (void)r;
    (void)g;
    (void)b;
    text(x, y, s);
#else
    RenderBuffer *buf = &ctx.buffers[ctx.active];
    SPRT_8 *sprt;
    DR_TPAGE *tpage;
    const char *p;
    int sx = x;
    if (ctx.next + text_glyphs(s) * sizeof(SPRT_8) + sizeof(DR_TPAGE) > buf->buffer + BUFFER_LENGTH)
        return;
    sprt = (SPRT_8 *)ctx.next;
    for (p = s; *p; p++) {
        int c = *p;
        int i;
        if (c == '\n') {
            sx = x;
            y += 8;
            continue;
        }
        if (c >= 'a' && c <= 'z')
            c -= 32;
        i = c - ' ';
        if (i > 0) {
            i--;
            setSprt8(sprt);
            setXY0(sprt, sx, y);
            setUV0(sprt, (i % 16) * 8, (i / 16) * 8);
            setRGB0(sprt, r, g, b);
            sprt->clut = _font_clut;
            addPrim(&buf->ot[0], sprt);
            sprt++;
        }
        sx += 8;
    }
    tpage = (DR_TPAGE *)sprt;
    tpage->code[0] = _font_tpage;
    setlen(tpage, 1);
    setcode(tpage, 0xe1);
    addPrim(&buf->ot[0], tpage);
    ctx.next = (uint8_t *)tpage + sizeof(DR_TPAGE);
#endif
}

void text_scale(int x, int y, const char *s, int r, int g, int b, int scale) {
#ifdef RUNNER_PREVIEW
    (void)r;
    (void)g;
    (void)b;
    (void)scale;
    text(x, y, s);
#else
    RenderBuffer *buf = &ctx.buffers[ctx.active];
    POLY_FT4 *poly;
    const char *p;
    int sx = x;
    int n = text_glyphs(s);
    if (scale < 1)
        scale = 1;
    if (scale > 4)
        scale = 4;
    /* A sprite's width is how many texels it samples, so a bigger sprite
     * reads a block of the font sheet. A quad stretches one 8x8 glyph. */
    if (ctx.next + n * sizeof(POLY_FT4) > buf->buffer + BUFFER_LENGTH)
        return;
    poly = (POLY_FT4 *)ctx.next;
    for (p = s; *p; p++) {
        int c = *p;
        int i;
        int u, v;
        if (c == '\n') {
            sx = x;
            y += 8 * scale;
            continue;
        }
        if (c >= 'a' && c <= 'z')
            c -= 32;
        i = c - ' ';
        if (i > 0) {
            i--;
            u = (i % 16) * 8;
            v = (i / 16) * 8;
            setPolyFT4(poly);
            /* Raw texels. A tinted stretch dithers, and the digit stops reading. */
            setShadeTex(poly, 1);
            setRGB0(poly, r, g, b);
            setXYWH(poly, sx, y, 8 * scale, 8 * scale);
            setUVWH(poly, u, v, 7, 7);
            poly->tpage = _font_tpage;
            poly->clut = _font_clut;
            addPrim(&buf->ot[0], poly);
            poly++;
        }
        sx += 8 * scale;
    }
    ctx.next = (uint8_t *)poly;
#endif
}

void fill_rect(int x, int y, int w, int h, int r, int g, int b) {
#ifndef RUNNER_PREVIEW
    RenderBuffer *buf = &ctx.buffers[ctx.active];
    TILE *tile;
    if (w < 1 || h < 1)
        return;
    if (ctx.next + sizeof(TILE) > buf->buffer + BUFFER_LENGTH)
        return;
    tile = (TILE *)ctx.next;
    setTile(tile);
    setXY0(tile, x, y);
    setWH(tile, w, h);
    setRGB0(tile, clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255));
    addPrim(&buf->ot[0], tile);
    ctx.next += sizeof(TILE);
#else
    (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b;
#endif
}

void front_plain(void) {
#ifndef RUNNER_PREVIEW
    RenderBuffer *buf = &ctx.buffers[ctx.active];
    DR_TPAGE *mode;
    if (ctx.next + sizeof(DR_TPAGE) > buf->buffer + BUFFER_LENGTH)
        return;
    mode = (DR_TPAGE *)ctx.next;
    setDrawTPage(mode, 0, 0, getTPage(0, 0, 0, 0));
    addPrim(&buf->ot[0], mode);
    ctx.next += sizeof(DR_TPAGE);
#endif
}

void tex_quad(int x, int y, int w, int h, int u, int v, int uw, int uh,
              int r, int g, int b, uint16_t tpage, uint16_t clut) {
#ifndef RUNNER_PREVIEW
    RenderBuffer *buf = &ctx.buffers[ctx.active];
    POLY_FT4 *poly;
    if (w < 1 || h < 1)
        return;
    if (ctx.next + sizeof(POLY_FT4) > buf->buffer + BUFFER_LENGTH)
        return;
    poly = (POLY_FT4 *)ctx.next;
    setPolyFT4(poly);
    setShadeTex(poly, 0);
    setRGB0(poly, clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255));
    setXYWH(poly, x, y, w, h);
    setUVWH(poly, u, v, uw, uh);
    poly->tpage = tpage;
    poly->clut = clut;
    addPrim(&buf->ot[0], poly);
    ctx.next += sizeof(POLY_FT4);
#else
    (void)x; (void)y; (void)w; (void)h; (void)u; (void)v; (void)uw; (void)uh;
    (void)r; (void)g; (void)b; (void)tpage; (void)clut;
#endif
}

void screen_cover(int r, int g, int b, int blend) {
    RenderBuffer *buf = &ctx.buffers[ctx.active];
    POLY_F4 *poly;
    size_t need = sizeof(POLY_F4);
    if (blend != BLEND_NONE)
        need += sizeof(DR_TPAGE);
    if (ctx.next + need > buf->buffer + BUFFER_LENGTH)
        return;
    poly = (POLY_F4 *)ctx.next;
    setPolyF4(poly);
    setRGB0(poly, clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255));
    setXY4(poly, 0, 0, SCREEN_W, 0, 0, SCREEN_H, SCREEN_W, SCREEN_H);
    addPrim(&buf->ot[0], poly);
    ctx.next += sizeof(POLY_F4);
    if (blend != BLEND_NONE) {
        DR_TPAGE *mode = (DR_TPAGE *)ctx.next;
        setDrawTPage(mode, 0, DITHER, getTPage(0, blend, 0, 0));
        setSemiTrans(poly, 1);
        addPrim(&buf->ot[0], mode);
        ctx.next += sizeof(DR_TPAGE);
    }
}

int project(int x, int y, int z, XY *out) {
    SVECTOR v __attribute__((aligned(4)));
    int sz = 0;
    v.vx = (short)clamp(x, -16000, 16000);
    v.vy = (short)clamp(CAM_H - y, -16000, 16000);
    v.vz = (short)z;
    v.pad = 0;
    PROF_COUNT_PROJECTION();
    gte_ldv0(&v);
    gte_rtps();
    gte_stsxy(out);
    gte_stsz(&sz);
    return sz;
}

void dim(int *r, int *g, int *b, int z) {
    int s = 280 - z / 16;
    if (s < 72) s = 72;
    if (s > 255) s = 255;
    *r = (*r * s) >> 8;
    *g = (*g * s) >> 8;
    *b = (*b * s) >> 8;
}

int xy_ok(const XY *p) {
    return p->x >= -1000 && p->x <= 1000 && p->y >= -1000 && p->y <= 1000;
}

int edge_ok(XY a, XY b) {
    int dx = iabs(a.x - b.x);
    int dy = iabs(a.y - b.y);
    return (dx > dy ? dx : dy) <= 480;
}

/* xy_ok and edge_ok for a quad in one pass, without branches per corner. */
static inline unsigned coord_bad(int v) { return (unsigned)(v + 1000) > 2000u; }
static inline unsigned span_bad(int a, int b) { return (unsigned)(a - b + 480) > 960u; }

static int quad_ok(const XY *p0, const XY *p1, const XY *p2, const XY *p3) {
    int x0 = p0->x, y0 = p0->y, x1 = p1->x, y1 = p1->y;
    int x2 = p2->x, y2 = p2->y, x3 = p3->x, y3 = p3->y;
    unsigned bad = coord_bad(x0) | coord_bad(y0) | coord_bad(x1) | coord_bad(y1) |
                   coord_bad(x2) | coord_bad(y2) | coord_bad(x3) | coord_bad(y3);
    bad |= span_bad(x0, x1) | span_bad(y0, y1) | span_bad(x2, x3) | span_bad(y2, y3) |
           span_bad(x0, x2) | span_bad(y0, y2) | span_bad(x1, x3) | span_bad(y1, y3);
    return !bad;
}

void poly_f4(const XY *p0, const XY *p1, const XY *p2, const XY *p3,
             int bucket, int r, int g, int b, int blend) {
    POLY_F4 *poly;
    if (!quad_ok(p0, p1, p2, p3))
        return;
    if (blend != BLEND_NONE &&
        world_bytes_used() + sizeof(POLY_F4) + sizeof(DR_TPAGE) > BUFFER_LENGTH)
        return;
    poly = alloc_prim(bucket, sizeof(POLY_F4));
    if (!poly)
        return;
    setPolyF4(poly);
    setRGB0(poly, clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255));
    setXY4(poly, p0->x, p0->y, p1->x, p1->y, p2->x, p2->y, p3->x, p3->y);
    if (blend != BLEND_NONE) {
        /* Linked after the quad, so it draws just before it in the same bucket. */
        DR_TPAGE *mode = alloc_prim(bucket, sizeof(DR_TPAGE));
        setDrawTPage(mode, 0, DITHER, getTPage(0, blend, 0, 0));
        setSemiTrans(poly, 1);
    }
}

void line_f2(XY a, XY b, int bucket, int r, int g, int bl) {
    LINE_F2 *line;
    if (!xy_ok(&a) || !xy_ok(&b) || !edge_ok(a, b))
        return;
    line = alloc_prim(bucket, sizeof(LINE_F2));
    if (!line)
        return;
    setLineF2(line);
    setRGB0(line, clamp(r, 0, 255), clamp(g, 0, 255), clamp(bl, 0, 255));
    setXY2(line, a.x, a.y, b.x, b.y);
}

void poly_f4v(const XY *p, int bucket, int r, int g, int b, int blend) {
    POLY_F4 *poly;
    int x0 = p[0].x, x1 = x0, y0 = p[0].y, y1 = y0, i;
    for (i = 1; i < 4; i++) {
        int x = p[i].x, y = p[i].y;
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
    }
    if (x1 < 0 || x0 >= SCREEN_W || y1 < 0 || y0 >= SCREEN_H)
        return;
    /* Tight bounds: oversized near-camera quads were sticking on screen. */
    if (x0 < -400 || x1 > SCREEN_W + 400 || y0 < -200 || y1 > SCREEN_H + 200)
        return;
    if (y1 - y0 > 320 || x1 - x0 > 280)
        return;
    if (blend != BLEND_NONE &&
        world_bytes_used() + sizeof(POLY_F4) + sizeof(DR_TPAGE) > BUFFER_LENGTH)
        return;
    poly = alloc_prim(bucket, sizeof(POLY_F4));
    if (!poly)
        return;
    setPolyF4(poly);
    setRGB0(poly, clamp(r, 0, 255), clamp(g, 0, 255), clamp(b, 0, 255));
    setXY4(poly, p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);
    if (blend != BLEND_NONE) {
        DR_TPAGE *mode = alloc_prim(bucket, sizeof(DR_TPAGE));
        setDrawTPage(mode, 0, DITHER, getTPage(0, blend, 0, 0));
        setSemiTrans(poly, 1);
    }
}

void poly_g4(const XY *p0, const XY *p1, const XY *p2, const XY *p3,
             int bucket,
             int r0, int g0, int b0, int r1, int g1, int b1,
             int r2, int g2, int b2, int r3, int g3, int b3, int blend) {
    POLY_G4 *poly;
    if (!quad_ok(p0, p1, p2, p3))
        return;
    if (blend != BLEND_NONE &&
        world_bytes_used() + sizeof(POLY_G4) + sizeof(DR_TPAGE) > BUFFER_LENGTH)
        return;
    poly = alloc_prim(bucket, sizeof(POLY_G4));
    if (!poly)
        return;
    setPolyG4(poly);
    setRGB0(poly, clamp(r0, 0, 255), clamp(g0, 0, 255), clamp(b0, 0, 255));
    setRGB1(poly, clamp(r1, 0, 255), clamp(g1, 0, 255), clamp(b1, 0, 255));
    setRGB2(poly, clamp(r2, 0, 255), clamp(g2, 0, 255), clamp(b2, 0, 255));
    setRGB3(poly, clamp(r3, 0, 255), clamp(g3, 0, 255), clamp(b3, 0, 255));
    setXY4(poly, p0->x, p0->y, p1->x, p1->y, p2->x, p2->y, p3->x, p3->y);
    if (blend != BLEND_NONE) {
        DR_TPAGE *mode = alloc_prim(bucket, sizeof(DR_TPAGE));
        setDrawTPage(mode, 0, DITHER, getTPage(0, blend, 0, 0));
        setSemiTrans(poly, 1);
    }
}

void dither_mode(int bucket, int on) {
    DR_TPAGE *mode = alloc_prim(bucket, sizeof(DR_TPAGE));
    if (mode)
        setDrawTPage(mode, 0, on && DITHER, getTPage(0, 0, 0, 0));
}

void gradient(int x, int y, int w, int h, int bucket,
              int r0, int g0, int b0, int r1, int g1, int b1) {
    POLY_G4 *p = alloc_prim(bucket, sizeof(POLY_G4));
    if (!p)
        return;
    setPolyG4(p);
    setXYWH(p, x, y, w, h);
    setRGB0(p, r0, g0, b0);
    setRGB1(p, r0, g0, b0);
    setRGB2(p, r1, g1, b1);
    setRGB3(p, r1, g1, b1);
}

void quad(int x0, int y0, int z0, int x1, int y1, int z1,
          int x2, int y2, int z2, int x3, int y3, int z3,
          int sortz, int r, int g, int b) {
    XY p0, p1, p2, p3;
    if (z0 < 8 || z1 < 8 || z2 < 8 || z3 < 8)
        return;
    project(x0, y0, z0, &p0);
    project(x1, y1, z1, &p1);
    project(x2, y2, z2, &p2);
    project(x3, y3, z3, &p3);
    poly_f4(&p0, &p1, &p2, &p3, sortz >> OT_SHIFT, r, g, b, BLEND_NONE);
}

void face(int x0, int y0, int z0, int x1, int y1, int z1,
          int x2, int y2, int z2, int x3, int y3, int z3,
          int sortz, int r, int g, int b) {
    dim(&r, &g, &b, sortz);
    quad(x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3, sortz, r, g, b);
}

void x_limits(int z, int *lo, int *hi) {
    if (z < 1)
        z = 1;
    *lo = (SAFE_X0 - SCREEN_W / 2) * z / (SCREEN_W / 2);
    *hi = (SAFE_X1 - SCREEN_W / 2) * z / (SCREEN_W / 2);
}

void slab(int xa, int z0, int xb, int z1, int y, int r, int g, int b,
          int sort_far, Shade shade) {
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
    shade(&r, &g, &b, znear);
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

void flat(int xa, int z0, int xb, int z1, int y, int r, int g, int b) {
    slab(xa, z0, xb, z1, y, r, g, b, 1, dim);
}

void lid(int xa, int z0, int xb, int z1, int y, int r, int g, int b) {
    slab(xa, z0, xb, z1, y, r, g, b, 0, dim);
}

void wall(int xa, int xb, int z, int ya, int yb, int r, int g, int b) {
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

void side(int x, int z0, int z1, int ya, int yb, int r, int g, int b) {
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
