# Seamless World experiment

Owner-approved plan, 2026-10-27. Game: `beads-8wg.7.4`; isolated engine
execution: `beads-8wg.2.162`. Reference: NES Zelda `beads-2dw.18.3` and its
explicit deferred limits in `beads-2dw.18.4`.

Worktrees, builds, profiles and raw evidence:
`G:/Temp/Codex/alttp-seamless-20261027`. Original working copies are preserved.
The owner authorized an experimental launcher release after the known
implementation burndown; no release has been published yet.

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
| 5. Cross-area interactions | Partial | Native sword/parry, enemy damage, bombs/arrows, ordinary pickups, held-object handoffs, boomerang/rod/hookshot/beam/Somaria impacts demonstrated. Roaming activation, native seam walking and terrain collision have initial gates. Attached arrows retain target references across handoffs and extra pools. Native NPC/item receipts and global spell damage now have focused gates below. Authored secret indices, complete spell presentation and broader puzzle/inter-pool qualification remain open. |
| 6. Connected dungeon floors | Initial implementation, qualification partial | Native floor-map membership with reciprocal-door fallback, original Map8/BG1/BG2 graphics, live resident actors and expanded door/plate animations. Eastern Palace native switch, room201->185, locked185->186/key consumption/reverse door, current-build save/load and replay pass. Actual stairs/other dungeons and complete lighting/effects remain unqualified. |
| 7. Presentation and state | Partial | Pointer-free native room records, OAM pieces, terrain changes and execution residue serialized with strict preflight and feature identity. 30 idle frames and 70 moving frames across area 18 -> 19 replay byte-identically. Native Mirror/portal round trip across an ordinary seam passed; menu/dialogue/effects/death/Continue and broader travel qualification remain open. |
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
18 -> 19 east, 18 -> 17 west, 21 -> 29 south, 29 -> 21 north. The setup places Link on a
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

Broader projectile/terrain variants, authored secret indices and other one-time
progression remain incomplete gates. Native object references and enemy-to-enemy
interactions across separate pools are not qualified. Connected dungeon floors
use the independent compositor; lighting and cross-room projectiles remain open.

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


## Ledge camera follow-up (2026-10-28, current schema v9)

An actual controller-triggered Pyramid ledge jump rebases landing Y from1912
to1992 and height from4 to84 in one frame. The old expanded camera followed
raw Y and shifted terrain80 pixels although Link's elevated position did not
move. Camera follow and activation now use the original `$02:BB90` rule:
subtract native height only when `allow_scroll_z` is set, with its FFFF
sentinel and word wrapping. Link's separate drawing projection is centered on
his elevated position so tall jumps do not clip body pieces at native bounds.

The previous drawing change also saved four consecutive words at E0..E6,
missing BG2 Y at E8 because BG3 X occupies E4. Projection restoration and
per-area BG1/BG2 metadata now use the explicit E0/E2/E6/E8 offsets. State
identity v9 rejects snapshots carrying the earlier incorrect metadata.

Bounded before/final frame captures and `evidence/ledge-camera-motion.json`
measure the takeoff terrain displacement changing from -80 to0 pixels with
100% equality of the registered terrain patch. Controller jumps at the Pyramid
and town ledges complete; a110-frame jump replay is byte-identical. Midair
save/load, paused zoom without time advance, a fresh ordinary seam crossing
and both CTests pass. New Ledge/Town/Seam checkpoints use assisted placement,
equipment and damage protection, without forcing jump/landing handlers or
success flags. This does not complete the remaining dungeon/progression gates.


## Connected dungeon floor checkpoint (2026-10-28, schema v10)

The original 320 room streams supply reciprocal ordinary door topology. The
in-game map tables at `$0A:F605` supply current-floor membership for mapped
palaces, including same-floor rooms reached by special doors. Unmapped caves
and interiors retain the connected door component. Native signed floor numbers
and basement offsets are respected; the Eastern Palace data probe identifies
eight rooms on 1F and four on 2F. That data probe is not a staircase traversal.

Each resident room retains its original Map8 layers, native graphics/palettes,
collision context and actor state. The same world compositor, native per-actor
projection, activation halo, zoom and F4 controls are reused. Tile priority and
BG1 subscreen color math use the room's native layer state. The normal dungeon
loader and gameplay continue to own keys, shutters, pressure plates, room
routing and door animations. Cached actors are adopted after the native loader
installs a connected destination, before its first projected sprite draw.

Private decoding initially exposed two calling-convention errors: A2 is the
previous-room field, while the sprite-loader room is 048E; and bank-1 room
code reads its object/door data in bank 0. The corrected loader decodes all
eight tested rooms and restores the serialized live core exactly. Geometry
inventory parses all 320 streams; this is content inventory, not 320 gameplay
passes. No extracted graphics or room data are distributed.

Recorded gates in the task's SERVER profiles:

- `dungeon-floor-doors-final`: actual A-button pot lift/throw and pressure-plate
  shutter, natural room201->185 crossing, live floor atlas and byte-exact
  20-frame replay. Current-build save/load restores the atlas and actors.
