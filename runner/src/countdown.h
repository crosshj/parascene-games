#pragma once
/* Simulation runs at 60 Hz; rendering may run at 30 Hz. */
static inline int countdown_digit(int ticks) {
    return ticks<0 || ticks>=180 ? 0 : 3-ticks/60;
}
void countdown_draw(int digit,int age);
