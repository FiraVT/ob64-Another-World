#pragma once

// The mission field overlay (docs/anotherworld/design-field-controls.md, phase 1): an army
// strip at the left of the screen with one tile per unit the player has out on
// the map: unit number, leader class, status, average alignment and HP. Shown
// only on the mission field (scene 0x03); it takes no input, so the game's own
// cursor keeps working underneath.

#include <filesystem>
#include <string>

namespace Rml {
class Context;
}

namespace ogre::field_view {

bool init(Rml::Context* context, const std::filesystem::path& asset_dir);

// Called once per present from app/features/rml_ui.cpp.
void update();

// A unit slot's last projected position in framebuffer pixels (320x240), or
// false when it is not on the map.
bool screen_of(int slot, float& x, float& y);

// The location whose marker is nearest (fx, fy) within 14 framebuffer pixels,
// with its world position, or -1.
int location_at(float fx, float fy, float& wx, float& wz);

// The id of the clickable overlay element at (nx, ny), given as fractions of
// the window, or "": "t<slot>" a strip tile, "s<slot>" a selection card,
// "cmd-..." a command button.
std::string hit_at(float nx, float ny);

// The pad's command menu: the overlay button next to `from` in a direction
// (dx, dy of -1, 0 or 1), from the same rectangles hit_at reads. `from` itself
// when nothing lies that way; the first button when `from` is not shown.
std::string hit_step(const std::string& from, int dx, int dy);

// Whether (nx, ny), fractions of the window, is over the army strip.
bool over_strip(float nx, float ny);

// The headquarters' world position (the location with state bit 0x10).
bool hq_point(float& wx, float& wz);

}  // namespace ogre::field_view

