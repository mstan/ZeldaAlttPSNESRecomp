# Zelda ALttP Recomp — Known Issues

## RESOLVED: Link movement/collision/animation broken (entry_s_offset bug)

**Status:** RESOLVED 2026-05-29. Link moves correctly, respects collision, and animates.
Verified post-fix: `$006D` and `$006E` now written every frame, `$0030` nonzero during
movement, `step` command works without timeout.

**Symptom:** Link's position at `$7E:0020`/`$0022` updates when a D-pad direction is
held, but:
- `$7E:006D` (movement-occurred flag) is **never written** — always 0
- `$7E:006E` (facing direction) is **never written** — always 0
- `$7E:0072` (animation index) stays 0 — `LinkOam_Main_M1X1` writes it as 0 every frame
- Link does not respect wall/stair collision
- Link's sprite is garbled/stuck in the wrong animation state

`$7E:0030` (move magnitude) gets values `0xFE`/`0xFF` during movement (correct velocity
computed), and position does change (velocity path runs), but the full collision+animation
path never fires.

**Evidence packet:** `debug_runs/20260529_194524/` — WRAM write traces for all key
addresses over ~30 movement frames. Key findings from `wram_writes_at`:
- `$006E`: 0 writes (idle and movement)
- `$006D`: 0 writes (idle and movement)
- `$0020`/`$0022`: written by `Link_HandleVelocity_M1X1` → `Link_HandleVelocityAndSandDrag_M1X1` ✓
- `$0030`: written by `Link_HandleVelocityAndSandDrag_M1X1` with correct velocity ✓
- `$00F2` (joypad P1): written by `NMI_ReadJoypads_M1X1` (joypad path is live) ✓

**Root cause (confirmed):** `Link_HandleVelocityAndSandDrag_M1X1` (PC `$07:E3E0`) is
tail-called from `Link_HandleVelocity_M1X1` (PC `$07:E245`), which begins with a
PHB/PHK/PLB preamble (net `cpu->S -= 1`). `Link_HandleVelocityAndSandDrag_M1X1` ends
with a matching PLB that pops the PHB byte before its RTL. This leaves `_ret_s`
(recorded before RTL pops) equal to `_entry_s` of `Link_HandleVelocity_M1X1`, which the
runtime's `cpu_resolve_ancestor_skip` interprets as a return-to-ancestor and fires an
NLR.

The NLR propagates back through `Link_HandleVelocity_M1X1` and out of
`PlayerHandler_00_Ground_3_M1X1`, skipping the `goto L_82C2` that would have reached:

1. **`Link_HandleCardinalCollision_M1X1`** (`$07:B7C7`) — first instruction writes
   `$006E = 0`; calls `Link_HandleDiagonalKickback_M1X1` which sets `$006D` and calls
   `TileDetection_Execute_M1X1` (`$07:D9D8`).
2. **`Link_HandleMovingAnimation_FullLongEntry_M1X1`** — updates animation index and
   facing direction.

Both are skipped every frame, explaining all observed symptoms simultaneously.

**Fix (pending regen+rebuild):**

*Recompiler change* — new `entry_s_offset` cfg directive. When non-zero, the emitted
`_entry_s` is `cpu->S + N` instead of `cpu->S`. This lets the RTL host-return check
(`_hrv && _ret_s == _entry_s`) pass correctly for functions entered with a caller-managed
stack imbalance.

Files changed:
- `recomp/bank07.cfg`: `Link_HandleVelocityAndSandDrag` line gets `entry_s_offset:1`
- `snesrecomp/recompiler/v2/emit_bank.py`: add `entry_s_offset: int = 0` to `BankEntry`
- `snesrecomp/recompiler/v2/cfg_loader.py`: parse `entry_s_offset:<n>` from func lines
- `snesrecomp/recompiler/v2/emit_function.py`: emit `_entry_s = (uint16)(cpu->S + Nu)`
  when offset != 0

After this fix:
- `_entry_s_SandDrag = cpu->S + 1` at function entry (= `S_jsl - 3`)
- After PLB epilogue: `_ret_s = S_jsl - 3 = _entry_s_SandDrag`
- `_hrv && _ret_s == _entry_s` → TRUE → clean host-return → RECOMP_RETURN_NORMAL
- `PlayerHandler_00_Ground_3_M1X1` reaches `L_82C2` → calls `Link_HandleCardinalCollision_M1X1`

**Regen status:** Full regen running as of this writing (all banks; required because the
partial `--banks 07` regen interacts with the variant-prune trimmer and drops M0X0/M0X1
variants for functions immediately after the changed boundary). Dispatch table must be
regenerated in the same pass.

