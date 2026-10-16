/* A chase camera on a straight road. The runner is one camera-facing
 * sprite. The scenery around the road comes from the chosen level. */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <psxgpu.h>
#include <psxpad.h>
#include <psxapi.h>
#include <psxcd.h>
#include <psxspu.h>
#include "world.h"
#include "course.h"
#include "path.h"
#include "level.h"
#include "profile.h"
#include "sprite_sheet.h"
#include "screen_art.h"
#include "sound_bank.h"
#include "music_tracks.h"

enum {
    ST_BOOT,
    ST_TITLE,
    ST_SELECT,
    ST_RUN,
    ST_PAUSE,
    ST_HIT,
    ST_OVER
};

/* Studio logo timing, in frames: black, fade in, hold, fade out, black. */
#define BOOT_IN 20
#define BOOT_SHOWN (BOOT_IN + 30)
#define BOOT_HOLD (BOOT_SHOWN + 120)
#define BOOT_OUT (BOOT_HOLD + 30)
#define BOOT_END (BOOT_OUT + 20)
#define FADE 30

#define MUSIC_FULL 128
#define MUSIC_PAUSED 40

static uint8_t pad_buff[2][34];
static uint16_t gpu_tpage[SPR_PAGE_COUNT];
static uint16_t gpu_clut[SPR_CHAR_COUNT];

static int state = ST_BOOT;
static int tick;
static int leaving;
static int pick;
static int scroll;
static int lane;
static int character;
static int level_index;
static int anim_tick;
static int jump_wind;
static int land_hold;
static int player_x, player_y, player_vy;
static int distance, best, shown;
static int hit_timer;
static uint16_t held, prev;

static int cd_on, cd_started;
static int music_level = MUSIC_FULL;
static uint16_t art_tpage[ART_COUNT];
static uint16_t art_clutid[ART_COUNT];
static uint32_t snd_addr[SND_COUNT];
static int snd_voice;

/* Shuffled playlist over every track in audio/ (see music_tracks.h). */
static int music_order[MUSIC_TRACK_COUNT];
static int music_at;
static int music_cur = -1;

#ifdef RUNNER_XA_MUSIC
static CdlLOC xa_loc[MUSIC_TRACK_COUNT];
static volatile int xa_need_next;

typedef struct {
    uint8_t file, channel;
    uint8_t submode, coding_info;
} XA_Header;

typedef struct {
    CdlLOC pos;
    XA_Header xa_header[2];
    uint8_t data[2048];
    uint32_t edc;
    uint8_t ecc[276];
} XA_Sector;

static XA_Sector xa_sector;

enum {
    XA_TYPE_AUDIO = 1 << 2,
    XA_END_OF_FILE = 1 << 7
};
#else
static int cd_wait;
#endif

static uint16_t pad_held(void) {
    PADTYPE *pad = (PADTYPE *)pad_buff[0];
    uint16_t b = 0;
    uint16_t w;
    if (pad->stat)
        return 0;
    if (pad->type != PAD_ID_DIGITAL && pad->type != PAD_ID_ANALOG &&
        pad->type != PAD_ID_ANALOG_STICK)
        return 0;
    w = pad->btn;
    if (!(w & PAD_LEFT)) b |= PAD_LEFT;
    if (!(w & PAD_RIGHT)) b |= PAD_RIGHT;
    if (!(w & PAD_UP)) b |= PAD_UP;
    if (!(w & PAD_DOWN)) b |= PAD_DOWN;
    if (!(w & PAD_CROSS)) b |= PAD_CROSS;
    if (!(w & PAD_CIRCLE)) b |= PAD_CIRCLE;
    if (!(w & PAD_TRIANGLE)) b |= PAD_TRIANGLE;
    if (!(w & PAD_START)) b |= PAD_START;
    if (pad->type != PAD_ID_DIGITAL) {
        if (pad->ls_x < 64) b |= PAD_LEFT;
        if (pad->ls_x > 192) b |= PAD_RIGHT;
        if (pad->ls_y < 64) b |= PAD_UP;
        if (pad->ls_y > 192) b |= PAD_DOWN;
    }
    return b;
}

