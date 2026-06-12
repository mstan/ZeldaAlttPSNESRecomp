<#
make_release.ps1 — build the Zelda ALttP windows release zips.

Ships TWO windows zips (and ONLY zips — never a bare exe; zelda.exe is
useless without SDL2.dll):

  standard    ZeldaAlttPSNESRecomp-windows-x64-v<Version>.zip
              config Widescreen = 0. The authentic 256-wide recompilation.

  widescreen  ZeldaAlttPSNESRecomp-widescreen-windows-x64-v<Version>.zip
              config Widescreen = 71 (~16:9). Opt-in 16:9 mode: the view
              widens out to the real room/overworld edge and the HUD status
              bar anchors to the screen edges.

NOTE vs SMW: Zelda's widescreen is entirely runtime-gated runner + game code
— there is NO generated-code injection — so the two zips contain the SAME
exe and differ only in config.ini's Widescreen value. With Widescreen = 0
every widescreen branch is not taken and behaviour is byte-identical to the
faithful build (there is no more-pristine variant to ship, unlike SMW's
gen-injection split). Both facts are stated in each zip's README.

Stage layout matches prior releases: zelda.exe (Production|x64, console-
free), SDL2.dll, config.ini (variant-specific Widescreen), keybinds.ini (if
present next to the built exe), README.md (generated below — release-specific
"what's new" belongs in the gh release body, not the zip).

Zips land in release-stage\. Publish via gh AFTER the user signs off:

  gh release create v<Version> `
      release-stage\ZeldaAlttPSNESRecomp-windows-x64-v<Version>.zip `
      release-stage\ZeldaAlttPSNESRecomp-widescreen-windows-x64-v<Version>.zip

Usage: powershell -File tools\make_release.ps1 -Version 0.3.0 [-Variant standard|widescreen|both]
#>
param(
  [Parameter(Mandatory = $true)][string]$Version,
  [ValidateSet('standard', 'widescreen', 'both')]
  [string]$Variant = 'both',
  [int]$WidescreenExtra = 71,
  [string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bin = Join-Path $root 'build\bin-x64-Production'
$out = Join-Path $root 'release-stage'
New-Item -ItemType Directory -Force $out | Out-Null

# Build once — the exe is identical for both variants (widescreen is runtime-
# gated, not compiled in/out).
& $MSBuild (Join-Path $root 'zelda.sln') /p:Configuration=Production /p:Platform=x64 /m /v:quiet /nologo
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE)" }

# Base config.ini (repo root copy, which has Widescreen = 0).
$baseConfig = Get-Content (Join-Path $root 'config.ini') -Raw

function New-Variant([string]$kind) {
  if ($kind -eq 'widescreen') {
    $stageName = "ZeldaAlttPSNESRecomp-widescreen-windows-x64-v$Version"
    $wsValue = $WidescreenExtra
    $wsBlurb = @"

This is the **widescreen** build: ``config.ini`` ships with ``Widescreen = $WidescreenExtra``
(~16:9). The view widens out to the real room/overworld edge (clamped, so no
garbage past the edge) and the HUD status bar anchors to the screen edges.
Set ``Widescreen = 0`` to return to the authentic 256-wide view at any time —
the widescreen renderer is runtime-gated, so off is byte-identical to the
standard build.
"@
  } else {
    $stageName = "ZeldaAlttPSNESRecomp-windows-x64-v$Version"
    $wsValue = 0
    $wsBlurb = @"

This is the **standard** build: ``config.ini`` ships with ``Widescreen = 0`` —
the authentic 256-wide recompilation. (The same exe also contains the opt-in
widescreen renderer, dormant unless you set ``Widescreen`` above 0; with it 0
behaviour is identical to the faithful build. For a ready-to-go 16:9 config,
use the ``-widescreen-`` zip.)
"@
  }

  $stage = Join-Path $out $stageName
  if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
  New-Item -ItemType Directory -Force $stage | Out-Null

  Copy-Item (Join-Path $bin 'zelda.exe') $stage
  Copy-Item (Join-Path $bin 'SDL2.dll') $stage
  # variant config.ini: force the Widescreen line to the variant's value
  ($baseConfig -replace '(?m)^Widescreen\s*=.*$', "Widescreen = $wsValue") |
    Out-File (Join-Path $stage 'config.ini') -Encoding utf8 -NoNewline
  $kb = Join-Path $bin 'keybinds.ini'
  if (Test-Path $kb) { Copy-Item $kb $stage }

  @"
# Legend of Zelda: A Link to the Past — SNES static recompilation (Windows x64)

This is the **Production** build: an optimized, console-free native port —
running ``zelda.exe`` opens the game window only, with no terminal/debug
console.

Static recompilation turns the game's 65816 CPU code into native C (via the
[snesrecomp](https://github.com/mstan/snesrecomp) framework); the rest of the
SNES (PPU, APU/SPC700, DMA, registers) runs through the framework's runner
core.
$wsBlurb
## How to run

1. Extract this folder anywhere.
2. Run ``zelda.exe``. On first launch a file picker asks for your
   **legally-obtained** *Legend of Zelda: A Link to the Past (USA)* ROM
   (``.sfc`` / ``.smc``).
   - Expected SHA-256:
     ``66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb``
   - 512-byte SMC copier headers are auto-stripped before hashing.
3. The picked path is cached to ``rom.cfg`` next to the exe. Save data lands
   in ``saves/`` next to the exe.

The ROM is **never** redistributed — supply your own dump.

``config.ini`` and ``keybinds.ini`` sit next to the exe and can be edited.
Plug a gamepad in **before** launching for SDL_GameController auto-detect.

## Widescreen (opt-in)

``Widescreen`` in ``config.ini`` is the extra columns rendered per side
(0 = authentic 256-wide; ~71 ≈ 16:9 at 224 lines; max 95). It is an opt-in
enhancement layered on the faithful recompilation — credit to
[snesrev/zelda3](https://github.com/snesrev/zelda3) (the extra-side-space PPU
model this reimplements) and [xander-haj/Z3R](https://github.com/xander-haj/Z3R)
+ z3c (widescreen / HUD-rearrange concepts), all MIT.

See the repo's ``ISSUES.md`` for the current known-issue list, and the
release notes on GitHub for what changed in v$Version.
"@ | Out-File (Join-Path $stage 'README.md') -Encoding utf8

  $zip = Join-Path $out "$stageName.zip"
  if (Test-Path $zip) { Remove-Item -Force $zip }
  Compress-Archive -Path "$stage\*" -DestinationPath $zip
  Write-Host "--- $stageName ---"
  Get-ChildItem $stage | Select-Object Name, Length | Out-Host
  Get-Item $zip | Select-Object Name, Length | Out-Host
}

if ($Variant -eq 'both' -or $Variant -eq 'standard')   { New-Variant 'standard' }
if ($Variant -eq 'both' -or $Variant -eq 'widescreen') { New-Variant 'widescreen' }
