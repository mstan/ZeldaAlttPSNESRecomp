# Seamless World experiment

Owner-approved plan, 2026-10-27. Game: `beads-8wg.7.4`; isolated engine
execution: `beads-8wg.2.162`. Reference: NES Zelda `beads-2dw.18.3` and its
explicit deferred limits in `beads-2dw.18.4`.

Worktrees, builds, profiles and raw evidence:
`G:/Temp/Codex/alttp-seamless-20261027`. Original working copies are preserved.
No release publication or delivery-folder replacement is part of this task.

## Contract

Optional, default-off local-play enhancement. Real terrain and persistent
native actors, adaptive horizontal/vertical Fit, mouse-wheel pixel zoom, F4
whole-current-map view. Visible areas plus a halo tick; other areas retain
state. The atlas activates the entire current world or connected dungeon
floor. Ordinary crossings become seamless; special entrances, stairs, pits,
warps, Mirror and world changes keep native routing. Cross-boundary combat
and projectiles are completion gates, not assumed from moving neighbors.

The independent world canvas uses square pixels, matching the NES experiment's
pixel zoom. Native fallback screens retain the title's existing pixel aspect
in both SDL software and OpenGL presenters.

Preserve stock/adaptive rendering, MSU audio and native SRAM. Current-build
states/rewind/rollback must work; old build states have no compatibility
promise. No netplay qualification is implied.

Owner clarification, 2026-10-28: full-map 60 FPS is an optional stretch goal,
never a completion blocker. Slowdown from rendering and simulating the whole
map is acceptable. Measure costs to describe the experience and identify
correctness regressions; do not gate the atlas on maintaining 60 FPS.

## Burndown

| Milestone | Status | Evidence / next gate |
|---|---|---|
| 0. Frozen baseline | Built | Game `8eee8b6`, engine `68896b6`, UI `eaf99ca`; GCC 15.2 MinGW, SDL3, trace on. Baseline executable SHA256 `0d765132d7ce3c055e22a95ad51ed85774ec7d708e674dcfa668252b8e7b4d5d`. Verified identical generation inputs. |
| 1. Isolated native calls | Initial gates passed | Success and forced interrupt-context/budget abort restore the serialized guest exactly, keep state generation, and leave bridge/host stack idle. Native decoder round trip passed. Main-thread-only entry; queued TCP mutations. Broader portability/side-effect qualification pending. |
| 2. Real overworld atlas | Partial | All 40 canonical areas in each Light/Dark World decode into separate 4096x4096 maps. Native tile/gfx/palette loaders, cached Map16 rasters. Layer priority, overlays, progression variants and native visual equality still open. |
| 3. Adaptive presentation | Partial | Independent width/height canvas, mouse zoom 1..8, consumed F4 atlas shortcut, native OAM projected into world space and native transparent HUD anchored to the actual viewport. The current atlas is 4096x4096, without a reserved HUD strip. Earlier 4096x4128 captures are historical. Follow, atlas, paused zoom, portrait and inventory overlay captures retained. Paused redraw keeps the native image frozen; frame counter and sampled RAM stay unchanged, then host resume advances again. Menu/effect and complete layer composition pending. |
| 4. Live overworld preview | Initial gates passed | Native Sprite_Main ticks visible areas plus 512px halo, or all current-world areas in atlas. Additional native pools activate 281/281 authored Light World and 275/275 Dark World spawns in the tested progression phase. Inactive areas retained. Native E/W/S/N crossings stay module 9/submodule 0 with continuous coordinates. Other progression phases and all mechanics remain unqualified. |
| 5. Cross-area interactions | Partial | Native sword hit/parry, enemy-to-Link damage, bomb and arrow damage, and ordinary pickup reward publication demonstrated across an edge. Attached arrows retain native target references across handoffs and into additional actor pools. Global ancillas survive a handoff and project outside the old viewport. Other projectile collisions/lifetime, enemy migration, authored secret indices, puzzle/NPC progression and broader inter-pool interactions remain open. |
| 6. Connected dungeon floors | Pending | Content-derived membership, floors/layers, doors, keys/shutters/puzzles. |
| 7. Presentation and state | Partial | Pointer-free native room records, OAM pieces, terrain changes and execution residue serialized with strict preflight and feature identity. 30 idle frames and 70 moving frames across area 18ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢19 replay byte-identically. Native Mirror/portal round trip across an ordinary seam passed; menu/dialogue/effects/death/Continue and broader travel qualification remain open. |
| 8. Qualification/handoff | Pending | Content-derived coverage, matched frame/memory/hitch costs, portability, saved individual human observations. |

