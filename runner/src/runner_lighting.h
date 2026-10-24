#pragma once
#include <stdint.h>
#include "level.h"

/* Lift shadow texels with diffuse sky fill in pale, bright environments.
 * The least-lit channel keeps dark or strongly saturated worlds unchanged.
 * Texture transparency and white highlights are invariant. GPU tint still
 * supplies the directional/color lighting after this palette adjustment. */
static inline uint16_t runner_ambient_texel(uint16_t texel, const uint8_t sky[3]) {
    int ambient=sky[0],strength,peak=0,c,out=texel&0x8000;
    if (!texel) return 0;
    if(sky[1]<ambient) ambient=sky[1];
    if(sky[2]<ambient) ambient=sky[2];
    strength=(ambient-80)*64/144;
    if(strength<=0) return texel;
    if(strength>64) strength=64;
    for(c=0;c<3;c++) {
        int value=(texel>>(c*5))&31;
        if(value>peak) peak=value;
    }
    for(c=0;c<3;c++) {
        int value=(texel>>(c*5))&31;
        int fill=sky[c]*strength*(31-peak)/(256*31*8);
        value+=fill;
        if(value>31) value=31;
        out|=value<<(c*5);
    }
    return (uint16_t)out;
}

/* Art-directed fill handles bright ground beneath a dark/saturated sky.
 * White highlights remain unchanged and the original palette stays intact. */
static inline uint16_t runner_lit_texel(uint16_t texel,const Light *light) {
    const uint8_t *fill=light->runner_ambient;
    int peak=0,c,out=texel&0x8000;
    if (!(fill[0]|fill[1]|fill[2])) return runner_ambient_texel(texel,light->sky);
    if (!texel) return 0;
    for(c=0;c<3;c++) {
        int value=(texel>>(c*5))&31;
        if(value>peak) peak=value;
    }
    for(c=0;c<3;c++) {
        int value=(texel>>(c*5))&31;
        value+=fill[c]*(31-peak)/(31*8);
        if(value>31) value=31;
        out|=value<<(c*5);
    }
    return (uint16_t)out;
}
