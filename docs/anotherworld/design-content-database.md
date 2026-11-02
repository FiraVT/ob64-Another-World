# Design: the content database

Status: built 2026-10-31. Names (section 7.2) and the battle reward table
(section 2.2) were added the same day. Step 2 of `ROADMAP.md` phase R0 (modding plan M1)
built the database; step 3 added JSON packs that reload while the game runs,
and the class rebalance is now the JSON file `app/content/another-world/class-rebalance.json`;
step 4 imports a LordlyCaliber project's class, item and consumable records
(section 8). Lua comes later and feeds the same pack type.

Code: `app/features/content_db.{hpp,cpp}` (database, folders, bridge),
`app/features/content_json.cpp` (the JSON format). Addresses:
`app/features/game_addresses.hpp`.

## 1. What it is

The content database holds the game's content records in the port. At start it
loads the original records from the ROM, then applies the content packs in load
order. A record is kept as the game's own bytes, in the game's byte order
(big-endian), and is read through named fields. While the recompiled game code
still reads the original tables, a bridge writes every byte a pack changed into
those tables, every frame.

The four tables in the database today:

| table | RAM (US revision 1) | ROM | records | stride |
|---|---|---|---|---|
| `classes` | `0x80187C34` | `0x5DB34` | 165 | `0x48` |
| `attacks` | `0x8018AA9C` | `0x6099C` | 159 | `0x10` |
| `items` | `0x8018C42C` | `0x6232C` | 278 | `0x20` |
| `consumables` | `0x8018E6EC` | `0x645EC` | 45 | `0x0C` |

All four sit back to back in the streamed segment B data, where
ROM = vram - `0x8016AF80` + `0x40E80`.

## 2. Evidence for the layouts

Each base, stride and count was read from the code that reads the table
(`asm/40E80.s`, segment B), and each count from where the next table starts.

- **Classes.** `func_8016DE3C` (the class size getter) computes `c*9*8` from
  `0x80187C79` (+0x45, the key) and `0x80187C38` (+0x04, the size).
  `func_8016E22C` loads `0x80187C34` (+0x00, the name pointer). Class 165's first
  word, `0x80190398`, is attack 0's name pointer, so the table ends at
  `0x8018AA9C`. The other class fields (stats `+0x08`, equipment `+0x2E`, rows
  `+0x37`, item limit `+0x47`) are documented in `tools/class_catalog.py`,
  `tools/refscan.py` and `design-organize-screen.md`.
- **Attacks.** 159 records have a name pointer (0 .. 158 "Fatal Dance"); the
  word at `0x8018B48C` (`0xFF373F47`) is not a pointer. No reader's bound check
  is traced yet. The class rows' attack ids index this table
  (`design-class-rebalance.md`).
- **Items.** The accessors `func_8016F5D0` .. `func_8016F8A4` each compute
  `id << 5` from `0x8018C42C` plus a field offset: `+0x00` name pointer (`lw`),
  `+0x04` category, `+0x05` element, `+0x06`, `+0x08` price (`lhu`), `+0x0A` ..
  `+0x0F`, `+0x10` .. `+0x14` (signed bytes). The count 278 is the game's own
  bound (`slti ..., 0x116` at `0x8016B720`, `0x8016B928`, `0x8016B9A0`,
  `0x8016BB4C`, `0x8016BBC4`), and `0x8018C42C + 278*0x20 = 0x8018E6EC` is
  consumable 0.
- **Consumables.** The accessors `func_8016F500` (`+0x00`, `lw`),
  `func_8016F520` (`+0x04`), `func_8016F540` (`+0x05`), `func_8016F560`
  (`+0x08`) and `func_8016F580` (`+0x09`) compute `id*3*4` from `0x8018E6EC`.
  The ROM holds 45 named records, 0 "None" to 44 "Pedra of Bane"; from
  `0x8018E908` the bytes are text. The `+0x06` price is read from the ROM values
  (Heal Leaf 10, Heal Seed 50, Altar of Resurrection 1500); its reader is not
  traced.

### 2.1 Named fields