Do not interpret implemented code, an atlas screenshot or an entrance load as
a passed gameplay gate. Record failures and unverified content separately.

## Sources and reuse

The existing AOT/LLE runtime remains authoritative. Native area loading and
actor routines are reused, not replaced with a second game implementation.
Local `snesrev/zelda3` at `fbbb3f967a51fafe642e6140d0753979e73b4090`
(MIT) supplies original PCs, WRAM layout, canonical area heads and topology.
Map16 definitions are read from the supplied USA ROM at `$0F:8000`;
graphics and palettes are produced by its native loaders. No extracted
graphics/maps are checked in or distributed.

## Initial evidence and known gaps

Raw command journals, screenshots and source/binary receipts are in the task
root above. `profiles/seams/*-frames.json` records four directed crossings:
18ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢19 east, 18ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢17 west, 21ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢29 south, 29ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢21 north. The setup places Link on a
native walkable corridor and explicitly leaves the seed's AsleepInBed handler;
only controller movement performs the crossing. It is an assisted setup, not
evidence of completing the prologue or walking through a dungeon exit.

`profiles/state-probe/result.json` records current-build save/load and idle
replay; `profiles/boundary-replay/result.json` records moving replay and an
actual handoff. Whole serialized bytes match, including neighboring actors.
These gates do not establish native SRAM progression, rewind UI, all terrain
interactions, or netplay support.

`profiles/active-pool-projection/result.json` and
`profiles/qualification-pools-v2/light-probe.json` record 281/281 authored
Light World spawns loaded. `profiles/qualification-pools-v2/dark-result.json`
records 275/275 Dark World spawns, zero scoped-call failures, and a 20-frame
full-atlas replay with both worlds retained in the serialized cache. The Dark
World setup uses the original exit table for room 74 with explicit world and
equipment assists; it does not validate a Mirror or portal journey.

Every eight authored actors receive a separate native pool with spare slots
for probes and other dynamic children. The tested maps need at most three
pools per area. Pools retain their full native RAM contexts and share terrain
and graphics. A native Mirror portal is retained only in the live context;
neighbor loading does not clone it. Per-actor camera projection also runs in
the active area so its distant actors do not disappear through native view
culling. Link is collected after the original Module09's final LinkOam draw.

`profiles/native-mirror-seam-v4/result.json` records a real Y-button Mirror
journey from Dark World area 82 to Light World area 18, controller crossings
18 to 19 and back, then native portal contact returning to area 82. The
single live portal survives handoffs and a byte-exact 20-frame replay; area
caches contain no portal clones. The initial exit/equipment/position setup
and moving physical guards away are explicit assists. Earlier failed probes
are retained: native Mirror immunity and subsequent guard damage blocked
portal contact until their blink timers expired; they are not counted as
successful portal routing checks.

`profiles/seam-sword-protected/result.json` demonstrates the neighboring
guard's health changing 6 to 4 while Link stays in area 18 and the guard stays
across its eastern edge. Player protection is an explicit test assist.
`profiles/native-seam-damage/result.json` demonstrates native damage/recoil
reaching Link across the same edge. The ordinary pickup probe increased bombs
0 to 8; it explicitly bypassed the authored item's hidden-under-a-rock
prerequisite, so lifting/destructible terrain remains unverified.

`profiles/native-seam-bomb/result.json` records a real bomb explosion across
the area 18/19 edge: the neighboring native guard receives pending damage 4
and health falls from 6 to 2 while Link remains in area 18. Only equipment,
protection and the existing guard's position/facing were assisted. Each
neighbor reuses the original enemy-damage routine at the captured explosion
phase; bomb movement, fuse, inventory cost and player damage run once.

