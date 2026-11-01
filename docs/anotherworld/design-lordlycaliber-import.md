# Design: one content format, LordlyCaliber as an importer

Status: plan, 2026-11-01. Built: what `design-content-database.md` section 8
records (tier 1 records and names), and steps 2 and 3 (section 5.1), checked
by the pack test and not played.

Related: `design-ecosystem.md` (who owns what), `design-content-database.md`
(the pack model and the existing LordlyCaliber reader),
`design-new-classes.md` (the new-class runtime, R2), `security.md`.

## 1. Decision

- The port's JSON pack format is the one content format. Every system reads
  content from the in-memory `Pack` (`app/features/content_db.hpp`), and
  nothing else.
- A LordlyCaliber project is an input. `parse_lordlycaliber`
  (`app/features/content_json.cpp`) translates it into a `Pack`. The
  translation holds no game rules; the rules live in the code that applies a
  `Pack`, so a JSON pack and an imported project behave the same.
- The port builds no data editor. LordlyCaliber stays the editor for data,
  art and cutscenes.
- LordlyCaliber is not the only format, for these reasons:
  - A project describes one edited ROM. Its added classes and items have ids
    implied by array position (`addedClasses[i]` is class `165 + i`), so two
    projects that add a class both claim id 165. Port packs name a new class
    by a key and get an id from 172 up when they load
    (`saves/class-ids.json`), so packs stack.
  - Port systems with no LordlyCaliber equivalent (War Director, Lua, native
    cutscenes and maps, HD art) need a format the port defines.
  - LordlyCaliber edits revision 0. Port packs carry no addresses and work on
    both revisions.
  - The project format belongs to another author. Only the importer reads it,
    so a change to it breaks one file.

## 2. The rule for the two formats

Every concept the port reads has one entry in the mapping table (section 4):
the port's JSON field, and the LordlyCaliber section and bytes it comes from,
or "no equivalent". The table is kept in this document and checked by the
pack tests (section 6).

- When a port field is added, add its row. If LordlyCaliber has the same
  concept, use its meaning, and say where the meaning was verified.
- When the importer learns a new LordlyCaliber section, add its rows, and
  remove the section from the `not_applied` list it logs.
- A field whose meaning comes from LordlyCaliber is verified at the game code
  that reads it before it is used (AGENTS rule 3). LordlyCaliber's names are
  leads.

## 3. LordlyCaliber's format for added content

Read from rempred/LordlyCaliber `native/engine/project.cpp` and
`promotion.cpp` on 2026-11-01:

