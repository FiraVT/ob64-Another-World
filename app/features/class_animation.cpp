// The class animation resource for new classes. See class_animation.hpp.

#include "class_animation.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <span>
#include <tuple>

#include <librecomp/game.hpp>

namespace ogre::class_animation {

namespace {

constexpr uint32_t ASSET = 0x00315736u;
// func_8009DAF4 / func_8009DBB8: an asset's size word is at ROM
// (id & 0x0FFFFFFF) + 0x594250, its bytes follow it.
constexpr uint32_t ASSET_BASE = 0x594250u;
constexpr int SLOTS = 18;      // header: 18 u16 section offsets (0x24 bytes)
constexpr int IDS = 256;       // every class id

// The class-indexed sections (LordlyCaliber animation_routes.cpp): header
// slot and bytes per class. Slot 7 is a table of u16 pointers, relative to the
// section's start, to selector records that follow the table.
struct Section {
    int slot;
    int stride;
};
constexpr Section SECTIONS[] = {{0, 8}, {1, 2}, {3, 1}, {7, 2}, {8, 2}};
constexpr int GROUPS_SLOT = 7;
constexpr uint16_t NO_GROUP = 0xFFFFu;

uint16_t get16(const std::vector<uint8_t>& d, size_t at) {
    return static_cast<uint16_t>(d[at] << 8 | d[at + 1]);
}

void put16(std::vector<uint8_t>& d, size_t at, uint16_t v) {
    d[at] = static_cast<uint8_t>(v >> 8);
    d[at + 1] = static_cast<uint8_t>(v);
}

std::mutex g_mutex;
std::vector<std::pair<uint16_t, uint16_t>> g_classes;
std::vector<std::tuple<uint16_t, uint16_t, uint16_t>> g_overrides;  // class, from, to
std::vector<uint8_t> g_built;  // empty: no new classes, the game reads the ROM
bool g_dirty = false;
bool g_logged = false;
// The size the last size call gave the game, which it allocated. A load writes
// only a resource of that size: a pack reload between the two calls changes
// the resource, and the larger copy would run past the game's buffer.
size_t g_reported = 0;

std::vector<uint8_t> original_bytes() {
    const std::span<const uint8_t> rom = recomp::get_rom();
    const size_t at = (ASSET & 0x0FFFFFFFu) + ASSET_BASE;
    if (at + 4 > rom.size()) {
        return {};
    }
    const size_t size = static_cast<size_t>(rom[at]) << 24 | rom[at + 1] << 16 | rom[at + 2] << 8 | rom[at + 3];
    if (at + 4 + size > rom.size()) {
        return {};
    }
    return {rom.begin() + static_cast<std::ptrdiff_t>(at + 4), rom.begin() + static_cast<std::ptrdiff_t>(at + 4 + size)};
}

// The resource to hand the game, rebuilt when the classes changed. Empty when
// there is nothing to extend. Called with g_mutex held.
const std::vector<uint8_t>& current() {
    if (g_dirty) {
        g_dirty = false;
        // OGRE_CLASS_ANIMATION=0 hands the game the ROM's resource even with
        // new classes, to tell a fault in the extension from one elsewhere.
        const char* knob = std::getenv("OGRE_CLASS_ANIMATION");
        const bool off = knob != nullptr && std::strcmp(knob, "0") == 0;
        if (off && !g_classes.empty()) {
            std::fprintf(stderr, "[content] OGRE_CLASS_ANIMATION=0: new classes keep the game's rows\n");
        }
        g_built = g_classes.empty() || off ? std::vector<uint8_t>{} : build(original_bytes(), g_classes);
        // Handle overrides (set_handle_override): slot 0 holds 4 u16 handles
        // per class.
        if (!g_built.empty()) {
            const size_t handles = get16(g_built, 0);
            for (const auto& [cls, from, to] : g_overrides) {
                for (size_t k = 0; k < 4; k++) {
                    const size_t at = handles + (static_cast<size_t>(cls) * 4 + k) * 2;
                    if (at + 2 <= g_built.size() && (get16(g_built, at) & 0x0FFFu) == from) {
                        put16(g_built, at, static_cast<uint16_t>((get16(g_built, at) & 0xF000u) | to));
                    }
                }
            }
        }
        if (!g_classes.empty() && !off && g_built.empty()) {
            std::fprintf(stderr, "[content] the class animation resource has an unexpected layout; new classes "
                                 "keep the game's rows\n");
        }
    }
    return g_built;
}

}  // namespace

std::vector<uint8_t> build(const std::vector<uint8_t>& original,
                           const std::vector<std::pair<uint16_t, uint16_t>>& classes) {
    std::vector<uint8_t> d = original;
    if (d.size() < SLOTS * 2 || get16(d, 0) != SLOTS * 2) {
        return {};
    }
    const int count = (get16(d, 2) - get16(d, 0)) / 8;  // ids the ROM has rows for (172)
    if (count < 165 || count >= IDS) {
        return {};
    }
    const int grow = IDS - count;
    // Grow each class-indexed section at its end, highest first, so a lower
    // section's offsets stay valid; every header offset at or past the
    // insertion point moves with it.
    std::vector<Section> order(std::begin(SECTIONS), std::end(SECTIONS));
    std::sort(order.begin(), order.end(),
              [&](const Section& a, const Section& b) { return get16(d, a.slot * 2) > get16(d, b.slot * 2); });
    for (const Section& s : order) {
        const size_t start = get16(d, s.slot * 2);
        const size_t at = start + static_cast<size_t>(count) * s.stride;
        const size_t n = static_cast<size_t>(grow) * s.stride;
        if (at > d.size()) {
            return {};
        }
        d.insert(d.begin() + static_cast<std::ptrdiff_t>(at), n, uint8_t{0});
        for (int slot = 0; slot < SLOTS; slot++) {
            const uint16_t v = get16(d, slot * 2);
            if (v >= at && slot != s.slot) {
                put16(d, slot * 2, static_cast<uint16_t>(v + n));
            }
        }
        if (s.slot == GROUPS_SLOT) {
            // The selector records followed the table, which grew by n.
            for (int id = 0; id < count; id++) {
                const uint16_t p = get16(d, start + id * 2);
                if (p != NO_GROUP) {
                    put16(d, start + id * 2, static_cast<uint16_t>(p + n));
                }
            }
        }
    }
    if (d.size() > 0xFFFF) {
        return {};
    }
    // Ids past the ROM's start as class 0 (no class); a new class takes its
    // template's entries in every section.
    const auto copy_entries = [&](int to, int from) {
        for (const Section& s : SECTIONS) {
            const size_t start = get16(d, s.slot * 2);
            std::copy_n(d.begin() + static_cast<std::ptrdiff_t>(start + from * s.stride), s.stride,
                        d.begin() + static_cast<std::ptrdiff_t>(start + to * s.stride));
        }
    };
    for (int id = count; id < IDS; id++) {
        copy_entries(id, 0);
    }
    for (const auto& [id, from] : classes) {
        if (id >= count && id < IDS && from < count) {
            copy_entries(id, from);
        }
    }
    return d;
}

void set_handle_override(uint16_t cls, uint16_t from, uint16_t to) {
    std::lock_guard lock(g_mutex);
    g_overrides.emplace_back(cls, from, to);
    g_dirty = true;
}

void set_new_classes(std::vector<std::pair<uint16_t, uint16_t>> classes) {
    std::lock_guard lock(g_mutex);
    if (classes != g_classes) {
        g_classes = std::move(classes);
        g_dirty = true;
    }
}

namespace {

// OGRE_ASSET_LOG=1: one line per call of the asset size and load functions,
// with the asset's ROM offset and size word. An id whose offset is past the
// ROM, or whose size is implausible, came from a table read out of range.
bool asset_log() {
    static const bool on = [] {
        const char* v = std::getenv("OGRE_ASSET_LOG");
        return v != nullptr && std::strcmp(v, "0") != 0;
    }();
    return on;
}

void log_asset(const char* what, uint32_t asset, uint32_t dst) {
    const std::span<const uint8_t> rom = recomp::get_rom();
    const size_t at = (asset & 0x0FFFFFFFu) + ASSET_BASE;
    if (at + 4 > rom.size()) {
        std::fprintf(stderr, "[asset] %s id=0x%08X dst=0x%08X rom=0x%zX PAST THE ROM\n", what, asset, dst, at);
        return;
    }
    const uint32_t size = static_cast<uint32_t>(rom[at]) << 24 | rom[at + 1] << 16 | rom[at + 2] << 8 | rom[at + 3];
    std::fprintf(stderr, "[asset] %s id=0x%08X dst=0x%08X rom=0x%zX size=0x%X%s\n", what, asset, dst, at, size,
                 at + 4 + size > rom.size() || size > 0x400000u ? " IMPLAUSIBLE" : "");
}

}  // namespace

}  // namespace ogre::class_animation