`profiles/weapon-projection-v4/result.json` records an arrow traveling beyond
the original viewport while Link remains fixed, zero scoped failures, exact
scope restoration, and a 30-frame byte-exact replay. Both native camera-copy
sets and the original OAM allocator are restored after weapon projection.
This is presentation/lifetime evidence, not a passed cross-area arrow damage
gate. State schema/feature identity is now `alttp-world-v3` and includes the
projected weapon pieces. A hardware reset cancels a developer replay.

`profiles/native-arrow-seam-v4/result.json` records an ordinary bow arrow
damaging the area 19 guard from health 6 to 2 while Link starts in area 18,
then remaining attached as Link crosses into area 19 and back. A 12-frame
replay with the remote attachment is byte-exact. `profiles/native-arrow-extra-pool/result.json`
records the same health change and replay for the guard in area 27 pool 1.
Only equipment/protection and the existing guard's position/facing were
assisted. The original `$08:8D68` collision result resumes the original arrow
at `$08:A16E`; movement, ammo and timers do not advance in the deferred pass.
Pointer-free area/pool/slot/type/spawn handles temporarily expose the actual
target to the native attached-arrow routine, then restore the live slot
before any enemy executes. Schema/feature identity is now `alttp-world-v4`.
This does not qualify every enemy, projectile, native reference or progression
phase. Far-projectile native terrain windows now pass the ray check below; native secret indexing remains open.

`profiles/viewport-input-qualification/commands.jsonl` records actual process
SDL F4 and wheel events. F4 toggles the atlas without loading a deliberately
different save slot 4; zoom changes 3 to 2 and back. `profiles/paused-portrait-v3/result.json`
records the actual 900x1500 window producing a 300x500 canvas, the same native
frame/RAM while paused, and resumed frame progress. The test helper schedules
the existing host pause command on the main loop; physical controls remain an
owner handoff check. Native raster/IRQ rendering is not repeated while paused.

`profiles/opengl-world` retains actual GPU framebuffer readbacks of follow
and full atlas views. `profiles/opengl-portrait` verifies a 900x1500 window:
native image top/bottom margins 356/356, atlas margins 297/296, and a filled
follow canvas. The title's OpenGL presenter now centers its viewport on both
axes. Hidden-window, dummy-audio QA preserves desktop focus; owner launches
retain normal visibility and audio defaults.

`profiles/world-boundaries/result.json` records eight assisted boundary cases
in Light/Dark Worlds: bottom/right overflow and north/west underflow clamp
without an ordinary handoff or accidental world change. The ROM-free
coordinate test checks all 65,536 native coordinate words in both worlds.
Configure with `-DBUILD_TESTING=ON` to run it and the generation-hook test via
CTest. Extra pool memory is reserved outside guest scopes, with unused and
failed output allocations reclaimed after the scope returns.

Cross-area projectile collision/lifetime, enemy migration, authored secret
indices, NPC receipts and other one-time progression remain incomplete gates.
Native object references and enemy-to-enemy interactions across separate pools
are not qualified. Dungeon floors still use the normal renderer.

Initial full-atlas samples were about 30.9ms native simulation plus 7.3ms
composition, before complete interactions and without matched host isolation.
This is not a 60Hz result or a qualified performance comparison. Retain raw
samples and measure the completed feature before choosing an optimization.

## Owner-reported presentation fixes, 2026-10-28

The owner found stale scene pixels behind the HUD, flashing water/grass and
corrupted terrain after travel, and inventory falling back to an earlier
native area. These are correctness fixes within the experiment.

The HUD now uses the engine's transparent BG3 capture during the one normal
native scanout. Magic/items, counters and hearts anchor to the viewport's
left, center and right at a window-based scale independent of world zoom.
Terrain fills the complete canvas. Inventory uses the same native BG3 layer
over the expanded world; the menu's native input, slide animation and actor
pause rules remain authoritative. Active-area sprite projection remains in
place while the native inventory draws. Other native interfaces/effects are
still unqualified and may use the existing fallback.

