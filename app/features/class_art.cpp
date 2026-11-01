// Art for new classes, built into the ROM image. See class_art.hpp.

#include "class_art.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <librecomp/game.hpp>

#include "class_animation.hpp"
#include "content_db.hpp"
#include "portraits.hpp"

// A single-header PNG decoder, static to this file (hd_backgrounds.cpp and RT64
// compile their own copies).
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#include "stb/stb_image.h"

// tools/relocate_tables.py: the three lui/ori pairs that name the portrait id
// table read its asset id from these (defined in content_db.cpp, which both
// builds compile; the ROM's table until build() moves it).
extern "C" unsigned int ogre_portrait_table_hi;
extern "C" unsigned int ogre_portrait_table_lo;

namespace ogre::class_art {

namespace {

// An asset id maps to ROM (id & 0x0FFFFFFF) + ASSET_BASE: a size word, then
// the payload (func_8009DAF4, func_8009DBB8).
constexpr uint32_t ASSET_BASE = 0x594250u;
constexpr uint32_t PORTRAIT_TABLE = 0x01DFB36Eu;
// The ROM's last data ends at 0x275D998; from here to the end of the 40 MiB
// image every byte is 0 or 0xFF (checked against the US revision 1 ROM).
constexpr size_t FREE_START = 0x2770000u;
constexpr int PORTRAIT_W = 40;
constexpr int PORTRAIT_H = 48;
constexpr size_t PORTRAIT_COLOURS = 80;
constexpr size_t PALETTE_BYTES = PORTRAIT_COLOURS * 2;

uint32_t g_table = PORTRAIT_TABLE;

void put32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}

// The game's LZ format (func_8007A110, tools/ogrelz.py) with literal runs only:
// the decoded size, then runs of up to 64 bytes, each led by 0x40 | (n - 1).
std::vector<uint8_t> lz_literal(std::span<const uint8_t> data) {
    std::vector<uint8_t> out;
    put32(out, static_cast<uint32_t>(data.size()));
    for (size_t at = 0; at < data.size(); at += 64) {
        const size_t n = std::min<size_t>(64, data.size() - at);
        out.push_back(static_cast<uint8_t>(0x40 | (n - 1)));
        out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(at),
                   data.begin() + static_cast<std::ptrdiff_t>(at + n));
    }
    return out;
}

// RGBA8888 -> RGBA5551, opaque.
uint16_t to5551(const uint8_t* px) {
    return static_cast<uint16_t>((px[0] >> 3) << 11 | (px[1] >> 3) << 6 | (px[2] >> 3) << 1 | 1);
}

int distance(uint16_t a, uint16_t b) {
    const auto c = [](uint16_t w, int shift) { return static_cast<int>((w >> shift) & 31); };
    const int dr = c(a, 11) - c(b, 11);
    const int dg = c(a, 6) - c(b, 6);
    const int db = c(a, 1) - c(b, 1);
    return dr * dr + dg * dg + db * db;
}

// A 40x48 PNG as a portrait asset's decoded bytes: the CI8 image (0x780 bytes)
// and one 80-colour RGBA5551 palette. Colours past 80 go to the nearest of the
// 80 most used.
bool portrait_bytes(const std::filesystem::path& png, std::vector<uint8_t>& out, std::string& error) {
    int w = 0;
    int h = 0;
    int comp = 0;
    uint8_t* data = stbi_load(png.string().c_str(), &w, &h, &comp, 4);
    if (data == nullptr) {
        error = std::string("cannot read the PNG: ") + stbi_failure_reason();
        return false;
    }
    if (w != PORTRAIT_W || h != PORTRAIT_H) {
        stbi_image_free(data);
        error = "a portrait is 40x48, this one is " + std::to_string(w) + "x" + std::to_string(h);
        return false;
    }
    std::map<uint16_t, int> counts;
    std::vector<uint16_t> pixels(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < pixels.size(); i++) {
        pixels[i] = to5551(data + i * 4);
        counts[pixels[i]]++;
    }
    stbi_image_free(data);
    std::vector<std::pair<int, uint16_t>> by_use;
    for (const auto& [colour, n] : counts) {
        by_use.emplace_back(-n, colour);
    }
    std::sort(by_use.begin(), by_use.end());
    std::vector<uint16_t> palette;
    for (size_t i = 0; i < by_use.size() && i < PORTRAIT_COLOURS; i++) {
        palette.push_back(by_use[i].second);
    }
    if (counts.size() > PORTRAIT_COLOURS) {
        std::fprintf(stderr, "[art] %s: %zu colours; the portrait keeps the 80 most used\n", png.string().c_str(),
                     counts.size());
    }
    out.assign(static_cast<size_t>(w) * h + PALETTE_BYTES, 0);
    for (size_t i = 0; i < pixels.size(); i++) {
        size_t best = 0;
        for (size_t k = 0; k < palette.size(); k++) {
            if (distance(palette[k], pixels[i]) < distance(palette[best], pixels[i])) {
                best = k;
            }
        }
        out[i] = static_cast<uint8_t>(best);
    }
    for (size_t k = 0; k < palette.size(); k++) {
        out[pixels.size() + k * 2] = static_cast<uint8_t>(palette[k] >> 8);
        out[pixels.size() + k * 2 + 1] = static_cast<uint8_t>(palette[k]);
    }
    return true;
}

