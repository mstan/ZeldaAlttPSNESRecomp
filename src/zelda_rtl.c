#include "zelda_rtl.h"
#include "variables.h"
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "cpu_state.h"
#include "funcs.h"
#include "debug_server.h"
#include "cpu_trace.h"

/* HLE the polyhedral coroutine that the SNES runs on a separate stack
 * (NMI tail context-switches into it; it loops at $09:F81D, runs one
 * slice of Poly_RunFrame, then yields back to main).
 *
 * Our HLE invokes Interrupt_NMI as a plain C call, so the context
 * switch into $09:F81D never executes — `intro_did_run_step` ($7E:1F00)
 * latches at 1 after Intro_AnimateTriforce's first call, and
 * Intro_RunStep is never re-entered. submodule_index sticks at 3
 * (Intro_HandleAllTriforceAnimations only animates; the transition to
 * submodule 4 lives inside Intro_RunStep's case 1, gated on poly_config1
 * decrementing below 225).
 *
 * Mirror the poly thread's loop body exactly (asm at $09:F81D):
 *   if ($7E:1F00 == 0) return;            ; intro_did_run_step
 *   if ($7E:1F0C != 0) return;            ; nmi_flag_update_polyhedral
 *   JSL Polyhedral_EmptyBitMapBuffer
 *   JSR Polyhedral_SetShapePointer
 *   JSR Polyhedral_SetRotationMatrix
 *   JSR Polyhedral_OperateRotation
 *   JSR Polyhedral_DrawPolyhedron
 *   STZ $00      ; intro_did_run_step = 0
 *   LDA #$FF     ; (m=1)
 *   STA $0C      ; nmi_flag_update_polyhedral = $FF
 *
 * One slice per host frame. NMI_UpdateIRQGFX at $80:9347 will clear
 * nmi_flag_update_polyhedral back to 0 on the next vblank-equivalent
 * (it's emitted by the recomp as part of Interrupt_NMI's chain), so the
 * two-flag handshake remains intact.
 *
 * Poly thread enters at $09:F81D with D=$1F00, DB=$09, PB=$09, P=$30
 * (m=1, x=1) per kPolyThreadInit. Call the M1X1 variants; save/restore
 * main-thread CPU context around the slice so subsequent main-thread
 * code keeps D=DB=PB=0.
 */
static void ZeldaRunPolyLoop(void) {
  if (g_ram[0x1F00] == 0)    return;   /* intro_did_run_step */
  if (g_ram[0x1F0C] != 0)    return;   /* nmi_flag_update_polyhedral */

  const uint16 saved_D  = g_cpu.D;
  const uint8  saved_DB = g_cpu.DB;
  const uint8  saved_PB = g_cpu.PB;
  const uint8  saved_P  = g_cpu.P;
  const uint8  saved_m  = g_cpu.m_flag;
  const uint8  saved_x  = g_cpu.x_flag;

  g_cpu.D      = 0x1F00;
  g_cpu.DB     = 0x09;
  g_cpu.PB     = 0x09;
  g_cpu.P      = 0x30;            /* m=1, x=1, others clear */
  cpu_p_to_mirrors(&g_cpu);

  Polyhedral_EmptyBitMapBuffer_M1X1(&g_cpu);
  Polyhedral_SetShapePointer_M1X1(&g_cpu);
  Polyhedral_SetRotationMatrix_M1X1(&g_cpu);
  Polyhedral_OperateRotation_M1X1(&g_cpu);
  Polyhedral_DrawPolyhedron_M1X1(&g_cpu);

  g_cpu.D      = saved_D;
  g_cpu.DB     = saved_DB;
  g_cpu.PB     = saved_PB;
  g_cpu.P      = saved_P;
  g_cpu.m_flag = saved_m;
  g_cpu.x_flag = saved_x;
  cpu_p_to_mirrors(&g_cpu);

  g_ram[0x1F00] = 0x00;
  g_ram[0x1F0C] = 0xFF;
}

