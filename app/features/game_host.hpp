// The interface between this project's features (app/features/) and the base
// they run on (app/src/).
//
// A feature reaches the base through four things only:
//   * this header (folders, the Esc menu, the active scene, saving settings),
//   * the option table (options.hpp),
//   * the event table (game_events.hpp) and the hook table (config/hooks.toml),
//   * N64ModernRuntime's own API (ultramodern::get_rdram_base(), recomp.h,
//     LOOKUP_FUNC), which every N64Recomp-based project shares.
// Moving the features to another base means implementing this header there
// (app/src/game_host.cpp is the current implementation) and applying the hook
// table with that base's hook mechanism. docs/anotherworld/separation.md lists
// the uses that do not go through it yet.
#pragma once

#include <cstdint>
#include <filesystem>

namespace ogre::host {

// The folder that holds the saves, settings.cfg and screenshots.
std::filesystem::path pref_dir();

// The folder the executable is in (texture-packs/, ui/, roms/ sit beside it).
std::filesystem::path exe_dir();

// True while the Esc menu is open over the game.
bool menu_visible();

// Holds the game at its frame gate (the pause the Esc menu uses) while `on`,
// for PAUSE ON SELECT. Main thread.
void hold_game(bool on);

// The game's active scene id (0x03 the mission field, 0x0D a battle, 0x05 the
// world map), read from the scene word 0x800C4C26.
uint16_t scene();

// Writes settings.cfg; called after an option changes.
void save_settings();

}  // namespace ogre::host

