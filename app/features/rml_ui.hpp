// RmlUi drawn by RT64 over the game picture: the engine for the port's own
// game screens (docs/anotherworld/design-organize-screen.md, section 4). See rml_ui.cpp.
#pragma once

#include <filesystem>

namespace ogre::rml_ui {

// Registers the RT64 render hooks. Call before the renderer is created.
// `asset_dir` holds the documents (`field.rml`, ...) and `fonts/`.
// `OGRE_RMLUI=0` leaves RmlUi off.
void install(const std::filesystem::path& asset_dir);

// Input, from the main thread; replayed on the render thread. Positions are in
// the swap chain's pixels.
void mouse_move(int x, int y);
void mouse_button(int button, bool down);
void mouse_wheel(float delta);

// The arrows move focus to the nearest focusable control in that direction
// anywhere in the focused document, across panels and into and out of lists
// (RmlUi's own navigation stays inside the nearest scroll container, which
// left the Organize view's side panels and row buttons out of reach of the
// pad). In a text field the keyboard's left and right move the caret; the
// pad's (`pad`) move focus.
enum class NavKey { Up, Down, Left, Right, Confirm, Back, Next };
void nav_key(NavKey key, bool pad = false);

}  // namespace ogre::rml_ui