// Writes an asset (the size word, then `payload`) at `cursor` in the ROM
// image and returns its id; 0 when it does not fit. The cursor stays 4-aligned.
uint32_t write_asset(std::span<uint8_t> rom, size_t& cursor, std::span<const uint8_t> payload) {
    const size_t size = 4 + payload.size();
    if (cursor + size > rom.size()) {
        return 0;
    }
    rom[cursor] = static_cast<uint8_t>(payload.size() >> 24);
    rom[cursor + 1] = static_cast<uint8_t>(payload.size() >> 16);
    rom[cursor + 2] = static_cast<uint8_t>(payload.size() >> 8);
    rom[cursor + 3] = static_cast<uint8_t>(payload.size());
    std::copy(payload.begin(), payload.end(), rom.begin() + static_cast<std::ptrdiff_t>(cursor + 4));
    const uint32_t id = static_cast<uint32_t>(cursor - ASSET_BASE);
    cursor = (cursor + size + 3) & ~size_t{3};
    return id;
}

// --- Formation figures ----------------------------------------------------------
//
// Each lane has two atlases (design-class-art.md 1.3): the enemy's front view
// (palette 0) and the army's back view (palette 1), each two 256-colour
// RGBA5551 palettes, then one CI8 plane per model at 0x400. A class's model is
// its appearance +4 & 0x3F, the same index in both. The army's ordinary atlas
// has a 0x200-byte tail after its 56 models (0x5800..0x5A00), so the first new
// model is 58, the first plane past the tail; models 56 and 57 stay clear.
// Only the ordinary lane takes new figures so far.
struct Lane {
    uint32_t front;
    uint32_t back;
    int width;
    int height;
    int front_clear;  // the palette index each atlas draws clear
    int back_clear;
};
constexpr Lane ORDINARY{0x01DBAF30u, 0x01DBEB94u, 16, 24, 0, 70};
constexpr int FIRST_NEW_MODEL = 58;
constexpr int LAST_MODEL = 63;  // appearance +4 & 0x3F

// asset -> its copy. Written once by build_once() on the main thread, read by
// the game's asset loaders (ogre_asset_redirect), so both take the lock.
std::mutex g_redirect_mutex;
std::map<uint32_t, uint32_t> g_redirect;

bool decode_asset(std::span<const uint8_t> rom, uint32_t asset, std::vector<uint8_t>& out) {
    return portraits::lz_decode(rom, (asset & 0x0FFFFFFFu) + ASSET_BASE + 4, out);
}