A field has a name when two things hold: a game reader loads that offset with
that width (the accessor functions in segment B, `func_8016D220` ..
`func_8016F9F0`, each `lbu`/`lb`/`lhu`/`lw` from the table base plus the field
offset), and its meaning has a source. The meanings past the fields this
project traced come from LordlyCaliber's field list (`native/engine/project.cpp`,
`fields()`, read 2026-10-31); every LordlyCaliber field offset has a game
reader of the same width. Every other byte is still in the record and can be
changed by offset (`"bytes"`).

| table | field | offset | width | reader | meaning from |
|---|---|---|---|---|---|
| classes | `size` | `+0x04` | 1 | `lbu` `0x8016DE78` | this project (`func_8016DE3C`) |
| classes | `body`, `leadership` | `+0x05`, `+0x06` | 1 | `lbu` `0x8016DECC`, `0x8016DF20` | LordlyCaliber ("Body / voice", "Leadership") |
| classes | `hp_base` .. `dex_base` | `+0x08` + 4*stat | 2 | `lhu` `0x8016D4A8`, `0x8016D550`, ... | this project; LordlyCaliber gives the width |
| classes | `hp_growth` .. `dex_growth` | `+0x0A` + 4*stat | 1 | `lbu` `0x8016D4FC`, ... | this project |
| classes | `luck`, `alignment` | `+0x23`, `+0x24` | 1 | `lbu` `0x8016D940`, `0x8016D994` | LordlyCaliber |
| classes | `res_physical`, `res_wind`, `res_fire`, `res_earth`, `res_water`, `res_virtue`, `res_bane` | `+0x25` .. `+0x2B` | 1 | `lbu` `0x8016D9E8` .. `0x8016DBE0` | LordlyCaliber |
| classes | `movement` | `+0x2C` | 1 | `lbu` `0x8016DC34` | LordlyCaliber; `tools/class_catalog.py` (1 = flying) |
| classes | `equipment_0` .. `equipment_3` | `+0x2E` + 2*slot | 2 | `lhu` `0x8016DFE4`, ... | lfarroco (`tools/refscan.py`); LordlyCaliber: weapon, body, off-hand, head |
| classes | `fixed_equipment` | `+0x36` | 1 | `lbu` `0x8016D304` | LordlyCaliber ("Fixed equipment mask") |
| classes | `front_attack` .. `back_count` | `+0x37` .. `+0x3C` | 1 | `lbu` `0x8016DC88`, ... | this project |
| classes | `phys_attack`, `mag_attack`, `phys_defense`, `mag_defense` | `+0x3D` .. `+0x40` | 1 | `lbu` `0x8016D358` .. `0x8016D454` | LordlyCaliber |
| classes | `base_class`, `first_transition_level`, `intermediate_class`, `second_transition_level` | `+0x41` .. `+0x44` | 1 | `lbu` `0x8016EABC`, `0x8016EA94`, `0x8016EB00`, `0x8016EAD8` | LordlyCaliber ("growth class" and "transition level") |
| classes | `key` | `+0x45` | 1 | `lbu` at 60 sites (`func_8016DE3C` compares it with its key argument) | this project; LordlyCaliber: "Default current class" |
| classes | `element` | `+0x46` | 1 | `lbu` `0x8016D2B0` | LordlyCaliber ("Default element") |
| classes | `item_limit` | `+0x47` | 1 | `lbu` `0x8016D25C` | this project (`design-organize-screen.md`) |
| attacks | `kind`, `element` | `+0x04`, `+0x05` | 1 | `lbu` `0x8016E48C`, `0x8016E4A8` | this project (`game_addresses.hpp`) |
| attacks | `formula` | `+0x09` | 1 | battle bank X copies `+0x06..+0x0D` (`func_ovlX_801EC2D0`) | this project (`design-class-rebalance.md`) |
| items | `category`, `element`, `grade` | `+0x04` .. `+0x06` | 1 | `lbu` `0x8016F5FC`, `0x8016F610`, `0x8016F648` | lfarroco (category, element); LordlyCaliber (grade) |
| items | `price` | `+0x08` | 2 | `lhu` `0x8016F660` | this project (`design-war-director.md` 4.10) |
| items | `str`, `int`, `agi`, `dex`, `vit`, `men` | `+0x0A` .. `+0x0F` | 1, signed | `lbu` then `sll 24` (`0x8016F6BC`, ...) | LordlyCaliber |
| items | `res_physical` .. `res_bane` | `+0x11` .. `+0x17` | 1, signed | `lb` `0x8016F870` .. `0x8016F900` | LordlyCaliber |
| items | `growth_hp_str`, `growth_int`, `growth_agi` | `+0x18` bits 7-6, 3-2, 1-0 | 2 bits | `lbu`, `srl 6` / `srl 2, andi 3` / `andi 3` (`func_8016F904` ..) | LordlyCaliber |
| items | `growth_dex`, `growth_vit`, `growth_men`, `growth_luck` | `+0x19` bits 7-6, 5-4, 3-2, 1-0 | 2 bits | `lbu` with the same shifts (`func_8016F97C` ..) | LordlyCaliber |
| items | `flags` | `+0x1A` | 1 | `lbu` `0x8016FA04` | this project (bit `0x80` unique, `design-war-director.md`) |
| consumables | `category` | `+0x04` | 1 | `lbu` `0x8016F53C` | this project (`design-organize-screen.md`) |
| consumables | `price` | `+0x06` | 2 | not traced | ROM values; LordlyCaliber ("Price (Goth)") |
| consumables | `tier` | `+0x08` | 1 | `lbu` `0x8016F57C` | LordlyCaliber |

