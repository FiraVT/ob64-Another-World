#!/usr/bin/env python3
"""Export a class's art from the ROM as PNG files, the starting point for a
new class's own art (docs/anotherworld/design-class-art.md).

    python tools/export_class_art.py <rom> <class id> <out dir> [--hue DEGREES [--only LOW-HIGH]]

Writes:
  portrait.png            40x48, the class's portrait in its palette variant
  figure-front.png        the formation figure as enemies show it (front view)
  figure-back.png         the formation figure as the army shows it (back view)
  battle/descriptor<d>-bank<b>.png
                          the battle sprite's palettes: one 16x16 swatch per
                          bank (pixel i = colour i), for each of the class's
                          battle descriptors in handle order

With --hue a colour is turned by that many degrees on the colour wheel,
keeping its brightness and saturation, so the files are a recolor of the class
to start from; --only limits it to colours whose hue is in LOW-HIGH degrees
(e.g. 190-260 for the blues), so skin and gold stay. Clear pixels stay clear. The battle sprite is not exported yet.

Sources (US revision 1 ROM; asset id -> ROM (id & 0x0FFFFFFF) + 0x594250):
  appearance row  ROM 0x64C00, 6 bytes per class: +0 portrait halfword
                  (index << 3 | palette variant), +4 figure model
                  (& 0x3F; 0x80 the special lane)
  class record    ROM 0x5DB34, 0x48 bytes per class: +4 size (2 = large lane)
  portrait ids    asset 0x01DFB36E, raw u32 ids (entry 0 unused); a portrait
                  decodes to a 40x48 CI8 image (0x780 bytes) and 80-colour
                  RGBA5551 palettes of 0xA0 bytes, one per variant
  atlases         two 256-colour RGBA5551 palettes (0 enemy, 1 army) at 0x000
                  and 0x200, then one CI8 plane per model at 0x400; the enemy
                  front view uses palette 0, the army back view palette 1
                  (rempred/LordlyCaliber army-sprites.js; ids here are its
                  revision 0 keys minus 0x37C, each found by decoded size)
  battle          the class animation resource (asset 0x00315736, raw) holds 4
                  u16 handles per class at 0x24 + (class * 4 + body) * 2; a
                  handle's low 12 bits minus 1 index the descriptor table
                  (asset 0x003B6CD0, raw, 208 u32 asset ids); a descriptor
                  (raw) is a list of asset ids whose member 3 is its palette
                  resource (LZ): banks of 256 RGBA5551 colours, 0x200 bytes
                  each (rempred/LordlyCaliber animation-art.js; same ids on
                  revision 1)
"""

from __future__ import annotations

import argparse
import colorsys
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ogrelz  # noqa: E402

from PIL import Image

ASSET_BASE = 0x594250
APPEARANCE_ROM = 0x64C00
CLASS_ROM = 0x5DB34
PORTRAIT_IDS = 0x01DFB36E
CLASS_ANIMATION = 0x00315736
DESCRIPTORS = 0x003B6CD0

# lane -> (front asset, back asset, width, height)
ATLASES = {
    "ordinary": (0x01DBAF30, 0x01DBEB94, 16, 24),
    "special": (0x01DC2FA4, 0x01DC5AF0, 16, 24),
    "large": (0x01DB4424, 0x01DB7BE0, 32, 28),
}


def asset(rom: bytes, asset_id: int, raw: bool = False) -> bytes:
    at = (asset_id & 0x0FFFFFFF) + ASSET_BASE
    if raw:
        size = int.from_bytes(rom[at:at + 4], "big")
        return rom[at + 4:at + 4 + size]
    data, _ = ogrelz.decompress(rom, at + 4)  # the asset's size word, then the LZ block
    return data


HUE_RANGE = (0.0, 360.0)


def colour(word: int, hue: float) -> tuple[int, int, int, int]:
    """An RGBA5551 word as RGBA8888, turned by `hue` degrees when its hue is in
    HUE_RANGE and it is not grey."""
    def expand(five: int) -> int:
        return (five << 3) | (five >> 2)
    r, g, b = expand((word >> 11) & 31), expand((word >> 6) & 31), expand((word >> 1) & 31)
    a = 255 if word & 1 else 0
    if hue:
        h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
        if s > 0.12 and HUE_RANGE[0] <= h * 360 <= HUE_RANGE[1]:
            r, g, b = (round(c * 255) for c in colorsys.hsv_to_rgb((h + hue / 360) % 1.0, s, v))
    return r, g, b, a


