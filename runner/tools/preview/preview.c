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
#include "countdown.h"
#include "flow.h"
#include "road.h"
#include "course.h"
#include "path.h"
#include "level.h"
#include "world_mesh.h"
#include "sprite_sheet.h"
#include "runner_lighting.h"

/* The GPU and GTE */

typedef struct {
    uintptr_t ot;
    int seq;
    void *prim;
    int asset;
} Link;

#define FRAME_ASSETS 64
typedef struct {
    const char *kind, *representation;
    int instance, primitives, raster_pixels, visible_pixels;
} AssetFrame;
static AssetFrame frame_assets[FRAME_ASSETS];
static int asset_count, active_asset, raster_asset, visibility_enabled, render_shell_only;
static uint64_t submitted_mask[SCREEN_H][SCREEN_W], visible_mask[SCREEN_H][SCREEN_W];

void world_asset(const char *kind, int instance, const char *representation) {
    int i;
    if (!visibility_enabled) return;
    for (i = 0; i < asset_count; i++)
        if (frame_assets[i].instance == instance && !strcmp(frame_assets[i].kind,kind)) break;
    if (i == FRAME_ASSETS) { fprintf(stderr,"visibility: frame asset limit exceeded\n"); exit(1); }
    if (i == asset_count) {
        memset(&frame_assets[i],0,sizeof frame_assets[i]);
        frame_assets[i].kind = kind;
        frame_assets[i].instance = instance;
        asset_count++;
    }
    frame_assets[i].representation = representation;
    active_asset = i;
}

#define MAX_LINKS 16384
static Link links[MAX_LINKS];
static int link_count;
static int atmosphere_check_enabled, atmosphere_failures, atmosphere_primitives;