Known gaps: item `+0x10` is read signed (`lb` at `0x8016F678`) and has no
meaning yet, nor do item `+0x1B`/`+0x1C`, consumable `+0x05`/`+0x09` and attack
`+0x06`..`+0x0F`. LordlyCaliber keeps attack bytes 4-15 opaque; it holds the
attack's gameplay settings (power, target, status) in a separate 32-byte
table, which is not in the database yet. `tools/class_catalog.py` reads class
`+0x41` as a gender lock and `+0x43`/`+0x44` as the promotion source and
level; LordlyCaliber's reading (growth classes and transition levels) is the
one used here, and neither has been traced past the accessor.

### 2.2 Battle rewards

The fifth table, `rewards`, is the battle reward table: what a defeated
monster can leave. It is data of bank N (record 7, ROM `0x101D00`, the
mission field module) at ROM `0x142278` (`0x142258` on revision 0,
LordlyCaliber `creature_rewards.h`), RAM `0x801EDB38`, and it shares that RAM
with banks C (rec10b), S and X (rec10c) (`tools/guestmap.py --rom 0x142278`).

The reader is bank N `0x801ADD3C`: it walks 8-byte rows from `0x801EDB38`,
comparing the `lhu` at `+0` with the defeated unit's class (`$s4`), and stops at
a match or at a 0 word. It then takes a random number modulo 3 (`* 0x55555556`)
and reads the `lhu` at `0x801EDB3A + row*8 + slot*2`. So each row is
`class, drop_0, drop_1, drop_2`, each drop has a one-in-three chance, and a class
without a row uses the terminating row (class 0, no drops). A drop is an
equipment id with bit 15 set (`32769` = item 1) or a consumable id with bit 15
clear, and 0 is nothing; the War Director's loot upgrade reads the same bit
(`design-war-director.md` 4.10). The ROM has 35 rows (classes 39 Hawkman to 80
Baldr Golem), the terminating row and one zero row; the database holds the 35
rows. A row's `class` can be changed to give a drop to another class; adding a
row is not possible yet.

The bridge writes this table only while the runtime reports bank N loaded
(`recomp::overlays::is_function_bank_loaded(0x101D00)`) and the first row's
class word reads the ROM's value or a pack's. The dump adds `_class` and
`_drop_0`..`_drop_2` notes with the names.

### 2.3 Runtime packs

A feature that computes content from the game's state publishes it as a
runtime pack (`set_runtime_pack`). Runtime packs apply after every JSON pack,
are replaced under their id, and are removed by publishing an empty pack.
`base(table, id)` is a record with the code and JSON packs and without the
runtime packs, which is what such a feature builds on. A runtime pack is not
reported as a conflict with the packs below it.

