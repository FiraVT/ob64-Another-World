# Roadmap: from a recompiled port to a modern remake

Status: direction set by the maintainer on 2026-10-31 (`DECISIONS.md`).
OB64: Another World is an open-source community project under GPL-3.0-or-later:
anyone can pick up a system or a phase below, and every phase is open to
contributions. This is
the top-level plan for OB64: Another World. The design documents in this folder
describe single systems; this file says how they fit and in which order they
are built. `FEATURES.md` lists what is built today.

## 1. Direction

OB64: Another World is a modern remake of Ogre Battle 64. It builds on the
recompilation and preservation work of the projects it credits, and goes past
it: the goal is a game that keeps growing. The original campaign is the first content set; new classes, units, cutscenes,
missions and larger maps follow it, and adding them must not need engine work.
The same data and scripts would in principle let someone build a whole new
campaign. That is a side effect of the design and not a goal of this project.

The recompiled N64 code is the starting engine. It runs the game today, and the
project replaces it one system at a time with new native systems.

## 2. Principles

1. **Replace systems; do not work around the engine.** A system the project
   wants to extend gets a native replacement (C++, RmlUi, data files, Lua)
   instead of new code that works inside the N64 engine's formats and limits.
   The recompiled code stays as the reference and the fallback until the native
   system covers it.
2. **Data and scripts build the game.** A native system reads its content from
   data files and its rules from Lua. The project's own content uses the same
   formats and the same API as mods, so a modder can do what the project does.
3. **Original formats are import formats.** A game format (the cutscene step
   table, the map data, the class table) is decoded once, to import the
   original content into the native format. New content is never authored in
   an N64 format.
4. **Changes are easy to make, and LordlyCaliber content comes in.** The
   port's own data files are the main way to change the game: plain files the
   port reads at start and reloads while it runs, with no separate tool, no ROM
   build and no particular ROM revision needed. LordlyCaliber projects are an
   import source, so content made in rempred's editor plays in the port
   (`design-ecosystem.md`). LordlyCaliber is also a research source; this
   project writes its own native systems (cutscene player, field, battle).
5. **Faithful pixel art.** The original art is reused and drawn crisp at any
   resolution, and new art matches its style. No AI upscaling.
6. **Reverse engineering stays exact.** Replacing a system starts from knowing
   what the original does. The rules in `AGENTS.md` (verify at instruction
   level, do not fabricate state) still apply to every reading of the original
   code.

## 3. Systems

| system | today | native replacement | content |
|---|---|---|---|
| Menus and screens | **mostly native**: the RmlUi Esc menu, the F3 Organize screen, the field overlay, alerts | the remaining game menus (shops, item lists, world-map menus) as RmlUi screens | screen layouts in `app/ui/` |
| Field command and AI | **partly native**: mouse orders, War Director, enemy commander, auto commander | native field rules (movement, pursuit, town capture) | Lua for AI and mission rules |
| Content data | **started**: the content database holds classes, attacks, items and consumables, read from the ROM and changed by JSON packs that reload while the game runs; the class rebalance is the first pack. Its bridge writes the packs' bytes into the game's tables every frame (`design-content-database.md`) | one content database (items, classes, units, shops, encounters, rewards, text). While the game's code still runs, the port writes the database into the tables that code reads; native systems read the database directly | LordlyCaliber projects, plus JSON for port-only data |
| Cutscenes and dialogue | the game's scene-`0x0D` engine and its 1,693-entry step table | a native scene player: a timeline of actors, movement, camera, portraits, text, music and choices, drawn with the game's sprites and backgrounds | timeline files plus Lua; the original 200+ scenes imported once by a step-table decoder |
| Mission field | the game's scene `0x03`: its map, movement and fog | a native field with maps as data: terrain, roads, towns, size, camera. Larger maps than the original | map files; the original maps imported |
| World map and campaign | the game's world map and mission order | a native campaign layer: missions, unlock order, story flags | campaign files plus Lua |
| Battles | the game's battle code, fed by the content database | a native battle last, once the other systems no longer depend on the original | class and attack data from the database |
| Saves | the cartridge save, plus the port's backups and profiles | a port save format that holds native-system state; original saves imported | — |
| Audio | the game's sound driver; song replacement from WAV | a native music and effects player | audio files |

## 4. Order

Each phase leaves the game playable, and each native system starts with the
original content imported into it. The phases keep their numbers; the build
order is R0, R2, R1, R3, R4, R5 (the maintainer, 2026-10-31: new classes come
before the cutscene player).