static int edge(uint16_t bit) {
    return (held & bit) && !(prev & bit);
}

#define SHADOW_W 64
#define SHADOW_H 32

static uint16_t shadow_tex[SHADOW_W * SHADOW_H];
static uint16_t shadow_tpage;

static void upload_shadow(void) {
    int x, y;
    RECT rect;
    int lim = SHADOW_W * SHADOW_W * SHADOW_H * SHADOW_H;
    for (y = 0; y < SHADOW_H; y++) {
        for (x = 0; x < SHADOW_W; x++) {
            int dx = x * 2 - (SHADOW_W - 1);
            int dy = y * 2 - (SHADOW_H - 1);
            int d = dx * dx * SHADOW_H * SHADOW_H + dy * dy * SHADOW_W * SHADOW_W;
            int v = 0;
            if (d < lim) {
                int t = (lim - d) * 255 / lim;
                t = t * t / 255;
                v = t * 4 / 255;
            }
            shadow_tex[y * SHADOW_W + x] =
                (uint16_t)(v | (v << 5) | (v << 10) | 0x8000);
        }
    }
    setRECT(&rect, 768, 256, SHADOW_W, SHADOW_H);
    LoadImage(&rect, (const uint32_t *)shadow_tex);
    DrawSync(0);
    shadow_tpage = getTPage(2, 2, 768, 256);
}

static void draw_shadow(void) {
    int w = 44 - player_y / 5;
    int depth = 34 - player_y / 8;
    int z0, z1, tone, bucket;
    XY far_l, far_r, near_l, near_r;
    POLY_FT4 *spr;
    if (w < 14)
        w = 14;
    if (depth < 12)
        depth = 12;
    z0 = CHAR_Z - depth / 2;
    z1 = z0 + depth;
    path_project(scroll + z1, player_x - w, 2, &far_l);
    path_project(scroll + z1, player_x + w, 2, &far_r);
    path_project(scroll + z0, player_x - w, 2, &near_l);
    path_project(scroll + z0, player_x + w, 2, &near_r);
    if (!xy_ok(&far_l) || !xy_ok(&far_r) || !xy_ok(&near_l) || !xy_ok(&near_r))
        return;
    if (!edge_ok(far_l, far_r) || !edge_ok(near_l, near_r) ||
        !edge_ok(far_l, near_l) || !edge_ok(far_r, near_r))
        return;
    tone = 128 - player_y;
    if (tone < 40)
        tone = 40;
    /* One slot in front of the road under the feet, so the near half is not covered. */
    bucket = (CHAR_Z >> OT_SHIFT) - 1;
    if (bucket < 1)
        bucket = 1;
    spr = alloc_prim(bucket, sizeof(POLY_FT4));
    if (!spr)
        return;
    setPolyFT4(spr);
    setSemiTrans(spr, 1);
    setRGB0(spr, tone, tone, tone);
    setXY4(spr,
        far_l.x, far_l.y, far_r.x, far_r.y,
        near_l.x, near_l.y, near_r.x, near_r.y);
    setUVWH(spr, 0, 0, SHADOW_W - 1, SHADOW_H - 1);
    spr->tpage = shadow_tpage;
    spr->clut = 0;
}

static int anim_count(int anim) {
    return spr_base_anim_count[spr_char_base[character]][anim];
}

static int play_frame(int anim, int index) {
    int base = spr_char_base[character];
    int n = spr_base_anim_count[base][anim];
    if (n < 1)
        return 0;
    if (index < 0)
        index = 0;
    if (index >= n)
        index = n - 1;
    return spr_anim_frames[spr_base_list_off[base] + spr_base_anim_begin[base][anim] + index];
}

