#pragma once

// Battle labels (battle_labels.cpp): a word or an image the port draws over a
// fighter during the battle's playback, such as a damage over time effect's
// "label" (content_db.hpp Dot).

#include <cstdint>
#include <filesystem>
#include <string>

namespace Rml {
class Context;
}

namespace ogre::battle_labels {

// The effect kind a label's event 0x36 carries (battle_dot.cpp schedules it,
// the playback hook takes it); the game's own kinds stay below 0x20.
constexpr uint32_t kLabelKind = 0xF0u;

// A fight's computation starts: the labels of the last fight are dropped.
void new_fight();

// Registers a label for this fight and gives its number, the value its event
// carries; -1 when the fight already has the most labels.
int add(const std::string& text, uint32_t rgb, const std::string& image);

// The RmlUi view (rml_ui.cpp): loads battle_labels.rml, and each frame shows
// the labels the playback has reached, rising and fading over about a second.
bool init(Rml::Context* context, const std::filesystem::path& asset_dir);
void update();

}  // namespace ogre::battle_labels
