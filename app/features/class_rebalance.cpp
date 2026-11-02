// Class rules. See class_rebalance.hpp.

#include "game_addresses.hpp"
#include "class_rebalance.hpp"
#include "content_db.hpp"
#include "lua_runtime.hpp"

#include "librecomp/overlays.hpp"
#include "ultramodern/ultramodern.hpp"

namespace ogre::class_rebalance {

namespace {

constexpr uint32_t CLASS_TABLE = ::ogre::addr::kClasses;
constexpr uint32_t CLASS_STRIDE = ::ogre::addr::kClassSize;
constexpr int CLASS_COUNT = ::ogre::addr::kClassCount;
constexpr uint32_t CLASS_SIZE = 0x04u;

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    uint8_t& byte = rdram[(address & 0x1FFFFFFFu) ^ 3u];
    if (byte != value) {
        byte = value;
    }
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd8(rdram, address)) << 24) |
           (static_cast<uint32_t>(rd8(rdram, address + 1)) << 16) |
           (static_cast<uint32_t>(rd8(rdram, address + 2)) << 8) | rd8(rdram, address + 3);
}

// Classes the ROM gives size 2, recorded the first time each is seen. The
// table keeps size 2 (an earlier build wrote 1 and is repaired by tick);
// ogre_class_size answers the unit_size filter's places to the Organize rules.
bool g_large[CLASS_COUNT] = {};

}  // namespace

bool original_large(int cls) {
    return cls > 0 && cls < CLASS_COUNT && g_large[cls];
}

void tick(uint8_t* rdram) {
    // The class table lives in the streamed segment B data.
    if (!content::segment_b_resident(rdram)) {
        return;
    }

    // The class table keeps the ROM's size 2, so every screen that draws a
    // unit (battle, the field's unit menus) picks the large figure; the
    // Organize rules see the unit_size filter's answer through
    // ogre_class_size. An earlier version wrote size 1 into the table, and
    // battle and the field menus then drew large-only classes through their
    // empty small-picture ids, which is picture 0, the Black Knight
    // (developer, 2026-10-29).
    for (int c = 1; c < CLASS_COUNT; c++) {
        const uint32_t size = CLASS_TABLE + static_cast<uint32_t>(c) * CLASS_STRIDE + CLASS_SIZE;
        if (rd8(rdram, size) == 2u) {
            g_large[c] = true;
        }
        if (g_large[c] && rd8(rdram, size) != 2u) {
            wr8(rdram, size, 2u);
        }
    }
}

}  // namespace ogre::class_rebalance

// The Organize screen's pictures. Its setup (func_ovlS_801D4BB0, 0x801D4D84)
// gives every character a kind byte at Organize state (word 0x80196B18)
// + 0x1873 + character*2: 3 when the class's size is 2, 1 otherwise. Five draw
// routines read it and draw the large picture for 3 (0x801BB43C, 0x801BAEC0,
// 0x801BB1E8, 0x801BCC10, 0x801BD38C). With large units at size 1 the large
// classes got 1 and were drawn through the small-picture path, whose picture
// ids (class +0x2E/+0x30) are 0 for every large-only class, which drew a Black
// Knight. config/hooks.toml makes those five loads go through this
// function, which answers 3 for a class the ROM gives size 2. The grid layout
// (0x801C6864) and the slot counts still see size 1, so the unit keeps one
// square.
extern "C" uint32_t ogre_display_kind(uint8_t* /*generated_rdram*/, uint32_t entry_base, uint32_t kind) {
    using namespace ogre::class_rebalance;
    uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram == nullptr || kind != 1u) {
        return kind;
    }
    const uint32_t state = rd32(rdram, 0x80196B18u);
    if (entry_base < state || ((entry_base - state) & 1u) != 0) {
        return kind;
    }
    const uint32_t character = (entry_base - state) / 2u;
    if (character >= 100u) {
        return kind;
    }
    const int cls = rd8(rdram, 0x80193BE0u + character * 0x38u + 0x11u);
    return original_large(cls) ? 3u : kind;
}

// A class change's alignment limits. The Change Class check
// func_ovlS_801D2378 reads byte 6 (minimum alignment) at 0x801D2580 and byte
// 7 (maximum) at 0x801D25AC of the target's requirement record, and skips the
// test when the byte is 0xFF; the qualify check reads the same pair at
// 0x801D720C and 0x801D7238. config/hooks.toml passes the four loads through
// this function, which runs the Lua filter event class_alignment
// (lua_runtime.hpp). The Another World mod pack answers 0xFF
// (content/another-world/class-rules.lua); with no handler the game's own
// limits stand.
extern "C" uint32_t ogre_alignment_requirement(uint32_t requirement) {
    return static_cast<uint32_t>(ogre::lua::filter("class_alignment", {}, requirement & 0xFFu, 0, 0xFF));
}

uint32_t ogre::class_rebalance::alignment_limit(uint32_t requirement) {
    return ogre_alignment_requirement(requirement);
}

int ogre::class_rebalance::unit_places(int size) {
    return static_cast<int>(ogre::lua::filter("unit_size", {}, size, 1, 2));
}

// The places a large class takes in the Organize rules. config/hooks.toml
// passes the class size getter's result (func_8016DE3C, class byte +4 or the
// class key's) through here with the caller's return address. The Organize
// screen (bank S, ROM 0x87220 at RAM 0x8019A7C0..0x801F1520) reads the size in
// 16 places to count a unit's places (+1 or +2), to place a large member away
// from its neighbours and to set each character's kind byte; for those the
// size goes through the Lua filter event unit_size, which the Another World
// mod pack answers with 1 for size 2 (content/another-world/class-rules.lua).
// Its two draw sites (the jal at 0x8019FB7C and 0x801BA990, return addresses
// +8) and every caller outside bank S get the real 2. With size 1 the kind
// byte stays 1, and the five Organize draw readers keep going through
// ogre_display_kind above.
extern "C" uint32_t ogre_class_size(uint8_t* /*rdram*/, uint32_t size, uint32_t return_address) {
    constexpr uint32_t BANK_S_RAM = 0x8019A7C0u;
    constexpr uint32_t BANK_S_SIZE = 0x56D60u;
    constexpr uint32_t BANK_S_ROM = 0x087220u;
    if ((size & 0xFFu) != 2u) {
        return size;
    }
    if (return_address < BANK_S_RAM || return_address >= BANK_S_RAM + BANK_S_SIZE ||
        !recomp::overlays::is_function_bank_loaded(BANK_S_ROM)) {
        return size;
    }
    if (return_address == 0x8019FB84u || return_address == 0x801BA998u) {
        return size;
    }
    return static_cast<uint32_t>(ogre::class_rebalance::unit_places(2));
}