The War Director's loot upgrade is the first: it reads the rewards from
`base`, so it upgrades a mod's drops, and publishes the upgrade as
`runtime/director-loot`. Before, it wrote the table itself and took any value
that differed from its own as a new original; with the database also writing
the table, the two would have undone each other every frame. The browser build
has no content database, so the loot upgrade is off there.

### 2.4 Segment B residency

Features used to test a name pointer in RAM (class 1 `0x80190388`, item 1
`0x8018B4D0`) to see whether segment B is loaded. A pack that renames that
record moves the pointer into the content arena, so the test failed.
`segment_b_resident(rdram)` accepts the ROM pointer or the pack's; the bridge,
`class_rebalance.cpp` and the War Director use it.

## 3. Types

- **`Table`**: `Classes`, `Attacks`, `Items`, `Consumables`. `info(table)` gives
  the name, address, stride and count; `table_by_name("items")` maps a JSON or
  LordlyCaliber table name to it.
- **`Field`**: a name, an offset, a width (1, 2 or 4 bytes, big-endian), a
  sign, and for a bit field the bit count and shift. `fields(table)` lists
  them; section 2.1 has every field.
- **`Edit`**: a table, a record id, an offset, the bytes written there and a
  bit mask (a bit field changes only its own bits, so two packs can set two
  lanes of one byte without a conflict).
- **`Pack`**: an id, a source and a list of edits. It is filled three ways:
  - `set_field(table, id, "str_base", 92)` writes a named field. A JSON pack
    maps `{"classes": {"34": {"str_base": 92}}}` to these calls (section 7).
  - `set_record(table, id, bytes)` replaces a whole record. A LordlyCaliber
    project's `records` section (table, id, hex bytes; step 4) maps to these
    calls after the hex is decoded.
  - `set_bytes(table, id, offset, bytes)` writes any byte range.
- **`Record`** and its typed views (`ClassRecord`, `AttackRecord`,
  `ItemRecord`, `ConsumableRecord`): read-only accessors over a record's bytes.
  `original(table, id)` is the ROM's record; `current(table, id)` has every pack
  applied. A native system reads `current`.

## 4. Load order and conflicts

`load()` reads the four tables from `recomp::get_rom()`, the ROM image after the
`.ips` patches (`rom_patch.cpp`), which is what the game's DMA of segment B
reads. It refuses a ROM whose class 1 name pointer is not `0x80190388`
("Soldier"), which is the revision 1 value (revision 0 has every table 0x20
lower, `notes-revisions.md`). Then it reads the packs, in this order:

1. Packs added from code with `add_pack`, in the order they were added. None
   exist today; the slot is for C++ features and, later, Lua.
2. `content/*.json` beside the executable, in file-name order. These are the
   project's own packs. The build copies `app/content/` there, and `make dist`
   puts it in the package. A package update replaces this folder.
3. `user-content/*.json` in the settings folder, in file-name order. These are
   the player's packs. The settings folder is the executable's folder unless it
   is read-only or `OGRE_PREF_DIR` is set, so the two folders have different
   names. An update never touches `user-content/`.

Mod folders (a folder with a `mod.json`) take a place in this order by their
priority and name, and after the mods they require: `design-mods.md`. The
listing is `app/features/content_files.cpp`, shared with the Lua runtime.

A later pack wins. When two packs write the same record, the database records a
`Conflict` (table, id, the earlier pack, the later pack) and logs it; the MODS
tab will list them. A file with any error is refused as a whole and logged with
the reason (`[content] user-content/x.json refused: classes 34: unknown field
"str_bse"`); `file_errors()` keeps the list. If `content/` holds no pack, the
log says that the class rebalance is off.

## 5. The bridge