static int posed_frame(void) {
    int dist, n, phase;
    if (jump_wind > 4)
        return play_frame(SPR_ANIM_JUMP, 0);
    if (jump_wind > 0)
        return play_frame(SPR_ANIM_JUMP, 1);
    if (player_y > 0) {
        if (player_vy > 11)
            phase = 2;
        else if (player_vy > 6)
            phase = 3;
        else if (player_vy > 1)
            phase = 4;
        else if (player_vy > -4)
            phase = 5;
        else if (player_y > 24)
            phase = 6;
        else
            phase = 7;
        return play_frame(SPR_ANIM_JUMP, phase);
    }
    if (land_hold > 0)
        return play_frame(SPR_ANIM_JUMP, anim_count(SPR_ANIM_JUMP) - 1);
    dist = lane * LANE - player_x;
    if (dist > 8) {
        n = anim_count(SPR_ANIM_RIGHT);
        return play_frame(SPR_ANIM_RIGHT, (LANE - dist) * n / LANE);
    }
    if (dist < -8) {
        n = anim_count(SPR_ANIM_LEFT);
        return play_frame(SPR_ANIM_LEFT, (LANE + dist) * n / LANE);
    }
    return play_frame(SPR_ANIM_RUN, (anim_tick / 4) % anim_count(SPR_ANIM_RUN));
}

static void draw_runner(void) {
    XY feet;
    int sz, h, w, frame, bucket;
    POLY_FT4 *spr;
    sz = path_project(scroll + CHAR_Z, player_x, player_y, &feet);
    if (sz < 8)
        return;
    h = (78 * (SCREEN_W / 2)) / sz;
    if (h < 8)
        h = 8;
    w = h * spr_base_fw[spr_char_base[character]] / spr_base_fh[spr_char_base[character]];
    frame = posed_frame();
    bucket = (CHAR_Z >> OT_SHIFT) - 1;
    if (bucket < 1)
        bucket = 1;
    spr = alloc_prim(bucket, sizeof(POLY_FT4));
    if (!spr)
        return;
    setPolyFT4(spr);
    setRGB0(spr, 128, 128, 128);
    setXY4(spr,
        feet.x - w / 2, feet.y - h,
        feet.x + w / 2, feet.y - h,
        feet.x - w / 2, feet.y,
        feet.x + w / 2, feet.y);
    setUVWH(spr,
        spr_frame_u[spr_base_uv_off[spr_char_base[character]] + frame],
        spr_char_v[character] + spr_frame_v[spr_base_uv_off[spr_char_base[character]] + frame],
        spr_base_fw[spr_char_base[character]] - 1,
        spr_base_fh[spr_char_base[character]] - 1);
    spr->tpage = gpu_tpage[spr_char_page[character]];
    spr->clut = gpu_clut[character];
}

static void hud_center(int y, const char *s) {
    int n = 0;
    while (s[n])
        n++;
    text((SCREEN_W - n * 8) / 2, y, s);
}

static void panel(int bucket, int x, int y, int w, int h) {
    TILE *t = alloc_prim(bucket, sizeof(TILE));
    if (t) {
        setTile(t);
        setXY0(t, x, y);
        setWH(t, w, h);
        setRGB0(t, 8, 12, 28);
    }
    t = alloc_prim(bucket, sizeof(TILE));
    if (t) {
        setTile(t);
        setXY0(t, x - 2, y - 2);
        setWH(t, w + 4, h + 4);
        setRGB0(t, 120, 140, 200);
    }
}

static void draw_art(int id, int x, int y, int bright) {
    POLY_FT4 *p;
    int w = art_w[id], h = art_h[id];
    if (bright <= 0)
        return;
    if (bright > 128)
        bright = 128;
    p = alloc_prim(2, sizeof(POLY_FT4));
    if (!p)
        return;
    setPolyFT4(p);
    setRGB0(p, bright, bright, bright);
    setXYWH(p, x, y, w, h);
    setUVWH(p, 0, 0, w - 1, h - 1);
    p->tpage = art_tpage[id];
    p->clut = art_clutid[id];
}

static void draw_preview(int cx, int feet_y, int scale) {
    POLY_FT4 *p;
    int base = spr_char_base[character];
    int fw = spr_base_fw[base], fh = spr_base_fh[base];
    int w = fw * scale, h = fh * scale;
    int frame = play_frame(SPR_ANIM_RUN, (anim_tick / 4) % anim_count(SPR_ANIM_RUN));
    int uv = spr_base_uv_off[base] + frame;
    p = alloc_prim(1, sizeof(POLY_FT4));
    if (!p)
        return;
    setPolyFT4(p);
    setRGB0(p, 128, 128, 128);
    setXYWH(p, cx - w / 2, feet_y - h, w, h);
    setUVWH(p, spr_frame_u[uv], spr_char_v[character] + spr_frame_v[uv], fw - 1, fh - 1);
    p->tpage = gpu_tpage[spr_char_page[character]];
    p->clut = gpu_clut[character];
}

