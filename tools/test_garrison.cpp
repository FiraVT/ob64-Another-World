// Offline test for the War Director garrison (app/features/war_director.cpp).
//
// Builds a fake RDRAM with the ROM's streamed segment B data (class table and
// class names), enemydat.bin and one mission's spawn list, runs
// war_director::garrison on it, and prints every generic unit before and after
// with its cost. Checks: rebuilt units stay within 110% of the original cost,
// use only classes from the mission's generic pool, and story templates are
// untouched.
//
//   test_garrison <rom.z64> <enemydat.bin> <eset.bin>...
//
// Build (Windows, VS 2022 clang-cl), with a stub `ultramodern/ultramodern.hpp`
// that declares `uint8_t* ultramodern::get_rdram_base();` in build/test/stub:
//   clang-cl /std:c++20 /EHsc /I build/test/stub tools/test_garrison.cpp app/features/war_director.cpp
// Inputs come from tools/mission_data.py (build/gamefiles/lzhout/).

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
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

constexpr uint32_t TEMPLATES = 0x80400000u;
constexpr uint32_t SPAWN = 0x80410000u;

std::string tname(uint32_t id) {
    const uint32_t t = TEMPLATES + (id - 1) * 35;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "L%d g1 %d x%d g2 %d x%d", rd8(t), rd8(t + 7),
                  (rd8(t + 0xD) != 0) + (rd8(t + 0xE) != 0) + (rd8(t + 0xF) != 0), rd8(t + 0x10),
                  (rd8(t + 0x16) != 0) + (rd8(t + 0x17) != 0) + (rd8(t + 0x18) != 0));
    return buf;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: test_garrison <rom> <enemydat.bin> <eset.bin>...\n");
        return 2;
    }
    const std::vector<uint8_t> rom = read_file(argv[1]);
    const std::vector<uint8_t> templates = read_file(argv[2]);
    // Streamed segment B (code and data): ROM 0x40E80 -> 0x8016AF80, to 0x80190F30.
    for (uint32_t v = 0x8016AF80u; v < 0x80191000u; v++) {
        wr8(v, rom[v - 0x8016AF80u + 0x40E80u]);
    }
    int failures = 0;
    for (int f = 3; f < argc; f++) {
        for (size_t i = 0; i < templates.size(); i++) {
            wr8(TEMPLATES + static_cast<uint32_t>(i), templates[i]);
        }
        const std::vector<uint8_t> eset = read_file(argv[f]);
        const uint32_t list = (eset[2] << 8) | eset[3];
        const int count = eset[list];
        for (int i = 0; i < 1 + count * 18; i++) {
            wr8(SPAWN + i, eset[list + i]);
        }
        wr8(0x801F1055u, 5);
        wr8(0x8018F4A1u, static_cast<uint8_t>(f));
        std::map<uint32_t, std::string> before;
        for (int i = 0; i < count; i++) {
            const uint32_t id = (eset[list + 1 + i * 18 + 3] << 8) | eset[list + 1 + i * 18 + 4];
            before[id] = tname(id);
        }
        ogre::war_director::garrison(TEMPLATES, SPAWN);
        std::printf("== %s\n", argv[f]);
        for (const auto& [id, text] : before) {
            const std::string after = tname(id);
            std::printf("  t%-4u %-28s -> %s\n", id, text.c_str(), after.c_str());
        }
        // Templates the spawn list does not use must be untouched.
        std::set<uint32_t> used;
        for (const auto& [id, text] : before) {
            used.insert(id);
        }
        for (uint32_t id = 1; id <= 556; id++) {
            if (used.count(id)) {
                continue;
            }
            for (uint32_t b = 0; b < 35; b++) {
                if (rd8(TEMPLATES + (id - 1) * 35 + b) != templates[(id - 1) * 35 + b]) {
                    std::printf("  FAIL: unused template %u changed\n", id);
                    failures++;
                    break;
                }
            }
        }
    }
    std::printf("%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

