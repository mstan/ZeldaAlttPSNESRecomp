#!/usr/bin/env bash
# Regen pipeline driver for LegendofZeldaAlttpRecomp.
#
# Regenerates src/gen/*.c from the recomp/bank_*.cfg configs over a verified
# zelda.sfc, then syncs recomp/funcs.h. Modeled on MegamanXRecomp/tools/regen.sh
# and SuperMarioWorldRecomp/tools/regen.sh.
#
# Flags:
#   --no-tests             skip the framework test suite (default: run it).
#   --strict-idempotent    regenerate into a temporary directory and require
#                          byte-identical output.
#   -h | --help            this message.
#
# Run from anywhere — paths resolve relative to this script's location.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

RUN_TESTS=1
STRICT_IDEMPOTENT=0
for arg in "$@"; do
  case "$arg" in
    --no-tests) RUN_TESTS=0 ;;
    --strict-idempotent) STRICT_IDEMPOTENT=1 ;;
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

ANALYSIS_BACKEND="${SNESRECOMP_ANALYSIS_BACKEND:-native}"
case "$ANALYSIS_BACKEND" in
  native|python|auto) ;;
  *) echo "regen.sh: invalid SNESRECOMP_ANALYSIS_BACKEND: $ANALYSIS_BACKEND" >&2; exit 2 ;;
esac

if [ "$ANALYSIS_BACKEND" = native ]; then
  step "Building native analyzer"
  "$PYTHON" snesrecomp/tools/build_native_analyzer.py
fi

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
# The LLE-first emitter publishes a complete staging directory atomically. It
# also removes legacy title-prefixed units from staging before publication, so
# a failed regeneration cannot leave the live output half-updated.
"$PYTHON" snesrecomp/tools/v2_emit.py --rom "$GEN_ROM" \
    --cfg-dir recomp --out-dir src/gen \
    --analysis-backend "$ANALYSIS_BACKEND"

step "Syncing funcs.h"
"$PYTHON" snesrecomp/tools/v2_sync_funcs_h.py --cfg-dir recomp \
    --out recomp/funcs.h

if [ "$STRICT_IDEMPOTENT" -eq 1 ]; then
  step "Idempotency check: regen into temp dir + byte-compare"
  TMP_GEN="$(mktemp -d)"
  trap 'rm -rf "$TMP_GEN"' EXIT
  "$PYTHON" snesrecomp/tools/v2_emit.py --rom "$GEN_ROM" \
      --cfg-dir recomp --out-dir "$TMP_GEN" \
      --analysis-backend "$ANALYSIS_BACKEND"
  "$PYTHON" snesrecomp/tools/v2_compare_output.py \
      --expected src/gen --actual "$TMP_GEN"
fi

if [ "$RUN_TESTS" -eq 1 ]; then
  step "Framework tests"
  "$PYTHON" "$TESTS"
fi

step "Done"
