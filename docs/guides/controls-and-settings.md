# Controls, settings and the in-game overlay

Command examples are bash. On Windows, run them from Git Bash. A path such as
`/tmp/x` that is passed to the app itself (an `OGRE_*` variable or a console
command) must be a path the Windows executable can open, for example
`$TEMP/x` or a folder in the repository.

The player's keyboard and gamepad bindings are data (`app/src/input_map.cpp`),
edited from the start screen's **CONTROLS** tab and from the in-game `ESC`
overlay, and saved as plain text at `<config>/controls.cfg`. The input callbacks
in `sdl_platform.cpp` read the map on every poll.

## The map

One row per N64 button, in libultra's bit order, with a keyboard scancode and one
gamepad source (a button, a right-stick direction, or none). The defaults are the
mapping the port used before the tab existed:

| N64 | key | gamepad |
|---|---|---|
| A | `X` | A |
| B | `Z` | B |
| Z | `C` | L-Stick |
| START | `Return` | Start |
| L | `Q` | LB |
| R | `E` | RB |
| D-pad | arrows | D-pad |
| C-UP / C-DOWN | `I` / `K` | X / Y |
| C-LEFT / C-RIGHT | `J` / `L` | (none; the right stick) |

The left stick is always the N64 analog stick, and the right stick presses the C
buttons on top of whatever the map binds to them. Both are fixed, and the
CONTROLS tab says so. `ESC` is reserved for the overlay and cannot be bound.
`=` and `-` step the game speed while the game runs, and the pad's **R3** cycles
it (see "Game speed" below); none of the three is part of this map. Binding one of
them to an N64 button would still work, but the press would change the speed as
well.

The file is line-oriented (`<n64 tag>.<key|pad> = <value>`); `-` and `none` are
unbound, and a missing file leaves the defaults. Key names are SDL's
(`SDL_GetScancodeName`), so the file is readable and survives SDL updates.

## Which N64 controller a pad drives

The game reads N64 controller 1 only, so the first connected pad fills
`Platform::controllers[0]` and the keyboard is OR'd into the same slot: both
drive player 1 at the same time. A second pad fills slot 1 (N64 controller 2) and
so on, and this game never polls those. Before session 118 a pad filled slots
1..3, so it worked in the launcher and the overlay and did nothing in game
(issue #13). SDL queues an `SDL_CONTROLLERDEVICEADDED` event for every pad that
was already connected at startup, so the app enumerates the connected set once
and skips an ADDED event for a device it already holds; the enumeration prints
one `[input] controller <n>: <name>` line per filled slot.

## The CONTROLS tab

```
A          KEY X            PAD A              - Selects the highlighted object.
B          KEY Z            PAD B              - Cancels a selection.
…
Reset bindings to defaults   Restore the default keyboard and gamepad map.
```

`SPACE` on a row arms a capture; the next keydown fills the keyboard slot and a
pad button fills the pad slot. `BACKSPACE` clears the keyboard slot, `DELETE`
clears the pad slot, `ESC` cancels. `TAB` / `SHIFT+TAB` (or a mouse click on the
tab bar) switch tabs. The second column's action text comes from the game's
controls description.

The line under the bindings is the panel's own pad controls, and is not a
binding:

```
MENU PAD NAVIGATION: D-PAD MOVE, A SELECT, SELECT MENU, LB/RB SWITCH TAB
```

`D-PAD` moves the selection and steps a radio row or slider, `A` and `START`
activate, `LB`/`RB` switch tab, and the pad's SELECT button is the overlay's
Back/toggle (it does nothing in the launcher). The line is one section row, and a
section row's title is wrapped to the whole panel width (`layout_rows` in
`app/src/ui.cpp`) because a section has no description to its right. `app/src/ui.cpp` maps the SDL button to a `Panel::PadCommand`
(`Panel::pad_command_for_button`), so the launcher and the overlay act on one
table. The events are `SDL_CONTROLLERBUTTONDOWN`, which is why a pad press
cannot auto-repeat; a rebind capture owns the pad while it is armed.

