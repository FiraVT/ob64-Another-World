// Battle figures on demand (docs/anotherworld/design-battle-transform.md).
//
// The battle keeps each class's figure (sprite sheets and animation data) in a
// cache of 20 entries of 0xB0 bytes at *(0x801D0748); entry +0x00 is the
// figure's handle (0 = free) and +0x08 the loaded figure. The battle setup
// func_ovlC_801CA6A0 loads the figures of the classes present once, through
// func_ovlC_801C41E8 (0x801CBAD8), and nothing loads one later. The draw
// func_ovlC_801B0790 finds a participant's figure with func_ovlC_801C2ED0,
// which reads the participant's current class; when no entry matches it
// returns 0 and the draw spins at 0x801B12C0 (`j 801b12c0`).
//
// So a participant whose class changes during the fight (a mod, a Lua script)
// hangs the game unless its new figure happened to be loaded. config/hooks.toml
// calls ogre_battle_figure_lookup in func_ovlC_801C2ED0 after it computed the
// handle (0x801C3000, handle at sp+0x34) and before it scans the cache. When no
// entry holds the handle, this loads the figure with the game's own loader,
// func_ovlC_801C41E8, given one-element arrays in the same order the setup
// passes them, and the game's scan then finds it.
//
// func_ovlC_801C2ED0's frame (0x68 bytes): class in $fp, key at sp+0x1C, side
// at sp+0x24, the alternate-figure flag at sp+0x78 and the equipment variant at
// sp+0x7C (the caller's 5th and 6th arguments). func_ovlC_801BE158(class, key,
// alt, side) gave the handle.
//
// Two of the game's own loops spin when they cannot go on, and this file
// avoids both: func_ovlC_801C41E8 spins at 0x801C4544 when no entry is free, so
// nothing is loaded into a full cache; func_ovlC_801C2818 writes the handle
// before it allocates (0x801C28E0) and leaves +0x08 zero when the allocation
// fails (0x801C2934), so such an entry is cleared again.
//
// The scan also matches the palette or variant of an entry with the right
// handle; a figure loaded for the same handle with another variant still
// misses there. That case is not handled yet.
//
// OGRE_BATTLE_FIGURE_LOAD=0 turns the loading off.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <tuple>

#include "battle_figures.hpp"
#include "recomp.h"

namespace {

constexpr uint32_t CACHE = 0x801D0748u;
constexpr uint32_t ENTRY_SIZE = 0xB0u;
constexpr int ENTRIES = 20;
constexpr uint32_t E_HANDLE = 0x00u;
constexpr uint32_t E_FIGURE = 0x08u;
constexpr uint32_t LOAD_FIGURES = 0x801C41E8u;
constexpr uint32_t FIGURE_HANDLE = 0x801BE158u;

uint32_t rd32(uint8_t* rdram, uint32_t address) {
    return static_cast<uint32_t>(MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

void wr32(uint8_t* rdram, uint32_t address, uint32_t value) {
    MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address))) = static_cast<int32_t>(value);
}

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

bool enabled() {
    static const bool on = [] {
        const char* value = std::getenv("OGRE_BATTLE_FIGURE_LOAD");
        return value == nullptr || value[0] != '0';
    }();
    return on;
}

using Request = std::tuple<uint32_t, uint32_t, uint32_t, uint32_t, uint32_t>;

// Requests already tried in the current fight, so a figure that cannot load is
// tried and logged once, not every frame. A new battle allocates a new cache,
// but it can land at the last one's address, so new_fight also clears the
// list: with only the address test, a fight whose cache took the last fight's
// address skipped every figure that fight had loaded (played, 2026-11-02:
// classes 63, 64, 65 "not loaded" in the third fight, 67 loaded).
uint32_t g_cache = 0;
std::set<Request> g_tried;

// func_ovlC_801BE158(class, key, alt, side): the figure's handle.
uint32_t figure_handle(uint8_t* rdram, recomp_context* ctx, uint32_t cls, uint32_t key, uint32_t alt,
                       uint32_t side) {
    const recomp_context saved = *ctx;
    set_arg(ctx->r4, cls);
    set_arg(ctx->r5, key);
    set_arg(ctx->r6, alt);
    set_arg(ctx->r7, side);
    LOOKUP_FUNC(FIGURE_HANDLE)(rdram, ctx);
    const uint32_t handle = static_cast<uint32_t>(ctx->r2);
    *ctx = saved;
    return handle;
}

}  // namespace

