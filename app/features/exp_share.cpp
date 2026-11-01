// Unit-shared battle EXP. See exp_share.hpp.

#include "game_addresses.hpp"
#include "exp_share.hpp"

#include <algorithm>
#include <cstdio>

#include "librecomp/overlays.hpp"
#include "options.hpp"

namespace ogre::exp_share {

namespace {

constexpr uint32_t SCENE_ADDRESS = ::ogre::addr::kActiveScene;
// A battle on the mission field runs as scene 0x0D (reached through 0x02), the
// same scene as dialogue and movies; a played session showed 23 battles as
// 0x03 -> 0x02 -> 0x0D -> 0x03 and no scene 0x0E at all. What only a battle
// loads is the battle bank, ROM 0x22A250 -> RAM 0x801E6FD0 (unit X), together
// with records 10-12. The field's own code is loaded over that RAM on the way
// back, which unloads the bank, so its residency brackets the battle.
constexpr int SCENE_BATTLE = 0x0D;
constexpr uint32_t BATTLE_BANK_ROM = 0x22A250u;

constexpr uint32_t PLAYER_UNIT_INDEX = 0x801976FCu;
constexpr uint32_t OTHER_UNIT_INDEX = 0x80197708u;  // the battle's other side
constexpr uint32_t UNIT_TABLE = ::ogre::addr::kUnits;
constexpr uint32_t UNIT_STRIDE = 0x19u;
constexpr int PLAYER_UNITS = 30;

constexpr uint32_t CHAR_TABLE = ::ogre::addr::kCharacters;
constexpr uint32_t CHAR_STRIDE = 0x38u;
constexpr uint32_t CHAR_CLASS = 0x11u;
constexpr uint32_t CHAR_LEVEL = 0x13u;
constexpr uint32_t CHAR_EXP = 0x35u;
constexpr int SOLDIER_REF = 100;

constexpr int MEMBERS = 5;
// Frames after the battle scene ends before the shares are computed (the
// result screen's last syncs run on the way out), and how long they are held.
constexpr int SETTLE_FRAMES = 30;
constexpr int HOLD_FRAMES = 120;

struct Member {
    int record = 0;  // 0 = empty slot
    int start_level = 0;
    int start_exp = 0;
    int target_exp = -1;  // written during the hold window
};

int g_last_scene = -1;
bool g_in_battle = false;
int g_settle = 0;
int g_hold = 0;
Member g_members[MEMBERS];

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    rdram[(address & 0x1FFFFFFFu) ^ 3u] = value;
}

uint16_t rd16(const uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>((rd8(rdram, address) << 8) | rd8(rdram, address + 1));
}

uint32_t char_record(int n) {
    return CHAR_TABLE + static_cast<uint32_t>(n) * CHAR_STRIDE;
}

void begin_battle(const uint8_t* rdram) {
    for (Member& m : g_members) {
        m = Member{};
    }
    // When an enemy unit starts the battle (the enemy commander's units,
    // enemy_commander.cpp) the first side holds the enemy and the player's unit
    // is on the other one (run-commander4: "unit 40 is not the player's").
    int unit = rd8(rdram, PLAYER_UNIT_INDEX);
    if (unit >= PLAYER_UNITS) {
        unit = rd8(rdram, OTHER_UNIT_INDEX);
    }
    if (unit >= PLAYER_UNITS) {
        std::fprintf(stderr, "[exp] battle start: unit %d is not the player's, no sharing\n", unit);
        return;
    }
    const uint32_t record = UNIT_TABLE + static_cast<uint32_t>(unit) * UNIT_STRIDE;
    std::fprintf(stderr, "[exp] battle start: unit %d", unit);
    for (int i = 0; i < MEMBERS; i++) {
        const int ref = rd8(rdram, record + 0x02 + static_cast<uint32_t>(i));
        if (ref == 0 || ref >= SOLDIER_REF) {
            continue;
        }
        const uint32_t c = char_record(ref);
        if (rd8(rdram, c + CHAR_CLASS) == 0) {
            continue;
        }
        g_members[i].record = ref;
        g_members[i].start_level = rd8(rdram, c + CHAR_LEVEL);
        g_members[i].start_exp = rd8(rdram, c + CHAR_EXP);
        std::fprintf(stderr, "  #%d L%d/%d", ref, g_members[i].start_level, g_members[i].start_exp);
    }
    std::fprintf(stderr, "\n");
}

void share(const uint8_t* rdram) {
    int gains[MEMBERS] = {};
    int levels[MEMBERS] = {};
    int exps[MEMBERS] = {};
    int count = 0;
    int total = 0;
    int level_sum = 0;
    for (int i = 0; i < MEMBERS; i++) {
        const Member& m = g_members[i];
        if (m.record == 0) {
            continue;
        }
        const uint32_t c = char_record(m.record);
        levels[i] = rd8(rdram, c + CHAR_LEVEL);
        exps[i] = rd8(rdram, c + CHAR_EXP);
        const int now = levels[i] * 100 + exps[i];
        const int then = m.start_level * 100 + m.start_exp;
        gains[i] = std::max(0, now - then);
        total += gains[i];
        level_sum += levels[i];
        count++;
    }
    if (count < 2 || total == 0) {
        std::fprintf(stderr, "[exp] battle end: %d members, %d EXP gained, nothing to share\n",
                     count, total);
        return;
    }

    // The game's own end-of-battle weighting (func_ovlJ_801D5DFC):
    // max(1, level difference + 8)^2, here against the unit's average level.
    double weights[MEMBERS] = {};
    double weight_sum = 0.0;
    const double average = static_cast<double>(level_sum) / count;
    for (int i = 0; i < MEMBERS; i++) {
        if (g_members[i].record == 0) {
            continue;
        }
        const double diff = std::max(1.0, average - levels[i] + 8.0);
        weights[i] = diff * diff;
        weight_sum += weights[i];
    }

    std::fprintf(stderr, "[exp] battle end: %d EXP across %d members\n", total, count);
    for (int i = 0; i < MEMBERS; i++) {
        Member& m = g_members[i];
        if (m.record == 0) {
            continue;
        }
        const int target_gain = static_cast<int>(total * weights[i] / weight_sum + 0.5);
        // Move toward the share inside the current level only.
        const int adjusted = std::clamp(exps[i] + (target_gain - gains[i]), 0, 99);
        m.target_exp = adjusted;
        std::fprintf(stderr, "[exp]   #%d L%d: gained %d, share %d, EXP %d -> %d\n", m.record,
                     levels[i], gains[i], target_gain, exps[i], adjusted);
    }
}

void hold(uint8_t* rdram) {
    for (const Member& m : g_members) {
        if (m.record == 0 || m.target_exp < 0) {
            continue;
        }
        const uint32_t address = char_record(m.record) + CHAR_EXP;
        if (rd8(rdram, address) != m.target_exp) {
            wr8(rdram, address, static_cast<uint8_t>(m.target_exp));
        }
    }
}

}  // namespace

void tick(uint8_t* rdram) {
    // SHARED EXP (GAMEPLAY tab); OGRE_SHARED_EXP forces it.
    if (rdram == nullptr || !feature_enabled(Choice::SharedExp, "OGRE_SHARED_EXP")) {
        return;
    }
    const int scene = rd16(rdram, SCENE_ADDRESS);
    const bool battle = scene == SCENE_BATTLE && recomp::overlays::is_function_bank_loaded(BATTLE_BANK_ROM);
    if (battle && !g_in_battle) {
        g_in_battle = true;
        g_hold = 0;
        begin_battle(rdram);
    }
    else if (!battle && g_in_battle) {
        g_in_battle = false;
        g_settle = SETTLE_FRAMES;
    }
    g_last_scene = scene;
    if (g_settle > 0 && --g_settle == 0) {
        share(rdram);
        g_hold = HOLD_FRAMES;
    }
    if (g_hold > 0) {
        hold(rdram);
        g_hold--;
    }
}

}  // namespace ogre::exp_share

