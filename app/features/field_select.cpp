#include "game_addresses.hpp"
#include "field_select.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>

#include "alerts.hpp"
#include "auto_commander.hpp"
#include "field_view.hpp"
#include "game_events.hpp"
#include "organize_view.hpp"
#include "game_host.hpp"
#include "input_map.hpp"
#include "recomp.h"
#include "settings.hpp"
#include "ultramodern/ultramodern.hpp"

namespace ogre::field_select {

namespace {

// Revision 1 addresses (docs/anotherworld/design-field-controls.md section 4).
constexpr uint32_t SCENE = ::ogre::addr::kActiveScene;
constexpr int SCENE_FIELD = 0x03;
constexpr uint32_t OBJECTS = ::ogre::addr::kMapObjects;
constexpr uint32_t CAMERA = 0x8018F5ACu;
constexpr uint32_t PROJECT = 0x801A9DD0u;  // func_801A9DD0(camera, &xyz, &sx, &sy)
constexpr uint32_t MOVE_TO = 0x801CC5D0u;  // func_ovlN_801CC5D0(object, x, y, z)
constexpr uint32_t HOLD = 0x801D4630u;     // func_ovlN_801D4630(object): destination = position
constexpr uint32_t END_PURSUIT = 0x801D45F8u;  // func_ovlN_801D45F8(object): clears flag 0x2000
// Dispatch, as the game's own confirm does it (func_ovlN_801B9694 at
// 0x801BA018..0x801BA2E0, found with an OGRE_COVER census around one Dispatch,
// 2026-10-30): the headquarters' position, the placement the opening units get
// at mission start, and the location update.
constexpr uint32_t HQ_POSITION = 0x801DA360u;  // (-1, &xyz): the location with state bit 0x10
constexpr uint32_t PLACE = 0x801CC4A0u;        // (object, x, y bits, z; sp+0x10 inside): flags |1, record +0x01 |4
constexpr uint32_t LOCATE = 0x801B551C;        // (object): the location the unit stands in, object +0x74
// Units out at once. The game allows 10 (the constant at 0x801B8B60 in
// func_ovlN_801B8AC8, 0x801B8C38 in func_ovlN_801B8B90's Dispatch menu and
// 0x801CBA88 in the legion split); config/hooks.toml replaces all three with
// ogre_field_limit (developer, 2026-10-30: dispatch 20).
constexpr int FIELD_LIMIT = 20;
// The R menu's Organize entry (the menu's handler table, second entry; found
// with an OGRE_COVER census around opening Organize, 2026-10-30): it loads the
// transition module (ROM 0x1A9260 -> 0x80214FA0), sets field mode 0x15 and the
// hold bit 0x01 of 0x801F367D, and the field then enters the game's Organize
// (scene 0x06), which the port's Organize view opens over.
constexpr uint32_t OPEN_ORGANIZE = 0x801B7B2Cu;
// The R menu's item entry, the handler before Organize: the same module load,
// then field mode 0x16, the game's item screen (the unit's carried items and
// a one-unit view; captures 1-15 of 2026-10-30 09:40: a Power Fruit and a Quit
// Gate used on Dio's unit, each taken from its record +0x0D.. and the stock).
constexpr uint32_t OPEN_ITEMS = 0x801B7A88u;
// The camera's fly-to (found with an OGRE_COVER census around the R menu's
// Mission Objective, 2026-10-30; called from over ten sites, e.g. 0x801B6CE0):
// $f12 x, $f14 y, $a2 z, $a3 the camera (*(0x8018F5AC)), and at sp+0x10 the
// field mode to return to. It sets 0x801F3630 = 1, which holds the field while
// the camera flies, and keeps the return mode at 0x801F0CC4.
constexpr uint32_t CAMERA_TO = 0x801B8D64u;
constexpr uint32_t CAMERA_MOVING = 0x801F3630u;
// Modern mode pans the field when the pointer is this close to the window's
// edge (window points); see pan_camera.
constexpr int EDGE_POINTS = 6;
constexpr int PLAYER_UNITS = 30;
constexpr int SLOTS = 60;
constexpr float HIT_RADIUS = 12.0f;  // framebuffer pixels
constexpr float DRAG_START = 4.0f;
// A synthetic button event this module re-sends to the game; it is let through.
constexpr Uint32 REPLAY_ID = 0x0F1E1D00u;

struct Order {
    bool hold = false;         // stop where they stand
    int strategy = -1;         // set Battle Strategy (unit record +0x17), 0..3
    bool dispatch = false;     // send the unit out of the headquarters
    bool organize = false;     // open the game's Organize screen
    bool items = false;        // open the game's item screen
    bool jump = false;         // fly the camera to the first slot's unit
    bool pursue = false;
    int target = -1;           // pursue: the enemy slot
    float fx = 0.0f, fy = 0.0f;  // move: the framebuffer pixel clicked
    bool at_location = false;    // move: straight to a location's own point
    float wx = 0.0f, wz = 0.0f;
    std::vector<int> slots;
};

std::mutex g_mutex;
std::vector<int> g_selected;
std::deque<Order> g_orders;
float g_pointer_x = -100.0f, g_pointer_y = -100.0f;
// The pan the pointer asks for: -1, 0 or 1 per axis (x right, y down).
std::atomic<int> g_pan_x{0};
std::atomic<int> g_pan_y{0};
bool g_strip_press = false;  // a left press on a strip tile; its release is ours too
int g_cycle = -1;            // the last unit idle cycling picked
int g_last_tile = -1;        // the strip tile clicked last, for the double click
Uint64 g_last_tile_at = 0;
bool g_items_open = false;  // the Group Items panel
std::atomic<int> g_strip_scroll{0};  // the army strip's scroll offset, pixels
bool g_dispatch_open = false;  // the Dispatch list
bool g_pressed = false;
bool g_dragging = false;
// The pad on the MODERN open field (see handle_pad). The menu is the overlay's
// own buttons walked with the D-pad; g_menu_id is the one in focus.
bool g_menu_open = false;
std::string g_menu_id;
bool g_circle = false;         // X held: the selection circle grows
Uint64 g_circle_at = 0;
Uint64 g_y_at = 0;             // Y went down at this tick, 0 while up
std::atomic<bool> g_y_game{false};  // Y held long: the game's R (its hold menu)
std::atomic<uint32_t> g_held_claims{0};  // pad buttons taken at their press, until release
std::atomic<int> g_inject_a{0};  // polls of the game's A still to press
std::atomic<int> g_stick_x{0}, g_stick_y{0};  // the left stick, for PAUSE ON SELECT
std::atomic<uint32_t> g_dpad_held{0};
float g_press_x = 0.0f, g_press_y = 0.0f, g_now_x = 0.0f, g_now_y = 0.0f;

uint8_t rd8(const uint8_t* rdram, uint32_t a) {
    return rdram[(a - 0x80000000u) ^ 3u];
}

uint32_t rd32(const uint8_t* rdram, uint32_t a) {
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) {
        v = (v << 8) | rd8(rdram, a + i);
    }
    return v;
}

void wr32(uint8_t* rdram, uint32_t a, uint32_t v) {
    for (uint32_t i = 0; i < 4; i++) {
        rdram[(a + i - 0x80000000u) ^ 3u] = static_cast<uint8_t>(v >> (24 - 8 * i));
    }
}

bool in_ram(uint32_t a) {
    return a >= 0x80000000u && a < 0x80800000u;
}

uint32_t bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

float as_float(uint32_t b) {
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

bool on_field() {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    return rdram != nullptr && ((rd8(rdram, SCENE) << 8) | rd8(rdram, SCENE + 1)) == SCENE_FIELD;
}

// The window point as a pixel of the 320x240 picture, aspect-fit and centered
// in the drawable (the mapping app/src/mission_cursor.cpp uses for the cursor).
bool to_framebuffer(SDL_Window* window, int wx, int wy, float& fx, float& fy) {
    int win_w = 0, win_h = 0, draw_w = 0, draw_h = 0;
    SDL_GetWindowSize(window, &win_w, &win_h);
    SDL_GL_GetDrawableSize(window, &draw_w, &draw_h);
    if (win_w <= 0 || win_h <= 0 || draw_w <= 0 || draw_h <= 0) {
        return false;
    }
    const float px = wx * static_cast<float>(draw_w) / win_w;
    const float py = wy * static_cast<float>(draw_h) / win_h;
    const float scale = std::min(draw_w / 320.0f, draw_h / 240.0f);
    fx = (px - (draw_w - 320.0f * scale) * 0.5f) / scale;
    fy = (py - (draw_h - 240.0f * scale) * 0.5f) / scale;
    return fx >= 0.0f && fy >= 0.0f && fx < 320.0f && fy < 240.0f;
}

// The unit marker nearest (fx, fy) within the hit radius, own units or enemies.
int unit_at(float fx, float fy, bool own) {
    int best = -1;
    float best_d = HIT_RADIUS * HIT_RADIUS;
    const int first = own ? 0 : PLAYER_UNITS;
    const int last = own ? PLAYER_UNITS : SLOTS;
    for (int slot = first; slot < last; slot++) {
        float x = 0.0f, y = 0.0f;
        if (!field_view::screen_of(slot, x, y)) {
            continue;
        }
        const float d = (x - fx) * (x - fx) + (y - fy) * (y - fy);
        if (d <= best_d) {
            best = slot;
            best_d = d;
        }
    }
    return best;
}

// A unit the Dispatch list offers: a player slot with members whose map object
// is not out (the list func_ovlN_801B87A0 builds takes flags & 0x19 == 0x18);
// legions (0x40) are left to the game's own menu for now.
bool dispatchable(const uint8_t* rdram, int slot) {
    if (rdram == nullptr || slot < 0 || slot >= PLAYER_UNITS) {
        return false;
    }
    const uint32_t object = rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
    if (!in_ram(object)) {
        return false;
    }
    const uint32_t flags = rd32(rdram, object);
    if ((flags & 0x19u) != 0x18u || (flags & 0x40u) != 0) {
        return false;
    }
    const uint32_t unit = 0x80197210u + static_cast<uint32_t>(slot) * 0x19u;
    for (uint32_t i = 0; i < 5; i++) {
        const int ref = rd8(rdram, unit + 0x02 + i);
        if (ref >= 1 && ref < 100) {
            return true;
        }
    }
    return false;
}

// Units out on the field, counted as func_ovlN_801B8B90 counts them.
int on_field_count(const uint8_t* rdram) {
    int n = 0;
    for (int slot = 0; slot < PLAYER_UNITS; slot++) {
        const uint32_t object = rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
        if (in_ram(object) && (rd32(rdram, object) & 0x31u) == 0x31u) {
            n++;
        }
    }
    return n;
}

// Selects `slot`, or with Shift adds or removes it. Caller holds g_mutex.
void pick(int slot, bool shift) {
    auto it = std::find(g_selected.begin(), g_selected.end(), slot);
    if (shift) {
        if (it != g_selected.end()) {
            g_selected.erase(it);
        }
        else {
            g_selected.push_back(slot);
        }
    }
    else {
        g_selected = {slot};
    }
}

int next_idle(int from, int step);

// A click on the overlay element `id` (field_view::hit_at), or its key. Caller
// holds g_mutex.
void command(const std::string& id, bool shift) {
    if (id.size() > 1 && id[0] == 't') {
        // Strip tile: select; a second click within 400 ms flies the camera there.
        const int slot = std::atoi(id.c_str() + 1);
        const Uint64 now = SDL_GetTicks64();
        if (slot == g_last_tile && now - g_last_tile_at < 400) {
            Order order;
            order.jump = true;
            order.slots = {slot};
            g_orders.push_back(order);
        }
        g_last_tile = slot;
        g_last_tile_at = now;
        pick(slot, shift);
    }
    else if (id.size() > 1 && id[0] == 's') {
        // Selection card: drop that unit from the selection.
        const int slot = std::atoi(id.c_str() + 1);
        g_selected.erase(std::remove(g_selected.begin(), g_selected.end(), slot), g_selected.end());
    }
    else if (id == "cmd-stop" && !g_selected.empty()) {
        Order order;
        order.hold = true;
        order.slots = g_selected;
        g_orders.push_back(order);
    }
    else if (id == "cmd-hq" && !g_selected.empty()) {
        Order order;
        order.slots = g_selected;
        order.at_location = field_view::hq_point(order.wx, order.wz);
        if (order.at_location) {
            g_orders.push_back(order);
        }
    }
    else if (id == "cmd-clear") {
        g_selected.clear();
    }
    else if (id == "cmd-organize") {
        // The game's Organize screen, so the port's view opens over it with
        // editing (the view alone on the field is read-only).
        Order order;
        order.organize = true;
        g_orders.push_back(order);
    }
    else if (id == "cmd-strategy" && !g_selected.empty()) {
        // The next of the four strategies after the first unit's, for all of them.
        const uint8_t* rdram = ultramodern::get_rdram_base();
        Order order;
        order.slots = g_selected;
        order.strategy = (rd8(rdram, 0x80197210u + static_cast<uint32_t>(g_selected.front()) * 0x19u + 0x17u) + 1) % 4;
        g_orders.push_back(order);
    }
    else if (id == "cmd-dispatch") {
        g_dispatch_open = !g_dispatch_open;
    }
    else if (id.size() > 1 && id[0] == 'd') {
        // A Dispatch list entry: send that unit out.
        Order order;
        order.dispatch = true;
        order.slots = {std::atoi(id.c_str() + 1)};
        g_orders.push_back(order);
        g_dispatch_open = false;
    }
    else if (id == "cmd-useitem") {
        // The game's own item screen, where the item is picked and used.
        Order order;
        order.items = true;
        g_orders.push_back(order);
        g_items_open = false;
    }
    else if (id == "cmd-item") {
        g_items_open = !g_items_open;
    }
    else if (id == "cmd-speed") {
        cycle_game_speed();
    }
    else if (id == "cmd-autocmd") {
        auto_commander::toggle();
    }
    else if (id == "cmd-ai" && !g_selected.empty()) {
        // The card's AI button: hand the selection back to the auto commander,
        // or take it from it.
        auto_commander::toggle_units(g_selected);
    }
    else if (id == "cmd-idle") {
        const int slot = next_idle(g_cycle, 1);
        if (slot >= 0) {
            g_cycle = slot;
            g_selected = {slot};
            Order order;
            order.jump = true;
            order.slots = {slot};
            g_orders.push_back(order);
        }
    }
}

// The next (step 1) or previous (step -1) player unit after `from` that stands
// on the open map: out, not marching, not inside a location.
int next_idle(int from, int step) {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    for (int i = 1; i <= PLAYER_UNITS; i++) {
        const int slot = ((from + step * i) % PLAYER_UNITS + PLAYER_UNITS) % PLAYER_UNITS;
        const uint32_t object = rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
        if (!in_ram(object)) {
            continue;
        }
        const uint32_t flags = rd32(rdram, object);
        if ((flags & 0x11u) == 0x11u && (flags & 0x02u) == 0 && (flags & 0x00800000u) == 0) {
            return slot;
        }
    }
    return -1;
}

void replay_click(const SDL_Event& event) {
    SDL_Event down = event;
    down.type = SDL_MOUSEBUTTONDOWN;
    down.button.state = SDL_PRESSED;
    down.button.which = REPLAY_ID;
    SDL_PushEvent(&down);
}

// --- Game thread -------------------------------------------------------------

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

// The game's projection of (x, y, z), using the field update's dead locals at
// sp+0x10..0x23 as the input vector and outputs.
bool project(uint8_t* rdram, recomp_context* ctx, uint32_t camera, float x, float y, float z, float& sx,
             float& sy) {
    const uint32_t sp = static_cast<uint32_t>(ctx->r29);
    wr32(rdram, sp + 0x18u, bits(x));
    wr32(rdram, sp + 0x1Cu, bits(y));
    wr32(rdram, sp + 0x20u, bits(z));
    set_arg(ctx->r4, camera);
    set_arg(ctx->r5, sp + 0x18u);
    set_arg(ctx->r6, sp + 0x10u);
    set_arg(ctx->r7, sp + 0x14u);
    LOOKUP_FUNC(PROJECT)(rdram, ctx);
    sx = as_float(rd32(rdram, sp + 0x10u));
    sy = as_float(rd32(rdram, sp + 0x14u));
    return std::isfinite(sx) && std::isfinite(sy);
}

// The ground point under a framebuffer pixel: the projection inverted by
// Newton's method on (x, z) at height y, starting from `x, z`.
bool unproject(uint8_t* rdram, recomp_context* ctx, uint32_t camera, float fx, float fy, float y, float& x,
               float& z) {
    for (int step = 0; step < 8; step++) {
        float sx = 0, sy = 0, ax = 0, ay = 0, bx = 0, by = 0;
        const float e = 0.05f;
        if (!project(rdram, ctx, camera, x, y, z, sx, sy) || !project(rdram, ctx, camera, x + e, y, z, ax, ay) ||
            !project(rdram, ctx, camera, x, y, z + e, bx, by)) {
            return false;
        }
        const float j11 = (ax - sx) / e, j21 = (ay - sy) / e, j12 = (bx - sx) / e, j22 = (by - sy) / e;
        const float det = j11 * j22 - j12 * j21;
        if (std::fabs(det) < 1e-6f) {
            return false;
        }
        const float rx = fx - sx, ry = fy - sy;
        x += (j22 * rx - j12 * ry) / det;
        z += (-j21 * rx + j11 * ry) / det;
        if (rx * rx + ry * ry < 0.25f) {
            return true;
        }
    }
    return true;
}

// Chasing an enemy (the overlay's own: the game's pursue, func_ovlN_801D44D4
// with mode 0, records the target but leaves a player unit's destination on
// itself, so the unit stood in place; developer playtest 2026-10-30). A chaser
// is sent a move to its target's current position every CHASE_EVERY field
// updates; the chase ends when the target leaves the field or is not shown.
constexpr int CHASE_EVERY = 24;
std::vector<std::pair<int, int>> g_chases;  // (chaser slot, target slot), under g_mutex
int g_chase_tick = 0;

void move_unit(uint8_t* rdram, recomp_context* ctx, uint32_t object, uint32_t x, uint32_t y, uint32_t z) {
    const recomp_context saved = *ctx;
    set_arg(ctx->r4, object);
    LOOKUP_FUNC(END_PURSUIT)(rdram, ctx);
    *ctx = saved;
    set_arg(ctx->r4, object);
    set_arg(ctx->r5, x);
    set_arg(ctx->r6, y);
    set_arg(ctx->r7, z);
    LOOKUP_FUNC(MOVE_TO)(rdram, ctx);
    *ctx = saved;
}

void run_chases(uint8_t* rdram, recomp_context* ctx) {
    if (++g_chase_tick % CHASE_EVERY != 0) {
        return;
    }
    std::vector<std::pair<int, int>> chases;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        chases = g_chases;
    }
    std::vector<int> ended;
    for (const auto& [slot, target] : chases) {
        const uint32_t object = rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
        const uint32_t prey = rd32(rdram, OBJECTS + static_cast<uint32_t>(target) * 4u);
        const bool chaser_ok = in_ram(object) && (rd32(rdram, object) & 0x11u) == 0x11u;
        const uint32_t prey_flags = in_ram(prey) ? rd32(rdram, prey) : 0;
        // On the map (0x11), shown (0x20), and not beaten (0x80000000).
        const bool prey_ok = (prey_flags & 0x31u) == 0x31u && (prey_flags & 0x80000000u) == 0;
        if (!chaser_ok || !prey_ok) {
            ended.push_back(slot);
            if (chaser_ok) {
                alerts::post(alerts::Kind::Info, "Unit " + std::to_string(slot + 1) + " lost its target");
            }
            continue;
        }
        move_unit(rdram, ctx, object, rd32(rdram, prey + 0x08), rd32(rdram, prey + 0x0C), rd32(rdram, prey + 0x10));
    }
    if (!ended.empty()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_chases.erase(std::remove_if(g_chases.begin(), g_chases.end(),
                                      [&](const std::pair<int, int>& c) {
                                          return std::find(ended.begin(), ended.end(), c.first) != ended.end();
                                      }),
                       g_chases.end());
    }
}

void run_orders(uint8_t* rdram, recomp_context* ctx) {
    run_chases(rdram, ctx);
    std::deque<Order> orders;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        orders.swap(g_orders);
    }
    if (orders.empty()) {
        return;
    }
    const uint32_t camera = rd32(rdram, CAMERA);
    if (!in_ram(camera)) {
        return;
    }
    const recomp_context saved = *ctx;
    const uint32_t sp = static_cast<uint32_t>(ctx->r29);
    for (const Order& order : orders) {
        if (order.jump) {
            // Only on the open field and with no flight under way.
            const int slot = order.slots.empty() ? -1 : order.slots.front();
            const uint32_t object = slot >= 0 ? rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u) : 0;
            if (in_ram(object) && (rd32(rdram, object) & 0x11u) == 0x11u && rd32(rdram, 0x801F0E00u) == 0 &&
                rd32(rdram, CAMERA_MOVING) == 0) {
                ctx->f12.u32l = rd32(rdram, object + 0x08);
                ctx->f14.u32l = rd32(rdram, object + 0x0C);
                set_arg(ctx->r6, rd32(rdram, object + 0x10));
                set_arg(ctx->r7, camera);
                wr32(rdram, sp + 0x10u, 0);  // back to the open field afterwards
                LOOKUP_FUNC(CAMERA_TO)(rdram, ctx);
                *ctx = saved;
            }
            continue;
        }
        if (order.organize || order.items) {
            // Only from the open field (mode 0), as the R menu would.
            const uint32_t mode = rd32(rdram, 0x801F0E00u);  // the field mode
            if (mode == 0) {
                LOOKUP_FUNC(order.organize ? OPEN_ORGANIZE : OPEN_ITEMS)(rdram, ctx);
                *ctx = saved;
                std::fprintf(stderr, "[field] opening the game's %s\n", order.organize ? "Organize" : "item screen");
            }
            else {
                std::fprintf(stderr, "[field] %s not opened: field mode 0x%X\n", order.organize ? "Organize" : "item screen",
                             mode);
                alerts::post(alerts::Kind::Info, std::string(order.organize ? "Organize" : "The item screen") +
                                                     " opens from the open field (close the game's menu first).");
            }
            continue;
        }
        if (order.dispatch) {
            for (int slot : order.slots) {
                const uint32_t object = rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
                if (!dispatchable(rdram, slot) || on_field_count(rdram) >= FIELD_LIMIT) {
                    std::fprintf(stderr, "[field] dispatch unit %d refused\n", slot + 1);
                    continue;
                }
                // The headquarters' position into the field update's dead locals
                // at sp+0x18..0x23, as the confirm uses its own sp+0x28.
                set_arg(ctx->r4, 0xFFFFFFFFu);
                set_arg(ctx->r5, sp + 0x18u);
                LOOKUP_FUNC(HQ_POSITION)(rdram, ctx);
                *ctx = saved;
                const uint32_t hx = rd32(rdram, sp + 0x18u), hy = rd32(rdram, sp + 0x1Cu), hz = rd32(rdram, sp + 0x20u);
                wr32(rdram, object, rd32(rdram, object) | 0x20u);
                wr32(rdram, sp + 0x10u, 1);  // fifth argument: inside the location
                set_arg(ctx->r4, object);
                set_arg(ctx->r5, hx);
                set_arg(ctx->r6, hy);
                set_arg(ctx->r7, hz);
                LOOKUP_FUNC(PLACE)(rdram, ctx);
                *ctx = saved;
                set_arg(ctx->r4, object);
                LOOKUP_FUNC(LOCATE)(rdram, ctx);
                *ctx = saved;
                std::fprintf(stderr, "[field] dispatched unit %d (flags 0x%08X)\n", slot + 1, rd32(rdram, object));
            }
            continue;
        }
        int moved = 0;
        // Start the inverse projection from the first selected unit's position.
        float x = 0.0f, y = 0.0f, z = 0.0f;
        bool have_point = false;
        for (int slot : order.slots) {
            const uint32_t object = rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
            if (!in_ram(object) || (rd32(rdram, object) & 0x11u) != 0x11u) {
                continue;
            }
            if (order.strategy >= 0) {
                // Battle Strategy lives in the unit record (+0x17), where the
                // Organize commit (func_ovlS_801D5328) writes it.
                rdram[(0x80197210u + static_cast<uint32_t>(slot) * 0x19u + 0x17u - 0x80000000u) ^ 3u] =
                    static_cast<uint8_t>(order.strategy);
                moved++;
                continue;
            }
            // A move, stop, pursue or return takes the unit from the auto
            // commander until its card's AI button hands it back.
            auto_commander::player_ordered(slot);
            {
                // Any new order (Stop too) ends this unit's chase; a pursue starts
                // a new one.
                std::lock_guard<std::mutex> lock(g_mutex);
                g_chases.erase(std::remove_if(g_chases.begin(), g_chases.end(),
                                              [&](const std::pair<int, int>& c) { return c.first == slot; }),
                               g_chases.end());
                if (order.pursue) {
                    g_chases.push_back({slot, order.target});
                }
            }
            if (order.hold) {
                set_arg(ctx->r4, object);
                LOOKUP_FUNC(HOLD)(rdram, ctx);
                *ctx = saved;
                moved++;
                continue;
            }
            // A new order ends any pursuit or hold first, as the game's own
            // commands do (0x801BAEAC): func_ovlN_801D45F8 clears flag 0x2000.
            // Neither the move nor the pursuit routine clears it, so without
            // this a unit that had pursued a target that then left the field,
            // or had been stopped, kept the flag with no target and stood
            // re-planning in place (developer playtest 2026-10-30: Lisa and
            // Nathan "camping", object flags 0x0004213B, +0x84 = -1).
            set_arg(ctx->r4, object);
            LOOKUP_FUNC(END_PURSUIT)(rdram, ctx);
            *ctx = saved;
            if (order.pursue) {
                // Straight at the target now; run_chases re-aims it as it moves.
                const uint32_t prey = rd32(rdram, OBJECTS + static_cast<uint32_t>(order.target) * 4u);
                if (in_ram(prey)) {
                    move_unit(rdram, ctx, object, rd32(rdram, prey + 0x08), rd32(rdram, prey + 0x0C),
                              rd32(rdram, prey + 0x10));
                }
                moved++;
                continue;
            }
            if (!have_point && order.at_location) {
                // The location's own point, at the unit's height, so it enters.
                x = order.wx;
                y = as_float(rd32(rdram, object + 0x0C));
                z = order.wz;
                have_point = true;
            }
            if (!have_point) {
                x = as_float(rd32(rdram, object + 0x08));
                y = as_float(rd32(rdram, object + 0x0C));
                z = as_float(rd32(rdram, object + 0x10));
                have_point = unproject(rdram, ctx, camera, order.fx, order.fy, y, x, z);
                *ctx = saved;
                if (!have_point) {
                    break;
                }
            }
            set_arg(ctx->r4, object);
            set_arg(ctx->r5, bits(x));
            set_arg(ctx->r6, bits(y));
            set_arg(ctx->r7, bits(z));
            LOOKUP_FUNC(MOVE_TO)(rdram, ctx);
            *ctx = saved;
            moved++;
        }
        std::fprintf(stderr, "[field] %s order for %d unit(s)%s\n",
                     order.strategy >= 0 ? "strategy" : order.hold ? "stop" : order.pursue ? "pursue" : "move", moved,
                     order.pursue || order.hold || order.strategy >= 0 ? "" : (have_point ? "" : ": no ground point"));
    }
}