`SDL_CONTROLLER_BUTTON_BACK` (the pad's SELECT) opens and closes the in-game overlay, and only while
the live map binds nothing to it (`pad_button_is_bound`), so opening the menu
cannot also press an N64 button in the game. While the overlay is visible the
game already receives an idle pad (`get_input` in `app/src/sdl_platform.cpp`), so
navigating the panel does not move the game's own cursor. The pad's **R3** is the
game speed's own button and is likewise ignored while the overlay is visible; see
"Game speed" below.

## Game speed (the SETTINGS tab)

GAME SPEED is the runtime's emulated-clock multiplier, the same quantity
`OGRE_SPEED` sets: `1` is real time, and `n` runs the emulated CPU counter and
the VI retrace schedule `n` times faster, so a timed sequence completes in `1/n`
of the wall time. The tab offers **1, 2, 4 and 8**; 8 was dropped when the row was
added because the game's own cursor aiming is fast at that speed, and it is
offered again at the developer's request (session 137). 6 stays out.

```
GAME SPEED   [ ] 1 [ ] 2 [ ] 4 [x] 8
```

`LEFT`/`RIGHT` (or `SPACE`) step the group, and a mouse click on a marker sets
that speed. The value is applied immediately, so it can be changed from the
in-game `ESC` overlay while the game runs; the handoff's timing evidence shows
the guest clock stays continuous across a change. It is saved as plain text at
`<config>/settings.cfg` (`game_speed = <n>`, written by `app/src/settings.cpp`),
read at startup by `load_settings()` before the game boots. `OGRE_SPEED` overrides
the file for one run and does not write it; a file that names a value the tab does
not offer is loaded as the closest offered one, so a stale `6` becomes `4`.

The runtime's setter is `ultramodern::set_speed_multiplier()` (`timer.cpp`). The
clock is piecewise linear: `speed_start_us`/`speed_start_ticks` anchor where the
current multiplier took effect, and the three values are published under a
seqlock, because the game, VI and timer threads read them while the app's main
thread changes them.

**The speed hotkeys.** `=` steps the multiplier up and `-` steps it down (the
keypad `+` and `-` too), through the same four values and stopping at the ends:
1 → 2 → 4 → 8 and back, with no press doing anything at 8 while stepping up or at
1 while stepping down. They are read from the SDL event in `pump_sdl_events`
(`app/src/sdl_platform.cpp`), so they work while the game has the keyboard, and
they are ignored while the `ESC` panel is up, because the panel owns the keyboard
and has its own GAME SPEED row. A run whose starting value is not one the row
offers steps from that value, so `OGRE_SPEED=6` goes to 8 up and 4 down.
`OGRE_SPEED_KEYS` exercises them from a script.

**The pad's speed button.** Pressing **R3** (the right stick's click) cycles the
same table with one button, so it wraps: 1 → 2 → 4 → 8 → 1. R3 is what the
default pad map leaves free (`Z` is L3 and `BACK` opens the `ESC` panel), so the
press does not also press an N64 button; a player who binds R3 to an N64 button
still gets that button, because this handler only observes the event. It is read
from `SDL_CONTROLLERBUTTONDOWN` in `pump_sdl_events`, on the same rule as the
keys: ignored while the `ESC` panel is open and before the game window exists.
A run whose starting value is not in the table joins at the first offered value
above it, so `OGRE_SPEED=6` cycles to 8 and then wraps to 1. `OGRE_SPEED_PAD`
exercises it from a script.

**The speed badge.** `app/src/speed_badge.cpp` shows `2x`, `4x` and so on in the
game window's lower-right corner for two seconds whenever the multiplier changes,
from the panel, the hotkeys or the row's own value at startup being replaced.
"1x" is drawn too, so stepping back down to normal speed is visible. The game's
picture is drawn by RT64 straight to its own swap chain and the renderer has no
UI pass, so the badge is a small borderless always-on-top SDL window placed over
the corner of the game window, drawn with the same bitmap font as the panel and
0.85 uniform window opacity (SDL2 has no per-pixel window alpha). No window is
created until the first change. Two limits, both from that design: the badge is
not drawn while the WINDOW row is `fullscreen`, because fullscreen is a display
space of its own, and it hides itself when the pointer moves over it, because
SDL2's window hit test has no click-through result and a click there would
otherwise be swallowed instead of reaching the game.


