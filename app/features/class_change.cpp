// New classes in Change Class. See class_change.hpp.

#include "class_change.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>
#include <span>

#include <librecomp/game.hpp>

namespace ogre::class_change {

namespace {

// The family table: 81 big-endian u16 in bank S data (0x801EF2EC; bank S is
// ROM 0x87220 at RAM 0x8019A7C0), then 0xFFFF.
constexpr uint32_t FAMILY_ROM = 0x87220u + (0x801EF2ECu - 0x8019A7C0u);
constexpr uint16_t STANDARD_COUNT = 81;
constexpr uint32_t REQUIREMENTS_ASSET = 0x01E398D2u;
constexpr uint32_t RECORD_SIZE = 8;
constexpr uint32_t IDS = 256;

std::mutex g_mutex;
std::vector<Standard> g_classes;

// The size the last size call gave func_8009DD38 for the requirements asset,
// on this thread: the fill writes only into a buffer of that size.
thread_local uint32_t t_grown = 0;

const Standard* find(uint16_t cls) {
    for (const Standard& s : g_classes) {
        if (s.id == cls) {
            return &s;
        }
    }
    return nullptr;
}

}  // namespace

uint16_t original_family(uint16_t cls) {
    if (cls >= STANDARD_COUNT) {
        return 0;
    }
    const std::span<const uint8_t> rom = recomp::get_rom();
    const size_t at = FAMILY_ROM + static_cast<size_t>(cls) * 2;
    if (at + 2 > rom.size()) {
        return 0;
    }
    return static_cast<uint16_t>(rom[at] << 8 | rom[at + 1]);
}

void set_standard_classes(std::vector<Standard> classes) {
    std::lock_guard lock(g_mutex);
    if (classes.size() != g_classes.size() ||
        !std::equal(classes.begin(), classes.end(), g_classes.begin(), [](const Standard& a, const Standard& b) {
            return a.id == b.id && a.family_of == b.family_of && a.requirements == b.requirements;
        })) {
        for (const Standard& s : classes) {
            std::fprintf(stderr, "[content] class %u is offered in Change Class (family of class %u)\n", s.id,
                         s.family_of);
        }
    }
    g_classes = std::move(classes);
}

std::vector<Standard> standard_classes() {
    std::lock_guard lock(g_mutex);
    return g_classes;
}

}  // namespace ogre::class_change

// tools/relocate_tables.py: the classic Change Class list's two reads of the
// class line table go to a copy at 0x801EF2EC + (shift << 16), and its bound on
// the current class rises with it. Without standard new classes the shift is
// 0 and the bound 81, the original code. The class key table's 14 readers go
// to a copy the same way while any new class exists.
extern "C" unsigned int ogre_family_table_shift = 0;
extern "C" unsigned int ogre_family_bound = 81;
extern "C" unsigned int ogre_class_key_shift = 0;

namespace ogre::class_change {
namespace {

constexpr uint32_t FAMILY_TABLE = 0x801EF2ECu;
constexpr uint32_t FAMILY_COPY = 0x806FF2ECu;  // content arena, above the names (game_addresses.hpp)
// The class key table: a byte per class in bank S data (0x801EF3D8), 0 for an
// ordinary class; Change Class and Organize read it 14 times by class id. Its
// 165 entries are followed by other data, nonzero from class 187 on, so a new
// class there would read as keyed. The copy holds 256 entries, 0 past 164.
constexpr uint32_t KEY_TABLE = 0x801EF3D8u;
constexpr uint32_t KEY_TABLE_ROM = 0x87220u + (KEY_TABLE - 0x8019A7C0u);
constexpr uint32_t KEY_COPY = 0x806EF3D8u;
constexpr uint16_t ROM_CLASSES = 165;
constexpr uint32_t KNOWN_CLASSES = 0x80196A96u;
constexpr uint32_t KNOWN_BYTES = 21;  // classes 0..167

bool g_new_classes = false;

void put8(uint8_t* rdram, uint32_t at, uint8_t v) {
    rdram[(at & 0x1FFFFFFFu) ^ 3u] = v;
}

}  // namespace

bool is_standard(uint16_t cls) {
    std::lock_guard lock(g_mutex);
    return find(cls) != nullptr;
}

void set_new_classes_present(bool present) {
    std::lock_guard lock(g_mutex);
    g_new_classes = present;
}

// Writes the copies and sets the shifts: the class line table (256 entries:
// the ROM's 81, 0 (no line) for classes 81..254 except the standard new
// classes, which take their family_of class's entry, and 0xFFFF at 255, where
// the list's walk stops), and the class key table.
void write_tables(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }
    std::lock_guard lock(g_mutex);
    if (g_new_classes) {
        const std::span<const uint8_t> rom = recomp::get_rom();
        for (uint32_t i = 0; i < IDS; i++) {
            const size_t at = KEY_TABLE_ROM + i;
            put8(rdram, KEY_COPY + i, i < ROM_CLASSES && at < rom.size() ? rom[at] : 0);
        }
        ogre_class_key_shift = (KEY_COPY - KEY_TABLE) >> 16;
    } else {
        ogre_class_key_shift = 0;
    }
    if (g_classes.empty()) {
        ogre_family_table_shift = 0;
        ogre_family_bound = STANDARD_COUNT;
        return;
    }
    std::array<uint16_t, IDS> entries{};
    for (uint16_t cls = 0; cls < STANDARD_COUNT; cls++) {
        entries[cls] = original_family(cls);
    }
    for (const Standard& s : g_classes) {
        if (s.id >= STANDARD_COUNT && s.id < IDS - 1) {
            entries[s.id] = original_family(s.family_of);
        }
    }
    entries[IDS - 1] = 0xFFFFu;
    for (uint32_t i = 0; i < IDS; i++) {
        put8(rdram, FAMILY_COPY + i * 2, static_cast<uint8_t>(entries[i] >> 8));
        put8(rdram, FAMILY_COPY + i * 2 + 1, static_cast<uint8_t>(entries[i]));
    }
    ogre_family_table_shift = (FAMILY_COPY - FAMILY_TABLE) >> 16;
    ogre_family_bound = IDS - 1;
}

}  // namespace ogre::class_change

