/* Draws each level on the host and writes PNG stills, so scenery can be
 * judged without a console. It links the game's own world.c, course.c,
 * and level files, replaces the GPU and GTE with software, and reports
 * what a frame costs: primitive buffer bytes, projections, and fill.
 *
 *   sh runner/tools/preview/preview.sh [out-dir]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psxgpu.h>
#include <psxgte.h>
#include "world.h"
#include "course.h"
#include "path.h"
#include "level.h"

/* The GPU and GTE */

typedef struct {
    uintptr_t ot;
    int seq;
    void *prim;
} Link;

#define MAX_LINKS 16384
static Link links[MAX_LINKS];
static int link_count;

void host_add_prim(uint32_t *ot, void *prim) {
    if (link_count < MAX_LINKS) {
        links[link_count].ot = (uintptr_t)ot;
        links[link_count].seq = link_count;
        links[link_count].prim = prim;
        link_count++;
    }
}

static int ofx, ofy, geom_h;
static SVECTOR gte_v;
static int16_t gte_sx, gte_sy;
static int gte_sz;
static long projections;

void host_gte_offset(int x, int y) { ofx = x; ofy = y; }
void host_gte_screen(int h) { geom_h = h; }
void host_gte_ldv0(const SVECTOR *v) { gte_v = *v; }

void host_gte_rtps(void) {
    int z = gte_v.vz < 0 ? 0 : gte_v.vz;
    long q, sx, sy;
    if (z * 2 <= geom_h)
        q = 0x1FFFF;
    else
        q = ((long)geom_h * 0x20000 / z + 1) / 2;
    if (q > 0x1FFFF)
        q = 0x1FFFF;
    sx = ((long)ofx * 65536 + gte_v.vx * q) >> 16;
    sy = ((long)ofy * 65536 + gte_v.vy * q) >> 16;
    gte_sx = (int16_t)(sx < -1024 ? -1024 : sx > 1023 ? 1023 : sx);
    gte_sy = (int16_t)(sy < -1024 ? -1024 : sy > 1023 ? 1023 : sy);
    gte_sz = z;
    projections++;
}

void host_gte_stsxy(void *xy) {
    int16_t *p = xy;
    p[0] = gte_sx;
    p[1] = gte_sy;
}

void host_gte_stsz(int *z) { *z = gte_sz; }

void ResetGraph(int mode) { (void)mode; }
void InitGeom(void) {}
void ClearOTagR(uint32_t *ot, size_t length) { (void)ot; (void)length; }
int DrawSync(int mode) { (void)mode; return 0; }
int LoadImage(const RECT *rect, const uint32_t *data) {
    (void)rect;
    (void)data;
    return 0;
}
int VSync(int mode) { (void)mode; return 0; }
void PutDispEnv(const DISPENV *env) { (void)env; }
int DrawOTagEnv(const uint32_t *ot, DRAWENV *env) { (void)ot; (void)env; return 0; }
void FntLoad(int x, int y) { (void)x; (void)y; }
void *FntSort(uint32_t *ot, void *pri, int x, int y, const char *s) {
    (void)ot; (void)x; (void)y; (void)s;
    return pri;
}

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h) {
    memset(env, 0, sizeof *env);
    setRECT(&env->clip, x, y, w, h);
    return env;
}

DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h) {
    memset(env, 0, sizeof *env);
    setRECT(&env->disp, x, y, w, h);
    return env;
}

MATRIX *RotMatrix(SVECTOR *r, MATRIX *m) { (void)r; return m; }
MATRIX *TransMatrix(MATRIX *m, VECTOR *r) { (void)r; return m; }

/* Rasterizer */

static uint8_t fb[SCREEN_H][SCREEN_W][3];
static int abr;
static long px_fill, px_blend;

static int sat(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void plot(int x, int y, int r, int g, int b, int semi) {
    uint8_t *d = fb[y][x];
    px_fill++;
    if (!semi) {
        d[0] = (uint8_t)r; d[1] = (uint8_t)g; d[2] = (uint8_t)b;
        return;
    }
    px_blend++;
    switch (abr) {
    case 0: d[0] = (uint8_t)((d[0] + r) / 2); d[1] = (uint8_t)((d[1] + g) / 2); d[2] = (uint8_t)((d[2] + b) / 2); break;
    case 1: d[0] = (uint8_t)sat(d[0] + r); d[1] = (uint8_t)sat(d[1] + g); d[2] = (uint8_t)sat(d[2] + b); break;
    case 2: d[0] = (uint8_t)sat(d[0] - r); d[1] = (uint8_t)sat(d[1] - g); d[2] = (uint8_t)sat(d[2] - b); break;
    default: d[0] = (uint8_t)sat(d[0] + r / 4); d[1] = (uint8_t)sat(d[1] + g / 4); d[2] = (uint8_t)sat(d[2] + b / 4); break;
    }
}

typedef struct { int x, y, r, g, b; } Vtx;

static long edge_fn(const Vtx *a, const Vtx *b, int x, int y) {
    return (long)(b->x - a->x) * (y - a->y) - (long)(b->y - a->y) * (x - a->x);
}

static void tri(Vtx a, Vtx b, Vtx c, int semi) {
    long area = edge_fn(&a, &b, c.x, c.y);
    int x0, x1, y0, y1, x, y;
    if (area == 0)
        return;
    if (area < 0) {
        Vtx t = b; b = c; c = t;
        area = -area;
    }
    x0 = a.x < b.x ? a.x : b.x; if (c.x < x0) x0 = c.x;
    x1 = a.x > b.x ? a.x : b.x; if (c.x > x1) x1 = c.x;
    y0 = a.y < b.y ? a.y : b.y; if (c.y < y0) y0 = c.y;
    y1 = a.y > b.y ? a.y : b.y; if (c.y > y1) y1 = c.y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (y1 > SCREEN_H - 1) y1 = SCREEN_H - 1;
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            long w0 = edge_fn(&b, &c, x, y);
            long w1 = edge_fn(&c, &a, x, y);
            long w2 = edge_fn(&a, &b, x, y);
            if (w0 < 0 || w1 < 0 || w2 < 0)
                continue;
            plot(x, y,
                 (int)((a.r * w0 + b.r * w1 + c.r * w2) / area),
                 (int)((a.g * w0 + b.g * w1 + c.g * w2) / area),
                 (int)((a.b * w0 + b.b * w1 + c.b * w2) / area), semi);
        }
    }
}

