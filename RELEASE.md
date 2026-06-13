# Zelda - A Link to the Past — macOS (Apple Silicon) build

Native arm64 macOS build of Zelda - A Link to the Past, attached to release **v0.4.0** as
`ZeldaAlttPSNESRecomp-macos-arm64.zip`.

## What this is
- The original game statically recompiled to native arm64 (no emulator core shipped).
- Self-contained `.app`: SDL2 bundled via `@executable_path`, ad-hoc codesigned.
- Verified by manual play on Apple Silicon (looks/sounds correct on the golden path).

## Status
First macOS (Apple Silicon) build — EXPERIMENTAL. Boots and runs, but the recompilation has unresolved stub paths (jump-table HLE + WRAM gotos) that will trap if reached during deeper play. Please report issues.


## Install
1. Download `ZeldaAlttPSNESRecomp-macos-arm64.zip` from the **v0.4.0** release and unzip.
2. First launch: right-click `Zelda - A Link to the Past.app` -> Open (ad-hoc signed), or
   `xattr -dr com.apple.quarantine "Zelda - A Link to the Past.app"`.
3. ROM not included — supply your own dump: The Legend of Zelda: A Link to the Past (USA) .sfc dump
4. Run: `"Zelda - A Link to the Past.app/Contents/MacOS/Zelda - A Link to the Past" /path/to/rom`

## Build it yourself
`scripts/release-mac.sh` reproduces this artifact (build -> .app -> zip);
`scripts/release-mac.sh --publish` re-attaches it to the latest release.
Requires: `brew install cmake ninja sdl2 dylibbundler` on Apple Silicon.
