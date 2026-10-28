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

Preserve stock/adaptive rendering, MSU audio and native SRAM. Current-build
states/rewind/rollback must work; old build states have no compatibility
promise. No netplay qualification is implied.

## Burndown

| Milestone | Status | Evidence / next gate |
|---|---|---|
| 0. Frozen baseline | Built | Game `8eee8b6`, engine `68896b6`, UI `eaf99ca`; GCC 15.2 MinGW, SDL3, trace on. Baseline executable SHA256 `0d765132d7ce3c055e22a95ad51ed85774ec7d708e674dcfa668252b8e7b4d5d`. Verified identical generation inputs. |
| 1. Isolated native calls | Initial gates passed | Success and forced interrupt-context/budget abort restore the serialized guest exactly, keep state generation, and leave bridge/host stack idle. Native decoder round trip passed. Main-thread-only entry; queued TCP mutations. Broader portability/side-effect qualification pending. |
| 2. Real overworld atlas | Partial | All 40 canonical areas in each Light/Dark World decode into separate 4096x4096 maps. Native tile/gfx/palette loaders, cached Map16 rasters. Layer priority, overlays, progression variants and native visual equality still open. |
| 3. Adaptive presentation | Partial | Independent width/height canvas, mouse zoom 1..8, consumed F4 atlas shortcut, native OAM projected into world space and centered HUD. 512x288 follow, 4096x4128 atlas and paused 300x500 portrait captures retained. Paused redraw keeps the native image frozen; frame counter and sampled RAM stay unchanged, then host resume advances again. Menu/effect and complete layer composition pending. |
| 4. Live overworld preview | Initial gates passed | Native Sprite_Main ticks visible areas plus 512px halo, or all current-world areas in atlas. Additional native pools activate 281/281 authored Light World and 275/275 Dark World spawns in the tested progression phase. Inactive areas retained. Native E/W/S/N crossings stay module 9/submodule 0 with continuous coordinates. Other progression phases and all mechanics remain unqualified. |
| 5. Cross-area interactions | Partial | Native sword hit/parry, enemy-to-Link damage and ordinary pickup reward publication demonstrated across an edge. Global ancillas survive a handoff. Full projectile collisions/lifetime, enemy migration, authored secret indices, puzzle/NPC progression and inter-pool interactions remain open. |
| 6. Connected dungeon floors | Pending | Content-derived membership, floors/layers, doors, keys/shutters/puzzles. |
| 7. Presentation and state | Partial | Pointer-free native room records, OAM pieces, terrain changes and execution residue serialized with strict preflight and feature identity. 30 idle frames and 70 moving frames across area 18→19 replay byte-identically. Menu/dialogue/effects/death/Continue/portal qualification remains open. |
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
18→19 east, 18→17 west, 21→29 south, 29→21 north. The setup places Link on a
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

`profiles/seam-sword-protected/result.json` demonstrates the neighboring
guard's health changing 6 to 4 while Link stays in area 18 and the guard stays
across its eastern edge. Player protection is an explicit test assist.
`profiles/native-seam-damage/result.json` demonstrates native damage/recoil
reaching Link across the same edge. The ordinary pickup probe increased bombs
0 to 8; it explicitly bypassed the authored item's hidden-under-a-rock
prerequisite, so lifting/destructible terrain remains unverified.

`profiles/viewport-input-qualification/commands.jsonl` records actual process
SDL F4 and wheel events. F4 toggles the atlas without loading a deliberately
different save slot 4; zoom changes 3 to 2 and back. `profiles/paused-portrait-v3/result.json`
records the actual 900x1500 window producing a 300x500 canvas, the same native
frame/RAM while paused, and resumed frame progress. The test helper schedules
the existing host pause command on the main loop; physical controls remain an
owner handoff check. Native raster/IRQ rendering is not repeated while paused.

Cross-area projectile collision/lifetime, enemy migration, authored secret
indices, NPC receipts and other one-time progression remain incomplete gates.
Native object references and enemy-to-enemy interactions across separate pools
are not qualified. Dungeon floors still use the normal renderer.

Initial full-atlas samples were about 30.9ms native simulation plus 7.3ms
composition, before complete interactions and without matched host isolation.
This is not a 60Hz result or a qualified performance comparison. Retain raw
samples and measure the completed feature before choosing an optimization.

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
