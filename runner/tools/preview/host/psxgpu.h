/* The SDK header with primitive linking routed to the preview, since a
 * host pointer does not fit the 24-bit link field. */

#pragma once
#include_next <psxgpu.h>

void host_add_prim(uint32_t *ot, void *prim);

#undef addPrim
#define addPrim(ot, p) host_add_prim((uint32_t *)(ot), (void *)(p))
