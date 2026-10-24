/* Exercise the console HUD code on the host, preserving PS1 packet order.
 * Include world.c so the harness can initialize its private packet buffer
 * without replacing any of its text, rectangle or allocation functions. */
#include <stdio.h>
#include <stdlib.h>
#include "../src/world.c"
#include "../src/music_credit.h"
#include "music_tracks.h"

uint16_t _font_tpage = 15;
uint16_t _font_clut;

int LoadImage(const RECT *rect, const uint32_t *data) { (void)rect; (void)data; return 0; }
int DrawSync(int mode) { (void)mode; return 0; }

typedef struct { uintptr_t bucket; int sequence; void *packet; } Packet;
static Packet packets[512];
static int packet_count;

void host_add_prim(uint32_t *ot, void *packet) {
    if (packet_count == 512) abort();
    packets[packet_count] = (Packet){(uintptr_t)ot, packet_count, packet};
    packet_count++;
}

static int order(const void *a, const void *b) {
    const Packet *p = a, *q = b;
    if (p->bucket != q->bucket) return p->bucket < q->bucket ? 1 : -1;
    return q->sequence - p->sequence;
}

int main(void) {
    static const int ages[] = {-1, 0, 6, 12, 60, MUSIC_CREDIT_HOLD,
                               MUSIC_CREDIT_HOLD+18, MUSIC_CREDIT_END-1, MUSIC_CREDIT_END};
    int track, a, i;
    music_credit_init();
    for (track = 0; track < MUSIC_TRACK_COUNT; track++) {
        for (a = 0; a < (int)(sizeof ages / sizeof ages[0]); a++) {
            ctx.active = 0;
            ctx.next = ctx.buffers[0].buffer;
            packet_count = 0;
            music_credit_draw(music_artist[track], music_title[track], ages[a]);
            qsort(packets, packet_count, sizeof packets[0], order);
            printf("frame %d %d\n", track, ages[a]);
            for (i = 0; i < packet_count; i++) {
                int code = getcode(packets[i].packet) & 0xfc;
                if (code == 0x60) {
                    const TILE *p = packets[i].packet;
                    printf("rect %d %d %d %d %d %d %d\n",
                           p->x0, p->y0, p->w, p->h, p->r0, p->g0, p->b0);
                } else if (code == 0x74) {
                    const SPRT_8 *p = packets[i].packet;
                    printf("glyph %d %d %d %d %d %d %d\n",
                           p->x0, p->y0, p->u0, p->v0, p->r0, p->g0, p->b0);
                } else if (code == 0x64) {
                    const SPRT *p = packets[i].packet;
                    printf("glyph %d %d %d %d %d %d %d %d %d\n",
                           p->x0, p->y0, p->u0, p->v0, p->r0, p->g0, p->b0, p->w, p->h);
                } else if (code != 0xe0) {
                    fprintf(stderr, "Unexpected HUD packet %x\n", code);
                    return 1;
                }
            }
            puts("end");
        }
    }
    return 0;
}
