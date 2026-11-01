#pragma once

namespace ogre {

// Applies the FRAME RATE setting to RT64's refresh rate.
//
// `Original` is always RT64's `RefreshRate::Original`. `Display` is
// `RefreshRate::Display` while the dispatcher runs the mission scene (`0x03`)
// and `Original` elsewhere, and while it is on, the RT64 patch tags the field's
// camera (`rt64_ogre_set_field_tags`, patches/rt64-anotherworld.patch). See
// docs/anotherworld/notes-frame-interpolation.md.
//
// Called once per frame from the app's `update_gfx` callback, beside
// `widescreen_update`. It writes the live `GraphicsConfig` only when the wanted
// `rr_option` differs, so a steady scene costs one comparison per frame.
void frame_rate_update();

}  // namespace ogre

