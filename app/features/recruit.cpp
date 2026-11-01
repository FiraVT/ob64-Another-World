// Recruits through the game's own character builder. See recruit.hpp.

#include "recruit.hpp"

#include <cstdio>
#include <deque>
#include <mutex>

#include "game_addresses.hpp"
#include "game_events.hpp"
#include "librecomp/overlays.hpp"
#include "recomp.h"

namespace ogre::recruit {

namespace {

constexpr uint32_t CHARACTERS = ::ogre::addr::kCharacters;
constexpr uint32_t CHARACTER_SIZE = ::ogre::addr::kCharacterSize;
constexpr int FIRST_REF = 1;
constexpr int LAST_REF = 99;
// As boss_escorts.cpp: the mission's base level, the game's template buffer,
// the builder's bank R and the field's bank P, which share the arena.
constexpr uint32_t BASE_LEVEL = 0x801F1055u;
constexpr uint32_t TEMPLATE_SLOT = 0x801E85C0u;
constexpr uint32_t TEMPLATE_SIZE = 35u;
constexpr uint32_t BANK_R_ROM = 0x195430u;
constexpr uint32_t BANK_R_SIZE = 0x2380u;
constexpr uint32_t BANK_P_ROM = 0x171EC0u;
constexpr uint32_t BANK_P_SIZE = 0x6030u;
constexpr uint32_t ARENA = ::ogre::addr::kBuilderArena;
constexpr uint32_t BUILDER = 0x802150DCu;
constexpr uint32_t LOAD_BANK = 0x8009DA50u;

struct Request {
    uint16_t cls;
    int level;
};

std::mutex g_mutex;
std::deque<Request> g_queue;

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address - 0x80000000u) ^ 3u];
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    rdram[(address - 0x80000000u) ^ 3u] = value;
}

void wr32(uint8_t* rdram, uint32_t address, uint32_t value) {
    for (uint32_t i = 0; i < 4; i++) {
        wr8(rdram, address + i, static_cast<uint8_t>(value >> (24 - 8 * i)));
    }
}

uint32_t record_of(int ref) {
    return CHARACTERS + static_cast<uint32_t>(ref) * CHARACTER_SIZE;
}

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

void call(uint8_t* rdram, recomp_context* ctx, uint32_t vram) {
    LOOKUP_FUNC(vram)(rdram, ctx);
}

void load_bank(uint8_t* rdram, recomp_context* ctx, uint32_t rom, uint32_t size) {
    const recomp_context saved = *ctx;
    set_arg(ctx->r4, rom);
    set_arg(ctx->r5, ARENA);
    set_arg(ctx->r6, size);
    call(rdram, ctx, LOAD_BANK);
    *ctx = saved;
}

// A free army record: class byte 0, which is how the game's own loops skip
// one (func_8016B774).
int free_ref(const uint8_t* rdram) {
    for (int ref = FIRST_REF; ref <= LAST_REF; ref++) {
        if (rd8(rdram, record_of(ref) + 0x11) == 0) {
            return ref;
        }
    }
    return -1;
}

// func_ovlR_802150DC(ref, template, group 1, enemy 0, [sp+0x10] unit 0): with
// the enemy flag clear and a unit below 30 it writes the army record `ref`
// (0x80215148) and stores the ref at +0x34 (0x8021532C). Group 1 of the
// template is +0x07 class, +0x08 level offset from the base level, +0x09..
// +0x0C items (0: the class's gear). The call gets its own stack space below
// the caller's frame.
bool build(uint8_t* rdram, recomp_context* ctx, int ref, const Request& r) {
    uint8_t original[TEMPLATE_SIZE];
    for (uint32_t i = 0; i < TEMPLATE_SIZE; i++) {
        original[i] = rd8(rdram, TEMPLATE_SLOT + i);
        wr8(rdram, TEMPLATE_SLOT + i, 0);
    }
    const int offset = r.level - rd8(rdram, BASE_LEVEL);
    wr8(rdram, TEMPLATE_SLOT + 0x07, static_cast<uint8_t>(r.cls));
    wr8(rdram, TEMPLATE_SLOT + 0x08, static_cast<uint8_t>(static_cast<int8_t>(offset)));
    wr8(rdram, TEMPLATE_SLOT + 0x0D, 1);

    const recomp_context saved = *ctx;
    set_arg(ctx->r29, static_cast<uint32_t>(saved.r29) - 0x40u);
    set_arg(ctx->r4, static_cast<uint32_t>(ref));
    set_arg(ctx->r5, TEMPLATE_SLOT);
    set_arg(ctx->r6, 1);
    set_arg(ctx->r7, 0);
    wr32(rdram, static_cast<uint32_t>(ctx->r29) + 0x10, 0);
    call(rdram, ctx, BUILDER);
    *ctx = saved;

    for (uint32_t i = 0; i < TEMPLATE_SIZE; i++) {
        wr8(rdram, TEMPLATE_SLOT + i, original[i]);
    }
    return rd8(rdram, record_of(ref) + 0x11) == r.cls;
}

void field_step(uint8_t* rdram, recomp_context* ctx) {
    std::deque<Request> requests;
    {
        std::lock_guard lock(g_mutex);
        requests.swap(g_queue);
    }
    if (requests.empty()) {
        return;
    }
    const bool bank_p = recomp::overlays::is_function_bank_loaded(BANK_P_ROM);
    load_bank(rdram, ctx, BANK_R_ROM, BANK_R_SIZE);
    for (const Request& r : requests) {
        const int ref = free_ref(rdram);
        if (ref < 0) {
            std::fprintf(stderr, "[recruit] class %u: the army has no free character record\n", r.cls);
            continue;
        }
        const bool built = build(rdram, ctx, ref, r);
        std::fprintf(stderr, "[recruit] class %u level %d: %s character %d (level %d)\n", r.cls, r.level,
                     built ? "built" : "FAILED to build", ref, rd8(rdram, record_of(ref) + 0x13));
    }
    if (bank_p) {
        load_bank(rdram, ctx, BANK_P_ROM, BANK_P_SIZE);
    }
}

ogre::events::Subscribe g_field_step{ogre::events::Event::FieldUpdate, "recruits", [](const ogre::events::Args& e) {
    if (e.rdram != nullptr && e.ctx != nullptr) {
        field_step(e.rdram, e.ctx);
    }
}};

}  // namespace

bool queue(uint16_t cls, int level) {
    std::lock_guard lock(g_mutex);
    if (g_queue.size() >= static_cast<size_t>(MAX_QUEUED)) {
        return false;
    }
    g_queue.push_back({cls, level});
    return true;
}

}  // namespace ogre::recruit
