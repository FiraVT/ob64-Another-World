#pragma once

// Class rules: the hook points for the class change alignment limits and the
// places a large class takes in a unit.
//
// The rules themselves are the Another World mod pack's
// (content/another-world/, docs/anotherworld/design-another-world-pack.md):
// the class and attack record changes are its pack class-rebalance.json, and
// "no alignment limit" and "large units take one square" are its script
// class-rules.lua, through the Lua filter events class_alignment and
// unit_size (lua_runtime.hpp). With the pack off the game's own rules stand.
//
//   class records   0x80187C34 + c*0x48
//                     +0x04 size (2 = large; kept at 2, see tick)
//
// This file keeps the parts that are not rules: large classes stay size 2 in
// the table, and the Organize screen draws a class the ROM gives size 2 with
// its large picture.

#include <cstdint>

namespace ogre::class_rebalance {

// Called once per frame on the main thread. Keeps the large classes at size 2.
void tick(uint8_t* rdram);

// True for a class the ROM gives size 2 (seen by tick). The Organize screen's
// pictures use it (ogre_display_kind).
bool original_large(int cls);

// A class change's alignment limit byte after the class_alignment filter;
// 0xFF means no limit.
uint32_t alignment_limit(uint32_t requirement);

// The places a class of `size` (1 or 2) takes in a unit after the unit_size
// filter.
int unit_places(int size);

}  // namespace ogre::class_rebalance
