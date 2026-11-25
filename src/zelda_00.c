#include "cpu_state.h"
#include "funcs.h"
#include "zelda_rtl.h"
#include "variables.h"

/* Storage for the host-protocol variables declared in variables.h.
 * counter_global_frames drives framework-side frame counting.
 * waiting_for_vblank and HDMAEN_copy are WRAM macros, so they need no
 * separate host storage. */
uint16 counter_global_frames = 0;


void ResetSpritesFunc(int wh) {
  for (; wh < 128; wh++)
    g_ram[0x201 + wh * 4] = 0xf0;
}
