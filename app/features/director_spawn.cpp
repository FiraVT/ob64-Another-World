// War Director phase 3: deploy a bought unit on the mission field.
//
// The game has its own mid-mission spawn in the field update
// func_ovlN_801B15A0 (0x801B222C..0x801B2584, used by one mission: mission
// byte 0x8018F4A1 == 0x36 with flag 0x14 set). It deploys a unit in these
// steps, and this file replays them for a director unit:
//
//   1. pick a free unit slot 32..48: map object (0x801F0CD0 + slot*4) flags
//      `& 0x11 == 0`. A story unit still waiting to enter has its object, so
//      it is never taken.
//   2. DMA the unit builder ROM 0x195430 (0x2380) to 0x80214FA0 with
//      func_8009DA50.
//   3. func_80215360(template, slot) builds the unit and its members from a
//      35-byte enemydat template (the game's are at 0x801E85C0 + variant*35).
//   4. unit record +0x0C and +0x17, then func_801D4F5C(slot) creates the map
//      object, whose +0xBA/+0xBB are the AI bytes.
//   5. the ground height at (x, z) from func_801A66E8 (object +0x70 == 1) or
//      func_801A641C, then func_801CC4A0(object, x, y, z, 0) places it.
//   6. clear object flag 0x01 and unit record +0x01 bit 0x04; object
//      +0x18/+0x1C are the sprite scale.
//   7. func_8021592C(slot, 0, 0, 0) gives the route; 0x8019369C[slot] is the
//      unit's trigger halfword; func_801D4760() refreshes the field.
//
// The template is the director's (war_director::take_spawn writes it into the
// game's template slot, which is saved and restored around the build). The
// per-unit bytes the game takes from its 8-byte parameter rows are copied from
// a generic enemy unit already on the field, so the new unit behaves like the
// mission's own. It starts at the enemy-held town nearest the player's units
// (front_line_location), or at the boss unit's position (slot 30), the enemy
// stronghold, when the enemy holds no town.
//
// Every call goes through get_function, because the bank resident at an
// address changes during a mission.
//
// config/hooks.toml inserts the call at the field update's exit
// (0x801B258C), where the function has finished with its registers and only
// restores them from its stack frame.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "game_addresses.hpp"
#include "recomp.h"
#include "feature_flags.hpp"
#include "game_events.hpp"
#include "alerts.hpp"
#include "war_director.hpp"

namespace {

constexpr uint32_t OBJECTS = ::ogre::addr::kMapObjects;
constexpr uint32_t UNIT_TABLE = ::ogre::addr::kUnits;
constexpr uint32_t UNIT_STRIDE = 0x19u;
constexpr uint32_t TRIGGERS = 0x8019369Cu;
constexpr uint32_t MISSION_INDEX = ::ogre::addr::kMissionIndex;
constexpr uint32_t TEMPLATE_SLOT = 0x801E85C0u;
constexpr uint32_t TEMPLATE_SIZE = 35u;
constexpr int SLOT_FIRST = 32;
constexpr int SLOT_LAST = 48;
constexpr int BOSS_SLOT = 30;

uint32_t rd32(uint8_t* rdram, uint32_t address) {
    return static_cast<uint32_t>(MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

void wr32(uint8_t* rdram, uint32_t address, uint32_t value) {
    MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address))) = static_cast<int32_t>(value);
}

