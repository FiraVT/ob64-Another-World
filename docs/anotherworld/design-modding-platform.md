# Design: the modding platform — content, scripts and native systems

Status: revision 2, 2026-10-31, rewritten for the remake direction
(`ROADMAP.md`, `DECISIONS.md` 2026-10-31). Revision 1 (2026-10-29) planned
modding inside the N64 engine: relocated tables, redirected readers and mods
written against the game's formats. Built: the hook table and the event table
(section 2), the content database (phase M1), and JSON content packs that
reload while the game runs, with the class rebalance as the first pack
(`design-content-database.md`, 2026-10-31).

Companions: `ROADMAP.md` (the order), `design-ecosystem.md` (LordlyCaliber),
`design-content-expansion.md` (the first new content).

## 1. Goal

**The project builds its own content with the same formats and API that
modders use. A modder adds items, classes, units, cutscenes, missions and maps
with data files and Lua scripts, or with LordlyCaliber, without rebuilding the
port and without writing MIPS code.**

## 2. Modding today

| layer | what exists | limit |
|---|---|---|
| Code mods (`.nrm`, N64Recomp) | loader and MODS tab (lfarroco); examples `item-grant-log`, `small-large-units`, `smart-enemy-formations` (lfarroco's `skip-boot-logos`, `exp-overflow` and `item-randomizer` were removed 2026-11-01) | C for the N64 built with a MIPS toolchain and symbol files that need a ROM build; no calls into game functions; most game logic is streamed bank code, so mods poll RAM |
| Asset packs | `mods/backgrounds/` (PNG plus a text table), `texture-packs/`, `music/<n>.wav`, `.ips` ROM patches | art, audio and ROM bytes only |
| Hook table | `config/hooks.toml`, 50 hooks, applied by `tools/apply_hooks.py` | port builds only |
| Events | `app/features/game_events.hpp`: mission load, field update, item granted, field frame | C++ subscribers only |
| API | none | no named game fields, no content database, no load order or conflict report |

## 3. The layers

A modder uses the simplest layer that does the job. The project's own content
uses the same layers.

### 3.1 Content: LordlyCaliber projects and JSON

- **The content database.** The port keeps one database of game content: items,
  classes, units, shops, encounters, rewards and text, later cutscenes, maps
  and missions. At start it loads the original content from the ROM, then each
  enabled mod in load order.
- **The port's own JSON files are the main format.** Every kind of content
  in the database can be written by hand as JSON, edited in place and
  reloaded while the game runs, with no separate tool and no ROM build.
- **LordlyCaliber projects are imported.** A mod folder holds a
  LordlyCaliber project (`project.json` plus assets), or a `mod.json` that
  points to one. The port reads the supported sections directly
  (`design-ecosystem.md` section 5).
- **Port-only data** has no LordlyCaliber equivalent (War Director tables,
  larger maps, timeline cutscenes) and exists only as JSON, like the mod
  manifest (`mod.json`: id, version, load order, dependencies).
- **One format family.** The manifest and the port's data files are JSON. The
  backgrounds pack's text table and the `.nrm` mods' `mod.toml` stay as they are
  for compatibility; new formats are JSON.
- **Merging.** Mods apply in load order. Two mods that change the same record
  are listed in the MODS tab, and the later one wins.
- **Feeding the original code.** While a system is still the recompiled
  original, the port writes the database's records into the tables that code
  reads (`content::tick`, `design-content-database.md` section 5). Larger tables, such
  as new item ids past 277, are placed in guest RAM the game does not use, and
  the port points the original readers at them. This is a bridge: a native
  system reads the database directly and needs neither.

### 3.2 Behaviour: Lua

- **Lua 5.4** embedded in the port (MIT, small, sandboxed). A mod's
  `main.lua` registers handlers. Scripts reload without restarting the game.
- **Lua holds rules, not content.** Content lives in the database (3.1).
  A script reacts to events and changes state; it does not define items or
  classes.
- **Events**, each fired on the game thread. Built: `mission_load`,
  `field_update`, `item_granted`, `field_frame`. Planned:

| event | a handler can |
|---|---|
| `unit_built(unit)` | change members, levels, items |
| `battle_start(a, b)` / `battle_end(result)` | read and adjust the result |
| `drop_roll(unit, item)` | replace the item |
| `town_liberated(town, unit)` | add effects |
| `scene_cue(scene, cue)` | drive a native cutscene (R1) |
| `menu_open(screen)` | add a panel |
| `frame()` | anything else, rate limited |

- **Named game state**, not addresses: `game.items[id]`, `game.classes[id]`,
  `game.characters[i]`, `game.units[slot]`, `game.towns[i]`, `game.goth`,
  `game.mission`. Each field is defined once in the port with its address and
  evidence (`app/features/game_addresses.hpp`). When a native system takes
  over, the same name reads the native state, so scripts survive the change.
- **Actions**: `grant_item`, `spawn_unit(template, where)`, `alert(text)`,
  `play_scene(id)`, `set_flag`.
- **Settings**: a mod declares options; the MODS tab shows them.

### 3.3 Native systems

The cutscene player, the native field and the campaign layer (`ROADMAP.md`
R1-R4) are built on 3.1 and 3.2: each reads its content from the database and
exposes events and actions to Lua. The project's own War Director, enemy
commander and auto commander move to the same events and named state, so they
serve as worked examples for modders.

### 3.4 Code mods (`.nrm`): kept, frozen

Code mods stay loadable for the existing examples and for experts. The project
adds no new `.nrm` examples and does not extend the toolchain, because the
native systems replace the code they would patch. The `small-large-units` and
`item-grant-log` examples keep shipping.

## 4. Tools for modders

- **A modder guide** with one example per layer: a LordlyCaliber item edit, a
  JSON data change, a Lua script, a timeline cutscene.
- **A mod package**: a zip with `mod.json` at its root, dropped into `mods/`.
- **A conflict and error report** in the MODS tab, and a log file per mod.
- **Hot reload** for Lua and data files while the game runs.
- **Settings in the menu.** Every feature switch that is only an environment
  variable today (`OGRE_CENTURION_COMMAND`, `OGRE_FAERIE_HEAL`, ...) gets an
  option row, so players and modders do not need environment variables.

## 5. Research

| id | question | method |
|---|---|---|
| P1 | rev0-to-rev1 function map | done: `tools/revmap.py`, `notes-revisions.md` |
| P2 | Readers of the item, name, description and icon tables on revision 1 (the bridge in 3.1) | port LordlyCaliber's relocation generator; compare its 62 references in 48 functions |
| P3 | The bound checks on the 278 item count | `slti/sltiu ..., 0x116` sites in the main ELF and the bank units |
| P4 | LordlyCaliber `project.json` data sections | its `native/engine/project.cpp` and serializers. **Record sections answered 2026-10-31** (`design-content-database.md` section 8.1) |
| P6 | Free guest RAM for the bridge tables | content plan C1 |

## 6. Phases

These are `ROADMAP.md` phase R0, split.

| phase | content | done when |
|---|---|---|
| M1 | Content database with the original content; named game state in C++ | the class rebalance reads and writes through the database. **Built 2026-10-31** (`design-content-database.md`) |
| M2 | LordlyCaliber import tier 1 (records, text, rewards); `mod.json`; MODS tab load order and conflicts | a project's edited sword stats and a reward change show in the port. **Built 2026-10-31**: records and names (`design-content-database.md` section 8) and the MODS tab's list of packs, scripts, refused files and conflicts (section 7.3), and mod folders with `mod.json` (`design-mods.md`). Open: descriptions, LordlyCaliber rewards |
| M3 | Lua runtime, the built events, named state, actions, settings, hot reload | an item randomizer as a short script with no RAM polling (lfarroco's code mod of that name was removed 2026-11-01). **Runtime, events, content API and hot reload built 2026-10-31** (`design-lua.md`); named state, actions and settings are open |
| M4 | New item and class ids through the bridge (P2, P3, P6): relocated tables in free guest RAM, original readers repointed (`ROADMAP.md` section 4.1) | a project's new item is bought, equipped and saved; a new class is recruited (R2) |
| M5 | Modder guide and mod packages | an outside modder makes a mod from the guide alone |

## 7. Decisions

Decided by the maintainer, 2026-10-31: the port's own data files are the
main way to make changes, and LordlyCaliber projects are imported so their
content plays in the port. Proposed in this revision and not yet confirmed:
Lua covers behaviour, and code mods are frozen. Open:

1. Confirm the proposed split above, or name another script language.
   (Decided 2026-10-31: Lua 5.4, fetched from lua.org at build time;
   `design-lua.md`.)
2. May the port's tools read LordlyCaliber's decompiled C from a local clone at
   build time, or only as a manual reference?
3. When to contact rempred about the stability of the `project.json` format.

