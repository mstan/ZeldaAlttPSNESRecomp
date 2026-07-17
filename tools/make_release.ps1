<#
make_release.ps1 - package the Zelda ALttP windows release zip.

Ships ONE windows zip (and ONLY a zip - never a bare exe;
ZeldaALttPSNESRecomp.exe is useless without SDL2.dll and the assets/
folder recomp-ui stages next to it):

  ZeldaAlttPSNESRecomp-windows-x64-v<Version>.zip

CONSOLIDATED (was dual standard/widescreen zips): the GUI launcher now has a
Widescreen 16:9 toggle and persists it to config.ini, so a separate
"-widescreen-" zip is redundant - one build serves both. config.ini ships
Widescreen = 0 (authentic); the player flips it in the launcher (Settings ->
Widescreen). Widescreen is runtime-gated (no gen-injection), so off is
byte-identical to the faithful build.

The build itself is intentionally separate so developers can choose their
toolchain and keep compilation priority under local control. The resulting
zip contains ZeldaALttPSNESRecomp.exe, MinGW runtime dependencies, config.ini
(Widescreen = 0), keybinds.ini (if present), assets/ (the recomp-ui
launcher's assets: fonts, img, boxart), and README.md. Release-specific
"what's new" belongs in the gh release body, not the zip. ROMs and
ROM-derived generated C are never staged.

Zips land in release-stage\. Publish via gh AFTER the user signs off:

  gh release create v<Version> `
      release-stage\ZeldaAlttPSNESRecomp-windows-x64-v<Version>.zip

Example:
  powershell -File tools\make_release.ps1 -Version 0.6.0 `
    -BuildDir build-recompui -RuntimeBinDir C:\msys64\mingw64\bin
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [string]$BuildDir = 'build-recompui',
  [string]$RuntimeBinDir = 'C:\msys64\mingw64\bin'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root $BuildDir
$exe = Join-Path $build 'ZeldaALttPSNESRecomp.exe'
$assets = Join-Path $build 'assets'

if (-not (Test-Path -LiteralPath $exe)) {
  throw "Release executable missing: $exe"
}
if (-not (Test-Path -LiteralPath $assets)) {
  throw "recomp-ui launcher assets/ missing: $assets"
}

$out = Join-Path $root 'release-stage'
$stageName = "ZeldaAlttPSNESRecomp-windows-x64-v$Version"
$stage = Join-Path $out $stageName
$zip = Join-Path $out "$stageName.zip"

$outFull = [IO.Path]::GetFullPath($out).TrimEnd('\') + '\'
$stageFull = [IO.Path]::GetFullPath($stage)
$zipFull = [IO.Path]::GetFullPath($zip)
if (-not $stageFull.StartsWith($outFull, [StringComparison]::OrdinalIgnoreCase) -or
    -not $zipFull.StartsWith($outFull, [StringComparison]::OrdinalIgnoreCase)) {
  throw 'Refusing to clean release paths outside release-stage.'
}

if (Test-Path -LiteralPath $stage) {
  Remove-Item -LiteralPath $stage -Recurse -Force
}
if (Test-Path -LiteralPath $zip) {
  Remove-Item -LiteralPath $zip -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination $stage

# config.ini ships Widescreen = 0 (authentic); the launcher toggles + persists it.
(Get-Content -LiteralPath (Join-Path $root 'config.ini') -Raw) -replace '(?m)^Widescreen\s*=.*$', 'Widescreen = 0' |
  Out-File (Join-Path $stage 'config.ini') -Encoding utf8 -NoNewline

$kb = Join-Path $build 'keybinds.ini'
if (Test-Path -LiteralPath $kb) {
  Copy-Item -LiteralPath $kb -Destination $stage
}

Copy-Item -LiteralPath $assets -Destination $stage -Recurse

$runtimeDlls = @(
  'SDL2.dll',
  'libgcc_s_seh-1.dll',
  'libstdc++-6.dll',
  'libwinpthread-1.dll'
)
foreach ($name in $runtimeDlls) {
  $source = Join-Path $RuntimeBinDir $name
  if (-not (Test-Path -LiteralPath $source)) {
    throw "Required MinGW runtime DLL missing: $source"
  }
  Copy-Item -LiteralPath $source -Destination $stage
}

@"
# Legend of Zelda: A Link to the Past - SNES static recompilation (Windows x64)

This is the **Production** build: an optimized, console-free native port -
running ``ZeldaALttPSNESRecomp.exe`` opens a launcher, then the game window,
with no terminal/debug console.

Static recompilation turns the game's 65816 CPU code into native C (via the
[snesrecomp](https://github.com/mstan/snesrecomp) framework); the rest of the
SNES (PPU, APU/SPC700, DMA, registers) runs through the framework's runner
core.

## How to run

1. Extract this folder anywhere (keep ``assets\`` next to the exe).
2. Run ``ZeldaALttPSNESRecomp.exe``. A launcher opens: pick your
   **legally-obtained** *A Link to the Past (USA)* ROM (``.sfc`` / ``.smc``),
   tune settings, press PLAY.
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

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip

Write-Host "--- $stageName ---"
Get-ChildItem -LiteralPath $stage | Select-Object Name, Length | Out-Host
Get-FileHash -LiteralPath $zip -Algorithm SHA256 | Out-Host