ogre::events::Subscribe g_run{ogre::events::Event::FieldUpdate, "field orders",
                              [](const ogre::events::Args& e) { run_orders(e.rdram, e.ctx); }};

// The RTS edge pan (developer, 2026-10-30). The game's own scroll
// (func_ovlN_801BECF0, after its cursor mover) moves the camera at
// *(0x8019F5AC) only while its cursor moves, by delta/128 x speed, adding the
// same amount to the eye (+0x14 x, +0x1C z) and the look-at point (+0x20 x,
// +0x28 z) and keeping the point inside the map's world rect (0x801F0DD8 min
// x, 0x801F0DDC min z, 0x801F0DE0 max x, 0x801F0DE4 max z); screen right is
// +x and screen up +z. Pressing the D-pad from the pointer made the pan
// follow the cursor's ramp and its pull back to the screen centre. This moves
// the camera itself, once per frame at that function's exit (FieldFrame,
// 0x801BEF60): the velocity eases towards the pointer's push (about 0.1 s to
// full speed or to rest), the speed is 1.2 camera distances a second in real
// time, so it feels the same at every zoom and game speed, and the look-at
// point stays inside the world rect. Only on the open field (mode 0).
constexpr uint32_t CAMERA_PTR = 0x8019F5ACu;
constexpr uint32_t WORLD_RECT = 0x801F0DD8u;

