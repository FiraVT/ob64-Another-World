// Mission-field speed: the in-game Settings menu's "Game speed" row.
//
// The option is the byte 0x800E9C13 (bank V's settings switch, case 4, stores
// it at 0x802155EC): 0 Slow, 1 Normal, 2 Fast. Bank N's per-frame field driver
// func_ovlN_801AE880 calls three routines that each read it as n = value + 1:
//
//   func_ovlN_801B134C  0x801B1390  the clock: 2 or 20 ticks x n per frame
//   func_ovlN_801E49D0  0x801E4DB4  a countdown: minus 2 x n per frame
//   func_ovlN_801C6D10  0x801C70B4  the field update func_ovlN_801B15A0
//                                   (units march), n times per frame
//
// Vanilla Fast is n = 3. Here the three settings make the units march about
// 1.7x, 3x and 6x as fast as vanilla Fast: n = 5, 9 and 18 field updates per
// frame. (It was 9, 18 and 27 until 2026-10-30; the developer found Normal too
// fast to react to and asked for the old Normal as Fast and the old Slow as
// Normal.)
// Only the update loop is changed (the developer: the setting speeds up the
// characters' movement, and game time keeps its vanilla rate).
// config/hooks.toml replaces the loaded value at 0x801C70B4 with n - 1
// (ogre_field_speed); the clock (0x801B1390) and the countdown (0x801E4DB4)
// keep reading the vanilla n = setting + 1.
//
// Vanilla checks two stop conditions once, before the update loop: the bit
// 0x02 of 0x801F367D and the word 0x801F3630. With up to 27 updates in one
// frame, the loop's back edge at 0x801C7100 repeats that check
// (ogre_field_hold), so updates stop on the frame a battle or an event starts,
// as they do before the first update.
//
// The menu and the save keep the vanilla 0..2 value. OGRE_FIELD_SPEED=0 keeps
// vanilla speeds.

#include <cstdint>
#include <cstdlib>

#include "ultramodern/ultramodern.hpp"

namespace {

constexpr int SPEEDS[3] = {5, 9, 18};

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

bool enabled() {
    static const bool value = [] {
        const char* env = std::getenv("OGRE_FIELD_SPEED");
        return env == nullptr || env[0] != '0';
    }();
    return value;
}

}  // namespace

extern "C" uint32_t ogre_field_speed(uint8_t* /*generated_rdram*/, uint32_t setting) {
    if (!enabled() || setting > 2) {
        return setting;
    }
    return static_cast<uint32_t>(SPEEDS[setting] - 1);
}

extern "C" int ogre_field_hold(uint8_t* /*generated_rdram*/) {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    if (!enabled() || rdram == nullptr) {
        return 0;
    }
    const bool flag = (rd8(rdram, 0x801F367Du) & 0x02u) != 0;
    const uint32_t word = (static_cast<uint32_t>(rd8(rdram, 0x801F3630u)) << 24) |
                          (static_cast<uint32_t>(rd8(rdram, 0x801F3631u)) << 16) |
                          (static_cast<uint32_t>(rd8(rdram, 0x801F3632u)) << 8) | rd8(rdram, 0x801F3633u);
    return flag || word != 0;
}

