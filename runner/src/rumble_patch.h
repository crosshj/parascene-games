#pragma once
#include <stdint.h>
#include <stddef.h>

/* Recognize the BIOS pad-output clamp and setter before touching kernel RAM.
 * Offsets are relative to the B0 ChangeClearPAD entry, not a BIOS version or
 * ROM address. Both documented second-port layouts are accepted. */
static inline int rumble_clamp_at(const uint32_t *code,size_t bytes,int offset) {
    static const uint32_t clamp[4]={0x0002102b,0x10400002,0x00000000,0x0015102b};
    if(bytes<(size_t)offset+20) return 0;
    for(int i=0;i<4;i++) if(code[offset/4+i]!=clamp[i]) return 0;
    return code[offset/4+4]==0xa2220000;
}
static inline int rumble_patch_layout(const uint32_t *code,size_t bytes) {
    if(bytes<0x7c0 || !rumble_clamp_at(code,bytes,0x3d8)) return 0;
    const uint32_t *s=code+0x7a0/4;
    if((s[0]&0xfffffffe)!=0x3c010000 || (s[1]&0xffff0000)!=0xac240000 ||
       (s[2]&0xffff0000)!=0xac260000 || (s[3]&0xfffffffe)!=0x3c010000 ||
       (s[4]&0xffff0000)!=0xac250000 || (s[5]&0xffff0000)!=0xac270000 ||
       s[6]!=0x03e00008 || s[7]!=0x24020001) return 0;
    if(rumble_clamp_at(code,bytes,0x4dc)) return 0x4dc;
    if(rumble_clamp_at(code,bytes,0x4e0)) return 0x4e0;
    return 0;
}