uint32_t float_bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

void pan_camera(uint8_t* rdram) {
    static float vel_x = 0.0f;
    static float vel_z = 0.0f;
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::min(0.1f, std::chrono::duration<float>(now - last).count());
    last = now;
    const bool open_field = owns_field() && rd32(rdram, 0x801F0E00u) == 0;
    const float want_x = open_field ? static_cast<float>(g_pan_x.load(std::memory_order_relaxed)) : 0.0f;
    const float want_z = open_field ? -static_cast<float>(g_pan_y.load(std::memory_order_relaxed)) : 0.0f;
    const float ease = std::min(1.0f, dt * 10.0f);
    vel_x += (want_x - vel_x) * ease;
    vel_z += (want_z - vel_z) * ease;
    if (std::fabs(vel_x) < 0.01f && std::fabs(vel_z) < 0.01f) {
        vel_x = vel_z = 0.0f;
        return;
    }
    const uint32_t cam = rd32(rdram, CAMERA_PTR);
    if (!in_ram(cam)) {
        return;
    }
    auto f = [&](uint32_t off) { return as_float(rd32(rdram, cam + off)); };
    const float ex = f(0x14), ey = f(0x18), ez = f(0x1C);
    const float lx = f(0x20), ly = f(0x24), lz = f(0x28);
    const float dist = std::sqrt((ex - lx) * (ex - lx) + (ey - ly) * (ey - ly) + (ez - lz) * (ez - lz));
    const float speed = std::max(1.0f, dist) * 1.2f;
    const float min_x = as_float(rd32(rdram, WORLD_RECT)), min_z = as_float(rd32(rdram, WORLD_RECT + 4));
    const float max_x = as_float(rd32(rdram, WORLD_RECT + 8)), max_z = as_float(rd32(rdram, WORLD_RECT + 12));
    const float nx = min_x < max_x ? std::clamp(lx + vel_x * speed * dt, min_x, max_x) : lx + vel_x * speed * dt;
    const float nz = min_z < max_z ? std::clamp(lz + vel_z * speed * dt, min_z, max_z) : lz + vel_z * speed * dt;
    const float dx = nx - lx;
    const float dz = nz - lz;
    wr32(rdram, cam + 0x14, float_bits(ex + dx));
    wr32(rdram, cam + 0x20, float_bits(nx));
    wr32(rdram, cam + 0x1C, float_bits(ez + dz));
    wr32(rdram, cam + 0x28, float_bits(nz));
}

