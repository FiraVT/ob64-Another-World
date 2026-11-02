# Design: new classes (phase R2)

Status: 2026-10-31. Step 1, the reader inventory (section 2), is done. Step
2, moving the class table with the same 165 records behind
`OGRE_CLASS_TABLE_MOVE=1` (section 3), is built: it boots to the title and is
checked offline; it has not been played. Decisions: `ROADMAP.md` section 4.1 (relocate the
tables on the original engine) and `DECISIONS.md` 2026-10-31 (R2 before R1).

## 1. Goal

A class id is one byte, so the game can address 256 classes; the ROM has 165
(`0x80187C34`, 0x48 bytes each, ending where the attack table starts). R2
places larger copies of the per-class tables in the content arena, points the
original code at them, and adds classes as records in the content database.
Done when a new class is recruited, fights and saves.

## 2. Who reads the per-class tables

`tools/table_refs.py <start> <end>` lists every instruction in the generated
code (main code and all 34 bank units) that forms an address inside a range,
following each `lui` through an index `addu` to the load, store or `addiu`
that adds the low half. Counts on revision 1:

| table | RAM | per class | sites | functions | units |
|---|---|---|---|---|---|
| classes | `0x80187C34` | 0x48 bytes | 299 | 113 | main 196 sites (56 functions); banks S 27, R 16, AF 15, C 12, N 9, I 5, AE 4, V 4, D 3, B 2, and one each in AA, AC, AG, AH, W, Z |
| one record before the class table | `0x80187BEC` | (class - 1) * 0x48 | 7 | 5 | main 1, S 3, AB 2, M 1 |
| appearances (portrait, model) | `0x8018ED00` | 6 bytes | 36 | 17 | S 15, main 6, N 5, AC 4, one each in AA, AG, C, M, V, Z |
| aliases (a class key's own class) | `0x801EF3D8` | 1 byte | 14 | 9 | S only |
| known classes | `0x80196A96` | 1 bit | 22 | 14 | M 8, S 9, main 5 |

Every one of these sites has its own `lui`, and none of those `lui`s builds an
address outside its table (`table_refs.py` reports shared upper halves; there
are none). Of the 299 class-table sites, 226 are `lbu`, 58 `lhu`, 8 `lw`, 6
`addiu` (a record pointer) and 1 `sb`. The most-read field is `+0x45`, the
class key (117 sites).

`func_80172C64` shows the "one record before" form: it multiplies the
character's class by 0x48 and adds `0x80187BEC` (and `0x80187C34`), so these
seven sites are class-relative and move with the table. The bytes at
`0x80187BEC` themselves belong to another table (name pointers), so the other
six sites are read before they are moved.

The appearances, aliases and known-classes tables were found through
LordlyCaliber (`class_runtime_inventory.cpp` names them at revision 0
`0x8018ECE0`, `0x801EF3B8`, `0x80196A76`, each 0x20 lower than here) and match
this project's own readings (`portraits.cpp`, `organize_edit.cpp`,
`design-game-ui.md`).

The class count: four explicit bounds `slti ..., 0xA5` (bank B `0x8019951C`,
`0x80199D48`, `0x8019A6E4`; bank C `0x80228A14`). Loops written another way
(a pointer walk, a count held in a register) are found by reading the 113
functions.

## 3. How the move works

Because no `lui` is shared, a table can move by a multiple of 0x10000 by
adding the same amount to the immediate of each listed `lui`; the low halves,
field offsets and index arithmetic stay as they are. A class table at
`0x80607C34` (inside the content arena, 255 records end at `0x8060C3EC`) keeps
every low half. LordlyCaliber relocates the same way (it rewrites each site's
`lui`/`addiu` immediates), but stays below `0x80400000`; the port has the
upper 4 MiB free (`security.md`, the content arena).

### 3.1 As built

- `tools/relocate_tables.py` rewrites the C line of each of the 301 `lui`s
  (the 299 class-table sites and the two class-relative sites one record
  before it) to add `ogre_class_table_shift` to the upper half, tagged
  `// app relocation: class table`. The Makefile runs it after
  `tools/apply_hooks.py` for the main unit and the bank units, so it survives
  `make recomp` and `make bank-recomp`. `--revert` gives back the generated
  files byte for byte (CRLF kept); `--check` reports 301 of 301.
