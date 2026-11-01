# Plan: Another World as a mod pack over the engine

Status: plan only, 2026-11-01. Nothing here is built, and it is not part of the
first release. The maintainer asked for it so the line between the engine and
Another World's gameplay is drawn now, while features are still being added.

## 1. Goal

The port has two layers that are mixed today:

- **The engine.** The recompiled game and everything that makes it run well on
  a computer and lets people mod it: rendering, input, menus, saves, the
  content database, the Lua runtime, mod loading and the MODS tab.
- **Another World.** This project's changes to how the game plays: the class
  rebalance, the War Director, new class rules, and the other rule changes in
  `FEATURES.md`.

The goal is that Another World is a mod pack like any other: a row in the MODS
tab, on by default, that a player can turn off to play the original game on
the modern engine, and that a modder can read as an example of a large pack.
A second total-conversion pack would then sit beside it instead of on top of
it.

## 2. Which side each feature is on (proposal)

Rule: a feature is **engine** when it changes how the game is presented,
controlled, saved or modded, and the original game's outcomes are the same
with it. It is **Another World** when it changes a rule, a number, an enemy or
a reward, so a battle or a mission can end differently.

| feature (`FEATURES.md`) | side | note |
|---|---|---|
| Class rebalance (`content/class-rebalance.json`, `class_rebalance.cpp`) | Another World | data already in a pack; the C++ part (size-2 answers to Organize) moves with it |
| Large units: ONE SQUARE, class alignment IGNORED | Another World | rule changes |
| Centurion bonus, Faerie heals (`battle_ai.cpp`) | Another World | battle rules |
| Shared EXP (`exp_share.cpp`) | Another World | |
| War Director, enemy commander, DIFFICULTY, loot upgrades, training catch-up, enemy weapons, formations, ranks, extra members, boss escorts | Another World | the whole enemy-army system |
| New classes from Another World (none ship yet; the Dragon Knight is an example mod) | Another World | |
| Battle speed, field speed, frame rate, game speed | engine | pace only |
| Auto-play dialogue, turbo A, skip intro, pause when unfocused, keep pointer in window | engine | |
| Field overlay, mouse orders, alerts, unit reports LOG | engine | presentation and control; the game's rules are unchanged |
| Pause on select | engine | maintainer, 2026-11-01 |
| Auto commander | engine | a player's tool that plays the field for them (maintainer, 2026-11-01) |
| Organize screen, ROSTER tab, Esc menu, button legend, prompts, mouse in menus, accessibility | engine | |
| Pixel filter, HD textures, text font, music and effects sliders, song replacement | engine | |
| Save backups, import, profiles, screenshots, `.ips` patches, revision 0 and 1 | engine | |
| Content database, new class support, Lua, mod folders, MODS tab, mod safety, hook table and events | engine | the modding platform itself |

## 3. How it would work

### 3.1 A shipped pack

`content/another-world/` becomes a mod folder shipped with the game:

```
content/another-world/
  mod.json            id "another-world", name "Another World", enabled true
  class-rebalance.json
  ...                 later: Lua scripts and data for the rules in section 2
```

The MODS tab lists shipped packs above the player's items, with a switch and
the order arrows inside the shipped group, and no DELETE, because an update
puts the files back. Today the tab hides `content/` (`design-mods.md` section
5); this step adds the shipped group back for packs that have a `mod.json`.

### 3.2 Built-in rules that are C++

Most Another World rules are C++ in `app/features/` and hook the game
directly (`config/hooks.toml`). They cannot become data or Lua until the
script API reaches that far, so the pack switches them instead:

- `mod.json` gains `"builtin": ["war-director", "class-rebalance-rules",
  "battle-rules", "exp-share", ...]`: names of C++ rule sets the pack turns
  on. Only a shipped pack may name them; a player's pack that names one is
  refused, so a mod cannot turn on code the player did not install.
- `game_host.hpp` gains `bool builtin_active(std::string_view name)`. Each
  rule set checks it where it starts: an event subscriber returns early, a
  hook falls through to the original code, a runtime pack is not published.
  The War Director already runs off a menu option, so the check joins that
  option rather than adding a second switch.
- A rule set's menu options (`options.cpp`) gain an owner. The GAMEPLAY rows
  of an inactive rule set are hidden, so the menu never offers a setting that
  does nothing.

Over time each rule set moves into the pack as data or Lua (roadmap phases
R1-R5 replace the original systems), and its `builtin` name is dropped. The
list in `mod.json` shrinks to nothing as the engine gains APIs; that list is
the measure of how much of Another World is still hard-wired.

### 3.3 When the switch applies

Turning Another World on or off applies at the next start, like a mod that
adds new classes (`design-mods.md` section 5.1). Its rules change saved state:
the War Director's spawned units and credits, upgraded loot, promoted enemy
ranks, extra members, and any new classes. A switch in the middle of a
campaign would leave a save holding things the other ruleset does not expect.

### 3.4 Saves

A save made by the port's menu (save profiles) records the packs that were on,
by id and version. Loading a save with a different set shows which packs
differ and asks before it loads. The cartridge's own three saves cannot hold
this, so for them the port keeps the last pack set used beside the battery
file and warns the same way.

### 3.5 Vanilla mode

With Another World off and no other packs, the port plays the original game:
the original class records, no War Director, the original EXP and the
original enemies, with the engine's comfort features still on. A first start
could offer the choice ("Another World" or "Original game"), which is just the
pack's switch.

## 4. Steps, after the first release

1. **Inventory.** Confirm section 2 with the maintainer, and list for each
   Another World rule set its files, hooks, events, options and saved state.
2. **Shipped pack folder.** Move `class-rebalance.json` into
   `content/another-world/` with a `mod.json`; show shipped packs in the MODS
   tab with switch and order and no delete; switching applies at the next
   start.
3. **`builtin` and `builtin_active`.** Add the `mod.json` key, its shipped-only
   rule, a `content_pack_test` case for a player's pack that names a builtin,
   and a `security.md` entry. Gate one rule set first (exp share is the
   smallest), then the rest.
4. **Option owners.** Give `options.cpp` entries an owner rule set and hide
   inactive ones.
5. **Save stamps.** Record the pack set in profile saves and beside the
   battery file; warn on a mismatch.
6. **Move rules to data.** As each roadmap phase lands, move that rule set out
   of `builtin` into the pack's data and scripts.

## 5. Answers from the maintainer (2026-11-01)

1. The auto commander is engine: a player's tool that plays the field for
   them.
2. Pause on select is engine.
3. Another World is probably one pack. To be discussed further.
4. The switch lives in the MODS tab; there is no first-start question. Some
   pure gameplay changes that already have hooks may be brought out of the
   engine first.
5. Yes. Any pack may require another (`"requires"`), so a player's own mod can
   name the mod it builds on, Another World included. This already works for
   mod folders (`design-mods.md` section 3).

None of this is built before the first release.
