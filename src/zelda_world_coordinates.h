#pragma once
#include <stdint.h>

/* Native Link coordinates wrap as 16-bit words. Interpret edge underflow as
 * negative before clamping, so walking west/north cannot jump to the far edge.
 * The feet/center used for ownership must stay inside the same 4096px world. */
static inline int ZeldaWorldLinkCoordinate(uint16_t native) {
 int n=(int16_t)native;return n<0?0:n>4080?4080:n;
}
/* Overworld_OperateCameraScroll ($02:BB90) follows the elevated position
 * when allow_scroll_z is set. A south ledge can move landing Y ahead and
 * add the same distance to Z in one frame; following raw Y snaps the view.
 * Keep native word wrapping and its FFFF no-height sentinel. */
static inline int ZeldaWorldFollowY(uint16_t y,uint16_t z,int scroll_z) {
 return (int16_t)(uint16_t)(y-(scroll_z&&z!=UINT16_MAX?z:0));
}
static inline unsigned ZeldaWorldCell(int x,int y,unsigned world) {
 x=x<0?0:x>4095?4095:x;y=y<0?0:y>4095?4095:y;
 return (unsigned)(y/512*8+x/512)+(world&64);
}
