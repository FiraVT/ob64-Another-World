// Offline test for catch-up training (app/features/war_director.cpp, Training).
//
// Builds a fake RDRAM with the ROM's streamed segment B (class table), a
// roster, a training unit and one enemydat template copied where bank AH's
// builder puts it (sp+24), then calls the two hook entry points. Checks: the
// level follows the catch-up formula, the template is rebuilt only from
// bestiary classes and only for a marked fight, items are cleared, and a
// scripted encounter (u16 0x80197B80 set) is left alone.
//
//   test_training <rom.z64> <enemydat.bin>
//
// Build (Windows, VS 2022 clang-cl), with the stub from tools/test_garrison.cpp:
//   clang-cl /std:c++20 /EHsc /I build/test/stub tools/test_training.cpp app/features/war_director.cpp

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
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

constexpr uint32_t STACK_TEMPLATE = 0x80300018u;
constexpr uint32_t PLAYER_CHARS = 0x80193BE0u;
constexpr uint32_t UNITS = 0x80197210u;

void set_char(int ref, int cls, int level) {
    wr8(PLAYER_CHARS + ref * 0x38u + 0x11u, static_cast<uint8_t>(cls));
    wr8(PLAYER_CHARS + ref * 0x38u + 0x13u, static_cast<uint8_t>(level));
}

int failures = 0;

void expect(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: test_training <rom> <enemydat.bin>\n");
        return 2;
    }
    const std::vector<uint8_t> rom = read_file(argv[1]);
    const std::vector<uint8_t> templates = read_file(argv[2]);
    for (uint32_t v = 0x8016AF80u; v < 0x80191000u; v++) {
        wr8(v, rom[v - 0x8016AF80u + 0x40E80u]);
    }
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "ogre_test_training";
    std::filesystem::create_directories(dir);
    {
        // Bestiary: Fighter (2), Knight (5), Archer (13) and Young Dragon (56)
        // lead; Soldier (1), Fighter (2) and Hawkman (39) serve.
        std::ofstream out(dir / "bestiary.cfg", std::ios::trunc);
        out << "leader 2\nleader 5\nleader 13\nleader 56\nmember 1\nmember 2\nmember 39\n";
    }
    ogre::war_director::init(dir);
    const std::set<int> leaders = {2, 5, 13, 56};
    const std::set<int> members = {0, 1, 2, 39};

    // Roster: ten characters at level 20, and a training unit (player unit 3)
    // of five characters at level 6..10.
    for (int ref = 1; ref <= 10; ref++) {
        set_char(ref, 5, 20);
    }
    for (int i = 0; i < 5; i++) {
        set_char(20 + i, 1, 6 + i);
    }
    wr8(0x801976FCu, 3);
    for (uint32_t i = 0; i < 5; i++) {
        wr8(UNITS + 3u * 0x19u + 0x02u + i, static_cast<uint8_t>(20 + i));
    }

    auto load_template = [&](int id) {
        for (uint32_t b = 0; b < 35; b++) {
            wr8(STACK_TEMPLATE + b, templates[(id - 1) * 35 + b]);
        }
    };

    // Template 12: a generic unit. Vanilla level = highest member, 10.
    std::printf("== behind the army: unit average 8, army 20\n");
    load_template(12);
    ogre::war_director::training_mark();
    int level = ogre::war_director::training_build(STACK_TEMPLATE, 10);
    expect(level == 8 + 6, "level = 8 + min(6, round((20 - 8) / 2)) = 14");
    expect(leaders.count(rd8(STACK_TEMPLATE)) == 1, "leader from the bestiary");
    expect(members.count(rd8(STACK_TEMPLATE + 7)) == 1 && members.count(rd8(STACK_TEMPLATE + 0x10)) == 1,
           "member groups from the bestiary");
    expect(rd8(STACK_TEMPLATE + 2) == 0 && rd8(STACK_TEMPLATE + 5) == 0, "leader items cleared");

    std::printf("== at the army's level\n");
    for (int i = 0; i < 5; i++) {
        set_char(20 + i, 1, 20);
    }
    load_template(12);
    ogre::war_director::training_mark();
    level = ogre::war_director::training_build(STACK_TEMPLATE, 20);
    expect(level == 20, "level = own average 20, no bonus");

    std::printf("== not marked (a battle that is not training)\n");
    load_template(12);
    const uint8_t before = rd8(STACK_TEMPLATE);
    level = ogre::war_director::training_build(STACK_TEMPLATE, 17);
    expect(level == 17 && rd8(STACK_TEMPLATE) == before, "level and template unchanged");

    std::printf("== scripted encounter (0x80197B80 set)\n");
    wr8(0x80197B80u, 0x00);
    wr8(0x80197B81u, 0x2A);
    load_template(12);
    ogre::war_director::training_mark();
    level = ogre::war_director::training_build(STACK_TEMPLATE, 17);
    expect(level == 17 && rd8(STACK_TEMPLATE) == before, "level and template unchanged");

    std::printf("%s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}

