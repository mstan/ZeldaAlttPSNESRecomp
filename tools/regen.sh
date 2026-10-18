#!/usr/bin/env sh
# Generation policy and game-owned hooks live in recomp/generate.toml.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
FRAMEWORK=${SNESRECOMP_ROOT:-"$ROOT/snesrecomp"}
exec sh "$FRAMEWORK/tools/regen_project.sh" --project-root "$ROOT" "$@"
