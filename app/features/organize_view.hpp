// The new Organize screens (docs/anotherworld/design-organize-screen.md), phase 2:
// read-only Formation and Character views drawn with RmlUi from live game
// memory. See organize_view.cpp.
#pragma once

#include <filesystem>

namespace Rml {
class Context;
}

namespace ogre::organize_view {

// Render thread (rml_ui.cpp's init hook): registers the "organize" data model
// and loads `organize.rml` from `asset_dir`.
bool init(Rml::Context* context, const std::filesystem::path& asset_dir);

// Render thread, every present: shows or hides the document and refreshes the
// data a few times a second while it is open.
void update();

// Main thread: F3 opens and closes the view. `open()` lets the platform keep
// game input away from the game while it is up.
void toggle();
bool open();

// Main thread: the pad's shoulder buttons step through the tabs.
void cycle_tab(int direction);

// The pad's B: closes the on-screen keyboard or an open picker and returns
// true, or returns false when nothing is open (the caller then closes the view).
bool back();
// The on-screen keyboard's pad shortcuts: "delete" (X), "space" (Y) and
// "done" (Start, renames). Nothing happens while the keyboard is closed.
void keyboard_key(const char* key);

}  // namespace ogre::organize_view

