#include "mod_runtime.h"
#include "config.h"
#include "zelda_world.h"

static void activate(void) {
  g_config.widescreen=0;
  ZeldaWorldEnable();
}
static void display_mode_claim(void) {}
SNES_MOD_CONSTRUCTOR(zelda_register_world_plugin) {
  (void)snes_mod_register_reset_callback(ZeldaWorldDisable);
  (void)snes_mod_register_activation_plugin("zelda-alttp.seamless-world",activate);
  (void)snes_mod_register_activation_plugin("zelda-alttp.display-mode",display_mode_claim);
}
