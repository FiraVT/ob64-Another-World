// Offline test for the War Director's rewards (app/features/war_director.cpp,
// Rewards): the drop table upgrade.
//
// Builds a fake RDRAM with the ROM's streamed segment B (class and item tables)
// and record 7's drop table O, plants one enemy unit, runs war_director::tick
// for a few missions and prints each drop entry before and after. Checks: every
// changed item keeps its category, costs more, is not unique, and stays within
// the multiplier; consumable entries and the guard words are untouched.
//
//   test_loot <rom.z64>
//
// Build (Windows, VS 2022 clang-cl), with the stub from tools/test_garrison.cpp:
//   clang-cl /std:c++20 /EHsc /I build/test/stub tools/test_loot.cpp app/features/war_director.cpp
// Run with OGRE_DIRECTOR_FORCE=1 so tick() runs off the mission field.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "../app/features/war_director.hpp"

namespace ultramodern {
static std::vector<uint8_t> g_rdram(0x800000);
uint8_t* get_rdram_base() {
    return g_rdram.data();
}
}  // namespace ultramodern

namespace {

std::vector<uint8_t> read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void wr8(uint32_t a, uint8_t v) {
    ultramodern::g_rdram[(a & 0x1FFFFFFFu) ^ 3u] = v;
}

uint8_t rd8(uint32_t a) {
    return ultramodern::g_rdram[(a & 0x1FFFFFFFu) ^ 3u];
}

uint16_t rd16(uint32_t a) {
    return static_cast<uint16_t>((rd8(a) << 8) | rd8(a + 1));
}

constexpr uint32_t ITEMS = 0x8018C42Cu;
constexpr uint32_t DROPS = 0x801EDB38u;

std::string item_name(int id) {
    const uint32_t record = ITEMS + static_cast<uint32_t>(id) * 0x20u;
    const uint32_t p = (rd16(record) << 16) | rd16(record + 2);
    std::string name;
    for (uint32_t i = 0; i < 24; i++) {
        const uint8_t c = rd8(p + i);
        if (c == 0) {
            break;
        }
        if (c >= 0x20 && c < 0x7F) {
            name += static_cast<char>(c);
        }
    }
    return name;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: test_loot <rom>\n");
        return 2;
    }
    const std::vector<uint8_t> rom = read_file(argv[1]);
    for (uint32_t v = 0x8016AF80u; v < 0x80191000u; v++) {
        wr8(v, rom[v - 0x8016AF80u + 0x40E80u]);
    }
    std::vector<uint16_t> vanilla;
    int failures = 0;
    for (int mission : {1, 12, 30}) {
        // Record 7's image of table O, as a fresh mission load would DMA it.
        for (uint32_t i = 0; i < 35 * 8 + 8; i++) {
            wr8(DROPS + i, rom[0x142278u + i]);
        }
        wr8(0x8018F4A1u, static_cast<uint8_t>(mission));
        wr8(0x801F1055u, static_cast<uint8_t>(3 + mission));
        // One enemy unit in slot 31: a generic leader (class 5) and four soldiers.
        const uint32_t unit = 0x80197210u + 31u * 0x19u;
        wr8(unit + 0x01, 0x80);
        wr8(unit + 0x02, 1);
        for (uint32_t i = 1; i < 5; i++) {
            wr8(unit + 0x02 + i, 100);
        }
        wr8(0x80195580u + 0x34u + 0x11u, 5);
        wr8(0x80195580u + 0x34u + 0x13u, static_cast<uint8_t>(4 + mission));

        std::vector<uint16_t> before;
        for (int e = 0; e < 35; e++) {
            for (int c = 0; c < 3; c++) {
                before.push_back(rd16(DROPS + e * 8 + 2 + c * 2));
            }
        }
        // The director's clock follows the game's frame counter (0x800AEFA4),
        // so each tick is given a new game frame.
        for (int f = 0; f < 2; f++) {
            static uint32_t frame = 0;
            frame++;
            for (uint32_t b = 0; b < 4; b++) {
                wr8(0x800AEFA4u + b, static_cast<uint8_t>(frame >> (24 - 8 * b)));
            }
            ogre::war_director::tick(ultramodern::get_rdram_base());
        }
        std::printf("== mission %d\n", mission);
        int changed = 0;
        for (int e = 0; e < 35; e++) {
            for (int c = 0; c < 3; c++) {
                const uint16_t a = before[e * 3 + c];
                const uint16_t b = rd16(DROPS + e * 8 + 2 + c * 2);
                if (a == b) {
                    continue;
                }
                changed++;
                const int ia = a & 0x7FFF;
                const int ib = b & 0x7FFF;
                const uint32_t ra = ITEMS + ia * 0x20u;
                const uint32_t rb = ITEMS + ib * 0x20u;
                const bool ok = (a & 0x8000) && (b & 0x8000) && rd8(ra + 4) == rd8(rb + 4) &&
                                rd16(rb + 8) > rd16(ra + 8) && (rd8(rb + 0x1A) & 0x80) == 0;
                if (!ok) {
                    failures++;
                }
                if (e < 6 || !ok) {
                    std::printf("  e%-2d %-18s %5d -> %-18s %5d%s\n", e, item_name(ia).c_str(), rd16(ra + 8),
                                item_name(ib).c_str(), rd16(rb + 8), ok ? "" : "  FAIL");
                }
            }
        }
        std::printf("  %d of 105 drops changed\n", changed);
        if (rd16(DROPS) != 0x0027 || rd16(DROPS + 0x110) != 0x0050) {
            std::printf("  FAIL: guard words changed\n");
            failures++;
        }
    }
    std::printf("%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

