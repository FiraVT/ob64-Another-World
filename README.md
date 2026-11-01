# OB64: Another World

OB64: Another World runs the Nintendo 64 game *Ogre Battle 64: Person of
Lordly Caliber* as a native program on Windows, Linux and macOS, and changes
the game: the enemy builds and deploys new units during a mission, the classes
and battle rules are rebalanced, the mission field takes mouse orders, several
menus are new, and the game's data and rules can be changed by mods made of data
files and Lua scripts.

**This repository and its releases contain no game data.** You need your own
dump of the USA Rev A cartridge (see [Play it](#play-it)).

## What changes in the game

- **A stronger enemy army.** The War Director earns money from the towns the
  enemy holds and spends it on new units during a mission. An enemy commander
  gives those units jobs: retake towns, raid your headquarters, hunt
  stragglers. DIFFICULTY sets how hard it plays, from EASY to VETERAN. Generic
  enemy units can carry stronger weapons and stand in formations that suit
  their classes. Enemy groups promote to the next class of their line as the
  war goes on, and enemy units, bosses included, can field extra members.
- **Class and battle rules.** The weak unit families are raised: Faerie heals,
  Gremlin debuffs, Ogre becomes a size-2 bruiser, and the undead get a real
  class ladder. Options share a battle's experience across the whole unit, let
  large classes take one square so they can stand side by side, and let class
  changes ignore alignment.
- **Field command.** Select and order units with the mouse, with an army strip
  and alerts on screen. An auto commander (F4) can command the units you send
  out.
- **New screens.** An Organize screen (F3), a roster, a button legend (F1) and
  an Esc menu with every setting.
- **Pace and display.** Battle speed apart from field speed, the mission field
  drawn at the monitor's refresh rate, auto-play dialogue (F2) and turbo A.
- **Saves.** Timestamped backups, saves kept per leader name, and save import
  from `save-import/` or a Controller Pak file.

The options are in the Esc menu. Some features are built and not yet tested in
play; [docs/anotherworld/FEATURES.md](docs/anotherworld/FEATURES.md) marks them
and has the full list.

## Modding

Mods change the game's data and rules without changing its code. A mod is a
folder of data files and Lua scripts in `user-content/`, and the port loads it
at start and reloads it while the game runs. The project's own class rebalance
is built this way: it is the content pack `content/class-rebalance.json`.

- **Content packs.** JSON files change the game's classes, attacks, items,
  consumables and battle rewards by record, field and name, and can rename any
  of them. The port reads those tables from the ROM into its own database and
  writes the changes back into the game. `content-dump/` lists every original
  record for reference.
  [design-content-database.md](docs/anotherworld/design-content-database.md)
- **New classes.** A pack can add a class based on an existing one. The port
  gives it a new id and keeps it with the saves, and Change Class can offer it.
  A pack can give the class its own portrait, formation figures and battle
  colours. [design-new-classes.md](docs/anotherworld/design-new-classes.md),
  [design-class-art.md](docs/anotherworld/design-class-art.md)
- **Lua scripts.** Scripts react to game events (a mission loading, the field
  updating, an item being granted), read and change content records, replace
  granted items, change a character's class and recruit new characters.
  [design-lua.md](docs/anotherworld/design-lua.md)
- **Mod folders and the MODS tab.** A `mod.json` gives a mod its id, version,
  load-order priority and the mods it requires. The MODS tab turns mods on and
  off, reorders them, and shows conflicts and errors.
  [design-mods.md](docs/anotherworld/design-mods.md)
- **Safety.** Packs and scripts cannot reach files, programs or other code on
  the computer, and scripts run with memory and time limits. Two tests check
  this on every build. [security.md](docs/anotherworld/security.md)
- **LordlyCaliber projects.** A project made in rempred's editor, copied into
  `user-content/`, applies its class, item and consumable edits.
  [design-lordlycaliber-import.md](docs/anotherworld/design-lordlycaliber-import.md)
- **Assets and patches.** HD texture and font packs, HD backdrops
  ([hd-backgrounds.md](docs/guides/hd-backgrounds.md)), replacement songs
  (`music/<n>.wav`), and `.ips` ROM patches such as text fixes and fan
  translations.

The package carries worked examples in `content/examples/`: a balance tweak, a
loot script, a mod with an add-on, and a new class with a script that recruits
it. Mods that other people share are listed at
<https://github.com/topics/ob64-mod>.

## How the project is put together

The game is not emulated. Its code is translated once into native code, and
this project changes the game on top of that translation. Three projects stack:

1. **[N64Recomp]**, by Wiseguy, statically recompiles N64 machine code into C
   that compiles into a native program. N64ModernRuntime and the RT64 renderer
   run that program.
2. **[lfarroco/ogre-battle-64-recomp](https://github.com/lfarroco/ogre-battle-64-recomp)**,
   by lfarroco, applies N64Recomp to Ogre Battle 64: the recompiled game, the
   app around it, the launcher and the settings.
3. **This project** started as a fork of lfarroco's recompilation at commit
   `6429179` (2026-10-14) and adds the overhaul. Its history in this
   repository starts at a single commit; lfarroco's commits are in their
   repository. FEATURES.md lists only what this project added.

A fourth project supplied most of what is known about the game's data:
rempred's [LordlyCaliber](https://github.com/rempred/LordlyCaliber), an Ogre
Battle 64 editor that ships a partial decompilation of the game. This project
does not run LordlyCaliber. It uses its research (see [Credits](#credits)), and
the port can load content made in it.

## Where it is going

The goal is a modern remake. The recompiled game is the starting engine, and
the project replaces its systems one at a time with native ones that read
their content from data files and their rules from Lua scripts. New classes,
units, cutscenes, missions and larger maps can then be added without engine
work. The plan and its order are in
[docs/anotherworld/ROADMAP.md](docs/anotherworld/ROADMAP.md).

## Play it

1. Download `OB64-AnotherWorld-windows.zip` from the
   [Releases](https://github.com/FiraVT/ob64-Another-World/releases) page and unzip it anywhere.
2. Copy your own dump of *Ogre Battle 64: Person of Lordly Caliber* (USA,
   Rev A; `.z64`, `.n64` or `.v64`, any file name) into the `roms` folder
   inside the unzipped folder.
3. Run `ob64-anotherworld.exe`.

The game had two USA releases, and the app runs only the second, Rev A
(revision 1, header byte `0x3F` = 1). The first USA release, other regions and
patched ROMs are refused. Support for the first USA release is in progress
(`docs/anotherworld/notes-revisions.md`).

The app checks the ROM by hash and starts the game. Without a usable ROM it
opens the start screen, where the ROM row on the **MAIN** tab picks one
(SPACE), or the file can be dragged onto the window. `README.txt` in the package covers controls,
saves, mods and troubleshooting.

## System Requirements

A 64-bit PC and a GPU the renderer supports:

* **GPU** — Direct3D 12.0 (Shader Model 6.0) or Vulkan 1.2 on Windows, Vulkan
  1.2 on Linux, Metal on macOS. The oldest GPUs those cover are roughly GeForce
  GT 630, Radeon HD 7750 (2012) and Intel HD 510 (Skylake).
* **CPU** — x86-64 with SSE4.1 (Intel Core 2 Penryn, AMD Bulldozer or newer).
  The macOS build needs Apple Silicon.
* **OS** — Windows 10 or 11 (64-bit); macOS 11 or newer on Apple Silicon; a
  glibc Linux with a Vulkan 1.2 driver.
* **RAM** — 2 GB. The emulated N64 machine commits 512 MB.
* **Disk** — about 150 MB for the app, plus the ROM.
* **Game data** — your own Ogre Battle 64 (USA, Rev A) cartridge dump, 40 MB in
  `.z64`, `.n64` or `.v64`. No game data is included.

A keyboard is enough to play; a gamepad with XInput (Windows) or SDL controller
support is optional. An audio device is optional too: with none, the game runs
silently. On Windows no Visual C++ redistributable is required — the package
carries the runtime its shader compiler needs.

If the game crashes as it starts, update the graphics driver first. On an older
GPU, `OGRE_CONSOLE=1` opens a console with the boot log and `OGRE_GRAPHICS_API`
pins the backend (`vulkan` or `d3d12`).

## AI use

Much of this project's code and documentation is written with AI coding
assistants under the maintainer's direction; the maintainer
plays the game to test the work. lfarroco's recompilation, which this project
forks, began as an experiment with DeepSeek. Contributions are welcome whether
or not they use AI tools, and each contributor is responsible for checking what
they submit.

## Status

This project's plan is [docs/anotherworld/ROADMAP.md](docs/anotherworld/ROADMAP.md),
its features are [docs/anotherworld/FEATURES.md](docs/anotherworld/FEATURES.md),
and its decisions are [docs/anotherworld/DECISIONS.md](docs/anotherworld/DECISIONS.md).
[PLAN.md](PLAN.md) holds the recompilation's status and technical findings.

## Contributing

OB64: Another World is an open-source community project, and contributions
are welcome: code, research into the game, content, art, testing and docs.
Contributions are licensed GPL-3.0-or-later, like the rest of this project's
work.

- [CONTRIBUTING.md](CONTRIBUTING.md): set up a build on Windows, Linux or
  macOS, and where to start.
- [docs/README.md](docs/README.md): the documentation index.
- [docs/anotherworld/ROADMAP.md](docs/anotherworld/ROADMAP.md): the systems
  and phases that are open.
- [AGENTS.md](AGENTS.md): the working rules for reverse-engineering the game,
  for people and AI agents alike.

A fresh clone cannot build the app on its own. The recompiled C is generated
from your own ROM by `make regenerate` and is never committed.

## License

This project's own work is licensed under the GNU General Public License,
version 3 or (at your option) any later version; the text is in
[LICENSE](LICENSE). That covers the overhaul: the war director, the enemy
commander, unit spawning and boss escorts, the class rebalance, the field
controls and screens, the Organize screen, the content database, Lua scripts
and mod system, save profiles, the version, the
`roms` folder and the packaging changes. GPL-3.0 is required in any case,
because the app links N64ModernRuntime, which is GPL-3.0.

Third-party components keep their own licenses; see
[packaging/CREDITS.txt](packaging/CREDITS.txt).

## Credits

[packaging/CREDITS.txt](packaging/CREDITS.txt) lists every project this one is
built on, with its license, and ships in the package.

**LordlyCaliber.** Before this project found rempred's
[LordlyCaliber](https://github.com/rempred/LordlyCaliber) (MIT), the work had
reached the new field controls and stopped there: the game's data was not
understood well enough to change what the enemy fields. LordlyCaliber's
decompiled C, record formats and validator rules supplied that understanding.
These parts are built directly on it:

- **Custom enemy units and spawning.** The War Director writes the game's own
  35-byte enemy unit record (a leader, then two member groups of up to three).
  Garrisons, bought units, boss escorts and supply trains are all built this
  way.
- **Enemy weapons.** The item choice in each member group of that record, and
  the rule that a choice replaces the class's basic weapon in the same slot
  (LordlyCaliber's `squads.js`). The enemy weapon upgrades are built on it.
- **Save data.** Field names in the save table (`hpMax` and its neighbours)
  that the field screens read.
- **The game's code.** Names for 1,070 of the game's functions, matched to this
  port by `tools/revmap.py` and looked up with `tools/lc_lookup.py`, and the
  decompiled routines used to understand the field orders and the town
  liberation rule.
- **Sprites.** The sprite format the Organize screen draws portraits with.

The port also imports LordlyCaliber projects, so content made in rempred's
editor plays here (`docs/anotherworld/design-ecosystem.md`).

**frankjniu's Windows fork.**
[frankjniu/ogre-battle-64-recomp-windows](https://github.com/frankjniu/ogre-battle-64-recomp-windows)
forked lfarroco's recompilation from the same commit. This project ports
several of its fixes and features, each with the measurement that found it:
the audio task reply, the idle thread that held a CPU core, RT64's spinning
queues, the lookup check for streamed code, sharp dialogue boxes at high
resolution, the cutscene backdrop's missing row, the hang report and the
F5/F9 quick save.

**The name.** The series takes its titles from songs: Queen's "Ogre Battle" and
"The March of the Black Queen". "Another World" is a song by Anohni, released
by Antony and the Johnsons.

## Legal

Ogre Battle 64 © Quest / Nintendo. This project is for preservation and
interoperability research. Never distribute the ROM or its extracted assets.

[N64Recomp]: https://github.com/N64Recomp/N64Recomp

