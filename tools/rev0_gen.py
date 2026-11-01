#!/usr/bin/env python3
"""Generate US revision 0's recompiled code from revision 1's build, without a
MIPS toolchain.

Revision 1 is built the normal way (splat -> ELF -> N64Recomp). Revision 0 is
the same code at known offsets (docs/anotherworld/notes-revisions.md), so this tool:

1. dumps each unit's function context from revision 1's ELF
   (`N64Recomp <config> --dump-context`),
2. translates every section, function and relocation to revision 0 with
   build/revmap/rev1_rev0.tsv and the RAM rule below,
3. writes a symbol-file config per unit and runs N64Recomp on
   assets/ogre64-rev0.z64 into rev0/RecompiledFuncs and rev0/Bank<U>Funcs.

RAM rule (the game's segment table, ROM 0x387C0, agrees): a revision 1
address below 0x8016D068 is the same in revision 0; from 0x8016D220 up it is
0x20 lower. Functions that changed between the revisions take their revision 0
bounds from the gap between their matched neighbours, split at `jr ra`.

    tools/rev0_gen.py [--units main,A,B,...] [--dump-only]

Run tools/revmap.py first.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
N64RECOMP = ROOT / "tools/N64Recomp/build" / ("N64Recomp.exe" if sys.platform == "win32" else "N64Recomp")
REV0_ROM = ROOT / "assets/ogre64-rev0.z64"
CTX = ROOT / "build/rev0/ctx"
OUT = ROOT / "rev0"
BANK_UNITS = "A B C D E F G H I J K L M N O P Q R S T U V W X Y Z AA AB AC AD AE AF AG AH".split()

sys.path.insert(0, str(Path(__file__).resolve().parent))
from revmap import find as revmap_find  # noqa: E402

_REV1 = None


def rev1_rom() -> bytes:
    global _REV1
    if _REV1 is None:
        _REV1 = (ROOT / "assets/ogre64.z64").read_bytes()
    return _REV1


def vram0(v: int) -> int:
    if v < 0x8016D068 or v >= 0x80800000:
        return v
    if v >= 0x8016D220:
        return v - 0x20
    raise ValueError(f"0x{v:08X} is inside the changed save/load functions")


def load_revmap() -> dict[int, tuple[int, str]]:
    """rev1 ROM -> (rev0 ROM or -1, match kind)."""
    out = {}
    for line in (ROOT / "build/revmap/rev1_rev0.tsv").read_text(encoding="utf-8").splitlines():
        if line.startswith("#") or line.startswith("name"):
            continue
        _, rom1, _, rom0, _, kind = line.split("\t")
        out[int(rom1, 16)] = (int(rom0, 16) if rom0 != "-" else -1, kind)
    return out


def rom0(rom1: int) -> int:
    """Block rule for ROM offsets (sections, data): see notes-revisions.md."""
    if rom1 < 0x042F68:
        return rom1
    if rom1 < 0x1960C8 + 0x1650:
        return rom1 - 0x20
    return rom1 + 0x30


def unit_config(unit: str) -> Path:
    return ROOT / ("config/config.toml" if unit == "main" else f"config/banks/config-bank{unit}.toml")


def dump(unit: str) -> Path:
    where = CTX / unit
    where.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(N64RECOMP), str(unit_config(unit)), "--dump-context"], cwd=where,
                            capture_output=True, text=True)
    if result.returncode != 0 or not (where / "dump.toml").is_file():
        sys.exit(f"rev0_gen: dumping {unit} failed:\n{result.stdout[-2000:]}{result.stderr[-2000:]}")
    return where / "dump.toml"


SECTION = re.compile(r'\[\[section\]\]\nname = "([^"]+)"\n(?:rom = 0x([0-9A-F]+)\n)?vram = 0x([0-9A-F]+)\nsize = 0x([0-9A-F]+)\n')
FUNC = re.compile(r'\{ name = "([^"]+)", vram = 0x([0-9A-F]+), size = 0x([0-9A-F]+) \}')


def statics(func_dir: Path) -> list[tuple[str, int, int]]:
    """(name, vram, size) of every static function in a unit's generated C. Its
    size runs to the last instruction comment in its body."""
    out = []
    head = re.compile(r"^RECOMP_FUNC void (static_\w+?_([0-9A-F]{8}))\(", re.M)
    for c in sorted(func_dir.glob("*.c")):
        text = c.read_text(encoding="utf-8", errors="replace")
        heads = list(head.finditer(text))
        for i, m in enumerate(heads):
            body = text[m.end():heads[i + 1].start() if i + 1 < len(heads) else len(text)]
            nxt = body.find("\nRECOMP_FUNC ")
            if nxt >= 0:
                body = body[:nxt]
            addrs = [int(a, 16) for a in re.findall(r"// 0x([0-9A-F]{8}):", body)]
            start = int(m.group(2), 16)
            # An empty body (a jump target right before data) stays size 0.
            out.append((m.group(1), start, max(addrs) + 4 - start if addrs else 0))
    return out


def split_at_returns(rom: bytes, start: int, end: int, count: int) -> list[tuple[int, int]]:
    """Split [start, end) into `count` functions after each `jr ra` + delay slot."""
    cuts = [start]
    at = start
    while at < end and len(cuts) < count:
        if int.from_bytes(rom[at:at + 4], "big") == 0x03E00008:
            nxt = at + 8
            while nxt < end and int.from_bytes(rom[nxt:nxt + 4], "big") == 0:
                nxt += 4
            if nxt < end:
                cuts.append(nxt)
            at = nxt
            continue
        at += 4
    if len(cuts) != count:
        raise ValueError(f"could not split 0x{start:X}..0x{end:X} into {count} functions")
    cuts.append(end)
    return [(cuts[i], cuts[i + 1] - cuts[i]) for i in range(count)]


def translate(dump_path: Path, revmap: dict, rev0: bytes, extra: list) -> tuple[str, list[str]]:
    text = dump_path.read_text(encoding="utf-8")
    out = ["# Revision 0 context, translated from revision 1 by tools/rev0_gen.py\n"]
    notes = []
    blocks = text.split("[[section]]")[1:]
    for block in blocks:
        block = "[[section]]" + block
        m = SECTION.match(block)
        if not m:
            raise ValueError("unexpected section layout in dump")
        name, rom_s, vram_s, size_s = m.groups()
        vram1, size1 = int(vram_s, 16), int(size_s, 16)
        rom1 = int(rom_s, 16) if rom_s else None
        funcs = [(n, int(v, 16), int(s, 16)) for n, v, s in FUNC.findall(block)]
        if rom1 is None:
            continue
        # Static functions N64Recomp discovered in revision 1 (bounded there by
        # data symbols that a symbol file does not carry): pass them explicitly.
        known = {v for _, v, _ in funcs}
        for fname, v, fsize in extra:
            if vram1 <= v < vram1 + size1 and v not in known:
                funcs.append((fname, v, fsize))
                known.add(v)
        funcs.sort(key=lambda f: f[1])
        sec_rom0, sec_vram0 = rom0(rom1), vram0(vram1)
        size0 = rom0(rom1 + size1 - 4) + 4 - sec_rom0

        # Revision 0 bounds of every function: matched ones from the map, changed
        # runs from the gap between their matched neighbours.
        bounds = {}
        changed = []
        delta = sec_rom0 - rom1
        for i, (fname, v, s) in enumerate(funcs):
            r1 = rom1 + (v - vram1)
            r0, kind = revmap.get(r1, (-1, "none"))
            if kind == "none" and r1 not in revmap:
                # Not in the recompiled tables (libultra the runtime replaces):
                # look it up here the same way tools/revmap.py does.
                r0, kind = revmap_find(rev1_rom(), rev0, r1, s, rom0(r1))
                if kind == "none" or abs(r0 - rom0(r1)) > 0x40:
                    # A small static that matches nothing nearby is data-shaped;
                    # place it by the block rule rather than at a lookalike far away.
                    r0, kind = rom0(r1), "masked"
            if kind in ("exact", "masked"):
                bounds[i] = (r0, s)
                delta = r0 - r1
            else:
                changed.append(i)
        runs, run = [], []
        for i in changed:
            if run and i != run[-1] + 1:
                runs.append(run)
                run = []
            run.append(i)
        if run:
            runs.append(run)
        for run in runs:
            first, last = run[0], run[-1]
            start = bounds[first - 1][0] + bounds[first - 1][1] if first > 0 else sec_rom0
            end = bounds[last + 1][0] if last + 1 in bounds else sec_rom0 + size0
            for i, (r0, s0) in zip(run, split_at_returns(rev0, start, end, len(run))):
                # A function that ends in `jr ra` + delay slot in revision 1 ends the
                # same way in revision 0; anything after its last return is data.
                r1_end = rom1 + (funcs[i][1] - vram1) + funcs[i][2]
                if int.from_bytes(rev1_rom()[r1_end - 8:r1_end - 4], "big") == 0x03E00008:
                    rets = [a for a in range(r0, r0 + s0, 4) if int.from_bytes(rev0[a:a + 4], "big") == 0x03E00008]
                    if rets:
                        s0 = rets[-1] + 8 - r0
                bounds[i] = (r0, s0)
                # A static that starts where this function ends in revision 1 (a jump
                # target at the function's end) starts where it ends in revision 0.
                prev, end0 = i, r0 + s0
                while (prev + 1 < len(funcs) and funcs[prev + 1][0].startswith("static_")
                        and funcs[prev + 1][1] == funcs[prev][1] + funcs[prev][2]):
                    prev += 1
                    bounds[prev] = (end0, funcs[prev][2])
                    end0 += funcs[prev][2]
                notes.append(f"{funcs[i][0]}: changed, rev0 ROM 0x{r0:06X} size 0x{s0:X} (rev1 size 0x{funcs[i][2]:X})")

        changed_ranges = [(funcs[i][1], funcs[i][1] + funcs[i][2]) for run in runs for i in run]

        # No `relocs` key: N64Recomp makes any section with one relocatable, and the
        # dump keeps only relocations within a section, so calls between sections
        # would have no target. The game loads every section at its own address
        # (the bank units are built the same way), so absolute code is correct.
        out.append(f'[[section]]\nname = "{name}"\nrom = 0x{sec_rom0:08X}\nvram = 0x{sec_vram0:08X}\nsize = 0x{size0:X}\n\n')
        out.append("functions = [\n")
        for i, (fname, v, s) in enumerate(funcs):
            r0, s0 = bounds[i]
            out.append(f'    {{ name = "{fname}", vram = 0x{sec_vram0 + (r0 - sec_rom0):08X}, size = 0x{s0:X} }},\n')
        out.append("]\n\n")
    return "".join(out), notes


def placeholders(unit: str) -> str:
    """Zero-size functions for the absolute call targets the linker script
    defines (calls into code outside this unit). N64Recomp emits a runtime
    lookup for a call to a zero-size function, as it does in ELF mode."""
    base = ROOT / ("config/symbols" if unit == "main" else f"build/bank{unit}")
    funcs_txt = (base / "undefined_funcs_auto.txt").read_text(encoding="utf-8")
    extra = base / "extra_syms.txt"
    extra_txt = extra.read_text(encoding="utf-8") if extra.is_file() else ""
    entries = re.findall(r"^\s*([A-Za-z_]\w*)\s*=\s*0x([0-9A-Fa-f]+);", funcs_txt, re.M)
    entries += [e for e in re.findall(r"^\s*(func_\w+)\s*=\s*0x([0-9A-Fa-f]+);", extra_txt, re.M)]
    out = []
    seen = set()
    for name, addr in entries:
        if name in seen or int(addr, 16) < 0x80000400:
            continue
        seen.add(name)
        m = re.match(r"(\w+) (\w+)", f"{name} {addr}")
        name, v = m.group(1), vram0(int(m.group(2), 16))
        out.append(f'[[section]]\nname = ".placeholder_{v:08X}"\nrom = 0x00001000\nvram = 0x{v:08X}\nsize = 0x0\n\n'
                   f'functions = [\n    {{ name = "{name}", vram = 0x{v:08X}, size = 0x0 }},\n]\n\n')
    return "".join(out)


def write_config(unit: str, ctx: Path) -> Path:
    src = unit_config(unit).read_text(encoding="utf-8")
    out_dir = OUT / ("RecompiledFuncs" if unit == "main" else f"Bank{unit}Funcs")
    keep = []
    in_input = False
    for line in src.splitlines():
        if line.strip().startswith("["):
            in_input = line.strip() == "[input]"
            keep.append(line)
            if in_input:
                keep.append(f'symbols_file_path = "{ctx.as_posix()}"')
                keep.append(f'rom_file_path = "{REV0_ROM.as_posix()}"')
                keep.append(f'output_func_path = "{out_dir.as_posix()}"')
            continue
        if in_input and re.match(r"\s*(elf_path|rom_file_path|output_func_path|relocatable_sections_path)\s*=", line):
            continue
        keep.append(line)
    # function_sizes stays: the sizes are already in the context, and N64Recomp
    # (patched, symbol-file mode) uses the list to mark size-overridden bodies.
    text = "\n".join(keep) + "\n"
    path = CTX / unit / "rev0.toml"
    path.write_text(text, encoding="utf-8")
    return path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--units", default="main," + ",".join(BANK_UNITS))
    ap.add_argument("--dump-only", action="store_true")
    args = ap.parse_args()
    revmap = load_revmap()
    rev0 = REV0_ROM.read_bytes()
    for unit in args.units.split(","):
        ctx_in = dump(unit)
        text, notes = translate(ctx_in, revmap, rev0,
                                statics(ROOT / ("RecompiledFuncs" if unit == "main" else f"Bank{unit}Funcs")))
        text += placeholders(unit)
        ctx_out = CTX / unit / "rev0_context.toml"
        ctx_out.write_text(text, encoding="utf-8")
        for note in notes:
            print(f"[{unit}] {note}")
        if args.dump_only:
            continue
        cfg = write_config(unit, ctx_out)
        out_dir = OUT / ("RecompiledFuncs" if unit == "main" else f"Bank{unit}Funcs")
        if out_dir.exists():
            for f in out_dir.iterdir():
                f.unlink()
        result = subprocess.run([str(N64RECOMP), str(cfg)], cwd=CTX / unit, capture_output=True, text=True)
        tail = (result.stdout + result.stderr).strip().splitlines()[-3:]
        status = "ok" if result.returncode == 0 else f"FAILED ({result.returncode})"
        print(f"[{unit}] N64Recomp {status}: " + " | ".join(tail))
        if result.returncode != 0:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