static int fade_in(int t) {
    return t >= FADE ? 128 : t * 128 / FADE;
}

static void draw_boot(void) {
    int b = 0;
    int id = ART_PARASCENE;
    if (tick >= BOOT_IN && tick < BOOT_SHOWN)
        b = (tick - BOOT_IN) * 128 / (BOOT_SHOWN - BOOT_IN);
    else if (tick >= BOOT_SHOWN && tick < BOOT_HOLD)
        b = 128;
    else if (tick >= BOOT_HOLD && tick < BOOT_OUT)
        b = (BOOT_OUT - tick) * 128 / (BOOT_OUT - BOOT_HOLD);
    draw_art(id, (SCREEN_W - art_w[id]) / 2, (SCREEN_H - art_h[id]) / 2, b);
}

static void draw_title(void) {
    int id = ART_TITLE;
    int b = fade_in(tick);
    if (leaving > 0)
        b = leaving * 128 / FADE;
    draw_art(id, (SCREEN_W - art_w[id]) / 2, 52, b);
    if (tick < FADE || leaving > 0)
        return;
    if (((tick - FADE) / 30) % 2 == 0)
        hud_center(176, "PRESS START");
    hud_center(220, "(C) 2026 PARASCENE GAMES");
}

/* The select screen's rows, chosen with up and down. */
enum { ROW_RUNNER, ROW_LEVEL, ROW_COUNT };

static void select_row(int y, int row, int count, const char *label, const char *name) {
    char line[40];
    snprintf(line, sizeof line, pick == row && count > 1 ? "%s < %s >" : "%s   %s  ",
             label, name);
    hud_center(y, line);
}

static void draw_select(void) {
    panel(3, 60, 20, 200, 200);
    hud_center(32, "CHOOSE YOUR RUNNER");
    draw_preview(SCREEN_W / 2, 164, 3);
    select_row(172, ROW_RUNNER, SPR_CHAR_COUNT, "RUNNER", spr_char_name[character]);
    select_row(186, ROW_LEVEL, level_count, "WORLD ", levels[level_index]->name);
    hud_center(204, "X SELECT   TRIANGLE BACK");
}

static const char *const k_pause_items[] = { "CONTINUE", "QUIT TO TITLE" };
static const char *const k_over_items[] = { "RUN AGAIN", "CHANGE RUNNER", "QUIT TO TITLE" };

static void draw_menu(int y, const char *const *items, int count) {
    char line[40];
    int i;
    for (i = 0; i < count; i++) {
        snprintf(line, sizeof line, i == pick ? "> %s <" : "  %s  ", items[i]);
        hud_center(y + i * 16, line);
    }
}

static void draw_ui(void) {
    char line[80];
    TILE *bar;
    if (state == ST_RUN || state == ST_HIT || state == ST_PAUSE) {
        bar = alloc_prim(1, sizeof(TILE));
        if (bar) {
            setTile(bar);
            setXY0(bar, 0, 0);
            setWH(bar, SCREEN_W, 22);
            setRGB0(bar, 8, 12, 24);
        }
        snprintf(line, sizeof line, "RUN %d     X JUMP   START PAUSE", distance);
        text(8, 8, line);
    }
    if (state == ST_PAUSE) {
        panel(3, 80, 80, 160, 84);
        hud_center(94, "PAUSED");
        draw_menu(118, k_pause_items, 2);
    } else if (state == ST_OVER) {
        panel(3, 52, 56, 216, 132);
        hud_center(70, "WIPED OUT");
        snprintf(line, sizeof line, "DISTANCE %d   BEST %d", shown, best);
        hud_center(88, line);
        draw_menu(116, k_over_items, 3);
    }
}