- `addedClasses`: an array. Each element has four hex strings: `record` (the
  class record, 0x48 bytes), `appearance` (the appearance row), `name` (the
  name in the game's encoding) and `description`. The id is
  `165 + index`; it is not stored. The class it was cloned from is not stored
  either: `copyClassVisuals()` copies the art at creation time, and the copies
  are saved in the art sections.
- LordlyCaliber's first added class is 165. The port starts new classes at
  172, because the class animation resource holds soldier rows for 165..171
  (`DECISIONS.md` 2026-11-01, "new class ids from 172"). The importer's key
  map (step 2) therefore always renumbers; an added class never keeps its
  LordlyCaliber id in the port.
- `addedItems`: an array of `record`, `name`, `description`; id `278 + index`.
- `classNames`, `itemNames`: renames, by id.
- `promotion`: `families` (a catalog of `{id, name, behavior}`, and
  `assignments`, index = class id, value = family id; `schemaVersion` 2 allows
  165 to 256 classes), `gates` (hex, 8 bytes per class), `masks` (hex, 1 byte
  per class) and an optional `flags` (1 byte per class; `0x80` = listed in the
  standard-equipment list).
- `animationRoutes`: written when there are added classes. When it is
  missing, LordlyCaliber's own import takes the entry of a source class with
  the same record bytes.
- Art: `spriteLibrary`, `avatarArtwork`, `armyArtwork`, `combatArtwork`,
  `iconArtwork`, `animationSequences`, `animationOverrides`.

## 4. Mapping table: classes

| port JSON (`classes.<key>`) | LordlyCaliber | status |
|---|---|---|
| the key | none; the importer makes `lc.<pack folder>.class-<165+i>` | to build |
| `copy_of` | not stored. Take the source of the class's `animationRoutes` entry; else the original class whose record bytes after the name pointer match most bytes; log the choice | to build |
| record fields (`str_base`, `equipment_0`, ...) | `addedClasses[i].record`, bytes after `+0x04` | to build (`Pack::set_record` exists) |
| appearance row | `addedClasses[i].appearance` | to build; the port copies `copy_of`'s row today and has no field for it |
| `name` | `addedClasses[i].name` | to build (`Pack::set_name_bytes` exists) |
| description | `addedClasses[i].description` | no port field; listed in `not_applied` |
| `change_class.family_of` | `promotion.families.assignments[id]` | to verify: the port's family is the game's family table (`0x801EF2EC`, u16 per class); LordlyCaliber's catalog ids must be matched to it |
| `change_class.str` .. `dex`, `alignment_min`, `alignment_max` | `promotion.gates`, 8 bytes at `id * 8` | to verify: the port's 8 requirement bytes are a record of the requirements asset that `func_8009DD38` loads; check that `gates` is the same asset and the same byte order |
| family mask | `promotion.masks[id]` | to verify against the bit masks at `0x801EF390`; the port answers the `family_of` class's mask today |
| `art.portrait`, `art.figure_front`, `art.figure_back` | `avatarArtwork`, `armyArtwork`, `spriteLibrary` | tier 4; until then the class draws as `copy_of` |
| battle animation | `animationRoutes` | tier 4; `class_animation.cpp` copies `copy_of`'s entry today |

Items get the same table when new item ids exist (ecosystem phase E).

## 5. Steps

Each step ends with the pack tests passing and the MODS tab log stating what
was applied and what was skipped.

1. **Record the decision.** This document, a `DECISIONS.md` entry, and a line
   in `design-ecosystem.md` section 5 that points here.
2. **Keys for LordlyCaliber ids.** The importer gives each added class the key
   `lc.<pack folder>.class-<165+i>`, and keeps a map from LordlyCaliber id to
   key for the whole project. Every later section that names a class by id
   (`records.classes`, `classNames`, `promotion`, and later `shops`,
   `encounters`) goes through that map. An id below 165 stays an original
   class. Done when a project that adds two classes and a JSON pack that adds
   one load together, and all three get different ids that stay the same
   across restarts.
3. **Added class records and names.** `addedClasses[i]` becomes a `NewClass`
   with `copy_of` from the rule in section 4, plus a whole-record edit from
   `record` and a name from `name`. Done when the class is given to a
   character with `game.set_class` and fights with the imported stats.
4. **Verify the promotion layout.** Read the consumer of each `promotion` part
   in the game code: `gates` against the requirements asset
   (`func_8009DD38`, the check `func_ovlS_801D2378`), `assignments` and
   `masks` against the family table and masks (`func_ovlS_801CF310`,
   `func_ovlS_801CF340`). Write the result into section 4. Ask the developer
   when a byte cannot be classified (AGENTS rule 1).
5. **Change Class for imported classes.** The verified `promotion` entries of
   an added class fill its `change_class` block. Changes to `promotion` for
   original classes become record edits on the requirements asset and the
   family table, if step 4 shows the port can apply them. Done when a project's
   added class is offered in the modern Change Class view with the project's
   requirements.
6. **Appearance.** Give `NewClass` an optional appearance row, used by both
   formats, and fill it from `appearance`. Done when an imported class's
   portrait and model indices are the project's.
7. **Art (tier 4).** Map `animationRoutes` into `class_animation.cpp`'s
   extended asset, and `avatarArtwork`/`armyArtwork` into the class art loader
   (`design-class-art.md`). This is its own plan once steps 2-6 are played.
8. **Added items** follow steps 2, 3 and 6 with `addedItems`, once new item
   ids exist (ecosystem phase E).

Steps 2 and 3 are the port side of ecosystem phase G for classes. Steps 4 and
5 need game-code reading before code. Step 7 waits on the art work.

### 5.1 Steps 2 and 3 as built (2026-11-01)

`parse_lordlycaliber` (`app/features/content_json.cpp`):

- Reads `addedClasses` first. Entry `i` is LordlyCaliber class `165 + i` and
  gets the key `lordlycaliber_class_key(pack id, 165 + i)`
  (`content_db.hpp`): `lc.<name>.class-<id>`, where `<name>` is the
  project's folder (or a loose file's name without `.json`), lower-cased, any
  other character as `-`, cut so the key is at most 64 characters, and
  `project` when nothing is left.
- Each entry becomes a `NewClass` with that key. `copy_of` is the original
  class (0..164) whose record matches the most bytes after the name pointer,
  the lowest id on a tie; the log line names the choice:
  `[content] <pack>: LordlyCaliber class 165 is lc.<name>.class-165, starting
  as class 34 (<n> of 68 bytes the same)`. `animationRoutes` is not used for
  this yet.
- The entry's `record` (bytes after the name pointer) and `name` become edits
  on the key, so the database gives them the class's port id (172 and up)
  when the packs apply. `appearance` and `description` are listed in
  `not_applied` (steps 6 and later).
- `records.classes` and `classNames` entries for an added id go to its key.
  Any other id past 164 is counted as before. An added record of the wrong
  size is now refused like any other record.
- Refusals: `addedClasses` that is not an array, more than 83 entries, an
  entry that is not an object, a `record` that is not 72 bytes of hex, and a
  `name` the name rules refuse.

Checked: `content_pack_test` (0 failed) adds eight key cases (two projects
that both add class 165 get different keys; odd and long folder names give
valid keys). The test runs without a ROM, and a LordlyCaliber project needs
the ROM's records, so the import itself is not run by the test. A one-class
test project (class 34's revision 1 record with `str_base` 120, named "Test
Knight") is for the first play test: put it in
`user-content/lc-test/project.json`, check the log line, and give the class
to a character with `game.set_class(..., content.class_id("lc.lc-test.class-165"))`.

## 6. Tests

`content_pack_test` gets a case per mapping row as it is built: a small
project JSON in the test, the `Pack` it must produce, and refusals for a
malformed section (wrong hex length, an `assignments` array shorter than the
class count, a `gates` string that is not a multiple of 8 bytes). The
importer reads untrusted input, so every size and index is checked before use
(`security.md`; a new route needs its own test, AGENTS rule 16).

## 7. Open questions for the developer

- Should the importer accept a project's edits to the promotion data of
  original classes, or only the entries of its added classes? The first
  changes how existing classes promote for every save that loads the project.
- Should an imported class's key use the project's folder name, which changes
  if the player renames the folder (and then the class gets a new id), or a
  name stored in the project? LordlyCaliber stores no such name; a `mod.json`
  next to the project could carry one.
- Should the port ask LordlyCaliber's author (ecosystem section 8) to store a
  stable key and the source class for an added class? Either would remove a
  guess from steps 2 and 3.
