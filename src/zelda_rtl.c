#include "zelda_rtl.h"
#include "variables.h"
#include "common_cpu_infra.h"
#include "snes/snes.h"
#include "cpu_state.h"
#include "execution_mode.h"
#include "funcs.h"
#include "debug_server.h"
#include "cpu_trace.h"
#include "widescreen.h"  // g_ws_extra, PpuSetExtraSideSpace via snes/ppu.h
#include "snes/interp_bridge.h"   /* faithful LLE of the $8034 main loop */

static SnesrecompExecutionMode zelda_execution_mode(void) {
  /* LLE is the correctness floor. The hand-written frame driver remains an
   * explicit optimization selected with SNESRECOMP_EXECUTION_MODE=hle. */
  return snesrecomp_execution_mode(SNESRECOMP_EXECUTION_MODE_LLE);
}

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

// Read-only accessors over the recompiled ROM's WRAM image. Offsets are the
// US-ROM WRAM addresses documented by snesrev/zelda3 (src/variables.h); the
// recompiled game maintains the same variables at the same addresses (banks
// $00-$3F mirror WRAM $0000-$1FFF, see common_rtl.c). g_ram is the 128KB WRAM.
extern uint8 g_ram[];
#define ZW8(off)  ((int)g_ram[(off)])
#define ZW16(off) ((int)(g_ram[(off)] | (g_ram[(off) + 1] << 8)))

static int ZwMax0(int v) { return v > 0 ? v : 0; }

