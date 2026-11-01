#pragma once

// On-screen alerts: short messages that stack at the top right of the game and
// fade after a few seconds (docs/anotherworld/design-war-director-v2.md 5.2 and 8). Any
// thread may post; app/features/alerts_view.cpp draws them with RmlUi.

#include <string>
#include <vector>

namespace ogre::alerts {

enum class Kind { Info, Threat, Good };

struct Alert {
    std::string text;
    Kind kind;
    double age;  // seconds since it was posted
};

void post(Kind kind, std::string text);

// The alerts younger than `lifetime` seconds, oldest first.
std::vector<Alert> recent(double lifetime);

}  // namespace ogre::alerts