def image(indices: bytes, width: int, height: int, palette: bytes, hue: float, opaque: bool) -> Image.Image:
    out = Image.new("RGBA", (width, height))
    for y in range(height):
        for x in range(width):
            i = indices[y * width + x]
            word = int.from_bytes(palette[i * 2:i * 2 + 2], "big") if i * 2 + 2 <= len(palette) else 0
            r, g, b, a = colour(word, hue)
            out.putpixel((x, y), (r, g, b, 255 if opaque else a))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("rom")
    ap.add_argument("cls", type=int)
    ap.add_argument("out")
    ap.add_argument("--hue", type=float, default=0.0)
    ap.add_argument("--only", help="LOW-HIGH: turn only colours whose hue is in this range (degrees)")
    args = ap.parse_args()
    global HUE_RANGE
    if args.only:
        low, high = (float(x) for x in args.only.split("-"))
        HUE_RANGE = (low, high)
    rom = Path(args.rom).read_bytes()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    row = rom[APPEARANCE_ROM + args.cls * 6:APPEARANCE_ROM + args.cls * 6 + 6]

    # Portrait: the class's image in its palette variant. Portraits are opaque
    # (the game draws them over a frame).
    v = int.from_bytes(row[0:2], "big")
    ids = asset(rom, PORTRAIT_IDS, raw=True)
    index, variant = v >> 3, v & 7
    if v and index and index * 4 + 4 <= len(ids):
        data = asset(rom, int.from_bytes(ids[index * 4:index * 4 + 4], "big"))
        palette = data[0x780 + variant * 0xA0:0x780 + (variant + 1) * 0xA0]
        image(data[:0x780], 40, 48, palette, args.hue, True).save(out / "portrait.png")
        print(f"portrait.png: portrait {index}, variant {variant}")
    else:
        print("the class has no portrait")

    # Formation figure: the model in the class's lane, front (enemy palette)
    # and back (army palette).
    size = rom[CLASS_ROM + args.cls * 0x48 + 4]
    route = row[4]
    lane = "large" if size == 2 else "special" if route & 0x80 else "ordinary"
    model = route & 0x3F
    front_id, back_id, w, h = ATLASES[lane]
    for name, asset_id, palette_no in (("figure-front.png", front_id, 0), ("figure-back.png", back_id, 1)):
        data = asset(rom, asset_id)
        plane = 0x400 + model * w * h
        if plane + w * h > len(data):
            print(f"{name}: model {model} is past the {lane} atlas")
            continue
        palette = data[palette_no * 0x200:(palette_no + 1) * 0x200]
        image(data[plane:plane + w * h], w, h, palette, args.hue, False).save(out / name)
        print(f"{name}: {lane} lane, model {model}, {w}x{h}")

    # Battle palettes: each distinct descriptor of the class's four handles.
    anim = asset(rom, CLASS_ANIMATION, raw=True)
    table = asset(rom, DESCRIPTORS, raw=True)
    slots = []
    for body in range(4):
        at = 0x24 + (args.cls * 4 + body) * 2
        slot = (int.from_bytes(anim[at:at + 2], "big") & 0xFFF) - 1
        if slot >= 0 and slot not in slots:
            slots.append(slot)
    (out / "battle").mkdir(exist_ok=True)
    for d, slot in enumerate(slots):
        descriptor = asset(rom, int.from_bytes(table[slot * 4:slot * 4 + 4], "big"), raw=True)
        lookup = asset(rom, int.from_bytes(descriptor[12:16], "big"))
        for b in range(len(lookup) // 0x200):
            swatch = Image.new("RGBA", (16, 16))
            for i in range(256):
                word = int.from_bytes(lookup[b * 0x200 + i * 2:b * 0x200 + i * 2 + 2], "big")
                r, g, b_, _ = colour(word, args.hue)
                swatch.putpixel((i % 16, i // 16), (r, g, b_, 255))
            swatch.save(out / "battle" / f"descriptor{d}-bank{b}.png")
        print(f"battle/descriptor{d}-bank*.png: descriptor {slot}, {len(lookup) // 0x200} banks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
