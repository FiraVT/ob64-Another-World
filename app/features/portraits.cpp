// The game's speaker portraits, decoded from the ROM (2026-10-29; found from a
// texture/RAM capture of a dialogue with Magnus's portrait on screen).
//
// The game picks a portrait in func_8016FD0C (streamedB, 0x8016FD0C):
//   D_8018ED00 (ROM 0x64C00) is 6 bytes per class id. The halfword at +0 (mode
//   0) or +2 (mode 1) is v; v >> 3 is the portrait index and v & 7 the palette
//   variant. v == 0 means no portrait.
//   Asset 0x01DFB36E is the portrait id table: a raw (uncompressed) payload of
//   u32 asset ids, entry 0 unused. func_8007938C(0x01DFB36E, index, -10) loads
//   the indexed asset.
//   A portrait asset decompresses (the LZ format of func_8007A110) to a 40x48
//   CI8 image (0x780 bytes) followed by one or more 80-colour RGBA5551
//   palettes; the variant's palette is at 0x780 + variant * 0xA0.
// Asset ids map to ROM as (id & 0x0FFFFFFF) + 0x594250 (func_8009DAF4): the
// word there is the payload size and the payload starts 4 bytes later.
// Verified: class 0x51 (Magnus) -> index 72 -> asset 0x01E16552, whose decode
// equals the image and palette the game had in RDRAM at 0x803411F0.

#include "portraits.hpp"

#include "class_art.hpp"
#include "content_db.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <mutex>
#include <span>

#include <librecomp/game.hpp>

