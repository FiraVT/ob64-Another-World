# Design: mod folders and load order

Status: built 2026-10-31 (`ROADMAP.md` phase R0, modding plan M2). Checked
offline and by `content_pack_test` on every build; not played.

Code: `app/features/content_files.{hpp,cpp}`. The content database
(`design-content-database.md`) and the Lua runtime (`design-lua.md`) both load
from its one listing, so they agree on the files and their order. Safety:
`security.md`.

## 1. What a mod is

A mod is a folder with a `mod.json` and its files beside it:

```
user-content/
  iron-soldiers/
    mod.json
    soldiers.json    a content pack
    rules.lua        a script
    project.json     a LordlyCaliber project (optional)
```

`app/content/examples/` holds the example mods, each a folder with a
`mod.json` and a description: `balance-tweak` (one pack of named fields),
`loot-tweaks` (one Lua script), `example-mod` (a pack and a script),
`example-mod-addon` (a mod that `requires` example-mod) and `new-class` (the
Dragon Knight, a new class with art). `content_pack_test` copies them all into
a `user-content/` and fails the build if one is refused or a pack does not
parse. Loose `.json` and
`.lua` files and LordlyCaliber project folders (a `project.json` without a
`mod.json`) still load. In `user-content/` each of them is an item of its own
in the MODS tab, with the id `file:` plus its file or folder name, and the
player switches, orders and deletes it like a mod folder. A mod folder is for
a group of files that belong together, with a name, a version and a
description.

## 2. `mod.json`

| key | type | meaning |
|---|---|---|
| `id` | string, required | 1 to 64 of `a-z`, `0-9`, `-`, `_`, `.`; unique among installed mods |
| `name` | string | shown in the MODS tab (first 64 characters); the id if missing |
| `version` | string | shown in the MODS tab |
| `author` | string | shown in the mod's details (first 64 characters) |
| `description` | string | shown under the name and in the details (first 1000 characters, one paragraph) |
| `url` | string | the mod's page, shown as PAGE in the details; ENTER, A or a click opens it in the web browser. `http://` or `https://` only, at most 200 characters, printable ASCII without spaces, quotes, `\`, `<`, `>` or `` ` ``; any other value refuses the mod |
| `priority` | integer | -1000 to 1000, default 0; a higher priority loads later and wins a conflict |
| `requires` | list of ids | mods that must be installed, enabled and loadable; this mod loads after them |
| `enabled` | boolean | the default; `false` keeps the mod installed but not loaded until the player turns it on in the MODS tab (section 5) |

Keys that start with `_` are free for notes. A `mod.json` larger than 64 KiB
is refused.

## 3. Load order

1. `content/` beside the executable, then `user-content/` in the settings
   folder.
2. In each, loose files, LordlyCaliber folders and mods sort by priority (0 for
   anything but a mod), then by file or folder name.
3. The player's order from `mod-settings.json` (section 5) puts the mods it
   lists into the places those mods hold after step 2, in the player's order.
   A mod it does not list keeps its place.
4. A mod moves after the mods it requires in the same folder. A shipped mod in
   `content/` always loads before a player's mod, so a requirement across the
   two holds without a move.
5. A mod's own files load in file-name order.

The later file wins a conflict, and the MODS tab names both.

## 4. When a mod does not load

The mod's row in the MODS tab shows `REFUSED:` or `MISSING REQUIREMENT:` and
the reason, and the log has the same line. A mod is refused when:

- `mod.json` is not valid JSON, is larger than 64 KiB, nests deeper than 16
  levels, or has a value of the wrong type;
- its `id` is missing or not in the allowed form;
- another mod has the same id (both are refused, because which one the player
  meant is not knowable);
- a mod it requires is missing, disabled or refused itself (this chains);
- its requirements form a cycle.

## 5. The MODS tab and the player's choices

Code: `app/src/ui.cpp` (`Panel::load_content_mods`, the `ContentMod*` rows),
drawn by `app/ui/options.rml` in the game's Esc menu and as text on the start
screen.

The tab is one table of what the player put into `user-content/`: mod folders,
loose packs and scripts, and LordlyCaliber folders, in load order. `content/`
beside the executable is the game's own (today `class-rebalance.json`, Another
World's class rebalance) and is not listed; neither are the packs the port
makes while it runs. The columns are:

| column | what it shows |
|---|---|
| grip | drag the row with the mouse to move it |
| `#` | its place in the load order, from 1 |
| `ON` | the switch |
| `MOD` | name and version, the start of the description, and `MORE` / `LESS` |
| `STATUS` | `LOADED` (`ON` on the start screen), `OFF`, `PENDING: ON/OFF AT THE NEXT START`, `REFUSED: <reason>` or `MISSING REQUIREMENT: <reason>`; a loaded item adds `, n CONFLICTS` and `, n ERRORS` |
| `ORDER` | up and down |
| `DELETE` | erases the item for good |

Each status that is not a refusal ends with `(YOUR CHOICE)` when the player's
switch differs from the item's default, or `(DEFAULT)`. Switching an item back
to its default removes the choice from the file.

Opening a row (`MORE`, a click on the row, or ENTER/A on the name) shows its
details: the description; what it is (a mod folder with its id, version and
author, a pack file, a script file, a LordlyCaliber folder) and where; where
its on/off comes from; its requirements; the refusal reason; how many new
classes it adds and when switching it applies; and one line per file (edits,
names, new classes, parts not applied), per script (handlers), per refused
file, per script error and per conflict.

