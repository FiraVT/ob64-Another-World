#!/usr/bin/env python3
"""Find LordlyCaliber's decompiled C for a function this port names.

LordlyCaliber (https://github.com/rempred/LordlyCaliber, MIT) bundles C
decompiled from US revision 0, named by revision 0 ROM offset
(`func_00107E38`). This port recompiles revision 1 and names functions by
revision 1 address (`func_ovlN_801AD6BC`). `tools/revmap.py` maps the two
revisions; this tool joins that map with LordlyCaliber's library manifest.

    tools/lc_lookup.py index [--lc DIR]          # build build/revmap/lc_functions.tsv
    tools/lc_lookup.py func_ovlR_80215C38        # our name -> decompiled C
    tools/lc_lookup.py 0x1960C8                  # our (rev1) ROM offset
    tools/lc_lookup.py func_00107E38             # LordlyCaliber name -> our name
    tools/lc_lookup.py --show NAME               # also print the C source

The LordlyCaliber clone is read from --lc, or $OGRE_LC_DIR. It is never copied
into this repository: the decompiled C is derived from the game's code and is
used here as a reference. Run tools/revmap.py first.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REVMAP = ROOT / "build/revmap/rev1_rev0.tsv"
INDEX = ROOT / "build/revmap/lc_functions.tsv"


def lc_dir(arg: str | None) -> Path:
    path = arg or os.environ.get("OGRE_LC_DIR")
    if not path:
        sys.exit("lc_lookup: give --lc <LordlyCaliber clone> or set OGRE_LC_DIR")
    path = Path(path)
    if not (path / "native/game/accepted/library-imports.json").is_file():
        sys.exit(f"lc_lookup: {path} is not a LordlyCaliber clone")
    return path


def revmap() -> dict[int, list[str]]:
    """rev0 ROM -> [our name, rev1 ROM, size, match]."""
    if not REVMAP.is_file():
        sys.exit("lc_lookup: run tools/revmap.py first")
    out = {}
    for line in REVMAP.read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or line.startswith("name"):
            continue
        name, rom1, size, rom0, _, kind = line.split("\t")
        if rom0 != "-":
            out[int(rom0, 16)] = [name, rom1, size, kind]
    return out


def build_index(lc: Path) -> int:
    rev0 = (ROOT / "assets/ogre64-rev0.z64").read_bytes()
    manifest = json.loads((lc / "native/game/accepted/library-imports.json").read_text(encoding="utf-8"))
    interest = json.loads((lc / "native/game/decomp-interest.json").read_text(encoding="utf-8"))
    behaviour = {}
    for group in interest["groups"]:
        for sym in group.get("symbols", []):
            behaviour.setdefault(sym.lower(), group.get("behavior", ""))
    by_rom = revmap()
    starts = sorted((rom, int(v[2], 16), v) for rom, v in by_rom.items())

    def containing(rom: int) -> list[str] | None:
        # A decompiled owner that starts inside one of our functions (a split the
        # recompiler made differently): name it as that function plus an offset.
        import bisect
        i = bisect.bisect_right([s[0] for s in starts], rom) - 1
        if i >= 0 and starts[i][0] <= rom < starts[i][0] + starts[i][1]:
            name, rom1, size, kind = starts[i][2]
            return [f"{name}+0x{rom - starts[i][0]:X}", rom1, size, kind]
        return None

    rows, missing, bad_hash = [], 0, 0
    for imp in manifest["imports"]:
        for owner in imp["owners"]:
            for phys in owner.get("physicalOwners", []):
                start, size = phys["romStart"], phys["bytes"]
                ok = hashlib.sha256(rev0[start:start + size]).hexdigest().upper() == phys["sha256"].upper()
                bad_hash += not ok
                ours = by_rom.get(start) or containing(start)
                missing += ours is None
                for fn in owner["functions"]:
                    rows.append([fn, f"0x{start:06X}", f"0x{size:X}", "yes" if ok else "no",
                                 *(ours or ["-", "-", "-", "-"]), imp["file"],
                                 behaviour.get(fn.lower(), "")])
    INDEX.parent.mkdir(parents=True, exist_ok=True)
    with INDEX.open("w", encoding="utf-8", newline="\n") as f:
        f.write("lc_name\trev0_rom\tlc_size\trev0_bytes_match\tour_name\trev1_rom\tour_size\tmatch\tlc_file\tbehaviour\n")
        for row in sorted(rows, key=lambda r: r[1]):
            f.write("\t".join(row) + "\n")
    print(f"{len(rows)} decompiled functions; {len(rows) - missing} with our name; "
          f"{bad_hash} owners whose revision 0 bytes differ from the manifest -> {INDEX}")
    return 0


def lookup(term: str, show: bool, lc_arg: str | None) -> int:
    if not INDEX.is_file():
        sys.exit("lc_lookup: run `tools/lc_lookup.py index` first")
    lines = INDEX.read_text(encoding="utf-8").splitlines()
    header = lines[0].split("\t")
    want = term.lower()
    if want.startswith("0x"):
        want = f"0x{int(want, 16):06x}"
    hits = [dict(zip(header, l.split("\t"))) for l in lines[1:]
            if want in (c.lower() for c in l.split("\t")[:6])]
    if not hits:
        print(f"{term}: no decompiled C in LordlyCaliber's library for this function")
        return 1
    for h in hits:
        print(f"{h['our_name']} (rev1 {h['rev1_rom']}, {h['match']}) = {h['lc_name']} (rev0 {h['rev0_rom']})")
        print(f"  file: {h['lc_file']}")
        if h["behaviour"]:
            print(f"  LordlyCaliber group: {h['behaviour']}")
        if show:
            path = lc_dir(lc_arg) / "native" / h["lc_file"]
            print(path.read_text(encoding="utf-8", errors="replace"))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("term", help="'index', a function name, or a ROM offset")
    ap.add_argument("--lc", help="LordlyCaliber clone (default $OGRE_LC_DIR)")
    ap.add_argument("--show", action="store_true", help="print the C source")
    args = ap.parse_args()
    if args.term == "index":
        return build_index(lc_dir(args.lc))
    return lookup(args.term, args.show, args.lc)


if __name__ == "__main__":
    sys.exit(main())

