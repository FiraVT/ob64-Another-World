#!/usr/bin/env python3
"""List every instruction that forms an address inside a guest RAM range.

    tools/table_refs.py <start> <end>            # counts per unit
    tools/table_refs.py <start> <end> --list     # every site
    tools/table_refs.py 0x80187C34 0x8018AA9C    # the class table

Reads the disassembly comments N64Recomp writes into the generated C
(RecompiledFuncs/*.c and Bank*Funcs/*.c: `// 0x8016DE4C: lui $v0, 0x8018`),
so it covers the main code and every bank unit, offline and in about two
seconds. It follows each `lui` through an index `addu` to the `addiu`, load or
store that adds the low half, and reports the sites whose address falls in
[start, end). An indexed site reports the address at index 0.

It also says whether each such `lui` builds any address outside the range (a
shared upper half). When none does, the range can move by a multiple of
0x10000 by changing only those `lui` immediates (docs/anotherworld/
design-new-classes.md section 3).

Not seen: an address held in data, passed in from elsewhere, or built
without a lui (for example from $gp). Read the listed functions before
relying on the count.
"""

from __future__ import annotations

import argparse
import collections
import glob
import re
import sys
from pathlib import Path

LINE = re.compile(r"//\s+0x([0-9A-F]{8}):\s+(\w+)\s+(.*)$", re.I)
FUNC = re.compile(r"^RECOMP_FUNC void (\w+)\(")
MEMORY = re.compile(r"(-?0x[0-9A-Fa-f]+|-?\d+)\((\$\w+)\)")
STORES = {"sb", "sh", "sw", "sd", "swc1", "sdc1"}


def scan(root: Path, start: int, end: int):
    """Returns (sites, luis): sites[(unit, address)] = (function, text, target);
    luis[(unit, lui address)] = [(site address, target, inside)]."""
    sites = {}
    luis = collections.defaultdict(list)
    files = sorted(glob.glob(str(root / "RecompiledFuncs" / "*.c")) + glob.glob(str(root / "Bank*Funcs" / "*.c")))
    for path in files:
        unit = Path(path).parent.name
        func = "?"
        upper: dict[str, tuple[int, int]] = {}
        seen: set[int] = set()
        for line in open(path, encoding="utf-8", errors="replace"):
            m = FUNC.match(line)
            if m:
                func, upper, seen = m.group(1), {}, set()
                continue
            m = LINE.search(line)
            if not m:
                continue
            address = int(m.group(1), 16)
            if address in seen:  # a delay slot's comment is repeated
                continue
            seen.add(address)
            op = m.group(2).lower()
            args = [a.strip() for a in m.group(3).split(",")]
            if op == "lui" and len(args) == 2:
                try:
                    upper[args[0]] = (int(args[1], 0) << 16, address)
                except ValueError:
                    upper.pop(args[0], None)
                continue
            # An index added to an upper half keeps it.
            if op in ("addu", "add", "daddu") and len(args) == 3:
                source = args[1] if args[1] in upper else args[2] if args[2] in upper else None
                if source is not None:
                    upper[args[0]] = upper[source]
                    continue
            target = lui = None
            if op == "addiu" and len(args) == 3 and args[1] in upper:
                try:
                    target = (upper[args[1]][0] + int(args[2], 0)) & 0xFFFFFFFF
                    lui = upper[args[1]][1]
                except ValueError:
                    pass
            elif len(args) == 2:
                mm = MEMORY.match(args[1])
                if mm and mm.group(2) in upper:
                    target = (upper[mm.group(2)][0] + int(mm.group(1), 0)) & 0xFFFFFFFF
                    lui = upper[mm.group(2)][1]
            if target is not None:
                inside = start <= target < end
                luis[(unit, lui)].append((address, target, inside))
                if inside:
                    sites[(unit, address)] = (func, op + " " + m.group(3).strip(), target)
            if args and args[0].startswith("$") and op not in STORES:
                upper.pop(args[0], None)
    return sites, luis


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("start", type=lambda s: int(s, 16))
    ap.add_argument("end", type=lambda s: int(s, 16))
    ap.add_argument("--list", action="store_true", help="print every site")
    ap.add_argument("--stride", type=lambda s: int(s, 0), default=0, help="record size, for the --list offsets")
    args = ap.parse_args()
    root = Path(__file__).resolve().parent.parent
    sites, luis = scan(root, args.start, args.end)
    feeding = {k: v for k, v in luis.items() if any(inside for _, _, inside in v)}
    shared = {k: v for k, v in feeding.items() if not all(inside for _, _, inside in v)}
    units = collections.Counter(unit for unit, _ in sites)
    functions = collections.defaultdict(set)
    for (unit, _), (func, _, _) in sites.items():
        functions[unit].add(func)
    print("range %08X..%08X: %d sites in %d functions; %d luis feed them, %d of those also build other addresses"
          % (args.start, args.end, len(sites), sum(len(f) for f in functions.values()), len(feeding), len(shared)))
    for unit in sorted(units):
        print("  %-16s %4d sites %3d functions" % (unit, units[unit], len(functions[unit])))
    for (unit, lui), uses in sorted(shared.items()):
        outside = sorted({"%08X" % t for _, t, inside in uses if not inside})
        print("  shared lui %s %08X also builds %s" % (unit, lui, " ".join(outside[:4])))
    if args.list:
        for (unit, address), (func, text, target) in sorted(sites.items()):
            where = ""
            if args.stride:
                where = " (+0x%X)" % ((target - args.start) % args.stride)
            print("%-16s %08X %-30s %-40s -> %08X%s" % (unit, address, func, text, target, where))
    return 0


if __name__ == "__main__":
    sys.exit(main())
