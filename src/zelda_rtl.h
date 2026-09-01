#ifndef ZELDA_RTL_H_
#define ZELDA_RTL_H_
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes_regs.h"

// ALttP's widescreen policy is authored for the snesrev/zelda3 446px logical
// limit: 256 native columns plus 95 columns per side. Wider shared-engine caps
// can expose wrapped tilemap content beyond this game's validated room data.
enum { kZeldaWsExtraMax = 95 };

void ZeldaDrawPpuFrame(void);
void RunOneFrameOfGame(void);

// Widescreen game policy (read-only): derive the PPU's per-side visible margin
// from live WRAM (room / scroll bounds) each frame and apply it. Called from
// RtlDrawPpuFrame only while widescreen is active. Reimplemented from snesrev's
// zelda3 ConfigurePpuSideSpace (MIT); see IMPROVEMENTS.md / attribution.
void ZeldaConfigurePpuSideSpace(void);

// Replaces Sprite_PrepOamCoordOrDoubleRet's stock horizontal Carry result
// with bounds expanded by the current adaptive framebuffer margin.
void ZeldaAdjustSpritePrepHorizontalCull(CpuState *cpu);

#endif  // ZELDA_RTL_H_
