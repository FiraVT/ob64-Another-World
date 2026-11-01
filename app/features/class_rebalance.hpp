#pragma once

// Class rebalance (docs/anotherworld/design-class-rebalance.md, Phase A).
//
// Raises the weak unit families by changing the game's own class and attack
// records. The changes are the content pack app/content/class-rebalance.json
// (content/ beside the executable), which the content database applies on top
// of the ROM's records; the database's bridge writes them into the tables in
// RDRAM every frame, so the change also survives the streamed segment being
// reloaded. Player and enemy units read the same tables, so the change applies
// to both.
//
//   class records   0x80187C34 + c*0x48
//                     +0x04 size (2 = large; kept at 2, see tick)
//                     +0x08 + 4*i: [0, base, growth, 0] for HP STR VIT INT MEN AGI DEX
//                     +0x37 + 2*row: attack id, attack count (front, middle, back)
//   attack records  0x8018AA9C + a*0x10 (ROM 0x6099C)
//                     +0x09 formula (0x01 physical, 0x07 magic)
//
// This file keeps the rules that are not content: large classes stay size 2,
// no alignment requirement for a class change, and the one-square Organize
// answers.

#include <cstdint>

namespace ogre::class_rebalance {

// Called once per frame on the main thread. Keeps the large classes at size 2.
void tick(uint8_t* rdram);

// True for a class the ROM gives size 2 (seen by tick before the one-square
// change). The Organize screen's pictures use it (ogre_display_kind).
bool original_large(int cls);

}  // namespace ogre::class_rebalance