// A figure PNG as one CI8 plane in an atlas palette: a clear pixel (alpha
// under 128) gets the atlas's clear index, any other the nearest opaque
// palette colour.
bool figure_plane(const std::filesystem::path& png, int width, int height, std::span<const uint8_t> palette,
                  int clear, std::vector<uint8_t>& plane, std::string& error) {
    int w = 0;
    int h = 0;
    int comp = 0;
    uint8_t* data = stbi_load(png.string().c_str(), &w, &h, &comp, 4);
    if (data == nullptr) {
        error = std::string("cannot read the PNG: ") + stbi_failure_reason();
        return false;
    }
    if (w != width || h != height) {
        stbi_image_free(data);
        error = "a figure is " + std::to_string(width) + "x" + std::to_string(height) + ", this one is " +
                std::to_string(w) + "x" + std::to_string(h);
        return false;
    }
    std::vector<std::pair<uint16_t, uint8_t>> opaque;  // colour, index
    for (int i = 0; i < 256; i++) {
        const uint16_t c = static_cast<uint16_t>(palette[i * 2] << 8 | palette[i * 2 + 1]);
        if (i != clear && (c & 1) != 0) {
            opaque.emplace_back(c, static_cast<uint8_t>(i));
        }
    }
    plane.assign(static_cast<size_t>(w) * h, static_cast<uint8_t>(clear));
    std::map<uint16_t, uint8_t> memo;
    for (size_t i = 0; i < plane.size(); i++) {
        const uint8_t* px = data + i * 4;
        if (px[3] < 128 || opaque.empty()) {
            continue;
        }
        const uint16_t c = to5551(px);
        auto found = memo.find(c);
        if (found == memo.end()) {
            size_t best = 0;
            for (size_t k = 1; k < opaque.size(); k++) {
                if (distance(opaque[k].first, c) < distance(opaque[best].first, c)) {
                    best = k;
                }
            }
            found = memo.emplace(c, opaque[best].second).first;
        }
        plane[i] = found->second;
    }
    stbi_image_free(data);
    return true;
}

// Appends each new class's figures to copies of its lane's two atlases, writes
// the copies into the ROM's free end, redirects the atlases to them, and sets
// the classes' appearance +4 to their new model.
void build_figures(std::span<uint8_t> rom, std::span<const uint8_t> view, size_t& cursor,
                   const std::vector<content::NewClassArt>& classes, content::Pack& pack) {
    std::vector<uint8_t> front;
    std::vector<uint8_t> back;
    const size_t plane_size = static_cast<size_t>(ORDINARY.width) * ORDINARY.height;
    int next = FIRST_NEW_MODEL;
    for (const content::NewClassArt& art : classes) {
        if (art.figure_front.empty() && art.figure_back.empty()) {
            continue;
        }
        uint8_t route = 0;
        int size = 1;
        {
            std::lock_guard<std::recursive_mutex> lock(content::records_mutex());
            route = content::current(content::Table::Appearances, art.id).u8(4);
            size = content::class_record(art.id).size();
        }
        if (size == 2 || (route & 0x80) != 0) {
            std::fprintf(stderr, "[art] %s: only classes of the ordinary figure lane take new figures so far\n",
                         art.key.c_str());
            continue;
        }
        if (next > LAST_MODEL) {
            std::fprintf(stderr, "[art] %s: the ordinary atlases have no free model\n", art.key.c_str());
            continue;
        }
        if (front.empty() && (!decode_asset(view, ORDINARY.front, front) || !decode_asset(view, ORDINARY.back, back))) {
            std::fprintf(stderr, "[art] the ordinary figure atlases do not decode; new figures are off\n");
            return;
        }
        const int model = next;
        const size_t end = 0x400 + static_cast<size_t>(model + 1) * plane_size;
        const struct View {
            const std::filesystem::path* path;
            std::vector<uint8_t>* atlas;
            int palette;
            int clear;
        } views[] = {{&art.figure_front, &front, 0, ORDINARY.front_clear},
                     {&art.figure_back, &back, 1, ORDINARY.back_clear}};
        bool placed = false;
        for (const View& v : views) {
            if (v.atlas->size() < end) {
                v.atlas->resize(end, static_cast<uint8_t>(v.clear));
            }
            // Without its own file a view keeps the template's figure.
            std::vector<uint8_t> plane;
            std::string error;
            if (v.path->empty()) {
                const size_t from = 0x400 + static_cast<size_t>(route & 0x3F) * plane_size;
                plane.assign(v.atlas->begin() + static_cast<std::ptrdiff_t>(from),
                             v.atlas->begin() + static_cast<std::ptrdiff_t>(from + plane_size));
            } else if (!figure_plane(*v.path, ORDINARY.width, ORDINARY.height,
                                     std::span<const uint8_t>(*v.atlas).subspan(static_cast<size_t>(v.palette) * 0x200, 0x200),
                                     v.clear, plane, error)) {
                std::fprintf(stderr, "[art] %s %s: %s\n", art.key.c_str(), v.path->filename().string().c_str(),
                             error.c_str());
                continue;
            }
            std::copy(plane.begin(), plane.end(), v.atlas->begin() + static_cast<std::ptrdiff_t>(end - plane_size));
            placed = true;
        }
        if (!placed) {
            continue;
        }
        next++;
        content::Edit edit{content::Table::Appearances, art.id, 4,
                           {static_cast<uint8_t>((route & 0xC0) | model)}};
        pack.edits.push_back(std::move(edit));
        std::fprintf(stderr, "[art] %s: figure model %d (ordinary lane)\n", art.key.c_str(), model);
    }
    if (front.empty() || next == FIRST_NEW_MODEL) {
        return;
    }
    const uint32_t front_id = write_asset(rom, cursor, lz_literal(front));
    const uint32_t back_id = write_asset(rom, cursor, lz_literal(back));
    if (front_id == 0 || back_id == 0) {
        std::fprintf(stderr, "[art] the ROM's free end is full; new figures are off\n");
        return;
    }
    {
        std::lock_guard lock(g_redirect_mutex);
        g_redirect[ORDINARY.front] = front_id;
        g_redirect[ORDINARY.back] = back_id;
    }
    std::fprintf(stderr, "[art] the ordinary figure atlases moved to assets 0x%08X and 0x%08X\n", front_id, back_id);
}

