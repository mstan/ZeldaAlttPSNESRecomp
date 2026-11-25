#include "common_cpu_infra.h"
#include "zelda_rtl.h"
#include "zelda_world.h"
#include "cpu_state.h"
#include "snes/saveload.h"
#include <string.h>

#define ZELDA_HOST_STATE_MAGIC 0x31545348u /* HST1 */
static uint32 s_loaded_frame_driver;
static void ZeldaStateSave(SaveLoadInfo *sli) {
  ZeldaWorldStateSave(sli);
  if (!ZeldaWorldEnabled()) RtlSaveExecutionState(sli);
  uint32 host[] = {ZELDA_HOST_STATE_MAGIC, ZeldaFrameDriverState()};
  sli->func(sli, host, sizeof(host));
}
static bool ZeldaStatePreflight(const void *data, size_t size) {
  const uint8 *bytes = data;
  uint32 host[2], version = 0, saved_version = 0;
  size_t guest = RtlSnapshotGuestSize(&version), execution = RtlExecutionStateSize();
  if (!bytes || !guest || size < guest || size - guest < execution + sizeof(host)) return false;
  memcpy(host, bytes + size - sizeof(host), sizeof(host));
  memcpy(&saved_version, bytes + sizeof(uint32), sizeof(saved_version));
  if (saved_version != version || host[0] != ZELDA_HOST_STATE_MAGIC || host[1] > 3 ||
      !RtlExecutionStateValid(bytes + size - sizeof(host) - execution, execution)) return false;
  return ZeldaWorldEnabled() ? ZeldaWorldStatePreflight(data, size - sizeof(host))
                            : size == guest + execution + sizeof(host);
}
static void ZeldaStateLoad(SaveLoadInfo *sli, uint32 version) {
  uint32 host[2] = {0};
  ZeldaWorldStateLoad(sli, version);
  if (!ZeldaWorldEnabled() && !RtlLoadExecutionState(sli)) {
    RtlStateStreamFail(sli);
    return;
  }
  sli->func(sli, host, sizeof(host));
  if (host[0] != ZELDA_HOST_STATE_MAGIC || host[1] > 3) {
    RtlStateStreamFail(sli);
    return;
  }
  s_loaded_frame_driver = host[1];
}
static void ZeldaStateLoaded(uint32 version) {
  g_cpu.ram = g_ram;
  ZeldaWorldStateLoaded(version);
  if (!ZeldaWorldEnabled()) RtlApplyExecutionState();
  ZeldaRestoreFrameDriverState(s_loaded_frame_driver);
}

const RtlGameInfo kSmwGameInfo = {
  .title = "smw",
  .initialize = NULL,
  .run_frame = &RunOneFrameOfGame,
  .draw_ppu_frame = &ZeldaDrawPpuFrame,
  .save_name_prefix = "save",
  .frame_end = ZeldaWorldFrameEnd,
  .frame_begin = ZeldaWorldFrameBegin,
  .hardware_reset = ZeldaWorldHardwareReset,
  .state_save_extra = ZeldaStateSave,
  .state_load_extra = ZeldaStateLoad,
  .on_state_loaded = ZeldaStateLoaded,
  .snapshot_guard_identity = ZeldaWorldStateIdentity,
  .snapshot_preflight = ZeldaStatePreflight,
};
