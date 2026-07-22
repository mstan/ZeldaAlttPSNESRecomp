#!/usr/bin/env python3
"""Apply deterministic adaptive-widescreen hooks to generated Zelda AOT C."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


MARKER = "ZELDA-WS-SPRITE-DRAW-CULL"
FUNCTION = "Sprite_PrepOamCoordOrDoubleRet"
SIGNATURE_RE = re.compile(
    rf"RecompReturn {FUNCTION}_(M[01]X[01])\(CpuState \*cpu\) \{{"
)
BRANCH_RE = re.compile(
    r"(?P<indent>\s*)if \(cpu->_flag_C == 1\) \{ cpu->cycles \+= 1; "
    r"cpu->master_cycles \+= 8; goto L_E476_(?P<mode>M[01]X[01]); \}"
)


def locate_definition(gen_dir: Path) -> Path:
    matches = []
    for path in sorted(gen_dir.glob("bank06_part*_v2.c")):
        text = path.read_text(encoding="utf-8")
        if SIGNATURE_RE.search(text):
            matches.append(path)
    if len(matches) != 1:
        raise SystemExit(
            f"expected one generated definition for {FUNCTION}, found {len(matches)}"
        )
    return matches[0]


def inject(text: str) -> tuple[str, int]:
    definitions = list(SIGNATURE_RE.finditer(text))
    changes = 0
    for index, match in reversed(list(enumerate(definitions))):
        start = match.end()
        end = definitions[index + 1].start() if index + 1 < len(definitions) else len(text)
        body = text[start:end]
        mode = match.group(1)
        marker = f"/*{MARKER}-{mode}*/"
        if marker in body:
            continue

        # M0X0 shares the generated M1X0 tail labels, so the branch suffix
        # need not match the entry variant's name. The first E476 branch in
        # each definition is the horizontal comparison; later ones are tails.
        branch = next(BRANCH_RE.finditer(body), None)
        if branch is None:
            raise SystemExit(f"could not locate horizontal cull branch for {mode}")
        indent = branch.group("indent")
        hook = (
            f"{indent}{marker}\n"
            f"{indent}ZeldaAdjustSpritePrepHorizontalCull(cpu);"
        )
        pos = start + branch.start()
        text = text[:pos] + hook + text[pos:]
        changes += 1
    return text, changes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--gen-dir", type=Path, default=Path("src/gen"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()

    path = locate_definition(args.gen_dir)
    text = path.read_text(encoding="utf-8")
    modes = [match.group(1) for match in SIGNATURE_RE.finditer(text)]
    expected = {f"/*{MARKER}-{mode}*/" for mode in modes}
    present = {marker for marker in expected if marker in text}

    if args.check:
        if present != expected:
            missing = ", ".join(sorted(expected - present))
            raise SystemExit(f"missing adaptive sprite cull hooks: {missing}")
        print(f"adaptive sprite draw-cull overrides present: {path}")
        return 0

    text, changes = inject(text)
    if changes:
        path.write_text(text, encoding="utf-8", newline="")
        print(f"injected {changes} adaptive sprite draw-cull hook(s): {path}")
    else:
        print(f"adaptive sprite draw-cull hooks already present: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
