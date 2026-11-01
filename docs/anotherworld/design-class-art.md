# Art for new classes

Goal (maintainer, 2026-11-01): a new class can have its own look in the three
places a class is drawn: the battle sprite, the portrait, and the small figure
on the Organize formation and the mission field. The first slice is a recolor
of the template class's art, as a proof of concept for modders; the same
mechanism later carries new images (PNG files in a pack).

## 1. The three places

Formats are from rempred/LordlyCaliber (`animation-art.js`, `army-sprites.js`,
`portraits.js`), whose offsets are for the US revision 0 ROM; each is checked
on revision 1 before use.

### 1.1 Battle sprite

- The class animation resource (asset `0x00315736`) starts with the class
  handle table at `+0x24`: 4 u16 handles per class (`class * 4 + body flags`;
  the 8-byte slot 0 that `class_animation.cpp` already extends to 256 ids).
- A handle's low 12 bits minus 1 index the combat descriptor table (asset
  `0x003B6CD0`, 208 u32 descriptor keys); its top 4 bits pick a body child.
- A descriptor is a list of u32 resource keys: member 0 metadata, 4 poses,
  8 config, 12 lookup (the palettes), then the art objects.
- The config maps each art object to a bank of the lookup resource; a bank is
  0x200 bytes, 256 RGBA5551 colours. Art objects are `0x5554` sprite
  resources, CI8 with an I4 plane; a child with flag `0x04` carries its own
  0x200-byte palette.

A recolor is a copy of the template's descriptor whose lookup member names a
recolored copy of the lookup resource, a new descriptor table entry for it,
and the new class's handles pointing at that entry (section 4).

### 1.2 Portrait

- The appearance row (6 bytes per class, ROM `0x64C00`, moved to
  `0x8060ED00` for new classes) holds at `+0`/`+2` a halfword `v`:
  `v >> 3` indexes the portrait id table (asset `0x01DFB36E`), `v & 7` is the
  palette variant (`func_8016FD0C`; `portraits.cpp`).
- A portrait asset decodes to a 40x48 CI8 image (0x780 bytes) followed by
  80-colour RGBA5551 palettes, 0xA0 bytes each, one per variant.

A recolor is a new variant: the template's portrait asset with one more
palette, and the new class's `v & 7` set to it (up to 8 variants).

### 1.3 Organize and field figure

- Six atlases, each two shared 256-colour palettes (enemy red, player
  blue/green) and one CI8 plane per model (`portraits.cpp`). A class picks its
  model with appearance `+4` (`& 0x3F`; `0x80` the special lane; size 2 the
  large lane).
- The palettes are shared by every model, so a class cannot have its own.

A recolor is a new model plane: the template's plane with each index moved to
the palette entry nearest the recolored colour, appended to the atlas, and the
new class's appearance `+4` set to it.

## 2. One mechanism: art in the ROM image

All three places load their art through the game's asset loaders, which read
the cartridge by DMA. The port builds the changed art into the in-memory ROM
image before the game starts, as `rom_patch.cpp` does for `.ips` patches:

- The runtime's ROM image (`librecomp/src/pi.cpp`, a `std::vector`) grows past
  the retail data through a runtime patch (`recomp::extend_rom`), so new
  assets get ids of their own: an asset id maps to ROM
  `(id & 0x0FFFFFFF) + 0x594250` with a size word first.
- New assets are written in the game's LZ format using literal runs only (flag
  `0x40 | (n - 1)`, up to 64 bytes; `tools/ogrelz.py`), which every decoder
  reads.
