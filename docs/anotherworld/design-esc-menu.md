# The Esc menu

Status: rebuilt in RmlUi on 2026-10-29 (developer request); not yet seen in
play.

## Design

The menu's model is still `ui::Panel` (`app/src/ui.{hpp,cpp}`) on the main
thread, with the keyboard, pad, rebinding capture and setting logic it always
had. Only the drawing changed:

- `app/src/overlay.cpp` builds a snapshot of the panel (tabs, rows, left and
  right text, radio options, the volume slider's cells) and publishes it to
  `app/features/options_view.cpp` when it changes.
- `options_view.cpp` draws the snapshot with RmlUi inside the game window
  (`app/ui/options.rml`, `options.rcss`, the Organize view's parchment style),
  from `rml_ui.cpp`'s render hook. Mouse clicks on tabs, rows, radio options and
  slider cells come back as commands that the overlay applies on the main thread
  (`apply_menu_commands`).
- The menu is inside the game window, so it no longer floats over other
  programs. The old separate SDL window is kept as a fallback when RmlUi is not
  running (`OGRE_RMLUI=0`) or when `OGRE_RMLUI_MENU=0` asks for it; that window
  is now owned by the game window instead of always-on-top.
- The launcher's start screen still uses the SDL panel, because the RmlUi pass
  runs inside RT64's present and RT64 is not drawing before a game starts.
- **Pause.** The first build parked every game thread at its next function
  entry (`ultramodern::checkpoint_pause_begin`), as the SDL panel does. In play
  the in-window menu froze: no mouse press reached RmlUi until the menu closed
  (the run log printed the presses after `[overlay] hidden`, landing on
  `#root#main`), and the arrow keys did not move the highlight (developer). A
  thread parked mid-frame leaves that frame's RT64 workload unfinished, and the
  present thread waits for it (`waitForWorkloadId` in `rt64_present_queue.cpp`),
  so nothing presents and the menu, drawn inside the present, cannot update.
  The in-window menu therefore pauses at a frame boundary instead:
  `tools/director_hook.py` inserts `ogre_frame_gate()` into the frame pump's swap
  wrapper `func_8008B110`, after its `osViSwapBuffer` call (0x8008B120), and the
  gate waits while the menu is open. The SDL panel keeps the thread park.

## Tabs

MAIN, MODS, CONTROLS, GRAPHICS, AUDIO, GAMEPLAY, ROSTER, DEBUG (developer,
2026-10-29: SETTINGS split into GRAPHICS and AUDIO).

- CONTROLS: MOUSE ENABLED, then the bindings.
- GRAPHICS: WINDOW (window mode, PAUSE WHEN UNFOCUSED), PICTURE (widescreen,
  frame rate, the experimental resolution and MSAA, pixel filter), MENUS (UI
  scale, high contrast).
- AUDIO: sound on/off, volume, music, effects.

GAMEPLAY gathers the port's gameplay switches, which were environment variables
only. Each is a `Choice` in `settings.cfg`; the environment variable still wins
for a developer run (`feature_enabled` in `settings.cpp`).

| row | key | default | code | env override |
|---|---|---|---|---|
| SKIP INTRO | `skip_intro` | OFF | the boot intro's update (scene 9, `func_80177DCC`) writes the pending scene `0x8004` (the title) at its epilogue `0x80177DFC` instead of waiting to write `0x800A` (the logos); hook by `tools/director_hook.py`, function `ogre_skip_intro` in `bank_overlays.cpp`. Measured: scene 9 at 2501 ms, title at 2769 ms. The title then waits for Start before its menu: in states 2..16 of its state word `0x801BA70C`, with flag `0x801BA564` clear, `func_8019BD0C` reads the input word `0x800C4B28` and START (`0x1000`) moves it to state 14. A second hook at that function's entry (`ogre_skip_title_press`) sets the START bit once per process, the first time the state is 2..13. A first version poked the scene id like `OGRE_SCENE=title` and missed the boot window in play | `OGRE_SCENE` wins |
| GAME SPEED | (moved from SETTINGS) | | | |
| TURBO A | (moved from SETTINGS) | | | |
| WAR DIRECTOR | `war_director` | ON | garrison scaling and spawns | `OGRE_DIRECTOR_GARRISON`, `OGRE_DIRECTOR_SPAWN` |
| LOOT UPGRADES | `loot_upgrades` | ON | drop table upgrade | `OGRE_DIRECTOR_LOOT` |
| TRAINING CATCH-UP | `training_catch_up` | ON | catch-up training level | `OGRE_TRAINING` |
| SHARED EXP | `shared_exp` | ON | `exp_share::tick` | `OGRE_SHARED_EXP` |
| CLASS ALIGNMENT | `class_alignment` | IGNORED | `ogre_alignment_requirement` | `OGRE_CLASS_ALIGNMENT` |
| LARGE UNITS | (moved from DEBUG) | | | |

`war_director.cpp`, `director_spawn.cpp` and `bank_overlays.cpp` read the
switches through `app/features/feature_flags.hpp`, which once also served the
browser build (removed 2026-11-01).

GRAPHICS has PAUSE WHEN UNFOCUSED (`pause_unfocused`, default OFF): the game
window losing focus opens the Esc menu. The window row's FULLSCREEN is now named
BORDERLESS FULLSCREEN, because RT64's full screen is a borderless `WS_POPUP`
window over the monitor; `settings.cfg` accepts the old `fullscreen` value.

## Checklist status (developer's game-settings checklist, 2026-10-29)

| item | status |
|---|---|
| settings before a game | yes (start screen, SDL panel) |
| mouse control | yes |
| pad d-pad, shoulder tabs | yes; the left stick is not mapped to the menu |
| windowed / borderless | yes; exclusive fullscreen not offered (RT64 has none) |
| resolutions | internal scale only (NATIVE / 2X / AUTO, experimental) |
| aspect ratios | widescreen on/off only |
| quality preset, V-Sync, FPS limit, gamma | not yet |
| master and per-channel volume | yes (sound, volume, music, effects) |
| rebinding, reset | yes; sequential rebind and stick invert not yet |
| pause when unfocused | yes (new) |
| gameplay options | yes (new GAMEPLAY tab) |
| quick save | not yet (checkpoints exist only in the debug console) |
| language | the game is English only |

