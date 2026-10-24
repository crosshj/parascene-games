#include <psxgpu.h>
#include "world.h"
#include "music_credit.h"
#include "credit_font_data.h"

static uint16_t credit_page, credit_clut;

void music_credit_init(void) {
    RECT rect;
    /* Beside the debug font; above the city facade atlas. */
    setRECT(&rect, 992, 0, 32, CREDIT_FONT_ATLAS_H);
    LoadImage(&rect, (const uint32_t *)credit_font_pixels);
    DrawSync(0);
    setRECT(&rect, 992, CREDIT_FONT_ATLAS_H, 16, 1);
    LoadImage(&rect, (const uint32_t *)credit_font_palette);
    DrawSync(0);
    credit_page = getTPage(0, 0, 960, 0);
    credit_clut = getClut(992, CREDIT_FONT_ATLAS_H);
}

static int glyph(unsigned char c) {
    return (c >= 32 && c < 127 ? c : '?') - 32;
}

static int small_width(const char *s) {
    int width = 0;
    for (; *s; s++) width += credit_font_width[glyph((unsigned char)*s)] + 1;
    return width ? width - 1 : 0;
}

static void small_text(int x, int y, int right, const char *s, int r, int g, int b) {
    for (; *s; s++) {
        int i = glyph((unsigned char)*s), width = credit_font_width[i];
        if (x + width > right) break;
        if (*s != ' ')
            tex_sprite(x, y, width, CREDIT_FONT_GLYPH_H,
                       128 + (i % 16) * 8, (i / 16) * CREDIT_FONT_CELL_H,
                       r, g, b, credit_page, credit_clut);
        x += width + 1;
    }
}

void music_credit_draw(const char *artist, const char *title, int age) {
    int n, width, x, rise, fade = 128;
    if (age < 0 || age >= MUSIC_CREDIT_END)
        return;
    n = small_width(title);
    if (small_width(artist) > n)
        n = small_width(artist);
    width = clamp(n + 31, 92, 280);
    rise = age < 12 ? age * age * 128 / 144 : 128;
    x = 8 - width * (128 - rise) / 128;
    if (age > MUSIC_CREDIT_HOLD)
        fade = (MUSIC_CREDIT_END - age) * 128 / MUSIC_CREDIT_FADE;

    /* Bucket zero prepends packets: submit the foreground first, then its
     * accent and background. The GPU draws these in the opposite order. */
    small_text(x + 25, 212, x + width - 6, artist,
               96 * fade / 128, 100 * fade / 128, 118 * fade / 128);
    small_text(x + 25, 222, x + width - 6, title, fade, fade, fade);
    tex_sprite(x + 6, 216, 8, 9, 248, 80, 112 * fade / 128,
               102 * fade / 128, 128 * fade / 128, credit_page, credit_clut);
    fill_rect(x, 213, 2, 16, 226 * fade / 128, 72 * fade / 128, 212 * fade / 128);
    fill_rect(x, 212, width, 18, 24 * fade / 128, 28 * fade / 128, 42 * fade / 128);
    fill_rect(x + 2, 210, width - 4, 22, 24 * fade / 128, 28 * fade / 128, 42 * fade / 128);
}
