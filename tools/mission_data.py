#!/usr/bin/env python3
"""Decode every mission's enemy army from the ROM (War Director research R3/R4/R7/R9).

    tools/mission_data.py [rom] [out.md]   (defaults: assets/ogre64.z64, docs/mission-enemies.md)

Needs 7-Zip (`7z`) on PATH to unpack the game's LHA archives.

Where the data lives:

- The game's files are LHA (-lh5-) archives stored as assets. Each file's
  level-2 LHA header carries its name in an extension block (`0x01` + name).
- `enemydat.bin`: 556 enemy unit templates of 35 bytes, indexed from 1. The
  mission loader (bank R `func_ovlR_80215C38`) opens it as asset 0x021AE3D0
  and builds a unit from template `id` at `(id - 1) * 35`
  (`func_ovlR_80215360`). Template bytes: +0x00 leader class, +0x01 leader
  level offset, +0x02..+0x05 leader items, +0x06 leader square (1-based);
  group 1 at +0x07 (class, or 1 = generic soldiers), +0x08 level offset,
  +0x09..+0x0C items, +0x0D..+0x0F squares; group 2 at +0x10 with the same
  layout to +0x18. +0x19..+0x22 are zero in every template checked.
- `eset<chapter>_<mission>[a|b].bin`: one mission's data block. Four big-endian
  u16 offsets head it; the loader takes the second as the spawn list: a count
  byte, then 18-byte entries:
      +0x00 group, +0x01 ?, +0x02 unit slot (30..59 enemy),
      +0x03..+0x04 template id, +0x05/+0x06 map position (or a location index
      when +0x06 is 0), +0x08/+0x09 AI bytes (map object +0xBA/+0xBB),
      +0x0A -> unit record +0x17, +0x0C trigger flag and +0x0D..+0x0E a u16
      written to 0x8019369C[unit], +0x0F..+0x11 route bytes
      (func_ovlR_8021592C).
- Levels are offsets from the mission base level (byte 0x801F1055).

Story units: entry 0 (unit slot 30) is the mission boss, and boss and named
units use the duplicate story class ids (81 and up: Berserker 118, Phalanx
117, ...). A template is "story" when it is entry 0 or uses any class >= 81.
"""

import glob
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from collections import Counter, OrderedDict

ROM_PATH = sys.argv[1] if len(sys.argv) > 1 else "assets/ogre64.z64"
OUT_PATH = sys.argv[2] if len(sys.argv) > 2 else "docs/mission-enemies.md"

rom = open(ROM_PATH, "rb").read()

MISSIONS = OrderedDict([
    ("eset0_00", "Tutorial practice"),
    ("eset0_01", "1 Tenne Plains"), ("eset0_02", "2 Volmus Mine"),
    ("eset1_01", "3 Crenel Canyon"), ("eset1_02", "4 Mylesia"), ("eset1_03", "5 Zenobian Border"),
    ("eset1_04", "6 Volmus Mine"), ("eset1_05", "7 Gunther Piedmont"),
    ("eset1_06", "8 Dardunnelles"), ("eset1_07", "9 Alba"),
    ("eset2_01", "10 Crenel Canyon"), ("eset2_02", "11 Mylesia"), ("eset2_03", "12 Highlands of Soathon"),
    ("eset2_04", "13 Sable Lowlands"), ("eset2_05", "14 Audvera Heights"), ("eset2_06", "15 Mount Ithaca"),
    ("eset2_07", "16 Azure Plains"), ("eset2_08", "17 Mount Keryoleth"), ("eset2_09", "18 Wentinus"),
    ("eset3_01", "19 Dardunnelles"), ("eset3_02", "20 Gules Hills"), ("eset3_03", "21 Fair Heights"),
    ("eset3_04", "22 Vert Plateau"), ("eset3_05", "23 Tremos Mountains (South)"),
    ("eset3_06", "24 Capitrium"), ("eset3_07", "25 Tremos Mountains (North)"),
    ("eset3_08", "26 Celesis"), ("eset3_09", "27 Temple of Berthe"), ("eset3_10", "28 Temple of Berthe"),
    ("eset4_01", "29 Gules Hills"), ("eset4_02", "30 Romulus"), ("eset4_03", "31 Fort Romulus"),
    ("eset4_04", "32 The Blue Basilica"), ("eset4_05", "33 Ptia"), ("eset4_06", "34 Barpheth"),
    ("eset4_07", "35 Tundra of Argent"), ("eset4_08", "36 Tybell"), ("eset4_09", "37 Latium"),
    ("eset4_10", "38 Winnea"), ("eset4_11", "39 Aurua Plains"), ("eset4_12", "40 Wentinus"),
    ("eset4_13", "41 Castle Talpaea"), ("eset4_14", "42 Mount Keryoleth"), ("eset4_15", "43 Aurua Plains"),
    ("eset5_01", "Extra 1"), ("eset5_02", "Extra 2"), ("eset5_03", "Extra 3"), ("eset5_04", "Extra 4"),
])

STORY_CLASS_MIN = 81


def rom_of(vram):
    return vram - 0x8016AF80 + 0x40E80


def class_name(c):
    off = rom_of(0x80187C34 + c * 0x48)
    p = struct.unpack(">I", rom[off:off + 4])[0]
    if not (0x80180000 <= p < 0x80192000):
        return f"class {c}"
    q = rom_of(p)
    return rom[q:rom.index(b"\0", q)].decode("latin1").replace("\x81", " ").replace(" c", " ")