static void ZeldaRestoreMainCpuAbi(void) {
  /* Host-frame entry/exit calls are not real JSR/IRQ return paths. Keep the
   * top-level main stack at the reset-loop baseline before handing control
   * back to generated game code. */
  g_cpu.S  = 0x01FF;
  g_cpu.D  = 0x0000;
  g_cpu.DB = 0x00;
  g_cpu.PB = 0x00;
}

void ZeldaDrawPpuFrame(void) {
  SimpleHdma hdma_chans[3];

  Dma *dma = g_dma;

  dma_startDma(dma, HDMAEN_copy, true);

  SimpleHdma_Init(&hdma_chans[0], &dma->channel[5]);
  SimpleHdma_Init(&hdma_chans[1], &dma->channel[6]);
  SimpleHdma_Init(&hdma_chans[2], &dma->channel[7]);

  int trigger = g_snes->vIrqEnabled ? g_snes->vTimer + 1 : -1;

  for (int i = 0; i <= 224; i++) {
    ppu_runLine(g_ppu, i);
    SimpleHdma_DoLine(&hdma_chans[0]);
    SimpleHdma_DoLine(&hdma_chans[1]);
    SimpleHdma_DoLine(&hdma_chans[2]);
    //    dma_doHdma(snes->dma);
    if (i == trigger) {
      // Simulate hardware IRQ latch: I_IRQ's first instruction reads HW_TIMEUP
      // ($4211) and branches on the N flag to distinguish timer-IRQ from
      // other sources. recomp_hw.c's ReadReg(0x4211) returns g_snes->inIrq<<7
      // and clears the flag; assert it here so the handler takes the
      // timer-IRQ path instead of exiting immediately.
      CpuState saved_cpu = g_cpu;
      g_snes->inIrq = true;
      I_IRQ(&g_cpu);
      g_cpu = saved_cpu;
      trigger = g_snes->vIrqEnabled ? g_snes->vTimer + 1 : -1;
    }
  }
}

