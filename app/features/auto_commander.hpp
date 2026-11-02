#pragma once

// The auto commander: an AI that commands the player's units on the mission
// field (docs/anotherworld/design-auto-commander.md). The player takes over at
// any time: AUTO COMMANDER off (the GAMEPLAY tab, F4, the top bar's button), or
// any order to a unit. A move, pursue or Return takes the unit until the order
// finishes; a Stop takes it until the unit card's AI button hands it back or a
// later order finishes; the AI button takes it until the button hands it back.
// A unit in the player's selection never receives an AI order.

#include <string>
#include <vector>

namespace ogre::auto_commander {

// The AUTO COMMANDER setting.
bool on();
// Flips the setting (F4, the top bar's button) and returns the new state.
bool toggle();

// The player gave `slot` an order (field_select.cpp, game thread): the AI
// leaves it alone until the order finishes, or with `stop` (a Stop) until it is
// handed back.
void player_ordered(int slot, bool stop);
// The unit card's AI button: hands the units back to the AI, or takes them
// from it when every one of them is already the AI's.
void toggle_units(const std::vector<int>& slots);
// Whether the AI commands `slot` (the setting on and the unit not taken back).
bool commands(int slot);

// The unit's current AI job in words for its card ("" when it has none).
std::string job_text(int slot);

}  // namespace ogre::auto_commander