// --- Battle sprite ---------------------------------------------------------------
//
// A class's 4 battle handles are u16 at 0x24 + (class * 4 + body) * 2 in the
// class animation resource (asset 0x00315736, raw); a handle's low 12 bits
// minus 1 index the descriptor table (asset 0x003B6CD0, raw, 208 u32 asset
// ids; func_801AD1B0 sizes and loads it through func_8009DAF4/DBB8). A
// descriptor (raw) is a list of asset ids whose member 3 is its palette
// resource (LZ): banks of 256 RGBA5551 colours, 0x200 bytes each. The
// Centurion's two descriptors (64, 65) each own their palette resource. A new
// class's battle folder holds one 16x16 swatch per bank,
// descriptor<d>-bank<b>.png (pixel i = colour i; d counts the template's
// distinct descriptors in handle order). Each descriptor with a swatch gets a
// copy naming a recolored palette resource, a new entry in a copy of the
// descriptor table, and the class's handles name the new entry
// (class_animation::set_handle_override).
constexpr uint32_t CLASS_ANIMATION = 0x00315736u;
constexpr uint32_t DESCRIPTORS = 0x003B6CD0u;

std::vector<uint8_t> raw_asset(std::span<const uint8_t> rom, uint32_t asset) {
    const size_t at = (asset & 0x0FFFFFFFu) + ASSET_BASE;
    if (at + 4 > rom.size()) {
        return {};
    }
    const size_t size = static_cast<size_t>(rom[at]) << 24 | rom[at + 1] << 16 | rom[at + 2] << 8 | rom[at + 3];
    if (at + 4 + size > rom.size()) {
        return {};
    }
    return {rom.begin() + static_cast<std::ptrdiff_t>(at + 4), rom.begin() + static_cast<std::ptrdiff_t>(at + 4 + size)};
}

uint32_t word_at(std::span<const uint8_t> d, size_t at) {
    return static_cast<uint32_t>(d[at]) << 24 | d[at + 1] << 16 | d[at + 2] << 8 | d[at + 3];
}

// A 16x16 swatch into a bank: colour i from pixel i, keeping each entry's
// alpha bit (which index draws clear is the game's).
bool read_swatch(const std::filesystem::path& png, std::span<uint8_t> bank, std::string& error) {
    int w = 0;
    int h = 0;
    int comp = 0;
    uint8_t* data = stbi_load(png.string().c_str(), &w, &h, &comp, 4);
    if (data == nullptr) {
        error = std::string("cannot read the PNG: ") + stbi_failure_reason();
        return false;
    }
    if (w != 16 || h != 16) {
        stbi_image_free(data);
        error = "a battle swatch is 16x16";
        return false;
    }
    for (size_t i = 0; i < 256; i++) {
        const uint16_t alpha = bank[i * 2 + 1] & 1u;
        const uint16_t c = static_cast<uint16_t>((to5551(data + i * 4) & 0xFFFEu) | alpha);
        bank[i * 2] = static_cast<uint8_t>(c >> 8);
        bank[i * 2 + 1] = static_cast<uint8_t>(c);
    }
    stbi_image_free(data);
    return true;
}

