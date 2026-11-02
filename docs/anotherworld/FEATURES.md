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
  runs; the database writes the changed bytes into the game's tables. A pack
  can also rename any class, attack, item or consumable. The class rebalance
  below is such a pack. `content-dump/` in the settings folder holds every
  original record with its name and fields, for reference.
  `content_db.cpp`, `docs/anotherworld/design-content-database.md`.
- **New classes.** A pack makes a class with a key and the class it starts as
  (`copy_of`); the port gives it an id from 172 and keeps it with the saves,
  and the class gets battle animation copied from that class. A pack with a
  `"change_class"` block offers the class in Change Class. Played 2026-11-01:
  the modern Change Class view over the Organize screen lists the example's
  Dragon Knight for a Fighter, the change applies, and the character fights.
  The classic Change Class menu's support is built and not played. Without the
  block the class is special, like Dio's, and a script puts a character in it.
  `class_change.cpp`, `class_animation.cpp`,
  `docs/anotherworld/design-new-classes.md`.
- **New class art** (not played). A pack's `art` block gives a new class its
  own portrait (40x48), formation figures (16x24, the ordinary lane) and
  battle sprite colours; the port builds them into the ROM image as game
  assets at start, so the game's own loaders draw them. `class_art.cpp`,
  `docs/anotherworld/design-class-art.md`.
- **Battle figures on demand** (not played). A fighter whose class changes
  during a battle (a mod or a script) gets its new class's figure loaded at its
  first draw; before, the game loaded figures only at the battle's start and
  the draw hung on a class it had not loaded. A full figure cache (20) or a
  failed load is logged as `[battle-figure]`. `battle_figures.cpp`,
  `docs/anotherworld/design-battle-transform.md`.
