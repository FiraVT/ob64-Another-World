#pragma once

// Camera zoom on the mission field (scene 0x03) and the world map (scene 0x05),
// a port engine feature (docs/anotherworld/design-camera-zoom.md).
//
// Input: the mouse wheel, and on a pad the right stick up/down while the left
// trigger is held. A wheel step zooms on the point under the pointer; the pad
// zooms on the screen centre. The CAMERA ZOOM option (CONTROLS tab, on by
// default) turns it off; off returns both maps to the game's own view. The zoom
// level persists for the session, one level per map.
//
// Mission field: `func_ovlN_801BEF74` (bank N, called every frame at
// 0x801AF138, right after the scroll `func_ovlN_801BECF0`) places the eye at the
// look-at point plus R * trig(angle) in y and z, where R is the f32 at
// 0x8018F598 that the mission set-up writes once (bank A 0x8019F18C, map scale
// x 78.2). The zoom scales R at game event 4 (the scroll's exit, 0x801BEF60),
// so the eye moves on the same frame. The game's projection `func_801A9DD0`
// reads the moved camera, so unit picking and the cursor follow it.
//
// World map: `func_ovlM_801A103C` (bank M) draws the map layer under the
// scissor (32,32)-(288,217): 25 terrain strips from the CI8 image at
// `state[+0x00]` + 0x200 (512 texels wide; scroll `state[+0x40]` 0..256,
// `state[+0x44]` 0..124), then labels, markers and the party as TEXRECTs. Three
// hooks (config/hooks.toml) replace the strips with port strips that cover the
// zoomed view, scale the sprite TEXRECTs about the frame centre, and move the
// cursor sprite. `map_cursor.cpp` maps the pointer through the inverse.

#include <cstdint>

namespace ogre::camera_zoom {

// The wheel turned `steps` notches (positive away from the player, which zooms
// in) with the pointer at framebuffer pixel (fb_x, fb_y) of the 320x240 picture.
// Returns true when a map that zooms is up and the wheel was used for the zoom;
// false leaves the event to its other users (the D-pad, the army strip).
// Platform thread.
bool wheel(int steps, float fb_x, float fb_y);

// The left trigger and the right stick's y (-1 up .. 1 down), from the pad poll.
// Returns true when the stick is zooming, so the poll does not also press
// C-UP/C-DOWN with it. Game thread (`get_input`).
bool pad(float left_trigger, float right_y);

// The world map's view, for map_cursor.cpp. `zoomed()` is false at 1x, where
// the map is the game's own picture and the two functions are identities.
bool map_zoomed();
// A framebuffer pixel on the zoomed map as the game's unzoomed screen pixel.
void map_unzoom(float fb_x, float fb_y, float& game_x, float& game_y);

}  // namespace ogre::camera_zoom

// Bank M hooks in func_ovlM_801A103C (config/hooks.toml). Game thread.
extern "C" void ogre_map_zoom_terrain(uint8_t* rdram);       // 0x801A1860, after the terrain loop
extern "C" void ogre_map_zoom_sprites(uint8_t* rdram);       // 0x801A18F8, after the map layer's sprites
extern "C" void ogre_map_zoom_cursor_begin(uint8_t* rdram);  // 0x801A1C4C, before the cursor sprite
extern "C" void ogre_map_zoom_cursor_end(uint8_t* rdram);    // 0x801A1C58, after the cursor sprite