## Widescreen (the SETTINGS tab)

WIDESCREEN puts RT64 into its **Expand** aspect ratio, which widens the field of
view instead of stretching the picture. `rt64_workload_queue.cpp:134-211` derives
an `aspectRatioScale` from the output window, and `ProjectionProcessor::
processScene` (`rt64_projection_processor.cpp:101`) counter-scales the game's
projection matrices by `1/scale` for the projections that cover the screen and
are wider than tall, so the 3D field keeps its proportions and gains width. The
setting reaches RT64 through the runtime: `ultramodern::renderer::
set_graphics_config` flags an `UpdateConfigAction`, the renderer thread picks it
up in `events.cpp`, and `RT64Renderer::update_config` (`app/src/renderer.cpp:917`)
forwards it to `app_->updateUserConfig`.

WIDESCREEN is a toggle, and its `ON` state is scoped by a scene gate rather than
by the aspect ratio, because the game's 2D screens are authored for 4:3:

```
WIDESCREEN   [ ] OFF [x] ON
```

* `OFF` (the default) never changes the aspect ratio.
* `ON` turns Expand on only while the dispatcher's active scene (`D_800E810E`) is
  `0x03`, the mission. The setting exists for this scene: the 3D field gains
  width, and the game's 2D blits (the boot's publisher stills, njpeg story
  backgrounds) keep their 4:3 proportions and are pillarboxed in the window.

Earlier builds offered three options, `OFF` / `MISSIONS` / `ALWAYS`. Both
widescreen options opened the same 16:9 window and pillarboxed the same 4:3
scenes, so the split is gone (developer, session 105); the retired `missions`,
`always`, `1` and `2` spellings in the file and in `OGRE_WIDESCREEN` are read as
`on`.

`app/src/widescreen.cpp` is the only writer of `ar_option`, and it is called
once per frame from `update_gfx` beside `poll_scene`, because the active scene can
change on any frame. It reads the live `GraphicsConfig` and writes only when the
wanted value differs, so a steady scene costs one comparison per frame.

`LEFT`/`RIGHT` (or `SPACE`) step the group, and a mouse click on a marker sets
that option, exactly like GAME SPEED; the value is saved at
`<config>/settings.cfg` (`widescreen = off|on`).

### The window's shape follows the toggle, not the scene

The window is sized once, from the toggle, and never changes shape while the game
runs. `create_window` asks `widescreen_initial_window_size` (`sdl_platform.cpp`)
and then `widescreen_fit_window` re-derives the width from the height the window
actually got, because the window manager can hand back a different height than
the one requested.

| toggle | window | a 4:3 scene | the mission |
|---|---|---|---|
| `OFF` | 4:3 | fills the window, no bars | 4:3, no bars |
| `ON` | 16:9 | **pillarboxed: black bars on the sides** | fills the width |

`widescreen_update` returns whether the **toggle** changed, and only that makes the
app re-fit the window (`main.cpp`). A scene transition must not resize the
window: the first version did exactly that, and the developer rejected it —
*"the window changes its size while the game is running"*. Changing the toggle from
the launcher or the `ESC` overlay re-fits the window once, which is the one case
where the shape is expected to change. A manual resize or a double-click to fill
the screen is left alone until the toggle changes.

### Observations

Observations from the verification runs (2026-09-25, `history-archive:docs/history/handoffs/HANDOFF-2026-09-25-session102.md`):

* Entering and leaving scene `0x03` toggles Expand and back. A mission that
  interleaves the story scenes `0x02`/`0x0D` toggles on every visit, and the run
  continues normally (`[widescreen]` lines in the log).
* With `OFF` the window opens 953x715 (4:3 at a 715 window height) and the
  4:3 picture reaches both edges — no pillarbox, which was the developer's first
  report about the old fixed 1280x720 window.
* With `ON` the window opens 1271x715 (16:9) and stays; the boot,
  the title, the Load Game book and the story cutscenes are pillarboxed inside
  it, which is what the developer asked for.