void build_battle(std::span<uint8_t> rom, std::span<const uint8_t> view, size_t& cursor,
                  const std::vector<content::NewClassArt>& classes) {
    const std::vector<uint8_t> anim = raw_asset(view, CLASS_ANIMATION);
    std::vector<uint8_t> table = raw_asset(view, DESCRIPTORS);
    if (anim.size() < 0x24 || table.empty()) {
        std::fprintf(stderr, "[art] the battle tables do not read; new battle palettes are off\n");
        return;
    }
    const size_t original_entries = table.size() / 4;
    for (const content::NewClassArt& art : classes) {
        if (art.battle.empty()) {
            continue;
        }
        // The template's distinct descriptors, in handle order.
        std::vector<uint16_t> slots;
        for (size_t k = 0; k < 4; k++) {
            const size_t at = 0x24 + (static_cast<size_t>(art.copy_of) * 4 + k) * 2;
            const uint16_t slot1 = static_cast<uint16_t>((anim[at] << 8 | anim[at + 1]) & 0x0FFFu);
            if (slot1 != 0 && slot1 <= original_entries &&
                std::find(slots.begin(), slots.end(), slot1) == slots.end()) {
                slots.push_back(slot1);
            }
        }
        for (size_t d = 0; d < slots.size(); d++) {
            const uint16_t slot1 = slots[d];
            std::vector<uint8_t> descriptor = raw_asset(view, word_at(table, (slot1 - 1u) * 4u));
            if (descriptor.size() < 16) {
                continue;
            }
            std::vector<uint8_t> lookup;
            if (!decode_asset(view, word_at(descriptor, 12), lookup) || lookup.size() % 0x200 != 0) {
                std::fprintf(stderr, "[art] %s: descriptor %u's palettes do not decode\n", art.key.c_str(), slot1 - 1u);
                continue;
            }
            int read = 0;
            for (size_t b = 0; b < lookup.size() / 0x200; b++) {
                const std::filesystem::path png =
                    art.battle / ("descriptor" + std::to_string(d) + "-bank" + std::to_string(b) + ".png");
                std::error_code ec;
                if (!std::filesystem::exists(png, ec)) {
                    continue;
                }
                std::string error;
                if (!read_swatch(png, std::span<uint8_t>(lookup).subspan(b * 0x200, 0x200), error)) {
                    std::fprintf(stderr, "[art] %s %s: %s\n", art.key.c_str(), png.filename().string().c_str(),
                                 error.c_str());
                    continue;
                }
                read++;
            }
            if (read == 0) {
                continue;
            }
            const uint32_t lookup_id = write_asset(rom, cursor, lz_literal(lookup));
            descriptor[12] = static_cast<uint8_t>(lookup_id >> 24);
            descriptor[13] = static_cast<uint8_t>(lookup_id >> 16);
            descriptor[14] = static_cast<uint8_t>(lookup_id >> 8);
            descriptor[15] = static_cast<uint8_t>(lookup_id);
            const uint32_t descriptor_id = lookup_id == 0 ? 0 : write_asset(rom, cursor, descriptor);
            if (descriptor_id == 0) {
                std::fprintf(stderr, "[art] the ROM's free end is full; new battle palettes are off\n");
                return;
            }
            const uint16_t new_slot1 = static_cast<uint16_t>(table.size() / 4 + 1);
            put32(table, descriptor_id);
            class_animation::set_handle_override(art.id, slot1, new_slot1);
            std::fprintf(stderr, "[art] %s: battle descriptor %u (copy of %u, %d palette bank(s))\n", art.key.c_str(),
                         new_slot1 - 1u, slot1 - 1u, read);
        }
    }
    if (table.size() / 4 == original_entries) {
        return;
    }
    const uint32_t table_id = write_asset(rom, cursor, table);
    if (table_id == 0) {
        std::fprintf(stderr, "[art] the ROM's free end is full; new battle palettes are off\n");
        return;
    }
    {
        std::lock_guard lock(g_redirect_mutex);
        g_redirect[DESCRIPTORS] = table_id;
    }
    std::fprintf(stderr, "[art] the battle descriptor table moved to asset 0x%08X (%zu entries)\n", table_id,
                 table.size() / 4);
}

}  // namespace

uint32_t portrait_table() {
    return g_table;
}