- Its bounded frame capture contains 15 native pressure-plate animation frames;
  every captured frame retains the expanded 512x288 view.
- `dungeon-floor-key-final`: assisted position/quadrant/camera at the authored
  plane-0 locked door. Zero keys block entry. Supplying one key lets the native
  routine consume it, save the opened-door bit, finish185->186 and return through
  the open door. A40-frame replay started during travel is byte-identical.
  Position/camera/key inventory are explicit assists; no door/travel/result flags
  are set. Finding the key and navigating the full route are not claimed.
- `dungeon-floor-boundary`: observed native201->185 transition states all keep
  512x288 bounds and retain the destination's running actor state.
- `dungeon-floor-ui-final`: queued SDL F4 enters the1536x2048 floor atlas,
  wheel zoom works, and returning to follow while paused preserves frame/RAM.
  The helper now clears the full SDL_Event union: a C `{0}` initializer left
  modifier bytes unspecified in the optimized build. Physical input handling
  is unchanged. All three CTests pass; current Light/Dark seam70-frame replays
  remain byte-exact after the floor integration.

The ordinary door routines retain their native walk/loading cadence; this does
not claim removal of their control lock. Actual stair/pit/warp travel and the
remaining dungeon effects still need qualification. Current-build room records
are pointer-free and validated in schema v10; earlier experiment states are
disposable. Native SRAM is preserved.

At this floor checkpoint, known implementation work included global spell coverage, roaming actors' terrain and
activation, inter-pool NPC interaction/secret progression, and remaining
interfaces/lighting/windows. This floor checkpoint does not close the full
burndown and is not a release claim. Whole-map60 FPS remains optional.

## Held-object handoff checkpoint (2026-10-28)

An original A-button bush lift reproduced a missing actor at18->19: Link kept
his carrying pose after the area actor slab replaced the held bush. Held actors
now travel with the live context like the unique Mirror portal. Cached outdoor
pools exclude them, destination slots are reserved before any mutation, and
attached-arrow handles follow an actor when its slot changes. Authored physical
actors are never overwritten to make room.

`native-carry-seam-final` validates a real bush lift, a40-frame byte-exact moving
replay across18->19, current-build save/load while holding,19->18 return without
cached held clones, and native A-button throw/break. Only player position and
equipment were assisted; the lift/carry/throw handlers were not forced. The
existing actual Mirror/portal seam round trip still passes after this change.
Other carried enemy types and dungeon carry routing remain unqualified.

## Other projectile checkpoint (2026-10-28)

Neighbor pools now reuse the original complete weapon handler from its exact
pre-entry image in a restoring guest scope. Native collision RTS results decide
whether to publish the impact's actor, weapon, garnish and sound changes. The
real weapon advances movement, timers and inventory once. Fire bursts can
affect multiple pools; a traveling shot resolves one impact. Arrow attachment
and bomb explosion handling retain their existing native paths.

Two additional original bounds checks needed correction. The ice rod rejected
shots spawned beyond the old screen; its creation view now centers on Link
until the real camera runs. The hookshot skipped collision beyond Link's old
area; its original check now uses the post-movement tip's area, with Link's
routing fields restored at the weapon boundary. AOT and interpreter paths use
the same hooks. Generator tests cover all width variants, actual hardware
returns, yield exclusion, idempotence and stale instrumentation detection.

Muted native-input gates, with explicit equipment/position/facing assists:

- `native-family-v2-boomerang`: red boomerang stuns the neighboring guard.
  The earlier blue-boomerang setup missed the retreating target; it is not a
  passing impact check.
- `native-family-v3-icerod`: the shot spawns across the old screen edge and
  the target enters native state6/health254 before disappearing. A later guard
  probe reuses that slot; it is not the original target surviving the hit.
- `native-family-v5-hookshot`: neighboring guard enters state11, weapon-hit31
  and stun226; the chain retracts.
- `native-family-v5-beam`: native weapon-hit12 and health6->4.
- `native-family-v5-somaria`: normal Y-button block creation/burst, native
  weapon-hit1 and health6->4.
- `native-family-v5-firerod`: native weapon-hit2 and fire-death state7.

Each gate passes a60-frame byte-exact replay with zero scoped failures. Raw
results, per-build identities and capacity receipts are retained in the task
evidence. `native-family-extra-state` also validates a fire impact in area27's
additional actor pool, current-build mid-flight save/load, two40-frame exact
replays, and exact success/forced-abort guest-scope restoration. This QA job
caps its own process tree to one CPU to fit available shared-host capacity;
its frame costs are not a performance claim.

Current record layout remains v10. Global spells, dungeon projectile
travel, broader enemy/item variants and the remaining progression/effect work
are not claimed complete. No release has been published.

## Roaming actor checkpoint (2026-10-28)

Area activation now considers resident actors' current coordinates, including
additional pools, so walking away from an original area does not freeze an
actor near Link. Actors retain their native slots and parent/projectile
references. Each outdoor actor uses a prepared stitched terrain window at its
own position. Both Map16 offsets and the separate native sprite collision
bounds follow that window; the canonical area context is restored afterward.