uint8_t rd8(uint8_t* rdram, uint32_t address) {
    return static_cast<uint8_t>(MEM_BU(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    MEM_B(0, static_cast<gpr>(static_cast<int32_t>(address))) = static_cast<int8_t>(value);
}

uint16_t rd16(uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>(MEM_HU(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

void wr16(uint8_t* rdram, uint32_t address, uint16_t value) {
    MEM_H(0, static_cast<gpr>(static_cast<int32_t>(address))) = static_cast<int16_t>(value);
}

uint32_t object_of(uint8_t* rdram, int slot) {
    return rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
}

uint32_t unit_of(int slot) {
    return UNIT_TABLE + static_cast<uint32_t>(slot) * UNIT_STRIDE;
}

bool valid_pointer(uint32_t address) {
    return address >= 0x80000000u && address < 0x80800000u;
}

bool unit_live(uint8_t* rdram, int slot) {
    const uint32_t unit = unit_of(slot);
    if ((rd8(rdram, unit + 0x01) & 0x80u) == 0) {
        return false;
    }
    for (uint32_t i = 0; i < 5; i++) {
        if (rd8(rdram, unit + 0x02 + i) != 0) {
            return true;
        }
    }
    return false;
}

void call(uint8_t* rdram, recomp_context* ctx, uint32_t vram) {
    LOOKUP_FUNC(vram)(rdram, ctx);
}

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

// The mission's locations (towns, temples, the headquarters), decoded from
// func_ovlN_801DA360, the game's "location -> position" function:
//   count            word 0x801F1090
//   state            u16 at 0x801951EC + i*0x24; 0x04 enemy-held, 0x02
//                    player-held, 0x10 the player's headquarters (the mission
//                    start files scincsv*.bin hold the same values: 0x0004,
//                    0x2012, 0x2002, 0x0000); the liberation code
//                    func_ovlN_801C2B90 clears 0x04 and sets 0x02
//   position         x, z floats at map_data + 0x1C + i*8, where map_data is
//                    the word at 0x800E7A90 + 4 * (byte 0x800E7AC5)
constexpr uint32_t LOCATION_COUNT = ::ogre::addr::kLocationCount;
constexpr uint32_t LOCATION_STATE = ::ogre::addr::kLocationState;
constexpr uint32_t LOCATION_STRIDE = 0x24u;
constexpr uint32_t MAP_DATA_TABLE = ::ogre::addr::kMapDataTable;
constexpr uint32_t MAP_ID = ::ogre::addr::kMapId;
constexpr int MAX_LOCATIONS = 64;
constexpr int PLAYER_UNITS = 30;

float as_float(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// The spawn point: the enemy-held location nearest the town the enemy most
// wants back (a counterattack, v2 5.3), or else nearest any player unit on the
// field, which is the front line. -1 when the enemy holds no location, and the
// caller then uses the boss's stronghold.
int front_line_location(uint8_t* rdram, uint32_t& x, uint32_t& z) {
    const int count = static_cast<int>(rd32(rdram, LOCATION_COUNT));
    const uint32_t map_data = rd32(rdram, MAP_DATA_TABLE + 4u * rd8(rdram, MAP_ID));
    if (count <= 0 || count > MAX_LOCATIONS || !valid_pointer(map_data)) {
        return -1;
    }
    int best = -1;
    float best_distance = 0.0f;
    const int target = ogre::war_director::grudge_town();
    for (int i = 0; i < count; i++) {
        const uint16_t state = rd16(rdram, LOCATION_STATE + static_cast<uint32_t>(i) * LOCATION_STRIDE);
        if ((state & 0x04u) == 0 || (state & 0x02u) != 0) {
            continue;
        }
        const uint32_t lx = rd32(rdram, map_data + 0x1Cu + static_cast<uint32_t>(i) * 8u);
        const uint32_t lz = rd32(rdram, map_data + 0x20u + static_cast<uint32_t>(i) * 8u);
        float nearest = -1.0f;
        if (target >= 0 && target < count) {
            const float dx = as_float(lx) - as_float(rd32(rdram, map_data + 0x1Cu + static_cast<uint32_t>(target) * 8u));
            const float dz = as_float(lz) - as_float(rd32(rdram, map_data + 0x20u + static_cast<uint32_t>(target) * 8u));
            nearest = dx * dx + dz * dz;
        }
        for (int s = 0; s < PLAYER_UNITS && target < 0; s++) {
            const uint32_t object = object_of(rdram, s);
            // On the map: flag 0x01 set (0x139 standing, 0x13B marching; an
            // undeployed unit reads 0x138 at position 0,0). Captures 2026-10-30.
            if (!valid_pointer(object) || (rd32(rdram, object) & 0x11u) != 0x11u) {
                continue;
            }
            const float dx = as_float(rd32(rdram, object + 0x08)) - as_float(lx);
            const float dz = as_float(rd32(rdram, object + 0x10)) - as_float(lz);
            const float d = dx * dx + dz * dz;
            if (nearest < 0.0f || d < nearest) {
                nearest = d;
            }
        }
        if (nearest >= 0.0f && (best < 0 || nearest < best_distance)) {
            best = i;
            best_distance = nearest;
            x = lx;
            z = lz;
        }
    }
    return best;
}

// WAR DIRECTOR (GAMEPLAY tab); OGRE_DIRECTOR_SPAWN forces it.
bool enabled() {
    return ogre::feature::enabled(OGRE_FEATURE(WarDirector), "OGRE_DIRECTOR_SPAWN");
}

}  // namespace

static void deploy_bought_unit(uint8_t* rdram, recomp_context* ctx) {
    if (!enabled() || !ogre::war_director::spawn_pending()) {
        return;
    }

    // 1. A free slot: an idle map object, the way the game's own spawn picks
    // one, or an unused unit record with no object at all. Most missions have
    // no idle objects, and func_801D4F5C builds the object in its fixed array
    // (0x801F10A0 + slot * 0xC0) either way. A record with any byte set may be a
    // story unit that has not entered yet, so it is left alone.
    int slot = -1;
    for (int s = SLOT_FIRST; s <= SLOT_LAST && slot < 0; s++) {
        const uint32_t object = object_of(rdram, s);
        if (valid_pointer(object) && (rd32(rdram, object) & 0x11u) == 0) {
            slot = s;
        }
    }
    for (int s = SLOT_FIRST; s <= SLOT_LAST && slot < 0; s++) {
        const uint32_t unit = unit_of(s);
        bool empty = !valid_pointer(object_of(rdram, s));
        for (uint32_t i = 0; i < 0x0C && empty; i++) {
            empty = rd8(rdram, unit + i) == 0;
        }
        if (empty) {
            slot = s;
        }
    }
    // A generic enemy on the field to copy the per-unit bytes from, and the
    // stronghold's position.
    // A generic enemy out on the map (flag 0x01 set), not one still waiting to
    // enter: those carry trigger-driven AI such as "attack after a liberation".
    // One that moves is preferred: the AI pair 0x01/0x04 holds its location (the
    // boss, Tenne Plains' slot 31 Wizard, the caravans below), and copying it made
    // every bought unit stand at its spawn town until slots 32..48 were full and
    // deployment stopped (run-mouse1, 1141 h: "no free enemy slot"). Marching
    // units carry other pairs (Tenne Plains' slot 33 Witch 05/02, mission 42's
    // Knight Templars 0E/03).
    int reference = -1;
    int holder = -1;
    for (int s = BOSS_SLOT + 1; s < 60; s++) {
        const uint32_t object = object_of(rdram, s);
        if (!unit_live(rdram, s) || !valid_pointer(object) || (rd32(rdram, object) & 0x11u) != 0x11u) {
            continue;
        }
        // Pairs starting 0x01 stay put (the boss and Tenne Plains' slot 31
        // 01/04, slot 32 01/01, run-commander1), and so do units whose pair
        // ends 0x04 (bought units copying 05/04 stood at their spawn town with
        // no destination, run-commander2); marching pairs end 0x02 or 0x03.
        // The enemy commander gives bought units their own orders as well.
        if (rd8(rdram, object + 0xBA) == 0x01 || rd8(rdram, object + 0xBB) == 0x04) {
            holder = holder < 0 ? s : holder;
            continue;
        }
        reference = s;
        break;
    }
    // The per-unit bytes a bought unit copies, remembered from the last generic
    // enemy seen on this mission, so deployment continues after the player has
    // beaten every generic unit.
    static struct {
        int mission = -1;
        int slot = -1;
        uint8_t unit_0c = 0, unit_17 = 0, ai_ba = 0, ai_bb = 0;
        uint32_t scale_x = 0, scale_y = 0;
        uint16_t trigger = 0;
    } ref;
    const int mission = rd8(rdram, MISSION_INDEX);
    if (ref.mission != mission) {
        ref = {};
        ref.mission = mission;
    }
    // A holder only when no moving unit has been seen on this mission yet; a
    // remembered mover's bytes are kept once its unit is beaten.
    if (reference < 0 && ref.slot < 0) {
        reference = holder;
    }
    if (reference >= 0) {
        const uint32_t u = unit_of(reference);
        const uint32_t o = object_of(rdram, reference);
        ref.slot = reference;
        ref.unit_0c = rd8(rdram, u + 0x0C);
        ref.unit_17 = rd8(rdram, u + 0x17);
        ref.ai_ba = rd8(rdram, o + 0xBA);
        ref.ai_bb = rd8(rdram, o + 0xBB);
        ref.scale_x = rd32(rdram, o + 0x18);
        ref.scale_y = rd32(rdram, o + 0x1C);
        ref.trigger = rd16(rdram, TRIGGERS + static_cast<uint32_t>(reference) * 2u);
    }
    reference = ref.slot;
    const uint32_t boss = object_of(rdram, BOSS_SLOT);
    if (slot < 0 || reference < 0 || !valid_pointer(boss)) {
        // Try again on a later frame; say why once per reason.
        static int said = 0;
        const int reason = slot < 0 ? 1 : reference < 0 ? 2 : 3;
        if (said != reason) {
            said = reason;
            std::fprintf(stderr, "[director] spawn waiting: %s\n",
                         reason == 1   ? "no free enemy slot in 32..48"
                         : reason == 2 ? "no generic enemy seen on this mission to copy AI bytes from"
                                       : "no boss map object");
        }
        return;
    }

    // The registers this function may change; the field update's own values
    // are restored from its stack frame after this returns, but its argument
    // registers are kept anyway so nothing observes a difference.
    const recomp_context saved = *ctx;

    // 3. The director's template in the game's template slot.
    uint8_t original[TEMPLATE_SIZE];
    for (uint32_t i = 0; i < TEMPLATE_SIZE; i++) {
        original[i] = rd8(rdram, TEMPLATE_SLOT + i);
    }
    std::string what;
    int kind = 0;
    if (!ogre::war_director::take_spawn(TEMPLATE_SLOT, what, kind)) {
        for (uint32_t i = 0; i < TEMPLATE_SIZE; i++) {
            wr8(rdram, TEMPLATE_SLOT + i, original[i]);
        }
        return;
    }

    // 2. The unit builder.
    set_arg(ctx->r4, 0x00195430u);
    set_arg(ctx->r5, 0x80214FA0u);
    set_arg(ctx->r6, 0x2380u);
    call(rdram, ctx, 0x8009DA50u);

    set_arg(ctx->r4, TEMPLATE_SLOT);
    set_arg(ctx->r5, static_cast<uint32_t>(slot));
    call(rdram, ctx, 0x80215360u);
    for (uint32_t i = 0; i < TEMPLATE_SIZE; i++) {
        wr8(rdram, TEMPLATE_SLOT + i, original[i]);
    }

    // 4. Record bytes and the map object.
    const uint32_t unit = unit_of(slot);
    wr8(rdram, unit + 0x0C, ref.unit_0c);
    wr8(rdram, unit + 0x17, ref.unit_17);
    set_arg(ctx->r4, static_cast<uint32_t>(slot));
    call(rdram, ctx, 0x801D4F5Cu);
    const uint32_t object = object_of(rdram, slot);
    if (!valid_pointer(object)) {
        std::fprintf(stderr, "[director] spawn: slot %d has no map object after the build\n", slot);
        *ctx = saved;
        return;
    }
    // A supply caravan holds its town like the mission's garrisons (AI 0x01/0x04,
    // the boss's and Mulsuk's spawn entries on Tenne Plains); attack units take
    // the copied generic unit's AI.
    wr8(rdram, object + 0xBA, kind == 2 ? 0x01 : ref.ai_ba);
    wr8(rdram, object + 0xBB, kind == 2 ? 0x04 : ref.ai_bb);

    // 5. The spawn point: the enemy-held town nearest the player's units, or
    // the boss's stronghold. Then the ground height and the placement.
    uint32_t x = rd32(rdram, boss + 0x08);
    uint32_t z = rd32(rdram, boss + 0x10);
    // A last stand for the boss forms at the stronghold itself.
    const int location = ogre::war_director::boss_stand() ? -1 : front_line_location(rdram, x, z);
    set_arg(ctx->r4, rd8(rdram, 0x800E7AB9u));
    set_arg(ctx->r5, x);
    set_arg(ctx->r6, z);
    call(rdram, ctx, rd32(rdram, object + 0x70) == 1 ? 0x801A66E8u : 0x801A641Cu);
    const uint32_t y = ctx->f0.u32l;
    const uint32_t sp = static_cast<uint32_t>(ctx->r29);
    wr32(rdram, sp + 0x10, 0);
    set_arg(ctx->r4, object);
    set_arg(ctx->r5, x);
    set_arg(ctx->r6, y);
    set_arg(ctx->r7, z);
    call(rdram, ctx, 0x801CC4A0u);

    // 6. Visible and released.
    wr32(rdram, object, rd32(rdram, object) & ~0x1u);
    wr8(rdram, unit + 0x01, rd8(rdram, unit + 0x01) & 0xFBu);
    wr32(rdram, object + 0x18, ref.scale_x);
    wr32(rdram, object + 0x1C, ref.scale_y);

    // 7. Route, trigger and field refresh.
    set_arg(ctx->r4, static_cast<uint32_t>(slot));
    set_arg(ctx->r5, 0);
    set_arg(ctx->r6, 0);
    set_arg(ctx->r7, 0);
    call(rdram, ctx, 0x8021592Cu);
    wr16(rdram, TRIGGERS + static_cast<uint32_t>(slot) * 2u, ref.trigger);
    call(rdram, ctx, 0x801D4760u);

    if (location >= 0) {
        std::fprintf(stderr, "[director] spawned %s in slot %d at location %d (%.0f, %.0f), AI from slot %d\n",
                     what.c_str(), slot, location, static_cast<double>(as_float(x)),
                     static_cast<double>(as_float(z)), reference);
    }
    else {
        std::fprintf(stderr, "[director] spawned %s in slot %d at the stronghold, AI from slot %d\n",
                     what.c_str(), slot, reference);
    }
    std::fflush(stderr);
    const int target = location >= 0 ? ogre::war_director::grudge_town() : -1;
    ogre::war_director::note_spawned(slot, kind, kind == 2 ? -1 : target);
    // Attack units deploy without an alert: the wave's "The enemy attacks"
    // already said they are coming.
    if (kind == 2) {
        ogre::alerts::post(ogre::alerts::Kind::Info, "A supply caravan has arrived at an enemy town. Take it for spoils.");
    }
    *ctx = saved;
}

// Spoils from beaten director units (v2 6.1), granted by the game's own item
// routine func_ovlN_801DC740 (a0 = item word, bit 15 equipment), which bank N
// keeps resident during the field update.
static void grant_spoils(uint8_t* rdram, recomp_context* ctx) {
    uint32_t goth = 0;
    const std::vector<uint16_t> spoils = ogre::war_director::take_spoils(goth);
    // One alert for everything won at once: "Spoils: Short Sword, Heal Leaf, 300 Goth".
    std::string text;
    if (!spoils.empty()) {
        const recomp_context saved = *ctx;
        for (const uint16_t word : spoils) {
            set_arg(ctx->r4, word);
            set_arg(ctx->r5, 0);
            call(rdram, ctx, 0x801DC740u);
            text += (text.empty() ? "" : ", ") + ogre::war_director::item_label(rdram, word);
        }
        *ctx = saved;
    }
    if (goth != 0) {
        // The army's Goth, u32 at 0x80196A8C (docs/anotherworld/design-organize-screen.md).
        wr32(rdram, 0x80196A8Cu, rd32(rdram, 0x80196A8Cu) + goth);
        text += (text.empty() ? "" : ", ") + std::to_string(goth) + " Goth";
    }
    if (!text.empty()) {
        ogre::alerts::post(ogre::alerts::Kind::Good, "Spoils: " + text);
    }
}

static ogre::events::Subscribe g_spoils{ogre::events::Event::FieldUpdate, "war director spoils",
                                       [](const ogre::events::Args& e) { grant_spoils(e.rdram, e.ctx); }};

static ogre::events::Subscribe g_deploy{ogre::events::Event::FieldUpdate, "war director deployment",
                                      [](const ogre::events::Args& e) { deploy_bought_unit(e.rdram, e.ctx); }};

