#!/usr/bin/env python3
"""Build an RT64 HD texture pack from a texture dump.

    OGRE_NO_AUDIO=1 OGRE_DUMP_TEX=<dump dir> build-dist/ogrebattle64.exe   # play, then quit
    python tools/hd_textures.py <dump dir> build-dist/texture-packs [--scale 4] [--method pixel|smooth]
                                [--fonts build-dist/fonts]

Writes `texture-packs/hd/` (every texture except the font glyphs) and one text
pack per font under `texture-packs/text/<name>/`: `original` (the game's own
glyphs, pixel-upscaled) and one for each .ttf/.otf in the fonts folder. The
game loads `hd` and the one text pack named by Settings > Graphics > TEXT FONT.
To try a font: copy it into build-dist/fonts and run this again.

The dump (RT64 `TextureManager::dumpTexture`) holds, per texture, the whole
4 KiB TMEM as `<hash>.v<N>.tmem` and the tile as `<hash>.v<N>.tile.json`
(`fmt`, `siz`, `line`, `tmem`, `palette`, `width`, `height`, `tlut`). Each one
is decoded here with a port of RT64's own `sampleTMEM`
(`src/shaders/TextureDecoder.hlsli`, `Formats.hlsli`), so the colours are the
renderer's, then upscaled and written as `<hash>.png`. `rt64.json` maps each
RT64 hash to its file (TEXTURE-PACKS.md, configuration version 3).

Methods:
  pixel   Scale2x applied until the scale is reached (2, 4 or 8). Keeps hard
          pixel-art edges and palette colours; for sprites, portraits, text.
  smooth  Lanczos resampling; for photographic backdrops.

Text (--font): the dialogue font is 166 glyphs of 11x14 at 2 bits per pixel,
asset 0x0218C17E (glyph = ASCII + 16; 0..31 are symbols), widths in assets
0x0218C0D4/0x0218DACC (func_8017909C). The text engine composites each glyph
into a CI4 cell with its own palette and draws one rectangle per glyph. A
dumped texture whose ink matches a ROM glyph is replaced by that character
rendered from a TrueType font at the pack's scale, fitted to the ROM glyph's
ink box (so spacing is unchanged), filled with the cell's body colour and
with its shadow one pixel right and down in the shadow colour.

YUV tiles (the njpeg backdrops) are skipped: their source is not in TMEM alone.
The game's own folder under `texture-packs/` loads at start while
Settings > Graphics > HD TEXTURES is ON (app/src/renderer.cpp).
"""

import argparse
import shutil
import subprocess
import json
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

G_IM_FMT_RGBA, G_IM_FMT_YUV, G_IM_FMT_CI, G_IM_FMT_IA, G_IM_FMT_I = 0, 1, 2, 3, 4
SIZ_4B, SIZ_8B, SIZ_16B, SIZ_32B = 0, 1, 2, 3
TMEM_BYTES = 0x1000
TMEM_PALETTE = 0x800
MASK8 = 0xFFF
MASK16 = 0x7FF


def rgba16(v):
    r, g, b = (v >> 11) & 0x1F, (v >> 6) & 0x1F, (v >> 1) & 0x1F
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255 if v & 1 else 0)


def ia16(v):
    i = (v >> 8) & 0xFF
    return (i, i, i, v & 0xFF)


