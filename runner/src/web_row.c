#include "web_row.h"

#include <string.h>

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void finish(uint8_t row[WEB_ROW_BYTES], const uint8_t *payload, int len) {
    int i;
    int sum = 0;
    memset(row, 0, WEB_ROW_BYTES);
    row[0] = WEB_ROW_MAGIC0;
    row[1] = WEB_ROW_MAGIC1;
    row[2] = WEB_ROW_MAGIC2;
    row[3] = WEB_ROW_MAGIC3;
    row[4] = WEB_ROW_VERSION;
    row[5] = (uint8_t)len;
    for (i = 0; i < len; i++) {
        row[7 + i] = payload[i];
        sum += payload[i];
    }
    row[6] = (uint8_t)sum;
}

void web_row_silence(uint8_t row[WEB_ROW_BYTES]) {
    uint8_t payload[2];
    payload[0] = WEB_CMD_SILENCE;
    payload[1] = 0;
    finish(row, payload, 2);
}

void web_row_music(uint8_t row[WEB_ROW_BYTES], int sequence, int track, int volume) {
    uint8_t payload[5];
    payload[0] = WEB_CMD_MUSIC;
    payload[1] = 3;
    payload[2] = (uint8_t)clamp_int(sequence, 1, 255);
    payload[3] = (uint8_t)clamp_int(track, 0, 255);
    payload[4] = (uint8_t)clamp_int(volume, 0, 128);
    finish(row, payload, 5);
}

void web_row_pixels(const uint8_t row[WEB_ROW_BYTES], uint16_t px[320]) {
    int i;
    for (i = 0; i < 320; i++) {
        int bit = (row[i >> 3] >> (7 - (i & 7))) & 1;
        px[i] = (uint16_t)(bit ? 0x7FFF : 0x0000);
    }
}