**Expected stop conditions after rebuild:**
- `$006E` gets non-zero writes during movement (first write = `Link_HandleCardinalCollision_M1X1` entry)
- `$006D` gets non-zero writes when moving
- `TileDetection_Execute_M1X1` at `$07:D9D8` executes
- Link respects wall/stair collision
- Link's sprite animation is no longer stuck

**Debug tooling created this session:**
- `debug_harness.py` — autonomous TCP debug harness (launch, connect, load states,
  drive controller, capture WRAM timeseries + write traces, screenshot). Run:
  `python debug_harness.py --phase all`
- `build_oracle.bat` — convenience wrapper for Oracle|x64 msbuild rebuild
- **Debug server fixes also applied:**
  - `cmd_step` held the global mutex during its entire wait loop, deadlocking the main
    thread after 1 frame (main thread decremented `s_step_remaining` then blocked on the
    mutex `debug_server_record_frame` needs). Fixed: unlock before wait, lock after.
  - `Sleep(0)` in the Windows step-wait branch ran 150 000 iterations in <1 ms, always
    timing out. Fixed: `Sleep(1)` with 5 000-iteration cap (~5 s).
  - Welcome banner (`{"connected":true,...}`) on TCP accept was consuming the first user
    command's response slot, shifting all responses by 1. Fixed in harness: banner is
    drained at connect time before any commands are sent.

---

## OPEN: Overworld BG garble + Start menu + item HUD broken (M/X drift)

**Status:** OPEN. Root cause traced to `Overworld_LoadOverlays2_M1X1`. Last updated 2026-05-30.

**Current symptom set (post Link-fix):**

1. **Overworld BG garble** — stepping onto the overworld renders horizontal
   red/orange stripe garbage across the entire background.

2. **Start button broken** — pressing Start from the overworld does not open the
   pause menu correctly.

3. **Item HUD not updating** — after Link receives the Lantern, it does not appear
   in the item HUD.

**Root cause (traced further, 2026-05-30):**

The garble is from **wrong tile data in the DMA source buffer `$7E:C700`**. NMI
reads `$7E:0219` as the VRAM word destination (→ `$2116`), then DMA-copies from
`$7E:C700` (source bank $7E, address $C700) to VRAM. The recomp writes the wrong
pixel bytes, causing the horizontal stripe pattern.

Confirmed: oracle writes `0xFB` to VRAM byte `0x8200` (BG1 tile area), recomp writes
`0xEF` — same VRAM destination, different data. So the VRAM address is not the problem;
the SOURCE DATA is wrong.

`$7E:0219` (VRAM word dest = 0x6040): hardcoded by bank $0C init code (`LDA #$6040 /
STA $0219`) — correct as-is. Both recomp and oracle use this value.

`$7E:C700`: set via DMA (bypasses CPU WRAM trace) early in the game session (frame ~172
after save state loads). The save states themselves contain the corrupted tile data —
the corruption was introduced during the ORIGINAL game initialization (intro sequence)
before any save states were created.

**Save state corruption chain:**
1. During game intro (before save states existed), some tile decompression function
   ran with M/X drift (X=0 when X=1 expected)
2. Wrong-width table lookups produced wrong pixel data in `$C700` area
3. NMI uploaded that wrong data to VRAM every frame
4. The saved game states preserved the wrong `$C700` values in WRAM

**pxwatch trips observed:**
- On overworld (with save state): first X=1→0 in `TileDetect_Movement_X_M1X1` — 
  collision detection, expected, has matching SEPs. Not the bug.
- `NMI_PrepareSprites_M1X1` REP #$31 with matching SEP #$10 + SEP #$20 — also expected.
- **Real bug site:** unknown, in the intro/init sequence. Must trace from FRESH BOOT.

**Key investigation blocker:** `$C700` is written via DMA (not CPU writes), so the
always-on WRAM trace cannot capture it. The DMA write bypasses `cpu_write8/16` hooks.

**`Overworld_LoadOverlays2_M1X1` note:** `REP #$30` at `$02:AF1E` IS correct per ROM
bytes (0xC2 0x30). The `$008C = 0x9F` value it writes is the correct value for area 
0x1B via the dispatch table. NOT the root cause of the garble.

**Next steps:**
1. Run full fresh boot (title → save file select → intro → overworld) and trace pxwatch
   during the intro to find the FIRST problematic M/X trip that produces wrong tile data
