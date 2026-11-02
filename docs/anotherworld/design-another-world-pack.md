# Plan: Another World as a mod pack over the engine

Status, 2026-11-01: the shipped pack folder exists and holds the class
rebalance and four rule sets as data and Lua (section 6). The MODS tab lists
and switches it (step 2, section 12). The War Director's rule sets are still
C++ (section 9). Its rules have options in the MODS tab (section 13). Sections
1-5 are the plan as the maintainer set it; sections 6-13 record what was built
and how it was tested.

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

The maintainer moved this ahead of the first release on 2026-11-01: shared
EXP and the other rules that need no War Director state move first, as Lua on
new hook events instead of a `builtin` list (section 6), and each further move
is tested before it is made.

## 6. What is in it

`app/content/another-world/` (shipped as `content/another-world/`) is a mod
folder with `mod.json` id `another-world`, on by default, priority -100, so a
player's mod loads after it and wins a conflict. The MODS tab switches it, and
the switch applies at the next start (section 12). The core keeps the hook points and the pack holds the rules:
with the pack off, every hook point gives the game's own value.

| file | rule | was |
|---|---|---|
| `class-rebalance.json` | the class and attack record changes (`design-class-rebalance.md`) | `app/content/class-rebalance.json` |
| `battle-tactics.lua` | Faerie's Throw a Kiss becomes Healing when an ally on its side is below 70% HP; each ready Centurion (34, 35, 94) gives every Soldier on its side one more attack | `app/features/battle_ai.cpp` |
| `shared-exp.lua` | a battle's EXP is pooled across the player's unit and split with the game's weighting `max(1, average level - level + 8)^2` | `app/features/exp_share.cpp`, GAMEPLAY > SHARED EXP |
| `class-rules.lua` | class changes do not check alignment; a large class takes one place in a unit | `class_rebalance.cpp`, GAMEPLAY > CLASS ALIGNMENT and LARGE UNITS |

The three GAMEPLAY rows are gone. Their `settings.cfg` keys (`shared_exp`,
`class_alignment`, `large_units`) are ignored when read.

## 7. The hook points

The core calls these Lua events (`app/features/lua_runtime.hpp`). A filter
event's handler gets the arguments and then the game's value, and may return
another value; a value outside the range is ignored.

| event | called from | range |
|---|---|---|
| `battle_start(unit)`, `battle_end(unit)` | `lua::tick`, when the battle bank (ROM `0x22A250`) becomes resident in scene `0x0D` and when it leaves; `unit` is the player's unit (0..29) or -1. The runtime logs `[battle] start` and `[battle] end` | none |
| `battle_attack(fighter, attack)` | `ogre_battle_pick_attack` (`0x801C89E4`), after a battle form rule | 0..158 |
| `battle_attack_count(fighter, count)` | `ogre_battle_attack_count`, the seven stores of `+0x70`, after a battle form rule | 0..15 |
| `class_alignment(limit)` | `ogre_alignment_requirement`, the four alignment loads of the Change Class and qualify checks; the Organize view's chart reads the same filter | 0..255 (255: no limit) |
| `unit_size(size)` | `ogre_class_size` for the Organize rules in bank S, `organize_edit.cpp` `blocked_large`, and the war director's `place_cost` | 1..2 |

The API gained `game.character(n)`, `game.set_exp(n, exp)`, `game.unit(u)`
and `battle.fighter(n)` (`design-lua.md` section 5, `security.md` section 3.1).

## 8. Shared EXP: the split keeps the battle's total

The first version moved each member's EXP toward its share inside its current
level and clamped to 0..99. A member that levelled up past its share kept the
level, the clamp dropped its negative adjustment, and the others still got
their full shares: 100 EXP earned became 156 (review AW-03, 2026-11-01). A
recipient near 99 lost the part of its share above 99.

`shared-exp.lua` gives each member limits in `level * 100 + EXP` units: at
least `max(0, level * 100 - start)` (it keeps every level and never drops
below what it had) and at most `level * 100 + 99 - start`. It splits the total
by weight, fixes the members whose share falls outside their limits at the
limit on the side that is further out, splits the rest again, and rounds so the
shares add up to the total. The sum of the lower limits is never more than the
total and the sum of the upper limits never less, so the unit's gain always
equals what the battle gave.

## 9. Not in the pack yet

These GAMEPLAY rows still live in the war director (`war_director.cpp`) and
read the director's own state. They need more API before they can move, and
each move is tested first (maintainer, 2026-11-01):

- LOOT UPGRADES (`loot_tick`): needs the director's reward multiplier and the
  mission's price band.
- TRAINING CATCH-UP (`training_mark`, `training_build`, the bestiary file):
  needs the training template and a place to keep the bestiary.
- ENEMY WEAPONS, ENEMY FORMATIONS, ENEMY RANKS and the extra members
  (`garrison`, `boss_escorts.cpp`): need the enemy templates and the class
  roles the director shares with its own spawns.

## 10. Tests (2026-11-01)

