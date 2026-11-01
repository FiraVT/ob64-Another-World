// ROM patches (`<pref dir>/patches/*.ips`); see rom_patch.cpp.
#pragma once

#include <filesystem>

namespace ogre {

// Applies every `.ips` file in `<pref_dir>/patches/` to the loaded ROM image, in
// file-name order. Called from the game's on_init callback, after the runtime
// has read the ROM and before any game thread runs.
void apply_rom_patches(const std::filesystem::path& pref_dir);

}  // namespace ogre