static void collide(void) {
    int pz = scroll + CHAR_Z;
    int b0 = (pz - STEP) / STEP;
    int b;
    if (b0 < 0)
        b0 = 0;
    for (b = b0; b <= b0 + 2; b++) {
        int oz, x;
        if (!has_obstacle(b))
            continue;
        oz = b * STEP + STEP / 2;
        if (iabs(oz - pz) > OBST_D / 2)
            continue;
        x = obstacle_lane(b) * LANE;
        if (iabs(player_x - x) < 80 && player_y < OBST_H - 4) {
            state = ST_HIT;
            tick = 0;
            hit_timer = 50;
            shown = distance;
            if (distance > best)
                best = distance;
            return;
        }
    }
}

static void play_sound(int id);
static void music_start(void);
static void music_volume(int level);

static void go(int next) {
    state = next;
    tick = 0;
    pick = 0;
    leaving = 0;
}

static void start_run(void) {
    go(ST_RUN);
    scroll = 0;
    lane = 0;
    player_x = 0;
    player_y = 0;
    player_vy = 0;
    jump_wind = 0;
    land_hold = 0;
    distance = 0;
}

static int confirm(void) {
    return edge(PAD_CROSS) || edge(PAD_START);
}

static int back(void) {
    return edge(PAD_TRIANGLE) || edge(PAD_CIRCLE);
}

static void menu_move(int count) {
    if (edge(PAD_UP) && !edge(PAD_DOWN)) {
        pick = (pick + count - 1) % count;
        play_sound(SND_MOVE);
    }
    if (edge(PAD_DOWN) && !edge(PAD_UP)) {
        pick = (pick + 1) % count;
        play_sound(SND_MOVE);
    }
}

/* Left and right step through count choices. */
static int cycle(int value, int count) {
    if (count < 2)
        return value;
    if (edge(PAD_LEFT) && !edge(PAD_RIGHT)) {
        value = (value + count - 1) % count;
        play_sound(SND_MOVE);
    }
    if (edge(PAD_RIGHT) && !edge(PAD_LEFT)) {
        value = (value + 1) % count;
        play_sound(SND_MOVE);
    }
    return value;
}

/* Returns 0 when the screen is not moving the runner on the road. */
static int update_screens(void) {
    switch (state) {
    case ST_BOOT:
        if (tick < BOOT_HOLD && tick > BOOT_SHOWN && confirm())
            tick = BOOT_HOLD;
        if (tick >= BOOT_END) {
            go(ST_TITLE);
            music_start();
        }
        return 0;
    case ST_TITLE:
        if (leaving > 0) {
            if (--leaving == 0)
                go(ST_SELECT);
        } else if (tick >= FADE / 2 && confirm()) {
            play_sound(SND_START);
            leaving = FADE;
        }
        scroll += 8;
        return 0;
    case ST_SELECT:
        menu_move(ROW_COUNT);
        if (pick == ROW_RUNNER)
            character = cycle(character, SPR_CHAR_COUNT);
        else
            level_index = cycle(level_index, level_count);
        if (confirm()) {
            play_sound(SND_SELECT);
            start_run();
        } else if (back()) {
            play_sound(SND_BACK);
            go(ST_TITLE);
        }
        scroll += 8;
        anim_tick++;
        return 0;
    case ST_PAUSE:
        menu_move(2);
        if (edge(PAD_START) || back() || (edge(PAD_CROSS) && pick == 0)) {
            play_sound(edge(PAD_CROSS) ? SND_SELECT : edge(PAD_START) ? SND_PAUSE : SND_BACK);
            music_volume(MUSIC_FULL);
            state = ST_RUN;
        } else if (edge(PAD_CROSS)) {
            play_sound(SND_SELECT);
            music_volume(MUSIC_FULL);
            go(ST_TITLE);
        }
        return 0;
    case ST_OVER:
        menu_move(3);
        if (confirm()) {
            play_sound(SND_SELECT);
            if (pick == 0)
                start_run();
            else
                go(pick == 1 ? ST_SELECT : ST_TITLE);
        }
        return 0;
    default:
        return 1;
    }
}