* The mission field and its projected 2D HUD (the `NOTE` tooltip, the dialogue
  box, the battle unit panels) keep their proportions and the field fills the
  width.
* The `MISSION` banner row clips at both screen edges in 4:3 as well: it is the
  game's own scrolling marquee and not an Expand artifact.
* Proofs: `native-widescreen-launcher.png` (the row) and
  `native-widescreen-overlay.png` (the same row in the `ESC` overlay),
  `native-widescreen-pillarbox.png` and `native-widescreen-43-window.png` (the
  Load Game book in `ON` and `OFF` — the identical screen with and without
  the side bars), `native-widescreen-mission-expand.png` and
  `native-widescreen-mission-43.png` (a battle in the mission, in `ON` and
  `OFF`; not the same battle frame, but the same area and camera).

```bash
# the row, as a PPM (a ROM is auto-found, so force the launcher)
OGRE_LAUNCHER=1 OGRE_LAUNCHER_TAB=settings OGRE_LAUNCHER_SHOT=/tmp/ws.ppm \
  ./build-app/ogrebattle64

# the scene gate: Expand exactly while scene 0x03 runs, and the window unchanged
OGRE_WIDESCREEN=on OGRE_SCENE_LOG=1 OGRE_EXIT_AFTER_MS=30000 \
  ./build-app/ogrebattle64 2>&1 | grep -E '\[scene\].*0x0003|\[widescreen\]'
```

## Display: window, and the experimental resolution and MSAA rows (the SETTINGS tab)

Shipped:

```
DISPLAY
WIDESCREEN   [x] OFF [ ] ON
WINDOW       [x] WINDOWED [ ] FULLSCREEN
```

`WINDOW` is `GraphicsConfig::wm_option`; the renderer calls its backend's
`setFullScreen`, so the window is not recreated and the swap chain follows the
display mode. It is saved as `window = windowed|fullscreen` and
`OGRE_WINDOW=windowed|fullscreen` overrides the file for one run.

