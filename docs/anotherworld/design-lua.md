# Design: Lua scripts

Status: built 2026-10-31 (`ROADMAP.md` phase R0, modding plan M3). The
maintainer confirmed Lua 5.4 the same day. Checked offline; not played.

Code: `app/features/lua_runtime.{hpp,cpp}`, the sandbox in
`app/features/lua_sandbox.{hpp,cpp}`, its test in
`app/features/lua_sandbox_test.cpp`. Mod safety: `security.md`. Example:
`app/content/examples/drops.lua`.

## 1. What it is

Scripts hold rules; content stays in the content database
(`design-content-database.md`). A script reacts to game events, reads and
changes content records through the database, and reads a little game state.
A script's content changes become its runtime pack, so they apply after the
JSON packs and leave the game's tables only through the database's bridge.

## 2. Lua itself

Lua 5.4.8 (MIT), downloaded by CMake from `https://www.lua.org/ftp/` and
pinned by the SHA-256 lua.org publishes
(`4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`). Lua has
no CMake build, so `app/CMakeLists.txt` compiles its library sources (all of
`src/*.c` but `lua.c` and `luac.c`) into `ogre_lua`. They are compiled as C++:
a Lua error is then a C++ exception, so the port's API functions can hold
`std::string`s and other objects with destructors when they raise one. The
license is in `packaging/CREDITS.txt`. The browser build has no Lua.

## 3. Loading

