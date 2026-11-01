// The army's characters for the ROSTER tab. See army_roster.hpp.

#include "game_addresses.hpp"
#include "army_roster.hpp"

#include <cstdint>

#include "ultramodern/ultramodern.hpp"

namespace ogre::army_roster {

namespace {

constexpr uint32_t CHAR_TABLE = ::ogre::addr::kCharacters;
constexpr uint32_t CHAR_STRIDE = 0x38u;
constexpr int CHAR_FIRST = 1;
constexpr int CHAR_COUNT = 100;
constexpr uint32_t CHAR_NAME = 0x00u;
constexpr uint32_t CHAR_CLASS = 0x11u;
constexpr uint32_t CHAR_LEVEL = 0x13u;

constexpr uint32_t CLASS_TABLE = ::ogre::addr::kClasses;
constexpr uint32_t CLASS_STRIDE = 0x48u;
constexpr int CLASS_COUNT = 165;

// RDRAM is byte-reversed in 32-bit words: guest byte `a` is at `(a ^ 3)`.
uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd8(rdram, address)) << 24) | (static_cast<uint32_t>(rd8(rdram, address + 1)) << 16) |
           (static_cast<uint32_t>(rd8(rdram, address + 2)) << 8) | rd8(rdram, address + 3);
}

std::string guest_string(const uint8_t* rdram, uint32_t address, size_t max_len) {
    std::string text;
    for (size_t i = 0; i < max_len; i++) {
        const uint8_t c = rd8(rdram, address + static_cast<uint32_t>(i));
        if (c == 0) {
            break;
        }
        // The game marks a space in some class names with 0x81 (two-byte
        // font escape); anything else outside ASCII ends the name.
        if (c == 0x81) {
            text += ' ';
            continue;
        }
        if (c < 0x20 || c > 0x7E) {
            break;
        }
        text += static_cast<char>(c);
    }
    return text;
}

std::string class_name(const uint8_t* rdram, int cls) {
    const uint32_t pointer = rd32(rdram, CLASS_TABLE + static_cast<uint32_t>(cls) * CLASS_STRIDE);
    std::string name;
    if (pointer >= 0x80000000u && pointer < 0x80800000u) {
        name = guest_string(rdram, pointer, 20);
    }
    if (name.empty()) {
        name = "Class " + std::to_string(cls);
    }
    return name;
}

}  // namespace

std::vector<Character> read() {
    std::vector<Character> list;
    const uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram == nullptr) {
        return list;
    }
    for (int n = CHAR_FIRST; n < CHAR_COUNT; n++) {
        const uint32_t record = CHAR_TABLE + static_cast<uint32_t>(n) * CHAR_STRIDE;
        const int cls = rd8(rdram, record + CHAR_CLASS);
        if (cls == 0 || cls >= CLASS_COUNT) {
            continue;
        }
        Character member;
        member.record = n;
        member.name = guest_string(rdram, record + CHAR_NAME, 16);
        if (member.name.empty()) {
            member.name = "Character " + std::to_string(n);
        }
        member.class_name = class_name(rdram, cls);
        member.level = rd8(rdram, record + CHAR_LEVEL);
        list.push_back(std::move(member));
    }
    return list;
}

}  // namespace ogre::army_roster

