#include "mod_runtime.h"
#include "common_rtl.h"
#include "config.h"
#include "snes/msu1.h"

#include <stdint.h>

#define ZELDA_MSU_PLUGIN "zelda-alttp.msu1"

#define ZELDA_MSU_VOLUME_FULL 0xff
#define ZELDA_MSU_VOLUME_MAP  0x75
#define ZELDA_MSU_FADE_START  0xf1
#define ZELDA_MSU_FADE_STOP   0x20
#define ZELDA_MSU_FADE_STEP   0x02

#define MSU_STATUS_ERROR 0x08
#define MSU_CONTROL_PLAY 0x01
#define MSU_CONTROL_REPEAT 0x02

static int g_zelda_msu1_active;
static int g_zelda_msu1_fading;
static uint8_t g_zelda_msu1_fade_volume;

static int zelda_msu1_command_loops(uint8_t command) {
  switch (command) {
    case 0x01:
    case 0x08:
    case 0x0a:
    case 0x0f:
    case 0x13:
    case 0x1d:
    case 0x21:
    case 0x22:
      return 0;
    default:
      return 1;
  }
}

static int zelda_msu1_try_play(uint8_t command) {
  const uint8_t control = (uint8_t)(MSU_CONTROL_PLAY |
      (zelda_msu1_command_loops(command) ? MSU_CONTROL_REPEAT : 0));

  if (command == 0 || command == 0xf1 || command == 0xf2 ||
      command == 0xf3 || command == 0xf4)
    return 0;

  if (g_ram[0x0129] == command) {
    g_ram[0x0133] = 0xf1;
    return 1;
  }

  msu1_write(0x2004, command);
  msu1_write(0x2005, 0);
  if (msu1_read(0x2000) & MSU_STATUS_ERROR) {
    msu1_write(0x2007, 0);
    return 0;
  }

  g_zelda_msu1_fading = 0;
  msu1_write(0x2006, ZELDA_MSU_VOLUME_FULL);
  msu1_write(0x2007, control);
  g_ram[0x0127] = 0;
  g_ram[0x0129] = command;
  g_ram[0x012c] = 0;
  g_ram[0x0130] = command;
  g_ram[0x0133] = 0xf1;
  return 1;
}

static void zelda_msu1_start_fade(void) {
  if (!g_zelda_msu1_fading) {
    g_zelda_msu1_fading = 1;
    g_zelda_msu1_fade_volume = ZELDA_MSU_FADE_START;
  }
  g_ram[0x012c] = 0xf1;
  g_ram[0x0130] = 0xf1;
  g_ram[0x0133] = 0xf1;
}

static void zelda_msu1_tick(void) {
  if (!g_zelda_msu1_active || !msu1_enabled() || !g_zelda_msu1_fading)
    return;

  if (g_zelda_msu1_fade_volume < ZELDA_MSU_FADE_STOP + ZELDA_MSU_FADE_STEP) {
    g_zelda_msu1_fading = 0;
    g_zelda_msu1_fade_volume = 0;
    g_ram[0x0129] = 0;
    msu1_write(0x2006, 0);
    msu1_write(0x2007, 0);
    return;
  }

  g_zelda_msu1_fade_volume =
      (uint8_t)(g_zelda_msu1_fade_volume - ZELDA_MSU_FADE_STEP);
  msu1_write(0x2006, g_zelda_msu1_fade_volume);
}

static int zelda_msu1_apu_write(uint16_t reg, uint8_t value) {
  if (!g_zelda_msu1_active || !msu1_enabled() || reg != 0x2140)
    return 0;

  switch (value) {
    case 0x00:
      return 0;
    case 0xf1:
      zelda_msu1_start_fade();
      return 1;
    case 0xf2:
      msu1_write(0x2006, ZELDA_MSU_VOLUME_MAP);
      return 1;
    case 0xf3:
      msu1_write(0x2006, ZELDA_MSU_VOLUME_FULL);
      return 1;
    case 0xf4:
      g_ram[0x0129] = 0;
      msu1_write(0x2007, 0);
      return 1;
    default:
      return zelda_msu1_try_play(value);
  }
}

static void zelda_msu1_reset(void) {
  g_zelda_msu1_active = 0;
  g_zelda_msu1_fading = 0;
  g_zelda_msu1_fade_volume = 0;
  g_config.msu1_enabled = false;
}

static void zelda_msu1_activate(void) {
  g_zelda_msu1_active = 1;
  g_zelda_msu1_fading = 0;
  g_zelda_msu1_fade_volume = 0;
  g_config.msu1_enabled = true;
  (void)snes_mod_register_frame_callback(zelda_msu1_tick);
  (void)snes_mod_register_apu_write_callback(zelda_msu1_apu_write);
}

SNES_MOD_CONSTRUCTOR(zelda_register_msu1_plugin) {
  (void)snes_mod_register_reset_callback(zelda_msu1_reset);
  (void)snes_mod_register_activation_plugin(ZELDA_MSU_PLUGIN,
                                            zelda_msu1_activate);
}
