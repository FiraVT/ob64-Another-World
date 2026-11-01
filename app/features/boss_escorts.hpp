// Boss escorts (app/features/boss_escorts.cpp): a mission boss's unit fights with up
// to two members beyond the five its unit record holds.
#pragma once

#include <cstdint>

namespace ogre::boss_escorts {

constexpr int BOSS_SLOT = 30;

// How many extra members (beyond the unit record's five) the enemy unit in
// `unit` fights with: 0 when ENEMY RANKS is off, the unit has none, or the
// mission's field has not run yet. They are made on the field update; this
// only reads the count, from any thread.
int extra_members(const uint8_t* rdram, int unit);

}  // namespace ogre::boss_escorts