namespace ogre::portraits {
namespace {

constexpr uint32_t CLASS_TABLE_ROM = 0x64C00;  // D_8018ED00
constexpr uint32_t CLASS_STRIDE = 6;
constexpr int CLASS_COUNT = 256;
constexpr uint32_t ASSET_BASE = 0x594250;
constexpr uint32_t IMAGE_BYTES = WIDTH * HEIGHT;
constexpr uint32_t PALETTE_BYTES = 80 * 2;

uint32_t be32(std::span<const uint8_t> rom, size_t at) {
    return at + 4 <= rom.size()
               ? (uint32_t{rom[at]} << 24) | (uint32_t{rom[at + 1]} << 16) | (uint32_t{rom[at + 2]} << 8) | rom[at + 3]
               : 0;
}

uint16_t be16(std::span<const uint8_t> rom, size_t at) {
    return at + 2 <= rom.size() ? static_cast<uint16_t>((rom[at] << 8) | rom[at + 1]) : 0;
}

// A class's appearance row (D_8018ED00, 6 bytes) and size (class +0x04). The
// content database holds every class id, new classes (172 and up) included,
// with the packs applied; without it the ROM's rows serve, which end at 164.
bool class_look(int cls, std::array<uint8_t, 6>& row, int& size) {
    if (content::loaded()) {
        std::lock_guard<std::recursive_mutex> lock(content::records_mutex());
        const std::span<const uint8_t> bytes = content::current(content::Table::Appearances, static_cast<uint16_t>(cls)).bytes();
        std::copy_n(bytes.begin(), row.size(), row.begin());
        size = content::class_record(static_cast<uint16_t>(cls)).size();
        return true;
    }
    const std::span<const uint8_t> rom = recomp::get_rom();
    const size_t at = CLASS_TABLE_ROM + static_cast<size_t>(cls) * CLASS_STRIDE;
    const size_t size_at = 0x5DB34 + static_cast<size_t>(cls) * 0x48 + 4;  // the class table, RAM 0x80187C34
    if (cls >= 165 || at + row.size() > rom.size() || size_at >= rom.size()) {
        return false;
    }
    std::copy_n(rom.begin() + static_cast<std::ptrdiff_t>(at), row.size(), row.begin());
    size = rom[size_at];
    return true;
}

size_t asset_payload(uint32_t asset_id) {
    return (asset_id & 0x0FFFFFFFu) + ASSET_BASE + 4;
}

// func_8007A110's LZ format (tools/ogrelz.py documents each token).
bool decompress(std::span<const uint8_t> src, size_t pos, std::vector<uint8_t>& out) {
    const uint32_t size = be32(src, pos);
    // The equipment icon pack is 0x11700 bytes (277 icons), so the cap is
    // tools/ogrelz.py's 0x400000 rather than 64 KiB.
    if (size == 0 || size > 0x400000) {
        return false;
    }
    size_t p = pos + 4;
    out.clear();
    out.reserve(size);
    auto copy_back = [&](size_t offset, size_t length) {
        if (offset + 1 > out.size()) {
            return false;
        }
        const size_t from = out.size() - offset - 1;
        for (size_t i = 0; i < length; i++) {
            out.push_back(out[from + i]);
        }
        return true;
    };
    while (out.size() < size) {
        if (p + 3 > src.size()) {
            return false;
        }
        const uint8_t b = src[p++];
        if (b & 0x80) {
            if (!copy_back(((b & 7u) << 8) | src[p], ((b >> 3) & 0xFu) + 3)) {
                return false;
            }
            p += 1;
        }
        else if (b & 0x40) {
            const size_t length = (b & 0x3Fu) + 1;
            if (p + length > src.size()) {
                return false;
            }
            out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(p),
                       src.begin() + static_cast<std::ptrdiff_t>(p + length));
            p += length;
        }
        else if (b & 0x20) {
            out.insert(out.end(), (b & 0x1Fu) + 2, 0);
        }
        else if (b & 0x10) {
            const uint8_t hi = src[p];
            const uint8_t lo = src[p + 1];
            p += 2;
            if (!copy_back((static_cast<size_t>(hi & 0x3F) << 8) | lo, 4 + (b & 0x0Fu) + ((hi >> 2) & 0x30u))) {
                return false;
            }
        }
        else if (b == 0) {
            const size_t n = src[p];
            const size_t offset = (static_cast<size_t>(src[p + 1]) << 8) | src[p + 2];
            p += 3;
            if (!copy_back(offset, n + 5)) {
                return false;
            }
        }
        else if (b == 1) {
            out.insert(out.end(), static_cast<size_t>(src[p++]) + 3, 0xFF);
        }
        else if (b == 2) {
            out.insert(out.end(), static_cast<size_t>(src[p++]) + 3, 0);
        }
        else {
            return false;
        }
    }
    out.resize(size);
    return true;
}

// --- Formation sprites ---------------------------------------------------------
//
// The format is the one rempred/LordlyCaliber decodes in army-sprites.js (MIT):
// six LZ resources ("atlases"), each two 256-colour RGBA5551 palettes
// (0x000/0x200; palette 0 is the enemy's red, 1 the player's blue/green)
// followed by one CI8 plane per model at 0x400 + model * w*h, no frames. A
// class picks its model with the byte at +4 of its 6-byte route record (the
// table this file already reads for portraits, ROM 0x64C00): model = byte &
// 0x3F; the lane is "large" when the class size (class table +4) is 2,
// "special" when the byte has 0x80, else "ordinary". That project's offsets are
// for the US revision 0 ROM; this ROM is revision 1 (header byte 0x3F = 1), so
// the three front-view atlases were found here by their exact decoded sizes
// (0x400 + models * plane): ordinary 56 x 16x24 = 0x5800 at ROM 0x234F184,
// special 38 x 16x24 = 0x3D00 at 0x23571F8, large 26 x 32x28 = 0x5F00 at
// 0x2348678 (the LZ block's size word at each). Checked by rendering all
// models (2026-10-29).

struct Atlas {
    uint32_t rom;
    int width;
    int height;
    int models;
};
constexpr Atlas kOrdinary{0x234F184, 16, 24, 56};
constexpr Atlas kSpecial{0x23571F8, 16, 24, 38};
constexpr Atlas kLarge{0x2348678, 32, 28, 26};
constexpr int SPRITE_PALETTE = 1;

const std::vector<uint8_t>& atlas_data(const Atlas& atlas) {
    static std::mutex mutex;
    static std::array<std::vector<uint8_t>, 3> cache;
    static std::array<bool, 3> tried{};
    const int slot = atlas.rom == kOrdinary.rom ? 0 : atlas.rom == kSpecial.rom ? 1 : 2;
    std::lock_guard<std::mutex> lock(mutex);
    if (!tried[slot]) {
        tried[slot] = true;
        std::vector<uint8_t> out;
        const size_t expected = 0x400 + static_cast<size_t>(atlas.models) * atlas.width * atlas.height;
        // The atlas's asset (its size word is 4 bytes before the LZ block), or
        // the copy with new class figures appended (class_art.cpp).
        const uint32_t asset = class_art::redirect(atlas.rom - 4 - 0x594250u);
        const size_t at = (asset & 0x0FFFFFFFu) + 0x594250u + 4;
        if (decompress(recomp::get_rom(), at, out) && out.size() >= expected) {
            cache[slot] = std::move(out);
        }
    }
    return cache[slot];
}

}  // namespace

