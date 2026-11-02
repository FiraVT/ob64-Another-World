# Mission-field controls: how the game moves the cursor and the camera

Research for the maintainer's report that the field's mouse control has
"clunk and friction" (2026-11-02). It records how the game's own cursor and
scroll work, what the port layers on top for the mouse and the pad, where the
friction comes from, and the options ranked by effect. Nothing here is applied
yet.

Instructions read from `build/bankN.elf`; settings from the maintainer's
`build-dist/settings.cfg` (`mouse = on`, `field_controls = classic`,
`select_pause = on`, `confine_pointer = on`, `frame_rate = original`).

## 1. The game's cursor mover, `func_ovlN_801BE4E0`

Called once per game frame from `0x801AF0DC` with `$f12` = 32.0. The cursor
state is `*(0x801AB870)` = `0x800E91D0`: `+0x06`/`+0x07` the s8 deltas,
`+0x08`/`+0x0C` the f32 screen position.

- **Stick** (`0x801BE524..0x801BE60C`): the s8 at `0x80196B0E` (x) and
  `0x80197204` (y). A value outside -4..4 is copied into the delta as-is and
  resets the D-pad ramp. The response is linear with a dead zone of 4.
- **D-pad** (`0x801BE574..0x801BE81C`, profile masks at `*(0x801F0CC0)`
  `+0x08..+0x0E`): a held direction counts `0x801E91E4` up to 10 (down by one
  per frame when released) and sets the delta to `counter * 80 / 15`, which is
  5, 10, 16 .. 53. The D-pad reaches full speed after 10 frames.
- **Integration** (`0x801BE924..0x801BE974`): `x += delta_x / 128 * 32`,
  `y -= delta_y / 128 * 32`. The D-pad moves the cursor 1.3 to 13.3 pixels a
  frame (320x240 framebuffer); a full stick moves it about 20.
- **Clamps**: the ground point under the cursor is clamped to the world rect
  (`0x801F0DD8..0x801F0DE4`) and projected back (`0x801BEA40..0x801BEB70`);
  then the screen position is clamped to x 40..280, y 30..210
  (`0x801BEB74..0x801BEC00`).
- **Edge push** (`0x801BEC04..0x801BEC70`): `s2` records whether a clamp
  changed the position. With no clamp the deltas are zeroed. With a clamp and a
  held input (`0x801F0DA8`), the scroll flag `0x801F3630` is set.
- **Centre** (`0x801BE830..0x801BE8E4`): the profile's `+0x1C` button (C-LEFT)
  in modes 0, 1 and `0x2A` pulls the cursor a third of the way to (160, 120)
  each frame and also sets the scroll flag.

## 2. The game's scroll, `func_ovlN_801BECF0`

Called after the mover (`0x801AF130`) with `$f12` = 8.5 * the map's scale
(`0x8018F834 + 12 * map`). It does nothing unless `0x801F3630` is set. When it
is set, it moves the eye and the look-at point by `delta / 128 * 8.5 * scale`
world units, clamped so the look-at stays in the world rect
(`0x801BEDC4..0x801BEF5C`).