**Fullscreen is the renderer's geometry, and the port must not touch it.** RT64's
fullscreen is a raw `SetWindowPos` to the monitor rect on Windows and
`[NSWindow toggleFullScreen:]` on macOS, and neither goes through SDL, so SDL's
idea of the window's size does not follow. The WIDESCREEN row derives the
window's shape from its height, so it must not run while fullscreen:
`widescreen_fit_window` returns early when the live config asks for fullscreen and
logs `[widescreen] fullscreen: the renderer owns the window's shape` once, and
`widescreen_update` no longer reports a mode change on its first call (the window
was created at that mode's shape already). A run that started fullscreen used to
re-fit the window on its first frame — invisible on macOS, where SDL ignores a
resize of a window it knows is fullscreen, and a 4:3 window in the display's
top-left corner on Windows, where SDL did not perform the transition and applied
the resize. Leaving fullscreen is the other direction: `RT64Renderer::update_config`
calls `widescreen_request_refit()` after it has restored the window, and the next
frame re-derives the shape, so a WIDESCREEN change made while fullscreen is
honoured on the way out.

**RESOLUTION and MSAA are experimental and off.** Both are implemented and both
reach RT64, and a player run with them produced visual artifacts (developer,
session 121), so they are hidden and inert until the artifacts are handled. With
`OGRE_DISPLAY_EXPERIMENTS=1` they appear under WINDOW and are applied:

```
DISPLAY
WIDESCREEN   [x] OFF [ ] ON
RESOLUTION   [x] NATIVE [ ] 2X [ ] AUTO
MSAA         [x] OFF [ ] 2X [ ] 4X [ ] 8X
WINDOW       [x] WINDOWED [ ] FULLSCREEN
```

| row | options | what it changes |
|---|---|---|
| `RESOLUTION` | `NATIVE` / `2X` / `AUTO` | `res_option`. `NATIVE` is RT64's `Original`, the game's own 320x240 target, and what the port drew before the row existed. `2X` is `Original2x`, which `set_application_user_config` (`app/src/renderer.cpp`) turns into RT64's `Manual` at `resolutionMultiplier 2.0`. `AUTO` is `WindowIntegerScale`, the largest integer multiple of the native resolution that fits the window |
| `MSAA` | `OFF` / `2X` / `4X` / `8X` | `msaa_option`, RT64's multisample antialiasing. A change also rebuilds the render targets (`updateMultisampling`) |

The gate is `OGRE_DISPLAY_EXPERIMENTS` (`1`/`on`/`true`/`yes`; `0`/`off`/`false`/
`no` is off). A set `OGRE_RESOLUTION` or `OGRE_MSAA` also enables it, so the debug
spellings below work on their own. While it is off:

- the rows are absent from the tab, in the launcher and in the overlay
  (`app/src/ui.cpp`, `Panel::build_rows`);
- the two values are not applied at all, whatever the file or the environment
  says (`applied_resolution`/`applied_antialias` in `app/src/settings.cpp`), and
  the boot prints
  `resolution 2x and msaa 4x are experimental and not applied; set OGRE_DISPLAY_EXPERIMENTS=1 to enable them`;
- a value already in `settings.cfg` is kept, not overwritten: the file records
  what was configured, and the flag decides whether it is used.

With the flag on they are saved as `resolution = native|2x|auto` and
`msaa = off|2x|4x|8x`, and `OGRE_RESOLUTION`/`OGRE_MSAA` override the file for one
run without writing it.

All three rows apply through `RT64Renderer::update_config`
(`app/src/renderer.cpp:917`) on the next frame, so they can be changed from the
in-game `ESC` overlay. `settings.cpp` and `widescreen.cpp` are two writers of one
`GraphicsConfig`: each starts from the live config and changes only its own
fields, so a widescreen scene transition does not reset the resolution and a
resolution change does not reset the aspect ratio. The applied values are printed
once per change:

```
[renderer] config: resolution 2x (mult 2.00), msaa 4x, window fullscreen
```

`GRAPHICS API` is not a row. It is fixed at renderer creation, so it stays an
environment variable (`OGRE_GRAPHICS_API`). `REFRESH RATE` and
`HIGH-PRECISION FRAMEBUFFER` are also in `GraphicsConfig` and remain at their
defaults.

Proofs: `native-launcher-settings.png` (shipped rows),
`native-launcher-settings-experiments.png` (`OGRE_DISPLAY_EXPERIMENTS=1`) and
`native-overlay-settings.png` (the shipped rows, with the pad script below having
set WINDOW to FULLSCREEN).

## Sounds and volume (the SETTINGS tab)
```
AUDIO
SOUNDS   [x] ON [ ] OFF
VOLUME   ----------| 100%
```

SOUNDS is the audio output's mute and VOLUME is its gain. `LEFT`/`RIGHT` (or
`SPACE`) step both: SOUNDS wraps between `ON` and `OFF` like the other radio
rows, and VOLUME moves the handle 5% per press and clamps at the ends rather than
wrapping. A mouse click sets a radio marker or the slider cell it lands on (11
cells, 10% each).

The bar is `kVolumeCells` = 11 characters with the handle on the cell nearest the
percent, so 0 renders `|----------`, 50 renders `-----|-----` and 100 renders
`----------|`.

The gain is applied in `queue_audio_samples` (`app/src/sdl_platform.cpp`): 100
queues the game's buffer unchanged, a lower gain scales a copy into a scratch
buffer, and 0 queues silence. The frame count is the same at every gain. That is
deliberate: `ultramodern::audio_dma_busy()` paces the game's audio generation
from the queued depth (`get_frames_remaining`), so a mute that queued nothing
would report the AI idle and make the game overproduce. Measured with a probe
(session 106, reverted): at gain 50 `peak_out = peak_in/2`, and at gain 0
`peak_out = 0` with 1104 samples still queued per buffer.

Both values are saved at `<config>/settings.cfg` (`sounds = off|on`,
`volume = <0..100>`). `OGRE_SOUNDS` and `OGRE_VOLUME` override the file for one
run without writing it, the same rule as `OGRE_SPEED`. A file written before the
rows existed has neither key and loads as `ON` / `100`, which is what the app did
before them. The browser build does not read `settings.cfg` and has no settings
UI, so `app/src/web_platform.cpp` still plays at full volume.

