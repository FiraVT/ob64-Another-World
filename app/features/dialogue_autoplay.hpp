// AUTO-PLAY DIALOGUE (settings.hpp -> Choice::DialogueAutoPlay): the game's
// message window advances by itself once its text is shown and a read delay has
// passed, and stops at a choice. See dialogue_autoplay.cpp for the mechanism and
// docs/anotherworld/notes-dialogue-autoplay.md for the addresses.
#pragma once

#include <cstdint>

namespace ogre::dialogue_autoplay {

// True while auto-play is on: the setting, or OGRE_AUTOPLAY ("0" off, anything
// else on), which wins over the setting.
bool enabled();

// Flips the setting (the F2 key) and returns the new state.
bool toggle();

// The N64 buttons auto-play is pressing now (0x8000 = A, or 0). get_input ORs
// this into controller 1, so the press goes through the game's own pad code.
uint16_t injected_buttons();

}  // namespace ogre::dialogue_autoplay

