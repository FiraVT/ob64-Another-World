# Security: what a mod can and cannot do

Status: 2026-10-31. This file is the reference for mod safety. Keep it true:
a change to the Lua sandbox, the script API, or the content pack loader updates
it, and adds a case to the build-time tests (section 5).

Mods are files from other people. A player who installs one should not have to
trust its author with their computer. This file says what each kind of mod can
reach, how the port enforces that, and what is not covered.

## 1. The kinds of mod

| kind | what it is | can it run code on the computer? |
|---|---|---|
| JSON content pack (`content/`, `user-content/`) | data: records, fields, names | no |
| LordlyCaliber project (`project.json` folder) | data: records, names and added classes | no |
| Lua script (`*.lua`) | code, run in the sandbox (section 3) | no: only the sandbox's functions |
| `.nrm` code mod (`mods/`) | N64 code that the runtime recompiles | not covered here (section 6) |
| `.ips` patch (`patches/`) | bytes written into the ROM image | no: the ROM image only (section 6) |

## 2. Content packs and LordlyCaliber projects

A pack is data. It runs no code; the risks are a crash and a write somewhere it
should not go.

- **What it can read.** A new class's `art` block names PNG files. Each name is
  checked before anything is read (`is_art_path`): a relative path of at most
  128 characters inside the pack's folder, made of letters, digits, `-`, `_`,
  `.` and `/`, with no empty, `.` or `..` part, ending in `.png`. Cases in
  `content_pack_test` cover a path that climbs out, a root path, a drive
  letter, a backslash, another extension and an unknown field.

- **Where it can write.** Only the content tables in guest RAM (classes,
  attacks, spell maps, items, consumables, appearances, battle rewards) and the content
  arena (guest RAM `0x80600000`..`0x806FFFFF`: the moved class and appearance
  tables and the names). A new class's record is written only in the arena
  copy. Every edit is checked against its
  table's record count and record size before the pack is accepted, and the
  arena has a fixed end. Guest RAM is the emulated N64's memory, which the port
  allocates; a pack never writes host memory or files.
- **Attack statuses** (`status`, `status_rule`, `status_chance` of an attack,
  `design-content-database.md` section 7.4). `status` and `status_rule` write
  bytes `+0x0A`, `+0x0B` and `+0x0D` of an attack record inside the attack
  table, and only the ids the game's own status code has (statuses 0, 3..8,
  10, 11; rules 2, 4, 5, 6, 11). `status_chance` is a whole percent 0..100
  kept by the port; the hook (`battle_status.cpp`) uses it to set the strike's
  own result register and writes nothing else. Eleven cases in
  `content_pack_test` cover the accepted forms, an unknown name, status 16,
  a status past a byte, an unknown rule, a chance over 100, a negative chance,
  a chance as a string and a status on another table, four more check the
  bytes each form writes, and five check `valid_attack_status`, the range of
  the `battle_status` event.
- **Damage over time** (`dots`, an attack's `dot` and `dot_chance`,
  `design-content-database.md` section 7.5). Every setting is a whole number
  in a fixed range (power 1..100, stacks 0..9, damage down 0..100, a visual
  kind 0..255 and value 0..65535, a tint 0..255, an attack 1..158), a label
  is 1..16 of letters, digits, space and `! ' . -` (no markup reaches the
  overlay), a label color is `#RRGGBB`, a label image is a PNG path inside the
  pack's folder (`is_art_path`: no `..`, no root, no drive), a status is one of the
  four the game keeps in a fighter's flags, a name is 1..32 of a-z, 0-9, `_`
  and `-`, and a pack holds at most 32 effects. During a battle the port
  writes only inside the battle's participant table and the event timeline,
  through the game's own routines: a fighter's HP and flags by
  `func_ovlX_801E7508`, events by the game's schedulers, its time cursor
  (`+0x94`), and the source's action words (`+0x78`, `+0x7C`), which it puts
  back after the damage call. Thirty-two cases in `content_pack_test` cover
  the accepted forms, a section that is not an object, bad names, every
  setting out of its range, an unknown setting, 33 effects, an attack `dot`
  that is not a name, a chance over 100, a `dot` on another table, a label
  with markup, a label too long or empty, a label color without `#` or not
  hex, and a label image that climbs out, starts at a drive or is not a PNG, one
  checks the settings read, and three check `valid_dot_status`.
