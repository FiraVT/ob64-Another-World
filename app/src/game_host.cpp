// OB64: Another World's features reach this base through app/features/
// game_host.hpp; this file is that interface's implementation on lfarroco's
// port. A different base implements the same header.

#include "game_host.hpp"

#include "bank_overlays.hpp"
#include "launcher.hpp"
#include "overlay.hpp"
#include "settings.hpp"

namespace ogre::host {

std::filesystem::path pref_dir() {
    return resolve_pref_dir();
}

std::filesystem::path exe_dir() {
    return executable_directory();
}

bool menu_visible() {
    return overlay_visible();
}

void hold_game(bool on) {
    field_pause(on);
}

uint16_t scene() {
    return active_scene_id();
}

void save_settings() {
    write_settings_file();
}

}  // namespace ogre::host