static void update(void) {
    int speed = 0;
    int target;
    int dx;
    held = pad_held();
    tick++;
    if (!update_screens()) {
        prev = held;
        return;
    }
    if (state == ST_RUN) {
        speed = 20;
        if (edge(PAD_START)) {
            play_sound(SND_PAUSE);
            music_volume(MUSIC_PAUSED);
            state = ST_PAUSE;
            pick = 0;
            prev = held;
            return;
        }
        if (edge(PAD_LEFT) && !edge(PAD_RIGHT) && lane > -1)
            lane--;
        if (edge(PAD_RIGHT) && !edge(PAD_LEFT) && lane < 1)
            lane++;
        if (edge(PAD_CROSS) && player_y == 0 && jump_wind == 0) {
            jump_wind = 8;
            land_hold = 0;
        } else if (jump_wind > 0) {
            jump_wind--;
            if (jump_wind == 0)
                player_vy = 15;
        }
        distance = scroll / 48;
#ifndef RUNNER_PROFILE
        collide();
#endif
    } else if (state == ST_HIT) {
        if (hit_timer > 0)
            hit_timer--;
        else {
            go(ST_OVER);
            jump_wind = 0;
            land_hold = 0;
        }
    }
    target = lane * LANE;
    dx = target - player_x;
    if (dx > 12) player_x += 12;
    else if (dx < -12) player_x -= 12;
    else player_x = target;
    if (state == ST_RUN && player_y == 0 && land_hold > 0)
        land_hold--;
    {
        int was_up = player_y > 0;
        player_y += player_vy;
        if (player_vy > -20)
            player_vy--;
        if (player_y <= 0) {
            if (was_up && state == ST_RUN)
                land_hold = 8;
            player_y = 0;
            player_vy = 0;
        }
    }
    anim_tick++;
    scroll += speed;
    prev = held;
}

static void upload_sprite(void) {
    int i;
    RECT rect;
    for (i = 0; i < SPR_PAGE_COUNT; i++) {
        setRECT(&rect, spr_page_x[i], spr_page_y[i], SPR_PAGE_W / 4, spr_page_h[i]);
        LoadImage(&rect, (const uint32_t *)(spr_tex + spr_page_off[i]));
        DrawSync(0);
        gpu_tpage[i] = getTPage(0, 0, spr_page_x[i], spr_page_y[i]);
    }
    for (i = 0; i < SPR_CHAR_COUNT; i++) {
        setRECT(&rect, spr_clut_x[i], spr_clut_y[i], 16, 1);
        LoadImage(&rect, (const uint32_t *)spr_clut[i]);
        DrawSync(0);
        gpu_clut[i] = getClut(spr_clut_x[i], spr_clut_y[i]);
    }
}

static void upload_art(void) {
    int i;
    RECT rect;
    for (i = 0; i < ART_COUNT; i++) {
        setRECT(&rect, art_vram_x[i], art_vram_y[i], art_stride[i] / 2, art_h[i]);
        LoadImage(&rect, (const uint32_t *)(art_pix + art_off[i]));
        DrawSync(0);
        art_tpage[i] = getTPage(1, 0, art_vram_x[i], art_vram_y[i]);
        setRECT(&rect, art_clut_x[i], art_clut_y[i], 256, 1);
        LoadImage(&rect, (const uint32_t *)art_clut[i]);
        DrawSync(0);
        art_clutid[i] = getClut(art_clut_x[i], art_clut_y[i]);
    }
}

/* The first 4 KB of SPU RAM hold capture buffers, and psxspu puts a dummy
 * sample at 0x1000, so the sounds start after it. */
#define SND_SPU_BASE 0x1010
#define SND_VOICE_FIRST 0
#define SND_VOICES 4

static void upload_sounds(void) {
    int i;
    uint32_t addr = SND_SPU_BASE;
    SpuSetTransferMode(SPU_TRANSFER_BY_DMA);
    for (i = 0; i < SND_COUNT; i++) {
        snd_addr[i] = addr;
        SpuSetTransferStartAddr(addr);
        SpuWrite((const uint32_t *)(snd_data + snd_off[i]), snd_len[i]);
        SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
        addr += snd_len[i];
    }
}