bool decode_sprite(int cls, std::vector<uint8_t>& rgba, int& width, int& height, int model_offset) {
    const std::span<const uint8_t> rom = recomp::get_rom();
    if (cls <= 0 || cls >= CLASS_COUNT || rom.empty()) {
        return false;
    }
    std::array<uint8_t, 6> row{};
    int size = 1;
    if (!class_look(cls, row, size)) {
        return false;
    }
    const uint8_t route = row[4];
    const Atlas& atlas = size == 2 ? kLarge : (route & 0x80) ? kSpecial : kOrdinary;
    const int model = (route & 0x3F) + (model_offset > 0 ? model_offset : 0);
    const std::vector<uint8_t>& data = atlas_data(atlas);
    // A new class's figure is past the ROM's models (class_art.cpp).
    if (data.empty() || 0x400 + static_cast<size_t>(model + 1) * atlas.width * atlas.height > data.size()) {
        return false;
    }
    const size_t plane = 0x400 + static_cast<size_t>(model) * atlas.width * atlas.height;
    width = atlas.width * SPRITE_SCALE;
    height = atlas.height * SPRITE_SCALE;
    rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    for (int y = 0; y < atlas.height; y++) {
        for (int x = 0; x < atlas.width; x++) {
            const uint8_t index = data[plane + static_cast<size_t>(y) * atlas.width + x];
            const size_t at = SPRITE_PALETTE * 0x200 + index * 2;
            const uint16_t colour = static_cast<uint16_t>((data[at] << 8) | data[at + 1]);
            const auto expand = [](uint32_t five) { return static_cast<uint8_t>((five << 3) | (five >> 2)); };
            const uint8_t px[4] = {expand((colour >> 11) & 31), expand((colour >> 6) & 31),
                                   expand((colour >> 1) & 31), static_cast<uint8_t>((colour & 1) ? 0xFF : 0)};
            for (int dy = 0; dy < SPRITE_SCALE; dy++) {
                for (int dx = 0; dx < SPRITE_SCALE; dx++) {
                    const size_t o = (static_cast<size_t>(y * SPRITE_SCALE + dy) * width + x * SPRITE_SCALE + dx) * 4;
                    // Premultiplied alpha, as RmlUi expects: a clear texel is all zero.
                    if (px[3] != 0) {
                        rgba[o + 0] = px[0];
                        rgba[o + 1] = px[1];
                        rgba[o + 2] = px[2];
                        rgba[o + 3] = 0xFF;
                    }
                }
            }
        }
    }
    return true;
}

// Item icons: rempred/LordlyCaliber art.js (MIT) documents two packs, each one
// 256-colour RGBA5551 palette (0x200 bytes) then a 16x16 CI8 plane per item
// (item id - 1): equipment, 277 icons, palette index 255 transparent; special
// items (consumables), 44 icons, index 0 transparent. Found in this revision 1
// ROM by decoded size: equipment 0x11700 at ROM 0x2382704, special 0x2E00 at
// 0x238BAD0 (checked by rendering: Heal Leaf, Heal Seed, Heal Pack...).
bool lz_decode(std::span<const uint8_t> src, size_t pos, std::vector<uint8_t>& out) {
    return decompress(src, pos, out);
}

