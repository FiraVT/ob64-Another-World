# OB64: Another World

An overhaul of the N64 game *Ogre Battle 64: Person of Lordly Caliber* (USA,
Rev A) that runs natively on PC. The game is statically recompiled with the
[N64Recomp] toolchain, and the war, the enemy commanders, the classes and the
item drops are reworked on top of it.

The goal is a modern remake: the recompiled game is the starting engine, and
the project replaces its systems one at a time with native ones that read their
content from data files and their rules from scripts, so new classes, units,
cutscenes, missions and maps can be added. The plan is
[docs/anotherworld/ROADMAP.md](docs/anotherworld/ROADMAP.md); what is built
today is [docs/anotherworld/FEATURES.md](docs/anotherworld/FEATURES.md).

This project is a fork of the Ogre Battle 64 recompilation project,
[lfarroco/ogre-battle-64-recomp](https://github.com/lfarroco/ogre-battle-64-recomp).
Its own source code is at [FiraVT/ob64-Another-World](https://github.com/FiraVT/ob64-Another-World).
[packaging/CREDITS.txt](packaging/CREDITS.txt) lists every project this one is
built on, and ships in the package.

## Built on LordlyCaliber

The overhaul exists because of rempred's
[LordlyCaliber](https://github.com/rempred/LordlyCaliber) (MIT), an Ogre Battle
64 editor built on a decompilation of the game. Before this project found it,
the work had reached the new field controls and stopped there: the game's data
was not understood well enough to change what the enemy fields. LordlyCaliber's
decompiled C, record formats and validator rules supplied that understanding,
and these parts of the overhaul are built directly on it:

- **Custom enemy units and spawning.** The war director writes the game's own
  35-byte enemy unit record (leader, then two member groups of up to three) and
  follows LordlyCaliber's validator rules for it, including the rule that a
  large unit has no neighbours. Garrisons, bought units, boss escorts and
  supply trains are all built this way.
- **Enemy weapons.** The item choice in each member group of that record, and
  the rule that a choice replaces the class's basic weapon in the same slot
  (`squads.js`). The enemy weapon upgrades are built on it.
- **Save data.** Field names in the save table (`hpMax` and its neighbours)
  that the field screens read.
- **The game's code.** Names for about a thousand of the game's functions,
  matched to this port by `tools/revmap.py` and looked up with
  `tools/lc_lookup.py`, and the decompiled routines used to understand the
  field orders and the town liberation rule.
- **Sprites.** The sprite format the Organize screen draws portraits with.

LordlyCaliber is also the planned content editor for this project
(`docs/anotherworld/design-ecosystem.md`).
The name follows the series' habit of taking its titles from songs (Queen's
"Ogre Battle" and "The March of the Black Queen"); "Another World" is a song by
Anohni.

**This repository contains no copyrighted game data.** You must supply your own
ROM dump (see below).

## Play it

1. Download `OB64-AnotherWorld-windows.zip` from the
   [Releases](https://github.com/FiraVT/ob64-Another-World/releases) page and unzip it anywhere.
2. Copy your own dump of *Ogre Battle 64: Person of Lordly Caliber* (USA,
   Rev A; `.z64`, `.n64` or `.v64`, any file name) into the `roms` folder
   inside the unzipped folder.
3. Run `ob64-anotherworld.exe`.

The app checks the ROM by hash and starts the game. Without a usable ROM it
opens the start screen, where the ROM row on the **MAIN** tab picks one
(SPACE), or the file can be dragged onto the window. `README.txt` in the package covers controls,
saves, mods and troubleshooting.

Mods that other people share are listed at
<https://github.com/topics/ob64-mod>.

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
controls and screens, the Organize screen, save profiles, the version, the
`roms` folder and the packaging changes. GPL-3.0 is required in any case,
because the app links N64ModernRuntime, which is GPL-3.0.

Third-party components keep their own licenses; see
[packaging/CREDITS.txt](packaging/CREDITS.txt).

## Legal

Ogre Battle 64 © Quest / Nintendo. This project is for preservation and
interoperability research. Never distribute the ROM or its extracted assets.

[N64Recomp]: https://github.com/N64Recomp/N64Recomp

