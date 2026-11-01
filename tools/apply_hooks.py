#!/usr/bin/env python3
"""Insert the app's hooks (config/hooks.toml) into the generated C.

Bank code is outside the mod hook map, and some main-unit hooks need a call in
the middle of a function, so the app's entry points are written into the
generated C after every regeneration. One entry per hook point in
config/hooks.toml; this tool replaces the per-feature scripts
(director_hook, mission_cursor, map_cursor, battle_ai, hd_backgrounds,
njpeg_readback).

    tools/apply_hooks.py [--rev 0|1] [--units main,N,...] [--revert]

`--rev 0` applies to rev0/ (tools/rev0_gen.py), with every address in an anchor
moved to revision 0 (docs/anotherworld/notes-revisions.md): RAM below 0x8016D068 is the same,
RAM from 0x8016D220 up is 0x20 lower. Applying is idempotent: a hook whose code
is already present is skipped.
"""

from __future__ import annotations

import argparse
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HOOKS = ROOT / "config/hooks.toml"


def vram0(v: int) -> int:
    if v < 0x8016D068 or v >= 0x80800000:
        return v
    if v >= 0x8016D220:
        return v - 0x20
    raise ValueError(f"0x{v:08X} is inside the functions that changed between revisions")


def for_revision(text: str, rev: int) -> str:
    if rev == 1:
        return text
    text = re.sub(r"0x(8[0-9A-F]{7})", lambda m: f"0x{vram0(int(m.group(1), 16)):08X}", text)
    return re.sub(r"L_(8[0-9A-F]{7})", lambda m: f"L_{vram0(int(m.group(1), 16)):08X}", text)


def decl_block(name: str, signatures: list[str]) -> str:
    lines = [f"// app hook: {name}", "#ifdef __cplusplus"]
    lines += [f'extern "C" {s};' for s in signatures]
    lines += ["#else"] + [f"{s};" for s in signatures] + ["#endif", "", ""]
    return "\n".join(lines)


_MAP = None


def unit_of_rom(rom: int) -> str | None:
    """The generated unit that holds the code at ROM address `rom`: "main" for
    the main ELF and its overlays, or a bank unit letter (tools/n64map.py reads
    config/config.yaml and app/src/bank_funcs.inc)."""
    global _MAP
    if _MAP is None:
        sys.path.insert(0, str(ROOT / "tools"))
        from n64map import load_map
        _MAP = load_map()
    seg = _MAP.by_rom(rom)
    if seg is None or seg.vram is None:
        return None
    return seg.unit or "main"


def unit_dir(base: Path, unit: str) -> Path:
    return base / ("RecompiledFuncs" if unit == "main" else f"Bank{unit}Funcs")


def apply(hook: dict, base: Path, rev: int, revert: bool) -> int:
    anchor = hook["anchor_rev0"] if rev == 0 and "anchor_rev0" in hook else for_revision(hook["anchor"], rev)
    if anchor.count("@@") != 1:
        print(f"apply_hooks: {hook['name']}: the anchor needs exactly one @@", file=sys.stderr)
        return 1
    before, after = anchor.split("@@")
    code, marker = hook["code"], hook["code"].splitlines()[0]
    decl = decl_block(hook["name"], hook["decls"]) if hook["decls"] else ""
    for path in sorted(unit_dir(base, hook["unit"]).glob("*.c")):
        text = path.read_text(encoding="utf-8")
        if revert:
            # The inserted code is removed wherever it is: two hooks can share
            # an anchor (0x8008B024), and the second one's code then sits
            # between the first one's and the anchor.
            if marker in text and code in text:
                text = text.replace(code, "", 1)
                if decl:
                    text = text.replace(decl, "", 1)
                path.write_text(text, encoding="utf-8")
                print(f"apply_hooks: {hook['name']}: reverted ({path.name})")
            elif marker in text:
                print(f"apply_hooks: {hook['name']}: marker found but its code is not one block in "
                      f"{path.name}; not reverted", file=sys.stderr)
                return 1
            continue
        if marker in text:
            return 0
        n = text.count(before + after)
        if n == 0:
            continue
        if n != 1:
            print(f"apply_hooks: {hook['name']}: anchor occurs {n} times in {path.name}", file=sys.stderr)
            return 1
        at = text.index(before + after) + len(before)
        text = text[:at] + code + text[at:]
        if decl:
            func = text.rfind("RECOMP_FUNC ", 0, at)
            text = text[:func] + decl + text[func:]
        path.write_text(text, encoding="utf-8")
        print(f"apply_hooks: {hook['name']}: applied ({path.name})")
        return 0
    if revert:
        return 0
    print(f"apply_hooks: {hook['name']}: anchor not found in {unit_dir(base, hook['unit']).name}/; "
          "the generated code changed", file=sys.stderr)
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rev", type=int, choices=(0, 1), default=1)
    ap.add_argument("--units", help="only these units (main, A, B, ...)")
    ap.add_argument("--revert", action="store_true")
    args = ap.parse_args()
    base = ROOT if args.rev == 1 else ROOT / "rev0"
    units = set(args.units.split(",")) if args.units else None
    status = 0
    hooks = tomllib.loads(HOOKS.read_text(encoding="utf-8"))["hook"]
    for hook in hooks:
        if "unit" not in hook:
            unit = unit_of_rom(hook["rom"])
            if unit is None:
                print(f"apply_hooks: {hook['name']}: no unit holds ROM 0x{hook['rom']:06X}", file=sys.stderr)
                status |= 1
                continue
            hook["unit"] = unit
        if units is None or hook["unit"] in units:
            status |= apply(hook, base, args.rev, args.revert)
    return status


if __name__ == "__main__":
    sys.exit(main())