The animated-terrain decoder incorrectly passed the graphics pack in A. The
ROM entry at `$00:D394` reads **Y**, so new areas loaded pack 0 (castle art)
instead of ordinary pack `$5A` or mountain pack `$58`. Each cached area now
receives the right three native frames. Neighbor rendering follows the live
native animation clock using its own pack, without advancing simulation.
`profiles/world-qualification-light-v6/qualification.json` and its Dark World
counterpart compare all 80 canonical areas with feature-off native exits,
observe changing neighbor animation, verify complete authored populations,
and pass 70-frame byte-exact replay (including a real Light World crossing).
Reference receipts are in `evidence/native-animation-reference-v6.json`.

`profiles/hud-layer-v6-final/result.json` passes native layer extraction,
paused zoom/atlas, successful/aborted isolation and 20-frame replay.
`profiles/inventory-layer-v6-final/result.json` passes inventory opening and
closing after an area adoption, 900x1500 paused resizing with a frozen native
frame, current-build save/load while inventory is open, and 20-frame replay.
Composed captures and actual GPU framebuffer readbacks are retained beside
these receipts. State identity is `alttp-world-v6`, including native UI pixels.

Native collision buffers now switch between Link's world window, each
traveling weapon's world window and the active area's canonical terrain for
its enemies. Areas near a global projectile stay active. The content-derived
`profiles/native-projectile-terrain-v3/result.json` gate demonstrates a native
arrow reaching a wall beyond Link's old 1024px collision window and stopping
there; guard positions were assisted away from the ray. `profiles/collision-window-qualification`
retains population, moving replay and bounded guard sight-probe observations.
The invisible probes now use their parent's native view for lifetime
rejection; centering on each probe had prevented that rejection.

The corrected owner build is staged separately in `owner-demo-fixed`, with
fresh build-specific Pyramid, Village and Seam checkpoints. The earlier
owner profile, SRAM, settings and observations remain preserved. Full-map
60 FPS remains optional, and connected dungeon floors and the remaining
interaction/progression gates remain unfinished.

## Native map, entrances and foreground follow-up

The owner clarified that the map flash was the game's own X-button Mode 7
map, not F4. `profiles/native-x-map-before/open-0.bmp` reproduces a stale,
garbled native background during its opening fade. The expanded world now
remains visible during that native fade and the restoring submodules 32-34;
the original Mode 7 map, zoom and input still run normally. World terrain
and projected pieces are retained while Mode 7 temporarily replaces native
graphics. Native brightness fades the expanded scene and resolves BG3 once.
`profiles/native-x-map-fixed` retains the corrected opening/closing captures.

Cave and door tables use positions relative to the canonical native area.
The moving Link collision window had changed their origin. The original
`$1B:BBF4` entrance routine now receives the canonical buffer and offsets,
after Link's movement and before lookup/persistent writes. Both interpreted
entry and every generated AOT width variant are covered; regeneration tests
also scan bank 1B. `profiles/native-entrances-baseline-aligned` versus
`profiles/native-entrances-fixed-aot-aligned` show native entries 95 and 53
changing from blocked movement to actual module-7 interiors (rooms 271 and
16), with entry 116 still reaching room 272. Entry 103's +8px alignment was
invalid in both builds; the earlier x=3472 control reached room 282. These
are assisted doorway setups followed by real controller movement, not
quest-completion claims.

The terrain compositor now retains each BG2 pixel's native high/low priority
and transparency. OBJ priority tests that surface before drawing, so an arch
covers Link at its actual world coordinates. Native off-screen 9-bit X
markers used to hide blinking Link pieces are culled rather than projected
256 pixels left. Damage blinking itself remains native. The protected
`profiles/arch-exact-protected-v6` and `profiles/arch-priority-fixed-v6`
captures at x=792, y=1456 show the arch occlusion change; y=1504 remains
visible below it with visibility, lower-level and blink fields zero. The
owner's disappearing-body screenshot also needs the corrected-build human
check; a still image alone does not distinguish damage blinking. BG1,
color-math and broader layering remain open.

The combined candidate uses state identity `alttp-world-v7`. No old-state
migration is provided. Native SRAM and previous owner notes remain preserved.

## Developer probes

The normal TCP server handles game commands on its network thread. World
commands queue to the game thread at a frame boundary; do not run guest
loaders from network callbacks. Raw background `read_ram` may observe a
temporary neighbor context. Use `game world` and `game world_area ID` for
authoritative player/actor observations.

