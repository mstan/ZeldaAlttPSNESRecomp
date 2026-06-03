#include "common_cpu_infra.h"
#include "zelda_rtl.h"

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .initialize = NULL,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &ZeldaDrawPpuFrame,
  .save_name_prefix = "save",
};