ogre::events::Subscribe g_pan{ogre::events::Event::FieldFrame, "field edge pan",
                              [](const ogre::events::Args& e) { pan_camera(e.rdram); }};

}  // namespace

bool modern() {
    return choice_index(Choice::FieldControls) == 0;
}

bool owns_field() {
    return modern() && on_field();
}

// UNIT REPORTS = LOG (settings.hpp -> Choice::UnitReports): the field stops in
// mode 0x2D (0x801F0E00) whenever a unit has something to say (camping,
// arrival; captures 1-3 of 2026-10-30 04:33: messages 0xD8 and 0xBF for the
// units in 0x801F0E20, written to 0x801F36F0), and waits for A. The overlay
// records the report in the alert log and presses A once for the player, so
// the game closes it through its own code; it presses again only after the
// mode has left 0x2D, so a press never lands on the field and opens a unit's
// command menu.
constexpr uint32_t FIELD_MODE = 0x801F0E00u;
constexpr uint32_t REPORT_UNIT = 0x801F0E20u;
constexpr uint32_t REPORT_MESSAGE = 0x801F36F0u;
constexpr uint32_t MODE_REPORT = 0x2Du;
bool g_report_seen = false;   // this stop has been logged and answered
int g_report_hold = 0;        // polls since this report came up

