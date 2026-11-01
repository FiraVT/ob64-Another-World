#pragma once

// Mouse selection and orders on the mission field (docs/anotherworld/design-field-controls.md
// phases 2-3): left click selects one of the player's units (Shift adds or
// removes), a left drag selects a box, right click orders the selection (an
// enemy: pursue it; ground: move there), Backspace clears. A left click that
// hits nothing is handed back to the game as its own `A`, so its menus keep
// working. Orders run on the game thread through the game's own routines.

#include <SDL.h>

#include <cstdint>
#include <string>
#include <vector>

namespace ogre::field_select {

// Called for every SDL event before the port's other mouse handling. Returns
// true when the event was used here and must not reach the game.
bool handle(const SDL_Event& event, SDL_Window* window, bool panel_owns_mouse);

// The FIELD CONTROLS setting (settings.hpp -> Choice::FieldControls): MODERN,
// the default, gives the mouse on the field to this overlay. Mouse buttons and
// the wheel never reach the game, its cursor no longer follows the pointer, the
// field scrolls only with the pointer at the window's edge. Right click with
// nothing selected is still the game's B, and R still opens the game's menu
// (Dispatch). CLASSIC keeps the game's own cursor and menus, with the overlay's
// selection on top.
bool modern();

// True in MODERN mode on the mission field (scene 0x03).
bool owns_field();

// True once after the game's Settings screen was closed for the port's menu
// (main thread; sdl_platform.cpp then opens the GAMEPLAY tab).
bool take_open_settings();

// get_input's last step: in MODERN mode on the field, adds the edge-scroll
// D-pad bits.
uint16_t filter_buttons(uint16_t buttons);

// The pointer in framebuffer pixels (false before it has been over the field).
bool pointer(float& fx, float& fy);

// The army strip's scroll offset in pixels (mouse wheel over it, Page Up and
// Page Down); field_view clamps it to the strip's height and writes it back.
int strip_scroll();
void set_strip_scroll(int pixels);

// Opens the game's Organize screen from the field through the R menu's own
// entry (the Organize button, F3 on the field in MODERN mode).
void open_game_organize();

// The Dispatch list (the top bar's Dispatch button): whether it is open,
// whether a unit slot can be sent out, how many units are out, and how many
// may be (20; the game's own 10 is raised by config/hooks.toml).
bool dispatch_open();
bool can_dispatch(int slot);
int units_out();
int field_limit();

// Main thread, once per event pump: PAUSE ON SELECT (settings.hpp).
void update_pause();

// The enemy slot `slot` is chasing (the overlay's chase), or -1.
int chase_target(int slot);

// Whether the Group Items list is open (the I key or the Use item button).
bool items_open();

// The selected unit slots, for the overlay.
std::vector<int> selected();

// The drag box in framebuffer pixels while a drag is in progress.
bool drag_box(float& x0, float& y0, float& x1, float& y1);

// The pad on the MODERN open field (field_select.cpp, "The pad on the MODERN
// open field"). claimed_pad_buttons() is a mask of `1 << SDL_GameControllerButton`
// that must press nothing in the game; it is read on every input poll.
uint32_t claimed_pad_buttons();
// X held: the selection circle, in framebuffer pixels, around the game's cursor.
bool pad_circle(float& x, float& y, float& radius);
// The overlay button the pad's command menu (Y) has in focus, or "".
std::string pad_focus();
bool pad_menu_open();

}  // namespace ogre::field_select

