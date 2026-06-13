# MSU-1 patch — attribution & thanks

The MSU-1 audio support in this build does **not** originate with us. The
game-side driver — the code that detects the MSU-1 chip and streams music
in place of the SPC soundtrack — comes entirely from the work of others,
and we want to credit them plainly and gratefully.

## Authors

- **qwertymodo** — authored and maintains the A Link to the Past MSU-1
  patch we bundle here (`alttp_msu.ips`, with the annotated source in
  `alttp_msu.asm`). Project: <https://github.com/qwertymodo/MSU1-Zelda>
- **Conn** — wrote the original ALttP MSU-1 patch that qwertymodo
  disassembled, documented, and continued (see the header of
  `alttp_msu.asm`).

## License

`alttp_msu.ips` and `alttp_msu.asm` are distributed by qwertymodo under
the **MIT License** — see [`LICENSE`](LICENSE) (Copyright © 2014
qwertymodo). The MIT terms permit redistribution and use; we comply by
shipping the license verbatim alongside the files.

## A note of thanks (not required by the license, offered anyway)

We borrowed this implementation from someone else's repository. The MIT
license would let us do so silently, but that wouldn't be right. The MSU-1
experience in this recompiled build exists because qwertymodo and Conn did
the hard reverse-engineering and homebrew work years ago and shared it
freely. **Thank you.** If you enjoy the orchestrated music here, the
credit is theirs.

## What we do with it

We do **not** redistribute any Nintendo ROM data. An IPS patch is a diff
of *new* bytes (the homebrew driver, the hook instructions, and padding) —
it contains no original ROM content. Our regeneration step applies this
patch to *your own* legally-obtained, stock A Link to the Past (USA) ROM
locally, then recompiles from the result. You supply the ROM; we supply
only the freely-licensed patch and the recompiler.