uint16_t report_buttons(const uint8_t* rdram) {
    if (rdram == nullptr || choice_index(Choice::UnitReports) != 0 || !on_field()) {
        g_report_seen = false;
        return 0;
    }
    if (rd32(rdram, FIELD_MODE) != MODE_REPORT) {
        g_report_seen = false;
        g_report_hold = 0;
        return 0;
    }
    if (!g_report_seen) {
        g_report_seen = true;
        g_report_hold = 0;
        const uint32_t slot = rd32(rdram, REPORT_UNIT);
        const unsigned message = (rd8(rdram, REPORT_MESSAGE) << 8) | rd8(rdram, REPORT_MESSAGE + 1);
        std::string who = "A unit";
        if (slot < PLAYER_UNITS) {
            who = "Unit " + std::to_string(slot + 1);
            const int ref = rd8(rdram, 0x80197210u + slot * 0x19u + 0x02u);
            if (ref >= 1 && ref < 100) {
                std::string name;
                for (uint32_t i = 0; i < 16; i++) {
                    const char c = static_cast<char>(rd8(rdram, 0x80193BE0u + static_cast<uint32_t>(ref) * 0x38u + i));
                    if (c == '\0') {
                        break;
                    }
                    name += (c >= 32 && c < 127) ? c : '?';
                }
                who += " (" + name + ")";
            }
        }
        // 0xD8 is the camping report (captures 8-12 of 04:37: five stops in six
        // seconds, units 3, 4 and 7); other ids are logged with their number.
        const std::string what = message == 0xD8 ? "is making camp" : "reports";
        char id[16];
        std::snprintf(id, sizeof(id), " [msg %02X]", message);
        alerts::post(alerts::Kind::Info, who + " " + what + id);
        std::fprintf(stderr, "[field] unit report: slot %u message 0x%02X answered\n", slot, message);
    }
    // A report can need more than one press (the text, then the close): A is
    // pulsed, two polls down and ten up, for as long as the field stays in
    // mode 0x2D (run 8 of 2026-10-30: one press left reports 0xBF up).
    g_report_hold++;
    if (g_report_hold > 600) {
        return 0;  // give the player the report after ~10 s of trying
    }
    return (g_report_hold % 12) < 2 ? 0x8000u : 0;  // A
}

// The game's Settings screen on the field (field mode 0x20, with the settings
// module's row labels at 0x802192C0; developer captures 22-46 of 2026-10-30):
// in MODERN mode B closes it as the player would, and the port's menu opens on
// its GAMEPLAY tab, which carries the same rows (ui.cpp game_settings()).
std::atomic<bool> g_open_settings{false};
bool g_settings_closing = false;
int g_settings_polls = 0;

