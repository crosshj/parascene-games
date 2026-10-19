/* Host check: a silence row and a music row pack to the 320 bits in docs/web.md. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "web_row.h"

static int fail(const char *msg) {
    fprintf(stderr, "web row: %s\n", msg);
    return 1;
}

static int checksum_ok(const uint8_t row[WEB_ROW_BYTES]) {
    int i;
    int sum = 0;
    int len = row[5];
    if (len > 33)
        return 0;
    for (i = 0; i < len; i++)
        sum += row[7 + i];
    return (sum & 0xFF) == row[6];
}

static int pixels_match(const uint8_t row[WEB_ROW_BYTES], const uint16_t px[320]) {
    int i;
    for (i = 0; i < 320; i++) {
        int bit = (row[i >> 3] >> (7 - (i & 7))) & 1;
        uint16_t want = (uint16_t)(bit ? 0x7FFF : 0x0000);
        if (px[i] != want)
            return 0;
    }
    return 1;
}

static int check_row(const uint8_t row[WEB_ROW_BYTES], const uint8_t *payload, int len) {
    uint16_t px[320];
    int i;
    if (row[0] != 0x57 || row[1] != 0x45 || row[2] != 0x42 || row[3] != 0x31)
        return fail("magic");
    if (row[4] != 1)
        return fail("version");
    if (row[5] != len)
        return fail("length");
    if (!checksum_ok(row))
        return fail("checksum");
    for (i = 0; i < len; i++) {
        if (row[7 + i] != payload[i])
            return fail("payload");
    }
    for (i = 7 + len; i < WEB_ROW_BYTES; i++) {
        if (row[i] != 0)
            return fail("tail");
    }
    web_row_pixels(row, px);
    if (!pixels_match(row, px))
        return fail("pixels");
    /* 0x57 is pixels 0,1,0,1,0,1,1,1 — the high bit is the first pixel. */
    if (px[0] != 0 || px[1] != 0x7FFF || px[2] != 0 || px[3] != 0x7FFF ||
        px[4] != 0 || px[5] != 0x7FFF || px[6] != 0x7FFF || px[7] != 0x7FFF)
        return fail("0x57 bits");
    return 0;
}

int main(void) {
    uint8_t row[WEB_ROW_BYTES];
    uint8_t silence[2] = { WEB_CMD_SILENCE, 0 };
    uint8_t music[5] = { WEB_CMD_MUSIC, 3, 3, 1, 40 };
    uint8_t broken[WEB_ROW_BYTES];

    web_row_silence(row);
    if (check_row(row, silence, 2))
        return 1;
    web_row_music(row, 3, 1, 40);
    if (check_row(row, music, 5))
        return 1;
    memcpy(broken, row, sizeof row);
    broken[6] ^= 0xFF;
    if (checksum_ok(broken))
        return fail("bad checksum accepted");
    printf("web row ok\n");
    return 0;
}