2. Add DMA write tracing to `common_rtl.c` / `snes9x_bridge.cpp` to capture $C700 writes
3. Once the decompressor-with-drift is found, add `exit_mx` cfg hint to fix it

**Decompression functions of interest (bank 00):**
- `DecompressAnimatedOverworldTiles_M1X1` at `$00:D394` — has PHB/PHK/PLB + PHY preamble
  then REP #$30, multiple JSR calls with NLR propagation paths. This pattern can cause
  M/X drift leakage if any callee returns NLR at M0X0 state.
- `bank_00_D5CE_M0X0` — called from DecompressAnimatedOverworldTiles, likely the inner
  decompressor loop writing pixel data to WRAM

**Symptom (original):** New game intro renders fine (title, story text, Link asleep
in bed, Link's HOUSE interior BG). The instant Link MOVES, his sprite broke and the
OVERWORLD background rendered as garbage stripes.
**Link-sprite is now fixed** (entry_s_offset:1 cfg hint, 2026-05-29). Overworld BG
garble and Start-button malfunction remain open per the new symptom set above.

**Oracle:** the in-process snes9x backend runs alongside the recomp every frame
and records WRAM/VRAM/insn traces. Use `wram_writes_at`, `get_oracle_vram_trace`,
and `vram_write_diff` to compare recomp vs snes9x output directly.

**Repro (exact):** boot zelda.exe → title (~frame 800) → `set_controller Start`
(PLAYER SELECT; save "1.LINK" exists) → `Start` (loads new-game intro) → tap `A`
~18× to clear the Agahnim telepathy text until the screen brightens (Link in bed,
house renders fine) → `set_controller Right` to walk Link out of bed → garble.

**Root cause (CLASS confirmed):** M/X flag DRIFT → wrong-width execution, exposed
by the MMX-era `bf8a34b` change ("fall-through tail-calls dispatch callee m/x by
RUNTIME flags, not static"). With that policy, the live `x`/`m` flags at every call
must match hardware. The live `x` drifts to 0 deep in the move's call chain
(`pxwatch_get` shows `REP` x=1→0 flips), so a GFX-load runs 16-bit when it should be
8-bit and mis-indexes → garbage tiles; the documented instance is
`UploadGraphicsFiles_Layer3` `TAY` at `$00:A9A5` writing `$7E` instead of `$0B` to
`$7E:008C`. Link's sprite is the SAME root: the LinkOam dispatch is reached at the
drifted `x=0`. During the move the runtime actively runs `Decompress_M1X1` (overworld
GFX decompress) repeatedly.

**Dead ends — do NOT redo:**
- Unresolved-dispatch TRAP COUNT is a RED HERRING. Drove it 141→7 (variant prune)
  and the garble is UNCHANGED. During the garble all tripwires are CLEAN
  (`phantom_trap_get`/`unresolved_stub_get`/`offrails_get` = 0); the game is running
  (frame advancing, frame-level `m=1 x=1`, `cpu->S` clean 0x01FF). It is SILENT
  wrong-output, not a dispatch trap. Restoring indirect-dispatch auto-recovery is NOT
  the fix.
- PHP/PLP-balanced flag-preserving classification is ALREADY implemented (snesrecomp
  `73e3d26`; see `exit_mx_autoroute.py` header). The stale `zelda_00.c` comment about
  "PHP/PLP not classified" is OBSOLETE. The residual drift has a subtler cause.
- Do NOT revert `bf8a34b` (it's the MMX guard). Fix the flags that FEED it.
- Do not rely on a pinned external "known-good" build — use the in-process snes9x
  oracle backend instead (always-on WRAM/VRAM traces, `vram_write_diff`, etc.).

**Work done this session (2026-05-29):** the variant prune in snesrecomp
`tools/v2_regen.py` (emit-truth marker + default-(1,1) canonical + NEW reference-taint
prune for dangling caller clones + dispatch-table-subtract-pruned fix; +14-assert test
`tests/v2/test_prune_unresolved_indirect_goto.py`) makes ALttP LINK (fixed a real
`LinkOam_Main_M1X0` LNK2019) and is cross-game-safe (SMW guard passed; MMX guard caught
a 316-dangling dispatch-table bug now fixed). UNCOMMITTED. It does NOT fix the garble.

**Fix direction / next step:** find the first M/X divergence using the in-process
snes9x oracle. Drive the repro, then query `wram_writes_at 8C` on the recomp side
and compare against oracle WRAM/VRAM traces. The first write where recomp and oracle
disagree on the value or writer identifies the drifted call site. Fix with an
`exit_mx` cfg hint on the callee that exits with wrong flags (preferred, general) or
a glue-level force in `zelda_00.c` as fallback.

## RESOLVED: Camera axis-swap on sword-hit against Sprite_4B (Green Knife Guard)

**Status:** Fixed in snesrecomp@7ef1f59 (`v2: NLR-detector tolerates paired
PHA/PLA in setup region`). Resolved 2026-05-21.

**Root cause (confirmed):** `$05:F971` ends with the NLR idiom
`PLA STA $0D50,X / PLA STA $0D40,X / PLA PLA / RTS` (return-to-grandparent
via `SKIP_1`), paired with 2 `PHA`s earlier in `$05:F97A`. The v2
NLR-detector's per-block "setup region has no Push/Pull" check rejected
this pattern because the two paired `PLA/STA` restores live in the same
block as the trailing NLR pulls. Without NLR recognition, `F971` popped
2 bytes from its caller's frame on return — and inside the
`Module07_Dungeon` `L_87E5..L_8842` sandwich around `Sprite_Main`, those
caller-frame bytes were the saved BG scroll values, so `L_8842`'s PLAs
wrote garbage to `$7E:00E0-E9`.

**Fix:** Relaxed the detector — setup region may now contain Push/Pull
ops; the requirement is `function-wide PLAs - PHAs == trailing pull_count`
instead. The existing `F94E` case (`PLA PLA RTS` with no preceding PHAs)
still passes (`0 - 0 = 0`, setup-region check skipped).

**Why only Sprite_4B + sword contact triggered it:** That combination
uniquely entered the `Sprite_CheckDamageFromLink` chain that called
`F971` with the broken trailing-PLA pattern. Other damage paths happened
to avoid this function or balanced via different idioms.

The original analysis below is kept for context.

### Symptom

In dungeon module $07, when Link's sword contacts a Green Knife Guard
(sprite type $4B), exactly **one frame later** the background scroll
registers shift in a coherent "H↔V axis swap" pattern. Visually the
camera appears to teleport — typically up by ~one room — but Link's
logical state is unchanged: room ID, position, module/sub-module all
remain identical. Walking towards what appears to be the door at the
bottom of the visible screen takes him up the stairs (proving he is
still upstairs, only the view is wrong).

Reproduced six+ times with bit-identical signature.

### Mechanism (verified)

The BG scroll quad lives at `$7E:00E0` (BG1 H), `$7E:00E2` (BG2 H),
`$7E:00E6` (BG1 V), `$7E:00E8` (BG2 V). `Module07_Dungeon_M1X1` at
`$02:87A2` runs an inline sequence around its `Sprite_Main` call that
looks like this:

1. `L_87E5`: 4 × PHA (save old `$E2`, `$E8`, `$E0`, `$E6` to the 65816
   stack), then per-frame scroll-target updates (each += `$011A` or
   `$011C` delta).
2. Conditionally `L_881E`: pull two of those pushed values, recompute
   `$E0`/`$E6` from `$0422`/`$0424` rebase offsets, push the new
   values back. **Gated on `$0428 & 0xFF != 0`** — confirmed NOT
   running during the bug (`$0428` stays zero in every reproduction).
3. `L_8838` → `L_883E`: call `Sprite_Dungeon_DrawAllPushBlocks` and
   then `Sprite_Main_M1X1` (PB=`$06`).
4. `L_8842`: 4 × PLA in reverse-push order, write the popped values
   straight to `$E6`, `$E0`, `$E8`, `$E2`.

The 4 PLAs in step 4 are intended to restore the saved scroll values
(or, on `L_881E` path, restore + propagate the rebased values). They
are unconditional — whatever is on the stack gets written to scroll.

When `Sprite_Main` is called with the active sprite being Sprite_4B
and Link's sword overlapping its hitbox, the damage chain runs:

```
Sprite_4B_GreenKnifeGuard
  → Sprite_CheckDamageToAndFromLink
    → bank_06_F2AA → Sprite_CheckDamageFromLink
      → Player_SetupActionHitBox        ← suspect (static imbalance)
      → Sprite_SetupHitBox
      → CheckIfHitBoxesOverlap
      → Sprite_AttemptZapDamage
        → Sprite_ProjectSpeedTowardsLink → Sprite_IsBelowLink / Sprite_IsRightOfLink
        → Sprite_CalculateSwordDamage → Sprite_ApplyCalculatedDamage
          → Sprite_GiveDamage → Sprite_CalculateSfxPan → CalculateSfxPan
```

**One of those functions leaves the 65816 stack imbalanced** —
pushes or pops one or more words more than its asm origin intended.
On return to `Module07_Dungeon`, `cpu->S` is shifted, so `L_8842`'s
PLAs grab the wrong values and write them to the scroll bytes.

### Empirical bug-frame signature

Captured from `cpu_write16` stderr instrumentation:

| Address | Before     | After      | Source           |
|---------|------------|------------|------------------|
| `$E6`   | `0b00`     | `0a09`     | Module07_Dungeon |
| `$E0`   | `0a09`     | `0b00`     | Module07_Dungeon |
| `$E8`   | `0b00`     | `0a09`     | Module07_Dungeon |
| `$E2`   | `0a09`     | `0000`     | Module07_Dungeon |

Pattern: top two PLAs (→ `$E6`, `$E0`) match the values that *would*
have been pushed back by `L_881E` (`new_E6 = $0424 + $E8`,
`new_E0 = $0422 + $E2`) — except `L_881E` didn't run this frame
(`$0428 = 0`), so those values are leftover from the *previous*
frame's `L_881E` push. Bottom two PLAs (→ `$E8`, `$E2`) write
`OLD $E2` and `0` (random RAM under the stack) — these are the
slots where Section 2 and Section 1's PHA'd values *should* have
been.

### Reproduction

Reliable. Save state in dungeon room `$0055` with Green Knife Guards
present. Walk to a Knife Guard and swing the sword — the camera
shifts within ~1 frame of contact.

### Root cause hypothesis (to verify)

Static analysis of the damage-chain function bodies for PHA/PLA
balance (counting all branches together) flagged two candidates:

- `Player_SetupActionHitBox_M1X1` at `$06:F5E0` — 1 PHX, 3 PLX across
  branches. Per execution one PLX should run, but a branch path may
  miss the PLX or hit it twice.
- `bank_05_F971_M1X1` — 2 push markers, 4 pull markers (also
  branch-coverage; need dynamic confirmation).

Both are in the damage path that runs uniquely on Sprite_4B contact.
Other damage-chain functions are statically balanced.

### Fix plan

1. Add dynamic `cpu->S` delta instrumentation at every
   `RecompStackPush` / `RecompStackPop` for the damage-chain
   functions. Reproduce once — the function with non-zero delta is
   the culprit.
2. Compare its recomp'd C against the original 65816 ASM (zelda3
   disassembly) at the matching PC. Identify the missing or extra
   PHA / PLA / PHX / PLX in the recomp emit.
3. Fix the **recompiler decoder** in `recompiler/v2/`. Never edit
   the generated `src/gen/*_v2.c`. Regen + rebuild + verify the bug
   is gone.

### Dead ends — do not redo

- `$0428` (axis-swap gate) stays zero during the bug. `L_881E` does
  NOT run on the bug frame. The bug is purely in `L_8842`'s PLAs.
- `$011A` / `$011C` (scroll deltas) stay zero in all reproductions.
- `$006C`, `$0011` (sub-module), `$0F32`, `$002D`, `$0047`, `$012F`
  are not triggers.
- The `pause` debug-server command was misused early on as a
  "freeze the ring to query" mechanism. It has since been
  policy-disabled — see the global ring-buffer rule.

### Instrumentation in place

These are useful for any future state-tracking and should stay until
the fix is verified, then be removed:

- `cpu_state.c::cpu_write8` / `cpu_write16`: stderr `[w8]/[w16]`
  lines for writes to `$E0-E9`, `$11`, `$6C`, `$11A-$124`,
  `$420-$428`.
- `debug_server.c::debug_on_wram_write_byte` / `_word`: stderr
  `[indir8]/[indir16]` lines for the same address set (caught nothing
  in this build — IndirWrite path is not emitted with hooks for
  Oracle config).
- Always-on WRAM-write watches on `$7E:00E2/E3/E8/E9` installed in
  `cpu_state_init` (slots 31-34 of `g_wram_watches`).
- `CALL_TRACE_LOG_SIZE` bumped from 65 536 to 1 048 576 entries
  (~140 s of call history at current rate).
- `CPU_WRAM_WATCH_MAX` bumped from 32 to 128.
- `s_call_trace.active` defaults to 1 (always-on call ring).
- `cmd_pause` is a no-op that returns a policy-error JSON.

All of the above are intentional always-on observability extensions
per the project's ring-buffer-is-principal-observability rule. The
`fprintf` instrumentation in `cpu_write8/16` is a temporary
debugging aid and should be removed once the bug is fixed.
