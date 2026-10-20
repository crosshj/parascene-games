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

void web_row_frame(uint8_t row[WEB_ROW_BYTES], int playing,
                   int sequence, int track, int volume, int save_gen) {
    uint8_t payload[8];
    int len = 0;
    if (playing) {
        payload[len++] = WEB_CMD_MUSIC;
        payload[len++] = 3;
        payload[len++] = (uint8_t)clamp_int(sequence, 1, 255);
        payload[len++] = (uint8_t)clamp_int(track, 0, 255);
        payload[len++] = (uint8_t)clamp_int(volume, 0, 128);
    } else {
        payload[len++] = WEB_CMD_SILENCE;
        payload[len++] = 0;
    }
    if (save_gen > 0) {
        payload[len++] = WEB_CMD_SAVE;
        payload[len++] = 1;
        payload[len++] = (uint8_t)clamp_int(save_gen, 1, 255);
    }
    finish(row, payload, len);
}

void web_row_silence(uint8_t row[WEB_ROW_BYTES]) {
    web_row_frame(row, 0, 0, 0, 0, 0);
}

void web_row_music(uint8_t row[WEB_ROW_BYTES], int sequence, int track, int volume) {
    web_row_frame(row, 1, sequence, track, volume, 0);
}

void web_row_pixels(const uint8_t row[WEB_ROW_BYTES], uint16_t px[320]) {
    int i;
    for (i = 0; i < 320; i++) {
        int bit = (row[i >> 3] >> (7 - (i & 7))) & 1;
        px[i] = (uint16_t)(bit ? 0x7FFF : 0x0000);
    }
}