// config/hooks.toml: the start of func_8009DAF4 (an asset's size). Returns the
// extended size for the class animation resource, or 0 to let the game run.
extern "C" uint32_t ogre_class_animation_size(uint32_t asset) {
    using namespace ogre::class_animation;
    if (asset_log()) {
        log_asset("size", asset, 0);
    }
    if (asset != ASSET) {
        return 0;
    }
    std::lock_guard lock(g_mutex);
    g_reported = current().size();
    return static_cast<uint32_t>(g_reported);
}

// config/hooks.toml: the start of func_8009DBB8 (load an asset into `dst`).
// Copies the extended resource into guest memory and returns 1, or returns 0
// to let the game load it (no new classes, or no destination: the game then
// allocates one itself).
extern "C" uint32_t ogre_class_animation_load(uint8_t* rdram, uint32_t dst, uint32_t asset) {
    using namespace ogre::class_animation;
    if (asset_log()) {
        log_asset("load", asset, dst);
    }
    if (asset != ASSET || dst == 0) {
        return 0;
    }
    std::lock_guard lock(g_mutex);
    const std::vector<uint8_t>& bytes = current();
    if (bytes.empty() || bytes.size() != g_reported) {
        if (!bytes.empty()) {
            std::fprintf(stderr, "[content] class animation resource changed between its size and its load; this "
                                 "load keeps the game's rows\n");
        }
        return 0;
    }
    for (size_t i = 0; i < bytes.size(); i++) {
        rdram[((dst + static_cast<uint32_t>(i)) & 0x1FFFFFFFu) ^ 3u] = bytes[i];
    }
    if (!g_logged) {
        g_logged = true;
        std::fprintf(stderr, "[content] class animation resource: %zu bytes with rows for %zu new class(es)\n",
                     bytes.size(), g_classes.size());
    }
    return 1;
}

// config/hooks.toml: the start of func_8007A110 (the LZ decompressor). A
// pointer outside RDRAM makes the decompressor fault; this line names the
// caller first. A Hugo/Dio dialogue crashed here with a character whose class
// a script had changed (docs/anotherworld/design-new-classes.md section 7).
extern "C" void ogre_lz_check(uint32_t dst, uint32_t src, uint32_t ra, uint32_t sp) {
    const auto bad = [](uint32_t p) { return p < 0x80000000u || p >= 0x80800000u; };
    if (bad(dst) || bad(src)) {
        std::fprintf(stderr, "[lz] func_8007A110 called with dst=0x%08X src=0x%08X from ra=0x%08X sp=0x%08X\n", dst,
                     src, ra, sp);
        std::fflush(stderr);
    }
}