So the game is an edge-push design. The cursor moves freely inside a 240x180
box, and the camera scrolls only while the stick or D-pad pushes the cursor
against that box's edge, at the stick's or the D-pad ramp's speed. The comment
on `pan_camera` in `field_select.cpp` ("moves the camera only while its cursor
moves") should say "while its cursor is pushed past the clamp".

## 3. Frame rate

`zoom.mkv` (30 fps capture, 2026-11-02) changes on every captured frame while
the zoom eases, so the field runs at 30 game frames a second or more. The
mover, the scroll, the port's cursor hook and the hand sprite all run at that
rate. FRAME RATE = DISPLAY interpolates the 3D camera through RT64; the hand is
a 2D sprite drawn by the game.

## 4. What the port adds for the mouse (CLASSIC)

`app/src/mission_cursor.cpp`, called at the mover's epilogue
(`0x801BECC8`, `config/hooks.toml`):

1. **Absolute position.** Every `SDL_MOUSEMOTION` stores the pointer's
   framebuffer pixel. Once per game frame the hook writes it over the game's
   cursor, clamped to 40..280 x 30..210. There is no scale or speed cap. The OS
   pointer is hidden over the game (`set_game_cursor_hidden`), so the hand is
   the only pointer the player sees.
2. **Edge band.** A pointer within 24 pixels inside the clamp box
   (`edge_direction_for`, `OGRE_MISSION_CURSOR_EDGE`) holds the N64 D-pad.
3. **Game takes over.** While any D-pad direction is held, the centre button is
   held, the mover moved the cursor, or a click's yield is running, the hook
   copies the game's cursor into the stored pointer position instead of the
   reverse (`game_driving`).
4. **Click.** A left click holds A for 8 input polls and starts a yield of 15
   game frames (`kYieldFramesDefault`, commented as "~0.25 s at 60 Hz"; at the
   field's 30 frames a second it is 0.5 s).

## 5. Where the friction comes from

Ranked by how much of the player's time each one affects.

1. **The hand stops following the mouse near the edges.** Items 2 and 3 above
   combine: in a 24-pixel band (a 64-pixel strip from each screen edge to the
   clamp) the D-pad is held, so the game moves the hand at the D-pad ramp
   (1.3 px/frame at first) and the hook copies that position over the mouse's.
   The mouse does nothing until the pointer leaves the band. At the world rect's
   edge nothing moves at all, which reads as a stop or a pause.
2. **The hand and the hidden OS pointer come apart.** Whenever the game drives
   (band, click yield, centring), the hand moves and the hidden OS pointer
   does not. The next mouse motion snaps the hand back to the OS pointer. With
   `confine_pointer = on`, the OS pointer can sit pinned on the window edge
   while the hand is elsewhere.
3. **Every click freezes the hand for half a second.** The yield is 15 game
   frames; the code comment assumes 60 Hz.
4. **Scrolling is slow to start and fixed in speed.** The edge scroll is the
   game's D-pad push: 10 frames (0.33 s) of ramp, then a fixed world speed per
   map. It does not follow how hard or how far the mouse pushes, and it looks
   slower when zoomed out because the speed is in world units.
5. **The hand lags the mouse by one to two game frames.** The position is
   written at 30 Hz and drawn by the game in its next frame. A mouse that a
   desktop draws at the monitor's rate will always feel ahead of it.
6. **MODERN's own edge pan is dead.** `field_select.cpp` reads the camera
   pointer at `0x8019F5AC`, which holds bank code (`0C01E4E3`, read from a field
   run on 2026-11-02). The camera is `*(0x8018F5AC)` (`80220FA0` in the same
   run). The pan returns early every frame.

The pad has one source of friction: the D-pad ramp is the only way to move
slowly, and the stick is linear with no response curve beyond the game's dead
zone of 4.

## 6. Comparison: console RTS controls (Age of Empires II DE on Xbox)

The console edition of Age of Empires II DE (2023) faced the same problem in
reverse: a mouse game moved to a pad. Its public description:

- The stick drives a cursor, and the second stick moves the camera directly.
- A selects; holding A grows a selection area; a double tap selects every
  visible unit of that type; LT adds to the selection.
- Commands go through a radial menu on RT instead of icons; the D-pad jumps to
  idle villagers, the town centre, military and support units.
- The design director said a cursor-only scheme "didn't feel right", so the
  team designed for the pad instead of emulating a mouse. Reviewers found the
  cursor's assistance sometimes moved the cursor off the single unit they
  wanted.

Ogre Battle 64 has the mirror image of that design. The camera is bound to the
cursor, as in a pad-first game, and the port emulates the pad with the mouse.
The friction in §5 comes from that emulation: the mouse is turned into a
D-pad at the edges and the game's cursor logic wins over the mouse whenever it
moves the cursor. A mouse-first scheme separates the two: the cursor is always
the mouse, and the camera moves on its own input.

## 7. Options

| # | change | effect | size |
|---|---|---|---|
| A | Edge push only when the pointer is at the window edge (with KEEP POINTER IN WINDOW), and the camera pans by itself there (the `pan_camera` mover, address fixed, run in CLASSIC too) at a screen-relative speed, with no D-pad held | the hand follows the mouse everywhere; scrolling starts at once and keeps the same on-screen speed at every zoom | medium |
| B | Never let the game's motion overwrite the mouse while the mouse is the input; when the game moves the hand (click recentre, C-LEFT), warp the OS pointer to the hand (`SDL_WarpMouseInWindow`) | the hand and the pointer never come apart; no snap-back | small |
| C | Count the click yield in milliseconds (about 150 ms) instead of 15 frames | the hand freezes for a short moment after a click | small |
| D | Fix `CAMERA_PTR` in `field_select.cpp` to `0x8018F5AC` | MODERN's edge pan works | one line |
| E | Draw the hand in the port at display rate, from the pointer, and hide the game's hand while the mouse drives | removes the one-to-two frame lag | large |
| F | Pad: a response curve on the left stick (fine aim near centre) and a port dead zone | slow, precise stick aiming without the D-pad | small |
| G | Pad, later: right stick pans the camera directly; a "jump to next idle unit" button | the AoE2-console split of cursor and camera | medium |

A, B and C remove items 1 to 4 of §5. They change how the field feels, so they
wait for the maintainer.

## 8. Built (2026-11-02)

The maintainer chose to keep CLASSIC as it is and to build the options into
PORT MENUS = MODERN, for the keyboard and mouse and for the pad. Everything
below acts only on the MODERN open field (field mode 0, no Organize view, no
Esc menu; `field_select::modern_field`).

| option | built as |
|---|---|
| A | `pan_camera` (FieldFrame) moves the camera from `pan_input`: the pointer within 6 points of the window's edge while the keyboard and mouse are in use, the cursor keys, the right stick. `CAMERA_PTR` is `0x8018F5AC` (D). Speed 1.2 camera distances a second, eased over 0.1 s, so the on-screen speed is the same at every zoom |
| B | `mission_cursor.cpp`: with the keyboard and mouse in use, the game's own motion (D-pad, centring, the click yield) never overrides the hand; the pointer places it every frame |
| C | not needed in MODERN: mouse clicks there go to the overlay, so the click yield is never armed. CLASSIC keeps its 15-frame yield |
| D | done (A) |
| E | the game's wand is the OS pointer (section 9) |
| F | `field_select::shape_stick`: radial dead zone 0.12, then `0.06 + 0.94 * t^1.8`, so the smallest push is just past the game's dead zone of 4 |
| G | the right stick pans (dead zone 0.2, same curve); the maintainer chose LT + D-pad for the C buttons (C-UP/C-DOWN camera angle, C-LEFT overview map, C-RIGHT scroll mode) and LT + right stick for zoom. Next and previous idle unit were already RB / LB |

Keyboard: the maintainer chose the cursor keys (WASD and the arrows, the
CURSOR rows of CONTROLS) to pan the camera. `field_select::keyboard_pan`
takes the keyboard's D-pad bits before they reach the game, so on the MODERN
open field the keys no longer move the game's cursor.

Measured: `OGRE_FIELD_PAN_FORCE=-1,0` (devtest scenario `field-pan`) moved the
look-at x from -4.7 to -9.3 in about 0.3 s in field mode 0. When an event
switches the field to mode `0x1E`, the game puts the camera back itself.

## 9. The wand as the OS pointer

The field cursor on open ground is a wand with a ring (16x16, drawn at 30 Hz).
`func_ovlN_801BBA40` (bank N) draws every form of the field cursor. Its only
caller is `0x801B0BF4` in `func_ovlN_801B0714`, with the cursor's pixel
(`0x800E91D0` +0x08/+0x0C) in `$f12`/`$f14`. The form is `*(0x801F0E08)`
(-1 draws nothing, `0x801BBB2C`). The function reads `$f12`/`$f14` only to
place rectangles, so moving them off-screen hides the drawing and changes no
state.

The wand form, from `OGRE_DL_DECODE` of a field frame with the cursor on open
ground (2026-11-02):

| rect | commands | combiner |
|---|---|---|
| 1 | SETTIMG CI 16b `0x80281A78`, LOADBLOCK 64 texels; SETTILE t0 CI4, line 1; SETTIMG `0x80281AF8`, LOADTLUT 15 | colour TEXEL0, alpha TEXEL0 x ENV (`FCFF9BFF FF34FE7F`) |
| 2 | SETTIMG `0x80282908` (RGBA16 mask, t0, line 4); SETTIMG `0x80282B38` (RGBA16 32x32, t1, masks 5, SETTILESIZE window moved per frame) | two-cycle: colour TEXEL1, alpha TEXEL0 (`FCFF9BFF FFFD7E38`) |

The addresses come from a sprite asset (`func_ovlN_80197E80` with
`0x0218E638`), so they can change between missions. Two hooks around the call
(`config/hooks.toml`, "mission cursor draw" and "mission cursor drawn") record
the display-list head (`0x800E9BA0`) before the call, and after it walk the
commands the call wrote with a small RDP state tracker. When the commands are
the two-rectangle wand, the hook composes one frame of it (rect 1, with rect
2's sheen where the mask is set) and records the form number. The image is
the game's own data, read at run time; nothing is shipped.

`sdl_platform.cpp` `set_wand_cursor` turns that image into an SDL colour
cursor, scaled by whole pixels to the game's size, with the hot spot at the
cursor pixel (the wand's tip, (0,0)). It is the pointer on the MODERN open
field while the keyboard and mouse are in use and the pointer is over the
game window. While it is, the draw hook moves the game's wand off-screen; the
other cursor forms (the brackets around a unit under the cursor) are still
drawn by the game.

Verified with devtest scenario `field-wand` (local): `OGRE_MISSION_CURSOR_LOG=1`
prints the captured wand's alpha map with `image=0x80281A78
sheen=0x80282B38 hot=(0,0)`; a tagged probe that forced the hide removed the
wand from the screenshot and nothing else (probe reverted, `probe-wand` grep
empty). The OS pointer itself needs a real mouse over the window, so it is
not checked by a script.

Sources for §6: [Age of Empires: Xbox controls lesson](https://www.ageofempires.com/learn-to-play/controlling-your-empire-gathering-resources-xbox/),
[Xbox Wire launch post](https://news.xbox.com/en-us/2023/01/31/age-of-empires-2-definitive-edition-on-console-available-now),
[Tom's Guide](https://www.tomsguide.com/features/age-of-empires-ii-xbox-controller),
[Mezha PS5 review](https://mezha.ua/en/reviews/oglyad-age-of-empires-ii-definitive-edition-dlya-playstation-5-302015/),
[Press Start](https://press-start.com.au/features/2023/01/31/age-of-empires-ii-definitive-edition-on-xbox-is-the-impossible-port-made-possible/).
