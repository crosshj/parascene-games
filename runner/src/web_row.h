/* 40-byte command row painted on the bottom line of the web disc.
 * docs/web.md is the contract the page reads. */
#pragma once

#include <stdint.h>

#define WEB_ROW_BYTES 40
#define WEB_ROW_MAGIC0 0x57
#define WEB_ROW_MAGIC1 0x45
#define WEB_ROW_MAGIC2 0x42
#define WEB_ROW_MAGIC3 0x31
#define WEB_ROW_VERSION 1

#define WEB_CMD_END 0
#define WEB_CMD_SILENCE 1
#define WEB_CMD_MUSIC 2

void web_row_silence(uint8_t row[WEB_ROW_BYTES]);
void web_row_music(uint8_t row[WEB_ROW_BYTES], int sequence, int track, int volume);
void web_row_pixels(const uint8_t row[WEB_ROW_BYTES], uint16_t px[320]);
