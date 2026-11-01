# Design: how the project is used — the port, LordlyCaliber and mods

Status: plan, 2026-10-29. Developer decision of the same day: **the port runs
both US revision 0 and US revision 1**, and it does not redo work that
LordlyCaliber has done.

Revision note (2026-10-31, `ROADMAP.md`): the project is a modern remake that
replaces the original systems with native ones. The port's own data files are
the main way to change the game, and LordlyCaliber projects are imported into
the port's content database so their content plays here (developer,
2026-10-31). The port does not depend on LordlyCaliber as its editor.

Related plans: `design-lordlycaliber-import.md` (one content format, the
importer, and the field mapping; 2026-11-01), `ROADMAP.md` (the order), `design-modding-platform.md` (the API),
`design-content-expansion.md` (new content), `design-war-director-v2.md`
(gameplay).

## 1. What the project is

| project | what it is | output |
|---|---|---|
| **This port** | a modern remake of OB64 (Windows, Linux, macOS, Android, browser) that starts from a static recompilation and replaces its systems with native ones | the game running with modern graphics, controls, UI, native systems and new content |
| **[LordlyCaliber](https://github.com/rempred/LordlyCaliber)** | an editor for OB64 data, art and scripts, built on a partial decompilation | edited projects (`project.json`) and patched N64 ROMs |

**The port runs the game and reads content from its own data files and from
LordlyCaliber projects.** A modder edits the port's data files directly, or
authors content in LordlyCaliber and imports the project; the port runs both
with everything it adds. The port does not grow its own data editor.

## 2. Who uses it, and how

### 2.1 Player

1. Starts the port and gives it a ROM: **US revision 0 or 1**. The port
   identifies it by SHA-256 and says which one it found.
2. Plays with the port's features: resolution and HD packs, modern controls,
   the F3 Organize screen, the Esc menu, the field controls, the War
   Director, training catch-up.
3. Installs mods by dropping a folder or archive into `mods/`. The MODS tab
   lists them with load order, settings and any conflict. A mod made from a
   LordlyCaliber project appears there like any other.

### 2.2 Content modder

1. Opens LordlyCaliber with the revision 0 ROM and edits items, classes,
   shops, rewards, encounters, text or art.
2. Saves the project folder (`project.json` plus assets).
3. Puts the project folder (or a `mod.json` that points to it) in the port's
   `mods/`.
4. Starts the port. The port reads the supported sections and applies them at
   runtime (section 5), on either revision.
5. Optional: a "test in port" launch that starts the port on a chosen mission
   with the project loaded, as LordlyCaliber's Testing tab does today with
   Project64 (needs the author's agreement, section 8).

### 2.3 Script modder

Writes `main.lua` against the port's events and game-state views
(`design-modding-platform.md` 4.2): change drops, react to a town liberated,
build director units, add alerts. No rebuild, no MIPS toolchain.

### 2.4 Port developer

- Reads LordlyCaliber's decompiled C for a function by the port's own name
  (`tools/lc_lookup.py`, section 6).
- Adds a hook point as one entry in `config/hooks.toml`, valid for both
  revisions.
- Builds port features on the same events modders use.

## 3. Who owns what

| area | LordlyCaliber | this port |
|---|---|---|
| Editing data (items, classes, shops, rewards, encounters, consumables) | **owns** | reads its project format; no editor |
| Art (sprites, animations, portraits, icons) | **owns** | loads the results; HD texture packs stay here |
| Cutscenes and dialogue authoring | **owns** | runs them; text import |
| Save editing | **owns** | — |
| Decompilation | **owns** (private decomp, 1,102 functions bundled) | reads it as reference |
| Patched N64 ROMs for emulators | **owns** | — |
| Running the game on modern platforms | — | **owns** |
| Graphics, input, UI overlays, platforms | — | **owns** |
| Applying content at runtime without a patched ROM | — | **owns** |
| New gameplay systems (director, field controls, training) | — | **owns** |
| Scripting API and mod manager | — | **owns** |
| Game knowledge (addresses, layouts, routines) | both: the port uses theirs and sends back revision 1 findings | |

### 3.1 Work the port will not do

- An item, class, shop, encounter, consumable or text editor. Quick changes
  are edits to the port's data files.
- A sprite, animation or portrait editor.
- A cutscene editor for the game's own format. The native cutscene player's
  timeline format and the native map format (`ROADMAP.md` R1, R3) have no
  LordlyCaliber equivalent; an editor for those is the one exception, decided
  when those systems exist.
- A save editor.
- Its own decompilation.
- Re-finding a table layout or a reader list that LordlyCaliber already
  generates; the port runs their method on its own revision instead.

## 4. Two revisions

### 4.1 What differs

Revision 0 (SHA-256 `571e8339…`, LordlyCaliber's baseline) and revision 1
(`3bfbaf0a…`, the port's current build) were measured in phase A
(`docs/anotherworld/notes-revisions.md`): of 4,991 functions, 4,988 are the same code at
shifted offsets (ROM blocks `0`, `-0x20`, `+0x30`), and 3 changed (the enemy
character quick save/load and the mission loader). Every RAM address above
`0x8016D220` is 0x20 lower in revision 0, including the item, character and
mission tables.

### 4.2 How the port runs both

- **Per-revision build configs**: `config/rev0/` and `config/rev1/` (splat
  yaml, bank unit configs, symbol and reloc files). The shared parts stay in
  `config/`.
- **Two recompiled code sets**, generated by `make recomp REV=0|1`. They are
  linked into one executable under per-revision name prefixes if the runtime
  allows it, or built as two executables behind the launcher (research E1).
- **Revision detection** at ROM load by SHA-256, as today; the runtime
  registers that revision's functions, overlays and bank units.
- **Addresses in the app** (`0x8008B024`, `0x801F1055`, ...) come from a
  per-revision table generated by the function map (phase A). App code names
  addresses (`addr::frame_hook`), not numbers.
- **Hook points** in `config/hooks.toml` carry one anchor per revision.
- **Saves**: whether a revision 0 save loads on revision 1 and back is checked
  (research E3); the port keeps saves per revision until then.

## 5. LordlyCaliber content on the port

The port reads a project's data and applies it at runtime. LordlyCaliber's
own ROM-side code is not run; what that code does is reimplemented in the port
where needed.

| LordlyCaliber area | kind of change | how the port applies it | tier |
|---|---|---|---|
| Item, class, consumable records (existing ids) | data in existing tables | write the records into RAM after load | 1 |
| Text: names, descriptions | data, resized pools | relocated pools, readers redirected | 1 |
| Shops (stock, facilities) | data, new shops | shop tables; new shops through relocation | 2 |
| Class rewards (drop table) | data | the drop table (table O) | 1 |
| Encounter pools and rates | data plus new code | tables; the dispatcher reimplemented as a port hook | 2 |
| New item ids (to 510) | expanded tables | relocation (content plan) | 2 |
| New class ids (to 255) | expanded tables, art | relocation plus art loading | 3 |
| Promotion requirements and families | data plus rebuilt routine | port hook on the class-change check | 3 |
| Persuasion, high attack, weapon status, Chaos Frame counter | new code | port-native reimplementation, one feature at a time | 3 |
| Capacity (100 enemy characters, 50 map objects) | relocated runtime arrays | relocation of the arrays and their readers | 3 |
| Scenarios, sites, cutscenes | data plus scripts | scenario and event loaders | 4 |
| Combat animations, sprites, portraits | art resources | resource replacement at load | 4 |

Tier 1 needs only writes into existing tables; tier 4 needs whole loaders.
A project section the port does not support yet is listed in the MODS tab as
"not applied", never silently dropped (LordlyCaliber's own rule).

## 6. The knowledge bridge

- **Function map**: every function of both revisions hashed with relocations
  masked; matched across revisions and against LordlyCaliber's owner list
  (named by revision 0 ROM offset).
- **`tools/lc_lookup.py <name>`**: given a port symbol (`func_ovlN_801AD6BC`)
  or address, prints the revision 0 offset, LordlyCaliber's name, the path of
  the decompiled C in a local clone, and its interest notes. The clone stays
  outside this repository.
- **`docs/symbols.md`** gains names from LordlyCaliber's catalog with that as
  the evidence and a confidence level, checked at instruction level as
  AGENTS.md rule 2 requires.
- **Generators**: LordlyCaliber's reader-list generators (items, classes,
  capacity, shops, rewards) ported to run on both revisions' units.

## 7. Phases

| phase | content | done when |
|---|---|---|
| A | Function map rev0↔rev1↔LordlyCaliber; `tools/lc_lookup.py`; difference report — **done 2026-10-29** (`tools/revmap.py`, `docs/anotherworld/notes-revisions.md`: 1,070 of 1,109 decompiled functions named; the 39 others are runtime-replaced libultra) | every LordlyCaliber owner has a revision 1 match or a "changed" note |
| B | Revision 0 recompiled — **done** (`make rev0`, `docs/anotherworld/notes-revisions.md` §5). The rest (app address table, second executable, launcher choice) is merged into C and waits until it is needed | revision 0 boots to the title and plays the opening; revision 1 unchanged |
| C | `config/hooks.toml` — **done** (28 hooks, both revisions, `tools/apply_hooks.py`). Event bus — **started**: `app/features/game_events.hpp` with events 1 mission load, 2 field update, 3 item granted; the War Director's garrison and deployment subscribe to it. Next: move the other hooks onto events, the app's address function, then the revision 0 executable | the director, training and Organize work on both revisions |
| D | LordlyCaliber import, tier 1 (records, text, rewards); MODS tab — **records done 2026-10-31** (`design-content-database.md` section 8; checked offline) | a project's edited sword stats and a reward change show in the port |
| E | Tier 2: new items, shops, encounters (relocation) | a project's new item is bought and saved in the port |
| F | Lua scripts | the item randomizer as a short script |
| G | Tier 3 and 4, one feature at a time | each feature listed in section 5 as it lands |
| H | "Test in port" from LordlyCaliber, if the author agrees | a project is tested in the port in one click |

Phase 8 modernization (field controls, War Director v2, graphics) continues
alongside and moves onto the event bus when phase C lands.

## 8. Working with LordlyCaliber's author

The integration is stronger with the author's agreement:

- a stable, versioned project format, or a documented export of the data
  sections;
- revision 1 findings sent back (the port has them);
- a "test in port" target beside Project64;
- clarity on using the decompiled C as a reference.

The project is MIT licensed, so reading and adapting its tools is allowed
without contact; contact is for coordination, not permission.

## 9. Research

| id | question |
|---|---|
| E1 | Can N64Recomp output for two ROMs share one executable (symbol prefixes, two overlay registries)? |
| E2 | The revision 0 to revision 1 difference: moved functions, changed functions, changed data |
| E3 | Save compatibility between the revisions |
| E4 | LordlyCaliber `project.json`: the data sections, their versions, and how assets are referenced. Record sections and version answered (`design-content-database.md` section 8.1); assets open |

## 10. Decisions for the developer

1. One executable for both revisions, or two behind the launcher (after E1)?
2. Contact rempred now, or after phase A shows the mapping works?
3. Which LordlyCaliber sections to import first (tier 1 proposal: items,
   classes, consumables, rewards, text).

