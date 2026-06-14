<#
make_release.ps1 - build the Zelda ALttP windows release zip.

Ships ONE windows zip (and ONLY a zip - never a bare exe; zelda.exe is
useless without SDL2.dll and the launcher/ assets):

  ZeldaAlttPSNESRecomp-windows-x64-v<Version>.zip

CONSOLIDATED (was dual standard/widescreen zips): the GUI launcher now has a
Widescreen 16:9 toggle and persists it to config.ini, so a separate
"-widescreen-" zip is redundant - one build serves both. config.ini ships
Widescreen = 0 (authentic); the player flips it in the launcher (Settings ->
Widescreen). Widescreen is runtime-gated (no gen-injection), so off is
byte-identical to the faithful build.

The zip contains: zelda.exe (Production|x64, console-free), SDL2.dll,
config.ini (Widescreen = 0), keybinds.ini (if present), launcher/ (the GUI
launcher's RmlUi assets: launcher.rml, fonts, img, boxart), and README.md.
Release-specific "what's new" belongs in the gh release body, not the zip.

Zips land in release-stage\. Publish via gh AFTER the user signs off:

  gh release create v<Version> `
      release-stage\ZeldaAlttPSNESRecomp-windows-x64-v<Version>.zip

Usage: powershell -File tools\make_release.ps1 -Version 0.5.0
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build\bin-x64-Production'
$out = Join-Path $root 'release-stage'
New-Item -ItemType Directory -Force $out | Out-Null

# Build Production (console-free, optimized). Builds the launcher + stages its
# assets into build\bin-x64-Production\launcher\ via the project's copy targets.
& $MSBuild (Join-Path $root 'zelda.sln') /p:Configuration=Production /p:Platform=x64 /m /v:quiet /nologo
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE)" }

$stageName = "ZeldaAlttPSNESRecomp-windows-x64-v$Version"
$stage = Join-Path $out $stageName
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $bin 'zelda.exe') $stage
Copy-Item (Join-Path $bin 'SDL2.dll') $stage
# config.ini ships Widescreen = 0; the launcher toggles + persists it.
(Get-Content (Join-Path $root 'config.ini') -Raw) -replace '(?m)^Widescreen\s*=.*$', 'Widescreen = 0' |
  Out-File (Join-Path $stage 'config.ini') -Encoding utf8 -NoNewline
$kb = Join-Path $bin 'keybinds.ini'
if (Test-Path $kb) { Copy-Item $kb $stage }

# Launcher assets (RmlUi) - the GUI menu needs these next to the exe.
$launcherSrc = Join-Path $bin 'launcher'
if (-not (Test-Path $launcherSrc)) { throw "launcher/ assets missing at $launcherSrc - did the Production build run the CopyLauncherAssets target?" }
Copy-Item $launcherSrc $stage -Recurse

@"
# Legend of Zelda: A Link to the Past - SNES static recompilation (Windows x64)

This is the **Production** build: an optimized, console-free native port -
running ``zelda.exe`` opens a launcher, then the game window, with no
terminal/debug console.

Static recompilation turns the game's 65816 CPU code into native C (via the
[snesrecomp](https://github.com/mstan/snesrecomp) framework); the rest of the
SNES (PPU, APU/SPC700, DMA, registers) runs through the framework's runner
core.

## How to run

1. Extract this folder anywhere (keep ``launcher/`` next to ``zelda.exe``).
2. Run ``zelda.exe``. A launcher opens: pick your **legally-obtained**
   *A Link to the Past (USA)* ROM (``.sfc`` / ``.smc``), tune settings, press PLAY.
   - Expected SHA-256:
     ``66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb``
   - 512-byte SMC copier headers are auto-stripped before hashing.
3. The picked path is cached to ``rom.cfg`` next to the exe; settings persist
   to ``config.ini``. Save data lands in ``saves/`` next to the exe.

The ROM is **never** redistributed - supply your own dump.

## Widescreen (in the launcher)

Settings -> **Widescreen 16:9** toggles the opt-in 16:9 view (extra columns
rendered per side; the HUD anchors to the screen edges). Off = authentic
256-wide, byte-identical to the faithful build (the widescreen renderer is
runtime-gated). Credit to [snesrev/zelda3](https://github.com/snesrev/zelda3)
and [xander-haj/Z3R](https://github.com/xander-haj/Z3R) + z3c, all MIT.

## MSU-1 audio (in the launcher)

Settings -> **MSU-1 audio** streams CD-quality music in place of the SPC
soundtrack when a pack is present. Drop a standard ALttP MSU-1 ``.pcm`` pack
into the ``msu/`` folder (next to the exe) and enable the toggle. No pack =
authentic SPC music. The MSU-1 driver is qwertymodo's ALttP MSU-1 patch,
building on Conn's work, bundled under the MIT license - see the repo's
``recomp/msu1/ATTRIBUTION.md``.

See the repo's ``ISSUES.md`` for known issues, and the GitHub release notes for
what changed in v$Version.
"@ | Out-File (Join-Path $stage 'README.md') -Encoding utf8

$zip = Join-Path $out "$stageName.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "--- $stageName ---"
Get-ChildItem $stage | Select-Object Name, Length | Out-Host
Get-Item $zip | Select-Object Name, Length | Out-Host
