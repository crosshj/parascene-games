#include "hit_feedback.h"
#include "rumble_patch.h"
#ifndef HIT_FEEDBACK_TEST
#include <psxapi.h>
#include <hwregs_c.h>
#endif

#define HIT_TICKS 12
#define MOTOR_TICKS 4
#define LARGE_MOTOR_STRENGTH 64
static int remaining,connected,missing_ticks,motor_cancelled;
static int dual_motor;
/* BIOS byte 0 is an output-enable flag, not a transmitted byte.
 * Bytes 1/2: small motor switch and large motor strength after configuration.
 * Older pads retain the legacy small-motor protocol. */
static volatile uint8_t output[2][8];

#ifndef HIT_FEEDBACK_TEST
static void pad_delay(void) {
    for(int i=0;i<2000;i++) __asm__ volatile("");
}

/* Bounded startup-only transactions, before StartPAD. Ordinary polling and
 * all runtime motor output remain with the BIOS. No custom interrupt handler. */
static int pad_command(const uint8_t *tx,int length,uint8_t *rx) {
    SIO_CTRL(0)=0x0010;
    pad_delay();
    SIO_CTRL(0)=0x0003;
    pad_delay();
    for(int i=0;i<length;i++) {
        int wait=10000;
        while(!(SIO_STAT(0)&1) && --wait) {}
        if(!wait) goto failed;
        SIO_DATA(0)=tx[i];
        wait=10000;
        while(!(SIO_STAT(0)&2) && --wait) {}
        if(!wait) goto failed;
        rx[i]=SIO_DATA(0);
        pad_delay();
    }
    SIO_CTRL(0)=0;
    return rx[2]==0x5a;
failed:
    SIO_CTRL(0)=0;
    return 0;
}

static int configure_motors(void) {
    static const uint8_t enter[9]={1,0x43,0,1,0,0,0,0,0};
    static const uint8_t map[9]={1,0x4d,0,0,1,0xff,0xff,0xff,0xff};
    static const uint8_t leave[9]={1,0x43,0,0,0,0,0,0,0};
    uint8_t rx[9];
    int critical=EnterCriticalSection();
    uint16_t ctrl=SIO_CTRL(0),mode=SIO_MODE(0),baud=SIO_BAUD(0);
    SIO_CTRL(0)=0x0040;
    SIO_MODE(0)=0x000d;
    SIO_BAUD(0)=0x0088;
    int ok=pad_command(enter,9,rx);
    if(ok) ok=pad_command(map,9,rx) && rx[1]==0xf3;
    /* Always leave configuration mode, even if mapping failed. */
    int left=pad_command(leave,9,rx);
    SIO_CTRL(0)=0x0040;
    SIO_MODE(0)=mode;
    SIO_BAUD(0)=baud;
    SIO_CTRL(0)=ctrl;
    if(critical) ExitCriticalSection();
    return ok && left;
}
#endif

static void motor_update(void) {
    int on=connected && !motor_cancelled && remaining>HIT_TICKS-MOTOR_TICKS;
    if(on==hit_feedback_motor()) return;
    /* Disable first, enable last, so an interrupt cannot see a spurious on. */
    output[0][0]=0;
    output[0][1]=on?(dual_motor?1:0x40):0;
    output[0][2]=on?(dual_motor?LARGE_MOTOR_STRENGTH:1):0;
    output[0][0]=on?1:0;
}
int hit_feedback_motor(void) { return output[0][0]!=0; }
#ifdef HIT_FEEDBACK_TEST
const volatile uint8_t *hit_feedback_test_output(void) { return output[0]; }
#endif
void hit_feedback_reset(void) { remaining=0;motor_cancelled=1;motor_update(); }
void hit_feedback_hit(void) { remaining=HIT_TICKS;motor_cancelled=!connected;motor_update(); }
void hit_feedback_tick(int gameplay,int present) {
    /* BIOS polling can briefly report an incomplete packet. Never tie the
     * visual pulse to pad status; require sustained loss to cancel the motor. */
    if(present) {connected=1;missing_ticks=0;}
    else if(missing_ticks<4 && ++missing_ticks==4) {connected=0;motor_cancelled=1;}
    if(!gameplay) {remaining=0;motor_cancelled=1;}
    else if(remaining>0) remaining--;
    motor_update();
}
void hit_feedback_offset(int *x,int *y) {
    static const int8_t dx[6]={1,-1,1,-1,0,0};
    static const int8_t dy[6]={0,1,0,-1,0,0};
    int frame=(HIT_TICKS-remaining)/2;
    *x=*y=0;
    if(remaining>0 && frame<6) {*x=dx[frame];*y=dy[frame];}
}
int hit_feedback_init(void) {
    connected=missing_ticks=0;hit_feedback_reset();
#ifndef HIT_FEEDBACK_TEST
    uint32_t *table=(uint32_t *)GetB0Table();
    uintptr_t physical=table[0x5b]&0x1fffffffu;
    /* Only the resident kernel's known instruction sequences are eligible.
     * Unknown/HLE BIOS layouts retain normal input, without rumble output. */
    if((physical&3) || physical<0x1000 || physical>0x10000-0x7c0) return 0;
    uint32_t *code=(uint32_t *)(physical|0xa0000000u);
    int second=rumble_patch_layout(code,0x7c0);
    if(!second) return 0;
    int critical=EnterCriticalSection();
    volatile uint32_t *patch=code;
    for(int port=0;port<2;port++) {
        int at=port?second:0x3d8;
        patch[at/4]=0x00551024; /* Keep the output byte instead of boolean clipping. */
        for(int i=1;i<4;i++) patch[at/4+i]=0;
    }
    FlushCache();
    typedef void (*SetOutput)(const volatile uint8_t *,int,const volatile uint8_t *,int);
    SetOutput set_output=(SetOutput)((physical+0x7a0)|0x80000000u);
    set_output(output[0],2,output[1],2);
    if(critical) ExitCriticalSection();
    dual_motor=configure_motors();
    return 1;
#else
    dual_motor=1;
    return 1;
#endif
}
