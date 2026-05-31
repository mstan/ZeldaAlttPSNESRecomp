#include "cpu_state.h"
#include "funcs.h"
#include "zelda_rtl.h"
#include "variables.h"

/* Storage for the host-protocol variables declared in variables.h.
 * counter_global_frames drives framework-side frame counting.
 * waiting_for_vblank and HDMAEN_copy are WRAM macros, so they need no
 * separate host storage. */
uint16 counter_global_frames = 0;


/* Host-side replacement for the asm main-loop spinlock at
 * $00:8034-$00:8060. Mirrors what zelda3's ZeldaRunGameLoop does
 * per frame: ClearOamBuffer → Module_MainRouting →
 * NMI_PrepareSprites → clear nmi_boolean ($00:0012 in WRAM).
 *
 * The cfg has `exclude_range 8034 8061` so the recompiler doesn't
 * emit the asm spinlock body; the I_RESET function ends just
 * before the spinlock thanks to `end:8034`. Each frame the SDL
 * main loop calls into ZeldaRunOneFrameOfGame which calls this
 * (after the per-frame I_NMI on non-zero frames). */
void ZeldaRunOneFrameOfGame_Internal(void) {
  assert(waiting_for_vblank != 0);
  /* Match the M/X state of the asm splice point at $00:8034. The
   * preceding asm at $00:8027 is `SEP #$30` (m=1, x=1); the spinlock
   * and per-frame JSR/JSL chain run in that contract. I_RESET's
   * intermediate `JSR $8901` chain transits PHP/PLP-bracketed
   * routines (e.g. $8888) that the static analyzer doesn't yet
   * classify as flag-preserving, so cpu->m_flag/x_flag can drift to
   * 0 by the time I_RESET returns. Re-establish the contract here so
   * the per-frame functions decode their operand widths correctly.
   * See ENHANCEMENTS.md: "PHP/PLP-balanced classification". */
  g_cpu.m_flag = 1;
  g_cpu.x_flag = 1;
  g_cpu.P |= 0x30;
  ++counter_global_frames;
  /* INC $1A — global frame counter, written each iteration by the
   * asm at $8051. Stays in WRAM since the recompiled
   * Module_MainRouting reads it. */
  ++g_ram[0x001A];
  /* The replaced ROM loop used JSR ClearOamBuffer, JSL Module_MainRouting,
   * then JSR NMI_PrepareSprites. Model those guest return frames at this
   * explicit HLE boundary so generated RTS/RTL epilogues never pop live
   * low-WRAM state as return bytes. */
  uint16 _s_main_pre = g_cpu.S;
  cpu_push_jsr_return_frame(&g_cpu);
  ClearOamBuffer(&g_cpu);
  cpu_push_jsl_return_frame(&g_cpu);
  Module_MainRouting(&g_cpu);
  cpu_push_jsr_return_frame(&g_cpu);
  NMI_PrepareSprites(&g_cpu);
  g_cpu.S = _s_main_pre;
  /* STZ $12 — clear the vblank-pending flag. */
  g_ram[0x0012] = 0;
  waiting_for_vblank = 0;
}

void ResetSpritesFunc(int wh) {
  for (; wh < 128; wh++)
    g_ram[0x201 + wh * 4] = 0xf0;
}