- `ogre_class_table_shift` is defined in `content_db.cpp`. It is 0 (the ROM's
  table, the code as before) unless `OGRE_CLASS_TABLE_MOVE=1`.
- With the option, the content database copies all 165 current class records
  (ROM plus packs) to `0x80607C34` on its first tick and then sets the shift to
  `0x48`. After that the moved copy takes the same pack bytes and released
  bytes as the original every frame; a byte the game writes into it itself
  (class 163 `+0x06` on a mission load, bank R `0x8021626C`) is left alone.
  The original table keeps being written too, because the port's features
  read it at `0x80187C34`.
- New names moved to `0x80610000` (`addr::kNamesArena`), past the room for 255
  class records.

### 3.2 Checks

- Offline: the shift is 0 until the copy is filled, then `0x48`; the copy
  matches the original table in every byte (Centurion `str_base` 92 from the
  rebalance in both); a game write to class 163 `+0x06` in the copy survives
  three ticks.
- A 30-second boot with the option (no audio): the log shows the switch and
  the game reaches the title.
- Play, 2026-11-01: with the option, the game locked up in Dio's dialogue
  (the menu still worked; the game's own logic stopped). Without it, the same
  save played through, so the move caused it. The likely cause: a class id is
  a byte, so code can read records 165..255 (255 is a common "none") or run a
  search past the last class, and in the ROM's layout those bytes are the
  attack and item tables that follow the class table; the first copy had zeros
  there, and a search that only stopped on a byte of that data would not stop.
  The fix fills the whole window of 256 records: the 165 class records, then
  the bytes that follow the original table, copied every frame. Offline the
  window matches the original layout in all 256 * 0x48 bytes. Replayed with
  the option the same day: Dio's dialogue plays through.
- The move is the default from 2026-11-01; `OGRE_CLASS_TABLE_MOVE=0` turns it
  off.

## 4. Open before building

1. Done: of the seven sites before the table, `func_80172C64` and bank M
   `0x801A4144` compute `class * 0x48 + 0x80187BEC` and move; the other five
   (bank S `0x801A7B70`, `0x801A7CEC`, `0x801A91E4`; bank AB `0x80216EBC`,
   `0x80217018`) read `0x80187BF4 + element * 4`, a pointer table indexed by an
   item's element (`func_8016F600`), and stay. The store (bank R `0x8021626C`)
   writes class 163 `+0x06`: 1 on missions 63 and 65, else 2.
2. Class-count bounds (2026-11-01): none found. The four `slti ..., 0xA5`
   are not class counts: bank B `0x8019951C`, `0x80199D48`, `0x8019A6E4`
   compare a screen coordinate plus 4 against 165 (tutorial menu layout), and
   bank C `0x80228A14` is the cutscene interpreter branching on a command
   number (`== 0xA4`, then `< 0xA5`). The `0xA4` immediates in class code are
   class 164 itself (Death Bahamut), a fixed id. The Change Class screen's
   loops count characters (`slti ..., 0x64` and `0x63` in
   `func_ovlS_801D736C`), not classes. The game indexes classes by id and
   builds its lists from characters and candidate lists, so a class past 164
   is reached when something refers to it. Still to read: the 113 class-table
   functions for a loop that walks the table by pointer up to its end.
2a. Class-change requirements: `func_ovlS_801D2378` reads the target class's
   8-byte record at `*(Organize state + 0x80) + class * 8` (Organize state is
   the word at `0x80196B18`). The records are a buffer the Organize screen
   fills when it opens, not a fixed ROM table. A new class needs that buffer
   to grow and its record in it. Found 2026-11-01: bank S stores the result
   of the asset loader `func_8009DD38(0x01E398D2)` there (`0x801C2558`,
   `0x801DFE68`, `0x801E1434`, `0x801E0AA0`). The asset decodes to 648 bytes:
   **81 records**, class ids 0..80. Centurion (34) is `00 34 3E 34 00 36 00 64`:
   minimum stats, then the alignment range (0..100) in bytes 6 and 7.
2b. The standard-class bound: 27 compares against 81 (`slti`/`sltiu ...,
   0x51`): bank S 19, bank V 2, and one each in the main code and banks N, AA,
   AC, AE and Z (`tools/table_refs.py`-style search of the instruction
   comments). The game treats ids 0..80 as standard classes, which have
   requirements and take part in Change Class, and 81..164 as special classes
   (unique characters, bosses, enemy-only classes). A new class past 164 is
   therefore special to the original code: it can exist, fight and be saved,
   but Change Class does not offer it.

2c. Change Class as read 2026-11-01 (bank S):
   - `func_ovlS_801CF36C` builds the offered list. It walks a table of u16
     per standard class at `0x801EF2EC` (bank S data) until `0xFFFF`. An entry
     is offered when its bit is set in the known-classes bitfield
     `0x80196A96` (masks at `0x801EF390`) and its family byte
     (`func_ovlS_801CF340`, the entry's high byte) shares a bit with the
     character's family. The list goes to Organize state `+0x1BEE`, beside two
     64-byte buffers at `+0x1C2E` and `+0x1C6E`.
   - `func_ovlS_801CF310` and `func_ovlS_801CF340` read that table's two
     bytes and return 0 for a class of 81 or more.
   - `func_ovlS_801D6DAC` is the family check (from-class to to-class).
   - `func_ovlS_801D6EC0` (the list's test) and `func_ovlS_801D2378` (the
     commit's test) check, in order: the family; the target's gender (class
     `+0x05`) against the character's `+0x14`, except class `0x26`; the
     minimum stats (record bytes 0..5 against character `+0x1C..+0x26`,
     `0xFF` none); the alignment range (bytes 6, 7 against `+0x1B`); for
     Centurion 34/35 also `func_8016FF14(3)` and character `+0x36 == 0x63`;
     then the target's four default equipment ids (`0x801EF2A8`). An id the
     character does not hold must be free in the inventory (`+0x02 < +0x03`
     at `0x80196B20`) or be bought with goth (`func_ovlS_801D2F88` against
     `0x80196A8C`).
   - The commit (`func_ovlS_801E13B0`, `0x801E2484..0x801E25A0`) returns the
     old equipment to the inventory, clears the character's four stored ids
     (`+0x2A..+0x30`), writes the class (`+0x11`) and key (`+0x12`), and
     counts the new class's default equipment as in use.
   A new standard class therefore needs: the u16 table entry before the
   `0xFFFF` (the table is followed by other data, so it moves), a
   known-classes bit (bit 172 is past the 21 bytes, so the bitfield moves and
   the save must keep it), the requirements record, a default-equipment set
   that the army can afford, and the 81 bounds raised where they compare a
   class.
2d. The 27 compares against 81, classified 2026-11-01:
   - Not a class: bank S `0x8019E334`, `0x8019E870` (a colour value).
   - Guard an index into the family table, so they rise with it: bank S
     `0x801CF314` (`func_ovlS_801CF310`), `0x801CF344` (`801CF340`),
     `0x801CF434` and `0x801CF4D8` (`801CF36C`), `0x801CF6FC` (`801CF67C`),
     `0x801D283C` (`801D2808`).
   - Test "is a standard class" for the character's current class, which
     gates its own Organize options: bank S `0x801CEFA8` (`801CEFA4`),
     `0x801D01AC` (`801D0150`, marks a special class's character `0x80`),
     `0x801D38A0`/`A4`, `0x801D3970`/`74`, `0x801D3BE4` (`801D379C`, the
     Organize options, 19 callers), `0x801D73B4` (`801D736C`, guards the
     known-bit write), and the main code's `0x8016BD9C` (class < 81 or
     114..116). These decide what a Dragon Knight can do after the change
     (change again, and so on); the first slice leaves them.
   - Guard the target's equipment check in the list test: bank S `0x801D72C0`.
     With a target past 80 the list skips the check; the commit test
     `801D2378` still makes it.
   - Not read yet: bank S `0x801E2C30`, `0x801E356C` (`801E2BEC`); bank N
     `0x801DB630`; bank V `0x80216C28`, `0x802188FC`; bank Z `0x80219B80`;
     bank AA `0x80216A50`; bank AC `0x80217C08`; bank AE `0x80219C98`.
2e. Other pieces found 2026-11-01:
   - The family table (`0x801EF2EC`, ROM `0xDBD4C`) holds 81 u16 and the
     `0xFFFF` at index 81; the bit masks follow at `0x801EF390`. Fighter (2)
     and Centurion (34) are both `0x8101`, Centurion F (35) `0x8202`.
   - Known-bit readers: bank S `0x801CF418`, `0x801CF6E0`, `0x801D294C`,
     `0x801D2C14`, and `801D736C` (`0x801D73E4..0x801D74C4`, which also
     writes). The byte for class 168 and up is army record `+0x33`
     (`0x80196AAB`), which holds something else. Banks M and X read bytes 11,
     16 and 2..6 of the bitfield by fixed address.
   - The requirements asset is LZ-compressed. `func_8009DD38` reads its size
     word, loads it raw through `func_8009DBB8`, allocates the decoded size
     from `func_8007A7E0` and decodes with `func_8007A110`. A larger
     allocation there plus the new records after the decode grows it in the
     heap the game frees it from.
   - The candidate list (`0x80191C0E + n*2`, written by `801D2808`) keeps the
     class in the low 7 bits and uses `0x80` for "listed, not selectable". A
     class past 127 does not fit; the 13 accesses move the flag to `0x8000`.
   - The port's own Change Class view (design-game-ui.md, PORT MENUS =
     MODERN) copies the requirement row and the list, so it reads the same
     data.

Build plan (maintainer's choice 2026-11-01: new classes in Change Class):

1. A pack marks a new class standard with a `"change_class"` block: the
   requirements (`str`..`dex` minimums, `alignment_min`/`_max`, 255 none) and
   the class whose family entry it takes (`"family_of"`, default `copy_of`).
   Missing requirements come from that class's record.
2. The family table moves to the content arena (`relocate_tables.py`), 256
   entries with the `0xFFFF` last, and its six index guards rise to 255.
   Classes 81..171 keep entry 0, which the original code treats as no family,
   so they behave as before.
3. The known bits of classes 168 and up come from the port at the five
   reader sites; a new standard class is always known. The writes stay
   guarded by `0x801D73B4`.
4. The requirements asset grows to 256 records in `func_8009DD38`.
5. The candidate list's flag moves to `0x8000`.
6. The modern Change Class view reads the grown data.
7. Read the eight unread compares, then the current-class gates (2d) for a
   new class changing again.

Built 2026-11-01, a smaller route than the plan above (not played). The port's
Change Class view (PORT MENUS = MODERN) builds its list from the game's list
and runs the game's own check `801D2378`; the check needs only the target's
family entry and requirement record. So:

- A pack's `"change_class"` block on a new class (`family_of`, `str`..`dex`,
  `alignment_min`, `alignment_max`; 255 none, the default) makes it a
  standard class (`app/features/class_change.cpp`).
- Hooks at the returns of `func_ovlS_801CF310` and `func_ovlS_801CF340` answer
  the `family_of` class's line and mask for it (`config/hooks.toml`). The
  family table does not move, and the game's own walks of it (the classic
  menu's list, the known bits) are unchanged, so steps 2, 3 and 5 are not
  needed for this route.
- Hooks in `func_8009DD38` grow the requirements asset to 256 records and
  write the new classes' records; every other record past 80 is `0xFF`.
- `organize_edit.cpp` adds the standard new classes of the character's
  gender to the view's list, when the game's check does not refuse them for
  the line (reason 1). A new class changes through the port's direct apply,
  without the wardrobe animation, because the game's screen cannot select it.
- The Organize view and edit code read class records from the active class
  table (`class_table()`, the moved copy when classes are moved), and list a
  character of a class past 164.
- The example Dragon Knight joins the Centurion's line (`family_of` 34, the
  same line as Fighter), has no requirements, and keeps the Fighter's basic
  equipment, so a Fighter changes without buying gear. Its
  `give-class.lua` stays as an example of `game.set_class`.

The game's classic Change Class menu does not offer a new class. A character
of a new class is special to the current-class gates (2d), so the classic
menus treat it as they treat Dio's class.

Played 2026-11-01: the modern view over the game's Organize screen lists the
Dragon Knight for a Fighter (`func_ovlS_801D2378` passes, reason 0), the
change applies, the character fights with normal attacks, and the formation
sprite is the Centurion's after `portraits.cpp` read class rows from the
content database (it read the ROM's appearance rows, which end at 164). Opened
alone with F3, the view is read only and lists classes from class `+0x43`
without the game's rules, so it shows neither new classes nor the game's
special changes (Cleric back to Amazon).

The classic Change Class menu (built 2026-11-01, maintainer: players will use
the original UI; not played):

- The list's flag moves from `0x80` to `0x8000`, so an entry holds the whole
  class byte. `tools/relocate_tables.py` rewrites the two writers
  (`0x801D2888`, `0x801D29B0`) and the readers that masked `0x7F`
  (`0x801DC8A0`, `0x801E0BFC`, `0x801E387C`, and the draw's `0x801B8C30`,
  `0x801B8C70`). The screen's draw records (state `+0x32C`, 4 bytes) took the
  entry's low byte with the flag in bit 7; two hooks after those stores
  (`0x801B8384`, `0x801B8440`) put the flag in the record's byte `+1`, and the
  draw's flag read (`0x801B8C38`, `srl 7`) reads that byte.
- The list builder's two reads of the class line table (`0x801D2854`, the
  current class's line, and the walk at `0x801D2898`) use a 256-entry copy at
  `0x806FF2EC`, written at the builder's entry; the bound on the current class
  (`0x801D283C`) rises to 255 with it. The table's other readers keep the
  original. Without standard new classes the shift is 0 and the bound 81.
- The seen-classes byte for classes 168 and up comes from the port in the
  builder (`0x801D294C`); a standard new class is seen.
- The port's view reads the list's whole low byte too, and picks a new class
  through the game's screen when the game's list holds it (wardrobe
  animation), else through the direct apply.

The battle run-off of section 7 appeared with four packs and `give-class.lua`
loaded. It did not appear with no packs, with the Dragon Knight pack alone, or
with the rebalance and the Dragon Knight pack (Change Class route). Example Mod
(`soldiers.json`) and `drops.lua` were not tested on their own.

To make a new class a standard one, the 27 bounds rise to 256 and the
requirements buffer becomes one of 256 records (the asset's 81, then the
database's), which the port puts in place of the loaded one. Those 27 sites
are read before that change; some may compare something other than a class.
3. The known-classes bitfield is 21 bytes at `0x80196A96`, near the goth
   (`0x80196A8C`) in the army data the save holds. 256 classes need 32 bytes;
   what follows the 21 bytes, and whether it is saved, decides whether the
   bitfield moves too.
4. Addresses the scan cannot see: a class record pointer kept in data or a
   struct, or passed in from another function. `table_refs.py` finds where an
   address is built; a pointer built once and stored is found by reading the
   six `addiu` sites' callers.
5. Code mods: `small-large-units` writes the class size at the old address.
6. Art and text for a new class: its appearances row, name (the content
   arena already holds new names) and description.

## 5. Class ids for mods

Decided by the maintainer on 2026-10-31.

- A class id is one byte, so there are 256; the ROM uses 0..164, and id 255 is
  kept free as a marker. The game itself uses 165..171 for soldier figures
  (found 2026-11-01, section 7: the class animation resource has rows for 172
  ids, and 165..171 hold soldier animation variants). That leaves **83 new
  classes (172..254) shared by
  every installed mod**.
- A mod never picks a number. It names a new class with a text key made of its
  mod id and a name: `"my-mod.dragon-knight"`. Packs and scripts refer to the
  class by that key.
- The port assigns the next free id the first time it sees a key and keeps the
  assignment in a class id map stored with the saves (one map per battery save
  and per profile save, since a save holds class ids). A key keeps its id for
  good, whatever other mods are installed and in whatever order, so a save
  made with a mod loads with the same classes.
- A save that names a key whose mod is no longer installed keeps the id
  reserved; the characters of that class are shown as missing until the mod
  returns.
- When the 90 ids are used up, a further new class is refused and the MODS tab
  says why. Existing assignments are never reused for another key.
- A class may also be a changed copy of an original class (a JSON pack with an
  existing id), which uses no new id.

What a new class needs besides its class record, each a per-class table to
grow (section 2): its appearances row (portrait and battle model, art), its
class-change requirements record (stats, alignment, the classes it comes from;
read by the Change Class check `func_ovlS_801D2378`, still to be inventoried),
its class key and alias, and its known-classes bit. Then it appears in Change
Class like an original class.

Unique characters (Dio and the like) are two things: a class, which is the
above, and a character joining the army, which is a character record (100
slots at `0x80193BE0`, 0x38 bytes each) plus the story event that recruits it.
The event belongs to the cutscene player (R1); until then a Lua action can
grant a character at a chosen moment, once the character record and the
game's own recruit routine are read.

## 6. New classes as built (special classes)

Built 2026-11-01; checked offline, not played. The maintainer chose to make
new classes special classes first (section 4, item 2b): they exist, fight and
are saved, and Change Class does not offer them, as with Dio's class.

- **The appearance table moves too.** `tools/relocate_tables.py` now patches
  the appearance table's 36 reader `lui`s with `ogre_appearance_shift`; its
  copy is at `0x8060ED00` (same `0x48` shift). Both copies hold 256 records:
  the ROM's 165, then the bytes that follow the original table (mirrored every
  frame), except the records a new class owns. `OGRE_CLASS_TABLE_MOVE=0` turns
  both moves off, and then new classes are logged as unavailable.
- **The database holds 256 records** for the class and appearance tables
  (`TableInfo::capacity`). A record past 164 exists only in the moved copy:
  its writes are `moved_only`, because the original address holds the attack
  and item tables. Offline, adding a new class leaves the original layout's
  attack table unchanged.
- **A new class in a pack** is a record keyed `"<mod id>.<name>"` in
  `classes`, with `"copy_of"` (0..164): its class record and appearance row
  start as copies of that class as it stands at that point in the load order,
  then its fields and name apply. Its edits carry the key; the id is resolved
  when the packs apply. A key may also appear in `appearances` to change the
  new class's look with `"bytes"`.
- **Ids** come from `saves/class-ids.json`: a key gets the lowest free id from
  165 to 254 on first sight and keeps it (offline: a second start gives the
  same id). The MODS tab shows `New classes  n OF 90 IDS USED`, and a class
  refused because the ids ran out.
- **The registry is kept safe** (2026-11-02, review AW-04). A new id is saved
  before it is used: the file is written to `class-ids.json.tmp` and renamed
  over the old one, so an interrupted write leaves the old file, and a write
  that fails gives the class no id this session. A `class-ids.json` that
  exists and cannot be read turns new classes off for the session and is left
  as it is; the MODS tab shows `New classes  OFF: SAVES/CLASS-IDS.JSON IS NOT
  READABLE`. Starting the ids again would let a save's class number name
  another class. A repaired or removed file is read again when the mod files
  next change.
- **Lua:** `content.get/set` take a new class's key in place of its id;
  `content.class_id(key)`; `game.class_of(character)`; and
  `game.set_class(character, class)`, which writes the character record's
  class byte (`+0x11`) and class key (`+0x12`, from the class record's
  `+0x45`), the pair the Change Class check reads.
- **Example:** `app/content/examples/new-class/` makes
  `new-class-example.dragon-knight` (a Centurion copy with more STR and VIT)
  and turns the first Soldier in the army into one on the world map.

Checks (before ids moved to 172): offline, a new class got id 165; record 165 in the moved table equals
Centurion's in every byte but STR and the name pointer, which points at "Dragon
Knight"; its appearance row equals Centurion's; the original attack table is
unchanged; `game.set_class` wrote class 165 into a character. The pack test
checks five new-class files. Not played: a battle with a class-165 character,
its portrait, the Organize screen, and saving and loading it.

## 7. The class animation resource

Play, 2026-11-01: a character set to the first new class (then id 165) went
into battle drawn as a Soldier and dealt little damage.

Battle animation comes from asset `0x00315736` (ROM `0x8A9986`, 11008 bytes,
loaded raw). LordlyCaliber calls it "animation routes"
(`native/engine/animation_routes.h`: class-indexed entries of four 16-bit
handles, speeds, attributes, body kind, equipment art and a selector list,
which its `cloneClass` copies for an added class). Four loaders read it: main
`func_801B3BD8` (`0x801B3BDC`), bank AF `0x8021B960`, bank C `0x801BE740`,
bank D `0x80197D44`. Each one takes the asset's size (`func_8009DAF4`),
allocates a buffer (`func_80070F30`), loads it (`func_8009DBB8`), and stores
the buffer and three section pointers taken from header halfwords `+0x0C`,
`+0x12` and `+0x14` (main: `0x801BA6D0`, `0x801BA6DC`, `0x801BA6D4`,
`0x801BA6D8`).

The header is 0x24 bytes of section offsets (18 halfwords). The first block,
`0x24..0x584`, is 172 rows of 8 bytes, one per class id: row 1 (Soldier) is
`0001 0002 1001 1002`, row 34 (Centurion) `0041 0042 1041 1042`. Rows
165..171 exist and hold soldier animation variants (`0001 0002` with flags
`0x10`/`0x20`/`0x30`), so the game uses class ids 165..171 for soldier
figures. That is why id 165 drew as a Soldier, and why new class ids now start
at 172 (section 5).

Built 2026-11-01 (`app/features/class_animation.{hpp,cpp}`), not played.
LordlyCaliber's `animation_routes.cpp` names the five class-indexed sections
(header slot, bytes per class): `+0x00` handles 8, `+0x02` speeds 2, `+0x06`
body kind 1, `+0x0E` selector groups 2 (u16 pointers relative to that section
to selector records after the table; `0xFFFF` none), `+0x10` attributes 2;
header values are offsets from the resource start, and nothing else holds an
offset. When packs make new classes, two hooks at the start of the asset
loader's size and load functions (`func_8009DAF4`, ROM `0x02DEF4`;
`func_8009DBB8`, ROM `0x02DFB8`; `config/hooks.toml`) hand all four loaders an
extended copy: each class-indexed section grows from 172 to 256 entries at its
end, every header offset past an insertion point and every selector pointer
moves with it, ids past 171 start as class 0's entries, and each new class
takes its template's. With no new classes, or when the game loads with no
destination buffer, the game reads the ROM as before. Offline: 11008 bytes
become 12268; every original class's entries and selector records are the
same, the other sections are the same at their new offsets, and class 172
equals class 34 in all five sections.

Played 2026-11-01, the example script turning character 2 (a Fighter) into
the Dragon Knight on a save at day 165. Battles show the Centurion model. Two
faults appeared, and both also appear when the script gives the game's own
Centurion (class 34), so neither comes from id 172 or the extended rows:

- In battle the character attacks, runs off screen and is placed back instead
  of walking back. It persists with Centurion 34 at two front-row attacks (the
  rebalance gives three) and is not tied to gender (Fighter and Centurion 34
  are both male, class byte `+0x05`). Open; the Centurion command bonus
  (`OGRE_CENTURION_COMMAND=0`) is not yet excluded.
- The second battle's opening dialogue (Hugo with Dio and the lord in the keep,
  song 36) crashed in 2 of 5 runs with a changed class and in none of the
  runs without one. The fault is a read at guest `0xFFFFFFDC` inside the LZ
  decompressor `func_8007A110` (resolved with a linker map of the same build),
  so the dialogue passed it a pointer of about `-0x24`. A first crash with the
  class at 172 faulted in `osSendMesg` (`do_send`) with a queue pointer near
  `0x0091F000`. A hook at the decompressor's entry (`ogre_lz_check`) logs
  `[lz]` with the caller's return address when a pointer is outside RDRAM.

