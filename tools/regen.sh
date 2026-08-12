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
SNESRECOMP_ROOT="${SNESRECOMP_ROOT:-snesrecomp}"
TESTS="$SNESRECOMP_ROOT/tests/run_tests.py"

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
  "$PYTHON" "$SNESRECOMP_ROOT/tools/build_native_analyzer.py"
fi

GEN_ROM="$ROM"

# MSU-1 is implemented as a trusted Mods plugin. The IPS in recomp/msu1 is
# retained only as credited reference material; regen always uses the stock ROM.

step "Regenerating banks"
# The LLE-first emitter publishes a complete staging directory atomically. It
# also removes legacy title-prefixed units from staging before publication, so
# a failed regeneration cannot leave the live output half-updated.
# --cfg-roots is the static-coverage policy (mirrors MegamanX/SMW): every
# declared `func` seeds the analysis closure so the proven surface is
# materialized as AOT; the interpreter is a failsafe for the unprovable
# remainder, never the plan of record for known code. Verified 2026-07-20:
# 4597 AOT variants, clean attract, 0 unresolved / dispatch misses, no crash.
# The historical "AOT promotion crashes Zelda" was resolved by PR #6's decoder
# rewrite.
"$PYTHON" "$SNESRECOMP_ROOT/tools/v2_emit.py" --rom "$GEN_ROM" \
    --cfg-dir recomp --out-dir src/gen --cfg-roots \
    --analysis-backend "$ANALYSIS_BACKEND"

# Generated game code performs the stock 256-wide sprite draw cull. Expand its
# horizontal comparison to the adaptive viewport after every regeneration.
"$PYTHON" tools/apply_widescreen_overrides.py --gen-dir src/gen

step "Syncing funcs.h"
"$PYTHON" "$SNESRECOMP_ROOT/tools/v2_sync_funcs_h.py" --cfg-dir recomp \
    --out recomp/funcs.h

if [ "$STRICT_IDEMPOTENT" -eq 1 ]; then
  step "Idempotency check: regen into temp dir + byte-compare"
  TMP_GEN="$(mktemp -d)"
  trap 'rm -rf "$TMP_GEN"' EXIT
  "$PYTHON" "$SNESRECOMP_ROOT/tools/v2_emit.py" --rom "$GEN_ROM" \
      --cfg-dir recomp --out-dir "$TMP_GEN" --cfg-roots \
      --analysis-backend "$ANALYSIS_BACKEND"
  "$PYTHON" tools/apply_widescreen_overrides.py --gen-dir "$TMP_GEN"
  "$PYTHON" "$SNESRECOMP_ROOT/tools/v2_compare_output.py" \
      --expected src/gen --actual "$TMP_GEN"
fi

if [ "$RUN_TESTS" -eq 1 ]; then
  step "Framework tests"
  "$PYTHON" "$TESTS"
fi

step "Done"
