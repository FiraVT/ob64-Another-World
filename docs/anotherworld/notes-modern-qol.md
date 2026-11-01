# Interface, input and save features (modernization items 5, 7, 8, 14, 15, 17, 18)

The developer ruled out new art, so these are built from the app's own panels,
input and files rather than from new textures for the game.

## Settings tab

| row | values | what it does | code |
|---|---|---|---|
| TURBO A | OFF / ON | a held A is reported pressed and released in turns, 60 ms each | `get_input`, `app/src/sdl_platform.cpp` |
| UI SCALE | NORMAL / LARGE | the launcher and Esc panel rows are drawn half again as large | `measure_panel`, `app/src/ui.cpp` |
| HIGH CONTRAST | NORMAL / HIGH | white text on black, a yellow highlight and a blue selection bar in the panels | `apply_contrast`, `app/src/ui.cpp` |
| PIXEL FILTER | SMOOTH / SHARP | how RT64 scales the game picture to the window (anti-aliased pixel scaling or nearest) | `set_application_user_config`, `app/src/renderer.cpp` |

All four are saved in `settings.cfg` (`turbo_a`, `ui_scale`, `contrast`,
`pixel_filter`) through one generic backend (`Choice` in
`app/src/settings.hpp`).

- **UI SCALE.** LARGE falls back to the normal size on any tab that would then
  be taller than three quarters of the window. At 720p that is SETTINGS and
  CONTROLS. The tab bar and the hint line always stay at the normal size.
- **PIXEL FILTER.** The filter is applied whenever the renderer applies a
  graphics config. It is not confirmed that changing the row mid-game reaches
  RT64 at once.

## Mouse in menus (item 7)

With MOUSE ENABLED on, the mouse presses N64 buttons on every screen:

| mouse | N64 button |
|---|---|
| left click | A |
| right click | B |
| middle click | Start |
| wheel up / down | D-pad up / down |

Each press is held for 80 ms so the game's poll sees it (`handle_menu_mouse`,
`app/src/sdl_platform.cpp`). On the world map and the mission field the existing
cursors still follow the pointer, and a left click is the same A they already
press. A click that only focuses the window also presses A.

## Button legend (item 8)

F1 shows and hides a small panel in the game window's upper-left corner. For
each N64 button the game's prompts name (A, B, C-UP, ...), it lists the bound
key and pad button, and it updates as soon as a binding changes. Like the speed
badge, it is a separate window over the game: it is not drawn in fullscreen,
and the pointer passes it to the game window (`app/features/button_legend.cpp`). F1
is reserved, so it cannot be bound to a game button.

## Saves (items 14 and 15)

The MAIN tab has a SAVES section (`app/features/save_backup.cpp`):

- **Back up save** copies the cartridge save to
  `<config>/save-backups/<YYYYMMDD-HHMMSS>.bin`. It is safe while the game runs.
  It copies what the game last wrote, so save in game first.
- **Import save** (start screen only) converts the first usable file in
  `<config>/save-import/`. If that folder has none, it uses the port's own
  Controller Pak `<config>/saves/<game id>.mpk`, the file the game's Controller
  Pak screen writes. Accepted formats:
  - a battery image
  - an emulator save
  - a DexDrive `.N64`
  - a bare pak

  Pak notes become battery slots (`convert_save_file`, `app/src/save_import.cpp`).
- **Restore `<name>`** (start screen only) lists the four newest backups and
  puts one back.

Import and restore first keep the current save as
`<stamp>-before-import.bin` or `<stamp>-before-restore.bin`. Both are offered
only before the game starts, because the runtime reads the save once and then
writes its in-memory copy back after every in-game save.

This is the PC stand-in for the game's Controller Pak copy/backup flow, which
stays unimplemented. A real quick save (a snapshot taken anywhere and loaded
anywhere) is not offered. The debug console's checkpoints restore memory but
not the game threads' native state, so loading one on a different screen can
break the game.

## Roster tab (item 17)

ROSTER (Esc panel only) lists every character with name, class and level. It
sorts by level (highest first), by class, or by name, and shows ten per page
(`army_roster::read`, `app/features/army_roster.cpp`). It reads the live roster each time the
panel opens.

## Not verified

None of this has been tried in play. Launcher screenshots confirmed the layout
of the SETTINGS and MAIN tabs, at NORMAL and LARGE. Things to check in play:

- turbo A in dialogue
- the right and middle clicks and the wheel in menus
- the F1 legend over the game
- a backup made in game, then restored from the start screen
- importing a `.mpk`
- the ROSTER tab's sort and pages
- PIXEL FILTER SHARP at a large window size

