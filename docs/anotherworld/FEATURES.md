# OB64: Another World — features

What this project adds to lfarroco's recompilation, which it forked at commit
`6429179` (2026-10-14). Everything in that commit (the recompiled game, the app
shell, the launcher, the Esc menu frame, the settings store, the input map and
the renderers) is lfarroco's work and is not listed here.

Each option named below is in the Esc menu, which also lists its default.
"Not played" means the feature is built and compiles, and nobody has tested it
in a game yet.

## Modding

- **Content database.** The game's classes, attacks, items, consumables and
  battle rewards are loaded from the ROM into the port's own database. JSON
  content packs in `content/` (the project's) and `user-content/` (the
  player's) change them by record, field and name, and reload while the game
  runs; the database writes the changed bytes into the game's tables. The class
  rebalance below is such a pack. `content-dump/` in the settings folder holds
  every original record with its name and fields, for reference.
  `content_db.cpp`, `docs/anotherworld/design-content-database.md`.
- **New classes** (not played). A pack makes a class with a key and the class
  it starts as (`copy_of`); the port gives it an id from 172 and keeps it with
  the saves. It is a special class, like Dio's: a script puts a character in
  it, and Change Class does not offer it yet. `docs/anotherworld/design-new-classes.md`.
- **Lua scripts.** `.lua` files in `content/` and `user-content/` react to game
  events, change content records and replace granted items, in a sandbox, and
  reload while the game runs. `lua_runtime.cpp`, `docs/anotherworld/design-lua.md`.
- **Mod folders.** A folder with a `mod.json` (id, name, version, priority,
  requirements, enabled) groups a mod's packs and scripts and gives it a place
  in the load order. `content_files.cpp`, `docs/anotherworld/design-mods.md`.
- **MODS tab** (not played). One table of the player's mods, packs and scripts
  in `user-content/`, in load order: a switch, the name and description,
  the status (loaded, off, pending, refused, conflicts, errors), up/down and
  drag to reorder, details, and delete. The choices are kept in
  `mod-settings.json`. `docs/anotherworld/design-mods.md` section 5.
- **Mod safety.** Packs and scripts cannot reach files, programs or other code
  on the computer; scripts have memory and time limits. Two tests check this on
  every build. `docs/anotherworld/security.md`.
- **LordlyCaliber import.** A LordlyCaliber project folder copied into
  `user-content/` applies its class, item and consumable edits, and the log
  lists the sections not applied yet.
  `docs/anotherworld/design-lordlycaliber-import.md`.
- **ROM patches.** Every `.ips` file in `patches/` is applied to the ROM at
  start, so a text fix or fan translation reaches the port. `rom_patch.cpp`.
- **Asset replacement.** HD texture and font packs and replacement songs; see
  Graphics and audio below.
- **Example mods.** The package carries `content/examples/`: `balance-tweak`,
  `loot-tweaks`, `example-mod` with `example-mod-addon`, and `new-class` with
  the scripts that give and recruit it. It ships no code mods (`DECISIONS.md`,
  2026-11-01); `mods/item-grant-log` and `mods/small-large-units` stay in the
  source tree.

## The war: enemy army

- **War Director** (GAMEPLAY → WAR DIRECTOR, on by default). The enemy earns
  credits from the towns and strongholds it holds and spends them on units
  built from the classes the mission already uses. It deploys them on the
  mission field during the mission, telegraphs attacks with alerts, and pays
  out spoils when its units are beaten. Story units are never touched.
  `app/features/war_director.cpp`, `director_spawn.cpp`.
- **Enemy commander and DIFFICULTY** (EASY / NORMAL / HARD / VETERAN). Gives the
  director's units jobs: defend and retake towns, rest hurt units, pair up, raid
  the player's headquarters, hunt stragglers. `enemy_commander.cpp`.
- **Loot upgrades.** Battle drops are upgraded by the mission's price band.
- **Training catch-up.** Training battles use every class the player has
  fought and bring a unit that is behind the army up toward its level.
- **Enemy weapons: VARIED.** Generic enemy units may carry a stronger weapon of
  their class's type, picked from the mission's price band; leaders are likelier
  to have one. Bosses and story units keep their gear.
- **Enemy formations: TACTICAL.** Generic enemy units are seated by where each
  class fights best, with the front row screening casters and leaders behind a
  member.
- **Enemy ranks: VETERANS.** As the war goes on, more enemy groups take the
  next class of their line (Valkyrie to Freya, Ghost to Lich). Boss escorts
  promote too.
- **Extra members.** Enemy units, including bosses, can field members beyond
  the five a unit record holds, scaled by difficulty. `boss_escorts.cpp`.

## Classes and battle

- **Class rebalance.** The weak unit families are raised by changing the
  game's class and attack records, for both sides, as the content pack
  `content/class-rebalance.json`: Faerie becomes a healer and
  booster, Gremlin a debuffer, Ogre a size-2 bruiser that keeps up, Gorgon's
  bow is fixed, Goblin and Saturos become raiders, the undead get a real
  ladder, and others are raised. `app/content/class-rebalance.json`,
  `class_rebalance.cpp`, `docs/anotherworld/design-class-rebalance.md`.
- **Centurion** (not played). Base stats halfway between Knight and Paladin,
  Slash ×3 / ×2 / ×1. Each Centurion in a battle gives every Soldier on its side
  one more attack, and the bonuses stack. `battle_ai.cpp`.
- **Faerie heals.** When an ally is below 70% HP, Faerie's Throw a Kiss becomes
  Healing. `battle_ai.cpp`.
- **Shared EXP.** A battle's experience is pooled and shared across the whole
  unit. `exp_share.cpp`.
- **Large units: ONE SQUARE.** Large classes take one square in a unit, so they
  can stand side by side.
- **Class alignment: IGNORED.** Class changes do not check alignment.
- **Battle speed.** Battles can run at their own speed (×1 to ×4), apart from
  the field's speed.

## Mission field

- **Field overlay** (PORT MENUS: MODERN). An army strip at the left of the
  screen with one tile per deployed unit: number, leader class, status,
  alignment and HP. It shows the liberate-or-capture verdict for a unit at a
  town. `field_view.cpp`.
- **Mouse orders.** Left click selects a unit (Shift adds), a drag selects a
  box, right click orders the selection to move or to pursue an enemy, and
  Backspace clears. `field_select.cpp`.
- **Pause on select.** Selecting a unit pauses the game so it can be given
  orders.
- **Auto commander** (not played; F4). An AI commands the units the player sends
  out: it takes and defends towns, rests hurt units, and attacks strong enemies
  and the boss in waves. Any order the player gives a unit takes that unit back.
  AUTO ENDING chooses whether it liberates, captures or does either.
  `auto_commander.cpp`.
- **Unit reports: LOG.** A unit's report goes to the alert log and closes by
  itself instead of stopping the game.
- **Field speed and frame rate.** The game's own Slow / Normal / Fast field
  speed is honoured, and FRAME RATE = DISPLAY interpolates the mission field to
  the monitor's refresh rate. `field_speed.cpp`, `frame_rate.cpp`.
- **Alerts.** Short messages stack at the top right and fade. `alerts.cpp`.

## Menus and screens

- **Organize screen (F3).** New Formation and Character views drawn with RmlUi,
  with the game's speaker portraits decoded from the ROM, and edits written back
  through the game's own Organize commit. `organize_view.cpp`,
  `organize_edit.cpp`, `portraits.cpp`.
- **ROSTER tab** in the Esc menu: every character with class and level.
- **RETURN TO TITLE** in the in-game menu.
- **Esc menu layout.** Options grouped into CONTROLS, GRAPHICS and GAMEPLAY
  tabs with sections, rebuilt in RmlUi. Every OFF / ON option lists OFF on the
  left and ON on the right.
- **Button legend (F1).** Names the key and pad button bound to each N64 button
  the game's prompts mention. `button_legend.cpp`.
- **Button prompts.** AUTO / XBOX / PLAYSTATION / NINTENDO.
- **Mouse in menus.** Left click A, right click B, middle click Start, wheel
  D-pad up/down.
- **Accessibility.** UI SCALE (LARGE) and HIGH CONTRAST for the port's menus.

## Pace and convenience

- **Auto-play dialogue (F2).** A dialogue line advances by itself after a pause
  that grows with its length; choices still wait. `dialogue_autoplay.cpp`.
- **Turbo A.** A held A presses and releases in turns.
- **Skip intro.** Boot straight to the title menu.
- **Pause when unfocused.**
- **Keep pointer in window.** Pushing the pointer against an edge scrolls the
  field instead of leaving the window.

## Graphics and audio

- **Pixel filter.** SMOOTH or SHARP scaling of the game picture.
- **HD textures.** Loads texture packs from `texture-packs/`, and TEXT FONT
  picks the font pack for the game's text.
- **MUSIC and EFFECTS sliders** that set the game's own sound driver volumes.
  `docs/anotherworld/notes-audio-mix.md`.
- **Song replacement.** `music/<n>.wav` plays instead of the game's song `n`.
  `music_replace.cpp`.

## Saves and files

- **Save backups.** Copies the cartridge save to `save-backups/` with a time
  stamp. `save_backup.cpp`.
- **Save import.** Converts a save from `save-import/` or the Controller Pak
  file.
- **Save profiles.** Saves made from the port's menu are kept per leader name,
  in addition to the game's own three records. `save_profiles.cpp`.
- **Screenshots** (Print Screen or F12, Windows). PNGs in `screenshots/`.
  `screenshot.cpp`.
- **US revision 0 (in progress).** The app runs only the USA Rev A cartridge
  (revision 1); `app/src/game.hpp` checks its hash. `make rev0` recompiles the
  first USA release (revision 0) for development, and no app build runs it
  yet. `docs/anotherworld/notes-revisions.md` §5.

## Platforms and packaging

- **Release package.** The OB64: Another World name, GPL-3.0-or-later license,
  credits, a `roms` folder for the player's ROM, and `make dist-zip`. No game
  data ships.
- **Android and on-device Linux builds.** `android/`,
  `tools/build-on-device.sh`.
- **Hook table and game events.** Features hook the game through
  `config/hooks.toml` and an event table that mods will share, and reach the
  base only through `app/features/game_host.hpp`, so they can move to another
  base later. `docs/anotherworld/separation.md`.

