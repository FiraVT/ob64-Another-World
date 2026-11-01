#pragma once

// Draws ogre::alerts (alerts.hpp) at the top right of the game window with
// RmlUi (ui/alerts.rml). The list takes no input.

#include <filesystem>

namespace Rml {
class Context;
}

namespace ogre::alerts_view {

bool init(Rml::Context* context, const std::filesystem::path& asset_dir);

// Called once per present from app/features/rml_ui.cpp.
void update();

}  // namespace ogre::alerts_view