Proofs: `native-launcher-settings.png`, `native-launcher-settings-experiments.png`
and `native-overlay-settings.png` (the tab in the launcher, the same tab with the
experimental rows enabled, and in the `ESC` overlay with the pad script above
having set WINDOW to FULLSCREEN).

## Mouse enabled (the SETTINGS tab)
```
INPUT
MOUSE ENABLED   [x] OFF [ ] ON
```

MOUSE ENABLED is the switch for the port's mouse support, and `OFF` is the
default. `ON` lets the pointer drive the two screen cursors the port implements:
the world map's (scene `0x05`) and the mission field's (scene `0x03`), with the
left click that acts as the game's `A` and the edge push that scrolls either
view. `OFF` is what every build before the row did, and the operating system
pointer is then left visible over the game window, because the game is played
with the keyboard or a pad.

The row is published to `ogre_map_cursor_set_enabled` and
`ogre_mission_cursor_set_enabled` on every change, so a change under the `ESC`
overlay takes effect on the next frame. The value is saved at
`<config>/settings.cfg` as `mouse = off|on`; `OGRE_MOUSE=off|on` overrides the
file for one run without writing it, and `OGRE_MAP_CURSOR` /
`OGRE_MISSION_CURSOR` still force one screen on or off whatever the row says.
A file written before the row existed has no key and loads as `OFF`.

Proofs: `docs/proofs/native-launcher-mouse-off.png` and
`-mouse-on.png` (the SETTINGS tab with the row off, and after the scripted key
run turned it on).

## The in-game overlay

`ESC` during play opens a borderless, always-on-top SDL window placed over the
game window (`app/src/overlay.cpp`). It draws the same `ui::Panel` with the same
bitmap font, so the two screens look identical; the tab bar shows MODS greyed and
MAIN, CONTROLS, SETTINGS and DEBUG active. MAIN stays active because it carries
the EXIT GAME row, which is the overlay's own way to end the run; MODS is the one
tab a running game cannot use, because the runtime loads mods before it boots.
`ESC` closes it and returns the keyboard to the game.

EXIT GAME records a request and `update_gfx` (`app/src/main.cpp`) polls it next
to the window-close flag, so both exit paths share one teardown, its dumps and
`ultramodern::quit`. Both are checked against the game's own frame counter
(`D_800AEFA4`, read by `overlay_frames_produced`): an exit before the game has
produced frames leaves the process with `_Exit(0)` instead of running the
runtime's teardown, because `ultramodern::quit` while the game thread is inside
`ultramodern::preinit`, or while RT64 is still building its renderer, leaves
`recomp::start` blocked in `game_thread.join` or throws out of the shutdown. That
is the same reason the `OGRE_EXIT_AFTER_MS` path leaves through `_Exit`.

While it is open, `get_input` reports an idle controller (`buttons=0`, stick 0),
so the game's own menus do not see the keys used to navigate the panel. The game
keeps running: the overlay does not pause emulation.

The window is created on the first `ESC`, sized and positioned from
`SDL_GetWindowPosition`/`SDL_GetWindowSize` of the game window and re-synced each
frame, and given `SDL_SetWindowOpacity` (default 0.90) because SDL has no
per-pixel alpha for a normal window. It is not covered on macOS fullscreen: a
fullscreen window lives in its own Space, which another window cannot overlay.
No RT64 change was needed.

The overlay's window is a second SDL window for the whole run, which breaks the
game window's close button unless the app handles the close event itself. SDL
synthesises `SDL_QUIT` from a window's `SDL_WINDOWEVENT_CLOSE` only while that
window is the last one in its global window list (`SDL_windowevents.c`), and the
overlay is hidden and shown again rather than destroyed, so it stays in that list.
`pump_sdl_events` (`app/src/sdl_platform.cpp`) therefore quits on a
`SDL_WINDOWEVENT_CLOSE` whose `windowID` is the game window's, and ignores a close
request for the overlay window, where `ESC` is the dismiss.

## Verifying it without a human

