# Changelog

Changes between releases of OB64: Another World. The design notes and the
evidence behind each change are in `docs/anotherworld/DECISIONS.md`.

## 0.3.0 (2026-11-02)

### Controls

- **Camera zoom.** The mission field zooms from 0.5x to 3.0x the game's camera
  distance, and the world map zooms too. Use the mouse wheel, or hold LT and
  move the right stick on a controller. CAMERA ZOOM in CONTROLS turns it off.
- **MODERN field controls** (PORT MENUS = MODERN; CLASSIC is unchanged):
  - The camera pans with the pointer at the window edge, the cursor keys or
    the right stick. The edge pan never moved the camera before.
  - The left stick has a response curve, so small pushes move the cursor.
  - LT + D-pad presses the C buttons.
  - With the keyboard and mouse, the OS pointer is the game's wand.
  - A click on a selection card selects only that unit; Shift + click
    removes it.

### Gameplay

- **The Another World rules are a mod.** The class rebalance, Faerie heals,
  the Centurion command, shared EXP, class changes without alignment limits
  and large units in one square are the shipped mod `content/another-world/`.
  Each one has an option under the mod's row in the MODS tab, in place of the
  old GAMEPLAY rows.
- **Breath statuses and damage over time** (Another World mod): the
  elemental breaths cause poison, sleep, paralysis or burn. Poison grows each
  round of a fight, up to five times its first tick. Fire Breath burns: up
  to 3 stacks, each dealing damage every round and lowering the fighter's
  damage by 10%. A burning fighter shows a BURNING label.
- **Story units** keep the game's own classes; the Wizard with dialogue on
  Tenne Plains is a Wizard again.
- **Boss units** gain escorts by difficulty: NORMAL +1 for the first 10
  missions, then 1 or 2; HARD and VETERAN 1 to 4. The extra boss
  reinforcement inside the unit is removed, so the first boss fights are
  smaller.
- The auto commander takes a unit back when the order you gave it finishes.

### Modding

- **Mod options.** A mod declares settings (toggle, choice, number) in its
  `mod.json`; players set them in the MODS tab, and scripts read them with
  `options.get(key)`.
- **The MODS tab** lists the shipped mods as their own group with a switch,
  ordering and DELETE.
- **Battle forms** (`battle_forms`): a class fights each strike of a battle as
  another class with its own attack, with the game's transformation effect.
  A strike's form and attack can each be a list drawn at random, and
  `"attack": "breath"` gives the form's breath from any row. The battle-forms
  and Dragon Knight examples are in the source repository, not the package.
- **Damage over time** (`dots`) and **attack statuses** (`status`,
  `status_rule`, `status_chance`, Lua filter `battle_status`) for packs.
  A damage over time effect can carry its own label, a PNG or a coloured
  word.
- **Story units** (`story_units`): a pack can mark a unit whose classes the
  port leaves alone.
- **LordlyCaliber import** reads the attacks section (attack records, names
  and spell links) and lists the settings it cannot apply.
- Imported class records get their class ids translated, and
  `saves/class-ids.json` is saved safely.

### Fixes

- A battle figure missing from the cache is loaded on demand instead of
  hanging the game.
- The game never writes to `%APPDATA%`; settings, saves and mods stay beside
  the executable.

### Not yet played

- Clicking squares with the mouse at the zoom limits.

## 0.2.0 (2026-11-01)

- Audio timing, lower CPU use, sharp dialogue boxes and full cutscene
  backdrops, ported from frankjniu's Windows fork.
- Quick save (F5) and quick load (F9).
- A hang report in `error.log` when the game stops producing frames.
- The mouse is on by default; A opens the title menu at PRESS START.

## 0.1.0 (2026-11-01)

- First release of OB64: Another World for Windows.
