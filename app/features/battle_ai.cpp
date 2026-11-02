// Battle attack choice and attack count: the hook points for battle forms
// (battle_form.cpp) and for scripts (the Lua filter events battle_attack and
// battle_attack_count, lua_runtime.hpp). The rules that used to live here,
// Faerie heals and the Centurion command, are the Another World mod pack's
// content/another-world/battle-tactics.lua.
//
// The battle's per-action routine func_ovlC_801C898C looks up the acting
// character's attack for its row with func_8016E250(class, key, row) at
// 0x801C89DC. config/hooks.toml inserts a call to ogre_battle_pick_attack
// right after that lookup returns, with the actor in $s0 and the attack id in
// $v0, and the routine continues with whatever id this returns.
//
// The participant's attacks for the fight are +0x70 (u32; +0x6C counts the
// attacks used, 0x801CC508). The game writes +0x70 from the class's row count
// (func_8016E358) at seven stores, and config/hooks.toml passes the value
// through ogre_battle_attack_count before each one: the round reset
// func_801AFE50 (0x801AFFA4), the row change (0x801C9808) and the five
// Princess passes, which store count + 1 for every member of a unit led by a
// Princess (0x801C9758, 0x801CAB90, 0x801CAFB8, 0x801CB4A0, 0x801CB82C).
//
// Battle structures (bank C helpers func_ovlC_801C9008, func_ovlC_801C8B88,
// func_ovlC_801C8E50): battle state *(u32*)0x801CE8DC, participant n at
// state + 0x1C4 + n*0xF8 (lua_runtime.cpp battle.fighter reads them).

#include <cstdint>

#include "battle_form.hpp"
#include "game_addresses.hpp"
#include "lua_runtime.hpp"

namespace {

// The most attacks a script may give a participant in one fight.
constexpr int64_t MAX_ATTACKS = 15;

}  // namespace

extern "C" uint32_t ogre_battle_pick_attack(uint8_t* generated_rdram, uint32_t actor, uint32_t attack) {
    attack &= 0xFFu;
    // A battle form rule's attack for this strike comes first (battle_form.cpp).
    if (const uint32_t form_attack = ogre::battle_form::attack(generated_rdram, nullptr, actor, attack, true); form_attack != attack) {
        return form_attack;
    }
    const int64_t fighter = ogre::lua::battle_fighter(actor);
    if (fighter < 0) {
        return attack;
    }
    return static_cast<uint32_t>(
        ogre::lua::filter("battle_attack", {fighter}, attack, 0, ogre::addr::kAttackCount - 1));
}

extern "C" uint32_t ogre_battle_attack_count(uint8_t* generated_rdram, uint32_t actor, uint32_t count) {
    // A battle form rule's "attacks" replaces the class's count (battle_form.cpp).
    count = ogre::battle_form::attack_count(generated_rdram, actor, count);
    const int64_t fighter = ogre::lua::battle_fighter(actor);
    if (fighter < 0) {
        return count;
    }
    return static_cast<uint32_t>(ogre::lua::filter("battle_attack_count", {fighter}, count, 0, MAX_ATTACKS));
}
