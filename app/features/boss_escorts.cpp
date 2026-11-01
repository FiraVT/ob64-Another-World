// Extra members: enemy units fight with members beyond the five a unit record
// holds (docs/anotherworld/design-balance-master-plan.md E5-E8). First built for mission
// bosses (hence the file name); now any generic enemy unit, by difficulty.
//
// A unit record (0x80197210 + unit*0x19) has five member references
// (+0x02..+0x06, the leader first) and five squares (+0x07..+0x0B, 0-based:
// 0..2 the back row, 3..5 the middle, 6..8 the front). The battle's
// participant table has 20 slots (*(0x801CE8DC) + 0x1C4 + n*0xF8).
// func_ovlJ_801D1A80 fills it from the two unit records: the enemy side's loop
// (0x801D1C98..0x801D1E4C) reads member i's reference from the unit record
// (0x801D1CC0, 0x801D1D7C, 0x801D1DD0) and its square from the battle's copy
// (*(0x801CE8E0) + 0x84F + i, 0x801D1D10), for i < 5 (0x801D1E48). The
// battle's occupancy grid func_ovlC_801C6304 reads the same five.
// config/hooks.toml passes those reads and the bounds through the functions
// below, so members 5..8 of a unit come from this file's table: enemy
// character records claimed for the mission, standing in the unit's free
// squares. The field never sees them; the unit record still lists five.
//
// Who gets extras (developer, 2026-10-30):
//
//   boss (slot 30)  1, 2 from mission 15 (any difficulty but EASY's rule below)
//   EASY            none for other units
//   NORMAL          1 weak extra (a base-tier class of the family, 3 levels
//                   lower) from mission 25, on about half the full units
//   HARD            1 standard extra (never elite) from mission 15, 2 from 30
//   VETERAN         2 from mission 10, 3 from 25, 4 from 35; +1 (at most 4)
//                   after 30 in-game days on the mission; elites allowed within
//                   war_director::elite_cap_now (4, then 6 after 90 days, 8
//                   after 180)
//
// Only full units (five record members) get extras, except the boss. Story
// units (a leader of a story class other than the army classes) never do.
// Classes come from war_director::themed_extra_class (flyers stay flyers,
// otherwise the escort's family, a missing role first) and are built with the
// game's own character builder func_ovlR_802150DC on the field update (game
// thread), loading its bank R (ROM 0x195430, 0x2380 bytes at 0x80214FA0) the
// way the director's spawns do, and loading the field's bank P (ROM 0x171EC0,
// 0x6030) back when it was resident. A record that cannot be built copies the
// escort.
//
// Records. A character record is free when its class key (+0x12) is 0 (the
// unit builder func_ovlR_80215360 takes the first such record,
// 0x80215430..0x8021544C). Extras are made only while more than RESERVE
// records stay free, so the director can still deploy units, and a unit's
// extras are freed (record zeroed) when the unit leaves the map or its slot is
// reused by another leader.
//
// ENEMY RANKS (GAMEPLAY) switches it; OGRE_BOSS_ESCORTS=0 or 1 forces it.

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "game_addresses.hpp"
#include "boss_escorts.hpp"
#include "feature_flags.hpp"
#include "game_events.hpp"
#include "librecomp/overlays.hpp"
#include "recomp.h"
#include "war_director.hpp"

