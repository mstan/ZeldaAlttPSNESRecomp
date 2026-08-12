# MSU-1 behavior - attribution and thanks

The MSU-1 audio behavior in this build does not originate with us. The classic
game-side driver, which detects the MSU-1 chip and streams music in place of the
SPC soundtrack, is homebrew work by others, and we credit them plainly.

## Which patch behavior we implement

The host-side Mods plugin implements qwertymodo's A Link to the Past MSU-1
behavior, based on the original Conn patch. The ROM is no longer patched during
regeneration. Stock ALttP remains the analysis input and runtime ROM. The
trusted static plugin observes ALttP music commands and drives the runner's
MSU-1 device directly.

- `alttp_msu.ips` is retained as legacy reference material. It injects the
  game-side MSU-1 driver and hooks into an expanded ROM.
- `alttp_msu.asm` is qwertymodo's annotated driver source used as the
  behavioral reference for the host-side plugin.

## Authors

- qwertymodo authored and maintains the A Link to the Past MSU-1 patch we
  bundle here as reference material. Project: <https://github.com/qwertymodo/MSU1-Zelda>
- Conn wrote the original ALttP MSU-1 patch that qwertymodo disassembled,
  documented, and continued.

## License / what we redistribute

`alttp_msu.ips` and `alttp_msu.asm` are distributed by qwertymodo under the MIT
License; see [`LICENSE`](LICENSE) (Copyright (c) 2014 qwertymodo). The active
regeneration pipeline no longer applies the IPS patch: `tools/regen.sh`
recompiles from your legally-obtained stock A Link to the Past (USA) ROM. The
recompiled build then runs on that stock ROM directly. No enabled MSU-1 Audio
mod means authentic SPC audio; a matching pack plus the MSU-1 Audio mod streams
music.

Thank you, qwertymodo, Conn, and the MSU-1 community. If you enjoy the streamed
music here, the credit is theirs.