// config/hooks.toml: the entry of func_ovlS_801D2808 (the classic class list).
// The content database's tick writes the copies every frame; this writes them
// once more before the list reads them.
extern "C" void ogre_family_table_prepare(uint8_t* rdram) {
    ogre::class_change::write_tables(rdram);
}

// tools/relocate_tables.py: the "class < 81" tests that decide what a
// character of a class may do in Organize (func_ovlS_801D379C and others,
// design-new-classes.md 2d) and the generic name of a new character
// (func_8016BD20). A standard new class counts as standard.
extern "C" uint32_t ogre_class_standard(uint32_t cls) {
    using namespace ogre::class_change;
    cls &= 0xFFu;
    if (cls < STANDARD_COUNT) {
        return 1;
    }
    std::lock_guard lock(g_mutex);
    return find(static_cast<uint16_t>(cls)) != nullptr ? 1 : 0;
}

// tools/relocate_tables.py: func_ovlS_801D379C reads the character's class
// key byte (+0x12) and refuses Change Class for 36..38 and 42 and up. A
// standard new class answers as its family_of class.
extern "C" uint32_t ogre_class_line_key(uint32_t key) {
    using namespace ogre::class_change;
    key &= 0xFFu;
    if (key < STANDARD_COUNT) {
        return key;
    }
    std::lock_guard lock(g_mutex);
    const Standard* s = find(static_cast<uint16_t>(key));
    return s != nullptr ? s->family_of : key;
}

// config/hooks.toml: in func_ovlS_801D2808, after the load of the seen-classes
// byte at `base` + 0x6A96. The bitfield holds classes 0..167; for a later
// byte the port answers: a standard new class is seen.
extern "C" uint32_t ogre_known_classes_byte(uint32_t base, uint32_t loaded) {
    using namespace ogre::class_change;
    const uint32_t index = base + 0x6A96u - KNOWN_CLASSES;
    if (index < KNOWN_BYTES) {
        return loaded;
    }
    std::lock_guard lock(g_mutex);
    uint32_t bits = 0;
    for (const Standard& s : g_classes) {
        if (s.id / 8u == index) {
            bits |= 1u << (s.id % 8u);
        }
    }
    return bits;
}

// config/hooks.toml: the returns of func_ovlS_801CF310 (`high` 1: the entry's
// high byte & 0xF, the class's line) and func_ovlS_801CF340 (`high` 0: the low
// byte, the class's property mask). `value` is what the game computed, 0 for a
// class of 81 or more.
extern "C" uint32_t ogre_class_family(uint32_t cls, uint32_t high, uint32_t value) {
    using namespace ogre::class_change;
    cls &= 0xFFu;
    if (cls < STANDARD_COUNT) {
        return value;
    }
    std::lock_guard lock(g_mutex);
    const Standard* s = find(static_cast<uint16_t>(cls));
    if (s == nullptr) {
        return value;
    }
    const uint16_t family = original_family(s->family_of);
    return high != 0 ? (family >> 8) & 0xFu : family & 0xFFu;
}

