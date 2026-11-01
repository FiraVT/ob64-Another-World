#pragma once

// Game events: the app's hook points in generated code (config/hooks.toml) call
// ogre_event(), and every subscriber of that event runs, in the order it
// subscribed. Port features subscribe here instead of owning a hook each; mods
// will subscribe through the same table (docs/anotherworld/design-modding-platform.md 4.2).
//
// The event numbers are written into config/hooks.toml, so they never change.

#include <cstdint>
#include <functional>

#include "recomp.h"

namespace ogre::events {

enum class Event : uint32_t {
    // Bank R's mission loader, after enemydat.bin is open and before any unit is
    // built (0x802164E0). a0 = template buffer, a1 = spawn list.
    MissionLoad = 1,
    // The mission field update's exit (bank N, 0x801B258C), once per field update.
    FieldUpdate = 2,
    // The item grant routine's entry (bank N func_ovlN_801DC740). a0 = item word
    // (bit 15: equipment list, else consumables), a1 = the routine's $a1. A
    // subscriber may change the item by writing ctx->r4.
    ItemGranted = 3,
    // The mission field's camera pan's exit (bank N func_ovlN_801BECF0,
    // 0x801BEF60), once per frame after the cursor mover and the camera have
    // moved, also while the field's updates are held. The field overlay
    // projects its markers here, so they follow the camera (2026-10-30: they
    // were projected in the field update, before the pan, and froze while the
    // field was held). The routine's locals at sp+0x10..0x27 are dead here.
    FieldFrame = 4,
};

struct Args {
    uint8_t* rdram;
    recomp_context* ctx;
    uint32_t a0;
    uint32_t a1;
};

using Handler = std::function<void(const Args&)>;

// Adds `handler` for `event`. `owner` names the subscriber in logs.
void on(Event event, const char* owner, Handler handler);

// Subscribes at static initialisation: `static ogre::events::Subscribe s{...};`.
struct Subscribe {
    Subscribe(Event event, const char* owner, Handler handler) { on(event, owner, std::move(handler)); }
};

}  // namespace ogre::events

extern "C" void ogre_event(uint8_t* rdram, recomp_context* ctx, uint32_t event, uint32_t a0, uint32_t a1);

