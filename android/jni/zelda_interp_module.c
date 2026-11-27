/* Interpreter-only program module (ZELDA_INTERP_ONLY builds).
 *
 * The distributable Android APK contains NO code or data derived from the
 * game. Instead of the generated src/gen tree, this file supplies the handful
 * of symbols the runtime and src/zelda_rtl.c reference, and routes each of
 * them to the snesrecomp interpreter floor (interp816), which executes the
 * player's own ROM directly.
 *
 * Everything here is hand-written: the entry addresses come from the public
 * per-bank declarations in recomp/bank*.cfg, never from the ROM.
 *
 * The dispatch tables are empty, so every guest call misses AOT and runs on
 * the interpreter. A later on-device AOT module (compiled from the user's ROM)
 * can be selected with snes_program_module_select() to accelerate it.
 */
#include <stdio.h>
#include <stdlib.h>

#include "cpu_state.h"
#include "snes/interp_bridge.h"

/* ---- Empty program tables (same shape program_emit writes for no code) --- */
const DispatchEntry g_dispatch_table[] = {
    { 0xFFFFFFFFu, { NULL, NULL, NULL, NULL }, 0 },
};
const unsigned g_dispatch_table_count = 0u;

const AotResumeEntry g_aot_resume_table[] = {
    { 0xFFFFFFFFu, 0, NULL },
};
const unsigned g_aot_resume_count = 0u;

const RamRoutineGuard g_ram_routine_guards[] = {
    { 0xFFFFFFFFu, 0u, 0u },
};
const unsigned g_ram_routine_guard_count = 1u;

/* ---- Entry points used by the Zelda host (src/zelda_rtl.c) -------------- */

static void require_normal(RecompReturn r, const char *what) {
  if (r != RECOMP_RETURN_NORMAL) {
    fprintf(stderr, "[zelda-interp] non-local return %d leaked past %s\n", (int)r, what);
    abort();
  }
}

/* RESET ($00:8000) runs the boot code and falls into the main-loop vblank
 * spin at $00:8034 (bank00.cfg: `func I_RESET 8000 end:8034`). Interpret it
 * until it reaches that spin, exactly where the generated body returns. */
void I_RESET(CpuState *cpu) {
  if (!interp_bridge_run_scheduler(cpu, 0x008000u, 0x008034u, 0x0012u))
    fprintf(stderr, "[zelda-interp] RESET did not reach the main loop cleanly\n");
}

/* NMI handler at $00:80C9. */
void Interrupt_NMI(CpuState *cpu) {
  uint8 hrv = cpu->host_return_valid;
  cpu->host_return_valid = 0;
  cpu_interrupt_context_enter();
  RecompReturn r = interp_tier_dispatch_interrupt(cpu, 0x0080C9u);
  cpu_interrupt_context_leave();
  cpu->host_return_valid = hrv;
  require_normal(r, "Interrupt_NMI");
}

/* IRQ handler at $00:82D8 (bank00.cfg vector). */
void I_IRQ(CpuState *cpu) {
  uint8 hrv = cpu->host_return_valid;
  cpu->host_return_valid = 0;
  cpu_interrupt_context_enter();
  RecompReturn r = interp_tier_dispatch_interrupt(cpu, 0x0082D8u);
  cpu_interrupt_context_leave();
  cpu->host_return_valid = hrv;
  require_normal(r, "I_IRQ");
}

/* Main-loop slices used by the HLE scheduler. The caller already pushed the
 * JSR/JSL frame; interp_tier_dispatch returns when the routine RTS/RTLs. */
#define INTERP_ENTRY(name, pc) \
  RecompReturn name(CpuState *cpu) { return interp_tier_dispatch(cpu, (pc)); }

INTERP_ENTRY(ClearOamBuffer_M1X1, 0x00841Eu)
INTERP_ENTRY(Module_MainRouting_M1X1, 0x0080B5u)
INTERP_ENTRY(NMI_PrepareSprites_M1X1, 0x0085FCu)

/* Polyhedral coroutine slice (bank09.cfg). */
INTERP_ENTRY(Polyhedral_EmptyBitMapBuffer_M1X1, 0x09FD04u)
INTERP_ENTRY(Polyhedral_SetShapePointer_M1X1, 0x09F83Du)
INTERP_ENTRY(Polyhedral_SetRotationMatrix_M0X0, 0x09F864u)
INTERP_ENTRY(Polyhedral_OperateRotation_M0X1, 0x09F8FBu)
INTERP_ENTRY(Polyhedral_DrawPolyhedron_M1X1, 0x09FA4Fu)