- Tables that name assets (the descriptor table, the portrait id table, an
  atlas's owner) point at the new copies; where a table cannot grow in place it
  moves too.
- The game's own loaders then read the new art with no hook in them.

Art changes apply at start; a pack's art edit needs a restart, unlike its
record edits.

## 3. The pack format and the template

Maintainer, 2026-11-01: the art is image files in the mod, made the way a
modder makes new artwork; the Dragon Knight is a recolor of the Centurion's art
built that way, and the mod is a template to copy
(`app/content/examples/new-class/README.md`).

```json
"art": {
  "portrait": "art/portrait.png",
  "figure_front": "art/figure-front.png",
  "figure_back": "art/figure-back.png"
}
```

- `tools/export_class_art.py <rom> <class> <dir> [--hue D --only LOW-HIGH]`
  writes a class's portrait and both formation figures as PNG files, turning
  the colours in a hue range (`--hue 140 --only 185-265`: the Centurion's blue
  armour to crimson, skin and gold kept). Checked by rendering the Centurion
  (class 34: portrait 33 variant 0; ordinary lane model 35).
- The images are made from the user's ROM and never committed
  (`.gitignore`: `app/content/examples/*/art/*.png`).
- The pack reader checks every art path (`is_art_path`; security.md section 2).
- The six formation atlases on revision 1 (asset id; LordlyCaliber's revision
  0 key minus `0x37C`): enemy front ordinary `0x01DBAF30`, special
  `0x01DC2FA4`, large `0x01DB4424`; army back ordinary `0x01DBEB94`, special
  `0x01DC5AF0`, large `0x01DB7BE0`. Each is the game's size word, then the LZ
  block.

## 4. As built (2026-11-01, not played)

`app/features/class_art.cpp` runs from the runtime's init callback, after the
`.ips` patches. It writes new assets into the ROM image's free end (ROM
`0x2770000`..`0x2800000`; the last retail data ends at `0x275D998`), each as
the game's size word and a literal-only LZ block, and redirects or repoints
the tables:

- **Asset redirect.** The entries of `func_8009DAF4` (size), `func_8009DBB8`
  (load) and `func_8009DD38` (LZ load) pass the asset id through
  `ogre_asset_redirect`, so a replaced asset loads from its copy wherever the
  game asks for it.
- **Portrait.** `portrait.png` (40x48, at most 80 colours) becomes a portrait
  asset; a copy of the portrait id table (`0x01DFB36E`, 142 entries) gets it
  appended; the three `lui`/`ori` pairs that name the table (`0x8016FC1C`,
  `0x8016FC8C`, `0x8016FD78`) read the id from `ogre_portrait_table_hi/lo`
  (`relocate_tables.py`); a runtime pack sets appearance `+0`/`+2`.
- **Formation figures.** `figure-front.png`/`figure-back.png` (16x24) map to
  the nearest colours of each atlas's palette and go into copies of the
  ordinary lane's two atlases at model 58 up (the army atlas has a 0x200-byte
  tail after model 55, so 56 and 57 stay clear); the atlases are redirected;
  the runtime pack sets appearance `+4`. Large and special lanes are not
  built.
- **Battle sprite.** The template's handles (class animation resource,
  `0x24 + (class*4 + body)*2`) name descriptors 64 and 65 for the Centurion.
  Each descriptor with swatches in `battle/` gets a copy whose member 3 names
  a recolored palette resource; the copies are appended to a copy of the
  descriptor table (`0x003B6CD0`, 208 entries; `func_801AD1B0` sizes and loads
  it through the redirected loaders); the class's handles name the new entries
  (`class_animation::set_handle_override`). The swatches are colour by number:
  one 16x16 image per bank, pixel `i` = colour `i`, each entry's clear bit
  kept.
- `tools/export_class_art.py` writes all of it from the ROM, recolored with
  `--hue`/`--only`; the template's `art/README.md` explains the files.

Checked offline: the export decodes the Centurion's portrait (33, variant 0),
its figures (model 35) and both descriptors' palettes (9 and 5 banks, no art
object with its own palette, no other descriptor sharing them).

## 5. Open

- Played checks: the portrait in menus, the figures on the formation and the
  field, the battle sprite's colours, and a save and reload.
- Whether any code bounds a descriptor index at 208.
- Large and special figure lanes (LordlyCaliber's owner sites hold those
  atlases in fixed caches).
- New battle shapes (new sprite objects), and art for other places (the
  battle portrait in the corner, the map piece).
