/* The GTE calls the renderer makes, computed on the host. */

#pragma once
#include <psxgte.h>

void host_gte_offset(int x, int y);
void host_gte_screen(int h);
void host_gte_ldv0(const SVECTOR *v);
void host_gte_rtps(void);
void host_gte_stsxy(void *xy);
void host_gte_stsz(int *z);

#define gte_SetGeomOffset(x, y) host_gte_offset(x, y)
#define gte_SetGeomScreen(h) host_gte_screen(h)
#define gte_SetRotMatrix(m) ((void)(m))
#define gte_SetTransMatrix(m) ((void)(m))
#define gte_ldv0(v) host_gte_ldv0((const SVECTOR *)(v))
#define gte_rtps() host_gte_rtps()
#define gte_stsxy(p) host_gte_stsxy((void *)(p))
#define gte_stsz(p) host_gte_stsz((int *)(p))