`content::tick(rdram)` runs once per frame on the main thread, from
`sdl_platform.cpp`, right before `class_rebalance::tick`. While segment B is
resident (class 1's name pointer in RAM is `0x80190388`), it writes every byte
that some pack wrote. The segment is DMA'd again during play and brings the ROM
bytes back, so the write repeats every frame, as the class rebalance did before.

Every 60 ticks (about a second) it lists both folders. When a file was added,
removed or changed (name, time or size), it reads all JSON packs again and
re-applies every pack. A byte that a pack wrote before the reload and no pack
writes after it gets its ROM value back on the next tick, so removing a pack or
a field undoes it while the game runs. A change shows in the game once the
game next reads that record; for example, a battle reads class and attack
records, so a stat change shows in the next battle.

Bytes no pack wrote are never touched. A code mod or another feature that
writes the same table (`small-large-units` writes class sizes) keeps working.
A pack that writes a byte with its original value still owns that byte and
still writes it, which is what the rebalance's per-field writes did.

## 6. The class rebalance as a pack

The rebalance is `app/content/another-world/class-rebalance.json`: 19 class records
(including the Centurion records 34, 35 and 94) and the two magic-formula
attacks (30 Magic Missile, 31 Abyss). It was generated from the former C++
`CHANGES` table, with each row's comment kept as `_comment` and the class name
as `_name`. A field that the table left alone (`KEEP`) is absent from the
file. `class_rebalance.cpp` keeps the rules that are not content: large classes
stay at size 2 (`DECISIONS.md` 2026-10-29, one square), the record of which
classes the ROM makes large for the Organize screen, the alignment answer and
the one-square Organize answers.

Checks, with an offline harness that runs the code on an 8 MiB RAM image whose
segment B is filled from `assets/ogre64.z64` (the game was not run):

- The original `class_rebalance.cpp` (commit `0b4d310`) and the database with
  `content/another-world/class-rebalance.json`, three ticks each: 0 bytes differ. The pack
  owns 245 bytes; 178 of them differ from the ROM.
- A `user-content/test.json` that sets class 34 `str_base` 99 and item 1
  `price` `0x1234`: after `reload_files` both show in RAM and the conflict with
  the rebalance is listed. After the file is deleted and reloaded, STR is the
  rebalance's 92 again and the price is the ROM's 20.
- Refused files, each with its reason: an unknown field, record 165 of 165, a
  value of 300 in a byte, an unknown table, a `record` of the wrong size, and
  broken JSON.

## 7. The JSON format

```json
{
  "_comment": "Keys that start with _ are notes and are skipped.",
  "classes": {
    "34": { "_name": "Centurion", "str_base": 92, "front_count": 3 }
  },
  "attacks": {
    "30": { "formula": 7 }
  },
  "items": {
    "0x1": { "name": "Iron Fang", "price": 1200, "bytes": { "0x10": "05" } }
  },
  "consumables": {
    "1": { "record": "801906d0 0000000a 01070000" }
  }
}
```

- The top level is an object of tables: `classes`, `attacks`, `spells`,
  `items`, `consumables` (and `appearances`, `rewards`, `battle_forms`).
- Each table is an object of records by id, in decimal or `0x` hex. The id must
  be below the table's count.
- Each record is an object of named fields (section 3). A value is a JSON
  integer or a string such as `"0x07"`, and must fit the field's width.
- `"record"` replaces the whole record with hex bytes (spaces allowed); it must
  be exactly the table's stride. It applies before the record's other keys.
- `"bytes"` maps an offset to hex bytes, for a byte that has no name yet.
- A bit field (the item growth lanes) takes 0..3.
- `"name"` renames the record (section 7.2).
- Within one record, the named fields and `bytes` apply in key order, so they
  should not overlap.

The values are the game's own: a class's base stat and growth, an attack id
and count per row, an item price in goth.

### 7.0 Spells

`spells` holds the attack each generic magic attack becomes, per element of
the caster's gear (`game_addresses.hpp` `kSpellMaps`, read by `func_8016E52C`).
Row 0..3 is attack 45..48 and row 4 is attack 145; the dump names it in
`_attack`. The fields are `physical`, `wind`, `fire`, `earth`, `water`,
`virtue` and `bane` (element 0..6). Rows 0..3 never read `physical`: element 0
takes the class's front-row attack. A value must be an attack 1..158 that is
not itself generic (45..54, 145).

```json
{ "spells": { "0": { "fire": 65 } } }
```

This makes attack 45 cast attack 65 for a caster with fire gear, where the
ROM has 63. LordlyCaliber's `spellLinks` import into this table
(`design-lordlycaliber-import.md` section 4.1).

### 7.1 The dump

At every start the database writes `content-dump/classes.json`,
`attacks.json`, `items.json` and `consumables.json` in the settings folder:
every ROM record in the pack format, with its name (`"name": "Short Sword"`),
every named field and the whole record as `"_hex"`. A modder looks up a value
there and copies the record into a pack in `user-content/`, keeping only the
fields to change. The dump is not loaded. A copied field or name equal to the
ROM's changes nothing in the game.

### 7.2 Names

A record's name is the string its name pointer (`+0x00`) points at, in
segment B. The encoding is the ROM's own:

- a one-line name is its ASCII bytes, NUL-terminated;
- a two-line name is `0x0E`, line 1, `0x10 'c'`, line 2, `0x0F`. Every table
  has some: 18 classes, 6 attacks, 52 items and 26 consumables. LordlyCaliber
  writes the same framing (`encodeClassName`).

In JSON, `"name"` is a string, with `\n` between two lines:
`"Altar of\nResurrection"`. Each line has 1 to 32 printable ASCII characters
(LordlyCaliber's item-name limit). The longest line in the ROM is 16
characters, so a longer line may not fit a window; that is not tested yet.
Record 0 of items and attacks has the name bytes `0x81 '|'`, which the decoder
shows as `|`.

A changed name cannot go back into the ROM's string pool, which has no room. The
bridge writes it, NUL-terminated and 4-byte aligned, into the content arena,
guest RAM `0x80600000`..`0x806FFFFF`, and points the record's name pointer at
it. The game never allocates above `0x80400000`: both heap set-ups pass a fixed
end of `0x80400000` (`0x80071EEC`, `0x800E9C20`..`0x80400000`; `0x801784B4`,
`0x80243DD0`..`0x80400000`), whatever `osGetMemSize` answers (8 MiB in this
port). The port's own users of the upper 4 MiB are the `OGRE_SYNTH_FRAME`
probe (`0x80500000`, `0x80700000`) and RT64's words at `0x807FFC08`. A name
equal to the ROM's takes no arena space and owns no byte. When a pack's name
is removed, the record's pointer gets its ROM value back (section 5). Two packs
that rename the same record conflict; the later wins.

The game reads a name through the pointer with the CPU, so a pointer into the
arena should work wherever the name is shown. This is checked offline only:
the pointer and the arena bytes are right in RAM, and no window has been seen
with a new name yet.

### 7.3 The MODS tab

The MODS tab lists, under CONTENT PACKS, each mod folder (`MOD <name>
<version>` with its file count and priority, or why it was refused), then every
pack in load order (code packs,
`content/`, `user-content/`, then the runtime packs) with its edit and name
counts and how many parts it does not apply; every refused file with its
reason; and every conflict as `classes 34: <later> OVER <earlier>`. Under
SCRIPTS it lists each script with its handler and change counts, the handlers
switched off after errors, or the error that stopped it (`design-lua.md`).

The tab is available in the game's Esc menu as well as on the start screen,
because packs and scripts load and reload while the game runs; the code mod
rows (`.nrm`, which load before the game boots) stay on the start screen only.
The lines are read when the tab is opened (`content::status()`,
`lua::status()`, `RowKind::ContentLine` in `app/src/ui.cpp`).

## 8. LordlyCaliber projects

A LordlyCaliber project folder (`project.json` plus assets) copied into
`user-content/` or `content/` loads as a pack, ordered by the folder's name. A
`.json` file whose `format` is `LordlyCaliberProject` loads the same way. The
packed archive form (`LC05PACK` header, zlib) is not read.

### 8.1 The format

Read from rempred/LordlyCaliber `native/engine/project.cpp` and `project.h` on
2026-10-31 (this answers modding plan P4 and ecosystem research E4 for the
record sections):

- `Project::manifest()` writes `"format": "LordlyCaliberProject"`,
  `"version": 1`, `"sourceSha256"` (the SHA-256 of the ROM it was made from)
  and `"sourcePath"`.
- `"records"` holds `"classes"`, `"items"` and `"consumables"`, each an object
  of record id (decimal string) to the whole record as lower-case hex. Only
  records whose bytes differ from the ROM's are written.
- `Project::fromRom` reads classes at ROM `0x5DB14` (165 records, stride 72),
  items at `0x6230C` (278, stride 32) and consumables at `0x645CC` (45, stride
  12) on revision 0, and adds `0x20` to each base on revision 1. On revision 1
  those are `0x5DB34`, `0x6232C` and `0x645EC`, the database's tables with the
  same strides and counts. Record id 0 is the first record.
- `Project::replaceBytes` refuses any change to a record's first 4 bytes, the
  name pointer. Names are edited through other sections (`classNames`,
  `descriptions`).
- Other sections (`shops`, `encounters`, `promotion`, `dialogues`, artwork,
  added classes and items, ...) are separate top-level keys.

### 8.2 How the port applies it

- The version must be 1; any other is refused.
- For each record, the pack takes the bytes after the name pointer that differ
  from the ROM's record, as one edit per run of changed bytes. A project's
  untouched bytes therefore own nothing, and two packs conflict only where
  both change the same record.
- `sourceSha256` is not checked. The record bytes after the name pointer are
  the same on both US revisions: comparing `assets/ogre64-rev0.z64` with
  `assets/ogre64.z64` over all four tables, 0 records differ after `+0x04`,
  and every name pointer is `0x20` higher on revision 1. LordlyCaliber itself
  refuses a project on a ROM with another SHA-256; the port accepts a project
  from either revision. The SHA-256 values of this machine's `.z64` dumps
  (`06c893dd…` revision 0, `a89adc59…` revision 1) differ from the ones in
  `notes-revisions.md`, so a SHA check would first need the byte order
  LordlyCaliber hashes.
- Records with an id past the table's count are classes or items
  LordlyCaliber added. An added class becomes a new class with a key
  (`design-lordlycaliber-import.md` step 2, 2026-11-01); a `records.classes`
  or `classNames` entry for its id goes to that key. An added item is skipped
  and counted until new item ids exist (modding plan M4).
- `classNames` and `itemNames` (id to the name's bytes in the game's encoding,
  as hex, for names that differ from the ROM's) become names (section 7.2).
  LordlyCaliber has no section for consumable or attack names. A name for an
  added class or item is skipped and counted.
- Every other non-empty top-level section is listed in `Pack::not_applied` and
  logged: `[content] user-content/my-project/project.json: not applied yet:
  1 added items, shops`.

Check, offline (the game was not run): a project built from revision 0 records,
with the Short Sword's price set to 1280, Centurion's item limit to 5 and Heal
Leaf's price to 99, plus an added item 300 and a `shops` section. After one
tick the three values are in RAM, the name pointers keep their revision 1
values (`0x8018B4D0`, `0x80190220`), the rebalance's Centurion STR 92 stays,
there is no conflict, and the log lists the added item and `shops`. Version 2,
another `format` and a 1-byte record are refused with their reasons.

## 9. Next steps

- **Attack settings**: LordlyCaliber's 32-byte attack settings table (power,
  target, status); its RAM address and readers on revision 1 are not traced.
- **Rewards from LordlyCaliber.** Its reward editor needs its own rebuilt
  reward code (256 rows with weights, a header `LC05REWD`, two patched
  instructions at revision 0 ROM `0x10245C`) and supports revision 0 only
  (`creature_rewards.cpp`), so its reward section is listed as not applied. A
  data-only import would map a project's retail-shaped rows onto this table.
- **Descriptions** (LordlyCaliber `descriptions`: banks of text per class,
  item and consumable), shops and encounters from
  LordlyCaliber projects: each needs its table in the database with the reader
  verified as in section 2, and the LordlyCaliber section mapped to it.
- **Named state for Lua** (`game.items[id]`, `game.classes[id]`) reads
  `current` records through the field table.
- **More tables:** shops, encounters, rewards and text, each with its reader
  verified as in section 2.
- **New ids** past a table's count need the bridge to place a larger table in
  free guest RAM and repoint the original readers (`ROADMAP.md` section 4.1,
  modding plan M4): classes up to id 255, items past 277. The database itself
  has no fixed count beyond `TableInfo`; the bridge's write targets become the
  relocated tables.
