#!/usr/bin/env python3
"""Make the generated code's per-class table addresses movable at run time.

    tools/relocate_tables.py [--units main,N,...] [--revert] [--check]

Every instruction that builds an address in a per-class table does it with its
own `lui` (docs/anotherworld/design-new-classes.md section 2;
tools/table_refs.py finds them). This tool rewrites the C line that each of
those `lui`s became so the upper half adds the table's shift variable:

    ctx->r2 = S32(RELOC_HI16(12, 0X1CC6C) << 16);
 -> ctx->r2 = S32((RELOC_HI16(12, 0X1CC6C) + ogre_class_table_shift) << 16); // app relocation: class table

With a shift of 0 the code is unchanged. With 0x48 every reader uses the copy
0x480000 higher, in the content arena, which keeps every low half, so no
other instruction changes (app/features/content_db.cpp fills the copies).

Tables:
  class table       0x80187C34..0x8018AA9C, plus the two sites that index one
                    record before it (func_80172C64, bank M 0x801A4144); the
                    five other sites before it read an element-name table
  appearance table  0x8018ED00..0x8018F0DE (portrait and battle model)

Run after tools/apply_hooks.py (the Makefile does). Idempotent. `--revert`
gives the generated files back byte for byte (their CRLF line ends kept).
`--check` reports without writing and exits 1 when a site is not patched.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import table_refs  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


class Table:
    def __init__(self, name: str, start: int, end: int, shift: str, extra: set[tuple[str, int]],
                 only: set[tuple[str, int]] | None = None):
        self.name = name
        self.start = start
        self.end = end
        self.shift = shift
        self.extra = extra
        self.only = only  # when set, these lui sites and no others
        self.tag = f"// app relocation: {name}"
        self.decl = (
            f"// app relocation: {name} (tools/relocate_tables.py)\n"
            f"#ifdef __cplusplus\nextern \"C\" unsigned int {shift};\n"
            f"#else\nextern unsigned int {shift};\n#endif\n\n"
        )


TABLES = [
    Table("class table", 0x80187C34, 0x8018AA9C, "ogre_class_table_shift",
          {("RecompiledFuncs", 0x80172C94), ("BankMFuncs", 0x801A4140)}),
    Table("appearance table", 0x8018ED00, 0x8018F0DE, "ogre_appearance_shift", set()),
    # The class-line table (bank S data, 81 u16 then 0xFFFF), for the classic
    # Change Class list's two reads only (func_ovlS_801D2808: the current
    # class's line, and the walk). Its copy holds 256 entries, so the list
    # reaches new classes (app/features/class_change.cpp). The table's other
    # readers keep the original.
    Table("class line table", 0x801EF2EC, 0x801EF390, "ogre_family_table_shift", set(),
          {("BankSFuncs", 0x801D2854), ("BankSFuncs", 0x801D2898)}),
    # The class key table (bank S data, a byte per class, 165 entries followed
    # by other data that is nonzero from class 187 on). Its copy holds 256
    # entries, 0 past 164, while any new class exists.
    Table("class key table", 0x801EF3D8, 0x801EF3D8 + 165, "ogre_class_key_shift", set()),
]

# Single instructions rewritten in place: (unit, address, generated text, new
# text). The classic Change Class list (state +0x1C0E, func_ovlS_801D2808)
# keeps a class in the low 7 bits with 0x80 for "listed, not selectable", so a
# class past 127 does not fit. The flag moves to 0x8000 and every reader takes
# the whole low byte; the screen's draw records (state +0x32C) carry the flag
# in their byte +1 (config/hooks.toml). docs/anotherworld/design-new-classes.md
# section 2e.
PATCH_TAG = "// app relocation: class list"
PATCHES = [
    ("BankSFuncs", 0x801D283C, "ctx->r3 < 0X51 ? 1 : 0", "ctx->r3 < ogre_family_bound ? 1 : 0"),
    ("BankSFuncs", 0x801D2888, "ctx->r3 | 0X80;", "ctx->r3 | 0X8000;"),
    ("BankSFuncs", 0x801D29B0, "ctx->r2 | 0X80;", "ctx->r2 | 0X8000;"),
    ("BankSFuncs", 0x801DC8A0, "ctx->r2 & 0X7F;", "ctx->r2 & 0XFF;"),
    ("BankSFuncs", 0x801E0BFC, "ctx->r4 & 0X7F;", "ctx->r4 & 0XFF;"),
    ("BankSFuncs", 0x801E387C, "ctx->r2 & 0X7F;", "ctx->r2 & 0XFF;"),
    ("BankSFuncs", 0x801B8C30, "ctx->r4 & 0X7F;", "ctx->r4 & 0XFF;"),
    ("BankSFuncs", 0x801B8C38, "S32(U32(ctx->r4) >> 7);", "MEM_BU(0X1, ctx->r16);"),
    ("BankSFuncs", 0x801B8C70, "ctx->r4 & 0X7F;", "ctx->r4 & 0XFF;"),
    # "class < 81" as "a standard class" for the character's own class: what it
    # may do in Organize (func_ovlS_801D379C's Change Class and other options,
    # func_ovlS_801CEFA4, func_ovlS_801D0150) and whether a new character gets a
    # generated name (func_8016BD20). A standard new class counts as standard
    # (ogre_class_standard). The known-bit write's guard 0x801D73B4 stays: the
    # bitfield ends at class 167.
    ("BankSFuncs", 0x801CEFA8, "ctx->r2 < 0X51 ? 1 : 0", "ogre_class_standard((uint32_t)ctx->r2)"),
    ("BankSFuncs", 0x801D01AC, "ctx->r2 < 0X51 ? 1 : 0", "ogre_class_standard((uint32_t)ctx->r2)"),
    ("BankSFuncs", 0x801D38A0, "ctx->r3 < 0X51 ? 1 : 0", "ogre_class_standard((uint32_t)ctx->r3)"),
    ("BankSFuncs", 0x801D38A4, "ctx->r3 < 0X51 ? 1 : 0", "ogre_class_standard((uint32_t)ctx->r3)"),
    ("BankSFuncs", 0x801D3970, "ctx->r4 < 0X51 ? 1 : 0", "ogre_class_standard((uint32_t)ctx->r4)"),
    ("BankSFuncs", 0x801D3974, "ctx->r4 < 0X51 ? 1 : 0", "ogre_class_standard((uint32_t)ctx->r4)"),
    ("BankSFuncs", 0x801D3BE4, "ctx->r2 < 0X51 ? 1 : 0", "ogre_class_standard((uint32_t)ctx->r2)"),
    ("RecompiledFuncs", 0x8016BD9C, "ctx->r6 < 0X51 ? 1 : 0", "ogre_class_standard((uint32_t)ctx->r6)"),
    # func_ovlS_801D379C's class key byte (+0x12; the Change Class options and
    # option 6, which the menu checks before it opens the class list): a
    # standard new class answers as its family_of class (ogre_class_line_key).
    ("BankSFuncs", 0x801D3878, "MEM_BU(ctx->r21, 0X12);", "ogre_class_line_key(MEM_BU(ctx->r21, 0X12));"),
    ("BankSFuncs", 0x801D3948, "MEM_BU(ctx->r21, 0X12);", "ogre_class_line_key(MEM_BU(ctx->r21, 0X12));"),
    ("BankSFuncs", 0x801D3B48, "MEM_BU(ctx->r21, 0X12);", "ogre_class_line_key(MEM_BU(ctx->r21, 0X12));"),
    # The portrait id table's asset id (0x01DFB36E): its size (0x8016FC1C), its
    # load (0x8016FC8C) and the portrait load (0x8016FD78) read it from
    # ogre_portrait_table_hi/lo, so new class portraits can append to a copy
    # (app/features/class_art.cpp; docs/anotherworld/design-class-art.md).
    ("RecompiledFuncs", 0x8016FC1C, "S32(0X1DF << 16);", "S32(ogre_portrait_table_hi << 16);"),
    ("RecompiledFuncs", 0x8016FC20, "| 0XB36E;", "| ogre_portrait_table_lo;"),
    ("RecompiledFuncs", 0x8016FC8C, "S32(0X1DF << 16);", "S32(ogre_portrait_table_hi << 16);"),
    ("RecompiledFuncs", 0x8016FC90, "| 0XB36E;", "| ogre_portrait_table_lo;"),
    ("RecompiledFuncs", 0x8016FD78, "S32(0X1DF << 16);", "S32(ogre_portrait_table_hi << 16);"),
    ("RecompiledFuncs", 0x8016FD7C, "| 0XB36E;", "| ogre_portrait_table_lo;"),
]
PATCH_DECL = (
    "// app relocation: class list (tools/relocate_tables.py)\n"
    "#ifdef __cplusplus\nextern \"C\" {\n#endif\n"
    "extern unsigned int ogre_family_bound;\n"
    "extern unsigned int ogre_portrait_table_hi;\n"
    "extern unsigned int ogre_portrait_table_lo;\n"
    "unsigned int ogre_class_standard(unsigned int cls);\n"
    "unsigned int ogre_class_line_key(unsigned int key);\n"
    "#ifdef __cplusplus\n}\n#endif\n\n"
)
LUI_LINE = re.compile(r"^(\s*ctx->r\d+ = S32\()(.+)( << 16\);)\s*$")
LUI_COMMENT = re.compile(r"//\s+0x([0-9A-F]{8}):\s+lui\s", re.I)


def lui_sites(table: Table) -> dict[str, set[int]]:
    """unit directory -> addresses of the lui instructions to patch."""
    out: dict[str, set[int]] = {}
    if table.only is not None:
        for unit, lui in table.only:
            out.setdefault(unit, set()).add(lui)
        return out
    _, luis = table_refs.scan(ROOT, table.start, table.end)
    for (unit, lui), uses in luis.items():
        if any(inside for _, _, inside in uses):
            out.setdefault(unit, set()).add(lui)
    for unit, lui in table.extra:
        out.setdefault(unit, set()).add(lui)
    return out


def patch_file(path: Path, table: Table, addresses: set[int], revert: bool) -> tuple[int, set[int]]:
    raw = path.read_bytes().decode("utf-8")
    eol = "\r\n" if "\r\n" in raw else "\n"
    text = raw.replace("\r\n", "\n")
    lines = text.split("\n")
    changed = 0
    seen: set[int] = set()
    for i, line in enumerate(lines[:-1]):
        m = LUI_COMMENT.search(line)
        if not m:
            continue
        address = int(m.group(1), 16)
        if address not in addresses:
            continue
        seen.add(address)
        target = lines[i + 1]
        if revert:
            if table.tag in target:
                body = LUI_LINE.match(target.replace(" " + table.tag, ""))
                if body:
                    inner = re.sub(rf"^\((.+) \+ {table.shift}\)$", r"\1", body.group(2))
                    lines[i + 1] = body.group(1) + inner + body.group(3)
                    changed += 1
            continue
        if table.tag in target:
            continue
        body = LUI_LINE.match(target)
        if not body:
            raise SystemExit(f"{path}: the line after the lui at 0x{address:08X} is not a lui: {target.strip()}")
        lines[i + 1] = f"{body.group(1)}({body.group(2)} + {table.shift}){body.group(3)} {table.tag}"
        changed += 1
    new = "\n".join(lines)
    if revert:
        new = new.replace(table.decl, "")
    elif changed and table.decl not in new:
        new = table.decl + new
    if new != text:
        path.write_bytes(new.replace("\n", eol).encode("utf-8"))
    return changed, seen


def patch_instructions(unit: str, revert: bool, check: bool) -> tuple[int, int, list[str]]:
    """Rewrites PATCHES in one unit: (lines changed or found patched, sites, missing)."""
    sites = [(a, old, new) for u, a, old, new in PATCHES if u == unit]
    if not sites:
        return 0, 0, []
    done = 0
    seen: set[int] = set()
    for path in sorted((ROOT / unit).glob("*.c")):
        raw = path.read_bytes().decode("utf-8")
        eol = "\r\n" if "\r\n" in raw else "\n"
        text = raw.replace("\r\n", "\n")
        lines = text.split("\n")
        for i, line in enumerate(lines[:-1]):
            m = re.search(r"//\s+0x([0-9A-F]{8}):", line)
            if not m:
                continue
            address = int(m.group(1), 16)
            for a, old, new in sites:
                if a != address:
                    continue
                target = lines[i + 1]
                if revert:
                    if PATCH_TAG in target:
                        lines[i + 1] = target.replace(" " + PATCH_TAG, "").replace(new, old)
                        seen.add(a)
                        done += 1
                    continue
                if PATCH_TAG in target:
                    seen.add(a)
                    done += check
                    continue
                if old in target:
                    seen.add(a)
                    if not check:
                        lines[i + 1] = target.replace(old, new) + " " + PATCH_TAG
                        done += 1
        new_text = "\n".join(lines)
        if revert:
            new_text = new_text.replace(PATCH_DECL, "")
        elif PATCH_TAG in new_text.replace(PATCH_DECL, "") and PATCH_DECL not in new_text:
            new_text = PATCH_DECL + new_text
        if not check and new_text != text:
            path.write_bytes(new_text.replace("\n", eol).encode("utf-8"))
    missing = [f"{unit} 0x{a:08X}" for a, _, _ in sites if a not in seen]
    return done, len(sites), missing


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--units", help="main and/or bank letters, comma separated (default: all)")
    ap.add_argument("--revert", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()
    wanted = None
    if args.units:
        wanted = {"RecompiledFuncs" if u.strip() == "main" else f"Bank{u.strip()}Funcs" for u in args.units.split(",")}
    failed = False
    for table in TABLES:
        total = patched = 0
        missing: list[str] = []
        for unit, addresses in sorted(lui_sites(table).items()):
            if wanted is not None and unit not in wanted:
                continue
            found: set[int] = set()
            for path in sorted((ROOT / unit).glob("*.c")):
                if args.check:
                    text = path.read_text(encoding="utf-8")
                    for a in addresses:
                        m = re.search(rf"//\s+0x{a:08X}:\s+lui[^\n]*\n([^\n]*)", text)
                        if m:
                            found.add(a)
                            patched += table.tag in m.group(1)
                    continue
                n, seen = patch_file(path, table, addresses, args.revert)
                patched += n
                found |= seen
            total += len(addresses)
            missing += [f"{unit} 0x{a:08X}" for a in sorted(addresses - found)]
        if args.check:
            print(f"{table.name}: {patched} of {total} lui sites patched")
            failed = failed or patched != total or bool(missing)
        else:
            print(f"{table.name}: {'reverted' if args.revert else 'patched'} {patched} lines for {total} lui sites")
        if missing:
            print("  missing:", ", ".join(missing))
            failed = True
    units = {u for u, _, _, _ in PATCHES}
    for unit in sorted(units):
        if wanted is not None and unit not in wanted:
            continue
        done, total, missing = patch_instructions(unit, args.revert, args.check)
        verb = "found patched" if args.check else ("reverted" if args.revert else "patched")
        print(f"class list: {verb} {done} lines for {total} instruction sites")
        if missing:
            print("  missing:", ", ".join(missing))
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
