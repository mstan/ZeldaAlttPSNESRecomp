# Without-disasm experiment log

Goal: see how far we can get with **zero references to any disasm
or decomp**. Pure framework auto-discovery from the vector table.
Pivot to the zelda3 decomp the moment we're truly blocked.

Session start: 2026-05-16

## Initial state

- Empty cfg: `bank = 0` + `auto_vectors` (one directive).
- ROM: ALttP USA [h1C] hack variant, CRC32 `0x8137C34D`, 1MB LoROM.
  Vectors: RESET=$8000, NMI=$80C9, IRQ=$82D8.
- Zero hand-named functions.

## Regen result

```
auto_vectors: bank $00.cfg seeded I_RESET=$8000, I_NMI=$80C9, I_IRQ=$82D8

Auto-detecting JSL dispatch helpers...
  detected 0 short + 0 long dispatch helpers (scanned 1 JSL/JML targets)

Auto-detecting leaf-function exit-(M, X) mutations...
  detected 6 leaf-function exit-(M, X) mutation(s); auto-routed:
    $00:80C9 'I_NMI' entry M=0 X=1 -> exit M=0 X=0
    $00:80C9 'I_NMI' entry M=1 X=0 -> exit M=0 X=0
    $00:80C9 'I_NMI' entry M=1 X=1 -> exit M=0 X=0
    $00:82D8 'I_IRQ' entry M=0 X=1 -> exit M=0 X=0
    $00:82D8 'I_IRQ' entry M=1 X=0 -> exit M=0 X=0
    $00:82D8 'I_IRQ' entry M=1 X=1 -> exit M=0 X=0

  OK    bank $00: 3 entries -> src\gen\zelda_00_v2.c
  auto-promote pass 1: added 8 entries (calls=8); re-emitting
  auto-promote pass 2: added 2 entries (calls=10); re-emitting

v2_regen: 1/1 banks emitted
```

**13 functions discovered statically** from 3 vector seeds, via
reachability tracing. Per-variant exit-MX inference recorded 6
auto-routes covering 2 of those 13. No dispatch helpers detected
(zero JSL targets matched the canonical PLA+ASL+TAY+JMP shape —
either ALttP doesn't use that shape, or we haven't reached one yet
through the static walk). Only bank 0 has reachable code from the
vectors alone.

Function names: `I_RESET_M1X1`, `I_NMI_M1X1`, `I_IRQ_M1X1`,
`bank_00_83D1_M1X1`, `bank_00_80B5_M1X1`, ..., `bank_00_92A1_M1X1`.
Anonymous pattern — zero semantic info.

## Build progress (without disasm)

Iterations to get a linking binary:

1. v2_regen output filename hardcoded `smw_`. Fixed in framework
   (added `--prefix` CLI arg).
2. vcxproj listed SMW's 9 bank files. Stripped to just
   `gen/zelda_00_v2.c`.
3. `snes9x_core.vcxproj` and `packages.config` missing — copied from
   SMW. NuGet `packages/` shared via Windows junction.
4. `third_party/gl_core` and `src/platform/win32` missing — copied
   from SMW.
5. `variables.h` missing — wrote a minimal stub declaring
   `counter_global_frames`, `waiting_for_vblank`, `mirror_hdmaenable`.
6. Scaffold rename script missed `SmwRunOneFrameOfGame_Internal`
   (only matched the `Smw` prefix, not the `_Internal` suffix).
   Renamed manually in zelda_00.c, zelda_rtl.c, zelda_rtl.h.
7. SMW's main-loop entry `InitAndMainLoop_ProcessGameMode` doesn't
   exist for Zelda. Stubbed `ZeldaRunOneFrameOfGame_Internal` to a
   no-op for first link-pass.
8. Link succeeds. 3.3MB binary at `build/bin-x64-Oracle/zelda.exe`.

Window title was still "SMW" — fixed (now "Zelda LttP").
Debug server port collided with SMW on 4377 — moved Zelda to 4378.

## First boot — partial success, hit the wall

