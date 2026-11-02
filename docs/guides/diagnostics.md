# Scripted runs and diagnostics

Command examples are bash. On Windows, run them from Git Bash. A path such as
`/tmp/x` that is passed to the app itself (an `OGRE_*` variable or a console
command) must be a path the Windows executable can open, for example
`$TEMP/x` or a folder in the repository.

## The world map's mouse cursor (scene `0x05`)

The world map's cursor follows the pointer, and a left click confirms the
location under it. The cursor is a `Flama` sprite at screen pixels in the map
module's state object (`*(u32*)0x80197B18`): the state machine
`func_ovlM_8019AF0C` keeps `state+0x52`/`+0x54` and clamps them to its play area
(x `0x28..0x110`, y `0x28..0xC0`), and the scene hook's cursor draw
(`func_ovlM_801A103C`) reads the pair at `0x801A1C48`/`0x801A1C4C` and emits the
sprite there.

`app/src/map_cursor.cpp` samples `SDL_MOUSEMOTION`, aspect-fits the 320x240
framebuffer inside the drawable, and writes the pixel into that pair at the draw
site. The draw site is bank code, which is outside the mod hook map, so
`config/hooks.toml` inserts the call and `make bank-recomp` re-applies it.

A click does not move the cursor: it ORs the N64 `A` bit into controller 0 for a
few pad polls in `get_input`, the same path the keyboard and the live console's
`press` command use. The map's `A` selects the location under the cursor.

The pointer scrolls the map by pushing past the view's edge. The map's state
machine clamps the cursor to x `0x28..0x110`, y `0x28..0xC0`, so a pointer beyond
a bound cannot place the sprite there; instead a pointer within 24 pixels of a
bound holds the matching **D-pad** bit in `get_input`, which is what the map
scrolls its view with. The direction is computed from the pointer's pixel clamped
into the framebuffer, so a pointer in the pillarbox or in the black border of a
fullscreen window still scrolls.

Both cursors are off until the player turns on **SETTINGS > INPUT > MOUSE
ENABLED** (`Off` by default; `mouse = on` in `<config>/settings.cfg`). The
setting is the player-facing switch; the per-screen `OGRE_*` variables below
override it for one run, and `OGRE_MOUSE=off|on` sets the row itself.

| Variable | Effect |
|---|---|
| `MOUSE ENABLED` (SETTINGS, default `ON`) | the player-facing switch for both cursors: the world map's and the mission field's, with the click and the edge scroll. `Off` is what builds before the row did |
| `OGRE_MOUSE=off\|on` | set the row for one run without the UI |
| `OGRE_CLASS_TABLE_MOVE=0` | keep the class table's readers on the ROM's table instead of the content database's copy at `0x80607C34`, which is the default (`docs/anotherworld/design-new-classes.md` section 3) |
| `OGRE_CLASS_ANIMATION=0` | hand the game the ROM's class animation resource (asset `0x00315736`) even when packs make new classes, instead of the copy extended to 256 class ids (`docs/anotherworld/design-new-classes.md` section 7). New classes then read past the ROM's 172 rows; the switch exists to isolate a fault |
| `OGRE_BATTLE_FORMS=0` | ignore the packs' `battle_forms` rules: no fighter changes class or attack per strike (`docs/anotherworld/design-battle-transform.md` section 7.2) |
| `OGRE_BATTLE_FORM_MESSAGE=1` | keep the game's "<name> has changed class." message for battle forms, which the port otherwise closes (`docs/anotherworld/design-battle-transform.md` section 8) |
| `OGRE_BATTLE_FORM_TRACE=1` | one `[battle-form] trace` line per battle form hook call: the site (executor, picker, reset), the participant, strikes used `+0x6C`, the count `+0x70`, the class and key words; and one per strike count or attack a rule sets |
| `OGRE_BATTLE_FIGURE_LOAD=0` | do not load a battle figure during the fight; a participant whose class changed to one the battle did not load at its start then hangs the draw at `0x801B12C0`, as in the ROM (`docs/anotherworld/design-battle-transform.md` section 4.3) |
| `OGRE_ASSET_LOG=1` | one `[asset]` line per call of the asset size and load functions (`func_8009DAF4`, `func_8009DBB8`) with the id, destination, ROM offset and size word; an id past the ROM or with an implausible size is marked, which shows a table read out of range |
| `OGRE_MAP_CURSOR=1` / `=0` | force the map's cursor on / off for one run, whatever the row says (`off`, `false` and `no` also work for 0) |
| `OGRE_MAP_CURSOR_LOG=1` | print one `[mapcursor]` line on every change of the game's cursor position, the pointer position, the draw flag or the edge push, and one `[cursor] hidden/shown` line on every change of the operating system pointer |
| `OGRE_MAP_CURSOR_CONFIRM_POLLS=<n>` | how many pad polls a click holds `A` (default 8) |
| `OGRE_MAP_CURSOR_EDGE=<n>` | how close to a bound the pointer must be to scroll (default 24 pixels) |
| `OGRE_MAP_CURSOR_EDGE_FORCE=<mask>` | hold that raw D-pad mask (hex or decimal, e.g. `0x100` for right) as if the pointer were at that edge, with no mouse event. A measurement aid, so a scripted run can scroll with nobody at the mouse |
| `OGRE_CURSOR=0` | keep the operating system pointer visible over the game window (`off`, `false` and `no` also work). It is hidden while the pointer is over the **game** window, and shown again while the pointer is over the Esc overlay window or outside the game window |

The operating system pointer belongs to the whole application, not to one window:
`pump_sdl_events` owns the hide and show of it, since whoever hides SDL's single
cursor has to be the one that shows it again. The state is derived from
`SDL_GetMouseFocus()` and `overlay_visible()` rather than from ENTER/LEAVE
events, so a window that opens under a stationary pointer is handled too. The
close path restores the pointer before the process exits.

## The mission field's mouse cursor (scene `0x03`)