void ZeldaConfigurePpuSideSpace(void) {
  // Reimplementation of zelda3's ConfigurePpuSideSpace (snesrev/zelda3, MIT)
  // over the recomp's live WRAM. Derives the per-side visible margin from the
  // current scroll position and room/overworld bounds, clamped so backgrounds
  // never reveal past the real room edge; PpuSetExtraSideSpace further clamps
  // each side to the configured framebuffer capacity (g_ws_extra). On screens
  // not handled here the margin stays 0 => centered pillarbox.
  //
  // Re-establish the centering budget every frame (ppu_reset zeroes it on soft
  // reset / load-state) and start the per-side margins at 0 (pillarbox).
  PpuSetExtraSpaceCentered(g_ppu, (uint8_t)g_ws_extra);

  int extra_left = 0, extra_right = 0, extra_bottom = 0;

  int main_module = ZW8(0x10);          // main_module_index   ($7E:0010)
  int submodule   = ZW8(0x11);          // submodule_index     ($7E:0011)
  int mod = main_module;
  if (mod == 14)                        // in a menu: use the module it overlays
    mod = ZW8(0x10C);                   // saved_module_for_menu ($7E:010C)

  int bg2hofs = ZW16(0xE2);             // BG2HOFS_copy2 (horiz scroll, $7E:00E2)
  int bg2vofs = ZW16(0xE8);             // BG2VOFS_copy2 (vert scroll,  $7E:00E8)

  if (mod == 9) {
    // Overworld. ow_scroll_vars0 @ $7E:0600 = uint16 {ystart,yend,xstart,xend}.
    if (main_module == 14 && submodule == 7 && ZW8(0x200) >= 4) {
      // World map (overworld_map_state >= 4): full margin, no bounds to clamp.
      extra_left = extra_right = kPpuExtraLeftRight;
      extra_bottom = 16;
    } else {
      int ow_yend   = ZW16(0x602);
      int ow_xstart = ZW16(0x604);
      int ow_xend   = ZW16(0x606);
      extra_left   = bg2hofs - ow_xstart;
      extra_right  = ow_xend - bg2hofs;
      extra_bottom = ow_yend - bg2vofs;
    }
  } else if (mod == 7) {
    // Dungeon. room_bounds_x @ $7E:0608, room_bounds_y @ $7E:0600 (uint16 v[4]).
    // Skip the horizontal widen while the dark-room lantern light cone is up
    // (its mask is authored for the 256-wide view only).
    int dark_lantern = ZW8(0x458);      // hdr_dungeon_dark_with_lantern
    int ts_copy      = ZW8(0x1D);       // TS_copy
    if (!(dark_lantern && ts_copy != 0)) {
      int qm = ZW8(0xA6) >> 1;          // quadrant_fullsize_x >> 1  (0 or 1)
      int bx_lo = ZW16(0x608 + qm * 2);         // room_bounds_x.v[qm]
      int bx_hi = ZW16(0x608 + (qm + 2) * 2);   // room_bounds_x.v[qm+2]
      extra_left  = ZwMax0(bg2hofs - bx_lo);
      extra_right = ZwMax0(bx_hi - bg2hofs);
    }
    int qy = ZW8(0xA7) >> 1;            // quadrant_fullsize_y >> 1
    int by_hi = ZW16(0x600 + (qy + 2) * 2);     // room_bounds_y.v[qy+2]
    extra_bottom = ZwMax0(by_hi - bg2vofs);
  } else if (mod == 20 || mod == 0 || mod == 1) {
    // Attract/intro/title scenes that pan a full scene: full margin.
    extra_left = extra_right = kPpuExtraLeftRight;
    extra_bottom = 16;
  }

  PpuSetExtraSideSpace(g_ppu, extra_left, extra_right, extra_bottom);

  // --- Tier D: widescreen HUD split (self-contained; revert this block to ----
  // pluck it out). The BG3 status strip is 256-wide tilemap content that
  // otherwise floats centered in the wide frame. Re-anchor its outer groups to
  // the screen edges: left group = magic meter (tiles 2-4) + Y-item box (5-7)
  // -> left edge; center = rupee/bomb/arrow/key counts (8-19) -> stay centered;
  // right group = hearts + LIFE (20+) -> right edge. The magic-meter column is
  // the tallest element and (with BG3's gameplay scroll) its lower rows reach
  // ~scanline 63, so the split band is 64px tall to keep the whole bar in one
  // piece rather than vertically clipping it. Only during
  // actual gameplay (overworld 9 / dungeon 7), where BG3 shows just the status
  // strip. Module 14/submodule 2 is the text renderer layered over the saved
  // gameplay module, so retain the split there; other interface states such as
  // dungeon/world maps and item menus render normally. The PPU primitive also
  // self-disables on any frame
  // BG3 carries a real window (e.g. transition irises). Entirely BG3 tiles —
  // no OAM elements in the bar, so nothing is left behind. (snesrev/zelda3 +
  // xander-haj/Z3R HUD-rearrange concept; see IMPROVEMENTS.md.)
  bool gameplay_hud = main_module == 7 || main_module == 9 ||
      (main_module == 14 && submodule == 2 && (mod == 7 || mod == 9));
  if (gameplay_hud) {
    PpuSetWidescreenHudSplit(g_ppu, 64, 64, 160);
    PpuSetWidescreenHudAlwaysVisible(g_ppu, true);
  } else {
    PpuSetWidescreenHudSplit(g_ppu, 0, 0, 0);
    PpuSetWidescreenHudAlwaysVisible(g_ppu, false);
  }
  // --- end Tier D HUD split -----------------------------------------------
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

void RunOneFrameOfGame(void) {
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
    cpu_push_interrupt_frame(&g_cpu);
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
  /* Swappable scheduler tier (mirrors mmx_rtl.c / smw_rtl.c). ALttP has no
   * cooperative task scheduler — its "scheduler" is the single main loop at
   * $00:8034:
   *     $8034: LDA $12 ; BEQ $8034            ; vblank-wait spin (NMI sets $12)
   *            ... INC $1A ; JSR ClearOamBuffer ; JSL Module_MainRouting ;
   *            JSR NMI_PrepareSprites ; STZ $12 ; BRA $8034
   * so LLE is interp_bridge_run_scheduler with entry == yield == the spin PC
   * (bank $00 — reset leaves PB=$00 and the BRA loop never leaves bank 0) and
   * flag == waiting_for_vblank ($12). The interp runs the real per-frame body
   * (incl. the alternate-frame slowdown path the HLE reproduces by hand) and
   * bounces module code to compiled bodies via the paired ABI (or interprets
   * when SNESRECOMP_LLE_BOUNCE=0). I_NMI already set $12 != 0; force it too so
   * frame 0 (I_NMI skipped) processes — matches MMX's waiting_for_vblank=0xFF.
   *
   * LLE is the default correctness path. The existing HLE frame driver stays
   * available as a convenience override through the shared execution-mode
   * option rather than a Zelda-specific scheduler switch. */
  {
    if (zelda_execution_mode() == SNESRECOMP_EXECUTION_MODE_LLE) {
      waiting_for_vblank = 0xFF;
      interp_bridge_run_scheduler(&g_cpu, 0x008034, 0x008034, 0x0012);
    } else {
      RunOneFrameOfGame_Internal();
    }
  }
  ZeldaRestoreMainCpuAbi();
  cpu_trace_px_breadcrumb(&g_cpu, 0x2003, "after_Internal");
  g_first_frame_done = true;
}