zelda.exe launches. Window opens. Framework brings up its WRAM
watches, M/X claim verifier, async-write tripwire, off-rails
detector — all clean. CPU is advancing (px_mutation_count = 756k
after ~5 seconds; the recompiled code IS executing). But:

- **Frame counter only reaches 6** after several seconds. SMW would
  be at frame ~200.
- **Black screen** — PPU has nothing to draw.
- **No tripwires fire** — the runtime isn't doing anything
  observably wrong, just spinning.

### Diagnosis

This is the same shape as SMW's `$00:806B` main-loop spinlock —
`LDA $10 ; BEQ -3 ; ...`. The recompiled I_RESET runs init, falls
into the spinlock, and spins forever waiting for the NMI flag to
flip. The framework's per-frame `I_NMI(&g_cpu)` call IS being made
(post-frame-0), and the NMI handler does write the flag, but the
recompiled spinlock body re-reads the flag in tight loop without
the framework's host-orchestration replacement that SMW uses via
`exclude_range 806B 8078` + `ZeldaRunOneFrameOfGame_Internal`.

The watchdog (`WatchdogCheck()` in every emitted block) is
presumably letting `I_RESET` longjmp out periodically, which is
why frame counter advances at all — but each frame is just one
`I_RESET` → watchdog timeout → next frame.

### Path to next milestone (without disasm)

To get past the spinlock, we need to identify:

1. **The spinlock's PC range** (the `LDA $xx ; BEQ` pattern in
   bank 00). With a disasm this is "look up MainLoop_*". Without:
   read ROM bytes at the tail of I_RESET ($00:8000+) and look for
   the canonical 4-5 byte pattern `A5 xx F0 FC` (LDA dp ; BEQ -4) or
   similar.

2. **The main-loop tick function** (what gets called once per NMI
   to advance game state). With a disasm: it's typically the
   function whose first instruction is right after the spinlock's
   BEQ target. Without: identify by reading bytes near the spinlock.

### Decision point

This is the natural break to pivot to the zelda3 decomp for naming.
Continuing without disasm requires reverse-engineering ALttP's
main-loop shape by hand from ROM bytes, which is essentially
disassembly without the tooling.

## Pivot — zelda3 decomp pulled in

Cloned `https://github.com/snesrev/zelda3.git` at `F:/Projects/zelda3/`
fresh. The decomp uses inline `// XXxxxx` comments on each function
definition giving the ROM PC. With one grep we found:

- `void Module_MainRouting() { // 8080b5 }` — the per-frame game tick.
  Maps to `$00:80B5` (LoROM banks $80+ mirror $00+). The recompiler
  had already discovered this as `bank_00_80B5_M1X1` from the
  vector-seeded reachability walk.
- `static void ZeldaRunGameLoop() { frame_counter++; ClearOamBuffer();
   Module_MainRouting(); NMI_PrepareSprites(); nmi_boolean = 0; }` —
  the per-frame call order.

ROM bytes at `$00:8034-8060` decode to the canonical
`LDA $12 ; BEQ -4 ; CLI ; BRA ... ; JSR ClearOamBuffer ; JSL
Module_MainRouting ; JSR NMI_PrepareSprites ; STZ $12 ; BRA $8034`
spinlock — exact analog of SMW's `$00:806B-8078`. Same handling:
`exclude_range` + host-side orchestration in
`ZeldaRunOneFrameOfGame_Internal`.

**End of the without-disasm experiment.** Stopping the log here;
further progress documented in normal commit history. Findings:

- The framework auto-discovers function entries from vector seeds
  fine (13 functions in bank 0 with zero hand authoring).
- Compile + link work after about an hour of mechanical scaffold-
  rename + missing-file fixes.
- The runtime boots far enough that the framework's per-frame loop
  ticks; CPU executes; tripwires are quiet on the verified path.
- Black screen / stalled-frame-counter is hit exactly when the
  recompiler emits the asm main-loop spinlock as actual code.
- This wall is **not** something a sufficient-disasm-substitute
  (just reading ROM bytes) can't get past — it's the
  identifying-PC step that's tedious without tooling. With the
  decomp's PC-annotated function map, the pivot took ~5 minutes
  to identify and ~5 more to write the cfg fix.
