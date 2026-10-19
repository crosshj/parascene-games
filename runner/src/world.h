/* The renderer every level shares: double-buffered ordering tables,
 * GTE projection, and flat polygon helpers. Camera space has x to the
 * right, y up from the ground, and z into the screen. */

#pragma once

#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define OT_LENGTH 512
#define OT_SHIFT 4
#define BUFFER_LENGTH (32 * 1024)

#define CLIP 64
#define FAR 4200
#define CAM_H 170
#define HORIZON_Y 72

/* Vertices past this screen range make the GPU drop a stray triangle. */
#define SAFE_X0 (-200)
#define SAFE_X1 (SCREEN_W + 200)

/* Semi-transparency modes for poly_f4 and the GPU's abr field. */
#define BLEND_NONE (-1)
#define BLEND_AVG 0
#define BLEND_ADD 1
#define BLEND_SUB 2
#define BLEND_QUARTER 3

typedef struct {
    int16_t x, y;
} __attribute__((aligned(4))) XY;

/* Darkens or tints a color for its distance z. */
typedef void (*Shade)(int *r, int *g, int *b, int z);

static inline int iabs(int v) { return v < 0 ? -v : v; }

static inline int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void world_init(void);
void world_flip(void);
void world_set_clear(int r, int g, int b);
/* After the buffer about to be shown has been cleared and drawn, copy 320
 * 15-bit pixels onto its bottom row. Used by the web disc's command line. */
void world_stamp_row(const uint16_t *px);
size_t world_bytes_used(void);

/* Bucket 0 draws last. Returns 0 when the frame's primitive buffer is full. */
void *alloc_prim(int bucket, size_t size);
/* Links a primitive the caller owns into this frame's ordering table, for
 * scenery built once instead of every frame. A primitive cannot be in both
 * tables at once, so keep a copy per buffer and pick it by world_buffer(). */
void world_link(int bucket, void *prim);
int world_buffer(void);
void text(int x, int y, const char *s);

int project(int x, int y, int z, XY *out);
void dim(int *r, int *g, int *b, int z);
int xy_ok(const XY *p);
int edge_ok(XY a, XY b);
void x_limits(int z, int *lo, int *hi);

/* One screen-space quad in setXY4 order: p0 and p1 are one edge, p2 and p3
 * the opposite edge. Quads the GPU cannot draw safely are skipped. */
void poly_f4(const XY *p0, const XY *p1, const XY *p2, const XY *p3,
             int bucket, int r, int g, int b, int blend);
/* The same for four points in an array, tested once by their bounding box:
 * it is skipped when off screen or too large for the GPU to draw safely.
 * Cheaper than poly_f4 for scenery that emits hundreds of quads. */
void poly_f4v(const XY *p, int bucket, int r, int g, int b, int blend);
/* A one-pixel screen-space line. */
void line_f2(XY a, XY b, int bucket, int r, int g, int bl);
/* One screen-space gouraud quad. Colours are per corner in setXY4 order. */
void poly_g4(const XY *p0, const XY *p1, const XY *p2, const XY *p3,
             int bucket,
             int r0, int g0, int b0, int r1, int g1, int b1,
             int r2, int g2, int b2, int r3, int g3, int b3, int blend);
void gradient(int x, int y, int w, int h, int bucket,
              int r0, int g0, int b0, int r1, int g1, int b1);
/* Dithering on or off for what draws after this in the bucket. Prims
 * allocated later in the same bucket draw before it. */
void dither_mode(int bucket, int on);

void quad(int x0, int y0, int z0, int x1, int y1, int z1,
          int x2, int y2, int z2, int x3, int y3, int z3,
          int sortz, int r, int g, int b);
void face(int x0, int y0, int z0, int x1, int y1, int z1,
          int x2, int y2, int z2, int x3, int y3, int z3,
          int sortz, int r, int g, int b);

/* A horizontal quad, sliced across x. sort_far keeps a ground strip behind
 * objects standing on it. */
void slab(int xa, int z0, int xb, int z1, int y, int r, int g, int b,
          int sort_far, Shade shade);
void flat(int xa, int z0, int xb, int z1, int y, int r, int g, int b);
void lid(int xa, int z0, int xb, int z1, int y, int r, int g, int b);
void wall(int xa, int xb, int z, int ya, int yb, int r, int g, int b);
void side(int x, int z0, int z1, int ya, int yb, int r, int g, int b);
