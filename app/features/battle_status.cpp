// Attack statuses: the status an attack gives after the hit, with a pack's
// "status_chance" and the Lua filter event battle_status (lua_runtime.hpp).
//
// Three of the battle bank's attack routines copy the attack record's bytes
// +0x06..+0x0D for each strike and roll, per target, first the hit, then the
// status: when +0x0A is 8 the status is +0x0B and the rule +0x0D switches on
// how it lands (content_db.hpp valid_status_rule). Each then joins at one
// branch with the result in $s1, which calls the immunity check
// func_ovlX_801F46FC(status, target) when it is set. config/hooks.toml calls
// ogre_battle_status at that branch, so a status chance and a script decide
// $s1 and the status before the immunity check. LordlyCaliber's names for the
// routines (hooks.json, revision 0 RAM + 0x20) are in brackets.
//
//   routine                         copy        +0x0A == 8   branch      hit  target  status    attack id         attacker
//   func_ovlX_801E9E9C (generic)    sp+0x28     0x801EAAA0   0x801EABF8  $s2  $s3     sp+0xE8   +0x7C of $s6      $s6
//   func_ovlX_801EC2D0 (combined)   0x801EC78C  0x801ECEA4   0x801ED000  $s2  $s3     sp+0x170  sp+0x227          last strike start
//   func_ovlX_801EF1A8 (pedra)      0x801EF1FC  0x801EF598   0x801EF6EC  $s7  $s4     sp+0xA8   byte 2 of *(sp+0x11C)  last strike start
//
// The generic routine resolves the ordinary attacks, the breaths among them;
// combined resolves joined attacks and pedra the Pedra items. The last strike
// start is battle_form.cpp's ogre_battle_form_strike (strike_started below).

#include "battle_status.hpp"

#include "battle_dot.hpp"

#include <atomic>
#include <cstdint>
#include <random>

#include "content_db.hpp"
#include "game_addresses.hpp"
#include "lua_runtime.hpp"
#include "recomp.h"

namespace {

std::atomic<uint32_t> g_striker{0};

bool roll(int percent) {
    static std::mt19937 random{std::random_device{}()};
    return std::uniform_int_distribution<int>(0, 99)(random) < percent;
}

}  // namespace

namespace ogre::battle_status {

void strike_started(uint32_t participant) {
    g_striker.store(participant, std::memory_order_relaxed);
}

}  // namespace ogre::battle_status

extern "C" uint32_t ogre_battle_status(uint8_t* rdram, uint32_t attacker, uint32_t attack, uint32_t target,
                                       uint32_t hit, uint32_t landed, uint32_t status_address) {
    attack &= 0xFFu;
    if (attack >= ogre::addr::kAttackCount) {
        return landed;
    }
    if (const std::optional<int> chance = ogre::content::status_chance(static_cast<uint16_t>(attack))) {
        landed = hit != 0 && roll(*chance) ? 1u : 0u;
    }
    if (landed == 0) {
        return 0;
    }
    if (attacker == 0) {
        attacker = g_striker.load(std::memory_order_relaxed);
    }
    gpr address = static_cast<gpr>(static_cast<int32_t>(status_address));
    const int64_t fighter = ogre::lua::battle_fighter(attacker);
    const int64_t victim = ogre::lua::battle_fighter(target);
    if (fighter >= 0 && victim >= 0) {
        const int64_t result =
            ogre::lua::filter("battle_status", {fighter, victim, attack}, MEM_BU(0, address), 0, 11);
        if (result == 0) {
            return 0;
        }
        if (ogre::content::valid_attack_status(result)) {
            MEM_B(0, address) = static_cast<int8_t>(result);
        }
    }
    // The status that lands; on a fighter that already has it, its damage over
    // time takes a stack (the game's immunity check refuses it next).
    ogre::battle_dot::status_landed(rdram, attacker, target, MEM_BU(0, address));
    return landed;
}