void ZeldaRunOneFrameOfGame(void) {
  // First-call reset gate. Was previously `if (*(uint16*)$7F8000 == 0) I_RESET()`,
  // which silently relied on WRAM being zero-initialized at power-on. Real hardware
  // (and snes9x) power-on WRAM is 0x55, so that check would never fire and I_RESET
  // would be skipped, leaving $0100 (GameMode) at 0x55 — out-of-bounds for the
  // 42-entry dispatch table at PC 0x009329. Use a host-side bool instead so the
  // gate is independent of WRAM contents.
  static bool g_did_reset = false;
  static bool g_first_frame_done = false;
  if (!g_did_reset) {
    cpu_state_init(&g_cpu, g_ram);
    cpu_trace_px_breadcrumb(&g_cpu, 0x1000, "after_cpu_state_init");
    I_RESET(&g_cpu);
    cpu_trace_px_breadcrumb(&g_cpu, 0x1001, "after_I_RESET");
    g_did_reset = true;
  }
  cpu_trace_px_breadcrumb(&g_cpu, 0x2000, "before_NMI_or_Internal");
  // NMI handler runs BEFORE the main-loop game code each frame.
  //
  // On real hardware NMI fires at vblank start (between frames).
  // Its handler polls HW_JOY ($4218/$4219) into the $15-$18 mirror;
  // the next frame's game logic reads that mirror. Demo inputs are
  // applied INSIDE the main loop by overwriting $15/$16; if NMI's
  // poll runs LAST it clobbers the demo bytes with the empty
  // controller state ($00) and the end-of-frame mirror reads as 0.
  //
  // Per snes9x oracle trace at GM=07: emu's per-frame writer order
  // is poll($86B2/$86C1) → DamagePlayer($F62F/$F631) → GameMode07
  // demo-override($9C93/$9C9C); demo bytes are LAST and stick. With
  // recomp's prior `Internal(); auto_00_816A()` order, PollJoypad
  // ran last instead, leaving $15/$16=$00. End-of-frame snapshot
  // diverges from oracle, and demo timing skews because the
  // VariousPromptTimer / TitleInputIndex tick keys off observable
  // input state.
  //
  // Frame 0 is special: real hardware fires the first NMI AFTER
  // I_RESET completes AND the main loop has had time to set up flags
  // (notably SEP #$10 → x=1). If we run I_NMI before Internal on the
  // very first frame, I_NMI's PHP captures I_RESET-end's P (x=0); its
  // RTI then restores x=0 to the main loop. Subsequent ProcessGameMode
  // → UploadGraphicsFiles_Layer3 → TAY at $00:A9A5 then runs as 16-bit,
  // copying A's polluted high byte into Y, indexing past the GFX bank
  // table and writing $7E (instead of $0B) to $7E:008C. Skip I_NMI on
  // frame 0 so the order matches hardware: I_RESET → main loop →
  // (vblank) → I_NMI → main loop → ...
  // Assert NMI-pending so the recompiled NMI handler's read of $4210
  // (RDNMI) returns bit 7 = 1, matching real hardware. snes_readReg
  // clears the latch on read.
  if (g_first_frame_done) {
    CpuState saved_cpu = g_cpu;
    g_snes->inNmi = true;
    /* NMI handler at $00:80C9. zelda3 names it Interrupt_NMI; recompiler
     * emits the body under that name (no `I_NMI` alias). */
    Interrupt_NMI(&g_cpu);
    /* HLE thread-context-switch restore (2026-05-17). Interrupt_NMI's
     * asm ends with a polyhedral-thread context switch (TCS to poly
     * stack + PLB/PLD/PLY/PLX/PLA from kPolyThreadInit, then RTI). On
     * real hardware RTI pops PC from the poly stack and resumes the
     * poly thread, which eventually yields back to the main thread
     * (restoring main's D=0, etc.). Our HLE invokes Interrupt_NMI as
     * a plain C function — RTI just returns to C, leaving D=$1F00
     * (the poly-thread state pulled off the switched stack). That
     * leaks into Module_MainRouting / Module00_Intro, whose DP-rel
     * accesses then land at $7E:1Fxx instead of $7E:00xx. Module00's
     * dispatch reads $7E:1F11 (= 0 because Polyhedral wiped it),
     * dispatches case 0 → Intro_Init, jingle replays every cycle.
     * Snes9x oracle confirmed: real game has submodule_index at
     * $7E:0011 (D=0); recomp had it at $7E:1F11 (D=$1F00). Restore
     * main-thread CPU state here so the stack-switch branch cannot leak
     * S/A/X/Y/P either. Then re-establish the host ABI's D/DB/PB=0
     * contract so subsequent C-host module calls see the correct memory map. */
    g_cpu = saved_cpu;
    ZeldaRestoreMainCpuAbi();
    cpu_trace_px_breadcrumb(&g_cpu, 0x2001, "after_I_NMI");
    /* One slice of the polyhedral coroutine per host frame. See
     * ZeldaRunPolyLoop comment for the leak this fixes. */
    ZeldaRunPolyLoop();
  }
  cpu_trace_px_breadcrumb(&g_cpu, 0x2002, "before_Internal");
  ZeldaRestoreMainCpuAbi();
  /* Rearm the P.X tripwire here so the first x=1→0 transition INSIDE
   * Internal() (the main game loop) is captured fresh. The earlier
   * boot-time REP #$38 in I_RESET is expected and intentional; we only
   * want to know where x flips during ProcessGameMode dispatch. */
  cpu_trace_arm_px_tripwire();
  ZeldaRunOneFrameOfGame_Internal();
  ZeldaRestoreMainCpuAbi();
  cpu_trace_px_breadcrumb(&g_cpu, 0x2003, "after_Internal");
  g_first_frame_done = true;
}