namespace {

constexpr uint32_t OBJECTS = ::ogre::addr::kMapObjects;
constexpr uint32_t UNIT_TABLE = ::ogre::addr::kUnits;
constexpr uint32_t UNIT_STRIDE = 0x19u;
constexpr uint32_t ENEMY_CHARS = ::ogre::addr::kEnemyCharacters;
constexpr uint32_t ENEMY_STRIDE = 0x34u;
constexpr uint32_t BATTLE_ENEMY_UNIT = 0x80197708u;
constexpr uint32_t MISSION_INDEX = ::ogre::addr::kMissionIndex;
constexpr uint32_t BASE_LEVEL = 0x801F1055u;
constexpr uint32_t TEMPLATE_SLOT = 0x801E85C0u;  // the game's template buffer (director_spawn.cpp)
constexpr uint32_t TEMPLATE_SIZE = 35u;
constexpr uint32_t BANK_R_ROM = 0x195430u;
constexpr uint32_t BANK_R_SIZE = 0x2380u;
constexpr uint32_t BANK_P_ROM = 0x171EC0u;
constexpr uint32_t BANK_P_SIZE = 0x6030u;
constexpr uint32_t ARENA = ::ogre::addr::kBuilderArena;
constexpr int BOSS = 30;
constexpr int FIRST_ENEMY = 30;
constexpr int SLOTS = 60;
constexpr int MAX_EXTRA = 4;
constexpr int RESERVE = 25;  // free enemy character records always left for the director

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address - 0x80000000u) ^ 3u];
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) {
        v = (v << 8) | rd8(rdram, address + i);
    }
    return v;
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    rdram[(address - 0x80000000u) ^ 3u] = value;
}

void wr32(uint8_t* rdram, uint32_t address, uint32_t value) {
    for (uint32_t i = 0; i < 4; i++) {
        wr8(rdram, address + i, static_cast<uint8_t>(value >> (24 - 8 * i)));
    }
}

bool generic_class(int cls) {
    return (cls > 1 && cls < 81) || cls == 114 || cls == 115 || cls == 116;
}

struct Extra {
    int ref = 0;
    int square = 0;
};

struct UnitExtras {
    bool made = false;   // decided for this unit (possibly zero extras)
    int leader_ref = 0;  // the unit record's leader when decided (slot reuse)
    int count = 0;
    int elites = 0;      // elite extras, for the spoils
    Extra extra[MAX_EXTRA];
};

int g_mission = -1;
UnitExtras g_units[SLOTS];
std::atomic<int> g_shown[SLOTS];
std::atomic<int> g_shown_mission{-1};

bool enabled() {
    if (const char* value = std::getenv("OGRE_BOSS_ESCORTS"); value != nullptr && value[0] != '\0') {
        return value[0] != '0';
    }
    return ogre::feature::enabled(OGRE_FEATURE(EnemyRanks), "OGRE_ENEMY_RANKS");
}

uint32_t unit_of(int slot) {
    return UNIT_TABLE + static_cast<uint32_t>(slot) * UNIT_STRIDE;
}

uint32_t char_of(int ref) {
    return ENEMY_CHARS + static_cast<uint32_t>(ref) * ENEMY_STRIDE;
}

bool on_map(const uint8_t* rdram, int slot) {
    const uint32_t o = rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
    return o >= 0x80000000u && o < 0x80800000u && (rd32(rdram, o) & 0x10u) != 0;
}

bool referenced(const uint8_t* rdram, int ref) {
    for (int slot = FIRST_ENEMY; slot < SLOTS; slot++) {
        for (uint32_t i = 0; i < 5; i++) {
            if (rd8(rdram, unit_of(slot) + 2 + i) == ref) {
                return true;
            }
        }
    }
    return false;
}

int free_records(const uint8_t* rdram) {
    int n = 0;
    for (int ref = 1; ref < 100; ref++) {
        n += rd8(rdram, char_of(ref) + 0x12) == 0 ? 1 : 0;
    }
    return n;
}

// A unit's extras still hold: its leader is the one they were made for, and
// each record is claimed and listed by no unit (a loaded save restores RAM and
// can free a record or hand it to a unit).
bool unit_valid(const uint8_t* rdram, int slot) {
    const UnitExtras& u = g_units[slot];
    if (!u.made || rd8(rdram, unit_of(slot) + 2) != u.leader_ref) {
        return false;
    }
    for (int k = 0; k < u.count; k++) {
        const int ref = u.extra[k].ref;
        if (rd8(rdram, char_of(ref) + 0x12) == 0 || referenced(rdram, ref)) {
            return false;
        }
    }
    return true;
}

