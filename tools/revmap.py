#!/usr/bin/env python3
"""Map every recompiled function of US revision 1 to its place in US revision 0.

The port recompiles revision 1 (assets/ogre64.z64). LordlyCaliber and its
decompilation use revision 0 (assets/ogre64-rev0.z64) and name functions by
their revision 0 ROM offset. This tool finds, for each revision 1 function, the
revision 0 ROM offset that holds the same code.

    tools/revmap.py [--rev1 ROM] [--rev0 ROM] [--out build/revmap/rev1_rev0.tsv]

Function list: the `FuncEntry` tables in RecompiledFuncs/ and Bank*Funcs/
`recomp_overlays.inl` (name, section ROM start + offset, rom_size).

Matching, in order, at the revision 0 position predicted by the running ROM
delta of the previous match, then in a window around it:

- `exact`  : the bytes are identical
- `masked` : identical once `j`/`jal` targets and the immediates of `lui` and of
             the instructions that use an address (`addiu`, `ori`, loads and
             stores) are masked, so a function that only moved matches
- `changed`: not found, but the previous function matched and ends where this
             one starts, so revision 0 has different code at that offset
- `none`   : not found, and no position can be inferred

Output columns: name, rev1 ROM, size, rev0 ROM (or -), delta, match kind.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Opcodes whose 16-bit immediate can be the low half of an address.
ADDRESS_IMM = {8, 9, 13, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 46, 49, 53, 57, 61}
WINDOW = 0x20000


def read_rom(path: Path) -> bytes:
    rom = bytearray(path.read_bytes())
    if rom[:4] == bytes.fromhex("37804012"):  # byte-swapped .v64
        rom[::2], rom[1::2] = rom[1::2], rom[::2]
    if rom[:4] != bytes.fromhex("80371240"):
        sys.exit(f"revmap: {path} is not a big-endian N64 ROM")
    return bytes(rom)


def functions_in(inl: Path) -> list[tuple[str, int, int]]:
    """(name, rom, size) for every function in one unit's recomp_overlays.inl."""
    entry = re.compile(r"\{ \.func = (\w+), \.offset = 0x([0-9A-F]+), \.rom_size = 0x([0-9A-F]+) \}")
    section = re.compile(r"\.rom_addr = 0x([0-9A-F]+), \.ram_addr = 0x([0-9A-F]+), \.size = 0x([0-9A-F]+), "
                         r"\.funcs = (\w+),")
    text = inl.read_text(encoding="utf-8")
    tables = {}
    for block in re.finditer(r"static FuncEntry (\w+)\[\] = \{(.*?)\n\};", text, re.S):
        tables[block.group(1)] = [(m.group(1), int(m.group(2), 16), int(m.group(3), 16))
                                  for m in entry.finditer(block.group(2))]
    out = []
    for m in section.finditer(text):
        rom, size, table = int(m.group(1), 16), int(m.group(3), 16), m.group(4)
        for name, offset, length in tables.get(table, []):
            if offset < size and length > 0:
                out.append((name, rom + offset, length))
    return out


def functions() -> list[tuple[str, int, int]]:
    """(name, rom, size) for every recompiled function, sorted by ROM."""
    out = {}
    for inl in sorted(ROOT.glob("*Funcs/recomp_overlays.inl")):
        for name, rom, length in functions_in(inl):
            out.setdefault(rom, (name, rom, length))
    return sorted(out.values(), key=lambda f: f[1])


def masked(rom: bytes, start: int, size: int) -> bytes:
    words = []
    for at in range(start, start + size, 4):
        w = int.from_bytes(rom[at:at + 4], "big")
        op = w >> 26
        if op in (2, 3):
            w &= 0xFC000000
        elif op == 15 or op in ADDRESS_IMM:
            w &= 0xFFFF0000
        words.append(w.to_bytes(4, "big"))
    return b"".join(words)


def find(rev1: bytes, rev0: bytes, start: int, size: int, guess: int) -> tuple[int, str]:
    code = rev1[start:start + size]
    if 0 <= guess and rev0[guess:guess + size] == code:
        return guess, "exact"
    key = masked(rev1, start, size)
    if 0 <= guess and masked(rev0, guess, size) == key:
        return guess, "masked"
    lo, hi = max(0, guess - WINDOW), min(len(rev0) - size, guess + WINDOW)
    # Nearest candidate first, so a repeated small function binds to its neighbour.
    head = key[:min(16, size)]
    best = None
    pos = rev0.find(code, lo, hi + size)
    while pos != -1 and pos % 4 != 0:
        pos = rev0.find(code, pos + 1, hi + size)
    if pos != -1:
        best = (pos, "exact")
    if best is None:
        span = masked(rev0, lo, hi - lo + size) if hi > lo else b""
        at = span.find(head)
        while at != -1:
            if at % 4 == 0 and span[at:at + size] == key:
                cand = lo + at
                if best is None or abs(cand - guess) < abs(best[0] - guess):
                    best = (cand, "masked")
            at = span.find(head, at + 1)
    return best if best else (-1, "none")