static void play_sound(int id) {
    int ch = SND_VOICE_FIRST + snd_voice;
    snd_voice = (snd_voice + 1) % SND_VOICES;
    SpuSetKey(0, 1 << ch);
    SPU_CH_FREQ(ch) = getSPUSampleRate(SND_RATE);
    SPU_CH_ADDR(ch) = getSPUAddr(snd_addr[id]);
    SPU_CH_VOL_L(ch) = 0x2c00;
    SPU_CH_VOL_R(ch) = 0x2c00;
    SPU_CH_ADSR1(ch) = 0x00ff;
    SPU_CH_ADSR2(ch) = 0x0000;
    SpuSetKey(1, 1 << ch);
}

static void setup_video(void) {
    world_init();
    upload_sprite();
    upload_shadow();
    upload_art();
    SetDispMask(1);
}

static void music_shuffle(int avoid) {
    int i, j, t;

    for (i = 0; i < MUSIC_TRACK_COUNT; i++)
        music_order[i] = i;
    for (i = MUSIC_TRACK_COUNT - 1; i > 0; i--) {
        j = rand() % (i + 1);
        t = music_order[i];
        music_order[i] = music_order[j];
        music_order[j] = t;
    }
    if (MUSIC_TRACK_COUNT > 1 && avoid >= 0 && music_order[0] == avoid) {
        j = 1 + rand() % (MUSIC_TRACK_COUNT - 1);
        t = music_order[0];
        music_order[0] = music_order[j];
        music_order[j] = t;
    }
    music_at = 0;
}

#ifdef RUNNER_XA_MUSIC
static int music_play_track(int track) {
    uint8_t mode = CdlModeRT | CdlModeSF;
    CdlFILTER filter;
    CdlATV mix;

    if (track < 0 || track >= MUSIC_TRACK_COUNT)
        return 0;
    music_cur = track;
    filter.file = 1;
    filter.chan = 0;
    if (!CdControlB(CdlSetmode, &mode, 0))
        return 0;
    if (!CdControlB(CdlSetfilter, (uint8_t *)&filter, 0))
        return 0;
    if (!CdControlB(CdlReadS, &xa_loc[track], 0))
        return 0;
    CdControl(CdlDemute, 0, 0);
    mix.val0 = (uint8_t)music_level;
    mix.val1 = 0;
    mix.val2 = (uint8_t)music_level;
    mix.val3 = 0;
    CdMix(&mix);
    SpuSetCommonMasterVolume(0x3fff, 0x3fff);
    SpuSetCommonCDVolume(0x3fff, 0x3fff);
    return 1;
}

static void music_advance(void) {
    music_at++;
    if (music_at >= MUSIC_TRACK_COUNT)
        music_shuffle(music_cur);
    music_play_track(music_order[music_at]);
}

static void xa_ready(CdlIntrResult event, uint8_t *payload) {
    uint8_t sm0, sm1;
    (void)payload;
    if (event != CdlDataReady)
        return;
    CdGetSector(&xa_sector, (int)(sizeof(XA_Sector) / 4));
    sm0 = xa_sector.xa_header[0].submode;
    sm1 = xa_sector.xa_header[1].submode;
    if ((sm0 & XA_END_OF_FILE) || (sm1 & XA_END_OF_FILE) ||
        (!(sm0 & XA_TYPE_AUDIO) && !(sm1 & XA_TYPE_AUDIO))) {
        CdControlF(CdlPause, 0);
        xa_need_next = 1;
    }
}

static void cd_service(void) {
    if (!xa_need_next)
        return;
    xa_need_next = 0;
    music_advance();
}

