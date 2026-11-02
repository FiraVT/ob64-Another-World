# Camera zoom on the mission field and the world map

A port engine feature: the mouse wheel, or the right stick with the left
trigger held, zooms the mission field (scene `0x03`) and the world map (scene
`0x05`). Code: `app/features/camera_zoom.{hpp,cpp}`, four bank M hooks in
`config/hooks.toml`, and the pointer mapping in `app/src/map_cursor.cpp`.

## 1. What the maintainer chose (2026-11-02)

| question | answer |
|---|---|
| world map | the map layer zooms inside the stone frame; the frame and the date panel keep their size; zooming out shows more of the map |
| field range | 0.5x to 1.5x of the game's camera distance at first; after the first play-test, "the zoom in looks good but the zoom out needs to double": 0.5x to 3.0x |
| centre | the wheel keeps the point under the pointer fixed; the pad zooms on the screen centre |
| wheel conflict | the wheel zooms while the map or the open field has no game menu up; in a menu it still presses D-pad up/down |

Chosen without asking, to be confirmed in play: the zoom level persists for the
session, one level per map (a new mission or a new visit to the map starts at
the last level); CAMERA ZOOM (CONTROLS tab, ON by default) turns it off, and off
returns both maps to the game's own view. The world map's range is 0.5x to 2x;
at 0.5x the frame already shows the map's whole 512-texel width.

## 2. Mission field

`func_ovlN_801BEF74` (bank N, called every frame at `0x801AF138`, right after
the scroll `func_ovlN_801BECF0`) places the camera at `*(0x8018F5AC)`:

- `0x801BF274..0x801BF2D0`: eye z = look-at z + R * trig(θ), with R the f32 at
  `0x8018F598` and θ = `state[+0x10]` / 360 * 2π (`state` = `*(0x801AB870)` =
  `0x800E91D0`).
- `0x801BF2DC..0x801BF2F4`: eye y = look-at y + R * `*(0x8018F5A0)` * trig(θ).
- `state[+0x04]` is the game's own camera level 0..2 (C-UP/C-DOWN, pad profile
  `+0x18`/`+0x1A` at `0x801BEFE4..0x801BF054`). It sets θ's target
  (`*(0x801E91E8)`, `*(0x801E91EC)` or 0.6 * their sum), not R. Session notes
  that called `state[+0x10]` "camera scroll position" mean this angle.
- It runs only in field modes `0`, `1`, `0x1F` and `0x2A` (`*(0x801F0E00)`,
  `0x801BEF90..0x801BEFD4`).