uint16_t settings_buttons(const uint8_t* rdram) {
    if (rdram == nullptr) {
        return 0;
    }
    static const char kLabel[] = "Message speed";
    bool settings = rd32(rdram, 0x801F0E00u) == 0x20;
    for (uint32_t i = 0; settings && i + 1 < sizeof(kLabel); i++) {
        settings = rd8(rdram, 0x802192C0u + i) == static_cast<uint8_t>(kLabel[i]);
    }
    if (settings) {
        g_settings_closing = true;
        g_settings_polls++;
        return (g_settings_polls % 12) < 2 ? 0x4000u : 0;  // B
    }
    if (g_settings_closing) {
        g_settings_closing = false;
        g_settings_polls = 0;
        g_open_settings.store(true);
        std::fprintf(stderr, "[field] the game's Settings -> the port's GAMEPLAY tab\n");
    }
    return 0;
}

bool take_open_settings() {
    return g_open_settings.exchange(false);
}

// --- The pad on the MODERN open field ---------------------------------------
//
// docs/anotherworld/design-field-controls.md section 2, gamepad column. The
// game's cursor (moved by the left stick and the D-pad, as in the game) is the
// pad's pointer:
//   A        a unit of ours under the cursor: select it. With units selected:
//            order them to the cursor (ground, town or enemy, as a right
//            click). Otherwise the game's own A.
//   B        close the command menu, else clear the selection, else the game's B.
//   X (hold) a green circle grows from the cursor, 8 to 96 field pixels over a
//            second; the release adds the units inside it to the selection.
//   Y        tap: the command menu, the overlay's buttons in turn (D-pad moves,
//            A presses, B or Y closes). Hold: the game's R, its hold menu.
//   RB / LB  next / previous idle unit.
// The buttons the overlay takes press nothing in the game (claimed_pad_buttons,
// read by sdl_platform.cpp gamecontroller_buttons). Before this the pad's A on
// the field opened the game's own menus and the overlay never selected
// anything (developer report, 2026-11-01).
namespace {

constexpr uint32_t CURSOR_STATE = 0x800E91D0u;  // mission_cursor.hpp: +0x08 x, +0x0C y, f32 framebuffer pixels
constexpr float CIRCLE_MIN = 8.0f;
constexpr float CIRCLE_MAX = 96.0f;
constexpr Uint64 Y_HOLD_MS = 300;
constexpr uint32_t pad_bit(int button) {
    return 1u << button;
}
constexpr uint32_t DPAD_BITS = pad_bit(SDL_CONTROLLER_BUTTON_DPAD_UP) | pad_bit(SDL_CONTROLLER_BUTTON_DPAD_DOWN) |
                               pad_bit(SDL_CONTROLLER_BUTTON_DPAD_LEFT) | pad_bit(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);

bool game_cursor(float& fx, float& fy) {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram == nullptr) {
        return false;
    }
    fx = as_float(rd32(rdram, CURSOR_STATE + 0x08));
    fy = as_float(rd32(rdram, CURSOR_STATE + 0x0C));
    return fx >= 0.0f && fy >= 0.0f && fx < 320.0f && fy < 240.0f;
}

// The open field with the overlay in charge: MODERN, field mode 0 (no game
// menu up), no Organize view and no Esc menu.
bool pad_field() {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    return rdram != nullptr && owns_field() && rd32(rdram, FIELD_MODE) == 0 && !organize_view::open() &&
           !host::menu_visible();
}

// Caller holds g_mutex.
float circle_radius() {
    const float t = std::min(1.0f, static_cast<float>(SDL_GetTicks64() - g_circle_at) / 1000.0f);
    return CIRCLE_MIN + (CIRCLE_MAX - CIRCLE_MIN) * t;
}

// A press the overlay acts on; returns whether it took the button. Caller holds
// g_mutex.
bool pad_down(int button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A: {
            if (g_menu_open) {
                if (!g_menu_id.empty()) {
                    command(g_menu_id, false);
                }
                return true;
            }
            float fx = 0.0f, fy = 0.0f;
            if (!game_cursor(fx, fy)) {
                return true;
            }
            const int own = unit_at(fx, fy, true);
            if (own >= 0) {
                pick(own, false);
            }
            else if (!g_selected.empty()) {
                Order order;
                order.slots = g_selected;
                order.target = unit_at(fx, fy, false);
                order.pursue = order.target >= 0;
                order.fx = fx;
                order.fy = fy;
                order.at_location = !order.pursue && field_view::location_at(fx, fy, order.wx, order.wz) >= 0;
                g_orders.push_back(order);
            }
            else {
                g_inject_a.store(3);  // nothing of ours here: the game's own A
            }
            return true;
        }
        case SDL_CONTROLLER_BUTTON_B:
            if (g_menu_open) {
                g_menu_open = false;
                return true;
            }
            if (!g_selected.empty()) {
                g_selected.clear();
                return true;
            }
            return false;
        case SDL_CONTROLLER_BUTTON_X:
            g_circle = true;
            g_circle_at = SDL_GetTicks64();
            return true;
        case SDL_CONTROLLER_BUTTON_Y:
            g_y_at = std::max<Uint64>(1, SDL_GetTicks64());
            return true;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: {
            const int slot = next_idle(g_cycle, button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ? 1 : -1);
            if (slot >= 0) {
                g_cycle = slot;
                g_selected = {slot};
                Order order;
                order.jump = true;
                order.slots = {slot};
                g_orders.push_back(order);
            }
            return true;
        }
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: {
            if (!g_menu_open) {
                return false;
            }
            const int dx = button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT ? 1 : 0;
            const int dy = button == SDL_CONTROLLER_BUTTON_DPAD_UP ? -1 : button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ? 1 : 0;
            g_menu_id = field_view::hit_step(g_menu_id, dx, dy);
            return true;
        }
        default:
            return false;
    }
}

// The release of a button pad_down took. Caller holds g_mutex.
void pad_up(int button) {
    if (button == SDL_CONTROLLER_BUTTON_X && g_circle) {
        g_circle = false;
        float fx = 0.0f, fy = 0.0f;
        if (game_cursor(fx, fy)) {
            const float r = circle_radius();
            for (int slot = 0; slot < PLAYER_UNITS; slot++) {
                float x, y;
                if (field_view::screen_of(slot, x, y) && (x - fx) * (x - fx) + (y - fy) * (y - fy) <= r * r &&
                    std::find(g_selected.begin(), g_selected.end(), slot) == g_selected.end()) {
                    g_selected.push_back(slot);
                }
            }
        }
    }
    else if (button == SDL_CONTROLLER_BUTTON_Y) {
        if (g_y_at != 0 && !g_y_game.load() && SDL_GetTicks64() - g_y_at < Y_HOLD_MS) {
            g_menu_open = !g_menu_open;
            if (g_menu_open) {
                g_menu_id = field_view::hit_step(g_selected.empty() ? "cmd-dispatch" : "cmd-stop", 0, 0);
            }
        }
        g_y_at = 0;
        g_y_game.store(false);
    }
}

}  // namespace