void release(uint8_t* rdram, int slot) {
    UnitExtras& u = g_units[slot];
    for (int k = 0; k < u.count; k++) {
        const int ref = u.extra[k].ref;
        if (rd8(rdram, char_of(ref) + 0x12) != 0 && !referenced(rdram, ref)) {
            for (uint32_t b = 0; b < ENEMY_STRIDE; b++) {
                wr8(rdram, char_of(ref) + b, 0);
            }
        }
    }
    u = UnitExtras{};
}

// The squares the extras take: in front of the leader's column first, then the
// rest of the front row, then the middle row, never a used square.
int pick_square(const bool used[9], int leader_square) {
    const int column = leader_square >= 0 && leader_square < 9 ? leader_square % 3 : 1;
    const int order[] = {6 + column, 3 + column, 6, 7, 8, 3, 4, 5, 0, 1, 2};
    for (int s : order) {
        if (!used[s]) {
            return s;
        }
    }
    return -1;
}

void call(uint8_t* rdram, recomp_context* ctx, uint32_t vram) {
    LOOKUP_FUNC(vram)(rdram, ctx);
}

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

void load_bank(uint8_t* rdram, recomp_context* ctx, uint32_t rom, uint32_t size) {
    const recomp_context saved = *ctx;
    set_arg(ctx->r4, rom);
    set_arg(ctx->r5, ARENA);
    set_arg(ctx->r6, size);
    call(rdram, ctx, 0x8009DA50u);
    *ctx = saved;
}

// The game's character builder func_ovlR_802150DC(ref, template, group, enemy,
// [sp+0x10] unit): group 1 of a scratch template in the game's template buffer
// (+0x07 class, +0x08 level offset from the base level, +0x09..+0x0C items, 0
// = the class's gear). With the fourth argument non-zero it writes the enemy
// table (0x80215108..0x80215140). The call gets its own stack space below the
// caller's frame, whose slots hold saved registers at the field update's exit.
bool build_character(uint8_t* rdram, recomp_context* ctx, int ref, int cls, int level, int slot) {
    uint8_t original[TEMPLATE_SIZE];
    for (uint32_t i = 0; i < TEMPLATE_SIZE; i++) {
        original[i] = rd8(rdram, TEMPLATE_SLOT + i);
        wr8(rdram, TEMPLATE_SLOT + i, 0);
    }
    const int offset = level - rd8(rdram, BASE_LEVEL);
    wr8(rdram, TEMPLATE_SLOT + 0x07, static_cast<uint8_t>(cls));
    wr8(rdram, TEMPLATE_SLOT + 0x08, static_cast<uint8_t>(static_cast<int8_t>(offset)));
    wr8(rdram, TEMPLATE_SLOT + 0x0D, 1);

    const recomp_context saved = *ctx;
    set_arg(ctx->r29, static_cast<uint32_t>(saved.r29) - 0x40u);
    set_arg(ctx->r4, static_cast<uint32_t>(ref));
    set_arg(ctx->r5, TEMPLATE_SLOT);
    set_arg(ctx->r6, 1);
    set_arg(ctx->r7, 1);
    wr32(rdram, static_cast<uint32_t>(ctx->r29) + 0x10, static_cast<uint32_t>(slot));
    call(rdram, ctx, 0x802150DCu);
    *ctx = saved;

    for (uint32_t i = 0; i < TEMPLATE_SIZE; i++) {
        wr8(rdram, TEMPLATE_SLOT + i, original[i]);
    }
    return rd8(rdram, char_of(ref) + 0x11) == cls && rd8(rdram, char_of(ref) + 0x12) != 0;
}

// How many extras the unit in `slot` gets, and how strong (0 weak, 1
// standard, 2 any), at this point of the war.
int extras_wanted(int slot, int members, int mission, double hours, int level, int& strength) {
    strength = level >= 3 ? 2 : level == 2 ? 1 : 0;
    int want = 0;
    if (slot == BOSS) {
        want = mission >= 15 ? 2 : 1;
        strength = std::max(strength, 1);
    }
    if (members >= 5) {
        switch (level) {
            case 1:  // NORMAL: one weak extra on about half the full units, late
                if (mission >= 25 && ((slot * 7 + mission) % 2) == 0) {
                    want = std::max(want, 1);
                }
                break;
            case 2:  // HARD
                want = std::max(want, mission >= 30 ? 2 : mission >= 15 ? 1 : 0);
                break;
            case 3: {  // VETERAN
                int n = mission >= 35 ? 4 : mission >= 25 ? 3 : mission >= 10 ? 2 : 0;
                if (n > 0 && hours >= 720.0) {
                    n++;
                }
                want = std::max(want, std::min(n, MAX_EXTRA));
                break;
            }
            default:
                break;
        }
    }
    return std::min(want, MAX_EXTRA);
}