Keyboard and controller: UP/DOWN pick a row; LEFT/RIGHT move the focus
between the row's cells (switch, name, up, down, delete; the focus is outlined
on the selected row); ENTER or A uses the cell. The focus stays on the same
cell from row to row, so UP/DOWN then ENTER on `up` moves the next item too.
Mouse: each cell is a button, and a row dragged onto another row takes that
row's place.

`DELETE` asks first: a dialog over the menu, "Delete for good?", names the
folder or file that will be erased, with Delete and Cancel buttons. ENTER or A
(DELETE again) erases it; Esc, B, moving to another row or cell, or another
tab cancels. The start screen's text panel shows the same question in its hint
line. Only an item directly inside `user-content/` can be
erased (`content::files::erase`). While the game runs, an item that adds new
classes and is loaded cannot be deleted; the hint line says to turn it off and
restart first, because a save may hold its classes.

`Activate all` and `Deactivate all` switch every item. The line under the
table counts the items installed, on, off, refused and pending. A new class
refused for want of ids gets a line there too.

The choices are kept in `mod-settings.json` in the settings folder, never in
the mod's own files:

```json
{
  "order": ["iron-soldiers", "file:drops.lua", "example-mod"],
  "enabled": { "example-mod": false }
}
```

`order` lists item ids, first loads first, at most 1024. A partial list puts
the listed items into the places those items hold and leaves the rest where
they are; the tab always writes the whole list. `enabled` maps item ids to
`true` or `false`, at most 1024; an item it does not name uses its default
(`mod.json` `"enabled"`, or on for a loose item). An id is a mod id or
`file:` plus a name of 1 to 200 bytes without `/`, `\` or control
characters; it is only compared with the listed items, never used as a path.
Ids of items that are not installed stay in the file, so an item taken out and
put back keeps its place and its switch. The file is ignored as a whole when it
is larger than 64 KiB, nests deeper than 16 levels, is not valid JSON, or has
any id or value of the wrong form; every item then uses its default, the tab
says so above the table, and the next change in the tab writes a new file. The
tab writes the file through a temporary file and a rename.

### 5.1 When a change applies

On the start screen every change applies when the game starts.

While the game runs:

- A mod with only packs and scripts applies within about a second (section
  6), as an edited file does.
- A mod that adds new classes (a pack with new classes, or a LordlyCaliber
  project with added classes) keeps the on/off state it had when the content
  database first loaded, until the next start. New class art is built into
  the ROM image once at start (`class_art.cpp`) and new class ids go into
  `saves/class-ids.json`, so switching such a mod mid-game could leave a save
  holding classes whose art or records are missing. Its row shows `PENDING`.
  `content::held_mods()` is that set of mods and their start states; the
  database and the Lua runtime both pass it to `scan()`, so they load the same
  files, scripts of the held mod included.
- A mod installed after the start that adds new classes is held off.
- Load order changes apply live for every mod.

The maintainer chose to keep the choices in a separate file that overrides the
mod's default, and asked that the tab show plainly where each state comes
from. The maintainer confirmed the apply rule above (2026-11-01).

## 6. Reloading

The database and the Lua runtime each keep a fingerprint of every file in the
two folders and one level of subfolders (name, time, size), and of
`mod-settings.json`. About once a second they compare it; any change, a
`mod.json` or the player's settings included, re-reads the listing and
reloads.

## 7. Checks

`content_pack_test` builds a tree with a shipped pack, a loose pack, a mod at
priority -5, a mod that requires a later one, a disabled mod, a LordlyCaliber
folder, a folder without either file, and nine broken mods (a bad id, two mods
with one id, a missing requirement, a two-mod cycle, broken JSON, a wrong
type, a 70 KB `mod.json`, a `mod.json` 30000 levels deep). It checks the load
order and each refusal reason. For `mod-settings.json` it checks that the
player's order and switches apply, that a mod still loads after its
requirement, that turning off a requirement refuses the mod that needs it, that
a held mod stays as it was and is pending, that a write reads back, and that
each of ten broken files (broken JSON, not an object, a wrong type, an id that
is not a mod id, an id listed twice, a switch that is not a boolean, 1025 ids,
nesting 30000 and 100000 levels, over 64 KiB, a folder of that name) is
ignored and the default order loads. It also checks that a loose file is an
item with its own id that is ordered and switched like a mod, that four
`file:` keys with a path in them are refused, and that `erase` deletes a loose
file and a mod folder in `user-content/` and refuses a file in `content/`, a
file inside a mod folder, and `user-content/` itself.
Offline, the example mod copied into `user-content/` applied its pack and
script and showed as `MOD Example Mod 1.0  2 FILES`. In the game (2026-10-31)
it renames Soldier (class 1) to "Iron Soldier" with HP 150 and STR 80, the
change easiest to see; the log shows the script's line and the pack loading.
Renaming class 1 also moves the name pointer that marks segment B as loaded,
and the bridge keeps writing (`design-content-database.md` section 2.4).

## 8. Sharing a mod

A mod is shared as a GitHub repository with the topic `ob64-mod`. GitHub lists
every public repository with that topic at <https://github.com/topics/ob64-mod>, so the topic is the
index of shared mods and needs no server of this project. The author adds the
topic with the gear icon next to "About" on the repository's page. The
repository holds the mod folder (`mod.json` and its files); a player copies
that folder into `user-content/`.

## 9. Next

- Mod settings a script declares, shown in the tab.
- A mod package: a zip with `mod.json` at its root, unpacked into
  `user-content/`.