uint32_t claimed_pad_buttons() {
    uint32_t claims = g_held_claims.load();
    if (!pad_field()) {
        return claims;
    }
    claims |= pad_bit(SDL_CONTROLLER_BUTTON_A) | pad_bit(SDL_CONTROLLER_BUTTON_X) | pad_bit(SDL_CONTROLLER_BUTTON_Y) |
              pad_bit(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) | pad_bit(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_menu_open || !g_selected.empty()) {
        claims |= pad_bit(SDL_CONTROLLER_BUTTON_B);
    }
    if (g_menu_open) {
        claims |= DPAD_BITS;
    }
    return claims;
}

bool pad_circle(float& x, float& y, float& radius) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_circle || !game_cursor(x, y)) {
        return false;
    }
    radius = circle_radius();
    return true;
}

std::string pad_focus() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_menu_open ? g_menu_id : std::string();
}

bool pad_menu_open() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_menu_open;
}

uint16_t filter_buttons(uint16_t buttons) {
    if (!owns_field()) {
        return buttons;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // A game menu (mode != 0) takes over: the overlay's menu closes.
        const uint8_t* rdram = ultramodern::get_rdram_base();
        if (rdram != nullptr && rd32(rdram, FIELD_MODE) != 0) {
            g_menu_open = false;
        }
        if (g_y_at != 0 && !g_menu_open && SDL_GetTicks64() - g_y_at >= Y_HOLD_MS) {
            g_y_game.store(true);
        }
    }
    if (g_y_game.load()) {
        buttons |= 0x0010u;  // R
    }
    if (const int a = g_inject_a.load(); a > 0) {
        g_inject_a.store(a - 1);
        buttons |= 0x8000u;  // A
    }
    buttons |= report_buttons(ultramodern::get_rdram_base());
    buttons |= settings_buttons(ultramodern::get_rdram_base());
    // R stays with the game: its hold menu is the only way to Dispatch a unit
    // until the overlay has its own (developer report 2026-10-30).
    return buttons;
}

bool handle(const SDL_Event& event, SDL_Window* window, bool panel_owns_mouse) {
    // The left stick and the D-pad, for PAUSE ON SELECT (update_pause), on every
    // screen: a release must be seen wherever it happens.
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            g_stick_x.store(event.caxis.value);
        }
        else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            g_stick_y.store(event.caxis.value);
        }
    }
    if ((event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) &&
        (pad_bit(event.cbutton.button) & DPAD_BITS) != 0) {
        if (event.type == SDL_CONTROLLERBUTTONDOWN) {
            g_dpad_held.fetch_or(pad_bit(event.cbutton.button));
        }
        else {
            g_dpad_held.fetch_and(~pad_bit(event.cbutton.button));
        }
    }
    // The release of a button the overlay took is the overlay's, wherever the
    // game has gone since the press.
    if (event.type == SDL_CONTROLLERBUTTONUP && event.cbutton.button < 32 &&
        (g_held_claims.load() & pad_bit(event.cbutton.button)) != 0) {
        g_held_claims.fetch_and(~pad_bit(event.cbutton.button));
        std::lock_guard<std::mutex> lock(g_mutex);
        pad_up(event.cbutton.button);
        return true;
    }
    if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button < 32 && window != nullptr && pad_field()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (pad_down(event.cbutton.button)) {
            g_held_claims.fetch_or(pad_bit(event.cbutton.button));
            return true;
        }
        return false;
    }
    if (window == nullptr || panel_owns_mouse || !on_field()) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_menu_open = false;
        g_circle = false;
        g_pressed = g_dragging = false;
        g_strip_press = false;
        g_pan_x.store(0, std::memory_order_relaxed);
        g_pan_y.store(0, std::memory_order_relaxed);
        return false;
    }
    const bool own = modern();
    if (!own) {
        // CLASSIC: the game's own cursor and menus, and no overlay.
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pressed = g_dragging = false;
        g_selected.clear();
        return false;
    }
    const Uint32 id = SDL_GetWindowID(window);
    // H returns the selection to the headquarters, Delete stops it, I opens the
    // Group Items list.
    if (event.type == SDL_KEYDOWN && !event.key.repeat &&
        (event.key.keysym.sym == SDLK_h || event.key.keysym.sym == SDLK_DELETE || event.key.keysym.sym == SDLK_i)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_selected.empty()) {
            return false;
        }
        command(event.key.keysym.sym == SDLK_h ? "cmd-hq" : event.key.keysym.sym == SDLK_i ? "cmd-item" : "cmd-stop",
                false);
        return true;
    }
    // F4 turns the auto commander on and off.
    if (event.type == SDL_KEYDOWN && !event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_F4) {
        auto_commander::toggle();
        return true;
    }
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_BACKSPACE && !event.key.repeat) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_selected.empty()) {
            g_selected.clear();
            return true;
        }
        return false;
    }
    // Idle cycling: `.` selects the next unit standing on the open map, `,` the
    // previous one.
    if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_PERIOD || event.key.keysym.sym == SDLK_COMMA)) {
        std::lock_guard<std::mutex> lock(g_mutex);
        const int slot = next_idle(g_cycle, event.key.keysym.sym == SDLK_PERIOD ? 1 : -1);
        if (slot >= 0) {
            g_cycle = slot;
            g_selected = {slot};
            Order order;
            order.jump = true;
            order.slots = {slot};
            g_orders.push_back(order);
        }
        return true;
    }
    if (event.type == SDL_WINDOWEVENT && event.window.windowID == id && event.window.event == SDL_WINDOWEVENT_LEAVE) {
        g_pan_x.store(0, std::memory_order_relaxed);
        g_pan_y.store(0, std::memory_order_relaxed);
        return false;
    }
    if (own && event.type == SDL_MOUSEWHEEL && event.wheel.windowID == id) {
        // Over the army strip the wheel scrolls it; elsewhere it is swallowed (it
        // would press the D-pad and move the game's cursor).
        int mx = 0, my = 0, win_w = 0, win_h = 0;
        SDL_GetMouseState(&mx, &my);
        SDL_GetWindowSize(window, &win_w, &win_h);
        if (win_w > 0 && win_h > 0 && field_view::over_strip(mx / static_cast<float>(win_w), my / static_cast<float>(win_h))) {
            g_strip_scroll.fetch_add(-event.wheel.y * 80);
        }
        return true;
    }
    if (own && event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_PAGEUP || event.key.keysym.sym == SDLK_PAGEDOWN)) {
        g_strip_scroll.fetch_add(event.key.keysym.sym == SDLK_PAGEUP ? -240 : 240);
        return true;
    }
    if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
        (event.button.windowID != id || event.button.which == REPLAY_ID)) {
        return false;
    }
    if (event.type == SDL_MOUSEMOTION && event.motion.windowID == id) {
        float fx, fy;
        to_framebuffer(window, event.motion.x, event.motion.y, fx, fy);
        if (own) {
            // Scroll only with the pointer at the window's own edge, so moving
            // to the strip, the bar or a town does not move the field.
            int win_w = 0, win_h = 0;
            SDL_GetWindowSize(window, &win_w, &win_h);
            g_pan_x.store(event.motion.x < EDGE_POINTS ? -1 : event.motion.x >= win_w - EDGE_POINTS ? 1 : 0,
                          std::memory_order_relaxed);
            g_pan_y.store(event.motion.y < EDGE_POINTS ? -1 : event.motion.y >= win_h - EDGE_POINTS ? 1 : 0,
                          std::memory_order_relaxed);
        }
        std::lock_guard<std::mutex> lock(g_mutex);
        g_pointer_x = fx;
        g_pointer_y = fy;
        if (g_pressed) {
            g_now_x = fx;
            g_now_y = fy;
            if (!g_dragging && std::hypot(fx - g_press_x, fy - g_press_y) > DRAG_START) {
                g_dragging = true;
            }
        }
        return false;  // classic: the game's cursor still follows the pointer
    }
    if (event.type != SDL_MOUSEBUTTONDOWN && event.type != SDL_MOUSEBUTTONUP) {
        return false;
    }
    const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;

    // The overlay's own clickable elements: strip tiles, selection cards and
    // command buttons. The press acts; its release is swallowed with it.
    if (event.button.button == SDL_BUTTON_LEFT) {
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(window, &win_w, &win_h);
        std::lock_guard<std::mutex> lock(g_mutex);
        if (event.type == SDL_MOUSEBUTTONUP && g_strip_press) {
            g_strip_press = false;
            return true;
        }
        const std::string hit = win_w > 0 && win_h > 0
                                    ? field_view::hit_at(event.button.x / static_cast<float>(win_w),
                                                         event.button.y / static_cast<float>(win_h))
                                    : std::string();
        if (event.type == SDL_MOUSEBUTTONDOWN && !hit.empty()) {
            g_strip_press = true;
            g_pressed = g_dragging = false;
            command(hit, shift);
            return true;
        }
    }

    float fx = 0.0f, fy = 0.0f;
    if (!to_framebuffer(window, event.button.x, event.button.y, fx, fy)) {
        return own;  // modern: the pillarbox is still not the game's
    }

    if (event.button.button == SDL_BUTTON_RIGHT) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_selected.empty()) {
            // The game's own B, in both modes: it backs out of the game's menus
            // (a town or unit list opened from the keyboard or pad would trap a
            // mouse player otherwise; captures 14-15, 2026-10-30).
            return false;
        }
        if (event.type == SDL_MOUSEBUTTONDOWN) {
            Order order;
            order.slots = g_selected;
            order.target = unit_at(fx, fy, false);
            order.pursue = order.target >= 0;
            order.fx = fx;
            order.fy = fy;
            // A location under the pointer: its own point, so the units enter it.
            order.at_location = !order.pursue && field_view::location_at(fx, fy, order.wx, order.wz) >= 0;
            g_orders.push_back(order);
        }
        return true;
    }
    if (event.button.button != SDL_BUTTON_LEFT) {
        return own;  // modern: the middle button is not the game's Start here
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (event.type == SDL_MOUSEBUTTONDOWN) {
        // Held back until the release decides between a selection and the game's A.
        g_pressed = true;
        g_dragging = false;
        g_press_x = g_now_x = fx;
        g_press_y = g_now_y = fy;
        return true;
    }
    if (!g_pressed) {
        return own;
    }
    g_pressed = false;
    if (g_dragging) {
        g_dragging = false;
        const float x0 = std::min(g_press_x, fx), x1 = std::max(g_press_x, fx);
        const float y0 = std::min(g_press_y, fy), y1 = std::max(g_press_y, fy);
        if (!shift) {
            g_selected.clear();
        }
        for (int slot = 0; slot < PLAYER_UNITS; slot++) {
            float x, y;
            if (field_view::screen_of(slot, x, y) && x >= x0 && x <= x1 && y >= y0 && y <= y1 &&
                std::find(g_selected.begin(), g_selected.end(), slot) == g_selected.end()) {
                g_selected.push_back(slot);
            }
        }
        return true;
    }
    const int hit = unit_at(g_press_x, g_press_y, true);
    if (hit >= 0) {
        pick(hit, shift);
        return true;
    }
    if (own) {
        // Modern: a click on nothing clears the selection; the game's command
        // menu never opens from the mouse.
        if (!shift) {
            g_selected.clear();
        }
        return true;
    }
    // Classic: nothing of ours under the click, so the game gets its own A.
    replay_click(event);
    return true;
}

