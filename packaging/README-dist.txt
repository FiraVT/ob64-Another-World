OB64: Another World
===================

An overhaul of Ogre Battle 64: Person of Lordly Caliber (USA, Rev A) that runs
natively on PC. The original N64 code is statically recompiled, and the war,
the enemy commanders, the classes and the item drops are reworked on top of it.
It is a fork of the Ogre Battle 64 recompilation project
(https://github.com/lfarroco/ogre-battle-64-recomp).

The overhaul is built on the research of rempred's LordlyCaliber
(https://github.com/rempred/LordlyCaliber), an Ogre Battle 64 editor built on a
decompilation of the game. Its decompiled code, record formats and rules are
what made the custom enemy units, unit spawning, enemy weapons, the field
orders and the Organize screen's portraits possible. CREDITS.txt describes this in
full and lists every project this one is built on.

This program is free software under the GNU General Public License, version 3
or later; see LICENSE.txt. It comes with no warranty. The source code of
this program, including every release, is at:

    https://github.com/FiraVT/ob64-Another-World

This package contains no game data. You must supply your own ROM dump of the
USA Rev A cartridge — the executable is useless without it, and none of the
game's copyrighted content is included here.


REQUIREMENTS
------------

A 64-bit PC and a GPU the renderer supports:

    GPU    Direct3D 12.0 (Shader Model 6.0) or Vulkan 1.2 on Windows, Vulkan 1.2
           on Linux, Metal on macOS. The oldest GPUs those cover are roughly
           GeForce GT 630, Radeon HD 7750 (2012) and Intel HD 510 (Skylake).
    CPU    x86-64 with SSE4.1 (Intel Core 2 Penryn, AMD Bulldozer or newer).
           The macOS build needs Apple Silicon.
    OS     Windows 10 or 11 (64-bit); macOS 11 or newer on Apple Silicon; a
           glibc Linux with a Vulkan 1.2 driver.
    RAM    2 GB. The emulated N64 machine commits 512 MB.
    Disk   about 150 MB for the app, plus your ROM.
    ROM    your own Ogre Battle 64 (USA, Rev A) dump, 40 MB, `.z64`, `.n64` or
           `.v64`. No game data is included.

A keyboard is enough to play; a gamepad with XInput (Windows) or SDL controller
support is optional. An audio device is optional too: with none, the game runs
silently.

On Windows nothing else has to be installed. The package carries the Microsoft
C++ runtime that its shader compiler (`dxcompiler.dll`) needs.

**If the game crashes as it starts, update your graphics driver first.** On an
older GPU, set `OGRE_CONSOLE=1` before launching to open a console with the boot
log, then try `OGRE_GRAPHICS_API=vulkan` or `OGRE_GRAPHICS_API=d3d12` to pin the
renderer backend (`set OGRE_CONSOLE=1` in `cmd`, or the environment variables
under System Properties).


HOW TO PLAY
-----------

1. Put your ROM in the `roms` folder.

   Copy your Ogre Battle 64 (USA, Rev A) dump into the `roms` folder of this
   package. Any file name works, and `.z64`, `.n64` and `.v64` are all
   accepted; the app converts the byte order. The `roms` folder is the only
   place the app needs anything from you.

2. Run the app:

   - **Windows:** `ob64-anotherworld.exe`.
   - **Linux:** `ob64-anotherworld`.
   - **macOS:** open `OB64 Another World.app` (double-click it). Open the bundle and
     not the executable inside `Contents/MacOS`: Finder opens a Terminal window
     for a bare executable.

   The app checks every ROM in `roms` by hash and uses the one that is the
   **USA Rev A** cartridge (internal name `OgreBattle64`). A ROM of another
   region, another revision (the first USA release), or a patched one is
   skipped, and the start screen says why.

3. With a good ROM in `roms`, the app goes straight into the game.

   Without one, the start screen asks for it: select the **ROM** tab and
   press SPACE to pick the file, or drag the file onto the window. The app
   stores the ROM, so later launches go straight into the game.

       OB64: ANOTHER WORLD
       [ START GAME ] [ ROM ] [ MODS ] [ CONTROLS ] [ SETTINGS ]

   Hold **Shift** while the app starts to open the start screen anyway, for
   example to change the ROM or restore a save.

The game, its save and its mods live in this folder; keep it together.


SAVES
-----

Battery saves (the cartridge SRAM) are written to:

    saves/ogrebattle64-us-rev1.bin

in this folder, next to the executable. Back that file up to keep your
progress; delete it to start fresh. Setting the environment variable
`OGRE_PREF_DIR` to another directory moves both the save and the stored ROM
there instead.

The Esc menu's MAIN tab backs the save up into `save-backups/`. Restoring a
backup and importing a save (from `save-import/` or a Controller Pak file) are
on the start screen, because they must happen before the game reads the save:
hold **Shift** while starting the game to open it.


SCREENSHOTS
-----------

Print Screen (or F12) saves the game window as a PNG in:

    screenshots/OB64-<date>-<time>.png

in this folder, and a message in the top right of the game names the file.


START SCREEN
------------

The start screen appears when no ROM is loaded, when a code mod (`.nrm`) is
installed, and when Shift is held while the app starts. It has five tabs:

    [ START GAME ]   [x] Start Game starts the game. It reads [ ] and cannot be
                     selected until a ROM is loaded.
    [ ROM ]          [x] Loaded! and the ROM's file name, or [ ] No ROM.
                     Selecting it opens the file picker; the chosen ROM comes
                     back here as loaded, with START GAME ready.
    [ MODS ]         one row per installed mod, with its short description
    [ CONTROLS ]     one row per N64 button, showing the keyboard key and the
                     gamepad button bound to it, and what the button does
    [ SETTINGS ]     GAME SPEED: [ ] 1  [ ] 2  [x] 4. The game runs that many
                     times faster than real time; 1 is normal speed.

    TAB / SHIFT+TAB  switch tab
    UP / DOWN        select a row
    SPACE            activate the selected row: START GAME starts the game, the
                     ROM row opens the file picker, a mod row turns the mod on
                     or off, an option row steps its value, the GAME SPEED row
                     steps the speed, a CONTROLS row starts a rebind (press the
                     key or gamepad button you want)
    LEFT / RIGHT     change the selected option's value
    ENTER            start the game, or open the ROM picker when there is no ROM
    mouse            click a tab to switch, a row to activate it, a GAME SPEED
                     number to select that speed, elsewhere to start

Bindings are saved in `controls.cfg` in this folder, and the game speed in
`settings.cfg`. **ESC** opens the same panel over the running game, with the ROM
and MODS tabs disabled and CONTROLS and SETTINGS active, so bindings and the
game speed can be changed mid-game; **ESC** closes it again.

A mod's own options live in `mod_config/<mod id>.json`; the start screen is the
normal way to change them.


MODS
----

Most mods are folders of JSON and Lua in `user-content/` (see CONTENT PACKS).
The **MODS** tab, in the game's Esc menu and on the start screen, is one table
of them in load order: switch one on or off, drag it or use its arrows to
change the order, open its details, or delete it.

The `mods/` folder is for code mods (`.nrm` files), compiled code that changes
the game itself. The package carries none; install code mods only from people
you trust. Every `.nrm` in `mods/` is opened at startup and listed on the
start screen's **MODS** tab, where it is switched on or off.

The start screen appears when no ROM is found or a code mod is installed. Hold
**Shift** while starting the game to open it anyway: it has the ROM picker and
the save restore and import (see SAVES). Dropping a `.nrm` into `mods/`
installs it, and deleting it uninstalls it.

Mods are recompiled into the client at startup, so a mod built for a different
build of the game may be refused; the start screen prints the reason.


CONTENT PACKS
-------------

The game's classes, attacks, spells, items and consumables can be changed with
JSON files. `content/` in this folder holds the overhaul's own mod,
`content/another-world/` (the class rebalance and the Another World battle,
EXP and class rules); an update replaces that folder. Its options, and any
other mod's, are set in the Esc menu's MODS tab under the mod's MORE, and kept
in `mod-options/` in this folder.
Put your own packs in `user-content/` in this folder (create it), which an
update never touches. Packs load in file-name order, `content/` first, and a
later pack wins. The game reads the files again about a second after one
changes, so an edit shows in the next battle without a restart. A file with a
mistake is skipped, and the reason is written to the log. The format is in
docs/anotherworld/design-content-database.md in the source repository.

At every start the game writes `content-dump/` in this folder: every original
class, attack, item and consumable with its name and values. Look a value up
there, then copy the record into a file in `user-content/` and change it.
Editing `content-dump/` itself changes nothing.

Lua scripts (`.lua` files) in `content/` and `user-content/` load the same way
and can change records and react to game events. A
script error is written to the log and does not stop the game.

Content packs and scripts cannot touch files on your computer, start programs
or load other code. Scripts run in a sandbox with a memory and a time limit.
The game checks this on every build. The details, and what is not covered
(code mods in `mods/`), are in docs/anotherworld/security.md in the source
repository. Install code mods only from people you trust.

A mod can be a folder in `user-content/` with a `mod.json` (its id, name,
version, description, priority and the mods it needs) and its `.json` and
`.lua` files. The MODS tab lists each mod, or why it was not loaded.

Mods other people share on GitHub carry the topic `ob64-mod`, and this page
lists them:

    https://github.com/topics/ob64-mod

To share your own mod, put its folder in a GitHub repository and add the topic
`ob64-mod` to it (the gear icon next to "About" on the repository's page).

`content/examples/` holds example mods. They do nothing until you copy a
folder into `user-content/`:

    balance-tweak/       one pack: an item and a class changed by named fields
    loot-tweaks/         one script: a changed drop, an item swap, an item log
    example-mod/         a pack and a script together (an "Iron Soldier")
    example-mod-addon/   a mod that needs example-mod and builds on it

A pack can add a new class: a record in "classes" keyed "<mod id>.<name>" with
"copy_of" set to the class it starts as. The game gives it an id and keeps it
in saves/class-ids.json; do not delete that file while saves use new classes.

A LordlyCaliber project folder (the folder with `project.json` in it) copied
into `user-content/` applies its class, item and consumable edits. Its other
parts (text, shops, new classes and items, art) are not applied yet; the log
lists them.


IF THE GAME DOES NOT START
--------------------------

The reason is written to `error.log` in this folder and, when the failure
happens before the game window exists, shown in a dialog. Check the
REQUIREMENTS section above first: the most common cause on Windows is a GPU
driver older than the renderer needs.


IF THE GAME CRASHES
-------------------

The app opens no console window, so a crash is not visible in one. If the
process dies on a fault or an unhandled error it writes:

    error.log

into this folder, next to the save. The file holds the reason, the fault
address, the N64 thread call chain and the last lines of the app's own log (the
`[boot]`, `[bank]` and `[overlays]` lines). Attach the whole file to a bug report
on this project's issue tracker, not the original recompilation project's:

    (issue tracker address: to be set before the first release)

`error.log` is overwritten by the next crash. Closing the window normally writes
nothing. A developer can also make the app crash on demand to check the file:
set `OGRE_CRASH_TEST=segv` in the environment before launching.

If the game does not start at all, the `[boot]` lines name the resolved ROM,
the renderer and the folder where saves are written. On macOS they are not
printed anywhere; run the executable inside the bundle from a Terminal
(`OB64 Another World.app/Contents/MacOS/ob64-anotherworld`) to see them. On Windows and
Linux, run the app from a terminal.

Troubleshooting, build instructions from source, and the full list of
environment options are in this project's repository:

    https://github.com/FiraVT/ob64-Another-World

See docs/guides/app-build.md there for the diagnostics toolkit.


LEGAL
-----

Ogre Battle 64 (c) Quest / Nintendo. This is a fan preservation and
interoperability project; it ships no ROM, no extracted assets and no game
data. Supply your own legally obtained dump.

The executable includes SDL2 (https://libsdl.org), Copyright (C) 1997-2025
Sam Lantinga, under the zlib license — see SDL2-LICENSE.txt in this folder.

