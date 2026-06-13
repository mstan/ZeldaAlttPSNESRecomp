#!/usr/bin/env bash
# Regen pipeline driver for LegendofZeldaAlttpRecomp.
#
# Regenerates src/gen/*.c from the recomp/bank_*.cfg configs over a verified
# zelda.sfc, then syncs recomp/funcs.h. Modeled on MegamanXRecomp/tools/regen.sh
# and SuperMarioWorldRecomp/tools/regen.sh.
#
# Flags:
#   --no-tests   skip the framework test suite (default: run it).
#   -h | --help  this message.
#
# Run from anywhere — paths resolve relative to this script's location.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RUN_TESTS=1
for arg in "$@"; do
  case "$arg" in
    --no-tests) RUN_TESTS=0 ;;
    -h|--help)  sed -n '2,/^set -euo/p' "$0" | sed -n '/^# /p' | sed 's/^# //'; exit 0 ;;
    *) echo "regen.sh: unknown flag: $arg (try --help)" >&2; exit 2 ;;
  esac
done

cd "$ROOT"

ROM="zelda.sfc"
TESTS="snesrecomp/tests/run_tests.py"

# Python interpreter: prefer python3 (macOS / most Linux have no bare `python`).
PYTHON="${PYTHON:-$(command -v python3 || command -v python || true)}"
if [ -z "$PYTHON" ]; then
  echo "regen.sh: no python3/python interpreter found on PATH" >&2
  exit 1
fi

if [ ! -f "$ROM" ]; then
  echo "regen.sh: $ROM not found at repo root — drop a verified ALttP ROM there." >&2
  exit 1
fi

step() { echo; echo "=== $* ==="; }

# MSU-1: the build is recompiled from an MSU-1-patched ROM (the patch injects
# the audio driver in bank $22; recomp/bank22.cfg emits it). We apply the
# bundled, MIT-licensed qwertymodo patch to the user's STOCK rom in a
# throwaway file — the user never has to patch anything, and at runtime still
# uses their stock ROM. See recomp/msu1/ATTRIBUTION.md.
MSU_IPS="recomp/msu1/alttp_msu.ips"
GEN_ROM="$ROM"
if [ -f "$MSU_IPS" ]; then
  PATCHED_ROM=".build/zelda_msu1.sfc"
  mkdir -p "$(dirname "$PATCHED_ROM")"
  step "Applying MSU-1 patch (qwertymodo, MIT — recomp/msu1/)"
  "$PYTHON" tools/apply_msu_patch.py --rom "$ROM" --ips "$MSU_IPS" --out "$PATCHED_ROM"
  GEN_ROM="$PATCHED_ROM"
fi

step "Regenerating banks"
# Emits bankXX_v2.c / dispatch_v2.c (game-agnostic). Both the CMake build and
# the MSBuild project (src/zelda.vcxproj) now glob src/gen/*.c. Drop any old
# zelda_NN_v2.c / zelda_dispatch_v2.c from the previous naming scheme first,
# so the glob doesn't compile both an orphan and its bankNN replacement
# (duplicate symbols at link).
rm -f src/gen/zelda_*_v2.c
"$PYTHON" snesrecomp/tools/v2_regen.py --rom "$GEN_ROM" \
    --cfg-dir recomp --out-dir src/gen --prefix zelda

step "Syncing funcs.h"
"$PYTHON" snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp \
    --out recomp/funcs.h

if [ "$RUN_TESTS" -eq 1 ]; then
  step "Framework tests"
  "$PYTHON" "$TESTS"
fi

step "Done"
