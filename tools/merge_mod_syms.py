#!/usr/bin/env python3
"""Write the mod reference symbols: the main ELF's dump plus every bank unit's.

RecompModTool reads one function reference file (`func_reference_syms_file` in a
mod's `mod.toml`) and resolves a hook's target name against its `[[section]]`
blocks. A hook records the section's `rom` and the function's `vram`, and the
runtime finds the function from those two values. `N64Recomp --dump-context`
on `config/config.toml` dumps only the main ELF's five code sections, so a
`RECOMP_HOOK("func_ovlN_...")` on a streamed-overlay bank function failed with
"hooks a function that doesn't exist in the original ROM".

This script runs `--dump-context` once for the main config and once per bank
config (`config/banks/config-bank<U>.toml`), then appends the bank units'
`[[section]]` blocks to the main dump. `from_symbol_file` numbers sections by
position and stores each function relative to its own section, so appending
blocks keeps the main sections' indices unchanged.

A bank record whose ROM start is already in the merged file is skipped, and so
is a function name that is already defined (the mod tool keeps the first name it
sees). Both are reported. `data_dump.toml` stays the main ELF's: bank data
symbols are not merged.

The main ELF links the ROM between its code sections as raw asset blobs
(`._66E30`, `._1F0A00`), whose data sections carry `rom = vram = <rom offset>`.
The mod tool requires a data section with a `rom` key to have the same `vram`
as the function section at that ROM address, and a bank record now starts at
`0x66E30`. Those blob sections only hold linker boundary symbols, and while no
function section had their ROM address they resolved as absolute symbols. The
script drops their `rom` key, which keeps them absolute.

The runtime side is `recomp::overlays::register_bank_hook_sections`, which
`app/src/bank_overlays.cpp` fills from `app/src/bank_funcs.inc`. Both read the
same bank ELFs, so run `make bank-recomp` before this.

Usage:
    tools/merge_mod_syms.py <N64Recomp> <out dir> <unit>...
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SECTION_SPLIT_RE = re.compile(r"^\[\[section\]\]\s*$", re.MULTILINE)
ROM_RE = re.compile(r"^rom\s*=\s*0x([0-9A-Fa-f]+)\s*$", re.MULTILINE)
NAME_RE = re.compile(r'^name\s*=\s*"([^"]*)"\s*$', re.MULTILINE)
VRAM_RE = re.compile(r"^vram\s*=\s*0x([0-9A-Fa-f]+)\s*$", re.MULTILINE)
FUNC_RE = re.compile(r'^\s*\{\s*name\s*=\s*"([^"]+)"')


def dump_context(n64recomp: Path, config: Path, out_dir: Path, elf: Path | None = None) -> None:
    if elf is not None and not elf.exists():
        raise SystemExit(f"merge_mod_syms: {elf.relative_to(ROOT)} is missing; run `make bank-recomp` first")
    result = subprocess.run(
        [str(n64recomp), str(config), "--dump-context"],
        cwd=out_dir, capture_output=True, text=True,
    )
    if result.returncode != 0 or not (out_dir / "dump.toml").exists():
        sys.stderr.write(result.stdout + result.stderr)
        raise SystemExit(f"merge_mod_syms: --dump-context failed for {config}")


def split_sections(text: str) -> list[str]:
    """Return each `[[section]]` block's body (without the header line)."""
    parts = SECTION_SPLIT_RE.split(text)
    return [part.strip("\n") for part in parts[1:]]


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    n64recomp = Path(sys.argv[1]).resolve()
    out_dir = Path(sys.argv[2]).resolve()
    units = sys.argv[3:]
    out_dir.mkdir(parents=True, exist_ok=True)

    dump_context(n64recomp, ROOT / "config" / "config.toml", out_dir)
    main_text = (out_dir / "dump.toml").read_text()

    seen_roms: set[int] = set()
    seen_funcs: set[str] = set()
    for body in split_sections(main_text):
        m = ROM_RE.search(body)
        if m:
            seen_roms.add(int(m.group(1), 16))
        for line in body.splitlines():
            fm = FUNC_RE.match(line)
            if fm:
                seen_funcs.add(fm.group(1))

    appended: list[str] = []
    bank_roms: dict[int, int] = {}  # rom -> vram of each appended bank section
    num_sections = 0
    num_funcs = 0
    skipped: list[str] = []
    for unit in units:
        config = ROOT / "config" / "banks" / f"config-bank{unit}.toml"
        tmp = Path(tempfile.mkdtemp(prefix=f"modsyms-{unit}-"))
        try:
            dump_context(n64recomp, config, tmp, ROOT / "build" / f"bank{unit}.elf")
            unit_text = (tmp / "dump.toml").read_text()
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

        for body in split_sections(unit_text):
            rom_m = ROM_RE.search(body)
            name_m = NAME_RE.search(body)
            section_name = name_m.group(1) if name_m else "?"
            if not rom_m:
                skipped.append(f"unit {unit} {section_name}: no rom key")
                continue
            rom = int(rom_m.group(1), 16)
            if rom in seen_roms:
                skipped.append(f"unit {unit} {section_name}: rom 0x{rom:X} already present")
                continue
            seen_roms.add(rom)
            vram_m = VRAM_RE.search(body)
            bank_roms[rom] = int(vram_m.group(1), 16) if vram_m else 0

            lines: list[str] = []
            for line in body.splitlines():
                fm = FUNC_RE.match(line)
                if fm:
                    if fm.group(1) in seen_funcs:
                        skipped.append(f"unit {unit} {section_name}: function {fm.group(1)} already defined")
                        continue
                    seen_funcs.add(fm.group(1))
                    num_funcs += 1
                lines.append(line)
            # Tag the section with its unit so the merged file says where it came from.
            appended.append(f"# bank unit {unit} ({config.relative_to(ROOT).as_posix()})")
            appended.append("[[section]]")
            appended.extend(lines)
            appended.append("")
            num_sections += 1

    merged = main_text.rstrip("\n") + "\n\n" + "\n".join(appended)
    (out_dir / "dump.toml").write_text(merged)

    data_path = out_dir / "data_dump.toml"
    data_blocks = SECTION_SPLIT_RE.split(data_path.read_text())
    num_absolute = 0
    for i in range(1, len(data_blocks)):
        rom_m = ROM_RE.search(data_blocks[i])
        vram_m = VRAM_RE.search(data_blocks[i])
        if not rom_m or not vram_m:
            continue
        rom = int(rom_m.group(1), 16)
        if rom in bank_roms and int(vram_m.group(1), 16) != bank_roms[rom]:
            data_blocks[i] = ROM_RE.sub("", data_blocks[i], count=1)
            num_absolute += 1
    data_path.write_text("[[section]]".join(data_blocks))

    for line in skipped:
        print(f"merge_mod_syms: skipped {line}", file=sys.stderr)
    print(f"merge_mod_syms: {out_dir.name}/dump.toml: main + {len(units)} bank unit(s), "
          f"{num_sections} bank section(s), {num_funcs} bank function(s); "
          f"{num_absolute} blob data section(s) made absolute")
    return 0


if __name__ == "__main__":
    sys.exit(main())

