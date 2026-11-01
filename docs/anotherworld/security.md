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
  attacks, items, consumables, appearances, battle rewards) and the content
  arena (guest RAM `0x80600000`..`0x806FFFFF`: the moved class and appearance
  tables and the names). A new class's record is written only in the arena
  copy. Every edit is checked against its
  table's record count and record size before the pack is accepted, and the
  arena has a fixed end. Guest RAM is the emulated N64's memory, which the port
  allocates; a pack never writes host memory or files.
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
  `user-content/` (its parent, resolved, must be `user-content/` itself), never
  `content/`, a file inside a mod folder, or `user-content/` itself. A link is
  removed as a link; what it points to is kept.
- **Hostile files.** A file larger than 16 MiB is refused unread. A document
  nested more than 64 levels is refused before parsing, because a very deep
  document is destroyed recursively and could overflow the stack. Every JSON
  error, including a value of the wrong type where the loader reads one, is
  caught and refuses the whole file with its reason in the log.
- **Values.** Each field takes only values that fit its width and sign; names
  take 1 to 32 printable ASCII characters a line and at most two lines.
- **LordlyCaliber added classes.** `addedClasses` takes at most 83 entries
  (the free new class ids), each `record` exactly one class record (72 bytes)
  and each `name` a name in the game's encoding with the same limits. Each
  becomes a new class with a generated key, so its record goes only into the
  arena copy, as a JSON pack's new class does.

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
content records through the content database (section 2's limits apply),
reads a few game values, and `game.set_class` writes two bytes of one army
character's record (its class and class key; the character is checked to be
1..99 and the class to be an existing id or a pack's new class).
`game.recruit` queues at most eight new army characters, each a class (an
existing id or a pack's new class) at a level 1..50, which the game's own
character builder writes into a free army record on the mission field; its
argument checks are cases in `content_pack_test`.

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
  `mod.json` `"url"` (two kept, and nine refused: `file:`, `javascript:`, no
  scheme, a scheme alone, a space, a quote, a control character, 220
  characters, a number), and 19 files, one valid and 18 hostile: broken JSON, 100,000 levels of nesting, a file over
  16 MiB, record ids out of range and negative, values too large, floats, byte
  edits past a record and far outside it, a whole record of the wrong size,
  bad hex, names with control characters, too long, or on a table without
  names, an unknown table, and LordlyCaliber projects with fields of the wrong
  type. Keys for the classes a LordlyCaliber project adds
  (`lordlycaliber_class_key`) are checked to be valid class keys of at most 64
  characters for odd folder names.

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