struct Pending {
    int slot;
    int index;
    int ref;
    int cls;
    int level;
    int source;
};

// Decides and makes the extras of every enemy unit on the map that has none
// decided yet, on the field update (game thread). The builder's bank is loaded
// at most once per call.
void field_step(uint8_t* rdram, recomp_context* ctx) {
    const int mission = rd8(rdram, MISSION_INDEX);
    if (mission != g_mission) {
        g_mission = mission;
        for (UnitExtras& u : g_units) {
            u = UnitExtras{};
        }
    }
    const double hours = std::max(0.0, ogre::war_director::mission_hours());
    const int level = ogre::war_director::difficulty_level();

    std::vector<Pending> builds;
    int free_now = free_records(rdram);
    for (int slot = FIRST_ENEMY; slot < SLOTS; slot++) {
        UnitExtras& u = g_units[slot];
        const bool present = on_map(rdram, slot) && rd8(rdram, unit_of(slot) + 2) != 0;
        if (u.made && (!present || !unit_valid(rdram, slot))) {
            // A unit with extras that left the field was beaten: its spoils
            // follow the difficulty (war_director::reward_extras).
            if (!present && u.count > 0) {
                ogre::war_director::reward_extras(u.count, u.elites);
                std::fprintf(stderr, "[extras] mission %d slot %d beaten: %d extra(s), %d elite\n", g_mission, slot,
                             u.count, u.elites);
            }
            release(rdram, slot);
            free_now = free_records(rdram);
        }
        if (!present || u.made) {
            continue;
        }
        const uint32_t record = unit_of(slot);
        const int leader_ref = rd8(rdram, record + 2);
        u.made = true;
        u.leader_ref = leader_ref;
        const int leader_cls = leader_ref > 0 && leader_ref < 100 ? rd8(rdram, char_of(leader_ref) + 0x11) : 0;
        if (slot != BOSS && !generic_class(leader_cls)) {
            continue;  // a story unit
        }
        int members = 0;
        int source = 0;
        int roles[3] = {};
        bool used[9] = {};
        int elites = 0;
        const int base = rd8(rdram, BASE_LEVEL);
        for (uint32_t i = 0; i < 5; i++) {
            const int ref = rd8(rdram, record + 2 + i);
            if (ref <= 0 || ref >= 100) {
                continue;
            }
            members++;
            const int cls = rd8(rdram, char_of(ref) + 0x11);
            const int role = ogre::war_director::class_role_index(cls);
            if (role >= 0) {
                roles[role]++;
            }
            if (i > 0 && source == 0 && generic_class(cls)) {
                source = ref;
            }
            if (ogre::war_director::elite_class(rdram, cls, rd8(rdram, char_of(ref) + 0x13))) {
                elites++;
            }
            const int sq = rd8(rdram, record + 7 + i);
            if (sq < 9) {
                used[sq] = true;
            }
        }
        (void)base;
        int strength = 0;
        const int want = extras_wanted(slot, members, mission, hours, level, strength);
        if (want == 0 || source == 0) {
            continue;
        }
        const int escort = rd8(rdram, char_of(source) + 0x11);
        const int escort_level = rd8(rdram, char_of(source) + 0x13);
        const int elite_cap = ogre::war_director::elite_cap_now(mission, hours);
        for (int k = 0; k < want; k++) {
            if (free_now <= RESERVE) {
                break;
            }
            const int square = pick_square(used, rd8(rdram, record + 7));
            if (square < 0) {
                break;
            }
            int ref = 0;
            for (int r = 1; r < 100 && ref == 0; r++) {
                bool taken = false;
                for (const Pending& p : builds) {
                    taken = taken || p.ref == r;
                }
                if (!taken && rd8(rdram, char_of(r) + 0x12) == 0) {
                    ref = r;
                }
            }
            if (ref == 0) {
                break;
            }
            const int extra_level = strength == 0 ? std::max(1, escort_level - 3) : escort_level;
            const int pick_strength = strength == 2 && elites >= elite_cap ? 1 : strength;
            int cls = ogre::war_director::themed_extra_class(rdram, escort, roles, extra_level, mission,
                                                             static_cast<uint32_t>(slot * 8 + k + 1), pick_strength);
            if (cls == 0) {
                cls = escort;
            }
            if (ogre::war_director::elite_class(rdram, cls, extra_level)) {
                elites++;
                u.elites++;
            }
            const int role = ogre::war_director::class_role_index(cls);
            if (role >= 0) {
                roles[role]++;
            }
            used[square] = true;
            u.extra[u.count++] = Extra{ref, square};
            builds.push_back(Pending{slot, u.count - 1, ref, cls, extra_level, source});
            free_now--;
        }
    }
    if (builds.empty()) {
        return;
    }

    const bool any_built = std::any_of(builds.begin(), builds.end(), [&](const Pending& p) {
        return p.cls != rd8(rdram, char_of(p.source) + 0x11) || p.level != rd8(rdram, char_of(p.source) + 0x13);
    });
    const bool bank_p = any_built && recomp::overlays::is_function_bank_loaded(BANK_P_ROM);
    if (any_built) {
        load_bank(rdram, ctx, BANK_R_ROM, BANK_R_SIZE);
    }
    for (const Pending& p : builds) {
        const bool same = p.cls == rd8(rdram, char_of(p.source) + 0x11) && p.level == rd8(rdram, char_of(p.source) + 0x13);
        bool built = false;
        if (!same) {
            built = build_character(rdram, ctx, p.ref, p.cls, p.level, p.slot);
        }
        if (!built) {
            for (uint32_t b = 0; b < ENEMY_STRIDE; b++) {
                wr8(rdram, char_of(p.ref) + b, rd8(rdram, char_of(p.source) + b));
            }
        }
        std::fprintf(stderr, "[extras] mission %d slot %d: member %d ref %d class %d (%s), level %d, square %d\n",
                     g_mission, p.slot, 6 + p.index, p.ref, rd8(rdram, char_of(p.ref) + 0x11),
                     built ? "built" : "copied", rd8(rdram, char_of(p.ref) + 0x13),
                     g_units[p.slot].extra[p.index].square);
    }
    if (bank_p) {
        load_bank(rdram, ctx, BANK_P_ROM, BANK_P_SIZE);
    }
}