static int cmp_link(const void *pa, const void *pb) {
    const Link *a = pa, *b = pb;
    if (a->ot != b->ot)
        return a->ot < b->ot ? 1 : -1;
    return b->seq - a->seq;
}

typedef struct { int f4, g4, tile, tpage, blended; } Counts;

static Counts render(int cr, int cg, int cb) {
    Counts n = {0, 0, 0, 0, 0};
    int i, x, y;
    for (y = 0; y < SCREEN_H; y++)
        for (x = 0; x < SCREEN_W; x++) {
            fb[y][x][0] = (uint8_t)cr; fb[y][x][1] = (uint8_t)cg; fb[y][x][2] = (uint8_t)cb;
        }
    abr = 0;
    qsort(links, (size_t)link_count, sizeof links[0], cmp_link);
    for (i = 0; i < link_count; i++) {
        uint8_t code = ((P_TAG *)links[i].prim)->code;
        int semi = (code & 2) != 0;
        if (code == 0xE1) {
            abr = (int)((((uint32_t *)links[i].prim)[1] >> 5) & 3);
            n.tpage++;
        } else if ((code & 0xFC) == 0x28) {
            POLY_F4 *p = links[i].prim;
            Vtx v0 = {p->x0, p->y0, p->r0, p->g0, p->b0};
            Vtx v1 = {p->x1, p->y1, p->r0, p->g0, p->b0};
            Vtx v2 = {p->x2, p->y2, p->r0, p->g0, p->b0};
            Vtx v3 = {p->x3, p->y3, p->r0, p->g0, p->b0};
            tri(v0, v1, v2, semi);
            tri(v1, v2, v3, semi);
            n.f4++;
            n.blended += semi;
        } else if ((code & 0xFC) == 0x38) {
            POLY_G4 *p = links[i].prim;
            Vtx v0 = {p->x0, p->y0, p->r0, p->g0, p->b0};
            Vtx v1 = {p->x1, p->y1, p->r1, p->g1, p->b1};
            Vtx v2 = {p->x2, p->y2, p->r2, p->g2, p->b2};
            Vtx v3 = {p->x3, p->y3, p->r3, p->g3, p->b3};
            tri(v0, v1, v2, semi);
            tri(v1, v2, v3, semi);
            n.g4++;
        } else if ((code & 0xFC) == 0x60) {
            TILE *t = links[i].prim;
            int yy, xx;
            for (yy = t->y0; yy < t->y0 + t->h; yy++)
                for (xx = t->x0; xx < t->x0 + t->w; xx++)
                    if (xx >= 0 && yy >= 0 && xx < SCREEN_W && yy < SCREEN_H)
                        plot(xx, yy, t->r0, t->g0, t->b0, semi);
            n.tile++;
        }
    }
    link_count = 0;
    return n;
}

/* PNG with stored deflate blocks */

static uint32_t crc_table[256];

static uint32_t crc(uint32_t c, const uint8_t *p, size_t n) {
    size_t i;
    if (!crc_table[1]) {
        uint32_t k, j;
        for (k = 0; k < 256; k++) {
            uint32_t v = k;
            for (j = 0; j < 8; j++)
                v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[k] = v;
        }
    }
    c = ~c;
    for (i = 0; i < n; i++)
        c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len) {
    uint8_t h[8];
    uint32_t c;
    be32(h, len);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (len)
        fwrite(data, 1, len, f);
    c = crc(0, h + 4, 4);
    c = crc(c, data, len);
    be32(h, c);
    fwrite(h, 1, 4, f);
}

