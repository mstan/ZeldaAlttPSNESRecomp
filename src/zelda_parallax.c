#include "zelda_parallax.h"

#include <stdio.h>

#include "config.h"
#include "parallax.h"
#include "snes/ppu.h"
#include "zelda_rtl.h"

extern Ppu *g_ppu;
extern bool g_new_ppu;
extern uint8_t g_ram[0x20000];

/* zelda_rtl.c's ZW8/ZW16 helpers are defined in the .c, not the header, so read
 * WRAM directly here rather than duplicating the macros. $7E maps to g_ram[0]. */
#define ZeldaWram8(off) ((int)g_ram[(off)])
extern void WriteConfigFile(const char *filename);
extern const char *ZeldaParallax_ConfigPath(void);

/* ── Layer stack ──────────────────────────────────────────────────────────
 *
 * Array order IS the painter's draw order and mirrors the Mode-1 priority
 * ranks (snes/ppu.c PpuDrawBackgrounds' table), so occlusion matches hardware.
 *
 * ALttP layer roles — note these are the OPPOSITE way round from a
 * side-scroller, and getting them backwards would tilt the world inside out:
 *   BG2  the GROUND. This is the layer Link walks on, and the layer the
 *        game's own camera scroll is read from — ZeldaConfigurePpuSideSpace
 *        derives the visible margin from BG2HOFS/BG2VOFS ($7E:00E2/$7E:00E8),
 *        which is what identifies BG2 as the primary map layer. This is the
 *        FOCAL plane: the camera frames on it and it stays put.
 *   BG1  the OVERLAY / upper layer — treetops, upper-floor edges, bridge
 *        decks, the things Link passes underneath. Physically ABOVE the
 *        ground, so it sits slightly in front of the focal plane. This is a
 *        genuine physical stack, not a decorative choice.
 *   OBJ  Link, enemies, objects. Standing ON the ground, so just barely in
 *        front of BG2 — enough to order them, not enough to detach them.
 *   BG3  the HUD (hearts, magic meter, item box). Forward and flat.
 *
 * The gaps are deliberately TIGHT (see the header): a top-down stack is only
 * a few world pixels tall. The camera pitch supplies the perspective; the
 * depths only keep the stack ordered.
 *
 * KNOWN LIMITATION, and the one most likely to decide whether this works at
 * all: sprites are captured as a flat full-screen PLANE, so under a tabletop
 * tilt Link is painted onto the sloping floor rather than standing upright on
 * it. Fixing that properly needs per-sprite billboard geometry driven from OAM
 * (decode each sprite's screen rect, emit an upright quad at the ground point)
 * — a real change, not a constant. ar-recomp does not do it either; its
 * sprites are a flat plane too, which is tolerable in a side-scroller where
 * the sprite plane is parallel to the screen and much less so here.
 */
static const ParallaxPlaneDesc kZeldaPlanes[] = {
  /* plane                    group                   z      shade r,g,b   shadow */
  { kParallaxPlane_Backdrop,  kParallaxGroup_Backdrop, 0.44f, 0.80f, 0.80f, 0.86f, false },
  { kParallaxPlane_Bg3,       kParallaxGroup_Bg3,      0.92f, 1.00f, 1.00f, 1.00f, false },
  { kParallaxPlane_Obj,       kParallaxGroup_Obj,      0.54f, 1.00f, 1.00f, 1.00f, false },
  { kParallaxPlane_Obj1,      kParallaxGroup_Obj,      0.54f, 1.00f, 1.00f, 1.00f, false },
  { kParallaxPlane_Bg2,       kParallaxGroup_Bg2,      0.50f, 1.00f, 1.00f, 1.00f, false },
  { kParallaxPlane_Bg1,       kParallaxGroup_Bg1,      0.58f, 0.97f, 0.97f, 0.99f, true  },
  { kParallaxPlane_Obj2,      kParallaxGroup_Obj,      0.55f, 1.00f, 1.00f, 1.00f, false },
  { kParallaxPlane_Bg2Hi,     kParallaxGroup_Bg2,      0.51f, 1.00f, 1.00f, 1.00f, false },
  { kParallaxPlane_Bg1Hi,     kParallaxGroup_Bg1,      0.59f, 0.97f, 0.97f, 0.99f, true  },
  { kParallaxPlane_Obj3,      kParallaxGroup_Obj,      0.56f, 1.00f, 1.00f, 1.00f, false },
  { kParallaxPlane_Bg3Hi,     kParallaxGroup_Bg3,      0.93f, 1.00f, 1.00f, 1.00f, false },
};