void host_add_prim(uint32_t *ot, void *prim) {
    if (link_count < MAX_LINKS) {
        links[link_count].ot = (uintptr_t)ot;
        links[link_count].seq = link_count;
        links[link_count].prim = prim;
        links[link_count].asset = active_asset;
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
static uint16_t vram[512][1024];
int LoadImage(const RECT *rect, const uint32_t *data) {
    const uint16_t *src=(const uint16_t *)data;
    int x,y;
    for (y=0;y<rect->h;y++) for(x=0;x<rect->w;x++)
        vram[(rect->y+y)&511][(rect->x+x)&1023]=src[y*rect->w+x];
    return 0;
}
int VSync(int mode) { (void)mode; return 0; }
void PutDispEnv(const DISPENV *env) { (void)env; }
static DRAWENV last_draw_env;
int DrawOTagEnv(const uint32_t *ot, DRAWENV *env) { (void)ot; last_draw_env=*env; return 0; }
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

/* PSX15=1 shows what the GPU shows: 15-bit color, with Gouraud prims
 * dithered while the draw mode's dither bit is set. */
static int psx15, dither_on, prim_dither;
static const int dither4[4][4] = {
    {-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2},
};

static void plot(int x, int y, int r, int g, int b, int semi) {
    uint8_t *d = fb[y][x];
    px_fill++;
    if (visibility_enabled) {
        uint64_t bit = UINT64_C(1) << raster_asset;
        submitted_mask[y][x] |= bit;
        /* Transparent primitives retain underlying contributors. Only opaque
         * draws prove something behind them is completely hidden. */
        if (semi) visible_mask[y][x] |= bit;
        else visible_mask[y][x] = bit;
    }
    if (psx15) {
        int k = prim_dither ? dither4[y & 3][x & 3] : 0;
        r = sat(r + k) & ~7;
        g = sat(g + k) & ~7;
        b = sat(b + k) & ~7;
    }
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
    if (psx15) {
        d[0] &= ~7;
        d[1] &= ~7;
        d[2] &= ~7;
    }
}

typedef struct { int x, y, r, g, b; } Vtx;
static int texture_on, texture_page, texture_clut, texture_raw;
static int texture_green, texture_blue;
/* Textured triangles use r/g for affine U/V and b for monochrome modulation. */
static void tex_plot(int x,int y,int u,int v,int shade,int semi) {
    int mode=(texture_page>>7)&3, tx=(texture_page&15)*64, ty=(texture_page&16)*16;
    int word, index; uint16_t c;
    u &=255; v &=255;
    word=vram[(ty+v)&511][(tx+(u>>(2-mode)))&1023];
    index=mode==0 ? (word>>((u&3)*4))&15 : (word>>((u&1)*8))&255;
    c=mode==2 ? word : vram[(texture_clut>>6)&511][((texture_clut&63)*16+index)&1023];
    if (!c) return;
    if (texture_raw) shade=128;
    plot(x,y,clamp(((c&31)<<3)*shade/128,0,255),
        clamp((((c>>5)&31)<<3)*(texture_raw?128:texture_green)/128,0,255),
        clamp((((c>>10)&31)<<3)*(texture_raw?128:texture_blue)/128,0,255),semi && (c&0x8000));
}

static long edge_fn(const Vtx *a, const Vtx *b, int x, int y) {
    return (long)(b->x - a->x) * (y - a->y) - (long)(b->y - a->y) * (x - a->x);
}

static int top_left(const Vtx *a, const Vtx *b) {
    return b->y < a->y || (b->y == a->y && b->x > a->x);
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
            /* Shared edges belong to one triangle. Inclusive edges previously
             * double-blended diagonals and made a 1x1 quad cover four pixels. */
            if (w0 < 0 || w1 < 0 || w2 < 0 ||
                (!w0 && !top_left(&b,&c)) || (!w1 && !top_left(&c,&a)) ||
                (!w2 && !top_left(&a,&b)))
                continue;
            if (texture_on) tex_plot(x,y,
                 (int)((a.r * w0 + b.r * w1 + c.r * w2) / area),
                 (int)((a.g * w0 + b.g * w1 + c.g * w2) / area),
                 (int)((a.b * w0 + b.b * w1 + c.b * w2) / area), semi);
            else plot(x, y,
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
    if (visibility_enabled) {
        memset(submitted_mask,0,sizeof submitted_mask);
        memset(visible_mask,0,sizeof visible_mask);
    }
    for (y = 0; y < SCREEN_H; y++)
        for (x = 0; x < SCREEN_W; x++) {
            fb[y][x][0] = (uint8_t)cr; fb[y][x][1] = (uint8_t)cg; fb[y][x][2] = (uint8_t)cb;
        }
    abr = 0;
    psx15 = getenv("PSX15") != 0;
    dither_on = 1;
    if (psx15)
        for (y = 0; y < SCREEN_H; y++)
            for (x = 0; x < SCREEN_W; x++)
                for (i = 0; i < 3; i++)
                    fb[y][x][i] &= ~7;
    qsort(links, (size_t)link_count, sizeof links[0], cmp_link);
    for (i = 0; i < link_count; i++) {
        if (render_shell_only && strncmp(frame_assets[links[i].asset].kind,"shell_",6))
            continue;
        uint8_t code = ((P_TAG *)links[i].prim)->code;
        if (atmosphere_check_enabled && code != 0xE1) {
            const char *kind=frame_assets[links[i].asset].kind;
            if (!strcmp(kind,"reach_sky") || !strcmp(kind,"reach_backdrop") || !strcmp(kind,"reach_ruin")) {
                atmosphere_primitives++;
                if ((code & 0xFC)!=0x38) atmosphere_failures++;
                else {
                    const POLY_G4 *p=links[i].prim;
                    /* Independent contract: the clear horizon and every
                     * distant backdrop/ruin base must meet exactly. */
                    atmosphere_failures += p->r2!=cr || p->g2!=cg || p->b2!=cb ||
                                           p->r3!=cr || p->g3!=cg || p->b3!=cb;
                    /* Distant landmarks retain subtle contrast, never the
                     * unattenuated dark silhouettes of the old backdrop. */
                    if (strcmp(kind,"reach_sky"))
                        atmosphere_failures += iabs(p->r0-cr)>16 || iabs(p->g0-cg)>16 ||
                                               iabs(p->b0-cb)>16 || iabs(p->r1-cr)>16 ||
                                               iabs(p->g1-cg)>16 || iabs(p->b1-cb)>16;
                }
            }
        }
        int semi = (code & 2) != 0;
        raster_asset = links[i].asset;
        if (visibility_enabled && code != 0xE1)
            frame_assets[raster_asset].primitives++;
        if (code == 0xE1) {
            abr = (int)((((uint32_t *)links[i].prim)[1] >> 5) & 3);
            dither_on = (int)((((uint32_t *)links[i].prim)[1] >> 9) & 1);
            n.tpage++;
        } else if ((code & 0xFC) == 0x2C) {
            POLY_FT4 *p=links[i].prim;
            Vtx a={p->x0,p->y0,p->u0,p->v0,p->r0}, b={p->x1,p->y1,p->u1,p->v1,p->r0};
            Vtx c={p->x2,p->y2,p->u2,p->v2,p->r0}, d={p->x3,p->y3,p->u3,p->v3,p->r0};
            texture_green=p->g0;texture_blue=p->b0;
            texture_on=1; texture_page=p->tpage; texture_clut=p->clut; texture_raw=code&1;
            abr=(p->tpage>>5)&3;
            prim_dither=dither_on && !texture_raw;
            tri(a,b,c,semi); tri(b,c,d,semi);
            texture_on=0; prim_dither=0; n.f4++;
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
            prim_dither = dither_on;
            tri(v0, v1, v2, semi);
            tri(v1, v2, v3, semi);
            prim_dither = 0;
            n.g4++;
        } else if ((code & 0xFC) == 0x40) {
            LINE_F2 *p = links[i].prim;
            int dx = iabs(p->x1 - p->x0), dy = iabs(p->y1 - p->y0);
            int steps = dx > dy ? dx : dy, k;
            for (k = 0; k <= steps; k++) {
                int xx = steps ? p->x0 + (p->x1 - p->x0) * k / steps : p->x0;
                int yy = steps ? p->y0 + (p->y1 - p->y0) * k / steps : p->y0;
                if (xx >= 0 && yy >= 0 && xx < SCREEN_W && yy < SCREEN_H)
                    plot(xx, yy, p->r0, p->g0, p->b0, semi);
            }
            n.f4++;
        } else if ((code & 0xFC) == 0x68) {
            TILE_1 *t=links[i].prim;
            if(t->x0>=0 && t->y0>=0 && t->x0<SCREEN_W && t->y0<SCREEN_H)
                plot(t->x0,t->y0,t->r0,t->g0,t->b0,semi);
            n.tile++;
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
    if (visibility_enabled)
        for (y = 0; y < SCREEN_H; y++)
            for (x = 0; x < SCREEN_W; x++) {
                uint64_t bits = submitted_mask[y][x];
                while (bits) {
                    int slot = __builtin_ctzll(bits);
                    frame_assets[slot].raster_pixels++;
                    bits &= bits-1;
                }
                bits = visible_mask[y][x];
                while (bits) {
                    int slot = __builtin_ctzll(bits);
                    frame_assets[slot].visible_pixels++;
                    bits &= bits-1;
                }
            }
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

/* A stand-in for the runner sprite, at its real depth, in the world's light. */
static void runner_marker(int scroll, const Light *light) {
    XY feet, p0, p1, p2, p3;
    int ax, ay;
    int sz = path_stand(scroll + CHAR_Z, 0, 0, &feet, &ax, &ay);
    int h = (78 * (SCREEN_W / 2)) / sz, w = h * 2 / 5;
    int bx = ax * w / 2 / 4096, by = ay * w / 2 / 4096;
    int ux = ay * h / 4096, uy = -ax * h / 4096;
    p0.x = (int16_t)(feet.x - bx + ux); p0.y = (int16_t)(feet.y - by + uy);
    p1.x = (int16_t)(feet.x + bx + ux); p1.y = (int16_t)(feet.y + by + uy);
    p2.x = (int16_t)(feet.x - bx); p2.y = (int16_t)(feet.y - by);
    p3.x = (int16_t)(feet.x + bx); p3.y = (int16_t)(feet.y + by);
    poly_f4(&p0, &p1, &p2, &p3, road_runner_bucket((CHAR_Z >> OT_SHIFT) - 1),
            clamp(230 * light->tint[0] / 128, 0, 255),
            clamp(230 * light->tint[1] / 128, 0, 255),
            clamp(236 * light->tint[2] / 128, 0, 255), BLEND_NONE);
}

typedef struct {
    size_t bytes;
    long proj, fill, blend;
    Counts n;
    unsigned long samples, transforms;
} Cost;

/* alt runs the frame on the first fork's other road. */
static void runner_sprite_preview(const LevelFrame *f,const Light *light,int ambient) {
    static int uploaded;
    uint16_t palette[16];RECT rect;XY feet;POLY_FT4 *spr;
    int i,ax,ay,sz,h,w,bx,by,ux,uy,cell= spr_char_cell[0];
    if(!uploaded) {
        for(i=0;i<SPR_PAGE_COUNT;i++) {
            setRECT(&rect,spr_page_x[i],spr_page_y[i],SPR_PAGE_W/4,spr_page_h[i]);
            LoadImage(&rect,(const uint32_t *)(spr_tex+spr_page_off[i]));
        }
        uploaded=1;
    }
    for(i=0;i<16;i++) palette[i]=ambient?runner_lit_texel(spr_clut[0][i],light):spr_clut[0][i];
    setRECT(&rect,spr_clut_x[0]+16,spr_clut_y[0],16,1);LoadImage(&rect,(const uint32_t *)palette);
    sz=path_stand(f->scroll+CHAR_Z,0,0,&feet,&ax,&ay);if(sz<8)return;
    h=78*(SCREEN_W/2)/sz;if(h<8)h=8;
    w=h*spr_base_fw[spr_char_base[0]]/spr_base_fh[spr_char_base[0]];
    bx=ax*w/2/4096;by=ay*w/2/4096;ux=ay*h/4096;uy=-ax*h/4096;
    int bucket=road_runner_bucket((CHAR_Z>>OT_SHIFT)-1);
    dither_mode(bucket,1);spr=alloc_prim(bucket,sizeof *spr);if(!spr)return;dither_mode(bucket,0);
    setPolyFT4(spr);setRGB0(spr,light->tint[0],light->tint[1],light->tint[2]);
    setXY4(spr,feet.x-bx+ux,feet.y-by+uy,feet.x+bx+ux,feet.y+by+uy,
           feet.x-bx,feet.y-by,feet.x+bx,feet.y+by);
    setUVWH(spr,spr_cell_u[cell],spr_cell_v[cell],spr_base_fw[spr_char_base[0]]-1,spr_base_fh[spr_char_base[0]]-1);
    spr->tpage=getTPage(0,0,spr_page_x[spr_cell_page[cell]],spr_page_y[spr_cell_page[cell]]);
    spr->clut=getClut(spr_clut_x[0]+16,spr_clut_y[0]);
}

static Cost frame(const Level *lv, const LevelFrame *f, int marker, int alt, const char *png) {
    Cost c;
    int r, g, b;
    projections = 0;
    path_preview_samples = path_preview_transforms = 0;
    px_fill = px_blend = 0;
    if (visibility_enabled) {
        asset_count = 0;
        world_asset("unclassified",0,"mixed");
    }
    if (!path_open(lv->stage_count > 0 && f->stage >= 0 && f->stage < lv->stage_count ?
                   lv->stages[f->stage].track : lv->track))
        path_open("straight.run");
    alt = alt && path_branches() > 0;
    if (alt)
        path_swap(0);
    path_begin(f->scroll);
    lv->clear_color(f, &r, &g, &b);
    lv->draw(f);
    if (marker) {
        world_asset("runner",0,"quad");
        if(marker>1) runner_sprite_preview(f,level_light(lv),marker==3);
        else runner_marker(f->scroll, level_light(lv));
    }
    c.bytes = world_bytes_used();
    c.proj = projections;
    c.samples = path_preview_samples;
    c.transforms = path_preview_transforms;
    c.n = render(r, g, b);
    c.fill = px_fill;
    c.blend = px_blend;
    if (png)
        write_png(png);
    world_flip();
    if (alt)
        path_swap(0);
    return c;
}

static void slug(char *out, const char *name) {
    for (; *name; name++)
        *out++ = (*name >= 'A' && *name <= 'Z') ? (char)(*name + 32) : *name == ' ' ? '_' : *name;
    *out = 0;
}

/* A/B the identical game state, not independently chosen stills. Sample the
 * whole run, both sides of schedule boundaries and rib approach boundaries.
 * CSV records every comparison; save the worst native-resolution pair per
 * stage plus a difference image so dark-wall regressions remain inspectable. */
static int compare_world(const char *dir, int strict) {
    static uint8_t reference[sizeof fb], baked[sizeof fb];
    const Level *lv = 0;
    char path[512];
    FILE *csv;
    int i, stage, band, offset, failures = 0, pairs = 0;
    static const int offsets[] = {0, 1, 127, 255};
    for (i = 0; i < level_count; i++)
        if (!strcmp(levels[i]->track, "sine.gate")) lv = levels[i];
    if (!lv) exit(1);
    snprintf(path, sizeof path, "%s/comparison.csv", dir);
    csv = fopen(path, "w");
    if (!csv) { perror(path); exit(1); }
    fprintf(csv, "stage,scroll,changed_pixels,strong_pixels,max_channel_delta,mean_channel_delta,legacy_bytes,baked_bytes\n");
    for (stage = 0; stage < 3; stage++) {
        double worst = -1;
        int worst_scroll = 0, strong_worst = 0;
        for (band = 0; band <= (STAGE_FINISH + 2048) / STEP; band++)
            for (offset = 0; offset < 4; offset++) {
                LevelFrame f = {band * STEP + offsets[offset], 20, band * 13, 0, stage};
                Cost a, b;
                int changed = 0, strong = 0, maximum = 0, px;
                long total = 0;
                world_mesh_preview_legacy = 1;
                a = frame(lv, &f, 1, 0, 0);
                memcpy(reference, fb, sizeof fb);
                world_mesh_preview_legacy = 0;
                b = frame(lv, &f, 1, 0, 0);
                memcpy(baked, fb, sizeof fb);
                for (px = 0; px < SCREEN_W * SCREEN_H; px++) {
                    int c, high = 0;
                    for (c = 0; c < 3; c++) {
                        int d = iabs(reference[px * 3 + c] - baked[px * 3 + c]);
                        total += d;
                        if (d > high) high = d;
                    }
                    changed += high > 0;
                    strong += high > 24;
                    if (high > maximum) maximum = high;
                }
                fprintf(csv, "%d,%d,%d,%d,%d,%.6f,%zu,%zu\n", stage, f.scroll, changed,
                        strong, maximum, (double)total / sizeof fb, a.bytes, b.bytes);
                /* Native 5-bit shading can differ slightly with tessellation.
                 * Bound both average drift and high-contrast feature changes. */
                failures += (double)total / sizeof fb > 0.02 || strong > 16;
                pairs++;
                if (strong > strong_worst) strong_worst = strong;
                if (total > worst) {
                    worst = total;
                    worst_scroll = f.scroll;
                    snprintf(path, sizeof path, "%s/stage%d_baked.png", dir, stage);
                    write_png(path);
                    memcpy(fb, reference, sizeof fb);
                    snprintf(path, sizeof path, "%s/stage%d_legacy.png", dir, stage);
                    write_png(path);
                    for (px = 0; px < (int)sizeof fb; px++)
                        ((uint8_t *)fb)[px] = (uint8_t)clamp(iabs(reference[px] - baked[px]) * 8, 0, 255);
                    snprintf(path, sizeof path, "%s/stage%d_difference_x8.png", dir, stage);
                    write_png(path);
                }
            }
        printf("stage %d: worst visual difference at scroll %d, mean channel delta %.3f; strong pixels max %d\n",
               stage, worst_scroll, worst / sizeof fb, strong_worst);
        fflush(stdout);
    }
    fclose(csv);
    if (strict) printf("visual comparison: %d frames exceed the image difference budgets\n", failures);
    else printf("visual review: %d identical-state frame pairs recorded for the redesigned tunnel\n",pairs);
    return strict && failures != 0;
}

static int countdown_check(const char *dir) {
    static uint8_t white_mask[SCREEN_H][SCREEN_W];
    int d,x,y,failures=0,st,lv,stages_checked=0;
    /* The story card must reveal the road before the one shared countdown;
     * it no longer owns a second three-number timer. */
    failures+=CARD_GO!=CARD_ROAD;
    failures+=countdown_digit(0)!=3 || countdown_digit(59)!=3 ||
        countdown_digit(60)!=2 || countdown_digit(119)!=2 ||
        countdown_digit(120)!=1 || countdown_digit(179)!=1 || countdown_digit(180)!=0;
    for(d=3;d>=1;d--) {
        int white=0,border=0;char name[512];
        countdown_draw(d,0);
        render(48,48,48);
        for(y=0;y<SCREEN_H;y++) for(x=0;x<SCREEN_W;x++) {
            white+=fb[y][x][0]==248;
            if(d==3) white_mask[y][x]=fb[y][x][0]==248;
            border+=fb[y][x][0]==8;
        }
        printf("countdown %d: white %d border %d\n",d,white,border);
        failures+=white<100 || border<50;
        snprintf(name,sizeof name,"%s/countdown_%d.png",dir,d);write_png(name);
        world_flip();
    }
    {
        static const int ages[]={0,12,36,44,52,58,59};
        int previous=0;
        for(d=0;d<(int)(sizeof ages/sizeof ages[0]);d++) {
            int lit=0;char path[512];
            countdown_draw(3,ages[d]);render(48,48,48);
            for(y=0;y<SCREEN_H;y++) for(x=0;x<SCREEN_W;x++) lit+=fb[y][x][0]==248;
            printf("countdown age %d: %d white pixels\n",ages[d],lit);
            if(ages[d]<=12) failures+=lit<previous;
            if(ages[d]>=36) failures+=lit>previous;
            previous=lit;
            snprintf(path,sizeof path,"%s/countdown_age_%d.png",dir,ages[d]);write_png(path);
            world_flip();
        }
    }
    for(lv=0;lv<level_count;lv++) {
        int n=levels[lv]->stage_count;
        if(n<1) n=1;
        for(st=0;st<n;st++) {
            LevelFrame f={levels[lv]->stage_count ? levels[lv]->stages[st].start : 0,0,0,0,st};
            countdown_draw(3,0);
            frame(levels[lv],&f,1,0,0);
            for(y=0;y<SCREEN_H;y++) for(x=0;x<SCREEN_W;x++)
                failures+=white_mask[y][x] && (fb[y][x][0]!=248 || fb[y][x][1]!=248 || fb[y][x][2]!=248);
            stages_checked++;
        }
    }
    point_f1((XY){10,10},1,208,0,0);render(0,0,0);
    d=0;
    for(y=0;y<SCREEN_H;y++) for(x=0;x<SCREEN_W;x++) d+=fb[y][x][0]!=0;
    failures+=d!=1 || fb[10][10][0]!=208;
    world_flip();
    printf("Countdown/vector and single-pixel checks: %d failures; %d stage openings checked\n",failures,stages_checked);
    return failures!=0;
}

static int city_check(const char *dir) {
    const Level *lv=0; int i,s,failures=0,holes=0,sky=0,poses=0;
    size_t peak=0; int distant_pixels=0,light_failures=0;
    for(i=0;i<level_count;i++) if(!strcmp(levels[i]->track,"vesper.sweep")) lv=levels[i];
    if(!lv) return 1;
    for(s=0;s<=116736;s+=256) {
        LevelFrame f={s,20,s/20,0,2};
        Cost c=frame(lv,&f,1,0,0);
        if(c.bytes>peak) peak=c.bytes;
        failures+=c.bytes>BUFFER_LENGTH-2048;
        poses++;
    }
    visibility_enabled=1;render_shell_only=1;
    for(s=0;s<=3;s++) {
        static const int at[]={0,5000,15000,48000};
        LevelFrame f={at[s],20,0,0,2};
        int x,y,l,r,half;
        if(s==0 || s==3) {
            int mid=0,far=0;
            render_shell_only=0;
            frame(lv,&f,0,0,0);
            for(i=0;i<asset_count;i++) if(!strcmp(frame_assets[i].kind,"city_windows")) {
                if(!strcmp(frame_assets[i].representation,"mid")) mid+=frame_assets[i].visible_pixels;
                if(!strcmp(frame_assets[i].representation,"far")) far+=frame_assets[i].visible_pixels;
            }
            light_failures+=mid==0 || far==0;
            printf("City lights at %d: mid %d visible pixels, far %d\n",at[s],mid,far);
            distant_pixels+=mid+far;
            render_shell_only=1;
        }
        frame(lv,&f,0,0,0);
        for(i=0;i<asset_count;i++)
            failures+=!strcmp(frame_assets[i].kind,"vesper_skyline");
        path_edges(at[s],&l,&r);half=r+64;
        for(x=0;x<100;x++) {
            int roof=clamp(HORIZON_Y-(767-CAM_H)*(160-x)/half+3,0,SCREEN_H-1);
            int foot=HORIZON_Y+CAM_H*(160-x)/half-3;
            for(y=roof;y<foot;y++) {
                holes+=visible_mask[y][x]==0;
                holes+=visible_mask[y][319-x]==0;
            }
        }
        for(y=0;y<30;y++) for(x=150;x<170;x++) sky+=visible_mask[y][x]!=0;
    }
    visibility_enabled=0;render_shell_only=0;
    {
        static const int shots[]={0,15000,22000,24000,30000,43008,47000,92000,96000,104000,112000};
        for(i=0;i<(int)(sizeof shots/sizeof shots[0]);i++) {
            char name[512]; LevelFrame f={shots[i],20,shots[i]/20,0,2};
            snprintf(name,sizeof name,"%s/canyon_%d.png",dir,shots[i]);
            frame(lv,&f,1,0,name);
        }
    }
    printf("City check: %d route poses, %zu peak command bytes, %d budget failures, %d wall holes, %d blocked sky pixels\n",poses,peak,failures,holes,sky);
    printf("City mid/far lights: %d visible pixels in straight reference views\n",distant_pixels);
    return failures || holes || sky || light_failures;
}

extern void vesper_preview_skyline(void);
static int vesper_motion_check(void) {
    static uint8_t previous[SCREEN_H][SCREEN_W][3];
    int s,frames=0,failures=0,prev_shift=0,prev_yaw=0,changed=0;
    path_open("vesper.sweep");
    for(s=24576;s<=65536;s+=20) {
        int yaw,shift,delta,x,y,c;
        path_begin(s);
        yaw=path_yaw(); shift=yaw*SCREEN_W/2048;
        vesper_preview_skyline();
        render(0,0,0);
        if(frames) {
            failures += yaw<prev_yaw || path_horizon()!=HORIZON_Y;
            delta=shift-prev_shift;
            for(y=0;y<100;y++) for(x=0;x<SCREEN_W-delta;x++) {
                int bad=0;
                for(c=0;c<3;c++)
                    bad |= iabs(fb[y][x][c]-previous[y][x+delta][c])>8;
                changed+=bad;
            }
        }
        memcpy(previous,fb,sizeof previous);
        prev_shift=shift; prev_yaw=yaw; frames++;
        world_flip();
    }
    printf("Vesper motion: %d frames, %d heading/horizon failures, %d pixels move independently of skyline\n",
           frames,failures,changed);
    return failures!=0 || changed!=0;
}

static int texture_self_test(void) {
    /* Verify packed nibbles, palette addressing, opacity and UV orientation
     * through the same uploaded VRAM and primitive path as the console. */
    uint16_t pixels[2] __attribute__((aligned(4)))={0x3210,0x1230};
    uint16_t palette[16] __attribute__((aligned(4)))={0,31,31<<5,31<<10};
    RECT rect; XY p[4]={{0,0},{4,0},{0,2},{4,2}}, uv[4]={{0,128},{4,128},{0,130},{4,130}};
    int failures=0;
    setRECT(&rect,960,128,1,2); LoadImage(&rect,(const uint32_t *)pixels);
    setRECT(&rect,0,483,16,1); LoadImage(&rect,(const uint32_t *)palette);
    poly_tex4(p,uv,4,128,getTPage(0,0,960,0),getClut(0,483));
    /* Test exact palette decoding independently of optional GPU dithering. */
    dither_mode(4,0);
    render(16,16,16);
    failures += fb[0][0][0]!=16 || fb[0][1][0]!=248 || fb[0][2][1]!=248 || fb[0][3][2]!=248;
    failures += fb[1][1][2]!=248 || fb[1][2][1]!=248 || fb[1][3][0]!=248;
    world_flip();
    printf("texture check: %d failures (4-bit upload, CLUT, UV, transparent zero)\n",failures);
    return failures!=0;
}

static int visibility_self_test(void) {
    XY a={0,0},b={8,0},c={0,8},d={8,8};
    int transparent, failures=0;
    visibility_enabled=1;
    for (transparent=0;transparent<2;transparent++) {
        asset_count=0;
        world_asset("back",0,"quad");
        poly_f4(&a,&b,&c,&d,4,80,80,80,BLEND_NONE);
        world_asset("front",0,"quad");
        poly_f4(&a,&b,&c,&d,3,160,160,160,transparent?BLEND_AVG:BLEND_NONE);
        render(0,0,0);
        failures += frame_assets[0].raster_pixels != 64 ||
                    frame_assets[0].visible_pixels != (transparent?64:0) ||
                    frame_assets[1].visible_pixels != 64;
        {
            int x,y,expected=transparent?120:160;
            for (y=0;y<8;y++) for(x=0;x<8;x++)
                failures += fb[y][x][0] != expected;
        }
        world_flip();
    }
    asset_count=0;
    world_asset("offscreen",0,"quad");
    a.x=c.x=-20;b.x=d.x=-10;
    poly_f4(&a,&b,&c,&d,4,80,80,80,BLEND_NONE);
    render(0,0,0);
    failures += frame_assets[0].raster_pixels != 0 || frame_assets[0].visible_pixels != 0;
    world_flip();
    asset_count=0;
    world_asset("one_pixel",0,"pixel");
    a.x=c.x=0;b.x=d.x=1;c.y=d.y=1;
    poly_f4(&a,&b,&c,&d,4,80,80,80,BLEND_NONE);
    render(0,0,0);
    failures += frame_assets[0].raster_pixels != 1 || frame_assets[0].visible_pixels != 1;
    world_flip();
    visibility_enabled=0;
    printf("visibility self-test: %d failures (opaque occlusion, transparency, off-screen)\n",failures);
    return failures != 0;
}

static int tunnel_check(const char *dir) {
    static const int positions[] = {0,255,256,2600,13312,24000,34817,62976,100000,108544};
    const Level *lv = 0;
    int stage, i, p, failures = 0;
    char path[512];
    FILE *csv;
    for (i = 0; i < level_count; i++)
        if (!strcmp(levels[i]->track,"sine.gate")) lv = levels[i];
    if (!lv) return 1;
    snprintf(path,sizeof path,"%s/tunnel.csv",dir);
    csv = fopen(path,"w");
    if (!csv) return 1;
    fprintf(csv,"stage,scroll,shell_pixels,shell_primitives,road_vanish_pixels,road_at_vp,far_rib_pixels,left_green,mid_green,horizon_green\n");
    visibility_enabled = 1;
    for (stage = 0; stage < 3; stage++)
        for (p = 0; p < (int)(sizeof positions/sizeof positions[0]); p++) {
            LevelFrame f = {positions[p],0,37,0,stage};
            uint64_t mask = 0;
            int shell_pixels = 0, primitives = 0, road_pixels = 0, road_at_vp = 0, far_pixels = 0, x,y;
            int near_g, mid_g, end_g;
            render_shell_only = 1;
            frame(lv,&f,0,0,0);
            for (i = 0; i < asset_count; i++)
                if (!strncmp(frame_assets[i].kind,"shell_",6)) {
                    mask |= UINT64_C(1)<<i;
                    primitives += frame_assets[i].primitives;
                }
            for (y = 0; y < SCREEN_H; y++)
                for (x = 0; x < SCREEN_W; x++)
                    shell_pixels += (submitted_mask[y][x]&mask) != 0;
            near_g = mid_g = end_g = 0;
            for (x = 0; x < 16; x++) {
                near_g += fb[HORIZON_Y][x][1];
                mid_g += fb[HORIZON_Y][80+x][1];
                end_g += fb[HORIZON_Y][144+x][1];
            }
            failures += shell_pixels != SCREEN_W*SCREEN_H || primitives != 6 ||
                        near_g <= mid_g || mid_g < end_g || end_g > 16*8;
            snprintf(path,sizeof path,"%s/shell_stage%d_s%d.png",dir,stage,f.scroll);
            write_png(path);
            render_shell_only = 0;
            snprintf(path,sizeof path,"%s/tunnel_stage%d_s%d.png",dir,stage,f.scroll);
            frame(lv,&f,1,0,path);
            for (i = 0; i < asset_count; i++) {
                AssetFrame *a = &frame_assets[i];
                if (!strcmp(a->kind,"road_vanish")) {
                    road_pixels += a->visible_pixels;
                    road_at_vp = (visible_mask[HORIZON_Y][SCREEN_W/2]&(UINT64_C(1)<<i)) != 0;
                }
                if (!strcmp(a->kind,"rib") && a->instance >= f.scroll+5000)
                    far_pixels += a->visible_pixels;
            }
            failures += road_pixels == 0 || !road_at_vp || far_pixels == 0;
            fprintf(csv,"%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",stage,f.scroll,shell_pixels,
                    primitives,road_pixels,road_at_vp,far_pixels,near_g,mid_g,end_g);
        }
    fclose(csv);
    visibility_enabled = 0;
    printf("tunnel check: %d failures (coverage, fade, road infinity, distant ribs)\n",failures);
    return failures != 0;
}

/* False-color only the scenery, through the actual submission/raster path.
 * A table entry alone does not prove distant geometry survives clipping,
 * ordering and rasterization. Count its final visible pixels as well. */
static int distance_world(const char *dir) {
    static const int positions[] = {0,1,255,256,257,1280,13312,61311,107520,108544};
    const Level *lv = 0;
    char path[512];
    int i, stage, pose, mode, failures = 0;
    FILE *csv;
    for (i = 0; i < level_count; i++)
        if (!strcmp(levels[i]->track, "sine.gate")) lv = levels[i];
    if (!lv) return 1;
    snprintf(path, sizeof path, "%s/distance.csv", dir);
    csv = fopen(path, "w");
    if (!csv) { perror(path); return 1; }
    fprintf(csv, "stage,scroll,mode,visible_5000_10000_px,visible_10000_20000_px\n");
    world_mesh_preview_depth = 1;
    for (stage = 0; stage < 3; stage++)
        for (pose = 0; pose < (int)(sizeof positions / sizeof positions[0]); pose++)
            for (mode = 0; mode < 2; mode++) {
                LevelFrame f = {positions[pose], 0, 0, 0, stage};
                int x, y, middle = 0, far = 0;
                world_mesh_preview_legacy = mode == 0;
                frame(lv, &f, 0, 0, 0);
                for (y = 0; y < SCREEN_H; y++)
                    for (x = 0; x < SCREEN_W; x++) {
                        uint8_t *p = fb[y][x];
                        middle += p[0] < 16 && p[1] > 200 && p[2] < 16;
                        far += p[0] < 16 && p[1] < 16 && p[2] > 200;
                    }
                fprintf(csv, "%d,%d,%s,%d,%d\n", stage, f.scroll,
                        mode == 0 ? "legacy" : "baked", middle, far);
                failures += !middle || !far;
                snprintf(path, sizeof path, "%s/depth_stage%d_s%d_%s.png", dir, stage,
                         f.scroll, mode == 0 ? "legacy" : "baked");
                write_png(path);
            }
    fclose(csv);
    world_mesh_preview_depth = 0;
    printf("distance check: %d frames missing visible distant wall pixels\n", failures);
    return failures != 0;
}

static int visibility_world(const char *dir, int step) {
    const Level *lv = 0;
    const char *track = getenv("RUNNER_PREVIEW_TRACK");
    char path[512];
    FILE *csv;
    int stage, s, i;
    if (step < 1 || step > 256) return 1;
    if (!track) track = "sine.gate";
    for (i = 0; i < level_count; i++)
        if (!strcmp(levels[i]->track,track)) lv = levels[i];
    if (!lv) return 1;
    snprintf(path,sizeof path,"%s/visibility.csv",dir);
    csv = fopen(path,"w");
    if (!csv) { perror(path); return 1; }
    fprintf(csv,"stage,scroll,kind,instance,representation,primitives,raster_pixels,visible_pixels\n");
    visibility_enabled = 1;
    for (stage = 0; stage < 3; stage++) {
        /* Register Sineworks ribs even when a camera pose never encounters them. */
        if (!strcmp(track,"sine.gate"))
            for (s = 0; s <= STAGE_FINISH+20000; s += 2560)
                fprintf(csv,"%d,-1,rib,%d,catalog,0,0,0\n",stage,s);
        for (s = 0; s <= STAGE_FINISH+2048; s += step) {
            LevelFrame f = {s,20,s/20+37,0,stage};
            frame(lv,&f,1,0,0);
            for (i = 0; i < asset_count; i++) {
                AssetFrame *a = &frame_assets[i];
                if (!strcmp(a->kind,"unclassified")) continue;
                fprintf(csv,"%d,%d,%s,%d,%s,%d,%d,%d\n",stage,s,a->kind,
                        a->instance,a->representation,a->primitives,a->raster_pixels,a->visible_pixels);
            }
        }
        printf("visibility stage %d: complete, camera step %d\n",stage,step);
        fflush(stdout);
    }
    visibility_enabled = 0;
    fclose(csv);
    return 0;
}

static int finish_gate_check(const char *dir) {
    static const uint8_t color[3]={240,240,240};
    int i,j,s,frames=0,failures=0;char name[512];
    visibility_enabled=1;
    for(i=level_count-1;i>=0;i--) for(j=levels[i]->stage_count-1;j>=0;j--) {
        const Level *lv=levels[i];int end=lv->stages[j].finish;
        for(s=end-8192;s<=end+8192;s+=512) {
            LevelFrame f={s,20,37,0,j};int k;
            frame(lv,&f,0,0,0);frames++;
            for(k=0;k<asset_count;k++) {
                AssetFrame *a=&frame_assets[k];
                if((!strcmp(a->kind,"scenery_gate") || !strcmp(a->kind,"rib")) &&
                   a->primitives && a->instance>=end-FAR) failures++;
            }
            /* Rejected gates must allocate no geometry, even after finish. */
            size_t before=world_bytes_used();
            road_gantry(end-FAR,color,color);road_gantry(end,color,color);road_gantry(end+2048,color,color);
            road_finish_gantry(end+1,color,color);
            failures+=world_bytes_used()!=before;
        }
        LevelFrame f={end-1024,20,37,0,j};
        frame(lv,&f,0,0,0);
        size_t before=world_bytes_used();
        road_finish_gantry(end,color,color);
        failures+=world_bytes_used()<=before;
        render(0,0,0);world_flip();
        if(!road_scenery_gate_allowed(end-FAR-1)) failures++;
    }
    path_open("hill.loop");failures+=!road_scenery_gate_allowed(STAGE_FINISH+8192);
    visibility_enabled=0;
    snprintf(name,sizeof name,"%s/finish-gates.txt",dir);FILE *report=fopen(name,"w");
    if(!report)return 1;
    fprintf(report,"%d camera poses; %d failures; all 21 story stages, exact finish gates and arcade reset checked\n",frames,failures);fclose(report);
    printf("finish gates: %d camera poses, %d failures\n",frames,failures);
    return failures!=0;
}

static int runner_lighting_check(const char *dir) {
    const uint8_t pale[3]={176,214,224},dark[3]={20,10,52},saturated[3]={24,220,255};
    int c,i,failures=0;char name[512];
    for(c=0;c<65536;c++) {
        uint16_t lit=runner_ambient_texel(c,pale);
        failures+=runner_ambient_texel(c,dark)!=c || runner_ambient_texel(c,saturated)!=c;
        failures+=(lit&0x8000)!=(c&0x8000) || ((lit==0)!=(c==0));
    }
    failures+=runner_ambient_texel(0x7fff,pale)!=0x7fff || runner_ambient_texel(0x8000,pale)==0x8000;
    int pairs=0;
    for(i=0;i<level_count;i++) if(!strcmp(levels[i]->track,"meridian.drift") ||
        !strcmp(levels[i]->track,"shatter.split") || !strcmp(levels[i]->track,"reach.ridge") ||
        !strcmp(levels[i]->track,"sine.gate")) for(int stage=0;stage<3;stage++) {
        LevelFrame f={15000,20,37,0,stage};
        static unsigned char before[sizeof fb];
        snprintf(name,sizeof name,"%s/runner-%s-%d-before.png",dir,levels[i]->track,stage);
        frame(levels[i],&f,2,0,name);memcpy(before,fb,sizeof fb);
        snprintf(name,sizeof name,"%s/runner-%s-%d-after.png",dir,levels[i]->track,stage);
        frame(levels[i],&f,3,0,name);
        const Light *light=level_light(levels[i]);
        int expected=stage<2 && (!strcmp(levels[i]->track,"meridian.drift") || !strcmp(levels[i]->track,"shatter.split"));
        int changed=memcmp(before,fb,sizeof fb)!=0;
        failures+=changed!=expected;pairs++;
        /* Every authored fill preserves transparency, STP and white, and
         * cannot turn any shadow channel darker. Covers every palette word. */
        for(c=0;c<65536;c++) {
            uint16_t lit=runner_lit_texel(c,light);
            failures+=(lit&0x8000)!=(c&0x8000) || ((lit==0)!=(c==0));
            for(int ch=0;ch<3;ch++) failures+=((lit>>(ch*5))&31)<((c>>(ch*5))&31);
        }
        failures+=runner_lit_texel(0x7fff,light)!=0x7fff || runner_lit_texel(0xffff,light)!=0xffff;
    }
    printf("runner lighting: 65536 palette words per stage and %d world/stage pairs, %d failures\n",pairs,failures);
    return failures!=0;
}

static int reach_atmosphere_check(const char *dir) {
    const Level *lv=0;
    char name[512];FILE *csv;
    int i,pass,s,frames=0,failures=0;
    /* Reverse stage transitions exercise persistent lighting/table caches. */
    static const int stages[]={0,1,2,1,0};
    for(i=0;i<level_count;i++) if(!strcmp(levels[i]->track,"reach.ridge")) lv=levels[i];
    if(!lv) return 1;
    snprintf(name,sizeof name,"%s/atmosphere.csv",dir);csv=fopen(name,"w");if(!csv) return 1;
    fprintf(csv,"stage,scroll,bytes,projections,atmosphere_primitives,failures\n");
    visibility_enabled=1;atmosphere_check_enabled=1;
    for(pass=0;pass<5;pass++) for(s=0;s<=lv->stages[stages[pass]].finish;s+=1024) {
        LevelFrame f={s,20,37,0,stages[pass]};Cost c;int r,g,b,bad;
        uint8_t expected[3];
        atmosphere_failures=atmosphere_primitives=0;
        lv->clear_color(&f,&r,&g,&b);expected[0]=r;expected[1]=g;expected[2]=b;
        c=frame(lv,&f,1,0,0);
        bad=atmosphere_failures+road_preview_atmosphere_check(expected);
        bad+=atmosphere_primitives<20 || c.bytes>=BUFFER_LENGTH || road_preview_far_limit_hits;
        failures+=bad;frames++;
        fprintf(csv,"%d,%d,%zu,%ld,%d,%d\n",f.stage,s,c.bytes,c.proj,atmosphere_primitives,bad);
        if(pass<3 && s%8192==0) {
            snprintf(name,sizeof name,"%s/atmosphere-%d-%d.png",dir,f.stage,s);write_png(name);
        }
    }
    visibility_enabled=atmosphere_check_enabled=0;
    fclose(csv);
    printf("Reach atmosphere: %d camera poses, %d failures\n",frames,failures);
    return failures!=0;
}

static int compiled_mesh_check(const char *dir) {
    static unsigned char reference[sizeof fb],candidate[sizeof fb];
    const Level *lv=0;
    char name[512];FILE *csv;
    int i,s,o,stage,frames=0,failures=0,worst_pixels=0,worst_s=0,worst_unmatched=0;
    double worst_mean=0,stage_worst[3]={0,0,0};
    for(i=0;i<level_count;i++) if(!strcmp(levels[i]->track,"reach.ridge")) lv=levels[i];
    if(!lv) return 1;
    snprintf(name,sizeof name,"%s/compiled-mesh.csv",dir);csv=fopen(name,"w");if(!csv) return 1;
    fprintf(csv,"stage,scroll,mean_delta,strong_pixels,unmatched_pixels,reference_samples,compiled_samples,reference_bytes,compiled_bytes\n");
    for(stage=0;stage<lv->stage_count;stage++)
    for(s=0;s<=lv->stages[stage].finish;s+=256) for(o=0;o<3;o++) {
        LevelFrame f={s+(o==0?0:o==1?127:255),20,37,0,stage};
        Cost a,b;unsigned long delta=0;int strong=0,unmatched=0,p;
        path_reset();road_preview_legacy_mesh=1;
        a=frame(lv,&f,1,0,0);memcpy(reference,fb,sizeof fb);
        path_reset();road_preview_legacy_mesh=0;
        b=frame(lv,&f,1,0,0);memcpy(candidate,fb,sizeof fb);
        for(p=0;p<SCREEN_W*SCREEN_H;p++) {
            int c,high=0;
            for(c=0;c<3;c++) { int d=iabs(reference[p*3+c]-((unsigned char*)fb)[p*3+c]);delta+=d;if(d>24) high=1; }
            strong+=high;
            if(high) {
                int dx,dy,x=p%SCREEN_W,y=p/SCREEN_W,near_ref=0,near_candidate=0;
                for(dy=-1;dy<=1;dy++) for(dx=-1;dx<=1;dx++) {
                    int xx=x+dx,yy=y+dy,q,match_ref=1,match_candidate=1;
                    if(xx<0||xx>=SCREEN_W||yy<0||yy>=SCREEN_H) continue;
                    q=(yy*SCREEN_W+xx)*3;
                    for(c=0;c<3;c++) {
                        if(iabs(reference[q+c]-((unsigned char*)fb)[p*3+c])>8) match_ref=0;
                        if(iabs(((unsigned char*)fb)[q+c]-reference[p*3+c])>8) match_candidate=0;
                    }
                    near_ref|=match_ref;near_candidate|=match_candidate;
                }
                unmatched+=!near_ref||!near_candidate;
            }
        }
        double mean=(double)delta/sizeof fb;
        if(mean>worst_mean) {
            worst_mean=mean;worst_s=f.scroll;
            snprintf(name,sizeof name,"%s/mesh-candidate.png",dir);write_png(name);
            memcpy(fb,reference,sizeof fb);
            snprintf(name,sizeof name,"%s/mesh-reference.png",dir);write_png(name);
        }
        if(mean>=stage_worst[stage]) {
            stage_worst[stage]=mean;
            memcpy(fb,candidate,sizeof fb);
            snprintf(name,sizeof name,"%s/mesh-stage%d-candidate.png",dir,stage);write_png(name);
            memcpy(fb,reference,sizeof fb);
            snprintf(name,sizeof name,"%s/mesh-stage%d-reference.png",dir,stage);write_png(name);
        }
        if(strong>worst_pixels) worst_pixels=strong;
        if(unmatched>worst_unmatched) worst_unmatched=unmatched;
        /* Allow at most 0.1% of the native image for residual fixed-point
         * edge/dither differences; geometric LOD is independently checked. */
        if(mean>0.15 || unmatched>SCREEN_W*SCREEN_H/1000 || b.bytes>=BUFFER_LENGTH) failures++;
        fprintf(csv,"%d,%d,%.6f,%d,%d,%lu,%lu,%zu,%zu\n",stage,f.scroll,mean,strong,unmatched,a.samples,b.samples,a.bytes,b.bytes);frames++;
    }
    fclose(csv);road_preview_legacy_mesh=0;
    printf("compiled mesh: %d frames, %d failures; worst mean %.6f at %d, strong pixels %d, unmatched beyond one pixel %d\n",frames,failures,worst_mean,worst_s,worst_pixels,worst_unmatched);
    return failures!=0;
}

static int pipeline_check(const char *dir) {
    static uint8_t reference[sizeof fb];
    static const int offsets[]={0,127,255};
    int i,stage,s,o,alt,failures=0,pairs=0;
    char name[512]; FILE *csv;
    XY p;
    if(project(2000,170,40000,&p)!=40000 || iabs(p.x-168)>1 || iabs(p.y-72)>1) {
        fprintf(stderr,"far GTE projection wrapped or changed scale\n"); return 1;
    }
    snprintf(name,sizeof name,"%s/pipeline.csv",dir);
    csv=fopen(name,"w"); if(!csv) return 1;
    fprintf(csv,"world,stage,track,scroll,alternate,pixel_mismatch,reference_samples,compiled_samples,reference_transforms,compiled_transforms,bytes,far_budget_exhausted\n");
    for(i=0;i<level_count;i++) {
        const Level *lv=levels[i];
        int stages=lv->stage_count?lv->stage_count:1;
        for(stage=0;stage<stages;stage++) {
            const char *track=lv->stage_count?lv->stages[stage].track:lv->track;
            int end=lv->stage_count?lv->stages[stage].finish:65536;
            long old_samples=0,new_samples=0; int count=0,saved=0;
            path_open(track);
            for(alt=0;alt<=(path_branches()>0);alt++)
            for(s=0;s<=end;s+=2048) for(o=0;o<3;o++) {
                LevelFrame f={s+offsets[o],20,(s+offsets[o])/20+37,0,stage};
                Cost a,b; int mismatch;
                path_reset(); road_preview_legacy_surfaces=1; road_preview_disable_occlusion=1;
                a=frame(lv,&f,1,alt,0); memcpy(reference,fb,sizeof fb);
                path_reset(); road_preview_legacy_surfaces=0; road_preview_disable_occlusion=0;
                road_preview_reset_haze(); road_preview_far_limit_hits=0;
                b=frame(lv,&f,1,alt,0);
                mismatch=memcmp(reference,fb,sizeof fb)!=0;
                if(mismatch && !saved) {
                    snprintf(name,sizeof name,"%s/%s-candidate.png",dir,track); write_png(name);
                    memcpy(fb,reference,sizeof fb);
                    snprintf(name,sizeof name,"%s/%s-reference.png",dir,track); write_png(name);
                    saved=1;
                }
                if(mismatch || b.bytes>=BUFFER_LENGTH || road_preview_far_limit_hits) {
                    if(failures<5) fprintf(stderr,"pipeline mismatch/budget: %s stage %d at %d alt %d: pixels %d, bytes %zu, far %d\n",lv->name,stage,f.scroll,alt,mismatch,b.bytes,road_preview_far_limit_hits);
                    failures++;
                }
                fprintf(csv,"%s,%d,%s,%d,%d,%d,%lu,%lu,%lu,%lu,%zu,%d\n",lv->name,stage,track,f.scroll,alt,mismatch,a.samples,b.samples,a.transforms,b.transforms,b.bytes,road_preview_far_limit_hits);
                old_samples+=a.samples;new_samples+=b.samples;count++;pairs++;
            }
            printf("pipeline %s: %d frame pairs, samples %ld -> %ld\n",track,count,old_samples/count,new_samples/count);
            fflush(stdout);
        }
    }
    fclose(csv);road_preview_legacy_surfaces=0;
    printf("pipeline: %d frame pairs, %d failures\n",pairs,failures);
    return failures!=0;
}

static int shake_check(void) {
    static const int offsets[][2]={{1,0},{-1,1},{1,0},{-1,-1},{0,0},{0,0},{9,-9},{0,0}};
    int failures=0;
    for(int repeat=0;repeat<2;repeat++) for(int i=0;i<8;i++) {
        int buffer=world_buffer(),x=offsets[i][0],y=offsets[i][1];
        world_set_shake(x,y);world_flip();
        failures+=last_draw_env.ofs[0]!=clamp(x,-1,1) || last_draw_env.ofs[1]!=clamp(y,-1,1);
        failures+=last_draw_env.clip.y!=buffer*SCREEN_H || last_draw_env.clip.h!=SCREEN_H;
        failures+=last_draw_env.clip.x!=0 || last_draw_env.clip.w!=SCREEN_W;
    }
    printf("shake: both framebuffer origins, bounded offsets and reset, %d failures\n",failures);
    return failures!=0;
}

int main(int argc, char **argv) {
    static const struct { const char *tag; int scroll, speed, alt, stage; } shots[] = {
        {"run_a", 2600, 20, 0, 0}, {"run_b", 15000, 20, 0, 0}, {"run_c", 31337, 20, 0, 0},
        {"run_d", 47210, 20, 0, 0}, {"paused", 15000, 0, 0, 0}, {"select", 6000, 8, 0, 0},
        {"crest", 10600, 20, 0, 0}, {"brink", 12100, 20, 0, 0}, {"top", 37000, 20, 0, 0},
        {"lap", 63600, 20, 0, 0}, {"flip", 16900, 20, 0, 0}, {"sweep", 30000, 20, 0, 0},
        {"fork", 1200, 20, 0, 0}, {"apart", 9000, 20, 0, 0}, {"other", 9000, 20, 1, 0},
        {"merge", 17400, 20, 0, 0}, {"narrow", 5200, 20, 0, 0}, {"single", 14200, 20, 0, 0},
        {"late", 24000, 20, 0, 2},
    };
    const char *dir = argc > 1 ? argv[1] : ".";
    int i, j;
    world_init();
    if(argc>2 && !strcmp(argv[2],"--shake-check")) return shake_check();
    if(argc>2 && !strcmp(argv[2],"--finish-gate-check")) return finish_gate_check(dir);
    if(argc>2 && !strcmp(argv[2],"--runner-lighting-check")) return runner_lighting_check(dir);
    if(argc>2 && !strcmp(argv[2],"--atmosphere-check")) return reach_atmosphere_check(dir);
    if(argc>2 && !strcmp(argv[2],"--mesh-check")) return compiled_mesh_check(dir);
    if(argc>2 && !strcmp(argv[2],"--pipeline-check")) return pipeline_check(dir);
    if (argc > 2 && !strcmp(argv[2],"--countdown-check"))
        return countdown_check(dir);
    if (argc > 2 && !strcmp(argv[2],"--city-check"))
        return city_check(dir);
    if (argc > 2 && !strcmp(argv[2],"--vesper-motion-check"))
        return vesper_motion_check();
    if (argc > 2 && !strcmp(argv[2],"--texture-self-test"))
        return texture_self_test();
    if (argc > 2 && !strcmp(argv[2],"--visibility-self-test"))
        return visibility_self_test();
    if (argc > 2 && !strcmp(argv[2],"--visibility"))
        return visibility_world(dir,argc > 3 ? atoi(argv[3]) : 128);
    if (argc > 2 && !strcmp(argv[2],"--tunnel-check"))
        return tunnel_check(dir);
    if (argc > 2 && !strcmp(argv[2], "--compare")) {
        return compare_world(dir,1);
    }
    if (argc > 2 && !strcmp(argv[2],"--compare-review"))
        return compare_world(dir,0);
    if (argc > 2 && !strcmp(argv[2], "--distance"))
        return distance_world(dir);
    if (argc > 4 && (!strcmp(argv[2], "--shot") || !strcmp(argv[2], "--runner-shot"))) {
        LevelFrame f = {atoi(argv[4]), 20, argc > 5 ? atoi(argv[5]) : 0, 0, atoi(argv[3])};
        char path[512];
        for (i = 0; i < level_count; i++)
            if (!strcmp(levels[i]->track, getenv("RUNNER_PREVIEW_TRACK") ? getenv("RUNNER_PREVIEW_TRACK") : "sine.gate")) {
                snprintf(path, sizeof path, "%s/shot.png", dir);
                frame(levels[i], &f, !strcmp(argv[2],"--runner-shot")?3:1, 0, path);
                return 0;
            }
        return 1;
    }
    world_mesh_preview_legacy = getenv("RUNNER_PREVIEW_LEGACY") != 0;
    for (i = 0; i < level_count; i++) {
        const Level *lv = levels[i];
        char name[64], path[512];
        Cost worst = {0};
        double proj = 0, fill = 0, bytes = 0;
        int frames = 0, s;
        unsigned long samples = 0, transforms = 0;
        const char *filter = getenv("RUNNER_PREVIEW_LEVEL");
        if (filter && !strstr(lv->name, filter))
            continue;
        slug(name, lv->name);
        for (j = 0; j < (int)(sizeof shots / sizeof shots[0]); j++) {
            LevelFrame f = {shots[j].scroll, shots[j].speed, shots[j].scroll / 20 + 37, 0, shots[j].stage};
            Cost c;
            snprintf(path, sizeof path, "%s/%s_%s.png", dir, name, shots[j].tag);
            c = frame(lv, &f, 1, shots[j].alt, path);
            printf("%-10s %-7s bytes %5zu  F4 %4d (blend %3d)  G4 %d  proj %5ld  fill %6ld (blend %5ld)\n",
                   lv->name, shots[j].tag, c.bytes, c.n.f4, c.n.blended, c.n.g4, c.proj, c.fill, c.blend);
        }
        /* A minute of running, for averages and the worst frame. */
        for (s = 0; s < 3600; s++) {
            LevelFrame f = {s * 20, 20, s, 0, 0};
            Cost c = frame(lv, &f, 0, 0, 0);
            if (c.bytes > worst.bytes) worst.bytes = c.bytes;
            if (c.proj > worst.proj) worst.proj = c.proj;
            if (c.fill > worst.fill) worst.fill = c.fill;
            if (c.blend > worst.blend) worst.blend = c.blend;
            if (c.n.f4 > worst.n.f4) worst.n.f4 = c.n.f4;
            proj += (double)c.proj;
            fill += (double)c.fill;
            bytes += (double)c.bytes;
            samples += c.samples;
            transforms += c.transforms;
            frames++;
        }
        printf("%-10s 60 s run: bytes avg %.0f max %zu of %d | F4 max %d | proj avg %.0f max %ld | fill avg %.0f max %ld | blend px max %ld\n",
               lv->name, bytes / frames, worst.bytes, BUFFER_LENGTH, worst.n.f4,
               proj / frames, worst.proj, fill / frames, worst.fill, worst.blend);
        printf("%-10s path work: samples avg %lu | transforms avg %lu\n",
               lv->name, samples / frames, transforms / frames);
        if (lv->stage_count > 1) {
            int stg;
            for (stg = 0; stg < lv->stage_count && stg < 3; stg++) {
                Cost sw = {0};
                long sp = 0, sf = 0;
                unsigned long ss = 0, st = 0;
                int sn = 0;
                for (s = 0; s < 180; s++) {
                    LevelFrame f = {s * 240, 20, s * 3, 0, stg};
                    Cost c = frame(lv, &f, 0, 0, 0);
                    if (c.bytes > sw.bytes) sw.bytes = c.bytes;
                    if (c.proj > sw.proj) sw.proj = c.proj;
                    if (c.fill > sw.fill) sw.fill = c.fill;
                    sp += c.proj;
                    sf += c.fill;
                    ss += c.samples;
                    st += c.transforms;
                    sn++;
                }
                printf("%-10s stage %d: proj avg %ld max %ld | bytes max %zu | fill avg %ld max %ld\n",
                       lv->name, stg, sp / sn, sw.proj, sw.bytes, sf / sn, sw.fill);
                printf("%-10s stage %d path work: samples avg %lu | transforms avg %lu\n",
                       lv->name, stg, ss / sn, st / sn);
            }
        }
    }
    return 0;
}
