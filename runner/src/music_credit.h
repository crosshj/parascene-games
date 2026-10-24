#pragma once

#define MUSIC_CREDIT_START_DELAY 60
#define MUSIC_CREDIT_HOLD 360
#define MUSIC_CREDIT_FADE 36
#define MUSIC_CREDIT_END (MUSIC_CREDIT_HOLD + MUSIC_CREDIT_FADE)

void music_credit_init(void);
/* age uses the game's 60 Hz clock. Hidden after the short credit animation. */
void music_credit_draw(const char *artist, const char *title, int age);
