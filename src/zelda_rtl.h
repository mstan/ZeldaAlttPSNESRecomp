#ifndef ZELDA_RTL_H_
#define ZELDA_RTL_H_
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/snes_regs.h"

void ZeldaRunOneFrameOfGame_Internal(void);
void ZeldaDrawPpuFrame(void);
void ZeldaRunOneFrameOfGame(void);

#endif  // ZELDA_RTL_H_