#pragma once

// Recruits (docs/anotherworld/design-new-classes.md section 8). A script asks
// for a new army character of a class at a level (game.recruit); on the next
// update of the mission field the port builds it with the game's own
// character builder func_ovlR_802150DC into a free army character record
// (0x80193BE0 + ref * 0x38): class and class key, stats rolled for the level
// (func_8016F13C), the class's gear, and a generated name (func_8016BD20). The
// new character stands in no unit; Organize lists it with the others.

#include <cstdint>

namespace ogre::recruit {

constexpr int MIN_LEVEL = 1;
constexpr int MAX_LEVEL = 50;
constexpr int MAX_QUEUED = 8;

// The request's own checks: a class id 1..254 and a level 1..50. Whether the
// class exists is the caller's check (the content database).
inline bool valid(int64_t cls, int64_t level) {
    return cls >= 1 && cls <= 254 && level >= MIN_LEVEL && level <= MAX_LEVEL;
}

// Queues one recruit. False when MAX_QUEUED are waiting.
bool queue(uint16_t cls, int level);

}  // namespace ogre::recruit
