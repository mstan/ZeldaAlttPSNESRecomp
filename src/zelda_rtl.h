#ifndef ZELDA_RTL_H_
#define ZELDA_RTL_H_
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes_regs.h"

void RunOneFrameOfGame_Internal(void);
void ZeldaDrawPpuFrame(void);
void RunOneFrameOfGame(void);

// Widescreen game policy (read-only): derive the PPU's per-side visible margin
// from live WRAM (room / scroll bounds) each frame and apply it. Called from
// RtlDrawPpuFrame only while widescreen is active. Reimplemented from snesrev's
// zelda3 ConfigurePpuSideSpace (MIT); see IMPROVEMENTS.md / attribution.
void ZeldaConfigurePpuSideSpace(void);

#endif  // ZELDA_RTL_H_