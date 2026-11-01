# Design: content expansion — new items and beyond

Status: plan, 2026-10-29; revised 2026-10-31 for the remake direction
(`ROADMAP.md`). Nothing is built. Companion to `design-war-director-v2.md`,
whose rewards (section 6) need new items.

Revision note (2026-10-31): content lives in the port's content database and
comes from LordlyCaliber projects and JSON (`design-modding-platform.md` 3.1).
The relocation in section 3 is kept as the bridge that lets the original code
read new content while that code still runs; native systems read the database
directly. New classes, cutscenes and missions are no longer out of scope: they
are `ROADMAP.md` phases R1-R4.

## 1. Goal

**Add new content to the game (first new equipment, then consumables,
classes, encounters and events) as data packs the port loads at runtime, so
the War Director has rewards worth chasing and a replay finds things the
first playthrough did not.**

Constraints:

- The original content must still load with no pack enabled. Whether saves
  from the original cartridge keep loading is an open question
  (`ROADMAP.md` section 5).
- Story order (the debut gate) applies to content too: an item tied to a
  faction or a story event does not drop before that point.
- A pack is data, not a patched ROM (section 3).

## 2. Prior work: LordlyCaliber

[rempred/LordlyCaliber](https://github.com/rempred/LordlyCaliber) (MIT) is a
Windows editor for OB64 (US, revision 0) that writes a patched ROM. Its README
and `native/rebuild/*.cpp` show what is possible on this game:

| area | LordlyCaliber |
|---|---|
| Equipment | adds new ids; the item id is 9 bits, so ids up to 510 (511 reserved); records, names, descriptions, icons and weapon appearance |
| Consumables | edits prices and metadata with ten effect models; no new ids |
| Classes | adds classes up to id 255 (new ones start at 1 HP and 1 in each stat); promotion requirements, families |
| Shops | adds shops up to id 255, 50 equipment and 30 consumables each |
| Encounters | 45 neutral encounter pools, terrain rates, custom neutral squads |
| Class rewards | three weighted outcomes per class (the drop table this port calls table O) |
| Cutscenes | dialogue, choices, camera and movement actions |
| Art | combat animations, sprites, army sprites |
| Capacity | enemy characters from 52 to 100, map objects to 50 (`native/rebuild/capacity_runtime.cpp`) |

How it does it: it compiles adapted C sources of the game's routines with an
N64 toolchain, relocates tables and their readers into free RAM below
`0x80400000` (the game's 4 MB layout), redirects the original routines with
jumps, extends the ROM past its end, and converts the save layout.

What this port takes from it:

- **Proof of scope**: new items, classes and shops work in this engine when
  every reader is moved together.
- **Reference data**: its reader lists, record layouts and the 9-bit item id
  limit. Its findings are cited with credit; its code is not copied without
  checking the license terms in each file.

What this port cannot take: **a LordlyCaliber-exported ROM**. This port runs
code statically recompiled from the vanilla ROM. Data edits in a patched ROM
would be read, but its new and redirected MIPS code would not exist in the
recompiled program. LordlyCaliber also targets US revision 0, and this port
recompiles US revision 1. The full comparison, the reader-list generator (62
item-table references in 48 functions on revision 0) and its decompiled C are
in `design-modding-platform.md` section 3.

## 3. How this port adds content

The port controls the runtime, so it does the same relocation in host code:

1. **Content pack**: a mod folder in `mods/` with `mod.json`, and either a
   LordlyCaliber project or JSON data files, icons as PNG and text
   (`design-modding-platform.md` 3.1). Loaded at boot into the content
   database; enabled in the MODS tab.
2. **Relocated tables in free RAM**: the port allocates the expanded tables
   in guest RAM the game does not use (research C1: `0x80400000..0x80800000`
   if the game never uses the Expansion Pak range), copies the vanilla records
   in, and appends the pack's records.
3. **Readers redirected at build time**: every read of a vanilla table base
   and every bound check against its count is rewritten in the generated code
   by a tool in the style of `tools/director_hook.py` (`tools/content_hook.py`),
   so the game's own code reads the expanded table. `make recomp` re-applies it.
4. **Saves**: the vanilla save keeps vanilla ids only. Pack items the player
   owns are stored in a port sidecar per save slot and merged into the owned
   list on load (research C4). With the pack removed, the save loads as
   vanilla minus the pack items.
5. **Icons and names**: names and descriptions are written into the relocated
   pools. Icons are served for ids above 277 by the port, in the same 16x16
   8-bit format as the icon sheet (asset `0x01DEE4B0`, `0x100 + id*0x100`), or
   as HD images through the texture pack path.

## 4. New equipment

### 4.1 What an item can do without new code

An equipment record (0x20 bytes at `0x8018C42C + id*0x20`) carries the
category, element, the equip mask, price, stat bonuses and resistances (layout
research C2). New items can therefore combine stats, elements and
resistances in ways the vanilla list does not.

Behaviour the game hardcodes to an item id (LordlyCaliber notes this too)
stays with that id. New special effects need port hooks (4.3).

### 4.2 Item families

| family | source | idea |
|---|---|---|
| **Lieutenant signatures** | beating a lieutenant (director 4.4) | one item per lieutenant archetype, themed on his class: a Knight lieutenant's shield, a Witch lieutenant's charm |
| **Faction gear** | units of a faction (Palatinus, Lodis, Zenobia, Gallian, undead) | a faction's look and element; drops only on missions with that faction (debut gate) |
| **Town gifts** | loyal towns (director 6.3) | modest gear with a local theme, one per region |
| **Relics** | opportunities and war conditions | a strong stat with a cost (+STR, -AGI), so it is a choice, not an upgrade |
| **Elemental lines** | drops by band | filling element gaps in each category and band |
| **Set pieces** | phase 2 (4.3) | three items that give a bonus together |

Each family has 6-20 items; a first pack targets about 40 new items.

### 4.3 New effects (later)

Effects that need code run in the port at the points where the battle reads
equipment (research C5): on-hit effects, set bonuses, aura effects for the
unit. Each effect is a named handler in C++, and a pack item names the handler.
This is a later phase; phase 1 items are data only.

## 5. Beyond items

Ordered by value to the War Director and by cost:

| content | value | cost | notes |
|---|---|---|---|
| New consumables | high: supply trains, town gifts | medium | the 40-slot list `0x80193AE0` and the 0xC-stride table `0x8018E6EC` (45 records) must grow; effect models from the game's own (LordlyCaliber: ten) |
| Neutral encounters | high: opportunities, variety | medium | the game's encounter pools (LordlyCaliber: 45 pools) can take director-made units |
| Shops and stock | medium: town favour | low | a loyal town's shop gets extra stock |
| New events and dialogue | high: lieutenants speak, deserters join | medium after R1 | timeline files for the native cutscene player (`ROADMAP.md` R1), not the game's step table |
| New classes | high | high | content database plus art, fed to the original code through a relocated class table up to id 255 (`ROADMAP.md` R2, section 4.1) |
| New missions and maps | very high | very high | the native field and campaign layer (`ROADMAP.md` R3, R4) |

## 6. Research needed

| id | question | method |
|---|---|---|
| C1 | Does the game ever use RAM `0x80400000..0x80800000`? | static: `osMemSize` reads and heap setup; a full playthrough with a watch on that range |
| C2 | The full 0x20-byte equipment record layout | static: the stat-sum and equip routines; compare against LordlyCaliber's item editor fields |
| C3 | Every reader of the item table, the name pool `0x8018B4D0`, the description resource, the icon sheet and the 278 bound (`slti/sltiu ..., 0x116`; 14 sites in the main ELF alone, plus the banks) | LordlyCaliber's generator ported to revision 1 (modding plan P2, P3) |
| C4 | How the owned list `0x80196B20` is saved and loaded | static: the save-field table at ROM `0x3864C` |
| C5 | Where battle reads equipment effects | static: the battle bank's stat and damage routines |
| C6 | Consumable effect dispatch | static: the item-use routine (field controls R11) |

## 7. Phases

| phase | content | done when |
|---|---|---|
| 0 | C1-C4; pack format; one test item | a pack item is bought, equipped, shown in Organize with its icon, and survives a save and load |
| 1 | First pack: about 40 items in the families of 4.2; director drops use them | new items drop from director units by band and family |
| 2 | Consumables (C6) and shop stock | supply trains and town gifts give new consumables |
| 3 | Neutral encounters and director opportunities using them | each mission has an optional encounter with pack rewards |
| 4 | Effect handlers and set pieces (4.3, C5) | a set gives its bonus in battle |
| 5 | Events and dialogue (text) | a lieutenant speaks when met |
| 6 | Classes, if art is available | one new class is playable |

## 8. Decisions for the developer

1. Decided 2026-10-31: the port's own data files are the main content
   format; LordlyCaliber projects are imported so their content plays in the
   port.
2. Item names, art and lore: who writes them, and in what tone (the game's
   own naming style)?
3. Should pack content be on by default, or opt-in?
4. Which family to build first.