def decode(tmem, tile):
    """RT64's sampleTMEM for every texel of the tile; returns an RGBA image."""
    fmt, siz = tile["fmt"], tile["siz"]
    width, height = tile["width"], tile["height"]
    stride = tile["line"] << 3
    address = tile["tmem"] << 3
    palette = tile.get("palette", 0)
    tlut = {"RGBA16": 2, "IA16": 3}.get(tile.get("tlut", "None"), 0)
    if fmt == G_IM_FMT_YUV and siz == SIZ_16B and tlut == 0:
        return None
    is_rgba32 = fmt == G_IM_FMT_RGBA and siz == SIZ_32B
    shift = 2 if is_rgba32 else siz
    mask = MASK16 if (is_rgba32 or tlut) else MASK8
    row = max(stride, 1)

    def load(rel, odd_row, or_address=0):
        row_start = (rel // row) * row
        word = (rel - row_start) // 4
        final = address + row_start + ((word ^ 1) * 4) + (rel & 3) if odd_row else address + rel
        return tmem[(((final & mask) | or_address) & MASK8)]

    out = Image.new("RGBA", (width, height))
    px = out.load()
    for y in range(height):
        odd_row = (y & 1) == 1
        for x in range(width):
            addr = y * stride + ((x << shift) >> 1)
            p0 = load(addr, odd_row)
            p1 = load(addr + 1, odd_row)
            p4 = (p0 >> (0 if x & 1 else 4)) & 0xF
            if tlut:
                pal = TMEM_PALETTE + (palette << 7) + (p4 << 3) if siz == SIZ_4B else TMEM_PALETTE + (p0 << 3)
                value = tmem[(pal + 1) & MASK8] | (tmem[pal & MASK8] << 8)
                px[x, y] = rgba16(value) if tlut == 2 else ia16(value)
                continue
            if siz == SIZ_4B:
                if fmt == G_IM_FMT_CI:
                    i = (palette << 4) | p4
                    px[x, y] = (i, i, i, i)
                elif fmt == G_IM_FMT_IA:
                    i = p4 & 0b1110
                    i = ((i << 4) | (i << 1) | (i >> 2)) & 0xFF
                    px[x, y] = (i, i, i, 255 if p4 & 1 else 0)
                else:
                    i = (p4 << 4) | p4
                    px[x, y] = (i, i, i, i)
            elif siz == SIZ_8B:
                if fmt == G_IM_FMT_IA:
                    i, a = (p0 >> 4) & 0xF, p0 & 0xF
                    px[x, y] = ((i << 4) | i,) * 3 + ((a << 4) | a,)
                else:
                    px[x, y] = (p0, p0, p0, p0)
            elif siz == SIZ_16B:
                value = p1 | (p0 << 8)
                if fmt == G_IM_FMT_RGBA:
                    px[x, y] = rgba16(value)
                elif fmt == G_IM_FMT_IA:
                    px[x, y] = ia16(value)
                else:
                    px[x, y] = (p0, p1, p0, p1)
            else:
                addr2 = addr if is_rgba32 else addr + 2
                or_address = TMEM_BYTES >> 1 if is_rgba32 else 0
                p2 = load(addr2, odd_row, or_address)
                p3 = load(addr2 + 1, odd_row, or_address)
                if fmt == G_IM_FMT_RGBA:
                    px[x, y] = (p0, p1, p2, p3)
                else:
                    px[x, y] = (p0, p1, p0, p1) if x & 1 else (p2, p3, p2, p3)
    return out


def scale2x(image):
    """EPX/Scale2x: each pixel becomes 2x2, taking a neighbour's colour only
    where two neighbours agree, so edges stay sharp and no new colours appear."""
    w, h = image.size
    src = image.load()
    out = Image.new("RGBA", (w * 2, h * 2))
    dst = out.load()
    for y in range(h):
        for x in range(w):
            p = src[x, y]
            a = src[x, y - 1] if y > 0 else p
            b = src[x + 1, y] if x < w - 1 else p
            c = src[x - 1, y] if x > 0 else p
            d = src[x, y + 1] if y < h - 1 else p
            e0 = a if (c == a and c != d and a != b) else p
            e1 = b if (a == b and a != c and b != d) else p
            e2 = c if (d == c and d != b and c != a) else p
            e3 = d if (b == d and b != a and d != c) else p
            dst[2 * x, 2 * y] = e0
            dst[2 * x + 1, 2 * y] = e1
            dst[2 * x, 2 * y + 1] = e2
            dst[2 * x + 1, 2 * y + 1] = e3
    return out


def upscale(image, scale, method):
    if method == "smooth":
        return image.resize((image.width * scale, image.height * scale), Image.LANCZOS)
    while scale > 1:
        image = scale2x(image)
        scale //= 2
    return image


GLYPH_ASSET = 0x0218C17E
GLYPH_W, GLYPH_H, GLYPH_COUNT = 11, 14, 166
SYMBOLS = "「」『』…~´“@【】±×∞♂♀"           "※-▲▼◀▶○△□■♪♥"


def glyph_char(index):
    if 48 <= index < 48 + 95:
        return chr(index - 16)
    if index < len(SYMBOLS):
        return SYMBOLS[index]
    return None


def rom_glyphs(rom_path):
    """The ROM font: index -> 11x14 list of 2-bit values (stored uncompressed
    after the asset's size word, ROM = (id & 0x0FFFFFFF) + 0x594250)."""
    rom = open(rom_path, "rb").read()
    at = (GLYPH_ASSET & 0x0FFFFFFF) + 0x594250 + 4
    glyphs = {}
    for g in range(GLYPH_COUNT):
        data = rom[at + g * 39: at + (g + 1) * 39]
        bits = "".join(f"{b:08b}" for b in data)
        px = [int(bits[2 * i: 2 * i + 2], 2) for i in range(GLYPH_W * GLYPH_H)]
        if any(px) and glyph_char(g):
            glyphs[g] = [px[y * GLYPH_W:(y + 1) * GLYPH_W] for y in range(GLYPH_H)]
    return glyphs


def match_glyph(image, glyphs):
    """The ROM glyph whose ink is exactly the texture's opaque ink, allowing a
    one-pixel offset; returns (index, dx, dy) or None."""
    w, h = image.size
    if not (GLYPH_W <= w <= 16 and GLYPH_H <= h <= 16):
        return None
    px = image.load()
    # A glyph cell is loaded as 16x16, so its last rows are the cell's own
    # palette bytes (`+0x60`); only the 11x14 glyph area is compared.
    ink = {(x, y) for y in range(min(h, GLYPH_H)) for x in range(min(w, GLYPH_W + 1)) if px[x, y][3] > 0}
    if len(ink) < 2:
        return None
    for g, rows in glyphs.items():
        for dy in (0, 1, -1):
            for dx in (0, 1, -1):
                mask = {(x + dx, y + dy) for y in range(GLYPH_H) for x in range(GLYPH_W) if rows[y][x]}
                if mask == ink:
                    return g, dx, dy
    return None


def render_glyph(image, rows, dx, dy, char, font_path, scale, shadow_offset=0.5, shadow_opacity=0.6, bold=1):
    """The character from the TrueType font at `scale`, fitted to the ROM
    glyph's stroke (2-bit value 3), in the cell's own colours. The ROM glyph's
    value 1 is a shadow one pixel right and down (the embossed look of the
    game's text), drawn here as the same shape offset by one source pixel."""
    px = image.load()
    stroke = [(x, y) for y in range(GLYPH_H) for x in range(GLYPH_W) if rows[y][x] == 3]
    shadow = [(x, y) for y in range(GLYPH_H) for x in range(GLYPH_W) if rows[y][x] in (1, 2)]
    if not stroke:
        stroke, shadow = shadow, []
    body_colours = [px[x + dx, y + dy] for x, y in stroke]
    shadow_colours = [px[x + dx, y + dy] for x, y in shadow]
    body_colour = max(set(body_colours), key=body_colours.count)
    shadow_colour = max(set(shadow_colours), key=shadow_colours.count) if shadow_colours else None
    box = (min(x for x, _ in stroke) + dx, min(y for _, y in stroke) + dy,
           max(x for x, _ in stroke) + dx + 1, max(y for _, y in stroke) + dy + 1)
    big = ImageFont.truetype(font_path, 256)
    probe = Image.new("L", (512, 512), 0)
    ImageDraw.Draw(probe).text((128, 64), char, font=big, fill=255)
    ink_box = probe.getbbox()
    out = Image.new("RGBA", (image.width * scale, image.height * scale), (0, 0, 0, 0))
    if ink_box is None:
        return out
    # The letter keeps close to its own proportions: its ink is scaled to the
    # ROM glyph's stroke height, and its width is its own, held between 100%
    # and 115% of the ROM stroke box: the game spaces glyphs by the original
    # widths, so a narrower letter leaves a visible gap (developer, in game). It starts at the box's left edge, as the game's glyphs do.
    from PIL import ImageFilter
    ink = probe.crop(ink_box)
    box_w, box_h = (box[2] - box[0]) * scale, (box[3] - box[1]) * scale
    height = max(1, box_h)
    width = round(ink.width * height / ink.height)
    width = max(1, min(max(width, box_w), round(box_w * 1.15), out.width))
    alpha = ink.resize((width, height), Image.LANCZOS)
    if bold > 0:
        alpha = alpha.filter(ImageFilter.MaxFilter(2 * bold + 1))
    left = max(0, min(box[0] * scale, out.width - alpha.width))
    layer = Image.new("L", out.size, 0)
    layer.paste(alpha, (left, box[1] * scale))
    if shadow_colour and shadow_offset > 0:
        # The game's shadow is one source pixel right and down; at HD that
        # reads as a smear, so it is offset by `shadow_offset` source pixels and
        # drawn at `shadow_opacity`.
        step = max(1, round(scale * shadow_offset))
        offset = Image.new("L", out.size, 0)
        offset.paste(alpha.point(lambda v: int(v * shadow_opacity)), (left + step, box[1] * scale + step))
        out.paste(Image.new("RGBA", out.size, shadow_colour), (0, 0), offset)
    out.paste(Image.new("RGBA", out.size, body_colour), (0, 0), layer)
    return out


def save_dds(image, path):
    """An uncompressed R8G8B8A8_UNORM DDS (DX10 header) with a full mip chain.

    The game draws text with point sampling, and RT64 point-samples a
    replacement the same way unless it has mipmaps: a 4x glyph drawn into a 2x
    frame then keeps every other texel, which breaks the strokes. With mipmaps
    RT64 picks the level that matches the on-screen size and blends two
    (TextureSampler.hlsli, `flagHasMipmaps`). PNG packs load with one level, so
    text packs are written as DDS. Levels are reduced with premultiplied alpha
    so edges do not darken."""
    levels = [image.convert("RGBA")]
    while levels[-1].width > 1 or levels[-1].height > 1:
        w = max(1, levels[-1].width // 2)
        h = max(1, levels[-1].height // 2)
        levels.append(levels[-1].convert("RGBa").resize((w, h), Image.BOX).convert("RGBA"))
    width, height = image.size
    DDSD_CAPS, DDSD_HEIGHT, DDSD_WIDTH, DDSD_PITCH, DDSD_PIXELFORMAT, DDSD_MIPMAPCOUNT = 0x1, 0x2, 0x4, 0x8, 0x1000, 0x20000
    flags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PITCH | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT
    header = struct.pack("<4sIIIIIII44x", b"DDS ", 124, flags, height, width, width * 4, 0, len(levels))
    header += struct.pack("<II4sIIIII", 32, 0x4, b"DX10", 0, 0, 0, 0, 0)  # pixel format: FourCC DX10
    header += struct.pack("<IIIII", 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)    # caps: texture, mipmap, complex
    header += struct.pack("<IIIII", 28, 3, 0, 1, 0)                         # R8G8B8A8_UNORM, 2D, 1 array
    with open(path, "wb") as f:
        f.write(header)
        for level in levels:
            f.write(level.tobytes())


def font_slug(path):
    name = os.path.splitext(os.path.basename(path))[0].lower()
    return "".join(c if c.isalnum() else "-" for c in name).strip("-") or "font"


def write_pack(folder, entries, hash_version):
    os.makedirs(folder, exist_ok=True)
    database = {
        "configuration": {
            "autoPath": "rt64",
            "configurationVersion": 3,
            "hashVersion": hash_version,
            "defaultOperation": "stream",
            "defaultShift": "half",
        },
        "operationFilters": [],
        "shiftFilters": [],
        "textures": [{"hashes": {"rt64": h}, "path": h} for h in entries],
    }
    with open(os.path.join(folder, "rt64.json"), "w") as f:
        json.dump(database, f, indent=4)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dump")
    parser.add_argument("packs", help="the texture-packs folder (for example build-dist/texture-packs)")
    parser.add_argument("--scale", type=int, default=4, choices=(2, 4, 8))
    parser.add_argument("--method", default="pixel", choices=("pixel", "smooth"))
    parser.add_argument("--fonts", default="build-dist/fonts", help="folder of .ttf/.otf fonts, one text pack each")
    parser.add_argument("--rom", default="assets/ogre64.z64", help="the ROM, for the font glyphs")
    parser.add_argument("--shadow", type=float, default=0.5, help="text shadow offset in source pixels (0 = none)")
    parser.add_argument("--shadow-opacity", type=float, default=0.6, help="text shadow opacity, 0..1")
    parser.add_argument("--packer", default="build-dist/texture_packer.exe",
                        help="RT64's texture_packer, to pack the main pack into hd.rtz")
    parser.add_argument("--png", action="store_true", help="write the main pack as PNG (no mipmaps) for editing")
    parser.add_argument("--bold", type=int, default=1, help="thicken text strokes by N pixels at the pack scale (0 = as drawn)")
    args = parser.parse_args()

    fonts = []
    if os.path.isdir(args.fonts):
        fonts = sorted(os.path.join(args.fonts, n) for n in os.listdir(args.fonts)
                       if n.lower().endswith((".ttf", ".otf")))
    glyphs = rom_glyphs(args.rom) if os.path.exists(args.rom) else {}
    # The main pack is built as a folder of DDS files in a staging folder and,
    # when RT64's texture_packer is available, packed into texture-packs/hd.rtz
    # (Zstandard, plus a low-mip cache); otherwise the folder itself is the pack.
    packer = os.path.abspath(args.packer) if args.packer and os.path.exists(args.packer) else None
    hd_dir = os.path.join(args.packs, ".staging", "hd") if packer and not args.png else os.path.join(args.packs, "hd")
    if os.path.isdir(hd_dir):
        shutil.rmtree(hd_dir)
    text_dirs = {"original": os.path.join(args.packs, "text", "original")}
    for font in fonts:
        text_dirs[font_slug(font)] = os.path.join(args.packs, "text", font_slug(font))
    for folder in [hd_dir] + list(text_dirs.values()):
        os.makedirs(folder, exist_ok=True)

    hd_entries, text_entries = [], []
    skipped = 0
    hash_version = 5
    for name in sorted(os.listdir(args.dump)):
        if not name.endswith(".tile.json"):
            continue
        base = name[: -len(".tile.json")]
        rt64_hash = base.split(".")[0]
        if ".v" in base:
            hash_version = int(base.split(".v")[1])
        tmem_path = os.path.join(args.dump, base + ".tmem")
        if not os.path.exists(tmem_path):
            continue
        with open(os.path.join(args.dump, name)) as f:
            tile = json.load(f)
        tile.update(tile.pop("tile", {}))
        with open(tmem_path, "rb") as f:
            tmem = f.read()
        if len(tmem) < TMEM_BYTES or tile.get("width", 0) <= 0 or tile.get("height", 0) <= 0:
            skipped += 1
            continue
        image = decode(tmem, tile)
        if image is None:
            skipped += 1
            continue
        found = match_glyph(image, glyphs) if glyphs else None
        if not found:
            # Mipmapped DDS for the same reason as the text packs (save_dds):
            # without mipmaps RT64 point-samples a 4x texture at any other
            # size, which drops texels. `--png` keeps plain PNGs for editing.
            hd_image = upscale(image, args.scale, args.method)
            if args.png:
                hd_image.save(os.path.join(hd_dir, rt64_hash + ".png"))
            else:
                save_dds(hd_image, os.path.join(hd_dir, rt64_hash + ".dds"))
            hd_entries.append(rt64_hash)
            continue
        g, dx, dy = found
        save_dds(upscale(image, args.scale, "pixel"), os.path.join(text_dirs["original"], rt64_hash + ".dds"))
        for font in fonts:
            save_dds(render_glyph(image, glyphs[g], dx, dy, glyph_char(g), font, args.scale,
                                  args.shadow, args.shadow_opacity, args.bold),
                     os.path.join(text_dirs[font_slug(font)], rt64_hash + ".dds"))
        text_entries.append(rt64_hash)

    write_pack(hd_dir, hd_entries, hash_version)
    if packer and not args.png:
        subprocess.run([packer, hd_dir, "--create-low-mip-cache"], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([packer, hd_dir, "--create-pack"], check=True, stdout=subprocess.DEVNULL)
        target = os.path.join(args.packs, "hd.rtz")
        if os.path.exists(target):
            os.remove(target)
        shutil.move(os.path.join(hd_dir, "hd.rtz"), target)
        shutil.rmtree(os.path.join(args.packs, ".staging"))
        loose = os.path.join(args.packs, "hd")
        if os.path.isdir(loose):
            shutil.rmtree(loose)  # the .rtz replaces the loose folder
        hd_dir = target
    for folder in text_dirs.values():
        write_pack(folder, text_entries, hash_version)
    print(f"hd_textures: {len(hd_entries)} textures at x{args.scale} ({args.method}) -> {hd_dir}")
    print(f"hd_textures: {len(text_entries)} glyphs -> text packs: {', '.join(text_dirs)}")
    if skipped:
        print(f"hd_textures: {skipped} skipped (YUV or empty)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

