/* Story world carousel, and the card that leads into a stage.
 *
 * reach counts unlocked stages in world order. One past the last stage
 * means that stage has been cleared. glow counts down from FLOW_GLOW
 * while global stage glow_at opens. */
#pragma once

#define FLOW_GLOW 48

/* Card clock, in the same ticks as the rest of the game (two a picture).
 * Names slide in, sit on black until CARD_HOLD, then the road fades in
 * and each countdown digit holds for CARD_BEAT. */
#define CARD_SLIDE 48
#define CARD_HOLD 210
#define CARD_FADE 60
#define CARD_BEAT 80
#define CARD_ROAD (CARD_HOLD + CARD_FADE)
#define CARD_GO (CARD_ROAD + CARD_BEAT * 3)

enum { FLOW_WORLDS, FLOW_STAGES };

int flow_world_count(void);
int flow_base(int slot);
int flow_total(void);
int flow_stages(int slot);
/* World slot that contains this global stage index. */
int flow_slot_for(int global);

void flow_draw(int slot, int pos_q8, int focus, int stage, int reach,
               int glow, int glow_at, int time);

/* 1 while the card is still black and the road should stay hidden. */
int card_black(int tick);
/* 1 when the countdown has finished and the run should start. */
int card_finished(int tick);
void card_overlay(const char *world, const char *stage, int tick);