namespace ogre::battle_figures {

void new_fight() {
    g_tried.clear();
}

int ensure(uint8_t* rdram, recomp_context* ctx, uint32_t cls, uint32_t key, uint32_t side, uint32_t alt,
           uint32_t variant, uint32_t handle) {
    if (!enabled()) {
        return -1;
    }
    const uint32_t cache = rd32(rdram, CACHE);
    if (cache == 0) {
        return -1;
    }
    if (cache != g_cache) {
        g_cache = cache;
        g_tried.clear();
    }
    if (handle == 0) {
        handle = figure_handle(rdram, ctx, cls, key, alt, side);
        if (handle == 0) {
            return -1;
        }
    }

    int free_entries = 0;
    for (int i = 0; i < ENTRIES; i++) {
        const uint32_t entry = cache + static_cast<uint32_t>(i) * ENTRY_SIZE;
        const uint32_t h = rd32(rdram, entry + E_HANDLE);
        if (h == handle && rd32(rdram, entry + E_FIGURE) != 0) {
            return 1;
        }
        free_entries += h == 0 ? 1 : 0;
    }

    if (!g_tried.insert({cls, key, side, alt, variant}).second) {
        return -1;
    }
    if (free_entries == 0) {
        std::fprintf(stderr, "[battle-figure] class %u (key %u): the figure cache is full\n", cls, key);
        return -1;
    }
    // Five one-word arrays and the two stack arguments, below the caller's
    // frame; the loader's own frame goes below them.
    const recomp_context saved = *ctx;
    const uint32_t sp = static_cast<uint32_t>(ctx->r29);
    const uint32_t frame = sp - 0x40u;
    wr32(rdram, frame + 0x20u, cls);
    wr32(rdram, frame + 0x24u, key);
    wr32(rdram, frame + 0x28u, side);
    wr32(rdram, frame + 0x2Cu, alt);
    wr32(rdram, frame + 0x30u, variant);
    wr32(rdram, frame + 0x10u, frame + 0x30u);
    wr32(rdram, frame + 0x14u, 1u);
    set_arg(ctx->r29, frame);
    set_arg(ctx->r4, frame + 0x20u);
    set_arg(ctx->r5, frame + 0x24u);
    set_arg(ctx->r6, frame + 0x28u);
    set_arg(ctx->r7, frame + 0x2Cu);
    LOOKUP_FUNC(LOAD_FIGURES)(rdram, ctx);
    *ctx = saved;

    // A failed allocation leaves the handle with no figure; clear it.
    bool loaded = false;
    bool failed = false;
    for (int i = 0; i < ENTRIES; i++) {
        const uint32_t entry = cache + static_cast<uint32_t>(i) * ENTRY_SIZE;
        const uint32_t h = rd32(rdram, entry + E_HANDLE);
        if (h == 0) {
            continue;
        }
        if (rd32(rdram, entry + E_FIGURE) == 0) {
            wr32(rdram, entry + E_HANDLE, 0);
            failed = true;
        } else if (h == handle) {
            loaded = true;
        }
    }
    const bool ok = loaded && !failed;
    std::fprintf(stderr, "[battle-figure] class %u (key %u, side %u, alt %u, variant %u): %s\n", cls, key, side,
                 alt, variant, ok ? "loaded during the fight" : "did not load (out of memory?)");
    return ok ? 1 : -1;
}

}  // namespace ogre::battle_figures

// config/hooks.toml, 0x801C3000 in func_ovlC_801C2ED0: load the figure the
// scan below is about to look for when no entry holds its handle.
extern "C" void ogre_battle_figure_lookup(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t sp = static_cast<uint32_t>(ctx->r29);
    const uint32_t handle = rd32(rdram, sp + 0x34u);
    if (handle == 0) {
        return;
    }
    ogre::battle_figures::ensure(rdram, ctx, static_cast<uint32_t>(ctx->r30), rd32(rdram, sp + 0x1Cu),
                                 rd32(rdram, sp + 0x24u), rd32(rdram, sp + 0x78u), rd32(rdram, sp + 0x7Cu), handle);
}