```bash
# every launcher tab, as a PPM
for tab in main mods controls settings debug; do
  OGRE_PREF_DIR=/tmp/ui OGRE_LAUNCHER=1 OGRE_LAUNCHER_TAB=$tab \
    OGRE_LAUNCHER_SHOT=/tmp/ui-$tab.ppm ./build-app/ogrebattle64
done

# rebind A to P through the launcher's real key handler
OGRE_PREF_DIR=/tmp/ui OGRE_LAUNCHER=1 OGRE_LAUNCHER_KEYS="Tab,Tab,Tab,Space,p" \
  OGRE_LAUNCHER_SHOT=/tmp/ui-capture.ppm ./build-app/ogrebattle64
grep '^a.key' /tmp/ui/controls.cfg      # a.key = P

# the overlay's real ESC path, and its own rebind
OGRE_PREF_DIR=/tmp/ov OGRE_ROM=assets/ogre64.z64 OGRE_OVERLAY_AT_MS=4000 \
  OGRE_OVERLAY_KEYS="Space,p" OGRE_CAPTURE_OVERLAY=/tmp/overlay.ppm \
  OGRE_EXIT_AFTER_MS=8000 ./build-app/ogrebattle64

# change GAME SPEED from 1 to 4 in the overlay while the game runs
OGRE_PREF_DIR=/tmp/gs OGRE_ROM=assets/ogre64.z64 OGRE_OVERLAY_AT_MS=2500 \
  OGRE_OVERLAY_TAB=settings OGRE_OVERLAY_KEYS="Right,Right" \
  OGRE_CAPTURE_OVERLAY=/tmp/gs.ppm OGRE_OVERLAY_SHOT_MS=1200 \
  OGRE_EXIT_AFTER_MS=7000 ./build-app/ogrebattle64
grep '^game_speed' /tmp/gs/settings.cfg     # game_speed = 4

# the experimental DISPLAY rows, from keys and then from a pad, in the launcher
# (without OGRE_DISPLAY_EXPERIMENTS the two rows are absent and inert)
OGRE_PREF_DIR=/tmp/dp OGRE_DISPLAY_EXPERIMENTS=1 OGRE_LAUNCHER=1 \
  OGRE_LAUNCHER_TAB=settings \
  OGRE_LAUNCHER_KEYS="Down,Down,Right,Down,Right,Down,Right" \
  OGRE_LAUNCHER_SHOT=/tmp/dp.ppm ./build-app/ogrebattle64
grep -E '^(resolution|msaa|window)' /tmp/dp/settings.cfg
# resolution = 2x / msaa = 2x / window = fullscreen

# a pad opens the overlay and changes WINDOW; the game sees an idle pad
OGRE_PREF_DIR=/tmp/pad OGRE_ROM=assets/ogre64.z64 OGRE_OVERLAY_TAB=settings \
  OGRE_OVERLAY_PAD="back,ddown,ddown,dright" OGRE_CAPTURE_OVERLAY=/tmp/pad.ppm \
  OGRE_OVERLAY_SHOT_MS=2500 OGRE_EXIT_AFTER_MS=14000 ./build-app/ogrebattle64
# must print "[overlay] shown", then a "[renderer] config:" line with window
# fullscreen, then "[input] overlay open: game input suppressed"

# the overlay's EXIT GAME (MAIN is the tab the panel opens on with OGRE_OVERLAY)
OGRE_PREF_DIR=/tmp/ex OGRE_ROM=assets/ogre64.z64 OGRE_OVERLAY=1 \
  OGRE_OVERLAY_TAB=main OGRE_OVERLAY_KEYS="Space" \
  OGRE_EXIT_AFTER_MS=60000 ./build-dist/ogrebattle64
# must print "[overlay] EXIT GAME" and exit 0 with no error.log

# the game window's close button, after the overlay has been opened and closed
OGRE_PREF_DIR=/tmp/cl OGRE_ROM=assets/ogre64.z64 OGRE_OVERLAY=1 \
  OGRE_OVERLAY_KEYS="Escape" OGRE_WINDOW_CLOSE_AT_MS=9000 \
  OGRE_EXIT_AFTER_MS=60000 ./build-dist/ogrebattle64
# must print "[SDL] game window close requested" and exit 0 with no error.log
```

Convert a PPM with `sips -s format png <in> --out <out>` on macOS.