bool pointer(float& fx, float& fy) {
    // With the pad in use the pointer is the game's cursor, which the stick moves.
    if (pad_in_use() && game_cursor(fx, fy)) {
        return true;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    fx = g_pointer_x;
    fy = g_pointer_y;
    return g_pointer_x >= 0.0f;
}

int strip_scroll() {
    return g_strip_scroll.load();
}

void set_strip_scroll(int pixels) {
    g_strip_scroll.store(pixels);
}

void open_game_organize() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "[field] Organize asked for\n");
    command("cmd-organize", false);
}

void update_pause() {
    // PAUSE ON SELECT. A new selection closes the frame gate; a command (an
    // order waiting for the game thread, which is held at the gate) or an
    // empty selection opens it. The unit stays selected after the command.
    // The pause holds the game's frames, and the edge pan, the game's cursor
    // and the markers all run on them, so while the pointer pushes an edge the
    // pause lifts and the field scrolls; it comes back when the push stops,
    // until a command or an empty selection ends it (developer, 2026-10-30:
    // "select all units, the mouse doesn't move and I can't scroll").
    static std::vector<int> last;
    static bool want_pause = false;
    const uint8_t* rdram = ultramodern::get_rdram_base();
    const bool active = rdram != nullptr && owns_field() && choice_on(Choice::SelectPause) &&
                        rd32(rdram, 0x801F0E00u) == 0 && !host::menu_visible();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!active || !g_orders.empty() || g_selected.empty()) {
        want_pause = false;
    }
    else if (g_selected != last) {
        want_pause = true;
    }
    last = g_selected;
    // The pad steers the game's cursor, which runs on the game's frames, so a
    // pushed left stick or a held D-pad lifts the pause too (outside the
    // command menu, where the D-pad walks the menu).
    const bool steering = std::abs(g_stick_x.load()) > 12000 || std::abs(g_stick_y.load()) > 12000 ||
                          (g_dpad_held.load() != 0 && !g_menu_open);
    const bool scrolling = g_pan_x.load(std::memory_order_relaxed) != 0 || g_pan_y.load(std::memory_order_relaxed) != 0 ||
                           steering;
    host::hold_game(want_pause && !scrolling);
}

int chase_target(int slot) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& [chaser, target] : g_chases) {
        if (chaser == slot) {
            return target;
        }
    }
    return -1;
}

bool dispatch_open() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_dispatch_open;
}

bool can_dispatch(int slot) {
    return dispatchable(ultramodern::get_rdram_base(), slot);
}

int units_out() {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    return rdram != nullptr ? on_field_count(rdram) : 0;
}

int field_limit() {
    return FIELD_LIMIT;
}

bool items_open() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_items_open && !g_selected.empty();
}

std::vector<int> selected() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_selected;
}

bool drag_box(float& x0, float& y0, float& x1, float& y1) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_dragging) {
        return false;
    }
    x0 = std::min(g_press_x, g_now_x);
    x1 = std::max(g_press_x, g_now_x);
    y0 = std::min(g_press_y, g_now_y);
    y1 = std::max(g_press_y, g_now_y);
    return true;
}

}  // namespace ogre::field_select

extern "C" int ogre_field_limit(void) {
    return ogre::field_select::field_limit();
}

