#include "mod_runtime.h"
#include "config.h"

/*
 * Widescreen moved out of the launcher's Display settings and into the mod
 * package catalog, matching Mega Man X / X2 / Super Mario World / Super Mario
 * Kart. The adaptive renderer, sprite culling and Mode 7 margin work all stay
 * where they are — this plugin only owns the player-facing activation.
 *
 * ALttP's widescreen is adaptive-only (a single config flag, no fixed-16:9 or
 * HUD-split variant), so the feature is a plain on/off with no options.
 *
 * The reset callback runs before active plugins on every launch, so a disabled
 * feature deterministically restores stock 4:3 even if config.ini still carries
 * a widescreen value written by an older build.
 */

static void zelda_widescreen_reset(void) {
  g_config.widescreen = 0;
}

static void zelda_widescreen_activate(void) {
  g_config.widescreen = 1;
}

SNES_MOD_CONSTRUCTOR(zelda_register_widescreen_plugin) {
  (void)snes_mod_register_reset_callback(zelda_widescreen_reset);
  (void)snes_mod_register_activation_plugin("zelda-alttp.widescreen",
                                            zelda_widescreen_activate);
}