bool unit_ready(const uint8_t* rdram, uint32_t unit) {
    return rdram != nullptr && unit >= static_cast<uint32_t>(FIRST_ENEMY) && unit < static_cast<uint32_t>(SLOTS) &&
           enabled() && g_units[unit].count > 0 && unit_valid(rdram, static_cast<int>(unit));
}

ogre::events::Subscribe g_field_step{ogre::events::Event::FieldUpdate, "extra members", [](const ogre::events::Args& e) {
    if (e.rdram == nullptr || e.ctx == nullptr || !enabled()) {
        return;
    }
    field_step(e.rdram, e.ctx);
    for (int slot = 0; slot < SLOTS; slot++) {
        g_shown[slot].store(slot >= FIRST_ENEMY && unit_ready(e.rdram, static_cast<uint32_t>(slot))
                                ? g_units[slot].count
                                : 0);
    }
    g_shown_mission.store(rd8(e.rdram, MISSION_INDEX));
}};

uint32_t member_for(const uint8_t* rdram, uint32_t unit, uint32_t index, uint32_t loaded) {
    if (index < 5) {
        return loaded;
    }
    const int k = static_cast<int>(index) - 5;
    return unit_ready(rdram, unit) && k < g_units[unit].count ? static_cast<uint32_t>(g_units[unit].extra[k].ref)
                                                             : 0u;
}