void build_once() {
    static bool done = false;
    if (done || !content::loaded()) {
        return;
    }
    done = true;
    const std::vector<content::NewClassArt> classes = content::new_class_art();
    std::fprintf(stderr, "[art] %zu new class(es) name art\n", classes.size());
    if (classes.empty()) {
        return;
    }
    // The runtime owns the image as a mutable vector and hands it out as a
    // const span. This writes only the free end, which no game asset names
    // until the redirects and the runtime pack below point at it.
    const std::span<const uint8_t> view = recomp::get_rom();
    const std::span<uint8_t> rom(const_cast<uint8_t*>(view.data()), view.size());
    if (rom.size() <= FREE_START) {
        std::fprintf(stderr, "[art] the ROM image has no free end; new class art is off\n");
        return;
    }
    size_t cursor = FREE_START;

    // The portrait id table as the ROM has it: a size word, then u32 ids.
    const size_t table_at = (PORTRAIT_TABLE & 0x0FFFFFFFu) + ASSET_BASE;
    const uint32_t table_size = static_cast<uint32_t>(view[table_at]) << 24 | view[table_at + 1] << 16 |
                                view[table_at + 2] << 8 | view[table_at + 3];
    std::vector<uint8_t> table(view.begin() + static_cast<std::ptrdiff_t>(table_at + 4),
                               view.begin() + static_cast<std::ptrdiff_t>(table_at + 4 + table_size));

    content::Pack pack;
    pack.id = "class-art";
    pack.source = "new class art";
    for (const content::NewClassArt& art : classes) {
        if (art.portrait.empty()) {
            continue;
        }
        std::vector<uint8_t> decoded;
        std::string error;
        if (!portrait_bytes(art.portrait, decoded, error)) {
            std::fprintf(stderr, "[art] %s portrait: %s\n", art.key.c_str(), error.c_str());
            continue;
        }
        const std::vector<uint8_t> payload = lz_literal(decoded);
        const uint32_t id = write_asset(rom, cursor, payload);
        if (id == 0) {
            std::fprintf(stderr, "[art] %s portrait: the ROM's free end is full\n", art.key.c_str());
            continue;
        }
        const uint32_t index = static_cast<uint32_t>(table.size() / 4);
        if (index >= 0x2000u) {
            continue;
        }
        put32(table, id);
        // Appearance +0 and +2: the portrait index << 3 | palette variant 0.
        const uint16_t v = static_cast<uint16_t>(index << 3);
        content::Edit edit{content::Table::Appearances, art.id, 0,
                           {static_cast<uint8_t>(v >> 8), static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8),
                            static_cast<uint8_t>(v)}};
        pack.edits.push_back(std::move(edit));
        std::fprintf(stderr, "[art] %s: portrait %u from %s (asset 0x%08X)\n", art.key.c_str(), index,
                     art.portrait.filename().string().c_str(), id);
    }
    if (!pack.edits.empty()) {
        const uint32_t table_id = write_asset(rom, cursor, table);
        if (table_id == 0) {
            std::fprintf(stderr, "[art] the ROM's free end is full; new portraits are off\n");
            pack.edits.clear();
        } else {
            g_table = table_id;
            ogre_portrait_table_hi = table_id >> 16;
            ogre_portrait_table_lo = table_id & 0xFFFFu;
            std::fprintf(stderr, "[art] the portrait id table moved to asset 0x%08X (%zu entries)\n", table_id,
                         table.size() / 4);
        }
    }

    build_figures(rom, view, cursor, classes, pack);
    build_battle(rom, view, cursor, classes);
    std::fprintf(stderr, "[art] %zu bytes of art in the ROM's free end\n", cursor - FREE_START);
    if (!pack.edits.empty()) {
        content::set_runtime_pack(std::move(pack));
    }
}

uint32_t redirect(uint32_t asset) {
    std::lock_guard lock(g_redirect_mutex);
    const auto found = g_redirect.find(asset);
    return found == g_redirect.end() ? asset : found->second;
}

}  // namespace ogre::class_art

// config/hooks.toml: the entries of func_8009DAF4, func_8009DBB8 and
// func_8009DD38. An asset a pack's art replaces loads from its copy.
extern "C" uint32_t ogre_asset_redirect(uint32_t asset) {
    return ogre::class_art::redirect(asset);
}