- `content/*.lua` beside the executable (the project's scripts), then
  `user-content/*.lua` in the settings folder (the player's), each in file-name
  order: the same folders and order as the JSON packs. `content/examples/` is
  not loaded.
- One Lua state. Each script runs in its own environment table, whose globals
  fall back to the shared sandbox, so scripts do not see each other's globals.
- A script is loaded as text only (`luaL_loadbufferx` mode `"t"`).
- `lua::tick` (main thread, every frame, after `content::tick`) loads the
  scripts once the content database is loaded, and about once a second lists
  both folders; when a script was added, removed or changed, it closes the
  state, removes every script pack and loads all scripts again.

## 4. The sandbox

`app/features/lua_sandbox.{hpp,cpp}`; `security.md` is the full account and
lists each route a script could use to escape and how it is closed. In short:
the base, `table`, `string`, `math` and `utf8` libraries without anything that
loads code or touches files; text chunks only; each script in its own
environment with its own library copies; 64 MiB of memory; about 1,000,000
instructions per call, which `pcall` cannot catch; no `__gc` finalizers; 20
log lines a second per script. `print` writes to the log like `log`. Two tests
run on every build (`security.md` section 5).

A handler that runs past the time limit is switched off until the next reload;
a handler that raises any other error is switched off after 10 errors. Every
error is logged with a traceback: `[lua] user-content/x.lua frame: ...`.

## 5. The API

| name | what it does |
|---|---|
| `log(...)` | writes `[lua] <script>: ...` to the log |
| `on(event, handler)` | adds a handler; events below |
| `content.get(table, id, field)` | the current value of a named field (`design-content-database.md` 2.1), or the name for `"name"` |
| `content.set(table, id, field, value)` | changes a field in this script's pack; `"name"` renames |
| `content.clear()` | drops this script's changes |
| `content.count(table)` | the number of records |
| `content.class_id(key)` | a new class's id, or nil (`design-new-classes.md` section 6) |
| `game.mission()` | the mission index (`0x8018F4A1`) |
| `game.scene()` | the active scene (`0x03` field, `0x05` world map, `0x0D` battle) |
| `game.goth()` | the army's goth (`0x80196A8C`) |
| `game.class_of(character)` | an army character's class (1..99) |
| `game.set_class(character, class)` | puts an army character into a class (an id or a new class's key) |
| `game.recruit(class, level)` | a new army character of a class (an id or a new class's key) at a level 1..50, built by the game's own builder on the next mission field update; false when eight wait (design-new-classes.md section 8) |
| `game.character(character)` | `{class, level, exp}` of an army character (1..99): record bytes `+0x11`, `+0x13`, `+0x35` |
| `game.set_exp(character, exp)` | an army character's EXP, 0..99; the level stays |
| `game.unit(unit)` | the five member bytes of a unit (0..29 the player's, 30..59 the enemy's), leader first: 0 empty, 1..99 a character, 100+ a soldier |
| `options.get(key)` | one of the script's own mod's options (`design-mods.md` section 5.2): `true`/`false`, the choice's label, or the number; a loose script or another mod's key raises an error |
| `battle.fighter(n)` | `{class, hp, hp_max, side, out, ready}` of battle participant 0..19, or nil outside a battle or for an empty slot; `out` is flags bit 0, `ready` bits 0 and 1 clear |

`content.get`/`set` take a new class's key in place of its id for `classes`
and `appearances`.

Tables are `"classes"`, `"attacks"`, `"spells"`, `"items"`, `"consumables"`
and `"rewards"` (`spells`: rows 0..4 for attacks 45..48 and 145,
`design-content-database.md` section 7.0). A wrong table, id, field or value raises an error.

| event | thread | when | handler |
|---|---|---|---|
| `mission_load` | game | bank R's mission loader, before any unit is built | no arguments |
| `field_update` | game | the mission field update's exit | no arguments |
| `field_frame` | game | the field camera pan's exit, every field frame | no arguments |
| `item_granted` | game | the item grant routine's entry | the item word (bit 15 set: equipment id, else consumable id); return another word to grant it instead |
| `frame` | main | every frame | no arguments |
| `battle_start`, `battle_end` | main | the battle bank (ROM `0x22A250`) becomes resident in scene `0x0D`, and leaves | the player's unit (0..29), or -1 |
| `battle_attack` | game | `ogre_battle_pick_attack` (`0x801C89E4`), after a battle form rule | the fighter (0..19) and the attack; return an attack 0..158 |
| `battle_attack_count` | game | `ogre_battle_attack_count`, before each store of the fight's attack count | the fighter and the count; return 0..15 |
| `battle_status` | game | `ogre_battle_status` (`0x801ED000`, `0x801EF6EC`), when an attack's status lands on a target, after its `status_chance` | the fighter, the target (0..19), the attack and the status; return 0 to stop it, or a status 3..8, 10 or 11 |
| `battle_dot` | game | `battle_dot.cpp`, each damage over time tick (`design-content-database.md` section 7.5) | the target (0..19), the source fighter, the attack whose formula it uses, the stacks and the damage; return the damage 0..9999 (0 skips the tick) |
| `battle_round_end` | game | `battle_dot.cpp`, at the end of each round of a fight, after the round's ticks | the round (1, 2, ..); a hook for round-based rules |
| `class_alignment` | game, main | the Change Class and qualify checks' alignment loads; the Organize view's chart | the limit byte; return 0..255 (255: no limit) |
| `unit_size` | game, main | the Organize rules (bank S), `organize_edit.cpp`, the war director's `place_cost` | a class's size (1 or 2); return the places it takes, 1..2 |

The four game events are the existing event table (`game_events.hpp`,
`config/hooks.toml`). The last four are filter events: the core calls
`lua::filter` at one of the game's decisions, each handler gets the value the
previous one returned, and a value outside the range is ignored. The Another
World pack uses them (`design-another-world-pack.md` section 7).

A handler may call `on()`. The new handler runs from the next dispatch of its
event: the dispatch loop indexes the handler list and finds each handler again
after its call, because adding to the list can move it (review AW-01,
2026-11-01; the loop used to hold a reference across the call).

## 6. Threads

The game events run on the game thread and `frame` on the main thread. One
recursive mutex guards the Lua state. `content.set` changes the script's pack
in memory. The pack keeps one edit per field and one name per record
(`Pack::keep_one_edit`, `keep_one_name`), and a write of the value the pack
already holds does not mark it changed, so a script that writes every frame
neither grows the pack nor republishes it (review AW-05, 2026-11-02).
`lua::tick` publishes changed packs on the main thread with
`content::set_runtime_pack`. `content.get` takes the database's
`records_mutex()`, which `apply_packs` holds, so a read from the game thread
never sees a half-applied pack. The lock order is always the Lua lock, then the
records lock.

## 7. Checks

Offline, with the real runtime and Lua against an 8 MiB RAM image filled from
the ROM, and the event table stubbed so the test fires `item_granted` itself
(the game was not run):

- `examples/drops.lua` copied into `user-content/`: reward row 0's first drop
  in RAM becomes `0x8001` (Short Sword); granting consumable 1 (Heal Leaf)
  grants 2 (Heal Seed); an equipment word passes unchanged.
- The script deleted: after the reload the drop is the ROM's `0x8031` again and
  Heal Leaf is granted as itself.
- A script that reads `io`, `os`, `load` and `string.dump` sees `nil`; an
  unknown field raises an error with a traceback; a `frame` handler with an
  endless loop stops at the limit and is switched off; an `item_granted`
  handler that raises an error leaves the item as it was.
- The class rebalance RAM is unchanged (0 bytes differ).

## 8. Next

- Named game state (`game.characters[i]`, `game.units[slot]`), each field with
  its address and evidence in `game_addresses.hpp`.
- Actions: `grant_item`, `alert(text)`, `set_flag`.
- The planned events (`unit_built`, `battle_start`, `battle_end`,
  `drop_roll`, `town_liberated`, `menu_open`) as hooks are added.
- Settings a script declares, shown in the MODS tab.
- A script per mod folder (`main.lua`) once `mod.json` exists.
