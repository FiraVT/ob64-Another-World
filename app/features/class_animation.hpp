#pragma once

// The class animation resource for new classes (docs/anotherworld/
// design-new-classes.md section 7).
//
// Battle animation comes from asset 0x00315736 (LordlyCaliber: "animation
// routes"), whose class-indexed sections have entries for class ids 0..171.
// When packs make new classes, the asset loader's size and load functions
// (func_8009DAF4, func_8009DBB8; config/hooks.toml) hand every one of the
// game's four loaders an extended copy: each class-indexed section grows to
// 256 entries, the header offsets and selector pointers follow, and each new
// class's entries are copies of its template class's. With no new classes the
// game gets the original bytes.

#include <cstdint>
#include <utility>
#include <vector>

namespace ogre::class_animation {

// The new classes, as (id, template class) pairs. The content database calls
// this after the packs apply; the next load of the asset uses it.
void set_new_classes(std::vector<std::pair<uint16_t, uint16_t>> classes);

// A new class's battle descriptors: in its four handles (slot 0 of the
// resource), a handle naming descriptor `from` names `to` instead (both the
// descriptor table index + 1, the handle's low 12 bits). class_art.cpp sets it
// for a class with its own battle palettes.
void set_handle_override(uint16_t cls, uint16_t from, uint16_t to);

// The extended resource for `classes` built from the ROM's `original`, or an
// empty vector when the layout is not the expected one. Exposed for the
// offline check.
std::vector<uint8_t> build(const std::vector<uint8_t>& original,
                           const std::vector<std::pair<uint16_t, uint16_t>>& classes);

}  // namespace ogre::class_animation