Private actor ticks stage terrain writes across windows and publish them only
after the restoring guest scope succeeds. They save a canonical home-area
buffer instead of accidentally capturing the last roaming actor's terrain.
The journal is bounded and transient; pointer-free state layout remains v10.

`roamer-activation-before2` reproduced a physical guard frozen near Link while
its home area18 was outside the activation halo. `roamer-activation-final`
uses the same explicit relocation assist, then native AI walks the guard from
y1544 to1524 across the21/29 seam. The guard continues ticking, remains visible
and passes a40-frame byte-exact replay with zero scoped failures. A subsequent
native fence approach produces wall-collision2, turns the guard around and
never crosses the fence (minimum x2629). Initial placement/facing are assisted;
movement, collision, turning and result flags are not forced. This does not
claim that a guard naturally walked the entire distance from its original home.

The two-window terrain probe verifies successful publication, forced-budget
abort discarding both writes, exact full-state restoration after fixture
cleanup and unchanged state generation. ROM-free tests cover all65536 world
tile indices, repeated writes, realm/range rejection and discard. Coordinate,
dungeon topology and generated-hook tests also pass.

`roamer-weapon-extra-state` retains a real fire-rod impact in an additional
actor pool, current-build mid-flight save/load, two40-frame exact replays and
exact guest-scope success/abort restoration. QA is muted and CPU-capped on
SERVER; its frame costs are not performance measurements. Broader actor and
terrain variants remain unqualified. NPC/secret progression, global spells,
dungeon projectile/effect coverage and travel/release checks remain open.

## Native interaction and medallion checkpoint (2026-10-28)

NPC message and item-receipt entries identify interactive native actors.
Private ticks defer quest-changing transactions; a nearby interactive pool
is paged into the live native machine to run its original dialogue, choices,
payment and receipt. Whole-pool swaps preserve native slot references and
remap attached arrows. Actor adoption now copies actor fields precisely,
preserving Link's previous position and global history/control fields.
The earlier broad slab copy could teleport Link when dialogue restored FC2/FC4.

`native-quest-vendor-boundary-v3` exercises a real Bottle Vendor transaction
with Link geographically in area34 and the vendor resident in24. Native
dialogue, default Yes choice, rupee goal150->50, bottle receipt and persistent
purchase flag pass without forcing result flags. Vendor relocation, starting
rupees/inventory and Link placement are explicit assists.
`native-quest-window-final-v2` collects the authored Mushroom from additional
pool1, opens its original dialogue, retains the512x288 world and anchored HUD,
and passes60/40-frame byte-exact replays, current-build disk save/load and
exact scope success/abort restoration. The textbox has its original opaque
black backing rather than showing terrain through native transparent BG3.

Bombos, Ether and Quake replay their original damage routine against other
active pools. Casting, magic cost, timing and player control run once live;
private actors receive the native FC1 freeze. `native-medallion-qualified-v2-*`
passes100-frame byte-exact impact replay for all three, with stationary remote
guards during casting and normal freeze release. Bombos queues native burn253,
Ether queues freeze254, and Quake transforms the guard into native type143.
The initial Bombos assertion missed pending damage during the native freeze;
it was corrected to observe the native queue and cast completion.

State identity/schema is now `alttp-world-v11`, including interactive actor
tags. Three regeneration-hook tests pass. Tested SERVER executable SHA256:
`a77f68fd67caab3d3f5d605c0b32df7a9eb46a002310afcaf3674ba175e614b1`.
Authored secrets, complete spell palette/rumble presentation, dungeon projectile
terrain/lighting and focused travel/release qualification remain open.

## Authored secret lookup and global flash checkpoint (2026-10-28)

Secret lookups translate stitched collision-window positions to the canonical
ROM area's Map16 index, then restore the caller's original index at the actual
native return. The caller still writes the correct world tile. Traveling
weapons temporarily select their physical native area for the original
bomb-wall/rock event-flag caller, restoring Link's area before actors run.
Scope failure restores transient lookup context as well as native state.

`authored-secret-dash-ability` derives area19/position1286/data132 from the ROM
table, starts its event bit clear, and uses a native boots dash against the
authored rock. The original code sets event00->20, reports secretFF and
changes terrain0226->0918. Thirty frames replay byte-exactly, failures0.
Position, equipment and the separate dash ability are explicit prerequisites;
the result flag is never set by the fixture. Earlier lift/dash probes lacked
the correct action or native ability and are retained as failed setup probes.

Global background flashes are derived from the actual native PPU palette's
original saturated+14 transform, so frozen redraw and restored states need no
extra clock or external flag. Area caches retain unfiltered colors and each
visible raster applies that same native flash. `native-ether-world-flash`
compares an unchanged neighboring-area patch: mean channel brightness92.506
normally and206.206 during two observed native flashes, failures0. Four
regeneration-hook tests pass, including secret cleanup at RTS rather than yield.
Tested SERVER binary14ad675dd2931a8f231eba128e5b2966e7b468682ceb938d276b63113f713b9c.
Peg/hardcoded puzzle updates, dungeon projectile terrain/lighting and remaining
travel/release gates are still open.
