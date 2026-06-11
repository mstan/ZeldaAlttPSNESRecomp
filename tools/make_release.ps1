<#
make_release.ps1 — build the Zelda ALttP windows release zip.

Ships ONE windows zip (and ONLY a zip — never a bare exe; zelda.exe is
useless without SDL2.dll). Stage layout matches v0.2.0: zelda.exe
(Production|x64, console-free), SDL2.dll, config.ini (repo root copy),
keybinds.ini (if present next to the built exe), README.md (generated
below — release-specific "what's new" belongs in the gh release body,
not the zip).

Zip lands in release-stage\ZeldaAlttPSNESRecomp-windows-x64-v<Version>.zip.
Publish via gh AFTER the user signs off:

  gh release create v<Version> release-stage\ZeldaAlttPSNESRecomp-windows-x64-v<Version>.zip

Usage: powershell -File tools\make_release.ps1 -Version 0.2.1
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

& $MSBuild (Join-Path $root 'zelda.sln') /p:Configuration=Production /p:Platform=x64 /m /v:quiet /nologo
if ($LASTEXITCODE -ne 0) { throw "MSBuild failed ($LASTEXITCODE)" }

$stageName = "ZeldaAlttPSNESRecomp-windows-x64-v$Version"
$stage = Join-Path $out $stageName
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item (Join-Path $bin 'zelda.exe') $stage
Copy-Item (Join-Path $bin 'SDL2.dll') $stage
Copy-Item (Join-Path $root 'config.ini') $stage
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

See the repo's ``ISSUES.md`` for the current known-issue list, and the
release notes on GitHub for what changed in v$Version.
"@ | Out-File (Join-Path $stage 'README.md') -Encoding utf8

$zip = Join-Path $out "$stageName.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Write-Host "--- $stageName ---"
Get-ChildItem $stage | Select-Object Name, Length | Out-Host
Get-Item $zip | Select-Object Name, Length | Out-Host
