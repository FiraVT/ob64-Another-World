# Mission-field frame interpolation (the Settings menu's "FRAME RATE")

Code: `app/features/frame_rate.cpp`, the FRAME RATE row in `app/src/settings.cpp` and
`app/src/ui.cpp`, and `RSP::ogreTagProjection` in `patches/rt64-anotherworld.patch`
(`tools/RT64/src/hle/rt64_rsp.cpp`).

## The setting

| value | effect |
|---|---|
| `ORIGINAL` (default) | RT64 `RefreshRate::Original` in every scene. This is what every build before the row did. |
| `DISPLAY` | RT64 `RefreshRate::Display` while the dispatcher runs scene `0x03` (the mission field), `Original` in every other scene. |

`settings.cfg` stores it as `frame_rate = original|display`.
`OGRE_FRAME_RATE=original|display` overrides it for one run.
`frame_rate_update()` runs once per frame from `update_gfx` in `main.cpp`,
beside `widescreen_update()`. It writes `GraphicsConfig::rr_option` only on a
change and prints one `[frame-rate]` line per transition.

The other scenes stay at the original rate because they are 2D screens, njpeg
backgrounds and movies. The setting was asked for the field only.

## How RT64 interpolates

With `RefreshRate::Display`, `rt64_workload_queue.cpp` sets `targetRate` to the
swap chain's rate. The present queue then draws extra frames between two game
frames. `GameFrame` (`rt64_game_frame.cpp`) pairs each projection and each
world transform of the current frame with one of the previous frame, and the
extra frames blend each pair.

Pairing depends on the transform's id (`TransformGroup::matrixId`):

- `G_EX_ID_AUTO`, the default for every transform the game does not tag: RT64
  pairs draw calls by a hash of combiner, other mode, geometry mode and
  triangle count, then pairs their world transforms by how close they are.
  A projection with `G_EX_ID_AUTO` is always paired with the previous frame's
  projection at the same index.
- An explicit id: a projection is paired only when the previous frame's
  projection at that index has the same id. Otherwise that projection is drawn
  without interpolation for that frame.

A game normally sets ids with the extended GBI (`gEXMatrixGroup`, opcode
`G_EX_MATRIXGROUP_V1`), which needs the extended GBI enabled by a hooked
`G_NOOP` and extra commands in the game's display list.

## What the field's display list does

Bank N's `func_ovlN_801C0D20` builds the field camera (instructions read from
`build/bankN.elf`):

- `0x801C0FC0` `jal 0x800921D0` (guPerspective) into a matrix slot, then
  `0x801C0FD8`..`0x801C0FF4` writes `G_MTX` `0xDA380007`: projection, load.
- `0x801C104C` `jal 0x80090CC0` (guLookAt) into the next slot, then
  `0x801C1054` writes `G_MTX` `0xDA380005`: projection, multiply.

The camera is therefore in the projection stack, and the model matrices
(`0xDA380003` load, `0xDA380001`/`0xDA380000` multiply in bank N's other draw
functions) carry only object placement.

Every matrix comes from a per-frame pool: the slot is
`*(0x800F9BE0)->+8 + 64 * (u16)0x800C4BE4`, and the halfword at `0x800C4BE4`
counts up as the frame allocates. A matrix's RDRAM address is therefore not
stable from one frame to the next and cannot serve as an id.

## The choice: an RT64-side tag on the projection load

`RSP::matrix` calls `ogreTagProjection` before it applies a `G_MTX`. While the
app has turned the flag on (`rt64_ogre_set_field_tags`, set by
`frame_rate_update` only in scene `0x03` with `DISPLAY`), each projection load
gets the id `0x0B640000 + n`, where `n` counts projection loads in the display
list. The field camera is the first load. A later load, such as an
orthographic 2D pass, gets its own id, so a frame whose passes differ in count
does not blend the camera into a 2D projection. Model matrices keep
`G_EX_ID_AUTO`.

Native hooks in the `tools/director_hook.py` style were not used, for three
reasons:

- A hook that emits `gEXMatrixGroup` writes extra commands into the game's
  display-list buffer (`*(0x800F9BA0)`). The buffer's size and headroom on the
  field are not measured, and an overflow corrupts the next allocation.
- The extended GBI must be enabled through a hooked `G_NOOP` with a free
  opcode, which is more guest-side state for one camera id.
- The only stable per-object key a hook could supply (a unit's struct
  address) would help the models only. The models are on RT64's AUTO matching
  either way until a field capture shows that matching fail.

The RT64 change writes no guest memory, runs only while the flag is on, and
`OGRE_FIELD_TAGS=0` turns it off for an A/B with `DISPLAY` still on.

## Not verified

Nothing here has been run on a mission. The code builds with
`tools/build.sh windows`. Things to check on a played mission, with FRAME RATE
set to DISPLAY on a display faster than 60 Hz:

- The camera pan (cursor scroll and edge scroll) is smooth.
- Units marching move smoothly, and none of them jumps, trails or swaps with
  another unit. That would be RT64's AUTO matching pairing the wrong
  transforms.
- A camera cut, such as jumping the view to another unit, may show as a fast
  sweep across the map for one game frame. RT64 interpolates a cut like any
  other camera move; the tag does not detect cuts.
- The HUD, the cursor and the menus over the field are unchanged.
- Battles. They run inside scene `0x03` (see `widescreen.cpp`), so DISPLAY
  stays on through them. Check the battle screen, its backgrounds and its
  effects for trails or flicker.
- Leaving the mission: the `[frame-rate]` line in the log shows the switch to
  `original`, and the next mission switches back to `display`.
- The same with WIDESCREEN on, since Expand also rescales the field's
  projection.
- `OGRE_FIELD_TAGS=0` against the default, if the camera misbehaves, to tell
  the tag from RT64's own matching.

If units jump or swap, the next step is per-unit ids. RT64's debugger
(developer mode) shows each transform's id and pairing for one frame.