- **Battle forms** (passed a scripted battle run, not played by hand). A pack's `battle_forms` rule makes a class
  fight each strike of a battle as another class with its own attack: the
  example `battle-forms` mod gives every Fighter three strikes, a Slash, a
  Claw as a Werewolf and Rotten Breath as a Tiamat from any row, with the
  game's transformation effect. The fighter turns back after its strike; the
  character's saved class and equipment do not change, and the game's "has
  changed class" message is not shown. A strike's form can be a list drawn at
  random, its attack a list drawn at random (never the same attack twice in a
  row when the list has another) or "breath" (the form's own breath), and `repeat` applies
  the strikes to every strike: the Dragon Knight example turns into a random
  elder dragon on each strike (passed a scripted battle run). `battle_form.cpp`,
  `docs/anotherworld/design-battle-transform.md`.
- **Lua scripts.** `.lua` files in `content/` and `user-content/` react to game
  events, change content records and replace granted items, in a sandbox, and
  reload while the game runs. Battle start and end events; filter events that
  change a fighter's attack and attack count, a class change's alignment limits
  and the places a large class takes; `game.character`, `game.set_exp`,
  `game.unit` and `battle.fighter`. `lua_runtime.cpp`,
  `docs/anotherworld/design-lua.md`.
- **Mod options** (not played). A mod may declare settings in `mod.json`
  (toggles, choices, numbers, each with a default and help); the player sets
  them under the mod's row in the MODS tab, they are kept per mod in
  `mod-options/<id>.json`, and the mod's scripts read them with `options.get`.
  `mod_options.cpp`, `docs/anotherworld/design-mods.md` section 5.2.
- **Mod folders.** A folder with a `mod.json` (id, name, version, priority,
  requirements, enabled) groups a mod's packs and scripts and gives it a place
  in the load order. `content_files.cpp`, `docs/anotherworld/design-mods.md`.
- **MODS tab** (not played). The mods shipped with the game (the Another World
  pack: switch, order and delete, on or off at the next start; a deleted one
  stays off if an update puts it back), then one
  table of the player's mods, packs and scripts
  in `user-content/`, in load order: a switch, the name and description,
  the status (loaded, off, pending, refused, conflicts, errors), up/down and
  drag to reorder, details, and delete. The choices are kept in
  `mod-settings.json`. `docs/anotherworld/design-mods.md` section 5.
- **Mod safety.** Packs and scripts cannot reach files, programs or other code
  on the computer; scripts have memory and time limits. Two tests check this on
  every build. `docs/anotherworld/security.md`.
- **LordlyCaliber import.** A LordlyCaliber project folder copied into
  `user-content/` applies its class, item and consumable edits, and the log
  lists the sections not applied yet. From its `attacks` section the port
  applies attack record edits, attack names and the spell arrays of the
  generic magic attacks 45..48 and 145; it lists added attacks and the
  settings LordlyCaliber applies with its own ROM hooks (target patterns, one
  strike for all targets, undead slaying and the rest). Checked by the pack
  test and against the ROM with `devtest lint`; not played.
  `docs/anotherworld/design-lordlycaliber-import.md`.
- **Spell maps.** A pack or script changes which attack a generic magic
  attack (45..48, 145) becomes for each element of the caster's gear, through
  the `spells` table. Not played. `docs/anotherworld/design-content-database.md`
  section 7.0.
- **ROM patches.** Every `.ips` file in `patches/` is applied to the ROM at
  start, so a text fix or fan translation reaches the port. `rom_patch.cpp`.
- **Asset replacement.** HD texture and font packs and replacement songs; see
  Graphics and audio below.
- **Example mods.** The package carries `content/examples/`: `balance-tweak`,
  `loot-tweaks`, `example-mod` with `example-mod-addon`, and `battle-forms`. The `new-class`
  example (the Dragon Knight) is the reference for new classes and stays in
  `app/content/examples/`; the package leaves it out. It ships no code mods (`DECISIONS.md`,
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

- **Another World mod pack.** The rules below marked *(pack)* are the shipped
  mod `content/another-world/`, on by default; the MODS tab switches it, and
  the change applies at the next start. Its options (MORE under its row) turn
  SHARED EXP, FAERIE HEALS and CENTURION COMMAND on or off and set CLASS
  ALIGNMENT and LARGE UNITS (IGNORED / ONE SQUARE or VANILLA).
  `docs/anotherworld/design-another-world-pack.md`.
- **Class rebalance** *(pack)*. The weak unit families are raised by changing the
  game's class and attack records, for both sides, as the pack's
  `class-rebalance.json`: Faerie becomes a healer and
  booster, Gremlin a debuffer, Ogre a size-2 bruiser that keeps up, Gorgon's
  bow is fixed, Goblin and Saturos become raiders, the undead get a real
  ladder, and others are raised. `app/content/another-world/class-rebalance.json`,
  `docs/anotherworld/design-class-rebalance.md`.
- **Centurion** *(pack; not played)*. Base stats halfway between Knight and Paladin,
  Slash ×3 / ×2 / ×1. Each Centurion in a battle gives every Soldier on its side
  one more attack, and the bonuses stack. `battle-tactics.lua`.
- **Faerie heals** *(pack)*. When an ally is below 70% HP, Faerie's Throw a Kiss becomes
  Healing. `battle-tactics.lua`.
- **Shared EXP** *(pack)*. A battle's experience is pooled and shared across the
  whole unit. A member keeps every level it gained, and the unit's total gain
  stays what the battle gave. `shared-exp.lua`.
- **Large units: one square** *(pack)*. Large classes take one square in a unit, so they
  can stand side by side. `class-rules.lua`.
- **No class alignment limits** *(pack)*. Class changes do not check alignment.
  `class-rules.lua`.
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
  and the boss in waves. A move, pursue or Return the player gives a unit takes
  that unit until the order finishes, and then the AI commands it again; a Stop
  or the unit card's AI button keeps it until the AI button hands it back. A new
  mission and turning the AI on give every unit back to the AI. AUTO ENDING chooses whether it liberates, captures or does either.
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
- **Confirm at PRESS START.** A (Enter or Space on the keyboard) opens the
  title menu as Start does; the game tests only Start there.
  `bank_overlays.cpp` `ogre_skip_title_press`.
- **Pause when unfocused.**
- **Keep pointer in window.** Pushing the pointer against an edge scrolls the
  field instead of leaving the window.

## Graphics and audio

- **Pixel filter.** SMOOTH or SHARP scaling of the game picture.
- **Sharp 2D rectangles.** Stretched 2D rectangles (the dialogue boxes)
  sample at the console's pixel positions at any resolution, so the boxes
  draw without seams (RT64 `upscale2D` `Original`; `OGRE_UPSCALE_2D`).
  `renderer.cpp`. From frankjniu's Windows fork.
- **Cutscene backdrops** place their bottom half at canvas row 239, as the
  game draws it, so no row is missing across the picture.
  `hd_backgrounds.cpp` `chunk_rect`. From frankjniu's Windows fork.
- **HD textures.** Loads texture packs from `texture-packs/`, and TEXT FONT
  picks the font pack for the game's text.
- **MUSIC and EFFECTS sliders** that set the game's own sound driver volumes.
  `docs/anotherworld/notes-audio-mix.md`.
- **Song replacement.** `music/<n>.wav` plays instead of the game's song `n`.
  `music_replace.cpp`.

## Saves and files

- **Save backups.** Copies the cartridge save to `save-backups/` with a time
  stamp. `save_backup.cpp`.
- **Quick save (F5) and quick load (F9).** F5 copies the battery save to
  `saves/<game id>-quick.bin`; F9 twice within 3 s puts it back and restarts
  the program at the title. `quick_save.cpp`. From frankjniu's Windows fork.
- **Hang report.** When the game produces no frame for 5 s while its window
  is open and the game is not paused on purpose (Esc menu, PAUSE ON SELECT),
  the port writes `error.log` with the message-queue snapshot
  (`OGRE_HANG_MS`, `0` turns it off). `hang_watch.cpp`. From frankjniu's
  Windows fork.
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