def address_pairs(rev1: bytes, rev0: bytes, a1: int, a0: int, size: int, pairs: dict) -> None:
    """Collect (rev1 address, rev0 address) for every lui/low pair and jal in a
    function whose code matched apart from addresses."""
    hi1, hi0 = {}, {}
    for k in range(0, size, 4):
        w1 = int.from_bytes(rev1[a1 + k:a1 + k + 4], "big")
        w0 = int.from_bytes(rev0[a0 + k:a0 + k + 4], "big")
        op, rs, rt = w1 >> 26, w1 >> 21 & 31, w1 >> 16 & 31
        if op == 3:
            p1 = 0x80000000 | (w1 & 0x3FFFFFF) << 2
            p0 = 0x80000000 | (w0 & 0x3FFFFFF) << 2
            pairs.setdefault((p1, p0), 0)
            pairs[(p1, p0)] += 1
            continue
        if op == 15:
            hi1[rt], hi0[rt] = (w1 & 0xFFFF) << 16, (w0 & 0xFFFF) << 16
            continue
        if op in ADDRESS_IMM and rs in hi1 and rs in hi0:
            def full(hi, w):
                imm = w & 0xFFFF
                return (hi | imm) if op == 13 else (hi + (imm - 0x10000 if imm & 0x8000 else imm)) & 0xFFFFFFFF
            p1, p0 = full(hi1[rs], w1), full(hi0[rs], w0)
            if 0x80000000 <= p1 < 0x80800000 and 0x80000000 <= p0 < 0x80800000:
                pairs.setdefault((p1, p0), 0)
                pairs[(p1, p0)] += 1
        # A register written by anything else no longer holds the lui value.
        dest = (w1 >> 11 & 31) if op == 0 else rt if op in (8, 9, 10, 11, 12, 13, 14, 32, 33, 34, 35, 36, 37, 38, 39) else None
        if dest is not None:
            hi1.pop(dest, None)
            hi0.pop(dest, None)


def ranges(pairs: dict) -> list[tuple[int, int, int, int]]:
    """Merge address pairs into (rev1 start, rev1 end, delta, references) runs of
    one delta, ordered by revision 1 address."""
    by = {}
    for (p1, p0), n in pairs.items():
        by.setdefault(p1, {}).setdefault(p0 - p1, 0)
        by[p1][p0 - p1] += n
    out = []
    for p1 in sorted(by):
        delta = max(by[p1], key=by[p1].get)
        n = by[p1][delta]
        if out and out[-1][2] == delta:
            out[-1] = (out[-1][0], p1, delta, out[-1][3] + n)
        else:
            out.append((p1, p1, delta, n))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rev1", type=Path, default=ROOT / "assets/ogre64.z64")
    ap.add_argument("--rev0", type=Path, default=ROOT / "assets/ogre64-rev0.z64")
    ap.add_argument("--out", type=Path, default=ROOT / "build/revmap/rev1_rev0.tsv")
    args = ap.parse_args()

    rev1, rev0 = read_rom(args.rev1), read_rom(args.rev0)
    if rev1[0x3F] != 1 or rev0[0x3F] != 0:
        print(f"revmap: header revisions are {rev1[0x3F]} and {rev0[0x3F]}, expected 1 and 0", file=sys.stderr)
    funcs = functions()
    rows, delta, counts = [], 0, {"exact": 0, "masked": 0, "changed": 0, "none": 0}
    for name, rom, size in funcs:
        at, kind = find(rev1, rev0, rom, size, rom + delta)
        if (at < 0 and rows and rows[-1][3] >= 0 and rows[-1][5] != "changed"
                and rows[-1][1] + rows[-1][2] == rom):
            # Not found, but the previous function matched and ends where this one
            # starts: in revision 0 this function starts right after it, with
            # different code.
            at, kind = rows[-1][3] + rows[-1][2], "changed"
        counts[kind] += 1
        if at >= 0 and kind != "changed":
            delta = at - rom
        rows.append((name, rom, size, at, at - rom if at >= 0 else 0, kind))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open("w", encoding="utf-8", newline="\n") as f:
        f.write(f"# rev1 {hashlib.sha256(rev1).hexdigest()}\n# rev0 {hashlib.sha256(rev0).hexdigest()}\n")
        f.write("name\trev1_rom\tsize\trev0_rom\tdelta\tmatch\n")
        for name, rom, size, at, d, kind in rows:
            f.write(f"{name}\t0x{rom:06X}\t0x{size:X}\t{'0x%06X' % at if at >= 0 else '-'}\t{d:+#x}\t{kind}\n")
    print(f"{len(rows)} functions: {counts['exact']} exact, {counts['masked']} moved, "
          f"{counts['changed']} changed, {counts['none']} not found -> {args.out}")

    pairs = {}
    for name, rom, size, at, d, kind in rows:
        if kind in ("exact", "masked"):
            address_pairs(rev1, rev0, rom, at, size, pairs)
    runs = ranges(pairs)
    data_out = args.out.with_name("data_rev1_rev0.tsv")
    with data_out.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# RAM addresses referenced by matched code, merged into runs of one delta.\n")
        f.write("# rev0 address = rev1 address + delta inside a run. Gaps between runs are unreferenced.\n")
        f.write("rev1_first\trev1_last\tdelta\treferences\n")
        for first, last, delta, n in runs:
            f.write(f"0x{first:08X}\t0x{last:08X}\t{delta:+#x}\t{n}\n")
    print(f"{len(pairs)} address pairs in {len(runs)} runs -> {data_out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