| phase | content | done when |
|---|---|---|
| R0 | **Foundation.** The content database; LordlyCaliber import tier 1 (records, text, rewards); the Lua runtime with the existing events; the MODS tab with load order and conflicts | a LordlyCaliber project's edited sword and a Lua script that changes a drop both work in the port |
| R1 | **Cutscene player.** Decode the step-table format; import the original scenes; the native player plays them; new scenes authored as timeline files | one original cutscene plays in the native player, and one new scene plays from a file |
| R2 | **New classes and units.** Classes, units and their art from the database and LordlyCaliber projects, fed to the game's original code through relocated tables (section 4.1) | a new class is recruited, fights and saves |
| R3 | **Native mission field.** Map format research; the native field with the original maps imported; larger maps | an original mission plays on the native field, and one larger new map plays |
| R4 | **Campaign layer.** Missions and story order as data; new missions | a new mission is reachable from the world map |
| R5 | **Native battle.** Replace the game's battle code | a battle runs without the recompiled battle module |

R0 progress (2026-10-31): built are the content database, JSON packs that
reload while the game runs, LordlyCaliber import of records and names, names,
the battle reward table, runtime packs (`design-content-database.md`), and the
Lua runtime (`design-lua.md`). The class rebalance is the first pack,
`app/content/class-rebalance.json`. Both "done when" cases work offline: a
LordlyCaliber project's edited sword applies, and a Lua script changes a drop
(`app/content/examples/drops.lua`); neither has been played yet. The MODS tab
lists the packs, scripts, refused files and conflicts, in the game's Esc menu
too. Mod safety is documented and tested on every build (`security.md`). Open
in R0: LordlyCaliber descriptions and its reward section (which needs its own
reward code). Mod folders with `mod.json`, priorities and requirements are built
(`design-mods.md`).

### 4.1 New classes before the native systems

Decided by the maintainer on 2026-10-31: new classes come in R2, on the
original engine, and do not wait for the native field, army and battle
(R3-R5).

A class id is one byte in each character record (`+0x11`) and in the save,
and the Organize screen, the field, the world map menus, the shops and the
battle all read the class table at `0x80187C34`. That table holds 165 records
and ends where the attack table starts, so it cannot grow in place. A native
battle alone would not lift the limit, because the units it fights with still
come from the original army records, menus and field.

R2 therefore relocates the tables: the content database places a larger class
table (up to id 255, the limit of the one-byte id) in guest RAM the game does
not use, and the port points the original readers at it. The items get the
same treatment (modding plan M4). This is a change to where the original code
looks for its data, and adds no new N64 code. LordlyCaliber does the same to
add classes up to id 255, so the method is known to work; its reader lists
are the starting point (modding plan P2, design-ecosystem.md section 6), each
checked on revision 1 at instruction level.

The relocation is a bridge. Each native system (R3-R5) reads the database
directly, and once no original system reads a table, its relocation is
removed.

R2 progress: the reader inventory is done (`design-new-classes.md`): 299
sites in 113 functions read the class table, each through its own `lui`, so
the table can move by changing those immediates.

R2 status (2026-11-01): the class and appearance tables move by default
(`OGRE_CLASS_TABLE_MOVE=0` keeps the ROM's). A pack makes a new class with a
key and `copy_of` (ids 172..254, kept in `saves/class-ids.json`); the class
animation resource grows for it; a `"change_class"` block offers it in Change
Class, in the modern view and in the classic menu. Played: the Dragon Knight
is made through the modern view's Change Class, fights with normal attacks,
and draws as a Centurion. `game.recruit` builds a new army character of any
class through the game's own builder. R2's "done when" was met in play the
same day: a recruited Dragon Knight joined the army, fought, and was still
there after a save and a reload. The game's own Organize screen changes a
Fighter into a Dragon Knight and back (played). Left in R2: recruiting on the
world map, and new art.

Work already planned in other documents fits into these phases:
`design-war-director-v2.md` and `design-auto-commander.md` (field AI, R0 and
R3), `design-content-expansion.md` (R0 and R2), `design-modding-platform.md`
(R0), `design-save-system.md` (saves, alongside R0), and
`design-field-controls.md` (the field interface, ahead of R3).

After the first release, `design-another-world-pack.md` separates the engine
from Another World's gameplay: Another World becomes a shipped mod pack that a
player can turn off to play the original game on the engine. Its `builtin`
list names the C++ rule sets that are not data or Lua yet, and each phase above
shortens it.

## 5. Open questions

The maintainer decides these, and contributors' views are welcome.

1. Must the remake keep loading saves from the original cartridge, or only
   import them once into the port's save format?
2. Should the original campaign stay playable unchanged as an option (a
   "classic" content set), or only as the base that the remake changes?
3. Which comes first after R0: the cutscene player (R1) or new classes (R2)?
   Decided 2026-10-31: new classes (R2) first, on the original engine through
   relocated tables (section 4.1), then the cutscene player (R1).
4. Art for new classes and maps: who draws it, and at what resolution?