def generic_class(c):
    """A story duplicate (81+) mapped to the generic class with the same name."""
    if c < STORY_CLASS_MIN:
        return c
    name = class_name(c)
    for g in range(1, STORY_CLASS_MIN):
        if class_name(g) == name:
            return g
    return c


def extract_files(wanted, out_dir):
    """Unpack the named LHA members from the ROM into out_dir."""
    os.makedirs(out_dir, exist_ok=True)
    seven = shutil.which("7z") or shutil.which("7z.exe")
    if seven is None:
        sys.exit("mission_data.py: 7z is not on PATH")
    with tempfile.TemporaryDirectory() as tmp:
        for m in re.finditer(rb"-lh[05]-", rom):
            h = m.start() - 2
            name = re.search(rb"\x01([A-Za-z0-9_\-.]{2,40}\.[A-Za-z0-9]{1,4})", rom[h:h + 80])
            if not name or not wanted(name.group(1).decode()):
                continue
            header = struct.unpack("<H", rom[h:h + 2])[0]
            packed = struct.unpack("<I", rom[h + 7:h + 11])[0]
            path = os.path.join(tmp, name.group(1).decode() + ".lzh")
            open(path, "wb").write(rom[h:h + header + packed])
            subprocess.run([seven, "x", "-y", f"-o{out_dir}", path], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    files = os.path.join("build", "gamefiles", "lzhout")
    extract_files(lambda n: n == "enemydat.bin" or n.startswith("eset"), files)
    templates = open(os.path.join(files, "enemydat.bin"), "rb").read()

    def template(t):
        r = templates[(t - 1) * 35:t * 35]
        groups = []
        for cls, lvl, squares in ((r[7], r[8], r[0x0D:0x10]), (r[0x10], r[0x11], r[0x16:0x19])):
            n = sum(1 for s in squares if s)
            if n:
                groups.append((cls, lvl if lvl < 128 else lvl - 256, n))
        return r[0], r[1] if r[1] < 128 else r[1] - 256, groups

    out = ["# Enemy armies by mission\n",
           "Generated by `tools/mission_data.py` from the ROM. Do not edit by hand.\n",
           "Levels are offsets from the mission's base level. \"Story\" marks the mission boss",
           "(unit slot 30) and any unit using a story class id (81+). Generic pools list the",
           "classes the generic units use, with how many units use each.\n"]
    debut_generic = {}
    debut_story = {}
    sections = []

    for key, title in MISSIONS.items():
        paths = sorted(glob.glob(os.path.join(files, key + "*.bin")))
        if not paths:
            continue
        lines = [f"## {title} (`{key}`)\n"]
        pool = Counter()
        story_classes = set()
        seen_units = set()
        for path in paths:
            d = open(path, "rb").read()
            spawn = struct.unpack(">H", d[2:4])[0]
            count = d[spawn]
            variant = os.path.basename(path)[len(key):-4] or "-"
            lines.append(f"| var | slot | template | leader | members | where | story |")
            lines.append("|---|---|---|---|---|---|---|")
            for i in range(count):
                e = d[spawn + 1 + i * 18:spawn + 1 + (i + 1) * 18]
                slot = e[2] & 0x7F
                t = (e[3] << 8) | e[4]
                leader, lvl, groups = template(t)
                classes = [leader] + [g[0] for g in groups if g[0] != 1]
                story = i == 0 or any(c >= STORY_CLASS_MIN for c in classes)
                where = f"location {e[5]}" if e[6] == 0 else f"({e[5]},{e[6]})"
                members = ", ".join(
                    f"{n}x {'Soldier' if c == 1 else class_name(c)} ({'+' if l >= 0 else ''}{l})"
                    for c, l, n in groups) or "-"
                lines.append(f"| {variant} | {slot} | {t} | {class_name(leader)} (+{lvl}) | {members} | "
                             f"{where} | {'yes' if story else ''} |")
                unit_key = (t, slot)
                for c in sorted(set(classes)):
                    g = generic_class(c)
                    if story:
                        story_classes.add(g)
                        debut_story.setdefault(g, title)
                    else:
                        if unit_key not in seen_units:
                            pool[g] += 1
                        debut_generic.setdefault(g, title)
                if groups and any(g[0] == 1 for g in groups) and not story:
                    debut_generic.setdefault(1, title)
                seen_units.add(unit_key)
            lines.append("")
        pool_text = ", ".join(f"{class_name(c)} {n}" for c, n in pool.most_common()) or "-"
        story_only = sorted(story_classes - set(pool))
        lines.append(f"- **Generic pool:** {pool_text}")
        lines.append(f"- **Story-only classes here:** "
                     f"{', '.join(class_name(c) for c in story_only) or '-'}\n")
        sections.append("\n".join(lines))

    out.append("## Debut table\n")
    out.append("First mission where each class appears, in a generic unit and in a story unit.\n")
    out.append("| class | first generic | first story |")
    out.append("|---|---|---|")
    for c in sorted(set(debut_generic) | set(debut_story), key=lambda c: (
            list(MISSIONS.values()).index(debut_generic.get(c, debut_story.get(c))), c)):
        out.append(f"| {class_name(c)} | {debut_generic.get(c, '-')} | {debut_story.get(c, '-')} |")
    out.append("")
    out.extend(sections)
    open(OUT_PATH, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print(f"{len(sections)} missions -> {OUT_PATH}")


if __name__ == "__main__":
    main()

