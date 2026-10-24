/* An isolated ROM exercising the production web save code. */
#include <stdio.h>
#include <psxgpu.h>
#include <psxpad.h>
#include "../../src/save.c"

int main(void) {
    DRAWENV draw;
    DISPENV disp;
    static uint8_t pads[2][34];
    static uint16_t pixels[320] __attribute__((aligned(4)));
    int ready, reach=0, requested=0, frames=0, last_state=-1;
    ResetGraph(0);
    SetDefDrawEnv(&draw,0,0,320,240);
    SetDefDispEnv(&disp,0,0,320,240);
    draw.isbg=1;
    setRGB0(&draw,8,16,24);
    PutDrawEnv(&draw);PutDispEnv(&disp);SetDispMask(1);
    FntLoad(960,0);FntOpen(8,8,304,224,0,256);
    save_init();
    InitPAD(pads[0],34,pads[1],34);StartPAD();ChangeClearPAD(0);
    printf("CARD_PROBE_BOOT\n");
    for(;;) {
        ready=save_poll(&reach);
        if(state!=last_state) {
            printf("CARD_PROBE_STATE %d ready=%d reach=%d\n",state,ready,reach);
            last_state=state;
        }
        if(ready && !requested) {
            printf("CARD_PROBE_LOAD %d\n",reach);
            requested=1;
            if(reach<7) save_store(7);
        }
        if(save_generation() && requested==1) {
            printf("CARD_PROBE_WRITE %d generation=%d\n",reach,save_generation());
            requested=2;
        }
        if(++frames%120==0)
            printf("CARD_PROBE_TICK %d ready=%d reach=%d generation=%d\n",frames,ready,reach,save_generation());
        FntPrint(-1,"CARD PROBE\nSTATE %d READY %d\nREACH %d GENERATION %d\n",state,ready,reach,save_generation());
        {
            uint8_t message[40]={'C','A','R','D',ready,reach,save_generation(),requested};
            RECT row;
            int i;
            message[8]=(uint8_t)frames;message[9]=(uint8_t)(frames>>8);
            message[10]=((PADTYPE *)pads[0])->stat;
            message[11]=((PADTYPE *)pads[0])->type;
            message[12]=(uint8_t)((PADTYPE *)pads[0])->btn;
            message[13]=(uint8_t)(((PADTYPE *)pads[0])->btn>>8);
            for(i=0;i<320;i++) pixels[i]=(message[i>>3]&(0x80>>(i&7)))?0x7fff:0;
            DrawSync(0);VSync(0);FntFlush(-1);DrawSync(0);
            setRECT(&row,0,239,320,1);LoadImage(&row,(const uint32_t *)pixels);DrawSync(0);
        }
    }
}
