#!/usr/bin/env python3
"""Apply the bundled MSU-1 IPS patch to a stock ALttP ROM, for regen.

The MSU-1 build is recompiled from an MSU-1-patched ROM (the patch injects
the audio driver in bank $22). Rather than make the user patch their ROM by
hand, regen applies qwertymodo's MIT-licensed IPS patch (recomp/msu1/) to
their *stock* ROM in a throwaway file and recompiles from that. The user
only ever provides — and, at runtime, only needs — the stock ROM.

The patch's IPS offsets assume the canonical US 1.0 ROM. If the supplied
ROM's SHA-256 doesn't match it, patching may produce a broken ROM, so we
warn loudly (but still proceed, in case it's an acceptable variant).

Usage:
    python tools/apply_msu_patch.py --rom zelda.sfc \
        --ips recomp/msu1/alttp_msu.ips --out build/zelda_msu.sfc
"""
import argparse
import hashlib
import sys

# Canonical "Legend of Zelda, The - A Link to the Past (USA).sfc", 1 MiB,
# unheadered — the ROM qwertymodo's IPS targets.
VANILLA_US_SHA256 = "66871d66be19ad2c34c927d6b14cd8eb6fc3181965b6e517cb361f7316009cfb"


def rom_sha256(data: bytes) -> str:
    # Match the launcher: strip a 512-byte SMC copier header if present.
    hdr = 512 if (len(data) % 1024) == 512 else 0
    return hashlib.sha256(data[hdr:]).hexdigest()


def apply_ips(rom: bytearray, ips: bytes) -> int:
    if ips[:5] != b"PATCH":
        raise ValueError("not an IPS file (missing PATCH magic)")
    i, records = 5, 0
    while True:
        if ips[i:i + 3] == b"EOF":
            break
        off = (ips[i] << 16) | (ips[i + 1] << 8) | ips[i + 2]
        i += 3
        size = (ips[i] << 8) | ips[i + 1]
        i += 2
        if size == 0:  # RLE record
            run = (ips[i] << 8) | ips[i + 1]
            val = ips[i + 2]
            i += 3
            end = off + run
            if end > len(rom):
                rom.extend(b"\x00" * (end - len(rom)))
            for j in range(off, end):
                rom[j] = val
        else:
            end = off + size
            if end > len(rom):
                rom.extend(b"\x00" * (end - len(rom)))
            rom[off:end] = ips[i:i + size]
            i += size
        records += 1
    return records


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", required=True, help="stock ALttP (USA) ROM")
    ap.add_argument("--ips", required=True, help="MSU-1 IPS patch")
    ap.add_argument("--out", required=True, help="output patched ROM")
    ap.add_argument("--expect-sha256", default=VANILLA_US_SHA256,
                    help="ROM hash the patch targets (for the mismatch warning)")
    args = ap.parse_args()

    rom = bytearray(open(args.rom, "rb").read())
    got = rom_sha256(bytes(rom))
    if args.expect_sha256 and got != args.expect_sha256:
        sys.stderr.write(
            "\n*** WARNING: MSU-1 patch / ROM mismatch ***\n"
            f"  {args.rom}\n"
            f"    sha256 : {got}\n"
            f"    expected: {args.expect_sha256}  (US 1.0, the patch's target)\n"
            "  The MSU-1 IPS patch is written for that exact ROM. Applying it to\n"
            "  a different ROM may produce a broken image and a non-working build.\n"
            "  Proceeding anyway.\n\n")

    ips = open(args.ips, "rb").read()
    n = apply_ips(rom, ips)
    open(args.out, "wb").write(rom)
    print(f"[apply_msu_patch] applied {n} IPS records -> {args.out} "
          f"({len(rom)} bytes, sha256 {rom_sha256(bytes(rom))})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