Checked and ruled out: the four loaders allocate by the hooked size; the
selector pointers are section-relative (`0x0158` = 172 x 2); bank C's
accessors (`0x801BE0FC..0x801BE730`) read only extended sections, the 80-entry
sections (indexed by the global `0x801CEAD0`) and the moved class table (its
20 `lui`s are relocated); the battle loader reads its sections through the
header. A pack reload between the size and load calls could overflow the
game's buffer; the load now writes only the size the game was given.
`OGRE_CLASS_ANIMATION=0` and `OGRE_ASSET_LOG=1` (`docs/guides/app-build.md`)
isolate the resource and list every asset load.

## 8. Recruits

Built 2026-11-01; played the same day: the example built character 20, a
level-5 Dragon Knight, and Organize listed it. `game.recruit(class, level)` (Lua) queues a new
army character; `app/features/recruit.cpp` builds it on the next update of the
mission field with the game's own character builder `func_ovlR_802150DC`, as
`boss_escorts.cpp` builds escorts:

- Arguments: `ref` (the first army record 1..99 whose class byte is 0, which
  is how `func_8016B774` skips a free one), the game's template buffer
  `0x801E85C0` with group 1 (`+0x07` class, `+0x08` level offset from the
  mission's base level `0x801F1055`, `+0x0D` 1), group 1, enemy 0, and the
  unit slot 0 at `[sp+0x10]`.
- With the enemy flag clear and a unit slot below 30, the builder writes the
  army record `0x80193BE0 + ref*0x38` (`0x80215148`): class and class key
  (`+0x11`, `+0x12`), stats rolled for the level (`func_8016F13C`), the four
  stored equipment slots cleared and the template's gear (0: the class's),
  `+0x33 = 0x80` (group 1; 0x82 for group 0), a name (`func_8016BD20(0, ref)`), and `+0x34 = ref`
  (`0x8021532C`).