`game world_isolation` verifies successful and aborted scopes. `game
world_decode ID` verifies core restoration around a native area decode.
`game world_edge ID east|west|north|south` is an assisted placement on a
content-derived walkable corridor, without setting crossing/result flags.
`game world_replay N [12-bit hexadecimal input mask]` records N complete
frames, restores the current-build checkpoint and replays the same input.
`game world_replay_status` reports the first mismatch and final byte equality.
Use isolated disposable saves and muted automated sessions.

`python tools/probe_world.py --port PORT --output NEW.json --coverage --edge
18 east --frames 70 --mask 80` runs the reproducible Light World coverage,
isolation and boundary replay gate against an already running disposable
session. For Dark World coverage omit the Light World edge setup. The tool
never launches a game or chooses a save/profile and preserves prior evidence.
`game world_area ID [POOL]` inspects an additional pool. The `world_qa_*` and
`world_place_actor` commands are explicit encounter assists, not gameplay
completion claims.

`--animation-reference PATH` extends `tools/probe_world.py` with native
feature-off `regular`/`mountain` frame-hash arrays; it checks every canonical
area and observes changing neighbor animation before the replay gate.

Combined v7 validation: `profiles/world-qualification-{light,dark}-v7`
passes all 80 native animation families, changing neighbor animation, full
authored populations and 70-frame byte-exact replay in both worlds (including
a native seam crossing). `hud-layer-v7-final`, `inventory-layer-v7-final`
and `native-x-map-v7` pass current-build save/replay and their respective
paused/portrait/native-map checks. The native map opens and closes with the
same player position, native Mode 7 controls, no scope failures and a
20-frame byte-exact map replay. CTest coordinates/generation hooks pass 2/2.

The next owner handoff uses `owner-demo-followup` with three independent
checkpoints: Arch plus native map, Pyramid-side Cave, and Seam. Only the Arch
setup has assisted damage protection to separate occlusion from hit blinking.
The first test is paused in a normal window with normal audio. Earlier owner
SRAM/settings/states/failed notes remain in their original profiles.


## Owner presentation follow-up (2026-10-28, current schema v8)

The owner confirmed cave entry and exit working in v7. Three separate later
reports remained: black tree corners, a small Link flicker at seams, and a zoom
jump immediately before entry/after exit. The original individual observations
are retained in `owner-demo-followup/observations.json` outside the repository.

Native Pyramid captures show a deliberately black backdrop, with BG1 on the
subscreen supplying terrain through transparent BG2 pixels. Each area now runs
the original overlay decoder/NMI upload and retains native scroll, palette,
subscreen and color-math state. The compositor resolves that layer behind BG2
and the eligible OBJ palettes. A native-versus-expanded Pyramid comparison matches
35,636/35,804 registered background pixels (99.53%), excluding colors from
the native OBJ palettes because expanded actor populations differ. The unmasked
repeat matches 96.99%; that is not a full-scene equality gate. It has zero new
black pixels. Native scanout starts at line 1; comparison accounts for that
registration. This does not qualify all weather, lighting or windows.

Frame-by-frame area 82->83 evidence reproduced missing Link body pieces at the
native 256px view edges. Link's original drawing routine now uses its own
centered projection, retaining native blink, equipment and foreground priority.
Both AOT and interpreted entries/returns are covered; generator tests include
bank 0D and all width variants. Before/after image sequences are retained in
`profiles/seam-flicker-before` and `profiles/seam-flicker-after`.

Outdoor closing/opening iris modules and the exit walk now retain the expanded
view. The native transition still owns loading, input and timing; iris progress
is applied over the actual viewport. An actual cave round trip checks every
captured visible outdoor transition frame at 512x288; interiors retain their
native view. A 70-frame moving seam replay remains byte-identical. The background
metadata is serialized in v8; earlier experimental snapshots are disposable.

Combined checks: native X-map open/zoom/close with replay, HUD/atlas paused redraw
and exact isolated-call restoration, inventory portrait/current-build load and
replay, and both CTests pass. Full-map 60 FPS remains optional. Connected dungeon
floors, other weapons/carry/migration/progression, remaining effects and broader
qualification are still open. No merge or release is claimed.
