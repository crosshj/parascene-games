#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hit_feedback.h"
#include "rumble_patch.h"

extern const volatile uint8_t *hit_feedback_test_output(void);

static void quiet(void) {
    int x,y;hit_feedback_offset(&x,&y);
    assert(x==0 && y==0 && !hit_feedback_motor());
    const volatile uint8_t *out=hit_feedback_test_output();
    assert(out[0]==0 && out[1]==0 && out[2]==0);
}
int main(int argc,char **argv) {
    int x,y,pulses=0;
    hit_feedback_init();quiet();
    /* Idle/clean traversal never starts feedback without a hit event. */
    for(int i=0;i<120;i++) {hit_feedback_tick(1,1);quiet();}
    hit_feedback_hit();
    /* Resident BIOS consumes byte 0 as its gate, then transmits bytes 1/2.
     * Check the actual adapter buffer, independently of motor-state helpers. */
    const volatile uint8_t *out=hit_feedback_test_output();
    assert(out[0]==1 && out[1]==1 && out[2]==64);
    for(int i=0;i<20;i++) {
        hit_feedback_offset(&x,&y);
        assert(abs(x)<=1 && abs(y)<=1);
        pulses+=hit_feedback_motor();
        if(i>=12) quiet();
        hit_feedback_tick(1,1);
    }
    assert(pulses==4);quiet();
    hit_feedback_hit();hit_feedback_tick(0,1);quiet(); /* pause/menu */
    hit_feedback_hit();hit_feedback_reset();quiet(); /* restart */
    /* An incomplete BIOS packet cannot erase a confirmed hit's visual pulse. */
    for(int bad_tick=0;bad_tick<12;bad_tick++) {
        int reference_x[12],reference_y[12];
        hit_feedback_tick(1,1);hit_feedback_hit();
        for(int i=0;i<12;i++) {
            hit_feedback_offset(&reference_x[i],&reference_y[i]);
            hit_feedback_tick(1,1);
        }
        hit_feedback_tick(1,1);hit_feedback_hit();
        for(int i=0;i<12;i++) {
            hit_feedback_offset(&x,&y);
            assert(x==reference_x[i] && y==reference_y[i]);
            hit_feedback_tick(1,i!=bad_tick);
        }
        quiet();
    }
    hit_feedback_tick(1,1);hit_feedback_hit();
    hit_feedback_tick(1,0);assert(hit_feedback_motor()); /* transient */
    hit_feedback_tick(1,1);assert(hit_feedback_motor());
    hit_feedback_hit();
    for(int i=0;i<4;i++)hit_feedback_tick(1,0);
    assert(!hit_feedback_motor()); /* sustained disconnect stops motor */
    hit_feedback_offset(&x,&y);assert(x || y); /* shake remains independent */
    hit_feedback_tick(1,1);assert(!hit_feedback_motor()); /* no old pulse resumes */
    for(int i=0;i<20;i++)hit_feedback_tick(1,1);
    quiet();

    /* Reject incomplete/unknown kernel code; recognize both documented layouts. */
    uint32_t code[0x7c0/4]={0};
    uint32_t clamp[5]={0x0002102b,0x10400002,0,0x0015102b,0xa2220000};
    uint32_t setter[8]={0x3c010000,0xac2474d0,0xac2674d4,0x3c010000,0xac2574e0,0xac2774e4,0x03e00008,0x24020001};
    assert(!rumble_patch_layout(code,sizeof code));
    for(int second=0x4dc;second<=0x4e0;second+=4) {
        memset(code,0,sizeof code);
        memcpy(code+0x3d8/4,clamp,sizeof clamp);
        memcpy(code+second/4,clamp,sizeof clamp);
        memcpy(code+0x7a0/4,setter,sizeof setter);
        assert(rumble_patch_layout(code,sizeof code)==second);
        assert(!rumble_patch_layout(code,sizeof code-4));
        code[0x7a0/4+6]=0;assert(!rumble_patch_layout(code,sizeof code));
    }
    if(argc>1) {
        FILE *f=fopen(argv[1],"rb");assert(f);
        unsigned char *bios=malloc(512*1024);assert(bios);
        assert(fread(bios,1,512*1024,f)==512*1024);fclose(f);
        int matches=0;
        for(int at=0;at<=512*1024-0x7c0;at+=4)
            if(rumble_patch_layout((uint32_t *)(bios+at),0x7c0)) matches++;
        assert(matches==1);free(bios);
        puts("Installed BIOS: one validated pad-output layout, no ROM modifications");
    }
    puts("Hit feedback: short pulse, bounded shake, reset/disconnect and BIOS guards passed");
    return 0;
}