- The builder's bank R (ROM `0x195430`) is loaded into the arena, and the
  field's bank P is loaded back when it was resident.
- The character stands in no unit.

`app/content/examples/new-class/recruit-dragon-knight.lua` recruits one
Dragon Knight at level 5 on the first mission field of a launch.

Open: recruiting on the world map (bank R's arena there is not checked);
the generated name; whether the save keeps every field the builder writes.

A save and a reload kept the recruited Dragon Knight (played 2026-11-01), so
R2's "done when" (a new class is recruited, fights and saves) is met.

## 9. Changing class again (built and played 2026-11-01)

A character of a standard new class is a standard character to Organize:

- `tools/relocate_tables.py` turns the "class < 81" tests of 2d into
  `ogre_class_standard(class)` (true below 81 and for a standard new class):
  bank S `0x801CEFA8`, `0x801D01AC`, `0x801D38A0`/`A4`, `0x801D3970`/`74`,
  `0x801D3BE4`, and the name generator's `0x8016BD9C` in `func_8016BD20`, so
  a recruit of a new class gets a generated name. The known-bit write's guard
  `0x801D73B4` stays, because the bitfield ends at class 167.
- `func_ovlS_801D379C` refuses Change Class for a class key (`+0x12`) of
  36..38 or 42 and up; its three key reads (`0x801D3878`, `0x801D3948`, and
  `0x801D3B48` for option 6, which the menu checks when Change Class is
  confirmed) answer a standard new class as its `family_of` class
  (`ogre_class_line_key`). Played 2026-11-01 before the third: the list lit
  the Fighter for a Dragon Knight (`func_ovlS_801D2378` passed) and option 6
  refused with code 9. Two diagnostic hooks log both checks for a character
  of a class past 80 when their result changes (`[change]` lines). Played
  after the third: option 6 allowed (`-1`) and the Dragon Knight changed back
  to Fighter in the game's own Organize screen.
- The class key table (`0x801EF3D8`, a byte per class; 0 for an ordinary
  class) has 165 entries followed by other data, nonzero from class 187 on.
  Its 14 readers (all bank S) move to a 256-entry copy at `0x806EF3D8`, 0 past
  164, while any new class exists. The names arena now ends at `0x806E0000`.
- The content database's tick writes both copies (class line and class key)
  every frame; the classic list's entry hook writes them again.
- The port's `named_character` (Discharge, Rename) treats a standard new
  class as standard.

Recruiting on the world map: `game.recruit` queues there, and the recruit is
built on the next mission field's update; the builder's bank R needs the
arena that the world map's modules may hold, which is not read.

## 10. Next

New art for a new class (its own appearance row, sprite and portrait), and
recruiting on the world map.
