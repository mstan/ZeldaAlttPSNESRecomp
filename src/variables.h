/* Game-specific RAM variable declarations for Zelda LttP.
 *
 * SMW had a tall stack of named RAM variables (counter_global_frames,
 * waiting_for_vblank, etc.) declared here and used by the host
 * orchestration in zelda_rtl.c. Without a disasm we don't have
 * Zelda's RAM variable names yet — anonymous PCs in the generated
 * code refer to memory by raw address. Populate this file as we
 * identify named RAM regions (either from runtime trips or, once we
 * pivot, from the zelda3 decomp).
 *
 * Until then we re-export a handful of host-side variables the
 * snesrecomp framework expects: counter_global_frames,
 * waiting_for_vblank. These are framework-protocol, not game state.
 */

#ifndef VARIABLES_H
#define VARIABLES_H

#include "types.h"

/* g_ram is declared by snesrecomp/runner/src/common_rtl.h; don't
 * redeclare here. */

/* Host-protocol frame counters, populated by the orchestration in
 * zelda_rtl.c::ZeldaRunOneFrameOfGame. Names are framework-shaped,
 * not game-specific. */
extern uint16 counter_global_frames;

/* waiting_for_vblank is the SNES WRAM byte at $7E:0012 — ALttP's
 * NMI flag. The asm spinlock at $00:8034 was `LDA $12 ; BEQ -4`,
 * blocked until I_NMI writes $12 = 1. Our host-side orchestration
 * sets this same byte to 1 before calling Internal() each frame
 * (mirroring NMI's action) and clears it inside Internal()
 * (mirroring the STZ $12 at $00:805D). SMW uses $10 here; address
 * differs per game because the SNES ROM author picks the DP slot. */
#define waiting_for_vblank (*(uint8*)(g_ram + 0x12))

/* DMA / HDMA mirror — written by the recompiled NMI handler and
 * read by ZeldaDrawPpuFrame to drive snes9x's HDMA channels.
 * Address $00:420C in SMW; mirroring is host-side so we can read
 * the value without going through the PPU.
 *
 * Without a disasm we don't yet know if ALttP uses HDMA the same
 * way SMW does. Stub for now; if it's unused the build still works,
 * if it's used wrong we'll surface it at runtime. */
extern uint8  mirror_hdmaenable;

#endif /* VARIABLES_H */
