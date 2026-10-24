#pragma once
/* One brief pulse per actual collision. Tick at the game's 60 Hz update rate. */
int hit_feedback_init(void);
void hit_feedback_reset(void);
void hit_feedback_hit(void);
void hit_feedback_tick(int gameplay,int connected);
void hit_feedback_offset(int *x,int *y);
int hit_feedback_motor(void);