static const ParallaxProfile kZeldaProfile = {
  .name = "zelda3",
  .planes = kZeldaPlanes,
  .plane_count = (int)(sizeof(kZeldaPlanes) / sizeof(kZeldaPlanes[0])),
  .capture_mask = (1u << kPpuOverlaySource_Bg1) |
                  (1u << kPpuOverlaySource_Bg2) |
                  (1u << kPpuOverlaySource_Bg3) |
                  (1u << kPpuOverlaySource_Obj),
};

void ZeldaParallax_Init(void) {
  Parallax_SetProfile(&kZeldaProfile);
  g_parallax.enabled = g_config.parallax;
  /* Top-down wants a much stronger pitch than the side-scrollers: this is a
   * tabletop view, and a timid tilt just looks like a mistake rather than a
   * perspective. Layer separation stays small (see the plane table). */
  g_parallax.tilt_x_mrad = 420;
  g_parallax.layer_gap = 100;
  g_parallax.depth_shade = 30;
  if (g_parallax.enabled) {
    char state[256];
    Parallax_DescribeState(state, sizeof state);
    fprintf(stderr, "[parallax] %s\n", state);
  }
}

void ZeldaParallax_Toggle(void) {
  g_config.parallax = !g_config.parallax;
  g_parallax.enabled = g_config.parallax;
  printf("Parallax = %s\n", g_config.parallax ? "on" : "off");
  WriteConfigFile(ZeldaParallax_ConfigPath());
}

/* main_module_index ($7E:0010): 9 = overworld, 7 = dungeon — the two modules
 * that are actually a walkable world. Module 14 is a menu overlaying another
 * module, so resolve through saved_module_for_menu ($7E:010C) exactly the way
 * ZeldaConfigurePpuSideSpace does, and then EXCLUDE it: the inventory screen
 * and the world map are flat UI, not a world.
 *
 * Reusing the widescreen policy's module vocabulary on purpose — it is already
 * the proven "is this a real room/overworld view" discriminator in this port. */
static bool ZeldaParallaxSceneIsWorld(void) {
  int main_module = ZeldaWram8(0x10);
  if (main_module == 14)
    return false;                       /* menu / map overlay: keep it flat */
  return main_module == 9 || main_module == 7;
}

/* Camera motion for the presenter's lean (Parallax_ReportCameraMotion). ALttP's
 * camera is BG2's scroll (BG2 is the ground layer — see the plane table), which
 * is also what ZeldaConfigurePpuSideSpace reads for the widescreen margin.
 *
 * Scroll registers are 10-bit and wrap, so wrap the difference to the shortest
 * signed distance rather than subtracting raw. A delta beyond a plausible frame
 * of travel is a discontinuity (room transition, warp, mirror) and is reported
 * as zero, not as a huge sweep. */
static void ZeldaParallaxReportMotion(void) {
  static bool have_prev;
  static int prev_x, prev_y;
  if (!g_ppu) return;
  int x = g_ppu->hScroll[1] & 0x3ff;
  int y = g_ppu->vScroll[1] & 0x3ff;
  if (!have_prev) {
    have_prev = true;
    prev_x = x;
    prev_y = y;
    Parallax_ReportCameraMotion(0.0f, 0.0f);
    return;
  }
  int dx = ((x - prev_x + 512) & 0x3ff) - 512;
  int dy = ((y - prev_y + 512) & 0x3ff) - 512;
  prev_x = x;
  prev_y = y;
  const int kMaxFrameTravel = 24;   /* px; Link on the Pegasus boots is inside this */
  if (dx > kMaxFrameTravel || dx < -kMaxFrameTravel) dx = 0;
  if (dy > kMaxFrameTravel || dy < -kMaxFrameTravel) dy = 0;
  Parallax_ReportCameraMotion((float)dx, (float)dy);
}

void ZeldaParallax_PrepareFrame(int frame_width, int frame_height, int extra) {
  ZeldaParallaxReportMotion();
  /* Parallax REQUIRES the priority-buffer PPU: host-overlay layer extraction
   * only exists on that path, so without this the feature would silently
   * capture nothing whenever the legacy renderer is selected. */
  if (Parallax_Enabled())
    g_new_ppu = true;
  /* Mode 1 is the only BG mode with overlay capture wired up. ALttP's world
   * views are Mode 1; the world map and some effects use Mode 7. */
  bool mode1 = g_ppu && PPU_mode(g_ppu) == 1;
  Parallax_PrepareFrame(g_ppu, frame_width, frame_height, extra,
                        mode1 && ZeldaParallaxSceneIsWorld());
}