The mission field's cursor follows the pointer, and a left click is the game's
`A`. The cursor is two **f32 framebuffer pixels** at `state+0x08` (x) and
`state+0x0C` (y) in the mission field's state object, where
`state = *(u32*)0x801AB870 = 0x800E91D0` — a **fixed** address (in the main ELF's
BSS) that the mission's modules each hold as a pointer constant in their data
(record 7's `0x801AB870`, record 9's `0x80213690`, record 2's `0x8019E1B4`,
record 3's `0x801AB870`). Session 115 read the same object through
`*(0x80213690)` and recorded it as a heap pointer; it is fixed. The object is
re-laid-out per phase, so the hook is gated by its call site, not by the object's
contents.

`func_ovlN_801BE4E0` (bank unit N, record 7) is the mover. It reads the **held**
button word `*(0x800E8108)` (the base ELF fills that slot with `&D_800E79B0`)
and the D-pad masks of the input profile at `*(0x801F0CC0)`, integrates a
per-frame delta (a ramp of `±26` then `±8*counter`, divided by 128 and multiplied
by 32.0), stores the pair at `0x801BE974`/`0x801BE988`, and clamps it to x
40.0..280.0 and y 30.0..210.0 (`0x801BEB98`/`0x801BEBB8`,
`0x801BEBE0`/`0x801BEC00`).

**The mission's cursor is digital (D-pad) where the world map's is analog.** No
mission bank reads the stick; the map reads it at `*(0x800C47F0)+2`/`+3`.

**The cursor is also a servo the game runs continuously.** Over a played run the
pair moves 1-9 px (median 3.6) on nearly every frame with nothing held, from the
mover's own integration, its pull toward the screen centre, and the snap-to-unit
writers. So the mouse cannot simply follow whatever the game does — that locks it
out — and it cannot always overwrite the game either, because a command recentres
the camera and carries the cursor with it, which is what leaves the cursor on the
place that was just acted on. The arbitration is by explicit signal:

| the game moves the cursor when | log |
|---|---|
| a D-pad direction is held | `pad=1` |
| C-LEFT is held in a mode that centres (profile `+0x1C`, field mode `*(0x801F0E00)` < 2 or == 42) | `centre=1` |
| the mover integrated a delta on this call (`state+0x06`/`+0x07`) | `nudge=1` |
| the window after a click is still running | `yield=1` |

In all four cases the pointer **adopts** the game's position instead of writing;
otherwise it places the cursor at the pointer's pixel.

The two insertions are in the mover: `ogre_mission_cursor_note_move` at the
generated label `L_801BEC04` (the only point where the per-frame delta bytes still
hold this call's values — the mover zeroes them at `0x801BEC48`-`0x801BEC58`), and
`ogre_mission_cursor` at the epilogue `L_801BECC8`, which both of the mover's
paths reach, including the screen-centring branch at `0x801BE890`. The sites are
bank code, so `config/hooks.toml` inserts both calls (it finds the unit that
defines `func_ovlN_801BE4E0`) and `make bank-recomp` re-applies them.

A click does not move the cursor: it ORs the N64 `A` bit into controller 0 for a
few pad polls in `get_input`, the same path the keyboard and the live console's
`press` command use. One click arms both the map's and the mission's confirm, and
each armed count is consumed on every poll whatever screen is up, so an `A` armed
on one screen cannot fire later on the other.

**The field's view scrolls when the pointer is close to a play-area edge.** The
camera pans with the cursor, not with a held direction: the mover converts the
held D-pad into the per-frame delta bytes `state+0x06`/`+0x07` (`0x801BE5A8`
`li $v1,26`, then the ramp at `0x801BE6D4`), and `func_ovlN_801BECF0` — called
right after the mover at `0x801AF130` — reads those same bytes and adds
`delta/128 * 8.5 * table[e]` to the eye x and the look-at x of the camera at
`*(u32*)0x8018F5AC` (`0x801BEE04`-`0x801BEE1C`), with the same for z
(`0x801BEE9C`-`0x801BEF5C`), and clamps the result to the map's world rect at
`0x801F0DD8`. It only runs while `*(0x801F3630)` is set, which the mover sets only
when the cursor pair actually changed on that call (`0x801BEC3C`-`0x801BEC70`), so
the pan is coupled to cursor movement. The pointer's own pixel is clamped by the
mover to 40..280 / 30..210, so a pointer past that bound cannot place the cursor
there and the intent arrives as a held direction instead: a pointer within
`OGRE_MISSION_CURSOR_EDGE` pixels (default 24) of a play-area bound holds the
matching D-pad bit, which the mover decodes through the profile `*(0x801F0CC0)`.
The masks this mode's profile uses are the N64 D-pad bits (right `0x100`, left
`0x200`, up `0x800`, down `0x400`), which the hook prints once per run beside
`hook active`. This is the world map's mechanism, and the arbitration already
adopts the game's cursor while a direction is held, so the push and the pointer
do not fight. Developer-confirmed: the camera could be moved around by moving the
cursor close to the edges (session 129).

| Variable | Effect |
|---|---|
| `OGRE_MISSION_CURSOR=1` / `=0` | force the mission's cursor on / off for one run, whatever the MOUSE ENABLED row says (`off`, `false` and `no` also work for 0) |
| `OGRE_MISSION_CURSOR_LOG=1` | print one `[missioncursor]` line on every change of the game's cursor position, the pointer position, the D-pad state or the camera, one line per edge push, one line per confirm poll, and one line per click |
| `OGRE_MISSION_CURSOR_CONFIRM_POLLS=<n>` | how many pad polls a click holds `A` (default 8) |
| `OGRE_MISSION_CURSOR_YIELD=<n>` | frames after a click during which the pointer adopts the game's cursor instead of placing it (default 15, ~0.25 s). It has to cover the camera coming to rest after a command; the cost is that the pointer does not respond during the window, so the player moves the mouse afterwards to re-sync it |
| `OGRE_MISSION_CURSOR_AT=<x>,<y>` | park the pointer at that framebuffer pixel **once**, with no mouse event, as if it had been moved there. A measurement aid, so a scripted run can place the cursor with nobody at the mouse. Do not run it while someone is testing with a real mouse: the parked pointer is what the placement uses |
| `OGRE_MISSION_CURSOR_EDGE=<n>` | how close to a play-area bound (40/280 x, 30/210 y) the pointer may be before it pushes that direction (default 24 px) |
| `OGRE_MISSION_CURSOR_EDGE_FORCE=<mask>` | hold that raw D-pad mask (hex or decimal, e.g. `0x100` for right) as if the pointer were at that edge, with no mouse event. A measurement aid, so a scripted run can hold a direction with nobody at the mouse |

The `[missioncursor]` line carries the field's camera as
`cam=<eye x>,<eye z>/<look x>,<look z>`. `*(u32*)0x8018F5AC` is the camera the
mover and `func_ovlA_801AAE30` both use, `+0x14` is the eye (its height is
`+0x18`) and `+0x20` the point it looks at, and `func_ovlN_801BECF0` moves both
to pan. `edge=` is the D-pad mask the pointer is currently pushing.

Environment variables make a run self-driving and bounded, for measurements and
captures without a human at the keyboard (see `history-archive:docs/history/DECISIONS.md`, sessions 25,
26 and 27). All default to off.

| Variable | Effect |
|---|---|
| `OGRE_TAP_MS=<n>` | controller 0 presses Start for 150 ms out of every `n` ms (the native equivalent of the web probes' Enter tap) |
| `OGRE_TAP_SCENE=<list>` | press `OGRE_TAP_MS`/`OGRE_TAP_BUTTON` only while the dispatcher's scene (`D_800E810E`) is in the list — names from `kScenes` (`title`, `new-game`, …) or hex, `,`/`+`-separated. Scopes a scripted tap to **game state instead of wall time**, so `OGRE_SPEED` and capture readback no longer break the schedule |
| `OGRE_TAP_NOT_SCENE=<list>` | the complement of the above. `OGRE_TAP_MS=1500 OGRE_TAP_NOT_SCENE=new-game` presses Start through the title and goes silent the moment New Game is confirmed — no `OGRE_TAP_MAX` tuning |
| `OGRE_TAP_BUTTON=<list>` | which pad buttons the synthetic taps press, cycled one per tap (`start`, `a`, `b`, `down`, …; `none` skips a tap). **The list shape matters, and so does the form (developer, session 64): a tap that lands while the cursor is in a text field opens a *tooltip*, and the tooltip blocks the sequence from advancing.** The name-entry form (scene `0x07`) is where this bites. The recorded working shape is **a few `start,a` pairs then a long run of plain `a`** — e.g. `"start,a,start,a,start,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a"` (see the checkpoint and `OGRE_NJREAD_LOG` recipes below). An *endless* `start,a` alternation stalls there forever: a scripted run that shows no crash, no stub call, and a final scene of `0x07` is almost always this. If a schedule keeps stalling, drive the run by hand and take a checkpoint instead |
| `OGRE_TAP_SCENE_BUTTON=<scene>:<button>[:<count>],…` | the **scene-keyed** sibling of `OGRE_TAP_BUTTON` (session 68). `OGRE_TAP_BUTTON`'s slot index advances with **wall time**, so a route across several scenes has to be hand-tuned to how long each takes — the same "title → Load Game → map" schedule landed on the map in one run and in the attract loop in the next, because the boot reaches the title anywhere between 1.6 s and 15 s (the forced-scene poke races the publisher stills). This one keys the button on the dispatcher's active scene (`D_800E810E`; names from `kScenes` or hex) and presses it every `OGRE_TAP_MS` while that scene runs, at most `<count>` times (default unlimited). Entries in one scene are consumed in order, so `OGRE_TAP_MS=1500 OGRE_TAP_SCENE_BUTTON="title:start:5,0x12:a:3,0x05:right:3,0x05:a:1"` is "Start up to five times on the title (early presses are sometimes swallowed; the scene change stops it), A three times on the Load Game screen, then walk the map cursor right three times and press A". Requires `OGRE_TAP_MS`; it replaces the slot schedule entirely |
| `OGRE_EXIT_AFTER_MS=<n>` | after `n` ms the app prints the last recompiled function *and the live call chain* of every game thread, then exits 0 (a scripted run does not unwind on purpose: the graceful path tears threads down mid-call and segfaults intermittently) |
| `OGRE_PREF_DIR=<dir>` | use `<dir>` as the runtime config dir instead of the executable's own folder, so a run keeps its `saves/` and `mods/` where you choose (e.g. inside the repo, or on a sandbox that cannot write next to the app). The boot log prints the resolved path |
| `OGRE_ROM=<path>` | boot this ROM without passing it as an argument (validated and stored like an argument; the start screen is skipped) |
| `OGRE_LAUNCHER=1` | force the start screen even when a ROM is available (testing the first-launch experience) |
| `OGRE_TEST_DROP=<path>` | feed one synthetic ROM drop to the start screen, exercising the drop handler without a human drag (SDL cannot synthesize a Finder drag) |
| `OGRE_LAUNCHER_TAB=<main\|mods\|controls\|settings>` | open the start screen on that tab (screenshot and scripted-run aid) |
| `OGRE_LAUNCHER_KEYS=<name>,…` | push one synthetic keydown per 150 ms through the start screen's real key handler. Names are SDL scancode names (`Tab`, `Down`, `Space`, `p`, `Return`), so `Tab,Tab,Tab,Space,p` opens CONTROLS, arms the A row's rebind and binds `P` |
| `OGRE_LAUNCHER_SHOT=<path>` | after drawing a frame, write the start screen's renderer as a PPM and quit. `OGRE_LAUNCHER_SHOT_MS=<n>` delays it (default: after all `OGRE_LAUNCHER_KEYS` and `OGRE_LAUNCHER_PAD` entries) |
| `OGRE_LAUNCHER_PAD=<tag>,…` | push one synthetic `SDL_CONTROLLERBUTTONDOWN` per 150 ms through the panel's real pad handler, so the pad path is exercised on a machine with no gamepad. Tags are the gamepad tags `controls.cfg` uses (`a`, `b`, `back`, `dup`, `dleft`, `lb`, …) |
| `OGRE_OVERLAY=1` | open the in-game overlay at startup |
| `OGRE_OVERLAY_AT_MS=<n>` | push one synthetic `ESC` down `n` ms after the overlay is initialised, so the real open path runs without a human |
| `OGRE_OVERLAY_TAB=<main\|controls\|settings\|debug>` | open the overlay's panel on that tab (`start` is accepted as an alias for `main`, which carries EXIT GAME) |
| `OGRE_OVERLAY_KEYS=<name>,…` | push one synthetic keydown per 150 ms while the overlay is visible (same names as `OGRE_LAUNCHER_KEYS`). `main` + `Space` activates EXIT GAME |
| `OGRE_OVERLAY_PAD=<tag>,…` | the same for the pad (same tags as `OGRE_LAUNCHER_PAD`), delivered whether or not the overlay is visible, so a script's first `back` opens it |
| `OGRE_OVERLAY_OPACITY=<0.2..1.0>` | the overlay window's opacity (default 0.90) |
| `OGRE_WINDOW_CLOSE_AT_MS=<n>` | push one `SDL_WINDOWEVENT_CLOSE` for the game window `n` ms after the platform was configured, so the window's close path can be exercised without a human clicking the button. Pair it with `OGRE_OVERLAY_AT_MS` and `OGRE_OVERLAY_KEYS="Escape"` to have the overlay's second SDL window open and close first, which is the state the close button used to stop working in |
| `OGRE_CAPTURE_OVERLAY=<path>` | write the overlay's renderer as a PPM once, after its first frame. `OGRE_OVERLAY_SHOT_MS=<n>` delays it, so the capture can show what `OGRE_OVERLAY_KEYS` changed |
| `OGRE_SPEED_KEYS=<up\|down>,…` | push one synthetic `=` or `-` keydown per 150 ms, through the same SDL event path a real press takes, so a scripted run exercises the GAME SPEED hotkeys. `OGRE_SPEED_KEYS_AT_MS=<n>` is the delay before the first press (default 1000). `up`/`+`/`=` and `down`/`-` are the names |
| `OGRE_SPEED_PAD=<tag>,…` | the same for the pad's R3 speed button, as synthetic `SDL_CONTROLLERBUTTONDOWN` events. Tags are the CONTROLS tab's (`r3`/`rstick` for the right stick's click). `OGRE_SPEED_PAD_AT_MS=<n>` is the delay before the first press (default 1000); both scripts use one clock, and a frame delivers the key script first when both are due |
| `OGRE_CAPTURE_BADGE=<path>` | write the speed badge's window as a PPM, one file per show, so the file holds the last badge the run put up (session 137) |
| `OGRE_AUTOPLAY=<0\|1>` | force AUTO-PLAY DIALOGUE off or on for the run, over the setting. `OGRE_AUTOPLAY_LOG=1` prints one `[autoplay]` line per change of the focused message window's verdict and one per press; `OGRE_AUTOPLAY_BASE_MS` (1000) and `OGRE_AUTOPLAY_CHAR_MS` (50) set the read delay. See `docs/anotherworld/notes-dialogue-autoplay.md` |
| `OGRE_SAVE=<name\|path>` | start the run from that save as the cartridge battery (session 78). A bare name is looked up as the path, then `<rom dir>/saves/<name>[.n64\|.bin]`, then `assets/saves/<name>[.n64\|.bin]`, so `OGRE_SAVE=prologue` finds `assets/saves/prologue.n64`. Accepts the port's 32 KiB image, an emulator wrapper of it (in either byte order, at any offset), a **DexDrive `.N64` Controller Pak dump** — converted through its notes' battery slots, checksums reseeded — or a bare pak. The result is written to `<config>/saves/<game id>.bin`, never back into the source |
| `OGRE_SAVE_RESET=1` | re-import even when the battery in the config dir is newer than the source. Without it a run **keeps** that battery, so progress the game wrote back survives a re-run, and swapping a save file in re-imports it (its mtime is newer) |
| `OGRE_SAVE_ALL=1` | with `OGRE_SAVE`, also take the stale records a deleted Controller Pak note leaves behind (still capped at the battery's two save slots) |
| `OGRE_DEBUG_TRACES=1` | the runtime's `[ev]`/`[mq]`/`[sch]`/`[vi-debug]` traces, including `[pi] inline DMA` for every streamed-overlay load (very chatty) |
| `OGRE_DEBUG_VI=1` | with `OGRE_DEBUG_TRACES`, log every `osViSetMode` (mode pointer + decoded geometry) and every VI geometry change (`[vi-debug]`) |
| `OGRE_DL_ANALYZE=1` | per-submission F3DEX2 workload counts (commands, triangles, texrects, textures) |
| `OGRE_DL_DECODE=<n>\|all` | dump the decoded command stream of display list `<n>` (or all): `SETTIMG`/`SETTILE`/`SETTILESIZE`/`LOAD*`/`TEXRECT` with its `RDPHALF` halves/`FILLRECT`/`SETSCISSOR`/combiner/othermode. The ground truth for "which RDP command draws this" |
| `OGRE_RAM_KEY=<1..9>` (`_ADDR`, `_LEN`, `_OUT`, `_STRIDE`, `_AT_MS`, `_FULL`) | press that key and the run dumps guest RDRAM to `<OGRE_PREF_DIR>/ram-<n>.bin` and `.txt`, so a bug can be investigated offline with nothing copied out of a terminal. `_ADDR` defaults to `0x802696C8` (the sprite-part pool session 130 dumped), `_LEN` to `0x1000`, `_STRIDE` sets the slot size the `.txt` summary reports (default `0x80`), and `_AT_MS` takes one dump at that time for a scripted run. `_FULL=1` writes the whole 8 MiB image to `ram-<n>-full.bin` instead of the window; `tools/ogre_ram.py` then answers the questions a window cannot (`find-ptr` = every word equal to an address, which names the table that chose a buffer; `cell` = one slot printed as the game's own CI4 image; `empty-cells` = which slots in a range are all zero). Read-only: nothing is written to guest RDRAM |
| `OGRE_DUMP_RDRAM=<path>` | write the whole 8 MiB RDRAM image for offline analysis: on the exit path (`OGRE_EXIT_AFTER_MS` or a closed window) and beside `error.log` on the crash path |
| `OGRE_TEX_TRACE=1` (`_DUMP`, `_KEY`, `_KEY2`, `_MS`, `_AFTER_MS`, `_DRAW`, `_MAX`, `_MIN_PX`, `_IMAGES`, `_COLOUR`, `_BLACK`) | **what every textured rectangle samples** (session 130), the tool for "one part of a sprite draws as a black rectangle". The submitted display list is walked before RT64 parses it and each `G_TEXRECT` prints one `[tex]` line: the rectangle, its `s`/`t`/`dsdx`/`dtdy`, the `SETTILESIZE` window, the tile's line and palette, and the `SETTIMG` address/format/size/width. `OGRE_TEX_TRACE_DUMP=<dir>` also writes the RDRAM the draw reads to `<dir>/tex/<n>_src.bin` (and `_pal.bin` for an indexed image) beside a `.json`, with a byte summary on `[tex]   src:` lines. **`OGRE_TEX_TRACE_COLOUR=1` decodes each sampled window through its palette (CI4/CI8) or as RGBA16 and prints `[tex]   colour: mean N/255 …`**, plus one `[tex] BLACK DRAW: dl=… n=… rect=… timg=…` line for the first draw whose mean is at or below `OGRE_TEX_TRACE_BLACK` (default 4 of 255, with at least 32 opaque texels) — that is the line that names a part which samples a correct cutout and paints it black. `OGRE_TEX_TRACE_KEY=<1..9>` arms the capture when that digit key is pressed, which is the way to capture a bad frame: press it while the frame is on screen, and `OGRE_TEX_TRACE_MS=<n>` (default 2000) closes the capture by itself; each press re-arms, so several captures fit in one run. `OGRE_TEX_TRACE_MS=0` holds the capture until the window closes, so every frame after the press is in the log by construction, and `OGRE_TEX_TRACE_KEY2=<1..9>` arms the same capture on a second key with its own edge state (a press that was too early does not consume the next one). `OGRE_TEX_TRACE_AFTER_MS=<n>` is the timer alternative (one-shot). `_DRAW=<n>` reports only the nth rectangle of the run and `_MAX=<n>` caps the rest. `tools/ogre_tex_uv.py <dir>` decodes the dump into a per-draw summary (`--top N` = the N darkest samples, `--dark`, `--png`). Read-only: nothing is patched. The directory named by `OGRE_TEX_TRACE_DUMP` is created on demand |
| `OGRE_TEX_TRACE_MODES=1` (or `=2`) | with `OGRE_TEX_TRACE`, print one `[modes]` table when each capture window closes: every textured draw (`G_TEXRECT`/`G_TEXRECTFLIP`, and `G_TRI1`/`G_TRI2`/`G_QUAD` while `G_TEXTURE` is on) keyed by image, format, size, tile window, texture filter (`OTHERMODE_H` bits 12-13: point/bilerp/average; COPY and FILL cycles print `copy/fill`) and cycle type, with the draw count and, for rectangles, native screen pixels per texel. `=1` drops the per-rectangle `[tex]` lines; `=2` keeps them. The walk decodes every list as F3DEX2, so in an S2DEX2 list the `tri1`/`tri2`/`quad` rows are object-sprite opcodes and their `img` can be stale. `tools/capture-sprites.bat` runs it on key 7. Read-only |
| `OGRE_CRASH_TEST=<segv\|bus\|abrt\|fpe\|ill>` | raise that signal right after the crash logger is armed, so a packaged build's `error.log` can be verified without provoking a real crash |
| `OGRE_SMOKE=1` | print `[smoke] main reached`, try SDL once (reported, not required) and exit 0. `tools/smoke-dist.sh` and `make smoke` use it to prove a package loads and reaches `main` |
| `OGRE_CONSOLE=1` | Windows: `AllocConsole()` and point stdin/stdout/stderr at it, so a double-clicked GUI build shows the `[boot]` log (the same switch Zelda64Recomp's Windows build calls `--show-console`) |
| `OGRE_LIVE_CONSOLE=1` | turn on the **live debug console** (session 117). Off by default: with no variable set a stock run polls no path and reads no keyboard, because a console command can write guest RAM (`w`) and restore a checkpoint (`load`). Either trigger can also be turned on on its own by naming its own variable (`OGRE_CONSOLE_FILE`/`OGRE_CONSOLE_AT_MS`, `OGRE_KEY_<n>`). See "The live console" below |
| `OGRE_GRAPHICS_API=<auto\|d3d12\|vulkan\|metal>` | pin RT64's backend instead of `auto`. RT64 switches D3D12 to Vulkan on an old driver (NVIDIA ≤ 475.14, AMD ≤ Jan 2019, Intel 6th-gen ≤ 31.0.101.2115) and that transition rebuilds the interface on the same window; `vulkan` skips it and `d3d12` is the other side of the A/B. The boot log names the GPU and whether the fallback ran |
| `OGRE_CHAIN_HISTORY=<tid>` | record and print the ordered sequence of live call chains thread `<tid>` goes through |
| `OGRE_SNAP=1\|<frames>` | the runtime's **periodic message-queue snapshot** (`[snap]`, `ultramodern::debug_dump_queue_snapshot`), which is what shows a frozen boot's full/blocked queues when every game thread is parked. **Off by default** (session 116): the snapshot samples `last_func_vram` 100 times at 200 us, dumps every parked thread's call chain, walks the malloc graph and prints ~80 lines, all on the Critical-priority VI thread, so it delays the retrace it observes — 26 ms of frame hitch every 1.5 s on macOS, and more on Windows where `sleep_for(200us)` resolves to the 1 ms system timer that SDL's `timeBeginPeriod(1)` sets. `=1` uses the default 90 VI frames (~1.5 s at 60 VI/s), a number sets its own period in VI frames |
| `OGRE_HANG_MS=<n>` | the **hang report** (`app/src/hang_watch.cpp`): when the frame counter `D_800AEFA4` stays still for `n` ms of counted time (default 5000; minimized time and main-thread gaps over 1 s are not counted), print `[hang]`, run the `[snap]` snapshot once and write `error.log`; `[hang] frames resumed` follows if frames come back. `0` turns it off |
| `OGRE_LOOKUP_CHECK=0` | turns off the **lookup check** (`app/src/bank_overlays.cpp`): by default `get_function` compares the code resident at a streamed address with the mapped body's ROM words and runs the body whose words are resident, logging one `[lookup]` line per address that disagrees with the map |
| `OGRE_UPSCALE_2D=scaled\|all` | RT64's 2D rectangle mode for an A/B; the default is `original` (`app/src/renderer.cpp`), which samples stretched rectangles at the console's pixel positions |
| `OGRE_QUICK_KEYS=<ms>:<f5\|f9>,...` | scripted quick-save presses for a test run, in ms after the first frame (`app/src/quick_save.cpp`) |
| `OGRE_COVER=1\|<path>` | recompiled-function **coverage census** (session 83): at exit, when the window is closed, or on the live console's `cover`, print every distinct recompiled function the run entered, one `[cover] 0xADDR count` line per function, for `tools/recompcov.py --log`. **Give it a path** (`OGRE_COVER=/tmp/cover.txt`) for a session played by hand — the census then lands in its own file instead of the end of a very chatty stdout, and the app prints one line saying so. `=1` keeps it on stdout for scripted runs. Unlike `OGRE_PROFILE=1` it starts no sampling thread and skips the shadow call chain, so the entry hooks cost one hash insert per call and a playthrough stays near normal speed |
| `OGRE_SYNTH_FRAME=1` | submit a synthetic F3DEX2 display list (seven colour bars) through the normal task path, as a renderer-path probe. Also turns on the two presenter diagnostics below |
| `OGRE_SYNTH_AT_MS=<n>` / `OGRE_SYNTH_PERIOD=<n>` | when the probe first submits, and how many VIs between re-submits (default 30; 0 = every VI). Keep `AT_MS` above ~600 ms: before that the game's ucode is not in RDRAM yet and RT64 cannot identify the GBI |
| `OGRE_NO_DUMMY_VI=1` | skip the pre-start dummy VI workload (it clears the screen black every VI and would hide a diagnostic draw) |
| `OGRE_SYNTH_RAW=1` | also write the bars straight into the VI framebuffer in RDRAM, bypassing the RDP (isolates the presenter from the display list) |
| `OGRE_SYNTH_ANIMATE=1` | rotate the raw palette every VI so RT64's change-driven presenter keeps presenting |
| `OGRE_SYNTH_NO_DL=1` | with `OGRE_SYNTH_RAW`, do not submit the display list (RAM path only) |
| `OGRE_SYNTH_FORCE_VI=0` | do not pin the VI to the probe's framebuffer (the pin is on by default; without it the probe is timing-dependent, because the game may have repointed the VI at one of its own framebuffers) |
| `OGRE_CAPTURE_PRESENT=<path>` | at present time, GPU-read back the exact swap-chain texture and write `<path>.<n>.ppm` (see "Capturing the native output") |
| `OGRE_CAPTURE_TARGET=<path>` | same, for the render target the VI renderer sampled (`<path>.<n>.bin`, `R16G16B16A16_UNORM`, 8 bytes/texel) |
| `OGRE_CAPTURE_AFTER=<n>` | skip the first `n` presents before capturing |
| `OGRE_CAPTURE_EVERY=<n>` | with `OGRE_CAPTURE_PRESENT`, capture only every `n`th present — a multi-minute run becomes a slideshow instead of one 3 MB PPM per frame (files stay numbered by present index) |
| `OGRE_SPEED=<n>` | scale the emulated clock (CPU counter **and** VI retrace schedule) by `n` (1..64), so timed sequences — the attract loop, songs — complete in `1/n` of the wall time. Semantics are unchanged: every timer scales together (audio is off in these runs). `OGRE_SPEED=8` reaches the attract loop's second variant in ~42 s instead of ~360 s. This is the debug spelling of **GAME SPEED**: with the variable unset the runtime takes its starting value from `<config>/settings.cfg`, and `OGRE_SPEED` overrides that file for one run without writing it |
| `OGRE_WIDESCREEN=off\|on` | debug spelling of **WIDESCREEN**: `on` turns RT64's Expand aspect ratio on only while the dispatcher runs the mission scene `0x03`. The retired `missions` and `always` spellings are read as `on`. Overrides `<config>/settings.cfg` for one run without writing it, the same rule as `OGRE_SPEED`. Every transition prints a `[widescreen]` line |
| `OGRE_FRAME_RATE=original\|display` | debug spelling of **FRAME RATE**: `display` sets RT64's refresh rate to the display's while the dispatcher runs the mission scene `0x03`, so RT64 interpolates the field; every other scene stays at the original rate. Overrides `<config>/settings.cfg` for one run without writing it. Every transition prints a `[frame-rate]` line. See `docs/anotherworld/notes-frame-interpolation.md` |
| `OGRE_FIELD_TAGS=0` | with FRAME RATE `display`, leave the field's camera on RT64's own matching instead of the fixed camera id the RT64 patch assigns (an A/B of the tags) |
| `OGRE_DISPLAY_EXPERIMENTS=1` | turn on the two experimental DISPLAY rows, RESOLUTION and MSAA, which are hidden and not applied without it. A set `OGRE_RESOLUTION` or `OGRE_MSAA` enables it too |
| `OGRE_RESOLUTION=native\|2x\|auto` | debug spelling of **RESOLUTION** (experimental; enables the row). `2x` renders at twice the native resolution in each axis and `auto` at the largest integer multiple that fits the window |
| `OGRE_MSAA=off\|2x\|4x\|8x` | debug spelling of **MSAA** (experimental; enables the row) |
| `OGRE_WINDOW=windowed\|fullscreen` | debug spelling of **WINDOW**, the same override rule |
| `OGRE_SOUNDS=off\|on` | debug spelling of **SOUNDS**: `off` queues silence. Overrides `<config>/settings.cfg` for one run without writing it, the same rule as `OGRE_SPEED` |
| `OGRE_VOLUME=<0..100>` | debug spelling of **VOLUME**, the output gain in percent, clamped. Same override rule |
| `OGRE_MOUSE=off\|on` | debug spelling of **MOUSE ENABLED**, the switch for both screen cursors. Overrides `<config>/settings.cfg` for one run without writing it. `OGRE_MAP_CURSOR` / `OGRE_MISSION_CURSOR` then override one screen on top of it |
| `OGRE_FORCE_SCENE=<hex>` | switch a run to attract scene `<hex>` by poking the scene id (`*(u16*)(D_800C4BBC+4)`) until `D_800E810E` reports it active — no need to wait out the attract loop. `OGRE_FORCE_SCENE_AFTER_MS` (default 3000) delays the first poke so boot can settle |
| `OGRE_PRESENT_ALWAYS=1` | push a present on every VI even when nothing changed (a stalled boot changes nothing, so the window otherwise freezes on an old frame) |
| `OGRE_PRESENT_FBTARGET=1` | if the framebuffer manager has no framebuffer at the VI address, present a non-empty render target there instead of the RDRAM copy |
| `OGRE_INSTANT_PRESENT=1` | switch RT64 to `PresentEarly` (a display list presents the framebuffer it drew) |
| `OGRE_RECT_LR_FIX=1` | restore RT64's **"Fix LR with Scissor"** rect enhancement, which is **off** by default here. RT64's `RDP::drawRect` (`rt64_rdp.cpp`) rewrites a rectangle's right edge to the scissor's right edge whenever the two are within one pixel, on the assumption that a game under-drew a full-screen background. OB64's rectangles are correct, and the unit profile's rightmost DEF/MDEF digit sits exactly one pixel inside the panel scissor, so the snap widened a 6-px glyph rectangle to 7 px and it sampled the next font cell's first column (`28` read as a cropped `9`, `11` as a cropped `2`). `OGRE_RECT_LR_FIX=1` A/Bs the old behaviour. See `history-archive:docs/history/handoffs/HANDOFF-2026-09-25-session101.md` |
| `OGRE_PRESENT_TRACE=1` | `[present]`/`[state]` traces: what each present carries and which path it took |
| `OGRE_RDP_TRACE=1` | `[rdp]` traces: `setColorImage`, `fillRect`, `drawRect` (with the scissor/empty checks) |
| `OGRE_WORKLOAD_TRACE=1` | `[workload]` traces: the framebuffer pair RT64 built (colour image, scissor, call count) |
| `OGRE_FBRENDER_TRACE=1` | `[fbrender]` traces: the renderer's per-call view (cycle type, fill colour, rect) |
| `OGRE_VI_TRACE=1` | `[vi]` trace of the decoded VI RT64 is about to present |
| `OGRE_OVERSCAN=<0..8>` | how much the presenter crops off the framebuffer's right/bottom edge (default **1 px**; `0` = present the full framebuffer, the pre-session-81 behaviour). OB64's own scissor and clear are `(0,0)-(1276,956)` = 319x239 of its 320x240 buffer, so the last row/column are never written by the scene and keep leftovers from the boot's njpeg draws; the VI's three-buffer rotation then toggled them into a flickering edge line. A CRT's overscan hid the game's off-by-one. Session 81's other half is **not** a knob — a game framebuffer is now sized from the VI rather than from the rectangle the frame drew (`framebufferHeightForDisplay`), because the RDP snaps a copy/fill rect's fractional bits and frames alternated between 239/240/241-row targets. See `history-archive:docs/history/handoffs/HANDOFF-2026-09-18-session81.md` §5-§7 |
| `OGRE_SCHED_TRACE=1` | enable the scheduler traces and, at exit, dump the race-free scheduler event ring (`[sched-ring]`): every insert/pop/remove/resume/park/wake/swap with a global sequence number and the running queue after it. This is how a thread that yielded and was never resumed is read off a run |
| `OGRE_SCHED_TRACE_TID=<tid>` | with `OGRE_SCHED_TRACE`, filter the ring dump to events involving thread `<tid>` |
| `OGRE_NOP_RECT=<selectors>` | app-side bisect aid: walk the display list before RT64 parses it and replace matching `G_TEXRECT`s (and their `RDPHALF_1`/`RDPHALF_2` pair) with `G_SPNOOP`, so a suspected draw disappears and the layers under it show. Selectors: `any`, `tex:<hex>` (the current `SETTIMG` address), `flip` (`dsdx < 0`), `x:<a>-<b>`, `y:<a>-<b>`, `n:<index>` |
| `OGRE_FOG=0` | disable the session-35 repair that rewrites OB64's fog/cloud "wrap" rectangles to sample the layer's own image (found through the game's layer table at `D_801B80E0`). On by default. The repair was a no-op until session 36 (its `G_SETTILESIZE`/`G_LOADTILE` extents were in texels, not the GBI's quarter-texels); it now draws the title's logo-band fog and the `©1999 QUEST` sweep (`history-archive:docs/history/handoffs/HANDOFF-2026-09-14-session36.md` §3) |
| `OGRE_FOG_TRACE=1` | report each repaired fog rectangle group (layer, image address, dimensions and the rectangle's `s`/`t`) |
| `OGRE_FOG_SCALE=<percent>` | scale the fog image's intensity when it is staged for the fog group (**default 100 = the game's own asset**, which is the right level: the combiner is `RGB=ONE, ALPHA=TEXEL0`, so the overlay is white modulated by the layer image's intensity, mean 5.1% / peak 20.4%). Lower values stage a scaled copy in the last 64 KiB of RDRAM, which OB64 leaves untouched (the first use reports it if that area is not zero); `45` gives a softer, non-retail look |
| `OGRE_FOG_SUMMARY=1` | one `[fog-summary]` line per display list (groups repaired, layer/image used) plus one `[fog-group]` line per white group (`s`, `t`, the inherited tile, layer, image installed). This is how the `©1999 QUEST` blink was traced |
| `OGRE_NO_AUDIO=1` | skip opening an audio output device; the boot logs `[SDL] OGRE_NO_AUDIO: no output device opened`. Every run of the game sets it (AGENTS.md rule 14). The audio microcode still runs, so a result that depends on the audio task is unaffected |
| `OGRE_NJPEG=0` | force the **stub** for the Nintendo-JPEG decoder (`M_NJPEGTASK`, type 4: the New Game step ≥ 2 background decode). On by default since session 47, when the recompiled microcode was fixed (`text_address` is a label base — see `docs/guides/rsp-microcode.md`); it costs 0–1 ms per image |
| `OGRE_NJPEG_TRACE=1` | `[njpeg]` lines: the colour image the YUV macroblock draw landed in. RT64 records it in the scratch word, but it is only *re*set by a YUV-texture-then-colour-image pair and never cleared, so it can be stale — the readback trusts the game's own `state[0x64]` instead (`config/hooks.toml`, `history-archive:docs/history/handoffs/HANDOFF-2026-09-16-session57.md` §1) |
| `OGRE_NJREAD_LOG=1` | `[njread]` one line per njpeg stage-3 pass (`func_ovlE_8019976C`): the pass number, scene/step, the framebuffer the game's `state[0x64]` selects, which rule the njpeg readback hook (`config/hooks.toml`) applied (`game` or `scratch`), the scratch word (`cimg`/`njpeg`/`lastfb`), the display word `D_800C4BB8`, and the pass's destination/size. This is how the stale-source artifact was found and is the before/after check for it |
| `OGRE_YUV_TRACE=1` | `[yuv]` one line per `G_SETTIMG` with `fmt=YUV` (the njpeg macroblock texture image): confirms the `0x800A5110` draws reach RT64 at all |
| `OGRE_BG=0` | run **without** the `mods/backgrounds/` HD-background pack (`off`, `false` and `no` also work). The pack is otherwise loaded automatically from the first folder that has a `backgrounds.txt` (`docs/guides/hd-backgrounds.md`) |
| `OGRE_BG_DIR=<dir>` | use **only** this folder as the pack, instead of `<config>/mods/backgrounds`, `<config>/../mods/backgrounds` and `<cwd>/mods/backgrounds`. Pointing it at an empty folder is the other way to run without a pack |
| `OGRE_BG_LOG=1` | `[hdbg]` one line per njpeg readback pass and per applied chunk: the scene/step, the destination, and which image (`01` or `01 wide`) |
| `OGRE_BG_DEBUG=1` | paint each readback pass a solid colour (320x240 red, 176x240 green, 320x144 blue, 176x144 yellow) instead of the pack image, so a capture reads the four-pass composition |
| `OGRE_BG_DUMP=<dir>` | write the game's own backdrop — the four sub-images assembled into the 496x384 canvas — as `original-step<NNNN>.png`, before the pack replaces anything. `tools/backgrounds.py` and `make bg-extract` drive this and file the result |
| `OGRE_WS_SCENES=<hex>[,<hex>…]` | the scenes RT64's Expand is used for when WIDESCREEN is on (default the mission `0x03` and the dialogue/cutscene scene `0x0D`); a scene the HD-background pack covers is always added |
| ~~`OGRE_NJ_WAIT_MS=<n>`~~ **removed (session 77)** | this used to make the **RSP worker** wait after *every* display list for the game's njpeg framebuffers (`0x400`/`0x25C00`/`0x4B400`) to appear in RT64's framebuffer manager, 500 ms timeout. Session 57 measured it as a no-op in the njpeg path (`[njwait] spins=1 found=1 ms=0`) but session 77 found its real cost: any scene that renders into neither of those three **fixed** addresses can never satisfy the condition, so the poll burned the whole 500 ms on every submitted list. The boot's **publisher stills (scene `0x0A`, the Nintendo/ATLUS/QUEST logos, which use a 640x480 image buffer) ran at 2 display lists/s instead of 30** — a visible ~16× slowdown. The readback never needed the wait (the game's own DP-completion wait plus `ogre_sync_framebuffers()` order the copy), so the call was deleted. `OGRE_SYNC_TRACE=1` no longer prints `[njwait]` lines, only `[syncfb]`. See `history-archive:docs/history/handoffs/HANDOFF-2026-09-18-session77.md` |
| `OGRE_SYNC_TRACE=1` | `[njwait]` (the RSP-worker render wait: spins, found, ms, queue/workload ids), `[syncfb]` (the framebuffer manager at the game's CPU readback) and `[njpair]` (the framebuffer pairs RT64 builds for `0x400`/`0x25C00`/`0x4B400`) |
| `OGRE_S2D_TRACE=1` | `[s2d]` the first 40 S2DEX 2D-object commands (`G_OBJ_RECTANGLE`/`_R`/`G_OBJ_SPRITE`), with the decoded `uObjSprite` (fmt/siz/image size/stride/TMEM address), the matrix-derived destination rectangle, the target colour image and the current texture image address. This is the diagnostic that showed Ogre Battle 64's 300 macroblock rectangles tiling its 320x240 njpeg framebuffer (session 51) — without the S2DEX2 object commands implemented, the same list looks like "texture loads only" |
| `OGRE_CONVERT_TRACE=1` | `[convert]` the raw 9-bit `G_SETCONVERT` fields the game programs (`k0..k5`). The YUV16 decode sign-extends and scales them `2*K+1` and converts with `R = Y+K0*V'`, `G = Y+K1*U'+K2*V'`, `B = Y+K3*U'` (`U'=U-128`, `V'=V-128`) — the game sets `k0=175 k1=469 k2=423 k3=222 k4=114 k5=42`, i.e. coefficients `351/−85/−177/445` (session 52; `k4`/`k5` are the combiner's `K4`/`K5`, used raw over 255) |
| `OGRE_FB_WRITEBACK=1` | `[fbpair]` (which framebuffer pairs RT64 renders, with their rows) and `[fbwrite]` (the CPU-visible RDP→RDRAM writeback, with the first pixels). This is how "does a rendered framebuffer reach RDRAM for a CPU read" is answered |
| `OGRE_RDP_TRACE=2` | instead of the first-20 `[rdp]` lines, a `[cimg]` histogram of every colour-image address plus a `[cimgseq]` ordered list of the game's framebuffer targets — the ground truth for "which buffers does the game render into" |
| `OGRE_FOG=alternate` | apply the repair on every other display list. On screens the game redraws every list this A/Bs the fog; it cannot say *which* screen a pair is on (session 35's isolation image was the story screen), and on the static title it shows nothing because no display list changes |
| `OGRE_SCENE=<name\|hex>` | boot straight into a screen: `title` (0x04: the attract title — logo plus the New Game / Tutorial / Stereo menu over the clouds), `intro` (0x09), `publishers` (0x0A), `story` (0x0B), `unit-info` (0x0C), `tutorial` (0x17: Deneb's "Is this your first time here?" dialogue, the title's Tutorial entry), `menu` (0x18), `new-game` (0x02), or any hex id (`OGRE_FORCE_SCENE` still works). The poke is only seen **before the boot enters its first scene (~1.1 s)**, so it runs from the streamed-DMA hook and a per-frame retry and releases once `D_800E810E` reports the target. `menu`/`new-game` still SIGSEGV (unfinished cross-bank work) |
| `OGRE_STEP=<n>` | **seed** the New Game opening's **step** at `n` for the selected scene — the scene-script step lives in `D_8018F1C0` and scene `0x0D` branches on it, so the movie (step 1, ~28.7 s) can be skipped. `OGRE_SPEED=8 OGRE_SCENE=new-game OGRE_STEP=2 OGRE_NJPEG=1 ./build-app/ogrebattle64` lands in the cathedral ~2.2 s after boot (step table: `history-archive:docs/history/handoffs/HANDOFF-2026-09-15-session44.md` §1, what each step shows: `docs/scenes.md`). The seed is written **once** on the first frame the target scene is active, then released as soon as the live step moves off it (or after a 1500 ms window). It is *not* held every frame: the VM writes the step itself when the sequence advances, and a per-frame hold overwrites that advance, so the opening loops on the seeded step forever (session 53) |
| `OGRE_SCENE_AFTER_MS=<n>` | delay before the first scene poke (**default 0**). A delay past ~1500 ms makes the jump a silent no-op: the running scene re-establishes the state block every frame |
| `OGRE_PROBE55=1` | `[probe55]` the New Game **sequence state** on every change: the scene, the scene-script step word `D_8018F1C0`, the next-scene word `D_8018F1C2`, and session 43's script-buffer words `0x80197B23`/`0x80197B38`/`0x80197B3C`. Session 53 used it to show the step stuck at 2 through the cathedral and then 0 (never 3), with the buffer words reading 0 the whole run |
| `OGRE_SCENE_LOG=1` | log every scene change with its descriptor and record mask — how the scene ids above were identified |
| `OGRE_PROBE57=1` | `[probe57]` the New Game **handoff state** on change: the scene, the step word `D_8018F1C0`, the next-scene word `D_8018F1C2`, the cutscene vtable pointer the scene-`0x0D` exit dispatches through (`0x801D0830`), the engine words `0x8023A994`/`0x8023A960` and `0x801DFC90`. Session 54 used it to show the natural opening advance `step 1 → 2` and the exit setting `D_8018F1C0 = 0` with `next = 0x0007` before scene `0x07` runs |
| `OGRE_DMA_TRACE=1` | at `OGRE_EXIT_AFTER_MS`, dump **every** streamed PI DMA the game issued, grouped by `(rom, ram)` base with a `first`/`last` event index and a chunk count. The `[bank]` line only covers records the port compiled, so this is how a module the game loads but the port has no record for is found (session 54: the scene-`0x07` black screen). Prints only bases in overlay C's window `0x80190000..0x801C0000`; `OGRE_DMA_TRACE_FULL=1` prints every base (≈17k in a 26 s run) |
| `OGRE_STEP` (corrected) | the `OGRE_STEP` seed is a **shortcut that changes the sequence's own state**, not just an entry point: entering `0x0D` at step 2 with no step-1 visit makes the exit take its `otherwise` arm and re-enter at step 0, which selects the movie-mode branch and crashes (session 53's wall). For a real New Game repro, drive the title instead: `OGRE_SCENE=title OGRE_TAP_MS=1000 OGRE_TAP_BUTTON="start,a,start,a,…"` (session 54) |
| `OGRE_SCENE_TRACE=1` | the forcing state every 500 ms (`active`, `pending(D_800E8214)`, the state block and its id, and whether the target is active) — use this when a jump "does nothing" |
| `OGRE_EMPTY_TILE=draw` | restore the pre-session-35 behaviour of drawing rectangles whose tile covers zero texels (`lrs == uls` or `lrt == ult`) instead of skipping them |
| `OGRE_EMPTY_TILE_TRACE=1` | name every rectangle skipped because its tile covers no texels |
| `OGRE_RECT_STATE=<y0>-<y1>` | per-rectangle render state for rectangles contained in rows `[y0,y1)`: cycle type, both combiner cycles (decoded by RT64's own `ColorCombiner::cycleColorText`/`cycleAlphaText`), the blender inputs, the primitive colour and the tile descriptor it samples |
| `OGRE_TILE_TRACE=1` | every tile whose sampling rectangle is degenerate (`sampleHeight <= 1`), with the texture the cache returned (`hash`, index, dimensions, `tcScale`, `rawTMEM`) |
| `OGRE_TILE_TRACE_ALL=1` (`_MAX`, `_MAXH`, `_MISS`, `_COPY`) | the same `[gpu-tile]` line for every tile (`_MAXH=<n>` keeps only tiles whose sample is at most n by n, so a scene's 320-wide layers do not consume the cap; `_MISS=1` prints only tiles whose texture-cache lookup failed; `_COPY=1` also prints the tiles served by a framebuffer tile copy, which otherwise print nothing) |
| `OGRE_SPRITE_TRACE=1` | one `[spr]` line per tile of a two-texture draw, printed **after** the upload decision: the draw's `call` index, the tile's position and its count, the cycle type, the combiner, PRIM and ENV, the tile descriptor (`fmt`/`siz`/`line`/`tmem`), the sample size, `tileCopy`, `rawTMEM`, the hash it resolved to, and the RDP tile's `address`/`stride`/masks/`nativeSampler`. This is the line that says which path a tile took; `[dec]` below says what that path read (session 134) |
| `OGRE_TILE_DECODE=1` | for every tile the texture cache hashes, the RGBA the renderer would decode out of the TMEM snapshot it is hashing, computed with the same rules as `TextureDecodeCS.hlsl` (`nz`, `mean`, three sample pixels, once per hash); the raw-TMEM path, which never reaches `uploadTexture`, prints its own line; and `[fbcopy]`/`[fbcopy-stale]`/`[fbcopy-outside]`/`[fbcopy-nowrite]` name every tile served by a framebuffer copy and every copy that was refused: `[fbcopy-stale]` the source framebuffer is out of sync with RDRAM, `[fbcopy-outside]` the tile's texels are not inside the framebuffer's `lastWriteRect`, `[fbcopy-nowrite]` the framebuffer was not written in the workload being processed (`fbWorkload` against `workload`) (sessions 134, 136) |
| `OGRE_FB_GPU_COPY=0` | turn RT64's `emulatorConfig.framebuffer.copyWithGPU` off for one run, which removes the framebuffer tile-copy path entirely and makes every texture load read RDRAM. The A/B for the black-head fix; `1` restores RT64's default |
| `OGRE_DUMP_TEX=<dir>` | write RT64's decoded-texture dump (a 4 KiB `.tmem` plus `.tile.json` per texture) into `<dir>` instead of opening its file dialog |

```sh
# a bounded run with taps, and the per-thread call chains
OGRE_TAP_MS=5000 OGRE_EXIT_AFTER_MS=60000 ./build-app/ogrebattle64 > /tmp/ogre.out 2>&1
grep -A 12 "per-thread last" /tmp/ogre.out

# where the boot thread actually goes (the session-26 measurement)
OGRE_CHAIN_HISTORY=3 OGRE_EXIT_AFTER_MS=3000 ./build-app/ogrebattle64 2>&1 | \
  sed -n '/chainhistory/,/callchain t1/p'

# renderer-path probe: build and submit a real display list while the boot is stuck
OGRE_SYNTH_FRAME=1 OGRE_SYNTH_AT_MS=1000 OGRE_SYNTH_PERIOD=30 \
  OGRE_EXIT_AFTER_MS=20000 ./build-app/ogrebattle64
```

## Jumping straight into a screen

The attract loop takes minutes at 1×. `OGRE_SCENE` enters a screen at the next
scene load instead, and `OGRE_SPEED` compresses whatever waits inside it:

```sh
# boot into the attract title (logo + PRESS START over the clouds)
OGRE_SCENE=title ./build-app/ogrebattle64

# a bounded title-screen capture: the title arrives ~17 s in at 4× and lasts
# ~10 s, so capture a window across it
OGRE_SCENE=title OGRE_SPEED=4 OGRE_CAPTURE_PRESENT=/tmp/title \
  OGRE_CAPTURE_AFTER=1700 OGRE_CAPTURE_EVERY=10 OGRE_EXIT_AFTER_MS=26000 \
  ./build-app/ogrebattle64

# if a jump "does nothing", this says whether the poke landed
OGRE_SCENE=title OGRE_SCENE_TRACE=1 OGRE_EXIT_AFTER_MS=6000 ./build-app/ogrebattle64
```

The poke only counts before the boot enters its first scene (~1.1 s), so leave
`OGRE_SCENE_AFTER_MS` at its 0 default. See
`history-archive:docs/history/handoffs/HANDOFF-2026-09-14-session36.md` for the dispatcher path and the verified
ids (`menu` 0x18 and `new-game` 0x02 still crash: unfinished cross-bank work).

## Capturing the native output

Do **not** use `screencapture` to decide whether the renderer works. On a locked
or asleep display it returns the last frame the window server committed, which
can be seconds old (a cycling clear colour stays one colour across many
captures), and the session-26 "the window is black" conclusion came from exactly
that. Measure the presented texture instead:

```sh
# one command: submit the probe every 30 VIs and read back what is presented
OGRE_SYNTH_FRAME=1 OGRE_SYNTH_AT_MS=900 OGRE_SYNTH_PERIOD=30 OGRE_NO_DUMMY_VI=1 \
  OGRE_CAPTURE_PRESENT=/tmp/frame OGRE_CAPTURE_AFTER=200 \
  OGRE_EXIT_AFTER_MS=8000 ./build-app/ogrebattle64
# -> /tmp/frame.201.ppm, /tmp/frame.202.ppm, ... (P6; the swap chain is BGRA8)
```

`OGRE_CAPTURE_PRESENT` is the ground truth for "what does RT64 present": it is
the swap-chain texture itself, with no window server in the path. Add
`OGRE_CAPTURE_TARGET=/tmp/target` to also dump the render target the VI renderer
sampled, which separates "the RDP did not draw" from "the presenter dropped it".
The port reads `OGRE_CAPTURE_PRESENT` **once** at startup and passes the path to
RT64 through `ogre_present_capture_path()`; the console's `snap` command toggles
it without touching the environment, because the UCRT's `putenv` copies the
string and hiding an entry by rewriting its name is a POSIX-only trick (that
copy left the capture on in v0.4.0's Windows build and crashed the D3D12 backend
on the first present — session 107). A value you set in the environment wins over
`snap`, and `snap` says so.

`OGRE_SYNTH_FRAME` turns on `OGRE_PRESENT_ALWAYS` and `OGRE_PRESENT_FBTARGET` for
you (with `setenv(..., overwrite=0)`, so an explicit value still wins); with
`OGRE_SYNTH_NO_DL` it turns on only `OGRE_PRESENT_ALWAYS`, because the
render-target fallback would bypass the RAM upload path the raw probe is testing.
The probe also pins the VI to its own framebuffer every VI (`osViSwapBuffer`), so
the presented frame is the probe's and not whichever of the game's framebuffers
the VI happened to point at; `OGRE_SYNTH_FORCE_VI=0` turns that off.

Without `OGRE_PRESENT_ALWAYS` a stalled boot never re-presents and the window
keeps an old black frame even though the render path works, and without
`OGRE_PRESENT_FBTARGET` an RDP-only fill has no framebuffer in the manager so the
presenter uploads empty RDRAM instead of the drawn target.

## The game's own frames

The game drives its own double-buffered VI, so none of the probe knobs are
needed to see it - only the capture. This is the end-to-end check that the port
renders the game (it should show the title scene's ring of soldiers):

```sh
# title scene, captured straight off the presented swap chain
OGRE_TAP_MS=4000 OGRE_CAPTURE_PRESENT=/tmp/game OGRE_CAPTURE_AFTER=30 \
  OGRE_EXIT_AFTER_MS=25000 ./build-app/ogrebattle64
# -> /tmp/game.31.ppm ... : the twelve soldiers of the title screen
```

A healthy run submits the game's real display lists
(`data=0x801C1520` / `0x801C80A0` alternating, ~2.5/s) rather than only the boot
blanking list at `0x800C6500`.

## Reading a dump

**The image is stored in ROM order** (verified session 132): the four file bytes
at offset `a & 0x1FFFFFFF`, read **big-endian**, are the guest word, and the file
byte at that offset is the logical byte the game holds at guest `a`. So a Python
read is `int.from_bytes(data[a & 0x1FFFFFFF : +4], 'big')` for words and
`data[a & 0x1FFFFFFF]` for logical bytes.

The check that settles it: the part cache's own layout words. `D_8018FC88` must
read `0x80269640`-style (a KSEG0 heap pointer) and `D_80190F70` must be eight
bytes past it plus `0x8000`. An earlier reading used
`struct.unpack_from('<I', ...)` and `data[off ^ 3]`, which returns the
byte-reversed value (`0x40962680`) and transposes every multi-byte field; that
reading produced session 131's withdrawn "12x16 cell with a palette at `+0x60`".
`tools/rdram.py` and `tools/ogre_ram.py` read ROM order now.

`tools/rdram.py` does all of that for you (`word`/`half`/`byte`/`string`/
`hexdump`/`find`/`ptr`), and one mode answers the question mis-binding walls turn
on:

```sh
tools/rdram.py /tmp/rdram.bin banks        # which module is resident in each window
```

It compares the dump against every record in `app/src/bank_funcs.inc` (+ the
uncompiled segment-table records) and says which ROM offset is *actually* live at
each streamed RAM base. Streamed RAM holds different modules at different times
(`0x802395E0` is `bankRec14b` for the movie visit and `bankRec14c` for the steps
after it), so "the port called the wrong function" usually shows up here as
"the bytes at the faulting address are a different module's".

The black canvas a stalled boot produces is the game's idle trajectory, not a
renderer failure - but do not conclude that from a screen capture; see
"Capturing the native output".

## Diagnostics toolkit

Four offline tools answer the questions every session otherwise re-derives by
hand. None of them needs a CUDA/GPU/game run except where noted. The **live
console** below is the fifth, and the only one that queries a *running* game.

### A slow screen — a host stack sample, not the profiler (sessions 112, 115)

`OGRE_PROFILE=1`'s per-thread function is the **most recently entered** function:
`recomp_trace_entry` writes `last_func_vram[tid]` on every entry and
`recomp_trace_return` never restores it
(`tools/N64ModernRuntime/ultramodern/src/function_trace.cpp:262`). Read its `t4`
line as a lead, and take a host stack sample while the screen is slow:

```sh
debug/menu-probe.sh                    # PROBE_DIR/APP/ROM/EXTRA_ENV overridable
sample <pid> 4 -file /tmp/slow.txt     # or by hand, against a running window
```

`sample` names the recompiled function each game thread is in, so a chain through
`yield_self` says the frame period is a recompiler-injected poll-loop wait.
`debug/menu-probe.sh` runs the app with `OGRE_SCENE_LOG`, `OGRE_DL_TRACE`,
`OGRE_PROFILE` and `OGRE_COVER`, keeps `sample <pid> 1` running for the life of
the process, and binds the number keys to `snap` (RDRAM + presented frames),
`save`, `cover` and `dump`, so a slow second is captured whether or not anyone is
watching for it. `[renderer] display list N at t=<ms>ms entries=<n>` gives the
same window as a period series; `entries` is what the list cost in recompiled
function entries.

A candidate is a **work loop** whose backward branch the heuristic
(`is_yield_poll_loop`, `tools/N64Recomp/src/recompilation.cpp`) has mistaken for
a poll: it terminates on a counter or on data already present, and not on a value
another thread produces. The landed fixes are the branches in
`config/banks/config-bankN.toml`'s `yield_work_loop_branches`
(`0x801B3008`, `0x801DA2C0`, `0x801D0C08`, `0x8020A910`, `0x80204C48`; sessions
80, 90, 98, 112, 115).

### A repeated stutter — `tools/dlgaps.py` (session 116)

A stutter that repeats at a constant period is one piece of periodic work, and
not a slow screen. `tools/dlgaps.py` reads the `OGRE_DL_TRACE=1` series and
reports the gap distribution plus the phase of every spike, which is what names
the period:

```sh
OGRE_DL_TRACE=1 OGRE_EXIT_AFTER_MS=40000 ./build-app/ogrebattle64 assets/ogre64.z64 > /tmp/run.log 2>&1
tools/dlgaps.py /tmp/run.log                 # summary + phase histogram
tools/dlgaps.py /tmp/run.log --check         # exit 1 when the spikes are periodic
tools/dlgaps.py /tmp/run.log --period 3000   # test another period
```

It skips the first 3 s by default (`--after`), because the boot's module
streaming produces gaps that are not a stutter, and it treats a gap above
`--spike` x p50 as a spike. `--check` fails when 5 or more spikes put at least
60 % of themselves in one tenth of `--period`; that is the signature of the
runtime's periodic `[snap]` snapshot (session 116) and of a
`yield_work_loop_branches` misfire (sessions 80, 90, 98, 112, 115), and it is
what a self-driving run should assert before and after a fix. The tool is a
heuristic: a degraded run whose slow frames are spread across many phases
reports `not periodic` even when the work is still there, so read the p90 and
the spike count next to it.

### `make elf-rom-check` — is the linked code the ROM's code? (session 65)

`tools/elfcheck.py` compares every `CONTENTS` section of every linked ELF
(`build/bank*.elf`, `build/ogrebattle64.elf`) with the ROM at the same ROM offset,
and `--syms` (what the target passes) also asserts that every symbol whose *name*
ends in eight hex digits is defined at that address — free, because the project
names labels `D_ovlM_801A6FD8` / `func_ovlM_801A2A7C`.

```
$ make elf-rom-check
bankM.elf: 0 differing bytes of 553504
bankM.elf: 479 address-named symbols all at their named address
ogrebattle64.elf: 0 differing bytes of 41943040
ogrebattle64.elf: 4224 address-named symbols all at their named address
```

Run it whenever a scene draws the wrong *size*, the wrong *slice* or the wrong
*string* from a table — before blaming the renderer or the game's data. Session 65
found unit M's whole `.data` subsegment 8 bytes high this way: `mips-linux-gnu-as`
pads `.text` to 16 bytes, unit M's code/data boundary is only 8-byte aligned, so
the data subsegment moved up 8 and **every** `lui/%lo` data reference in the
recompiled module read 8 bytes too high — which put the map's sprite table one
entry late and made the party, the shadow, the cursor and the date panel all draw
their neighbours' sizes for four sessions. A misalignment is fixed in the unit's
config (`align`/`subalign`, or by ending the `asm` subsegment on the 16-byte
boundary as unit M's now does), never in the generated `.s` or the generated C.
`make bank-recomp` runs the check before recompiling, so a misaligned link fails
the build instead of producing a scene that looks wrong.

### The live console — query a running game (session 56)

The offline tools can only look at a dump, and a bounded run can only dump at its
exit; by then the buffer that mattered has usually been overwritten (session 56
lost an afternoon to a framebuffer readback that had to be caught mid-frame).
The console executes commands inside the running game, on the **main thread**,
where RDRAM is available and a multi-megabyte write cannot race the game thread.

**The console is off unless a run asks for it.** It is a development tool, and a
command can write guest RAM (`w`) or restore a checkpoint (`load`), so a stock run
does not poll any path and does not read the keyboard. Turn both triggers on with
`OGRE_LIVE_CONSOLE=1`. Naming a trigger's own variable also turns that trigger on
by itself: `OGRE_CONSOLE_FILE` (or `OGRE_CONSOLE_AT_MS`) for the file,
`OGRE_KEY_<n>` for the keys. `OGRE_LIVE_CONSOLE=0` is an explicit off. Recipes
written before session 117 write `/tmp/ogre-console.txt` with no variable set and
need `OGRE_LIVE_CONSOLE=1` added. `OGRE_CONSOLE_ON_CMD` below is unaffected: it
calls the console directly.

Two triggers, both landing in the same command set:

* **a watched command file** (default `/tmp/ogre-console.txt`, override with
  `OGRE_CONSOLE_FILE`): when the file exists its lines are executed and the file
  is **removed**, so an external tool can drive a live run:

  ```sh
  OGRE_LIVE_CONSOLE=1 OGRE_EXIT_AFTER_MS=600000 \
    ./build-app/ogrebattle64 assets/ogre64.z64 | tee /tmp/live.log
  # in another shell (or from an agent):
  printf 'c\nr 0x8018FDAC 8\nk 0x80243E00 8192\ndump /tmp/at-now.bin\n' > /tmp/ogre-console.txt
  ```

* **number keys `1`..`9`** run `OGRE_KEY_1`..`OGRE_KEY_9` (edge-triggered, one
  command per press), e.g.
  `OGRE_KEY_1='dump /tmp/here.bin' OGRE_KEY_2='c' ./build-app/ogrebattle64 …`.

Commands (all addresses are guest `0x80xxxxxx`; output goes to stdout prefixed
`[console]` and is flushed):

| command | what it does |
|---|---|
| `r <addr> [n]` | `n` words in the runtime's dump order (what `tools/rdram.py word` prints) |
| `rh` / `rb` / `rk` | halfwords / logical bytes (`addr ^ 3`) / **raw** host-order words |
| `d <addr> [len]` | hex + ASCII dump of a byte range |
| `f <value> [max]` | find a 32-bit word anywhere in RDRAM (guest + file offset) |
| `fb <hexbytes> [max]` | find a byte pattern |
| `s <addr> [len] [max]` | strings in a range |
| `k <addr> [len]` | fnv1a checksum of a range — the cheap before/after for A/B runs |
| `w <addr> <value>` | store a word (A/B experiments; it is a real write) |
| `c` | scene, pending scene, current descriptor + its record mask, step, next, spin |
| `cover` | the recompiled functions entered so far (needs `OGRE_COVER=1`), one per line — a mid-run coverage snapshot without ending the run; `tools/recompcov.py --log` reads the same lines |
| `dump [path]` | write the whole 8 MiB RDRAM image **at this instant**. A bare `dump` never overwrites: it writes `/tmp/ogre-rdram-NNNN.bin`, one new file per press, so the bound key can be hit as often as you like and every snapshot is kept |
| `snap [prefix] [ms]` | **the one-key report for "something looks wrong on screen"**: writes the whole 8 MiB RDRAM image **at this instant** to `<prefix>.bin` (bare: `/tmp/ogre-snap-NNNN.bin`) with the game threads parked so the image is one instant, and turns RT64's presented-frame capture on for `ms` (default 400) so `<prefix>.<present>.ppm` files land for the same screen. The PPM path is fixed at `/tmp/ogre-shot.<present>.ppm`; the toggle sets a flag the app owns and never frees the path the present thread holds, so `OGRE_CAPTURE_PRESENT` is read once at startup and a value you set wins over `snap` (see the block comment in `sdl_platform.cpp`). Add `OGRE_VI_TRACE=1` to the run and the same log names the framebuffer, which renders out of the dump with `tools/rdram.py <dump> image <addr> <w> <h> --fmt rgba16` |
| `save [path]` | write a **checkpoint** (whole RDRAM + the runtime's overlay state). A bare `save` writes `/tmp/ogre-checkpoint-NNNN.ckpt` and remembers it |
| `load [path]` | **restore** a checkpoint this build wrote — the machine rewinds to the instant of the save and the game re-runs from there. A bare `load` uses the most recent bare `save` |
| `press <buttons> [polls] [x] [y]` | **hold a synthetic pad press** for `polls` input polls and then release it, so an external tool can walk a menu interactively ("press right", look at the capture, "press a") instead of guessing a wall-clock tap schedule (session 68). `<buttons>` is the `+`-joined `OGRE_TAP_BUTTON` vocabulary (`start`, `a`, `up`, `left`, …), `polls` and the stick values are parsed as **hex** for the poll count and `atof` for `x`/`y`, so `press a 400` is 1024 polls and `press none 60 -1 0` is a full-left analog stick. **A short press is much shorter than it looks: the game polls input about once per VI retrace (measured, session 70: `min=1 max=2 mean=1.01` calls per retrace), so `polls` ≈ emulated frames — `press right 8` is invisible, `press right 2000` walks the map cursor across the world; budget a few hundred polls per cursor step** |
| `profile <file.rec>` | load a profile save record (`<stamp>-s<slot>.rec`, as the Esc menu's SAVES tab writes them) the way that tab's LOAD does: from any scene the game goes to the title first, then loads. The rest of the line is the path |
| `shot <file.png>` | save the game window as a PNG (Windows; the Print Screen capture), moved to the path given |
| `savegame` | make a profile save as the Esc menu's SAVE does (world map: slot 1; mission field and battle: the suspend record); the `[saves] Saved to ...` line follows |
| `help` | the list |

**Reaching a played state with no input.** `OGRE_AUTOLOAD=<file.rec>` loads a
profile save record once the title runs; with `OGRE_SCENE=title` a slot-1
record reaches the world map in about 6 s and a slot-15 (field suspend) record
the mission field in about 5.5 s. `OGRE_BACKGROUND=1` opens the window without
taking focus, never confines the pointer and never pauses on focus loss
(PAUSE WHEN UNFOCUSED), so a scripted run leaves the
mouse and keyboard to whoever is at the machine; pair it with `OGRE_MOUSE=off`.
The first run in a new `OGRE_PREF_DIR` can stall about 5 s on its first screen
(`[hang]` followed by `frames resumed`); later runs in the same folder do not.

`dump` is the one that fixes the "wrong moment" problem: run the game until the
screen is in the state you want, then drop the command file (or press the bound
key) and the image is written while that state is live. Pair it with
`tools/rdram.py <dump> image …` / `diff …` as usual.

`OGRE_CONSOLE_AT_MS=<n>` delays every watched-file read until `n` ms of wall
clock have elapsed, so a scripted run can leave the command file in place at
launch instead of racing it from a background writer.

**Write the watched file atomically** (`printf … > f.tmp && mv f.tmp f`). The app
polls the path, opens, reads and **removes** it; a plain `> f` truncates before
the write, so the reader can catch an empty file, delete it and the bytes land in
an unlinked inode — the command silently never runs (session 68 lost two rounds
to this).

**`OGRE_CONSOLE_ON_SCENE` / `OGRE_CONSOLE_ON_STEP` / `OGRE_CONSOLE_ON_CMD`** run
one console command on the first frame a chosen scene (and optionally that exact
sequence step) is active — the deterministic alternative to timing a command
against wall-clock taps:

```sh
OGRE_CONSOLE_ON_SCENE=0x0D OGRE_CONSOLE_ON_STEP=974 \
  OGRE_CONSOLE_ON_CMD='save /tmp/ck.ckpt' ./build-app/ogrebattle64 assets/ogre64.z64
# [scene] console trigger: scene 0x000D step 974 -> save /tmp/ck.ckpt
```

`OGRE_CONSOLE_ON_SCENE` takes a name from the scene table (`new-game`, `tutorial`,
`title`, …) or a hex id; `OGRE_CONSOLE_ON_STEP` is the value of the sequence step
word `D_8018F1C0` (`docs/scenes.md` lists the opening's steps). It fires once per
process. This exists because every tap route in this project is wall-clock while
the game's own progress is not: a schedule tuned for a 2 s title lands in the
attract loop when the title takes 9 s, and the trigger removes that failure mode
(session 58).

### Checkpoints — `save` / `load` (session 58)

A checkpoint is a **full machine rewind**, so a long scripted path only has to be
played once: reach the scene you are working on, `save`, then `load` it instead
of replaying the New Game opening every run (the opening is ~45 s at 4x before
the first form and ~60 s to the closing movie).

```sh
# leave `c\nsave /tmp/ck.ckpt\nc\n` in the watched file before launch:
printf 'c\nsave /tmp/ck.ckpt\nc\n' > /tmp/ogre-console.txt
OGRE_LIVE_CONSOLE=1 OGRE_SCENE=title OGRE_SPEED=4 OGRE_TAP_MS=1500 \
  OGRE_TAP_BUTTON="start,a,start,a,start,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a,a" \
  OGRE_CONSOLE_AT_MS=30000 OGRE_EXIT_AFTER_MS=600000 \
  ./build-app/ogrebattle64 assets/ogre64.z64 | tee /tmp/live.log
# later, from another shell:
printf 'load /tmp/ck.ckpt\nc\n' > /tmp/ogre-console.txt
```

The file is `[8-byte magic][u32 version][u32 rdram_size][u32 host_size][u32
flags][u64 fnv1a][overlay-state blob][whole RDRAM image]` (8 MiB + a few KiB).
`load` verifies magic/version/sizes/checksum and refuses a file from another
build; a bare `dump` is still the plain image `tools/rdram.py` reads.

Why it is not just "write RDRAM to a file":

* **The function map is host state.** Which recompiled body runs at each RAM
  address is the runtime's `func_map`, not RDRAM. Restoring RDRAM alone would
  rewind the game's data but leave the map on a later bank, so the restored
  scene would run the wrong module's bodies at those addresses (the
  session-45/55 mis-binding class). The blob carries the loaded section and
  function-bank records and rebuilds the map from them
  (`recomp::overlays::get_overlay_state_blob` / `restore_overlay_state_blob`).
* **The game runs on its own host threads**, so reading or writing 8 MiB from
  the console's main thread raced the game thread that was mid-frame. Session
  58's first attempts produced **torn images**: the visible symptoms were a
  checkpoint whose own checksum did not match its bytes and whose first `0x300`
  bytes were zeros while the game kept submitting RSP tasks and building frames
  during the write. `save`/`load` now wrap the file I/O in
  `ultramodern::checkpoint_pause_begin()` / `checkpoint_pause_end()`, which
  parks every thread executing recompiled code at a function-entry boundary
  (`recomp_trace_entry`, the hook the recompiler emits at every function start)
  for the duration, then releases it. The console reports how many threads
  parked.

Verified (session 58): a checkpoint's stored checksum matches its own bytes; a
`load` 20 s later puts scene/step back to the saved values and the game
**continues from there** (a checkpoint taken at the personality questions
replays forward into scene `0x16`, the closing movie); a save/load pair in the
*same* run reproduces the rewind.

Verified again, and bounded, at the map (session 65): a `save` + `load` pair in
the **same run**, taken on the live map (scene `0x05`, a busy screen with every
N64 thread active), restores and continues with no crash. Loading the *same
bytes* written by an **earlier process** (the developer's run, same binary)
crashes deterministically and immediately in `do_send` (`SIGSEGV` at
`do_send + 0x54C`, on an N64 thread, with a garbage host address) even though the
load reports success and `c` shows the restored scene. Reason: the image holds
**host pointers** — `OSThread::context` is "an actual pointer regardless of
platform" and is written into guest RDRAM, one per N64 thread (~10 in a map
image, values in the `0x00007F…` range in the file). Same process ⇒ still valid;
another process ⇒ a stale address the scheduler dereferences. So a checkpoint
is **not transferable between runs** yet — a session cannot hand one to the next
one, and a `load` recipe must `save` and `load` in the same run. Making it
transferable means rebuilding the runtime's host state from the restored guest
state after the rewind (rebind a per-thread context registry, and reset the
scheduler/blocked-thread host state), not just the RDRAM image.

Limits: valid only in the process that wrote it (function pointers are
process-local) and for the same binary — the file carries an FNV-1a hash of the
running executable (`version 2`), so a **rebuild invalidates every checkpoint**
and `load` says so. The app's own knobs (`OGRE_TAP_BUTTON` position,
`OGRE_SCENE`, `OGRE_STEP`) are not part of the snapshot, and RT64's own render
state is not rewound (the next frame redraws from the restored RDRAM).
**Where** you save matters: a checkpoint taken while a *stable step* is live
resumes and replays (verified at the cathedral and at the personality
questions), but one taken at a scene/step **transition** (e.g. during the brief
`0x02` loader visit, when the engine has already reset the step word to 0)
restores into the "no step" movie-mode branch and the sequence bounces between
`0x00` and `0x0D` instead of continuing — so save *inside* the step you want to
replay, and check `c` reports the step you expect before `save` (session 58).

### Saves — the cartridge battery (SRAM), and emulator interchange (session 66)

**OB64 saves to the cartridge battery, and the chip is SRAM: 32 KiB, no
Controller Pak and no EEPROM/FlashRAM.** The Controller Pak strings
(`Controller Pak Menu`, `Data saved to Controller Pak.`) are the game's
*copy/backup* feature, not the save. `app/src/main.cpp` sets
`entry.save_type = recomp::SaveType::Sram`; the evidence is in the comment there.

The runtime keeps the image at

```
<config>/saves/<game id>.bin          # ogrebattle64-us-rev1.bin, exactly 32768 bytes
# <config> is the executable's own directory (so the save sits beside the app,
# and a distributed package carries its saves with it), unless OGRE_PREF_DIR
# relocates it or the install location is read-only — in which case the
# per-user preference dir is used (macOS: ~/Library/Application Support/ogrebattle64/)
```

and the game's own DMA path reaches it: `func_8008BC40` queues an `OSIoMesg` to
`D_800AA408`, and the runtime completes it inline in `librecomp/src/pi.cpp`
(`pi_perform_dma`) — **the device is chosen from the OSPiHandle the message
carries**, not from the address: the save handle `func_8008A040` builds has
`baseAddress 0xA8000000` (physical `0x08000000` = the SRAM window), the cart one
from `osCartRomInit` is `0xA0000000`. The game reads the whole image at boot in
256-byte DMAs at device offsets `0..0x7F00` (`func_80074CF0` and friends) and
writes it back the same way when its dirty flag is set (`func_80074BF0` →
`func_80074C58`). Save slots are `0x1850` (6224) bytes at `0x10 + n*0x1850`; the
device signature and slot-0 magic are the ASCII `QuestOG3`.

**A slot's first 4 bytes are two `u16` checksums, and their seed is the slot's own
device offset** (session 78). `func_8007541C` rejects a slot unless
`memcmp(D_800B8240, slot+4, 8)` matches `QuestOG3` **and**

```
u16[+0x00] == (sum of the bytes of slot+0x0C .. slot+0x1850) + (0x10 + n*0x1850)   // func_80075A84
u16[+0x02] == (count of their set bits)                     + (0x10 + n*0x1850)   // func_80075B00
```

So a slot is valid **only in the slot index it was checksummed for**. The game's
own copy-to-pak path uses the *un-seeded* twins (`func_80075AC4`/`func_80075B60`),
so a slot lifted out of a Controller Pak is seeded 0 and must be reseeded for the
battery slot it lands in. Any tool that moves save data between slots, or from a
pak, has to rewrite those two halfwords; `tools/sramsave.py` does, and `check`
reports each slot as `ok` or `MISMATCH (the game will reject this slot)`.

Diagnostics: the runtime prints one `[save]` line when it loads/creates the file,
one on the game's first read and first write of the window, and one when the
saving thread writes the file (`[save] wrote …`); a failed write also prints
`[save] FAILED to write …` to stderr before its message box.

**Bringing an emulator save in.** Ports/emulators all hold the same 32 KiB of
logical bytes but wrap them differently, so use `tools/sramsave.py` rather than
guessing:

```sh
tools/sramsave.py check  ~/Downloads/save.srm        # what is in the file
tools/sramsave.py import ~/Downloads/save.srm \
  "$HOME/Library/Application Support/ogrebattle64/saves/ogrebattle64-us-rev1.bin"
tools/sramsave.py export <that .bin> out.sra         # 32-bit byteswapped, .sra-style
```

`import` finds the SRAM by its magic at any offset and byte order. It was
written for a **parallel-n64** dump: 296960 bytes, SRAM at **0x20800**, every
32-bit word byte-reversed (so the magic reads `seuQ3GOt`). mupen64plus/RetroArch
`.srm` dumps are the bare logical 32 KiB (offset 0, "logical") and import
unchanged. It never invents data: a file without the magic is refused.

**DexDrive `.N64` files are Controller Pak dumps, not battery images** (session
78). A DexDrive dump is a 0x1040-byte header (`"123-456-STD"` at 0x00) followed
by a 32 KiB Controller Pak, so it is 36928 bytes — which is exactly what
`assets/saves/*.n64` are. They hold **no** battery image at any offset (scanning
every offset for the device magic finds none), but they do hold the game's
*copy/backup* notes: one 25-page (6400-byte) note per save in the pak's own PFS
filesystem, game code `NOBE`, publisher `EB`, named `OgreBATTLE64 <n>`, and each
note carries a verbatim copy of a battery slot at `note+0x20`. `import` follows
the pak's FAT to the **live** notes (a deleted note leaves its 25 pages behind,
still readable), extracts each slot and reseeds it into battery slot 0 (then 1);
the battery has **only two save slots** — slot index 2's offset `0x30B0` is where
the game's 19176-byte map record begins, and it covers the rest of the image, so
extra notes are dropped with a warning rather than written into it;
`--all` also takes the stale records, and `pak --all` lists them:

```sh
tools/sramsave.py pak --all assets/saves/prologue.n64
```

### Running with one of these saves — `OGRE_SAVE`, or `tools/run-save.sh`

```sh
# one knob: import (if needed) and boot
OGRE_SAVE=prologue ./build-app/ogrebattle64 [rom.z64]
OGRE_SAVE=~/Downloads/save.srm OGRE_SAVE_RESET=1 ./build-app/ogrebattle64

# or the wrapper, which gives each save its own config dir
tools/run-save.sh --list             # the saves in assets/saves
tools/run-save.sh prologue           # import it, then boot with it as the battery
tools/run-save.sh prologue --reset   # re-import, discarding progress saved since
```

`OGRE_SAVE` writes the converted image to `<config>/saves/<game id>.bin` — the
active config dir, i.e. the default one or whatever `OGRE_PREF_DIR` says — and
never back into the source, so a run left on a battery keeps its progress and
`OGRE_SAVE_RESET=1` starts over. `run-save.sh` does the same thing with a
per-save config dir, `.ogre-prefs-save-<name>/` (gitignored like the other
`.ogre-prefs-*` sandboxes), and its `--import-only` prints the path without
booting. Both accept the same file kinds and pass every `OGRE_*` knob through:

```sh
OGRE_SAVE=prologue OGRE_TAP_MS=1500 \
  OGRE_TAP_SCENE_BUTTON="title:start:1,0x12:a:3,0x05:a:1" \
  OGRE_EXIT_AFTER_MS=60000 ./build-app/ogrebattle64
```

Verified (session 78): with `assets/saves/prologue.n64` imported, the tap route
takes the game **title → `0x12` (the Load Game book) → `0x05` (the map)**, and
battery slot 0 stays **byte-identical** to the converted note. The *discriminator*
is what the game's own data screen reads back, not the route — a **blank** battery
also reaches `0x12`/`0x05` (pressing A on the data screen starts a new game), so
the route alone proves nothing:

| battery image in the config dir | the book's GAME DATA 1 |
|---|---|
| `assets/saves/prologue.n64` converted | `Magnus / Prologue / Alba / 0:20:42` |
| `assets/save-mission-1.srm` converted (the older import) | `Magnus / Prologue / Alba / 0:07:02` |

Same screen, same route, two config dirs, different playtime — the game is reading
the file this script installed. `OGRE_SAVE=prologue` rebuilds that battery
**byte-identically** to `tools/sramsave.py import` (device header and slot 0
compared directly), so the two entry points are one conversion. The same file imported *without*
reseeding the header checksums makes the game repair the whole image to its blank
format (sha256 `ed38cfd7…`), which is what the seed rule above predicts.

Verified (session 66, all on this build): boot with no file → the game formats a
blank battery and the port writes a real 32768-byte image with the `QuestOG3`
signature; restart → the game reads it back and does **not** re-write it (it is
accepted); overwrite the file with `0xA5`-garbled bytes → the game reads,
rejects and repairs it, and the file is valid again; a converted parallel-n64
save with real progress → the game reads it and does not re-write it (accepted).
A `[save]` write is followed by `[save] wrote <path>` unless the directory is
unwritable, which raises the runtime's "Failed to write to the save file" box —
**from the saving thread**, so a run that cannot write its `saves/` directory
stalls (that is what the sandboxed first attempt of session 66 hit; use
`OGRE_PREF_DIR`).

### `tools/runlog.py <run.log>` — one screen per run

```sh
OGRE_SPEED=4 OGRE_TAP_MS=1500 OGRE_TAP_NOT_SCENE=new-game,0x0D OGRE_SCENE_LOG=1 \
  OGRE_EXIT_AFTER_MS=45000 ./build-null/ogrebattle64 > /tmp/run.log 2>&1
tools/runlog.py /tmp/run.log              # scene timeline, RSP tasks, bank loads, problems
tools/runlog.py /tmp/run.log --check      # exit 1 on crash / stub call / unknown module
tools/runlog.py /tmp/run.log --json       # machine-readable
```

The **non-gfx RSP task table** is the part that matters: graphics tasks go to
RT64 regardless, but a non-gfx task served by `stub microcode` is work the port
silently drops. Session 46's four `M_NJPEGTASK` background-decode tasks were
invisible inside 23k lines of gfx chatter and this prints them with their ucode,
data and `data_ptr` on one screen. `--check` is the assertion a future
`make smoke` should use.

### `tools/rdram.py ... image` — is the buffer blank or an image?

```sh
tools/rdram.py /tmp/rdram.bin image 0x80243E28 --width 320 -o /tmp/bg.png
tools/rdram.py /tmp/rdram.bin image 0x80243E28 --fmt ia8 --width 320
tools/rdram.py /tmp/rdram.bin image 0x800D0000 --offset 0x18 --scale 3
```

Renders a region as an N64 texture (`rgba16`, `rgba32`, `ia16`, `ia8`, `ia4`,
`i8`, `i4`, `ci8` + `--palette`) to an RGBA PNG (zlib only, no Pillow) and
prints a luminance summary with a **`(near-)uniform` note** — the difference
between "nobody drew this" and "an asset failed to decode".

### `OGRE_NJREAD_LOG=1` — which framebuffer the njpeg readback copies

```sh
OGRE_NJREAD_LOG=1 OGRE_SCENE=title OGRE_SPEED=4 OGRE_TAP_MS=1000 \
  OGRE_TAP_BUTTON="start,a,start,a,start,a,start,a,start,a,a,a,a" \
  OGRE_SCENE_LOG=1 OGRE_EXIT_AFTER_MS=25000 ./build-app/ogrebattle64 assets/ogre64.z64 \
  > /dev/null 2> /tmp/njread.log
grep njread /tmp/njread.log
```

One `[njread]` line per stage-3 pass. Each New Game njpeg assembly runs four
passes — the four tiles of the backdrop's 2x2 grid, `width x height` 240x320
(top-left, destination `0x801AAE90`), 240x176 (`0x801D06B0`, bottom-left),
144x320 (`0x801E50D0`, top-right), 144x176 (`0x801FB8F0`, bottom-right) — each
with its own YUV draw. The four correct sources are the deterministic frames
`573FF47B8A5A3279`, `48892A76C362A4BA`, `3F79B6732153B2FD`, `38BCBEBE5B555C3C`
in that order, so an off-by-one read shows as a frame mismatch. **Pass 0 (the
top-left tile) is where a stale selection shows**: the scratch word still names
the previous step's target while `gameSrc` is the current one. See
`history-archive:docs/history/handoffs/HANDOFF-2026-09-16-session57.md` §1.

To check the *result* rather than the selection — the assembled backdrop the game
blits — drive the live console from game state, not wall time (the 8 MiB dumps
shift the tap schedule, so a fixed-time schedule does not reproduce):

```sh
# poll `c` and dump only while the dispatcher is on a chosen scene+step
OGRE_LIVE_CONSOLE=1 OGRE_EXIT_AFTER_MS=600000 ./build-app/ogrebattle64 assets/ogre64.z64
printf 'c\n' > /tmp/ogre-console.txt            # read state...
printf 'dump /tmp/at-cathedral.bin\n' > /tmp/ogre-console.txt   # ...then dump
tools/rdram.py /tmp/at-cathedral.bin image 0x80243E28 --width 320 -o /tmp/dst.png
```

With the console reading `desc=0x8018FC3C` and `step=0x021D8002` (the cathedral
step after the name form), `0x80243E28` is the backdrop: the fix shows the
cathedral, the old scratch-first rule showed the name-entry form — that is the
intermittent artifact.

### `tools/rdram.py diff A.bin B.bin` — what did a run change, by module?

```sh
tools/rdram.py diff /tmp/stub.bin /tmp/with-fix.bin --min-run 16 --limit 20
```

Clusters changed bytes into runs, merges runs within `--gap`, and prints the
running total **grouped by owning record/overlay** (from `tools/n64map.py`),
flagging RAM that several records share. This replaces "add a probe to `rsp.cpp`
that dumps a region before and after a call".

### `tools/guestmap.py <addr>` — which module is this?

```sh
tools/guestmap.py 0x801B7EBC 0x802395E0 0x2F180    # vram or rom, auto-detected
tools/guestmap.py 0x801B7EBC --context 4           # +-symbols
tools/guestmap.py --list                           # the whole segment/record table
```

Prints the ROM offset (`vram - 0x80070C60 + 0x1060` and back), the owning
segment/record and unit, whether the address is a **function entry in the ELF
that owns that layout**, and — the mis-binding warning — the other records that
map the same RAM. Example: `0x801B7EBC` is `func_801B7B9C + 0x320` (not an
entry) in `ogrebattle64.elf`, but `func_ovlC_801B7EBC` (an entry) in `bankC.elf`
and `func_ovlG_801B7EBC` in `bankG.elf`; which one is live is decided by the
game's DMA.

### `tools/scenemap.py` — the scene registry, the transitions, the script (session 64)

```sh
tools/scenemap.py                 # all three tables (~3 s, ROM-only, no run)
tools/scenemap.py scenes          # 25 scene types -> accessor -> descriptor words
tools/scenemap.py transitions     # every writer of the pending-scene word
tools/scenemap.py steps           # the scripted step table summary
tools/scenemap.py scenes --dump /tmp/map3.bin   # read descriptors from a live image
```

Answers *"do we have to link the scenes by hand?"* with numbers, and replaces
hunting a scene's address one at a time. Three layers, only the first two code:

1. **The registry** — `func_80075BC0` writes **25** accessor pointers into
   `D_800AF028[0..24]`; each accessor is a 1-3 instruction stub returning a
   descriptor, and the descriptor table is **static ROM data** (`streamedB`).
   `scenes` prints `id → accessor → descriptor → enter/update/hook/leave/mask`
   (20 resolve statically; ids 2, 3, 6, 8, 23 branch and say
   `(indirect accessor)`). It reproduces every address earlier sessions found by
   hand — id 5 → `0x8018FD70` (map), id 7 → `0x8018FDAC` (form), id 13 →
   `0x8018FC3C` (the `0x0D` movie/dialogue engine).
2. **The transitions** — every store into `D_800C4C26` in the game is
   **39 sites in 30 functions**; 21 carry a statically-known value, the rest are
   script-driven. That is the whole scene-to-scene *code*, so there is no graph
   to author.
3. **The content** — the scripted step table (asset `0x19A8804`, ROM
   `0x1F3CA54`) holds **1693** per-step asset ids (1499 distinct), so the 200+
   dialogues are *entries in one table* driven by the same few scene types, not
   200 scene links.

Pair it with `tools/guestmap.py <descriptor>` for the ROM offset and owning
segment of a descriptor, and `tools/runlog.py` for which scenes a run actually
entered.

### `tools/arenamap.py` — every streamed arena, and whether we have it (session 73)

```sh
tools/arenamap.py                      # every arena, every bank (~10 s, offline)
tools/arenamap.py --missing            # banks no unit links
tools/arenamap.py --arena 0x80214FA0   # one RAM window
tools/arenamap.py --verify             # every unit vs the loader that DMA's it
tools/arenamap.py --coverage           # the ROM the scanned ELFs disassemble
tools/arenamap.py --entries            # cross-record entry candidates
tools/arenamap.py --md                 # markdown table (docs/scenes.md)
```

Answers *"what is this bank's size, and is it compiled?"* without a run, from the
loader's own instructions. Every bank DMA is

```
lui/addiu x3  ->  jal func_8009DA50  ->  subu a2,a2,a0   (the size)
func_800900C0(ram_base, code_size)   icache   (the brackets)
func_80090010(code_end, data_size)   dcache
func_80093380(data_end, bss_size)    bss
```

so the block yields ROM start, RAM base, size and the code/data boundary. **The
size is the loader's `subu`, never the chunk count**; the tool reads the `subu`'s
own operands because the assembler sometimes hoists it above the `jal`, and it
evaluates each block in ascending address order because `addiu a1,a1,%lo` must run
*after* the `lui` that gives it its base.

`--verify` cross-checks every compiled record against the loader that DMA's it.
A **split** difference is not automatically a bug: `mips-linux-gnu-as` pads
`.text` to 16 bytes, so a record whose data starts on an 8-byte boundary links its
data subsegment up to 8 bytes high (session 65's unit M), and several units'
linker scripts declare their record as one subsegment; `make elf-rom-check` is
what proves the bytes. `--coverage` is the negative check that matters: a loader
can only be found in code some ELF disassembles, so it lists the uncovered ROM
gaps and any `jal 0x8009DA50` inside them.

Current answer: **27 bank loads across 5 arena RAM windows, 26 compiled**, the one
gap being a `0xE80` scene-`0x14` setup fragment whose RAM another unit's record
already owns (see `docs/scenes.md`).

### `tools/stubmap.py` — which dispatches can find nothing, and who points at them

```sh
tools/stubmap.py report                 # every statically dispatched target, ranked
tools/stubmap.py report --verbose       # also list inert gaps and resolved targets
tools/stubmap.py report --json          # machine-readable, for triage scripts
tools/stubmap.py report --strict        # exit 1 on a definite/unresolvable/likely gap
tools/stubmap.py target 0x80219594      # one address: owners, bodies, callers, scenes
tools/stubmap.py log /tmp/run.log       # attribute a run's stub hits
tools/stubmap.py pointers               # data words pointing at risky targets (jalr)
```

`arenamap.py` answers *"is this bank compiled?"*; this answers the next question,
*"does the function map have the address a call dispatches to?"* — the half of the
whack-a-mole that a run only shows you one hit at a time as
`[overlays] streamed function stub called @ 0xADDR (not yet loaded)`.

It reads the runtime's own registration tables, not the generated C: the bank
side from `app/src/bank_funcs.inc` (what `on_streamed_dma` registers), the base
side from `RecompiledFuncs/recomp_overlays.inl`'s section table (what
`load_overlays(0x1000, entrypoint, 0x3E1B0)` installs — the only place a
*reimplemented* function such as `osSetIntMask_recomp` appears, because it has no
generated body). Call sites come from the generated C's `jal` comments, so each
finding names the caller, its unit and the file:line.

The buckets are ordered by how much is actually known, and the tool refuses to
claim more:

* **DEFINITE** — either **no module registers the address at all**, or the bank
  the caller executes from has no entry at the target while the target is inside
  that bank's own DMA'd code. The first is the strongest form: `func_map` is
  only ever filled from the registration tables, so if nothing in the model
  lists the address, *no run state* can resolve the lookup — this is the
  session-82 shape (a bank the port has no record for is resident, and the call
  lands where a compiled module's layout has no function). Replayed against the
  pre-session-82 tree, the tool marks both of that bug's call targets
  (`0x801D1508`, `0x801D0AAC`) DEFINITE.
* **MISSING FUNCTION** — a bank that can be resident lacks the entry, but its own
  ROM bytes there open like a function prologue. That is either a function the
  split missed (an indirect-only entry, which a disassembler cannot find) or a
  bank the port has no record for. **The shortest list to act on.**
* **UNRESOLVABLE** — no compiled module's *code* covers the address at all.
* **BANK SELECTION** — the missing bank's bytes there are a body interior: the
  caller disposed of another bank's address. Which bank of an arena the game
  streams is scene data, so a run confirms these.
* **INERT** — the missing bank's segment-table record is never loaded by a scene
  that also loads the caller's record, so it cannot be resident when the call
  runs (the scene-descriptor record-mask model from `cross_bank.py check`).
* **RSP IMEM** — `0x84000000..`: recompiled microcode text the CPU never runs;
  `get_function` accepts the window, so it would log as a stub.

Current answer (session 83): **985 static dispatch targets, 0 definite, 0
unresolvable, 129 missing-function candidates, 152 bank-selection candidates, 369
inert, 332 resolvable** — i.e. every static `jal` target is registered by *some*
compiled module, so the remaining whack-a-mole is `jalr` (288 indirect sites,
which no static pass can name), a bank the port has no record for, or a load that
missed `on_streamed_dma`. `log` mode covers all three.

**`log` mode is the arbiter for a real hit.** The runtime already prints the live
first words at a stub address (`[overlays] @0xADDR: …`); the tool matches them
against every candidate module's ROM image, so it names the module that was
*actually resident*, and then — using the run's own `loading overlay record`
lines — says which of the two bugs it is:

* the resident module **does** register the address → the bank was up but its load
  never matched `kBankRecords` (`UNKNOWN module`), or it was registered and then
  evicted by a later overlapping load (the tool says which, from the log);
* it **does not** → the caller dispatched an address that does not exist in that
  bank's layout, and the tool names the body it landed in.

Worked example (the session-73 settings-menu stub, `ogre-s73-r5.log`):

```
0x80226B7C  1 hit(s)  status: partial
    live words: 27BDFFE824020029AFBF0014AFB00010
    RESIDENT: bankRec16b (unit AG, record 16) rom=0x279FF0 ram=0x802258B0 -- bytes
      match its ROM image, it registers this address
      the run has NO `loading overlay record` line for it: the DMA never matched
      kBankRecords, so its load bypassed on_streamed_dma
    dispatched from func_80178130(0x80178130)  RecompiledFuncs/funcs_11.c:8321
```

The log's own `UNCOMPILED streamed record 16` line says the port had no record
for it that day; unit AG is that record, and the byte match proves it was the
resident module. **A stub whose bytes match nothing compiled is a bank the port
still has no record for** — the tool then searches the ROM for those bytes and
names the record they belong to, which is the unit to add. The same day's
`enc2.log` (session 82's pre-fix neutral encounter) reads the mirror image: 18
records registered, none of them `bankRec10s`'s `0x23A370 → 0x801D0860`, and the
stub at `0x801D1508` matches that record's own bytes.

### `tools/recompcov.py` — how complete is the recompilation? (session 83)

```sh
tools/recompcov.py                       # the offline measurement (~20 s)
tools/recompcov.py --no-scan             # fast: modules + dispatch only
tools/recompcov.py --log /tmp/run.log    # + what a run executed (OGRE_COVER=1)
tools/recompcov.py --json
```

There is no single "recomp %", because the question has four answers with
different certainty. This tool reports all four and says which is which:

| measure | value today | how |
|---|---|---|
| **code bytes** | **99.05%** — `0x2B11D0` of the code span `0x001000..0x2B8B70` | the ELFs' `CODE` `CONTENTS` sections vs the ROM; the one `0x6990`-byte gap (`0x0DDF80..0x0E4910`) classifies as data |
| **modules** | **28/28** loader-pattern arenas have a unit; 44 bank records across 34 units + 10 base sections | `tools/arenamap.py`'s scan + `app/src/bank_funcs.inc` |
| **dispatch** | **985 static targets, 0 unresolvable**; 288 `jalr` sites unmeasurable statically | `tools/stubmap.py`'s model |
| **execution** | per run, from `OGRE_COVER=1` | the runtime prints every recompiled function it entered; the tool joins that with the registered set |

`OGRE_COVER=1` is the one piece that needs the game. The runtime already kept a
per-function entry counter for `OGRE_PROFILE`'s hot list, but only ever printed
the per-second top 8; the counters' *keys* are never cleared, so their occupied
slots are exactly the set of functions the run entered. `OGRE_COVER=<path>` dumps
that set at exit — or when the window is closed, or on demand from the live
console's `cover` — without starting the sampling thread.

**The measurement is a session played by hand**, not a scripted run:

```sh
OGRE_COVER=/tmp/cover.txt ./build-app/ogrebattle64 assets/ogre64.z64
# … play as long as you like, then close the window …
tools/recompcov.py --log /tmp/cover.txt
```

Scripting it is a trap this session hit twice: taps drive the game to one screen
and then stop, so the window sits idle, and `OGRE_EXIT_AFTER_MS` then kills a
human's play mid-session. What it answers is *which parts of the port this route
executed* — and the modules at 0% are the untested surface, which no offline
measure can see.

### Adding a streamed bank unit (the session-67 recipe)

When a scene reaches RAM the port has no code for, the run says so — take it in
this order, because each step is cheaper than the next and the later ones
mislead if the earlier one is skipped.

1. **The port names the missing module by itself.**
   `[bank] UNKNOWN module rom=0x… ram=0x…` (a *segment-table* record the port
   has no unit for) or `[overlays] streamed function stub called @ 0x…` (a
   `LOOKUP_FUNC` onto an address nothing registered). Run it, read the address,
   and only then look for the DMA.
2. **Add the unit.** `config/banks/config-bank<U>.yaml` (splat: `type: code`, `start`/`vram`,
   `[start, asm]` + `[<code end>, data]`, `symbol_name_format: "ovl<U>_$VRAM"`)
   and `config/banks/config-bank<U>.toml` (N64Recomp), then `BANK_UNITS += <U>` in the
   `Makefile`. A **bank's size is its DMA size**, and the code/data boundary is
   its last `jr ra` **rounded up to a 16-byte boundary** — otherwise the
   assembler pads `.text` and `make bank-recomp`'s ELF check fails loudly (the
   session-65 trap; that is exactly what it is for).
3. **Give records their BSS.** If the segment table's `ram_end` word is past
   `ram start + rom size`, add the record to `RAM_END` in
   `tools/gen_bank_funcs.py`; the runtime then zeroes it on load as the game's
   loader does.
4. **Declare cross-record function entries.** `config/symbols/symbol_addrs-bank<U>.txt`
   (`name = 0xADDR; // type:func`, wired with `symbol_addrs_path` in the splat
   config) for every address the game *calls* that the target record never `jal`s
   from inside itself. Each record is disassembled as its own segment, so such an
   address is emitted as a label inside the preceding body, N64Recomp has no
   entry there, and `LOOKUP_FUNC` becomes the **no-op** streamed stub
   (`get_function` has no interior fallback). Derive the list mechanically —
   every `LOOKUP_FUNC(0x…)` in `Bank<U>Funcs/` whose target is inside the unit's
   own RAM ranges and is not already a `recomp_trace_entry`. Do **not** try a
   subsegment split at the same address: the assembler's 16-byte `.text` padding
   breaks the ELF-vs-ROM identity.
5. **Watch for a bank the trace cannot see.** `OGRE_DMA_TRACE=1
   OGRE_DMA_TRACE_FULL=1` names the transfers, but its own overhead changes which
   path the run takes — in session 67 it showed one arena bank and the clean runs
   loaded a different one. A one-line probe in `recomp::do_rom_read` (log reads
   whose destination is the arena, or whose source is the ROM region) is
   lighter and found the second bank immediately.
6. **Fix the binding, not the symptom.** If a call runs the wrong bank's body,
   the unit that *contains* the target record is the problem: move the target
   record to a unit that does not call it, so the caller compiles as
   `LOOKUP_FUNC`. `make cross-bank-check` is the audit, and
   `tools/cross_bank_known_hazards.txt` is the explicit allowlist of accepted
   pre-existing hazards (a *new* one still fails `make bank-recomp`).

### `tools/watch.sh <guest-addr>` — who writes this address?

```sh
OGRE_SPEED=4 OGRE_TAP_MS=1500 OGRE_TAP_NOT_SCENE=new-game,0x0D \
  tools/watch.sh 0x80243E28 --after func_80178920 --ignore 1 --hits 3
```

Arms an lldb watchpoint on `rdram + (guest - 0x80000000)` after a given guest
symbol (skip its first `--ignore` hits to pick a later visit), and prints a
backtrace per write.

`--value <n>` makes it conditional: it keeps running until the watched word
holds `<n>`, which is how a *specific* write is caught on an address that is
written many times with different values (session 48 used
`tools/watch.sh 0x800C4BB8 --value 0x80000400 --hits 2 --size 4` to catch the VI
manager writing a placeholder into the display word, with the caller chain
`func_8007307C ← func_80089540`).

The app, the recompiled code and `librecomp`/`ultramodern` are built with
`-fno-omit-frame-pointer` (RT64 is not), because the runtime's shadow *guest*
call chain is empty when a fault happens inside runtime code reached through a
bridge. With frame pointers a breakpoint in the runtime unwinds across the
bridge to the guest caller:

```
frame #0  do_recv            frame #2  osRecvMesg_recomp
frame #1  osRecvMesg         frame #3  func_80088F08 + 717   <- the guest caller
```

(`bt` at a *signal* stop can still print only the faulting frame — if the crash
is a `SIGFPE`/`SIGBUS` inside the runtime, break on that function instead:
`lldb -b -o "breakpoint set -n do_recv" -o run -o "bt 14" -- ./build-null/ogrebattle64`.)