| test | what it covers | result |
|---|---|---|
| `lua_sandbox_test` (build) | the sandbox | 0 failed |
| `content_pack_test` (build) | hostile packs, and 15 new cases for the argument checks of `game.character`, `game.set_exp`, `game.unit` and `battle.fighter` | 0 failed |
| devtest harness, 12 cases (local) | the pack's scripts load (2, 2 and 3 handlers) and `class-rebalance.json` applies (Faerie `hp_base` 69); `class_alignment` gives 255; `unit_size` gives 1 for size 2, and `ogre_class_size` gives 1 only to bank S rule callers; Faerie heals at 69% and not at 80%, not for a hurt enemy, not for an ally out of the fight; a Soldier with two ready Centurions gets 2 + 2 attacks, a held or enemy Centurion does not count; shared EXP splits 30 EXP as 14/13/3, holds the values 120 frames, finds the player's unit on the second side; AW-03: 100 EXP stays 100 with a level-up (was 156), 80 stays 80, 40 stays 40 with a member near 99 (was 24); AW-01: a handler that adds 128 handlers and errors; 11 bad API calls refused | 12 passed |
| the harness under AddressSanitizer | every case above | 12 passed, no report |
| AW-01 control under AddressSanitizer | the old dispatch loop put back for one build (tag `probe-aw01`, then restored; `grep` finds no tag) | `heap-use-after-free` in `dispatch`, freed by `api_on` |
| `devtest lint` | `build-dist` and every example mod: the pack's 3 scripts compile, `class-rebalance.json` has 245 edits | 0 problems |
| `devtest boot` | no-audio boot to the title | the title was reached, log clean; `OGRE_SCENE=title` now lands at 26.5 s instead of 0.8 s, with or without the pack (section 11) |
| scenario `aw-pack-battle` | the field save plays itself into a battle: 3 scripts load, `[battle] start` and `end`, shared EXP logs its split (5 EXP across 5 members), no script error, back to the field | passed |
| scenario `battle-forms` | the battle-forms example with the pack loaded | passed (37 steps) |

Not played by hand. Faerie heals and the Centurion command were checked on
battle states built in RAM, not in a played battle.

## 11. Open findings from the tests

- `OGRE_SCENE=title` misses its window: the forced id is written once, before
  the game's own scene 0 entry, which replaces it, and the boot plays the
  intro first. The same happens with the pack removed, so the pack is not the
  cause. The last boot that landed at once was at 15:37, before the commits
  `ad75511` and `3c97690`.
- A scenario `watch` step takes a screenshot each second, and each one shows a
  "Screenshot saved" alert, so a stack of alerts covers the corner of the
  window while it runs.

## 12. The MODS tab switch (step 2)

The MODS tab lists the mod folders in `content/` as a first group, SHIPPED
WITH THE GAME, above the player's mods (`design-mods.md` section 5):

- The switch, details, up/down within the group, and DELETE (maintainer,
  2026-11-01: a player who does not want it can delete it). DELETE works once
  the pack is off and the game restarted, like a loaded mod that adds classes.
  It erases the folder (`files::erase` accepts a mod folder with `mod.json`
  directly inside `content/`) and writes the mod OFF in `mod-settings.json`, so
  an update that puts the folder back leaves it off; the switch brings it
  back.
- On or off applies at the next start (section 3.3). The content database
  holds every shipped mod at its start state, the same hold a mod that adds
  classes gets (`content_db.cpp` `g_held`); the Lua runtime loads scripts with
  the same holds. The row reads PENDING until the restart.
- ALL ON and ALL OFF set the player's mods only.
- The choice is the `"enabled"` entry in `mod-settings.json`, as for any mod.

Tests (2026-11-01): the harness case "another-world pack: the MODS tab switch
applies at the next start" switches the pack off through `mod-settings.json`
and checks that it is held on with its rebalance and scripts still running, is
listed as shipped and PENDING, that a scan with no holds (the next start)
loads none of its files, and that switching it back clears PENDING.
`content_pack_test` erases a shipped mod folder, refuses a loose file in
`content/`, `content/` itself, a `content/` folder without `mod.json` and a
shipped item outside `content/`, and lists a deleted shipped mod that comes
back OFF. 13 harness cases pass; `content_pack_test` and `lua_sandbox_test` 0
failed. The tab was
not seen on screen: a background test window (`OGRE_BACKGROUND`, commit
`3c97690`) closes the Esc menu as soon as it opens, so the drawing of the
group, the DELETE refusal while it is loaded and the PENDING status need one
look in a played run.

## 13. The pack's options

The GAMEPLAY rows SHARED EXP, CLASS ALIGNMENT and LARGE UNITS came back as
the pack's own options (`design-mods.md` section 5.2; maintainer, 2026-11-01:
mods may declare settings, and a mod does not have to). They are under the
pack's row in the MODS tab (MORE), kept in `mod-options/another-world.json`:

| key | name | values (default first) | read by |
|---|---|---|---|
| `shared_exp` | SHARED EXP | ON, OFF | `shared-exp.lua` at each battle start |
| `faerie_heals` | FAERIE HEALS | ON, OFF | `battle-tactics.lua` |
| `centurion_command` | CENTURION COMMAND | ON, OFF | `battle-tactics.lua` |
| `class_alignment` | CLASS ALIGNMENT | IGNORED, VANILLA | `class-rules.lua` |
| `large_units` | LARGE UNITS | ONE SQUARE, VANILLA | `class-rules.lua` |

A change applies within about a second, without reloading the scripts. The
class rebalance is data and has no option; switching the pack off removes it.

Tests (2026-11-01): `content_pack_test` 0 failed, with 35 new cases (a good
declaration, twenty hostile declarations refused with their reasons, a values
file at its defaults, read, with wrong types, past max, nested, over 16 KiB
and broken; `write_value` storing a valid value and refusing an off-step value
and an undeclared key; device and hidden file names; a restart option keeping
its first value). Harness 15 passed, also under AddressSanitizer: the pack's
five options each switch their rule (CLASS ALIGNMENT VANILLA keeps limit 40;
LARGE UNITS VANILLA gives 2 places; Faerie and Centurion off; SHARED EXP off
leaves 30 / 0) with no reload; a loose script's `options.get` raises an error;
a player's mod reads its own number option (4) and cannot read the pack's.
Scenario `aw-pack-battle` passes (a played battle, shared EXP logged, no
script error). The options rows were not seen on screen (section 12).