R is written once per mission by bank A at `0x8019F18C` (map scale * 78.2), with
`0x8018F59C` (map scale * 92) and `0x8018F5A0` = `F59C / R`. Scaling R therefore
scales the eye's whole offset from the look-at point. The zoom writes
`base * scale` into R at game event 4 (`0x801BEF60`, the scroll's exit), so
`func_ovlN_801BEF74` uses it on the same frame. A value in R that the module
did not write is a new base (mission set-up).

Picking needs no change: the game's projection `func_801A9DD0` reads the camera
struct, and the field overlay and the game's cursor both go through it.

A wheel notch records the ground point P under the pointer (`unproject`, the
game's projection inverted by Newton's method). While the scale eases from s to
s', the look-at point moves to P + (s'/s)(look - P), and the eye's x moves with
it (the game recomputes the eye's y and z). Scaling the eye offset about the
look-at point by k shows P where P + (look - P)/k was shown, so this keeps P
in place.

## 3. World map

`func_ovlM_801A103C` (bank M, the scene hook) draws, when `state[+0x1C4]` <= 100:

1. The map layer's set-up, with the scissor `ED080080 00480364` =
   (32,32)-(288,217) at `0x801A1428`.
2. The terrain: 25 strips (`0x801A16BC..0x801A185C`), 0x60 bytes each: SETTIMG
   (CI8, width 512, `state[+0x00]` + 0x200, the TLUT being the first 0x200
   bytes), SETTILE, LOADSYNC, LOADTILE of texels
   `[state[+0x40], +256) x [state[+0x44] + 8i, +8)`, PIPESYNC, SETTILE,
   SETTILESIZE, PIPESYNC, TEXRECT at (32, 32 + 8i) with dsdx = dtdy = 1.0,
   E1, F1, PIPESYNC.
3. `func_ovlM_801A349C`, `func_ovlM_801A2D84` and `func_ovlM_801A2A7C`: labels,
   route dots, markers and the party, as TEXRECTs from the builder
   `func_ovlM_8019F83C` (E7, E4, E1, F1 = `04000400`, E7). No triangles.
4. A screen-space overlay (`0x801A194C..0x801A1B14`): an I8 image at
   `state[+0x2C]`, 256 wide, with no scroll term. It is left as drawn.
5. The scissor reset (`0x801A1B84`), a window when `state[+0x428]` is set
   (`func_ovlM_801A39B4`), and the cursor (`0x801A1C50`, the builder at
   `state[+0x52]`/`[+0x54]`), then the date panel.

The scroll is clamped to x 0..256 and y 0..124 (`0x8019B640..0x8019B684`), and
the visible window is 256x185, so the map the player can see is 512x309 texels.

The port keeps its own view: a magnification z and the map texel m shown at
the frame centre (160, 124.5). A game screen pixel x is shown at
160 + (x - 32 + scroll - m) * z. Three hooks act on the display list
(`*(0x800E9BA0)` is its head):

- `0x801A1860` (after the terrain loop): when the view differs from the game's,
  the 25 strips become one `G_DL` call into port strips at `0x80440000`
  (three 32 KiB buffers in RAM the game never allocates; the heaps end at
  `0x80400000`), followed by G_NOOPs. The port strips are the game's own first
  strip with the LOADTILE, SETTILESIZE, TEXRECT, E1 and F1 words changed, one
  per 8 rows and 256 columns of the zoomed view.
- `0x801A18F8` (after the sprites): every TEXRECT since the terrain is moved and
  scaled (dsdx and dtdy divided by z, s and t corrected for the rounding and for
  a rectangle clamped at 0).
- `0x801A1C4C` / `0x801A1C58` (around the cursor): the same for the cursor.

The view's centre is constrained so the zoomed picture never needs a sprite the
game did not draw: zoomed in, the view stays inside the game's 256x185 window;
zoomed out, it contains that window; both stay inside the 512x309 map when they
fit. At 1x both rules give the game's own centre, and the hooks leave the
display list alone.

Zoomed in, the view follows the game's cursor when the pointer is not placing
it (D-pad, stick), and the pointer within 12 pixels of the frame's edge pans it.
`map_cursor.cpp` maps the pointer back to the game's pixel
(`camera_zoom::map_unzoom`) and measures the edge push there, so a pointer on a
part of the map outside the game's window scrolls the game towards it.

## 4. Input

- Wheel: `sdl_platform.cpp` asks `camera_zoom::wheel` before the wheel presses
  the D-pad; the MODERN field overlay asks it for a wheel that is not over the
  army strip. It is used while the open field (mode 0) or the world map with its
  cursor up and no window (`state[+0x50]` set, `state[+0x428]` clear) ran
  within the last 250 ms. One notch is x1.12.
- Pad: `gamecontroller_buttons` passes the left trigger and the right stick's y.
  With the trigger past half on a map that zooms, the stick zooms (dead zone
  0.25, up to e^1.4 per second) and presses neither C-UP nor C-DOWN.

## 5. Test knobs

`OGRE_ZOOM_FIELD=<share>` and `OGRE_ZOOM_MAP=<magnification>` set the session's
starting zoom; `OGRE_CAMERA_ZOOM=0` turns the feature off for one run.

## 6. Open

- Checked by the maintainer's first try and by scripted screenshots (the
  "camera zoom" entry in `DECISIONS.md`); mouse picking at the far limits is
  not yet played.
- The field's far limit shows the ground outside the mission's world rect; the
  look-at point stays inside the rect.
- A world map window other than `state[+0x428]` (the `R` menu) may still let the
  wheel zoom; the maintainer's play-test decides whether that needs a rule.
