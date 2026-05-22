# Zelda ALttP Recomp — Known Issues

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