bool decode_icon(bool equipment, int item, std::vector<uint8_t>& rgba) {
    static std::mutex mutex;
    static std::vector<uint8_t> packs[2];
    static bool tried[2] = {};
    const int which = equipment ? 0 : 1;
    const uint32_t rom_at = equipment ? 0x2382704u : 0x238BAD0u;
    const int count = equipment ? 277 : 44;
    const int transparent = equipment ? 255 : 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!tried[which]) {
            tried[which] = true;
            std::vector<uint8_t> out;
            if (decompress(recomp::get_rom(), rom_at, out) && out.size() == 0x200u + static_cast<size_t>(count) * 0x100) {
                packs[which] = std::move(out);
            }
        }
    }
    const std::vector<uint8_t>& data = packs[which];
    if (data.empty() || item <= 0 || item > count) {
        return false;
    }
    const size_t plane = 0x200 + static_cast<size_t>(item - 1) * 0x100;
    const int size = ICON_SIZE * ICON_SCALE;
    rgba.assign(static_cast<size_t>(size) * size * 4, 0);
    for (int y = 0; y < ICON_SIZE; y++) {
        for (int x = 0; x < ICON_SIZE; x++) {
            const uint8_t index = data[plane + static_cast<size_t>(y) * ICON_SIZE + x];
            if (index == transparent) {
                continue;
            }
            const uint16_t colour = static_cast<uint16_t>((data[index * 2] << 8) | data[index * 2 + 1]);
            const auto expand = [](uint32_t five) { return static_cast<uint8_t>((five << 3) | (five >> 2)); };
            const uint8_t px[4] = {expand((colour >> 11) & 31), expand((colour >> 6) & 31), expand((colour >> 1) & 31), 0xFF};
            for (int dy = 0; dy < ICON_SCALE; dy++) {
                for (int dx = 0; dx < ICON_SCALE; dx++) {
                    const size_t o = (static_cast<size_t>(y * ICON_SCALE + dy) * size + x * ICON_SCALE + dx) * 4;
                    rgba[o + 0] = px[0];
                    rgba[o + 1] = px[1];
                    rgba[o + 2] = px[2];
                    rgba[o + 3] = px[3];
                }
            }
        }
    }
    return true;
}

bool decode(int cls, int variant, std::vector<uint8_t>& rgba) {
    const std::span<const uint8_t> rom = recomp::get_rom();
    if (cls <= 0 || cls >= CLASS_COUNT || rom.empty()) {
        return false;
    }
    std::array<uint8_t, 6> row{};
    int size = 1;
    if (!class_look(cls, row, size)) {
        return false;
    }
    const uint16_t v = static_cast<uint16_t>(variant != 0 ? (row[2] << 8 | row[3]) : (row[0] << 8 | row[1]));
    const uint32_t index = v >> 3;
    const uint32_t palette = v & 7u;
    if (v == 0 || index == 0) {
        return false;
    }
    // The ROM's table, or the copy with new class portraits (class_art.cpp).
    const size_t table = asset_payload(class_art::portrait_table());
    const uint32_t table_bytes = be32(rom, table - 4);
    if (index * 4 + 4 > table_bytes) {
        return false;
    }
    const uint32_t asset = be32(rom, table + index * 4);
    std::vector<uint8_t> data;
    if (asset == 0 || !decompress(rom, asset_payload(asset), data)) {
        return false;
    }
    const size_t palette_at = IMAGE_BYTES + palette * PALETTE_BYTES;
    if (data.size() < palette_at + PALETTE_BYTES) {
        return false;
    }
    rgba.resize(static_cast<size_t>(IMAGE_BYTES) * 4);
    for (uint32_t i = 0; i < IMAGE_BYTES; i++) {
        const uint8_t texel = data[i];
        uint16_t colour = 0;
        if (texel < 80) {
            colour = static_cast<uint16_t>((data[palette_at + texel * 2] << 8) | data[palette_at + texel * 2 + 1]);
        }
        const auto expand = [](uint32_t five) { return static_cast<uint8_t>((five << 3) | (five >> 2)); };
        rgba[i * 4 + 0] = expand((colour >> 11) & 31);
        rgba[i * 4 + 1] = expand((colour >> 6) & 31);
        rgba[i * 4 + 2] = expand((colour >> 1) & 31);
        rgba[i * 4 + 3] = 0xFF;  // portraits are opaque; the game draws them over a frame
    }
    return true;
}

}  // namespace ogre::portraits

