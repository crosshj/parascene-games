/* Standalone hardware-path test: same initialization and feedback as the game.
 * No stage, collision changes, or memory-card writes. Cold-boot this EXE. */
#include <stdio.h>
#include <stdint.h>
#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <psxpad.h>
#include "hit_feedback.h"
#include "save.h"

static uint8_t pads[2][34];

int main(void) {
    DISPENV disp;
    DRAWENV draw;
    ResetGraph(0);
    SetDefDispEnv(&disp,0,0,320,240);
    SetDefDrawEnv(&draw,0,0,320,240);
    draw.isbg=1;
    setRGB0(&draw,12,20,28);
    PutDispEnv(&disp);
    PutDrawEnv(&draw);
    SetDispMask(1);
    FntLoad(960,0);
    int font=FntOpen(16,32,288,176,0,512);
    save_init();
    InitPAD(pads[0],34,pads[1],34);
    int ready=hit_feedback_init();
    StartPAD();
    ChangeClearPAD(0);
    printf("RUMBLE TEST begin BIOS=%d\n",ready);
    FntPrint(font,"RUMBLE OUTPUT TEST\n\nThree short pulses, then one long.\n\nUses the game's hit-feedback code.\nSee DuckStation's motor log.\n\nBIOS output setup: %s",ready?"OK":"FAILED");
    FntFlush(font);
    DrawSync(0);
    int previous=0;
    for(int tick=0;;tick++) {
        VSync(0);
        int present=((PADTYPE *)pads[0])->stat==0;
        hit_feedback_tick(1,present);
        if(tick==120 || tick==240 || tick==360 || (tick>=480 && tick<540))
            hit_feedback_hit();
        int motor=hit_feedback_motor();
        if(motor!=previous) {
            printf("RUMBLE TEST tick=%d requested=%d pad_status=%02x\n",tick,motor,pads[0][0]);
            previous=motor;
        }
        if(tick==660) {
            hit_feedback_reset();
            printf("RUMBLE TEST done\n");
        }
    }
}
