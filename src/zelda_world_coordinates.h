#pragma once
#include <stdint.h>

/* Native Link coordinates wrap as 16-bit words. Interpret edge underflow as
 * negative before clamping, so walking west/north cannot jump to the far edge.
 * The feet/center used for ownership must stay inside the same 4096px world. */
static inline int ZeldaWorldLinkCoordinate(uint16_t native) {
 int n=(int16_t)native;return n<0?0:n>4080?4080:n;
}
static inline unsigned ZeldaWorldCell(int x,int y,unsigned world) {
 x=x<0?0:x>4095?4095:x;y=y<0?0:y>4095?4095:y;
 return (unsigned)(y/512*8+x/512)+(world&64);
}