// config/hooks.toml: in func_8009DD38, after the decoded size of `asset`
// (func_8007A7E0). Returns the size to allocate: room for 256 requirement
// records when new classes are offered in Change Class.
extern "C" uint32_t ogre_requirements_size(uint32_t asset, uint32_t size) {
    using namespace ogre::class_change;
    t_grown = 0;
    if (asset != REQUIREMENTS_ASSET) {
        return size;
    }
    std::lock_guard lock(g_mutex);
    if (g_classes.empty() || size >= IDS * RECORD_SIZE) {
        return size;
    }
    t_grown = IDS * RECORD_SIZE;
    return t_grown;
}

// config/hooks.toml: in func_8009DD38, after the decode into `dst`. Writes
// the records past the asset's 81: 0xFF (no requirement) for every class but
// the standard new ones, which get their own.
extern "C" void ogre_requirements_fill(uint8_t* rdram, uint32_t asset, uint32_t dst) {
    using namespace ogre::class_change;
    const uint32_t grown = t_grown;
    t_grown = 0;
    if (asset != REQUIREMENTS_ASSET || grown == 0 || dst < 0x80000000u || dst + grown > 0x80800000u) {
        return;
    }
    std::lock_guard lock(g_mutex);
    const auto put = [&](uint32_t at, uint8_t v) { rdram[((dst + at) & 0x1FFFFFFFu) ^ 3u] = v; };
    for (uint32_t at = STANDARD_COUNT * RECORD_SIZE; at < grown; at++) {
        put(at, 0xFFu);
    }
    for (const Standard& s : g_classes) {
        if (s.id >= STANDARD_COUNT && (s.id + 1u) * RECORD_SIZE <= grown) {
            for (uint32_t k = 0; k < RECORD_SIZE; k++) {
                put(s.id * RECORD_SIZE + k, s.requirements[k]);
            }
        }
    }
}

namespace {

// The diagnostics' last result per (character, check), so a check run every
// frame logs once per change.
std::mutex g_log_mutex;
std::map<uint32_t, uint32_t> g_last;

int character_class(const uint8_t* rdram, uint32_t character) {
    return rdram[((0x80193BE0u + (character & 0xFFu) * 0x38u + 0x11u) & 0x1FFFFFFFu) ^ 3u];
}

bool changed(uint32_t key, uint32_t value) {
    std::lock_guard lock(g_log_mutex);
    const auto it = g_last.find(key);
    if (it != g_last.end() && it->second == value) {
        return false;
    }
    g_last[key] = value;
    return true;
}

}  // namespace

// config/hooks.toml: the exit of func_ovlS_801D2378 (DIAGNOSTIC).
extern "C" void ogre_class_check_log(uint8_t* rdram, uint32_t character, uint32_t target, uint32_t result) {
    const int cls = character_class(rdram, character);
    if (cls < 81 && target < 81) {
        return;
    }
    const uint32_t organize = (static_cast<uint32_t>(rdram[0x00196B18u ^ 3u]) << 24) |
                              (static_cast<uint32_t>(rdram[0x00196B19u ^ 3u]) << 16) |
                              (static_cast<uint32_t>(rdram[0x00196B1Au ^ 3u]) << 8) | rdram[0x00196B1Bu ^ 3u];
    const int reason = organize >= 0x80000000u && organize < 0x80800000u
                           ? rdram[((organize + 0x5EDu) & 0x1FFFFFFFu) ^ 3u]
                           : -1;
    const uint32_t value = (result & 0xFFu) << 8 | static_cast<uint32_t>(reason & 0xFF);
    if (changed(0x10000u | (character & 0xFFu) << 8 | (target & 0xFFu), value)) {
        std::fprintf(stderr, "[change] check: character %u (class %d) to class %u: %s, reason %d\n", character & 0xFFu,
                     cls, target & 0xFFu, (result & 0xFFu) != 0 ? "passes" : "fails", reason);
    }
}

// config/hooks.toml: the exit of func_ovlS_801D379C (DIAGNOSTIC).
extern "C" void ogre_class_option_log(uint8_t* rdram, uint32_t character, uint32_t option, uint32_t result) {
    if ((character & 0xFFu) >= 100 || character_class(rdram, character) < 81) {
        return;
    }
    if (changed(0x20000u | (character & 0xFFu) << 8 | (option & 0xFFu), result)) {
        std::fprintf(stderr, "[change] option %u for character %u (class %d): %s %d\n", option & 0xFFu,
                     character & 0xFFu, character_class(rdram, character),
                     static_cast<int32_t>(result) < 0 ? "allowed" : "refused, code", static_cast<int32_t>(result));
    }
}
