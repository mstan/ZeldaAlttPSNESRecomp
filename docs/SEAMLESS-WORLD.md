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
| 2. Real overworld atlas | Partial | All 40 canonical Light World areas decoded into a 4096x4096 map. Native tile/gfx/palette loaders, cached Map16 rasters. Layer priority, overlays, progression variants and native visual equality still open. |
| 3. Adaptive presentation | Partial | Independent width/height canvas, mouse zoom 1..8, F4 atlas, native OAM projected into world space and centered HUD. 512x288 follow and 4096x4128 atlas captures retained. Menu/effect/paused resize and complete layer composition pending. |
| 4. Live overworld preview | Initial gates passed | Native Sprite_Main ticks visible areas plus 512px halo, or all current-world areas in atlas. Inactive areas retained. Real-input E/W/S/N crossings stay module 9/submodule 0 with continuous coordinates and resident destination actors. Spawn completeness remains open. |
| 5. Cross-area interactions | Pending | Enemy/projectile migration, damage, items, terrain and one-time progression. |
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

The original 16-slot pool and proximity spawn selection still limit actors
within each native area. An initial atlas showed 264 live actors; that count
does not establish that every authored spawn is active. Neighbor ticks protect
Link from damage and isolate inventory/progression side effects. Cross-area
combat, projectile continuity, authored secret indices, and one-time rewards
therefore remain explicit incomplete gates. Dungeon floors still use the
normal renderer.

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
