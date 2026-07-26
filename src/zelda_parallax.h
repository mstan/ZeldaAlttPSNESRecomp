#ifndef ZELDA_PARALLAX_H
#define ZELDA_PARALLAX_H

#include <stdbool.h>

/* A Link to the Past's binding for the shared layered-parallax presenter
 * (snesrecomp/runner/src/parallax.h, docs/PARALLAX.md).
 *
 * ALttP is TOP-DOWN, which makes this a materially different effect from the
 * side-scroller ports (MMX/SMW) even though it is the same machinery:
 *
 *   * In a side-scroller the layers are at different DISTANCES and the game
 *     already scrolls them at different rates, so the presenter's job is to
 *     turn an existing parallax ratio into visible depth.
 *   * Top-down, the layers all scroll together — there is no parallax ratio to
 *     amplify. What the camera tilt produces instead is a TABLETOP view: the
 *     floor recedes toward a horizon, the way Mode 7 does it. The perspective
 *     keystone is the entire point here, whereas in a side-scroller the same
 *     keystone is just distortion of a plane the player reads for platforming.
 *
 * That also means the layer separation should stay SMALL. ALttP's two map
 * layers are a real physical stack (BG1's treetops/upper-floor sit above BG2's
 * ground), but only by a few world pixels — pushing them far apart would read
 * as the overlay floating, not as depth. The tilt does the work; the gap only
 * has to keep the stack ordered.
 */

/* Install the ALttP layer profile and seed the master switch from g_config.
 * Call once, after the config files are parsed. */
void ZeldaParallax_Init(void);

/* Per-frame gate + capture policy. Call from RtlDrawPpuFrame, after the
 * widescreen side-space policy and BEFORE draw_ppu_frame(). */
void ZeldaParallax_PrepareFrame(int frame_width, int frame_height, int extra);

/* Hotkey toggle (persists to config.ini). */
void ZeldaParallax_Toggle(void);

#endif  /* ZELDA_PARALLAX_H */