uint32_t square_for(const uint8_t* rdram, uint32_t unit, uint32_t index, uint32_t loaded) {
    if (index < 5) {
        return loaded;
    }
    const int k = static_cast<int>(index) - 5;
    return unit_ready(rdram, unit) && k < g_units[unit].count ? static_cast<uint32_t>(g_units[unit].extra[k].square)
                                                             : loaded;
}

uint32_t more_for(const uint8_t* rdram, uint32_t unit, uint32_t index, uint32_t result) {
    if (!unit_ready(rdram, unit)) {
        return result;
    }
    return index < 5u + static_cast<uint32_t>(g_units[unit].count) ? 1u : 0u;
}

}  // namespace

// func_ovlJ_801D1A80's entry: logs the extras of the battle's enemy unit.
// HP between battles: func_ovlJ_801D9100 writes every live participant (all 20
// slots, 0x801D92D4) back to its character record by its reference (+0xF6), so
// the extras' damage carries into the next fight.
extern "C" void ogre_battle_setup(uint8_t* rdram) {
    const uint32_t unit = rd8(rdram, BATTLE_ENEMY_UNIT);
    if (!unit_ready(rdram, unit)) {
        return;
    }
    std::string hp;
    for (int k = 0; k < g_units[unit].count; k++) {
        const uint32_t c = char_of(g_units[unit].extra[k].ref);
        const int now = (rd8(rdram, c + 0x18) << 8) | rd8(rdram, c + 0x19);
        const int max = (rd8(rdram, c + 0x16) << 8) | rd8(rdram, c + 0x17);
        hp += " ref " + std::to_string(g_units[unit].extra[k].ref) + " class " + std::to_string(rd8(rdram, c + 0x11)) +
              " HP " + std::to_string(now) + "/" + std::to_string(max);
    }
    std::fprintf(stderr, "[extras] battle with unit %u: %d extra member(s):%s\n", unit, g_units[unit].count, hp.c_str());
}

// The enemy setup loop (func_ovlJ_801D1A80); the enemy unit is 0x80197708.
extern "C" uint32_t ogre_battle_member(uint8_t* rdram, uint32_t index, uint32_t loaded) {
    return member_for(rdram, rd8(rdram, BATTLE_ENEMY_UNIT), index, loaded);
}

extern "C" uint32_t ogre_battle_square(uint8_t* rdram, uint32_t index, uint32_t loaded) {
    return square_for(rdram, rd8(rdram, BATTLE_ENEMY_UNIT), index, loaded);
}

extern "C" uint32_t ogre_battle_more(uint8_t* rdram, uint32_t index, uint32_t result) {
    return more_for(rdram, rd8(rdram, BATTLE_ENEMY_UNIT), index, result);
}

// The battle's occupancy grid, func_ovlC_801C6304 (side a0, unit record a1,
// unit index a2 stored at side struct +0x10): one 9-square grid per side
// (*(0x801CFA90) + side*0x24, +0x14..) built from the unit record's members
// (+0x02+i), squares (+0x07+i, mirrored 8 - square for side 0) and class sizes,
// for i < 5 (0x801C65CC); the extras stand in it as well.
extern "C" uint32_t ogre_unit_member(uint8_t* rdram, uint32_t unit, uint32_t index, uint32_t loaded) {
    return member_for(rdram, unit, index, loaded);
}

extern "C" uint32_t ogre_unit_square(uint8_t* rdram, uint32_t unit, uint32_t index, uint32_t loaded) {
    return square_for(rdram, unit, index, loaded);
}

extern "C" uint32_t ogre_unit_more(uint8_t* rdram, uint32_t unit, uint32_t index, uint32_t result) {
    return more_for(rdram, unit, index, result);
}

namespace ogre::boss_escorts {

int extra_members(const uint8_t* rdram, int unit) {
    if (rdram == nullptr || unit < 0 || unit >= SLOTS || g_shown_mission.load() != rd8(rdram, MISSION_INDEX)) {
        return 0;
    }
    return g_shown[unit].load();
}

}  // namespace ogre::boss_escorts