static int music_setup(void) {
    CdlFILE file;
    char path[16];
    int i;

    for (i = 0; i < MUSIC_TRACK_COUNT; i++) {
        sprintf(path, "\\MUSIC%02d.XA", i);
        if (!CdSearchFile(&file, path))
            return 0;
        xa_loc[i] = file.pos;
    }
    srand((unsigned)VSync(-1));
    EnterCriticalSection();
    CdReadyCallback(&xa_ready);
    ExitCriticalSection();
    return 1;
}
#else
/* CD track 1 is data; audio tracks start at 2. */
static int music_play_track(int track) {
    uint8_t mode = CdlModeDA;
    uint8_t cd_track = itob(track + 2);
    uint8_t result[8];
    CdlLOC loc;
    CdlATV mix;

    if (track < 0 || track >= MUSIC_TRACK_COUNT)
        return 0;
    music_cur = track;
    if (!CdControlB(CdlSetmode, &mode, 0))
        return 0;
    if (!CdCommand(CdlGetTD, &cd_track, 1, result))
        return 0;
    if (CdSync(1, 0) != CdlComplete)
        return 0;
    loc.minute = result[1];
    loc.second = result[2];
    loc.sector = 0;
    loc.track = cd_track;
    if (!CdControlB(CdlSetloc, &loc, 0))
        return 0;
    if (!CdControlB(CdlPlay, 0, 0))
        return 0;
    CdControl(CdlDemute, 0, 0);
    mix.val0 = (uint8_t)music_level;
    mix.val1 = 0;
    mix.val2 = (uint8_t)music_level;
    mix.val3 = 0;
    CdMix(&mix);
    SpuSetCommonMasterVolume(0x3fff, 0x3fff);
    SpuSetCommonCDVolume(0x3fff, 0x3fff);
    return 1;
}

static void music_advance(void) {
    music_at++;
    if (music_at >= MUSIC_TRACK_COUNT)
        music_shuffle(music_cur);
    if (music_play_track(music_order[music_at]))
        cd_wait = music_cd_frames[music_cur];
    else
        cd_wait = 0;
}

static void cd_service(void) {
    if (!cd_on || cd_wait == 0)
        return;
    cd_wait--;
    if (cd_wait > 0)
        return;
    music_advance();
}

static int music_setup(void) {
    srand((unsigned)VSync(-1));
    return 1;
}
#endif

static void music_start(void) {
    if (!cd_on || cd_started)
        return;
    cd_started = 1;
    music_shuffle(-1);
#ifdef RUNNER_XA_MUSIC
    music_play_track(music_order[music_at]);
#else
    if (music_play_track(music_order[music_at]))
        cd_wait = music_cd_frames[music_cur];
    else
        cd_wait = 0;
#endif
}

/* 128 is full volume. The stream keeps playing; only the mix changes. */
static void music_volume(int level) {
    CdlATV mix;
    music_level = level;
    mix.val0 = (uint8_t)level;
    mix.val1 = 0;
    mix.val2 = (uint8_t)level;
    mix.val3 = 0;
    CdMix(&mix);
}

int main(int argc, const char **argv) {
    int last_scroll = 0;
    (void)argc;
    (void)argv;
    setup_video();
    SpuInit();
    upload_sounds();
    InitPAD(pad_buff[0], 34, pad_buff[1], 34);
    StartPAD();
    ChangeClearPAD(0);
    cd_on = CdInit();
    if (cd_on)
        cd_on = music_setup();
#ifdef RUNNER_PROFILE
    prof_start();
    start_run();
    printf("PROF start\n");
#endif

    for (;;) {
        int sr = 0, sg = 0, sb = 0;
        LevelFrame frame;
        PROF(PF_UPDATE, update());
        cd_service();
        frame.scroll = scroll;
        frame.speed = scroll > last_scroll ? scroll - last_scroll : 0;
        frame.time = anim_tick;
        last_scroll = scroll;
        if (state == ST_BOOT) {
            draw_boot();
        } else if (state == ST_TITLE) {
            draw_title();
        } else {
            const Level *lv = levels[level_index];
            if (!path_open(lv->track))
                path_open("straight.run");
            path_begin(frame.scroll);
            lv->clear_color(&frame, &sr, &sg, &sb);
            PROF_LONG(PF_LEVEL, lv->draw(&frame));
            if (state == ST_SELECT) {
                draw_select();
            } else {
                PROF(PF_SPRITES, (draw_runner(), draw_shadow(), draw_ui()));
            }
        }
        world_set_clear(sr, sg, sb);
#ifdef RUNNER_PROFILE
        if (prof_frame(levels[level_index]->name, world_bytes_used())) {
            if (++level_index == level_count) {
                printf("PROF done\n");
                level_index = 0;
            }
            start_run();
        }
#endif
        world_flip();
    }
    return 0;
}