static void write_png(const char *path) {
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    enum { ROW = 1 + SCREEN_W * 3, RAW = ROW * SCREEN_H };
    static uint8_t raw[RAW];
    static uint8_t z[RAW + RAW / 65535 * 5 + 64];
    uint8_t ihdr[13];
    uint32_t a = 1, b = 0;
    size_t i, zn = 0;
    int y;
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    for (y = 0; y < SCREEN_H; y++) {
        raw[y * ROW] = 0;
        memcpy(raw + y * ROW + 1, fb[y], SCREEN_W * 3);
    }
    z[zn++] = 0x78;
    z[zn++] = 0x01;
    for (i = 0; i < RAW; ) {
        size_t n = RAW - i > 65535 ? 65535 : RAW - i;
        z[zn++] = i + n == RAW;
        z[zn++] = (uint8_t)n; z[zn++] = (uint8_t)(n >> 8);
        z[zn++] = (uint8_t)~n; z[zn++] = (uint8_t)(~n >> 8);
        memcpy(z + zn, raw + i, n);
        zn += n;
        i += n;
    }
    for (i = 0; i < RAW; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    be32(z + zn, (b << 16) | a);
    zn += 4;
    be32(ihdr, SCREEN_W);
    be32(ihdr + 4, SCREEN_H);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    fwrite(sig, 1, 8, f);
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zn);
    chunk(f, "IEND", 0, 0);
    fclose(f);
}

/* Frames */

/* A stand-in for the runner sprite, at its real depth. */
static void runner_marker(void) {
    XY feet;
    int sz = project(0, 0, CHAR_Z, &feet);
    int h = (78 * (SCREEN_W / 2)) / sz, w = h * 2 / 5;
    TILE *t = alloc_prim((CHAR_Z >> OT_SHIFT) - 1, sizeof(TILE));
    if (!t)
        return;
    setTile(t);
    setXY0(t, feet.x - w / 2, feet.y - h);
    setWH(t, w, h);
    setRGB0(t, 230, 230, 236);
}

typedef struct {
    size_t bytes;
    long proj, fill, blend;
    Counts n;
} Cost;

static Cost frame(const Level *lv, const LevelFrame *f, int marker, const char *png) {
    Cost c;
    int r, g, b;
    projections = 0;
    px_fill = px_blend = 0;
    path_begin(f->scroll);
    lv->clear_color(f, &r, &g, &b);
    lv->draw(f);
    if (marker)
        runner_marker();
    c.bytes = world_bytes_used();
    c.proj = projections;
    c.n = render(r, g, b);
    c.fill = px_fill;
    c.blend = px_blend;
    if (png)
        write_png(png);
    world_flip();
    return c;
}

static void slug(char *out, const char *name) {
    for (; *name; name++)
        *out++ = (*name >= 'A' && *name <= 'Z') ? (char)(*name + 32) : *name == ' ' ? '_' : *name;
    *out = 0;
}

int main(int argc, char **argv) {
    static const struct { const char *tag; int scroll, speed; } shots[] = {
        {"run_a", 2600, 20}, {"run_b", 15000, 20}, {"run_c", 31337, 20},
        {"run_d", 47210, 20}, {"paused", 15000, 0}, {"select", 6000, 8},
    };
    const char *dir = argc > 1 ? argv[1] : ".";
    int i, j;
    world_init();
    for (i = 0; i < level_count; i++) {
        const Level *lv = levels[i];
        char name[64], path[512];
        Cost worst = {0, 0, 0, 0, {0, 0, 0, 0, 0}};
        double proj = 0, fill = 0, bytes = 0;
        int frames = 0, s;
        slug(name, lv->name);
        for (j = 0; j < (int)(sizeof shots / sizeof shots[0]); j++) {
            LevelFrame f = {shots[j].scroll, shots[j].speed, shots[j].scroll / 20 + 37};
            Cost c;
            snprintf(path, sizeof path, "%s/%s_%s.png", dir, name, shots[j].tag);
            c = frame(lv, &f, 1, path);
            printf("%-10s %-7s bytes %5zu  F4 %4d (blend %3d)  G4 %d  proj %5ld  fill %6ld (blend %5ld)\n",
                   lv->name, shots[j].tag, c.bytes, c.n.f4, c.n.blended, c.n.g4, c.proj, c.fill, c.blend);
        }
        /* A minute of running, for averages and the worst frame. */
        for (s = 0; s < 3600; s++) {
            LevelFrame f = {s * 20, 20, s};
            Cost c = frame(lv, &f, 0, 0);
            if (c.bytes > worst.bytes) worst.bytes = c.bytes;
            if (c.proj > worst.proj) worst.proj = c.proj;
            if (c.fill > worst.fill) worst.fill = c.fill;
            if (c.blend > worst.blend) worst.blend = c.blend;
            if (c.n.f4 > worst.n.f4) worst.n.f4 = c.n.f4;
            proj += (double)c.proj;
            fill += (double)c.fill;
            bytes += (double)c.bytes;
            frames++;
        }
        printf("%-10s 60 s run: bytes avg %.0f max %zu of %d | F4 max %d | proj avg %.0f max %ld | fill avg %.0f max %ld | blend px max %ld\n",
               lv->name, bytes / frames, worst.bytes, BUFFER_LENGTH, worst.n.f4,
               proj / frames, worst.proj, fill / frames, worst.fill, worst.blend);
    }
    return 0;
}
