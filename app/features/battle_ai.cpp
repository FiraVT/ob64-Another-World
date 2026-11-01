// Battle attack choice (docs/anotherworld/design-class-rebalance.md, section 4.1).
//
// The battle's per-action routine func_ovlC_801C898C looks up the acting
// character's attack for its row with func_8016E250(class, key, row) at
// 0x801C89DC. config/hooks.toml inserts a call to ogre_battle_pick_attack
// right after that lookup returns, with the actor in $s0 and the attack id in
// $v0, and the routine continues with whatever id this returns.
//
// Faerie: its Throw a Kiss (attack 0x17) powers up allies. When an ally on its
// side (itself included) is below 70% HP, the Kiss becomes Healing (0x57), so the
// game's own heal targeting picks the most hurt ally. Otherwise the Kiss stays.
// Faerie heals once from the front and middle rows, so with its MEN it heals
// about 60% of what a Cleric's two back-row casts heal, and far less than a
// Priest's row heal.
//
// Battle structures (bank C helpers func_ovlC_801C9008, func_ovlC_801C8B88,
// func_ovlC_801C8E50):
//   battle state     *(u32*)0x801CE8DC
//   participant n    state + 0x1C4 + n*0xF8, n < 20, live while +0x48 != 0
//   flags            +0x40 (u32): bit 0 out of the fight, bit 8 enemy side
//   HP               +0x20 current, +0x22 maximum (u16)
//
// OGRE_FAERIE_HEAL=0 turns the swap off.
//
// Centurion command (developer, 2026-10-31): every Centurion (class 34 male,
// 35 female, 94 Europea) in the fight adds one attack to each Soldier (class 1)
// on its side, and the bonuses stack. The participant's attacks for the fight
// are +0x70 (u32; +0x6C counts the attacks used, 0x801CC508). The game writes
// +0x70 from the class's row count (func_8016E358) at seven stores, and
// config/hooks.toml passes the value through ogre_battle_attack_count before
// each one: the round reset func_801AFE50 (0x801AFFA4), the row change
// (0x801C9808) and the five Princess passes, which store count + 1 for every
// member of a unit led by a Princess (0x801C9758, 0x801CAB90, 0x801CAFB8,
// 0x801CB4A0, 0x801CB82C). A Centurion counts while flags bits 0 and 1 are
// clear, the test the Princess passes make for the leader. The participant's
// class is the low byte of +0x48 (the `lbu 0x4B` the count lookups read).
//
// OGRE_CENTURION_COMMAND=0 turns the bonus off.

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "ultramodern/ultramodern.hpp"

namespace {

constexpr uint32_t BATTLE_STATE = 0x801CE8DCu;
constexpr uint32_t PARTICIPANTS = 0x1C4u;
constexpr uint32_t PARTICIPANT_STRIDE = 0xF8u;
constexpr int PARTICIPANT_COUNT = 20;
constexpr uint32_t P_HP = 0x20u;
constexpr uint32_t P_HP_MAX = 0x22u;
constexpr uint32_t P_FLAGS = 0x40u;
constexpr uint32_t P_LIVE = 0x48u;

constexpr uint32_t ATTACK_FAERIE_KISS = 0x17u;
constexpr uint32_t ATTACK_HEALING = 0x57u;
constexpr int HURT_PERCENT = 70;

constexpr uint32_t CLASS_SOLDIER = 1u;
constexpr uint32_t CLASS_CENTURION_MALE = 34u;
constexpr uint32_t CLASS_CENTURION_FEMALE = 35u;
constexpr uint32_t CLASS_CENTURION_EUROPEA = 94u;

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

uint16_t rd16(const uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>((rd8(rdram, address) << 8) | rd8(rdram, address + 1));
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd16(rdram, address)) << 16) | rd16(rdram, address + 2);
}

bool in_rdram(uint32_t address) {
    return address >= 0x80000000u && address < 0x80800000u;
}

bool faerie_heal_enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("OGRE_FAERIE_HEAL");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

// True when a live participant on `actor`'s side is below HURT_PERCENT.
bool ally_hurt(const uint8_t* rdram, uint32_t actor) {
    const uint32_t state = rd32(rdram, BATTLE_STATE);
    if (!in_rdram(state)) {
        return false;
    }
    const uint32_t side = (rd32(rdram, actor + P_FLAGS) >> 8) & 1u;
    for (int n = 0; n < PARTICIPANT_COUNT; n++) {
        const uint32_t p = state + PARTICIPANTS + static_cast<uint32_t>(n) * PARTICIPANT_STRIDE;
        if (rd32(rdram, p + P_LIVE) == 0) {
            continue;
        }
        const uint32_t flags = rd32(rdram, p + P_FLAGS);
        if ((flags & 1u) != 0 || ((flags >> 8) & 1u) != side) {
            continue;
        }
        const int hp = rd16(rdram, p + P_HP);
        const int max = rd16(rdram, p + P_HP_MAX);
        if (max > 0 && hp * 100 < max * HURT_PERCENT) {
            return true;
        }
    }
    return false;
}

bool centurion_command_enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("OGRE_CENTURION_COMMAND");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

bool is_centurion(uint32_t cls) {
    return cls == CLASS_CENTURION_MALE || cls == CLASS_CENTURION_FEMALE || cls == CLASS_CENTURION_EUROPEA;
}

// Centurions on `actor`'s side that can still fight.
int centurions_on_side(const uint8_t* rdram, uint32_t actor) {
    const uint32_t state = rd32(rdram, BATTLE_STATE);
    if (!in_rdram(state)) {
        return 0;
    }
    const uint32_t side = (rd32(rdram, actor + P_FLAGS) >> 8) & 1u;
    int count = 0;
    for (int n = 0; n < PARTICIPANT_COUNT; n++) {
        const uint32_t p = state + PARTICIPANTS + static_cast<uint32_t>(n) * PARTICIPANT_STRIDE;
        if (!is_centurion(rd32(rdram, p + P_LIVE) & 0xFFu)) {
            continue;
        }
        const uint32_t flags = rd32(rdram, p + P_FLAGS);
        if ((flags & 3u) != 0 || ((flags >> 8) & 1u) != side) {
            continue;
        }
        count++;
    }
    return count;
}

}  // namespace

extern "C" uint32_t ogre_battle_pick_attack(uint8_t* /*generated_rdram*/, uint32_t actor,
                                            uint32_t attack) {
    attack &= 0xFFu;
    if (attack != ATTACK_FAERIE_KISS || !faerie_heal_enabled()) {
        return attack;
    }
    const uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram == nullptr || !in_rdram(actor)) {
        return attack;
    }
    if (ally_hurt(rdram, actor)) {
        std::fprintf(stderr, "[battle-ai] faerie %08X: ally hurt, Kiss -> Healing\n", actor);
        return ATTACK_HEALING;
    }
    return attack;
}

extern "C" uint32_t ogre_battle_attack_count(uint8_t* /*generated_rdram*/, uint32_t actor, uint32_t count) {
    if (!centurion_command_enabled()) {
        return count;
    }
    const uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram == nullptr || !in_rdram(actor) || (rd32(rdram, actor + P_LIVE) & 0xFFu) != CLASS_SOLDIER) {
        return count;
    }
    const int bonus = centurions_on_side(rdram, actor);
    if (bonus > 0) {
        std::fprintf(stderr, "[battle-ai] soldier %08X: %d centurion(s), attacks %u -> %u\n", actor, bonus, count,
                     count + static_cast<uint32_t>(bonus));
    }
    return count + static_cast<uint32_t>(bonus);
}

