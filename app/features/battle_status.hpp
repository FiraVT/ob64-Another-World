#pragma once

// Attack statuses after the hit (battle_status.cpp): a pack's
// "status_chance" and the Lua filter event battle_status.

#include <cstdint>

namespace ogre::battle_status {

// The participant whose strike starts (battle_form.cpp's strike-start hooks):
// the attacker the battle_status event names.
void strike_started(uint32_t participant);

}  // namespace ogre::battle_status
