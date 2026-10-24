/* Use the actual controller decoding, playlist cursor and web commands. */
#include <assert.h>
#include <stdio.h>
#include <setjmp.h>
#undef SEEK_SET
#undef SEEK_CUR
#undef SEEK_END
typedef jmp_buf JumpBuffer;
#define getc psx_getc
#define putc psx_putc
#include <psxapi.h>
#undef getc
#undef putc
#define RUNNER_WEB
#define main runner_game_main
#include "../src/main.c"
#undef main

int save_generation(void) { return 0; }

static void buttons(uint16_t mask) {
    PADTYPE *pad = (PADTYPE *)pad_buff[0];
    pad->stat = 0;
    pad->type = PAD_ID_DIGITAL;
    pad->btn = (uint16_t)~mask;
    held = pad_held();
    music_input();
    prev = held;
}

int main(void) {
    int i, at, seq, order[MUSIC_TRACK_COUNT];
    assert(MUSIC_TRACK_COUNT > 1);
    for (i = 0; i < MUSIC_TRACK_COUNT; i++)
        order[i] = music_order[i] = MUSIC_TRACK_COUNT - 1 - i;
    state = ST_RUN;
    cd_on = cd_started = music_on = 1;
    music_at = 0;
    music_cur = music_order[0];
    music_notice_delay = 60;
    web_seq = 254;
    buttons(PAD_R1);
    assert(music_at == 1 && music_cur == order[1] && web_seq == 255);
    assert(music_notice_age == 0 && music_notice_delay == 0);
    assert(cd_wait == music_web_frames[music_cur]);
    assert(web_bytes[10] == music_cur);
    buttons(PAD_R1);
    assert(music_at == 1 && web_seq == 255); /* No repeat while held. */
    buttons(0); buttons(PAD_L1);
    assert(music_at == 0 && music_cur == order[0] && web_seq == 1);
    buttons(0); buttons(PAD_L1);
    assert(music_at == MUSIC_TRACK_COUNT-1 && music_cur == order[MUSIC_TRACK_COUNT-1]);
    buttons(0); buttons(PAD_R1);
    assert(music_at == 0 && music_cur == order[0]);
    at = music_at; seq = web_seq;
    buttons(0); buttons(PAD_L1 | PAD_R1);
    assert(music_at == at && web_seq == seq);
    state = ST_TITLE;
    buttons(0); buttons(PAD_R1);
    assert(music_at == at && web_seq == seq);
    state = ST_RUN; music_menu_active = 1;
    buttons(0); buttons(PAD_R1);
    assert(music_at == at && web_seq == seq);
    music_menu_active = 0; music_fade = 1;
    buttons(0); buttons(PAD_R1);
    assert(music_at == at && web_seq == seq);
    music_fade = 0; state = ST_PAUSE; music_level = MUSIC_PAUSED;
    buttons(0); buttons(PAD_R1);
    assert(music_at == 1 && music_level == MUSIC_PAUSED && web_bytes[11] == MUSIC_PAUSED);
    state = ST_COUNTDOWN;
    buttons(0); buttons(PAD_L1);
    assert(music_at == 0 && music_notice_delay == 0);
    assert(!memcmp(order, music_order, sizeof order));
    puts("Music controls: next/previous, wraparound, held buttons, menu guards, pause volume, credit restart and web commands passed.");
    return 0;
}