- **Story units** (`story_units`, `design-content-database.md` section 7.6).
  A unit is a mission 0..63 and an enemy slot 31..59, both whole numbers. The
  port only reads the list: a listed unit is left out of the enemy features'
  rewrites (`war_director.cpp`'s garrison, `boss_escorts.cpp`), so a pack
  writes nothing through it. A pack holds at most 256 units. Nine cases in
  `content_pack_test` cover the accepted form, a section that is not an array,
  the boss slot 30, slot 60, mission 64, a missing slot, a slot as text, an
  unknown setting and 257 units, and one checks the units read and the port's
  own list.
- **Battle forms** (`battle_forms`, `design-battle-transform.md`). A rule
  makes the port write, during a battle, the class and class key words (`+0x48`,
  `+0x4C`) of a fighter in the battle's participant table, which is guest RAM
  the game allocated; it writes only participants the game has in its table
  (20 slots, checked against the table's start and stride) and puts their own
  words back at the next strike, the round reset and the battle's end. Every
  class and form is checked before the pack is accepted: a class id 1..164 or
  a new class key, an attack id inside the attack table, 1..9 strikes and
  attacks, form lists of 1..32 valid classes, attack lists of 1..32 valid
  attack ids, `"breath"` as the only attack word, a boolean `repeat`. Nineteen
  cases in `content_pack_test` cover a rule that is not a list, a missing
  class, a class past the table, class 0, an attack past the table, no
  strikes, ten strikes, ten attacks, an unknown field, an empty form list, a
  list of 33, a list with a class past the table, an empty attack list, an
  attack list of 33, an attack list with an attack past the table, `"breath"`
  inside an attack list, another attack word and a `repeat` that is not a
  boolean. A form whose figure
  the battle cannot load is skipped, because the game's draw spins on a missing
  figure.
- **What it can read.** Only its own file. The port lists `content/` beside the
  executable and `user-content/` in the settings folder, and one level of
  folders in each (mod folders, `design-mods.md`); a pack or a `mod.json`
  cannot name another file. A folder that is a link to somewhere else on the
  disk is followed, but only `.json` and `.lua` files in it are read, as packs
  and scripts, so nothing outside the game's tables is written and nothing is
  sent anywhere.
- **`mod.json`.** Larger than 64 KiB is refused unread, and deeper than 16
  levels is refused before parsing; every JSON error and wrong type refuses
  the mod; the id is limited to 64 of `a-z 0-9 - _ .`, and names, versions and
  descriptions are cut at 64, 32 and 1000 characters (control characters in a
  description become spaces).
- **`mod.json` `"url"`** (the mod's page, `design-mods.md` section 2). The MODS
  tab hands it to the system's web browser (`SDL_OpenURL`) only when the player
  activates its PAGE row; nothing opens it by itself. Only `http://` and
  `https://` are accepted, at most 200 characters of printable ASCII with no
  space, quote, backslash, angle bracket or backtick (`content::files::valid_url`),
  so a `file:`, `javascript:` or other scheme, or a value that could split a
  command line, never reaches the system; a mod with any other `"url"` is
  refused. The address is checked again right before it is opened.
- **Mod options** (`mod.json` `"options"` and `mod-options/<id>.json`,
  `design-mods.md` section 5.2, `app/features/mod_options.cpp`). A declaration
  outside the limits (32 options, keys of `a-z0-9_`, names and help without
  control characters, 2 to 8 choices, whole numbers within ±1,000,000 and at
  most 1000 steps) refuses the mod. A values file is read as untrusted: larger
  than 16 KiB, nested deeper than one object or not JSON is ignored, and each
  value is checked against its declared option, else the default stands. Keys
  are only compared with the declared ones. The file name is the mod id
  (`valid_id`: no `/`, `\` or `..` path), with a leading `_` for a Windows
  device name or a leading `.`, inside `mod-options/`. A script reads only its
  own mod's options (`options.get`); it cannot write them.
- **`mod-settings.json`** (the MODS tab's choices, in the settings folder;
  `design-mods.md` section 5). The port writes it, but a player or another
  program can edit it, so it is read as untrusted. Larger than 64 KiB is
  ignored unread, deeper than 16 levels is ignored before parsing, and every
  JSON error, wrong type, id that is not a mod id, repeated id or list of more
  than 1024 ids makes the port ignore the whole file and use each mod's
  default. It holds only item ids and booleans. An id is a mod id or `file:`
  and a name without `/`, `\` or control characters, and it is only compared
  with the items the port listed, never opened as a path, so the file can only
  reorder and switch items that are already installed. The port writes it to a
  temporary file beside it and renames that over it.
- **Deleting from the MODS tab** (`content::files::erase`). Only the player's
  action deletes, and it asks twice; no pack, script or settings file can
  trigger it. It erases only an item the listing found directly inside
  `user-content/` (its parent, resolved, must be `user-content/` itself), or a
  shipped mod folder with its `mod.json` directly inside `content/` beside the
  executable; never a loose file in `content/`, `content/` itself, a file
  inside a mod folder, or `user-content/` itself. `content_pack_test` has a
  case for each. A link is
  removed as a link; what it points to is kept.
- **Hostile files.** A file larger than 16 MiB is refused unread. A document
  nested more than 64 levels is refused before parsing, because a very deep
  document is destroyed recursively and could overflow the stack. Every JSON
  error, including a value of the wrong type where the loader reads one, is
  caught and refuses the whole file with its reason in the log.
- **Values.** Each field takes only values that fit its width and sign; names
  take 1 to 32 printable ASCII characters a line and at most two lines. A
  `spells` field takes only an attack 1..158 that is not a generic magic
  attack (45..54, 145), so a spell map never points past the attack table or
  back into another map (`spell_target`, checked by `Pack::set_field` for
  packs, LordlyCaliber projects and Lua alike).
- **LordlyCaliber added classes.** `addedClasses` takes at most 83 entries
  (the free new class ids), each `record` exactly one class record (72 bytes)
  and each `name` a name in the game's encoding with the same limits. Each
  becomes a new class with a generated key, so its record goes only into the
  arena copy, as a JSON pack's new class does. A class link or row attack
  inside a class record that names an id the port does not have is left out
  and listed (`translate_class_ids`, `design-lordlycaliber-import.md`
  section 5), so an imported record never points past the class or attack
  table.
- **LordlyCaliber attacks** (`read_lordlycaliber_attacks`,
  `design-lordlycaliber-import.md` section 4.1). The section must be version
  1 with at most 254 entries. Each entry's `data` is exactly 12 bytes of hex
  and becomes edits of the bytes after the name pointer of an attack 1..158;
  an entry past 158 is counted and skipped. A `name` is 1..128 bytes of hex,
  and one the name rules refuse is counted and skipped. Every setting must be
  an integer; settings are compared and listed, never written. A spell link
  writes only through `spells` fields, with the value check above. Ten
  refusal cases in `content_pack_test` cover a wrong version, entries that are
  not a list, 255 entries, an 11-byte record, a missing record, a name that is
  not hex, a 129-byte name, spell links that are not an object, a spell link
  that is not a number and a setting that is not an integer. They run on a
  synthetic ROM image (zeros, the revision 1 check word and the spell rows),
  because the importer compares with the ROM's records.

A pack can still change the game in ways its author did not intend, for
example give a class absurd stats. That is a game problem, not a computer one;
removing the file undoes it.

## 3. Lua scripts

Scripts run in Lua 5.4.8 inside a sandbox (`app/features/lua_sandbox.{hpp,cpp}`).

### 3.1 What a script can reach

Only these: the base functions `assert`, `error`, `getmetatable`, `ipairs`,
`next`, `pairs`, `pcall`, `rawequal`, `rawget`, `rawlen`, `rawset`, `select`,
`setmetatable`, `tonumber`, `tostring`, `type`, `xpcall`; the `table`,
`string`, `math` and `utf8` libraries; and the port's API (`log`, `on`,
`content.*`, `game.*`; `design-lua.md` section 5). The API reads and changes
content records through the content database (section 2's limits apply,
including the `spells` value check),
reads a few game values, and `game.set_class` writes two bytes of one army
character's record (its class and class key; the character is checked to be
1..99 and the class to be an existing id or a pack's new class).
`game.recruit` queues at most eight new army characters, each a class (an
existing id or a pack's new class) at a level 1..50, which the game's own
character builder writes into a free army record on the mission field; its
argument checks are cases in `content_pack_test`. `game.set_exp` writes one
byte, the EXP of an army character 1..99, with a value 0..99.
`game.character`, `game.unit` and `battle.fighter` read a character record
(1..99), a unit's five member bytes (0..59) and a battle participant (0..19);
`battle.fighter` reads only while the battle bank is resident and only inside
RDRAM. Their range checks are `lua_runtime.hpp` `valid_character`,
`valid_exp`, `valid_unit` and `valid_fighter`, and cases in
`content_pack_test`. `options.get` reads a value of the script's own mod's
options and nothing else (section 1, mod options). The filter events
(`design-lua.md` section 5) return a
number to the game's code only inside the event's range (an attack 0..158, an
attack count 0..15, an alignment byte, 1 or 2 places, a status the game has:
`valid_attack_status`, with cases in `content_pack_test`); any other return
keeps the game's value. `battle_status` writes the status it returns into the
strike's own stack slot for the status, the byte the game's immunity check
reads next. `battle_dot` returns a tick's damage, 0..9999.

### 3.2 What a script cannot reach

| route | how it is closed |
|---|---|
| files, the OS, other programs | `io`, `os`, `package`, `debug` and `coroutine` are never opened |
| loading other code | `load`, `loadfile`, `dofile` and `require` are removed |
| precompiled bytecode (can break Lua's own checks) | chunks load in text mode only; `string.dump` is removed, also from the string library behind every string's methods |
| the state's globals, or another script | each script gets its own environment with its own copies of the library tables, no metatable, and no path back; the shared string metatable answers `false` to `getmetatable` |
| memory | the state's allocator refuses to go past 64 MiB; Lua turns that into a "not enough memory" error |
| time | a call stops after about 1,000,000 instructions; the stop is raised again at every later step, and the sandbox's `pcall` and `xpcall` pass it on, so a script cannot catch it and keep running |
| finalizers (`__gc`), which Lua runs with the instruction counter off | `setmetatable` refuses a metatable that has `__gc` when it is set, which is the only time Lua marks an object for finalization |
| deep recursion | Lua's own stack and C-call limits turn it into an error |
| the disk through the log | `log` writes at most 20 lines a second per script, each cut at 500 characters |
| files larger than 1 MiB | not loaded |

A handler that runs past the time limit is switched off until the next reload;
one that raises any other error is switched off after 10 errors. A script error
never stops the game; it is logged with a traceback.

### 3.3 Known limits

- **A string pattern can freeze the game.** `string.find` and its relatives run
  inside one C call, which the instruction counter cannot interrupt, and some
  patterns backtrack for a very long time on a long string. The game then stops
  responding until it is closed. It cannot reach the computer: the memory limit
  holds. A fix would replace the pattern functions with ones that count steps.
- **Scripts share one Lua state**, so one script using its 64 MiB leaves none
  for the others. The limit is per state.
- **A script changes the game.** Within the API it can do what a pack can,
  and replace granted items. Removing the file undoes its content changes.

## 4. Threads

Game events run on the game thread and the `frame` event on the main thread.
One lock guards the Lua state, and reads of the content database take its
records lock (`design-lua.md` section 6). A script cannot start a thread.

## 5. Checks on every build

`app/CMakeLists.txt` builds two tests with the game and runs them after each
build; if a case fails, the build fails.

- `lua_sandbox_test` (`app/features/lua_sandbox_test.cpp`): 22 checks, 19 of
  them escape attempts covering each route in section 3.2 (file and OS access, loading code,
  bytecode, `string.dump` through a string, the string metatable, reaching the
  globals, an endless loop, a loop that catches the time limit with `pcall` and
  with `xpcall`, a `__tostring` loop, a 1 GiB string, a doubling string, a
  growing table, deep Lua and C recursion, finalizers set at once and added
  later, and one script changing another's library), plus three checks that
  `pcall` and `xpcall` still work for ordinary errors and that an error object
  that is not a string is reported.
- `content_pack_test` (`app/features/content_pack_test.cpp`): mod folders (the
  load order, and ten broken mods each refused with its reason), the player's
  `mod-settings.json` (order, switches, a held mod, loose files as items, four
  `file:` keys holding a path, and ten broken or hostile files that must be
  ignored with the defaults loading), `erase` (two deletes inside
  `user-content/` and three refusals outside it: `design-mods.md` section 7),
  mod options (a good declaration read, twenty hostile ones refused, and
  eleven values-file and session cases), `mod.json` `"url"` (two kept, and nine refused: `file:`, `javascript:`, no
  scheme, a scheme alone, a space, a quote, a control character, 220
  characters, a number), 62 files, 8 valid and 54 hostile (section 2 lists the fifteen battle form cases), among them: seven `spells` refusals (a row past the table, a generic attack, attack 145, attack 0, an attack past the table, an unknown field, a name), broken JSON, 100,000 levels of nesting, a file over
  16 MiB, record ids out of range and negative, values too large, floats, byte
  edits past a record and far outside it, a whole record of the wrong size,
  bad hex, names with control characters, too long, or on a table without
  names, an unknown table, and LordlyCaliber projects with fields of the wrong
  type. Keys for the classes a LordlyCaliber project adds
  (`lordlycaliber_class_key`) are checked to be valid class keys of at most 64
  characters for odd folder names. Four cases check that a script's pack keeps
  one edit per field (600 writes of one value leave one edit). Last, on a
  synthetic ROM image: six `saves/class-ids.json` cases (an unreadable file
  gives no id and is left as it is, a removed one turns new classes back on,
  a failed save gives no id and keeps the file); twenty LordlyCaliber
  `attacks` cases: ten accepted projects whose edits and `not_applied` lines
  are checked, and the ten refusals in section 2; and thirteen LordlyCaliber
  class cases: `records` and `itemNames` of the wrong type refused with the
  error of their own check (without the ROM's records the importer stops
  earlier, so these two cases did not reach their checks before 2026-11-02,
  review AW-06), and class links and row attacks translated, left out or
  kept, read back from the applied database.

The tests found two holes while they were written (2026-10-31): a finalizer
with an endless loop hung the test, because Lua runs finalizers with hooks off,
and a LordlyCaliber project with `"version": "x"` threw an exception that would
have closed the game. Both are fixed.

## 6. Not covered

- **`.nrm` code mods** are N64 code that N64ModernRuntime recompiles at start.
  Their isolation is the runtime's and is not reviewed here; install them only
  from people you trust. The port ships its own examples.
- **`.ips` patches** change the ROM image in memory before the game starts
  (`app/features/rom_patch.cpp`); a record past the end of the ROM is skipped.
  They change game data only.
- **The game itself.** A mod can make the game unplayable or a save unusable,
  as any mod can. Keep backups (the SAVES tab).

## 7. When you change the sandbox or the API

- Add a function to the script API only if it reads or writes game content or
  game state through the content database or `game_addresses.hpp`. Never expose
  files, processes, the network, host memory or another script's state.
- Check every argument in the API function (type, table, id, field, range)
  and raise a Lua error for a bad one; the error unwinds as a C++ exception, so
  locals with destructors are safe.
- Add a case to `lua_sandbox_test.cpp` or `content_pack_test.cpp` for any new
  route, and update section 3.2 or section 2 here.
