#pragma once

// Damage over time in battle (battle_dot.cpp): a pack's "dots", and the Lua
// events battle_dot and battle_round_end.

#include <cstdint>

#include "recomp.h"

namespace ogre::battle_dot {

// The battle's compute pass is over (0x801EFDE0, battle_form.cpp's
// compute-end hook): the "round" ticks, then the battle_round_end event.
void compute_end(uint8_t* rdram, recomp_context* ctx);

// A status's roll landed on `target` (battle_status.cpp, before the game's
// immunity check): a fighter that already has it gives its effects a stack.
void status_landed(uint8_t* rdram, uint32_t attacker, uint32_t target, int status);

}  // namespace ogre::battle_dot
