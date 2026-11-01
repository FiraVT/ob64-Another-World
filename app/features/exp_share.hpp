#pragma once

// Unit-shared battle EXP.
//
// The game gives EXP in two ways (docs: func_ovlJ_801D5DFC and the battle
// interpreter's command 0x35 in func_ovlJ_801DB90C): a level-weighted pool
// shared by the surviving members at the end of a battle, and per-action EXP to
// whoever acted. This module makes the whole battle's EXP a unit pool:
//
//   1. When a battle starts (scene 0x0D with the battle bank, ROM 0x22A250,
//      resident; see exp_share.cpp), it records level*100 + EXP for each
//      named member of the player's unit (unit index byte 0x801976FC, the same
//      byte func_ovlJ_801D5DFC reads; unit record 0x80197210 + u*0x19,
//      members +0x02..+0x06; character record 0x80193BE0 + n*0x38, level
//      +0x13, EXP +0x35).
//   2. After the battle scene ends, it measures each member's gain, adds them,
//      and splits the total with the game's own level weighting,
//      max(1, unit average level - level + 8)^2, so lower levels get more.
//   3. It moves each member's EXP toward that share, inside the member's
//      current level (0..99). A level already gained is kept, so the game's
//      level-up and stat growth are never redone or undone.
//
// The result is written for a short window after the battle, the way the EXP
// Overflow mod does, so a late sync from the game cannot put the old value back.

#include <cstdint>

namespace ogre::exp_share {

// Called once per frame on the main thread.
void tick(uint8_t* rdram);

}  // namespace ogre::exp_share

