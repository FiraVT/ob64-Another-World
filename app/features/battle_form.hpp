#pragma once

// Battle forms (battle_form.cpp, docs/anotherworld/design-battle-transform.md).

#include <cstdint>

#include "recomp.h"

namespace ogre::battle_form {

// The participant's strikes this battle: a rule's "attacks" for its class,
// else `count`. battle_ai.cpp's ogre_battle_attack_count calls it.
uint32_t attack_count(uint8_t* rdram, uint32_t participant, uint32_t count);

// The attack for the participant's strike: the rule's "attack", else `attack`.
// `picker`: the call comes from an attack picker, which runs before the strike
// counter +0x6C advances, so the strike is +0x6C; otherwise +0x6C - 1.
// `ctx` (may be null) lets a picker look up the form's attack for the row when
// the rule gives none.
uint32_t attack(uint8_t* rdram, recomp_context* ctx, uint32_t participant, uint32_t attack, bool picker);

}  // namespace ogre::battle_form
