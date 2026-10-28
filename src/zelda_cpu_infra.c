#include "common_cpu_infra.h"
#include "zelda_rtl.h"
#include "zelda_world.h"

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .initialize = NULL,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &ZeldaDrawPpuFrame,
  .save_name_prefix = "save",
  .frame_end = ZeldaWorldFrameEnd,
  .frame_begin = ZeldaWorldFrameBegin,
  .hardware_reset = ZeldaWorldReset,
  .state_save_extra = ZeldaWorldStateSave,
  .state_load_extra = ZeldaWorldStateLoad,
  .on_state_loaded = ZeldaWorldStateLoaded,
  .snapshot_guard_identity = ZeldaWorldStateIdentity,
  .snapshot_preflight = ZeldaWorldStatePreflight,
};
