#pragma once

// Battle figures on demand (battle_figures.cpp,
// docs/anotherworld/design-battle-transform.md section 7.1).

#include <cstdint>

#include "recomp.h"

namespace ogre::battle_figures {

// Makes sure the battle's figure cache holds the figure for one class, loading
// it with the game's loader func_ovlC_801C41E8 when it is missing. `side`,
// `alt` and `variant` are the participant's flag bit 8, flag bit 10 and
// equipment variant (func_8016FA54), the values the battle setup passes.
// `handle` is the figure's handle when the caller knows it (0 when not): with
// it, a cached figure returns at once. Called on the game thread from a hook
// with that hook's context; every register is restored. 1 when the figure is
// in the cache, -1 when it is not and could not be loaded (a full cache, out
// of memory, a request already refused in this battle).
int ensure(uint8_t* rdram, recomp_context* ctx, uint32_t cls, uint32_t key, uint32_t side, uint32_t alt,
           uint32_t variant, uint32_t handle);

// A fight's computation is over (battle_form.cpp's compute-end hook): the
// next fight may try every figure again.
void new_fight();

}  // namespace ogre::battle_figures
