// See camera_zoom.hpp and docs/anotherworld/design-camera-zoom.md.

#include "camera_zoom.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game_addresses.hpp"
#include "game_events.hpp"
#include "options.hpp"
#include "recomp.h"

extern "C" bool ogre_map_cursor_pointer(int* fb_x, int* fb_y);

namespace ogre::camera_zoom {

namespace {

// --- Guest memory --------------------------------------------------------------

constexpr uint32_t SCENE = ::ogre::addr::kActiveScene;  // u16
constexpr int SCENE_FIELD = 0x03;
constexpr int SCENE_MAP = 0x05;

uint8_t rd8(const uint8_t* rdram, uint32_t a) {
    return rdram[(a - 0x80000000u) ^ 3u];
}

uint32_t rd32(const uint8_t* rdram, uint32_t a) {
    uint32_t v = 0;
    std::memcpy(&v, rdram + (a - 0x80000000u), sizeof(v));
    return v;
}

void wr32(uint8_t* rdram, uint32_t a, uint32_t v) {
    std::memcpy(rdram + (a - 0x80000000u), &v, sizeof(v));
}

int16_t rd16s(const uint8_t* rdram, uint32_t a) {
    uint16_t v = 0;
    std::memcpy(&v, rdram + ((a - 0x80000000u) ^ 2u), sizeof(v));
    return static_cast<int16_t>(v);
}

bool in_ram(uint32_t a) {
    return a >= 0x80000000u && a < 0x80800000u;
}

float as_float(uint32_t b) {
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}

uint32_t bits(float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

int scene(const uint8_t* rdram) {
    return (rd8(rdram, SCENE) << 8) | rd8(rdram, SCENE + 1);
}

// --- Input, shared with the platform thread -------------------------------------

using Clock = std::chrono::steady_clock;

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}

// A map counts as up for the input side while its frame ran within this long.
constexpr int64_t kSeenMs = 250;

bool enabled() {
    return feature_enabled(Choice::CameraZoom, "OGRE_CAMERA_ZOOM");
}

// The last frame each map could zoom: the field in mode 0 (the open field, no
// menu), the world map with its cursor shown and no window open.
std::atomic<int64_t> g_field_zoomable{0};
std::atomic<int64_t> g_map_zoomable{0};

bool recent(const std::atomic<int64_t>& at) {
    return now_ms() - at.load(std::memory_order_relaxed) <= kSeenMs;
}

// Wheel notches not yet applied, and the pointer of the last one.
std::atomic<int> g_wheel_steps{0};
std::atomic<float> g_wheel_x{0.0f};
std::atomic<float> g_wheel_y{0.0f};

// The pad's zoom axis (-1 zoom in .. 1 zoom out) and when the poll set it.
std::atomic<float> g_pad_axis{0.0f};
std::atomic<int64_t> g_pad_at{0};

float pad_axis() {
    return now_ms() - g_pad_at.load(std::memory_order_relaxed) <= kSeenMs ? g_pad_axis.load(std::memory_order_relaxed)
                                                                           : 0.0f;
}

// Seconds since the previous call with the same clock, clamped so a pause or a
// hitch does not jump the zoom.
float step_dt(Clock::time_point& last) {
    const Clock::time_point now = Clock::now();
    const float dt = std::chrono::duration<float>(now - last).count();
    last = now;
    return std::clamp(dt, 0.0f, 0.1f);
}

// How fast the shown zoom follows its target (per second), and the rates.
constexpr float kEase = 14.0f;
constexpr float kWheelStep = 1.12f;  // one notch
constexpr float kPadRate = 1.4f;     // ln(zoom) per second at full stick

// `OGRE_ZOOM_FIELD` / `OGRE_ZOOM_MAP`: the session's starting zoom (the field's
// camera distance share, the map's magnification), clamped to the range. A test
// aid: a scripted run can take its pictures at a known zoom with no wheel.
float start_zoom(const char* env, float lo, float hi) {
    const char* value = std::getenv(env);
    if (value == nullptr || value[0] == '\0') {
        return 1.0f;
    }
    const float parsed = static_cast<float>(std::atof(value));
    return parsed > 0.0f ? std::clamp(parsed, lo, hi) : 1.0f;
}

float ease(float value, float target, float dt) {
    const float next = value + (target - value) * std::min(1.0f, dt * kEase);
    return std::fabs(next - target) < 0.001f * target ? target : next;
}

// --- Mission field -------------------------------------------------------------------

// R, the camera distance (see camera_zoom.hpp), the camera pointer, the field
// mode and the world rect the look-at point stays in (field_select.cpp).
constexpr uint32_t FIELD_DISTANCE = 0x8018F598u;
constexpr uint32_t FIELD_CAMERA = 0x8018F5ACu;
constexpr uint32_t FIELD_MODE = 0x801F0E00u;
constexpr uint32_t WORLD_RECT = 0x801F0DD8u;
constexpr uint32_t PROJECT = 0x801A9DD0u;  // func_801A9DD0(camera, &xyz, &sx, &sy)
// The camera distance as a share of the game's own, maintainer's range. The far
// limit was 1.5 and was doubled after the first play-test ("the zoom out needs
// to double", 2026-11-02).
constexpr float kFieldNear = 0.5f;
constexpr float kFieldFar = 3.0f;

struct Field {
    bool have_base = false;
    float base = 0.0f;           // the game's R for this mission
    uint32_t written = 0;        // the bits this module last stored
    float scale = 1.0f;          // shown, as a share of base
    float target = start_zoom("OGRE_ZOOM_FIELD", kFieldNear, kFieldFar);
    bool anchored = false;       // a wheel zoom keeps (anchor_x, anchor_z) under the pointer
    float anchor_x = 0.0f, anchor_z = 0.0f;
    Clock::time_point last = Clock::now();
};
Field g_field;

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

// The game's projection of (x, y, z) through `camera`, using the scroll's dead
// locals at sp+0x10..0x23 (game event 4) for the vector and the outputs.
bool project(uint8_t* rdram, recomp_context* ctx, uint32_t camera, float x, float y, float z, float& sx, float& sy) {
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

// The ground point at height y under framebuffer pixel (fx, fy): the
// projection inverted by Newton's method, starting from (x, z).
bool unproject(uint8_t* rdram, recomp_context* ctx, uint32_t camera, float fx, float fy, float y, float& x, float& z) {
    for (int step = 0; step < 10; step++) {
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
        if (rx * rx + ry * ry < 0.04f) {
            return std::isfinite(x) && std::isfinite(z);
        }
    }
    return std::isfinite(x) && std::isfinite(z);
}

// Game event 4: the scroll has moved the look-at point, and func_ovlN_801BEF74
// places the eye from R next.
void field_frame(uint8_t* rdram, recomp_context* ctx) {
    Field& f = g_field;
    const float dt = step_dt(f.last);
    if (scene(rdram) != SCENE_FIELD) {
        return;
    }
    const uint32_t camera = rd32(rdram, FIELD_CAMERA);
    const uint32_t r_bits = rd32(rdram, FIELD_DISTANCE);
    const float r = as_float(r_bits);
    if (!in_ram(camera) || !std::isfinite(r) || r <= 0.0f) {
        return;
    }
    // The mission set-up stored a new R (a new mission, or the field after a
    // battle or a menu that reloads it): that is the game's own distance.
    if (!f.have_base || r_bits != f.written) {
        f.have_base = true;
        f.base = r;
        f.scale = f.target;  // the session's zoom applies at once to the new field
        f.anchored = false;
    }
    const bool open_field = rd32(rdram, FIELD_MODE) == 0;
    const bool on = enabled();
    if (on && open_field) {
        g_field_zoomable.store(now_ms(), std::memory_order_relaxed);
    }
    if (!on) {
        f.target = 1.0f;
        f.anchored = false;
    }
    if (on && open_field) {
        const int steps = g_wheel_steps.exchange(0, std::memory_order_relaxed);
        if (steps != 0) {
            f.target = std::clamp(f.target * std::pow(kWheelStep, static_cast<float>(-steps)), kFieldNear, kFieldFar);
            // The ground under the pointer stays under it while the zoom eases.
            const float lx = as_float(rd32(rdram, camera + 0x20u));
            const float ly = as_float(rd32(rdram, camera + 0x24u));
            const float lz = as_float(rd32(rdram, camera + 0x28u));
            float px = lx, pz = lz;
            f.anchored = unproject(rdram, ctx, camera, g_wheel_x.load(std::memory_order_relaxed),
                                   g_wheel_y.load(std::memory_order_relaxed), ly, px, pz);
            f.anchor_x = px;
            f.anchor_z = pz;
        }
        const float axis = pad_axis();
        if (axis != 0.0f) {
            f.target = std::clamp(f.target * std::exp(axis * kPadRate * dt), kFieldNear, kFieldFar);
            f.anchored = false;  // the pad zooms on the look-at point
        }
    }
    else {
        g_wheel_steps.store(0, std::memory_order_relaxed);
    }

    const float before = f.scale;
    f.scale = ease(f.scale, f.target, dt);
    if (f.anchored && open_field && f.scale != before) {
        // Scaling the eye's offset about the look-at point by k moves a ground
        // point P on screen as if the look-at point had moved to
        // P + (look - P) / k; moving it to P + k (look - P) keeps P in place.
        const float k = f.scale / before;
        const float lx = as_float(rd32(rdram, camera + 0x20u));
        const float lz = as_float(rd32(rdram, camera + 0x28u));
        float nx = f.anchor_x + k * (lx - f.anchor_x);
        float nz = f.anchor_z + k * (lz - f.anchor_z);
        const float min_x = as_float(rd32(rdram, WORLD_RECT)), min_z = as_float(rd32(rdram, WORLD_RECT + 4));
        const float max_x = as_float(rd32(rdram, WORLD_RECT + 8)), max_z = as_float(rd32(rdram, WORLD_RECT + 12));
        if (min_x < max_x) {
            nx = std::clamp(nx, min_x, max_x);
        }
        if (min_z < max_z) {
            nz = std::clamp(nz, min_z, max_z);
        }
        // func_ovlN_801BEF74 sets the eye's y and z from the look-at point; the
        // eye's x moves with the look-at point, as in the game's own scroll.
        const float ex = as_float(rd32(rdram, camera + 0x14u));
        const float ez = as_float(rd32(rdram, camera + 0x1Cu));
        wr32(rdram, camera + 0x14u, bits(ex + (nx - lx)));
        wr32(rdram, camera + 0x1Cu, bits(ez + (nz - lz)));
        wr32(rdram, camera + 0x20u, bits(nx));
        wr32(rdram, camera + 0x28u, bits(nz));
    }
    if (f.scale == f.target) {
        f.anchored = false;
    }
    const uint32_t want = bits(f.base * f.scale);
    if (want != r_bits) {
        wr32(rdram, FIELD_DISTANCE, want);
    }
    f.written = want;
}

ogre::events::Subscribe g_field_sub{ogre::events::Event::FieldFrame, "camera zoom",
                                    [](const ogre::events::Args& e) { field_frame(e.rdram, e.ctx); }};

// --- World map -----------------------------------------------------------------------

constexpr uint32_t GFX_HEAD = 0x800E9BA0u;   // the display list's next free command
constexpr uint32_t MAP_STATE = 0x80197B18u;  // the map module's state pointer
constexpr uint32_t ST_SCROLL_X = 0x40u, ST_SCROLL_Y = 0x44u;  // s32, 0..256 / 0..124
constexpr uint32_t ST_CURSOR_DRAW = 0x50u;                    // u8
constexpr uint32_t ST_CURSOR_X = 0x52u, ST_CURSOR_Y = 0x54u;  // s16, screen pixels
constexpr uint32_t ST_WINDOW = 0x428u;  // u8: a window over the map (func_ovlM_801A39B4 draws it)
// The map layer's scissor (0x801A1428: ED080080 00480364) and its centre.
constexpr float kLeft = 32.0f, kTop = 32.0f, kRight = 288.0f, kBottom = 217.0f;
constexpr float kCx = (kLeft + kRight) * 0.5f, kCy = (kTop + kBottom) * 0.5f;
// The game's window onto the map, and the map the scroll can show.
constexpr float kWinW = kRight - kLeft, kWinH = kBottom - kTop;
constexpr float kMapW = 512.0f, kMapH = 124.0f + kWinH;
constexpr float kMapNear = 0.5f;  // the whole map's width fits the frame
constexpr float kMapFar = 2.0f;
// The terrain loop: 25 strips of 0x60 bytes (0x801A16BC..0x801A185C).
constexpr int kStrips = 25;
constexpr uint32_t kStripBytes = 0x60u;
// The port's strips, three buffers in RAM the game never allocates
// (game_addresses.hpp: the heaps end at 0x80400000).
constexpr uint32_t kZoomDl = 0x80440000u;
constexpr uint32_t kZoomDlBytes = 0x8000u;
constexpr int kZoomDlCount = 3;

struct Map {
    bool init = false;
    float z = 1.0f;          // magnification, shown
    float target = start_zoom("OGRE_ZOOM_MAP", kMapNear, kMapFar);
    float mx = 0.0f, my = 0.0f;  // the map texel at the frame centre
    float scroll_x = 0.0f, scroll_y = 0.0f;
    bool active = false;     // the picture differs from the game's
    bool anchored = false;
    float anchor_x = kCx, anchor_y = kCy;  // the framebuffer pixel a wheel zoom keeps
    int64_t seen = 0;
    uint32_t terrain_end = 0;
    uint32_t cursor_begin = 0;
    int buffer = 0;
    Clock::time_point last = Clock::now();
};
Map g_map;

// The zoomed picture's framebuffer pixel for a game screen pixel.
float zx(float x) {
    return kCx + (x - kLeft + g_map.scroll_x - g_map.mx) * g_map.z;
}
float zy(float y) {
    return kCy + (y - kTop + g_map.scroll_y - g_map.my) * g_map.z;
}

int quarter(float pixels) {
    return std::clamp(static_cast<int>(std::lround(pixels * 4.0f)), 0, 0xFFF);
}

// Keeps `centre` so a view of half-size `half` lies inside [lo, lo + span] when
// it is smaller, and around it when it is larger; then inside [0, size].
float constrain(float centre, float half, float lo, float span, float size) {
    const float a = lo + half, b = lo + span - half;
    centre = std::clamp(centre, std::min(a, b), std::max(a, b));
    if (2.0f * half <= size) {
        centre = std::clamp(centre, half, size - half);
    }
    else {
        centre = size * 0.5f;
    }
    return centre;
}

void update_map_view(const uint8_t* rdram, uint32_t state) {
    Map& m = g_map;
    const float dt = step_dt(m.last);
    const int64_t now = now_ms();
    m.scroll_x = static_cast<float>(static_cast<int32_t>(rd32(rdram, state + ST_SCROLL_X)));
    m.scroll_y = static_cast<float>(static_cast<int32_t>(rd32(rdram, state + ST_SCROLL_Y)));
    const float game_mx = m.scroll_x + kWinW * 0.5f, game_my = m.scroll_y + kWinH * 0.5f;
    if (!m.init || now - m.seen > 1000) {
        // A new visit: the session's zoom, centred where the game is.
        m.init = true;
        m.z = m.target;
        m.mx = game_mx;
        m.my = game_my;
        m.anchored = false;
    }
    m.seen = now;
    const bool on = enabled();
    const bool cursor_up = rd8(rdram, state + ST_CURSOR_DRAW) != 0 && rd8(rdram, state + ST_WINDOW) == 0;
    if (on && cursor_up) {
        g_map_zoomable.store(now, std::memory_order_relaxed);
    }
    if (!on) {
        m.target = 1.0f;
        m.anchored = false;
    }
    else if (cursor_up) {
        const int steps = g_wheel_steps.exchange(0, std::memory_order_relaxed);
        if (steps != 0) {
            m.target = std::clamp(m.target * std::pow(kWheelStep, static_cast<float>(steps)), kMapNear, kMapFar);
            const float px = g_wheel_x.load(std::memory_order_relaxed);
            const float py = g_wheel_y.load(std::memory_order_relaxed);
            const bool inside = px >= kLeft && px < kRight && py >= kTop && py < kBottom;
            m.anchor_x = inside ? px : kCx;
            m.anchor_y = inside ? py : kCy;
            m.anchored = true;
        }
        const float axis = pad_axis();
        if (axis != 0.0f) {
            m.target = std::clamp(m.target * std::exp(-axis * kPadRate * dt), kMapNear, kMapFar);
            m.anchor_x = kCx;
            m.anchor_y = kCy;
            m.anchored = true;
        }
    }
    else {
        g_wheel_steps.store(0, std::memory_order_relaxed);
    }

    const float before = m.z;
    m.z = ease(m.z, m.target, dt);
    if (m.anchored) {
        // The map texel under the anchor pixel stays under it.
        const float ux = m.mx + (m.anchor_x - kCx) / before, uy = m.my + (m.anchor_y - kCy) / before;
        m.mx = ux - (m.anchor_x - kCx) / m.z;
        m.my = uy - (m.anchor_y - kCy) / m.z;
        if (m.z == m.target) {
            m.anchored = false;
        }
    }
    const float hx = kWinW * 0.5f / m.z, hy = kWinH * 0.5f / m.z;
    if (m.z > 1.0f) {
        // Zoomed in: the pointer at the frame's edge pans the view. Without
        // the pointer the view follows the cursor, so the cursor stays on
        // screen. (With the pointer the cursor is always under it, and following
        // the cursor as well would pan without end at the frame's edge.)
        int px = 0, py = 0;
        if (ogre_map_cursor_pointer(&px, &py)) {
            constexpr float kEdge = 12.0f;
            constexpr float kPan = 200.0f;  // framebuffer pixels per second
            const float dx = px < kLeft + kEdge ? -1.0f : px >= kRight - kEdge ? 1.0f : 0.0f;
            const float dy = py < kTop + kEdge ? -1.0f : py >= kBottom - kEdge ? 1.0f : 0.0f;
            m.mx += dx * kPan / m.z * dt;
            m.my += dy * kPan / m.z * dt;
        }
        else if (rd8(rdram, state + ST_CURSOR_DRAW) != 0) {
            const float kx = rd16s(rdram, state + ST_CURSOR_X) - kLeft + m.scroll_x;
            const float ky = rd16s(rdram, state + ST_CURSOR_Y) - kTop + m.scroll_y;
            const float mg = 16.0f / m.z;
            m.mx = std::clamp(m.mx, kx - hx + mg, std::max(kx - hx + mg, kx + hx - mg));
            m.my = std::clamp(m.my, ky - hy + mg, std::max(ky - hy + mg, ky + hy - mg));
        }
    }
    m.mx = constrain(m.mx, hx, m.scroll_x, kWinW, kMapW);
    m.my = constrain(m.my, hy, m.scroll_y, kWinH, kMapH);
    m.active = std::fabs(m.z - 1.0f) > 0.001f || std::fabs(m.mx - game_mx) > 0.01f || std::fabs(m.my - game_my) > 0.01f;
}

// One TEXRECT (E4/E5 + E1 + F1) of the game's map layer, moved and scaled from
// game screen pixels to the zoomed picture.
void transform_rect(uint8_t* rdram, uint32_t at) {
    const uint32_t w0 = rd32(rdram, at), w1 = rd32(rdram, at + 4);
    const uint32_t st = rd32(rdram, at + 12), dd = rd32(rdram, at + 20);
    const int ds = static_cast<int16_t>(dd >> 16), dt = static_cast<int16_t>(dd & 0xFFFFu);
    if (std::abs(ds) >= 0x1000) {
        return;  // a copy-mode rectangle cannot scale
    }
    const bool flip = (w0 >> 24) == 0xE5u;
    const float lrx = ((w0 >> 12) & 0xFFFu) / 4.0f, lry = (w0 & 0xFFFu) / 4.0f;
    const float ulx = ((w1 >> 12) & 0xFFFu) / 4.0f, uly = (w1 & 0xFFFu) / 4.0f;
    const float z = g_map.z;
    const float x0 = zx(ulx), y0 = zy(uly), x1 = zx(lrx), y1 = zy(lry);
    const float nds = ds / z, ndt = dt / z;  // 5.10
    // The first pixel centre moves by (q - x0) pixels; s and t are 10.5.
    int qx0 = static_cast<int>(std::lround(x0 * 4.0f)), qy0 = static_cast<int>(std::lround(y0 * 4.0f));
    float off_x = qx0 / 4.0f - x0, off_y = qy0 / 4.0f - y0;
    if (qx0 < 0) {
        off_x -= qx0 / 4.0f;
        qx0 = 0;
    }
    if (qy0 < 0) {
        off_y -= qy0 / 4.0f;
        qy0 = 0;
    }
    float s = static_cast<int16_t>(st >> 16), t = static_cast<int16_t>(st & 0xFFFFu);
    // A flipped rectangle steps s along y and t along x.
    s += (flip ? off_y : off_x) * nds / 32.0f;
    t += (flip ? off_x : off_y) * ndt / 32.0f;
    qx0 = std::min(qx0, 0xFFF);
    qy0 = std::min(qy0, 0xFFF);
    const int qx1 = std::max(quarter(x1), qx0), qy1 = std::max(quarter(y1), qy0);
    wr32(rdram, at, (w0 & 0xFF000000u) | (static_cast<uint32_t>(qx1) << 12) | static_cast<uint32_t>(qy1));
    wr32(rdram, at + 4, (w1 & 0xFF000000u) | (static_cast<uint32_t>(qx0) << 12) | static_cast<uint32_t>(qy0));
    const int si = std::clamp(static_cast<int>(std::lround(s)), -32768, 32767);
    const int ti = std::clamp(static_cast<int>(std::lround(t)), -32768, 32767);
    wr32(rdram, at + 12, (static_cast<uint32_t>(si & 0xFFFF) << 16) | static_cast<uint32_t>(ti & 0xFFFF));
    const int dsi = std::clamp(static_cast<int>(std::lround(nds)), -32768, 32767);
    const int dti = std::clamp(static_cast<int>(std::lround(ndt)), -32768, 32767);
    wr32(rdram, at + 20, (static_cast<uint32_t>(dsi & 0xFFFF) << 16) | static_cast<uint32_t>(dti & 0xFFFF));
}

// Every TEXRECT in [from, to) of the display list.
void transform_range(uint8_t* rdram, uint32_t from, uint32_t to) {
    for (uint32_t at = from; at + 24 <= to; at += 8) {
        const uint32_t op = rd32(rdram, at) >> 24;
        if ((op == 0xE4u || op == 0xE5u) && (rd32(rdram, at + 8) >> 24) == 0xE1u &&
            (rd32(rdram, at + 16) >> 24) == 0xF1u) {
            transform_rect(rdram, at);
            at += 16;
        }
    }
}

bool map_resident(const uint8_t* rdram, uint32_t& state) {
    if (scene(rdram) != SCENE_MAP) {
        return false;
    }
    state = rd32(rdram, MAP_STATE);
    return in_ram(state) && in_ram(state + ST_WINDOW);
}

}  // namespace

bool wheel(int steps, float fb_x, float fb_y) {
    if (steps == 0 || !enabled() || (!recent(g_field_zoomable) && !recent(g_map_zoomable))) {
        return false;
    }
    g_wheel_x.store(fb_x, std::memory_order_relaxed);
    g_wheel_y.store(fb_y, std::memory_order_relaxed);
    g_wheel_steps.fetch_add(steps, std::memory_order_relaxed);
    return true;
}

bool pad(float left_trigger, float right_y) {
    if (left_trigger < 0.5f || !enabled() || (!recent(g_field_zoomable) && !recent(g_map_zoomable))) {
        return false;
    }
    constexpr float kDead = 0.25f;
    const float axis = std::fabs(right_y) < kDead ? 0.0f
                                                  : (right_y - std::copysign(kDead, right_y)) / (1.0f - kDead);
    g_pad_axis.store(std::clamp(axis, -1.0f, 1.0f), std::memory_order_relaxed);
    g_pad_at.store(now_ms(), std::memory_order_relaxed);
    return true;
}

bool map_zoomed() {
    return g_map.active && now_ms() - g_map.seen <= kSeenMs;
}

void map_unzoom(float fb_x, float fb_y, float& game_x, float& game_y) {
    const Map& m = g_map;
    game_x = (fb_x - kCx) / m.z + m.mx - m.scroll_x + kLeft;
    game_y = (fb_y - kCy) / m.z + m.my - m.scroll_y + kTop;
}

}  // namespace ogre::camera_zoom

using namespace ogre::camera_zoom;

// After the terrain loop: the game's 25 strips end at the display list's head.
// When the view differs from the game's, the strips become one call into the
// port's strips, built from the game's own first strip.
extern "C" void ogre_map_zoom_terrain(uint8_t* rdram) {
    uint32_t state = 0;
    Map& m = g_map;
    m.terrain_end = 0;
    if (rdram == nullptr || !map_resident(rdram, state)) {
        return;
    }
    update_map_view(rdram, state);
    const uint32_t head = rd32(rdram, GFX_HEAD);
    const uint32_t start = head - kStrips * kStripBytes;
    if (!in_ram(start) || !in_ram(head)) {
        return;
    }
    m.terrain_end = head;
    if (!m.active) {
        return;
    }
    // The strip is SETTIMG, SETTILE, LOADSYNC, LOADTILE (word 6/7), PIPESYNC,
    // SETTILE, SETTILESIZE (12/13), PIPESYNC, TEXRECT (16/17), E1 (19), F1 (21),
    // PIPESYNC.
    uint32_t tpl[kStripBytes / 4];
    for (uint32_t i = 0; i < kStripBytes / 4; i++) {
        tpl[i] = rd32(rdram, start + i * 4);
    }
    if ((tpl[0] >> 24) != 0xFDu || (tpl[6] >> 24) != 0xF4u || (tpl[12] >> 24) != 0xF2u || (tpl[16] >> 24) != 0xE4u ||
        (tpl[18] >> 24) != 0xE1u || (tpl[20] >> 24) != 0xF1u ||
        (rd32(rdram, head - kStripBytes + 0x40) >> 24) != 0xE4u) {
        return;  // not the layout this was written for
    }
    const uint32_t out_base = kZoomDl + static_cast<uint32_t>(m.buffer) * kZoomDlBytes;
    m.buffer = (m.buffer + 1) % kZoomDlCount;
    uint32_t out = out_base;
    const float z = m.z, hx = kWinW * 0.5f / z, hy = kWinH * 0.5f / z;
    const int u_lo = std::max(0, static_cast<int>(std::floor(m.mx - hx)));
    const int u_hi = std::min(static_cast<int>(kMapW), static_cast<int>(std::ceil(m.mx + hx)));
    const int v_lo = std::max(0, static_cast<int>(std::floor(m.my - hy)));
    const int v_hi = std::min(static_cast<int>(kMapH), static_cast<int>(std::ceil(m.my + hy)));
    const int ds = std::clamp(static_cast<int>(std::lround(1024.0f / z)), 1, 0x7FFF);
    auto sx = [&](float u) { return kCx + (u - m.mx) * z; };
    auto sy = [&](float v) { return kCy + (v - m.my) * z; };
    for (int v0 = v_lo; v0 < v_hi; v0 += 8) {
        const int v1 = std::min(v0 + 8, v_hi);
        for (int u0 = u_lo; u0 < u_hi; u0 += 256) {
            const int u1 = std::min(u0 + 256, u_hi);
            if (out + kStripBytes + 8 > out_base + kZoomDlBytes) {
                break;
            }
            uint32_t blk[kStripBytes / 4];
            std::memcpy(blk, tpl, sizeof(blk));
            const uint32_t uls = static_cast<uint32_t>(u0 * 4), ult = static_cast<uint32_t>(v0 * 4);
            const uint32_t lrs = static_cast<uint32_t>((u1 - 1) * 4), lrt = static_cast<uint32_t>((v0 + 7) * 4);
            blk[6] = 0xF4000000u | (uls << 12) | ult;
            blk[7] = (tpl[7] & 0x07000000u) | (lrs << 12) | lrt;
            blk[12] = 0xF2000000u | (uls << 12) | ult;
            blk[13] = (tpl[13] & 0x07000000u) | (lrs << 12) | lrt;
            const float x0 = sx(static_cast<float>(u0)), y0 = sy(static_cast<float>(v0));
            const int qx0 = quarter(x0), qy0 = quarter(y0);
            const int qx1 = std::max(quarter(sx(static_cast<float>(u1))), qx0);
            const int qy1 = std::max(quarter(sy(static_cast<float>(v1))), qy0);
            blk[16] = 0xE4000000u | (static_cast<uint32_t>(qx1) << 12) | static_cast<uint32_t>(qy1);
            blk[17] = (tpl[17] & 0x07000000u) | (static_cast<uint32_t>(qx0) << 12) | static_cast<uint32_t>(qy0);
            const int s = static_cast<int>(std::lround((u0 + (qx0 / 4.0f - x0) / z) * 32.0f));
            const int t = static_cast<int>(std::lround((v0 + (qy0 / 4.0f - y0) / z) * 32.0f));
            blk[19] = (static_cast<uint32_t>(s & 0xFFFF) << 16) | static_cast<uint32_t>(t & 0xFFFF);
            blk[21] = (static_cast<uint32_t>(ds) << 16) | static_cast<uint32_t>(ds);
            for (uint32_t i = 0; i < kStripBytes / 4; i++) {
                wr32(rdram, out + i * 4, blk[i]);
            }
            out += kStripBytes;
        }
    }
    wr32(rdram, out, 0xDF000000u);  // G_ENDDL
    wr32(rdram, out + 4, 0);
    // The game's strips: a call into the port's, then G_NOOPs.
    wr32(rdram, start, 0xDE000000u);
    wr32(rdram, start + 4, out_base);
    for (uint32_t at = start + 8; at < head; at += 4) {
        wr32(rdram, at, 0);
    }
}

// After func_ovlM_801A2A7C: labels, markers and the party, in game screen pixels.
extern "C" void ogre_map_zoom_sprites(uint8_t* rdram) {
    Map& m = g_map;
    if (rdram == nullptr || m.terrain_end == 0 || !m.active) {
        return;
    }
    const uint32_t head = rd32(rdram, GFX_HEAD);
    if (in_ram(head) && head > m.terrain_end) {
        transform_range(rdram, m.terrain_end, head);
    }
}

extern "C" void ogre_map_zoom_cursor_begin(uint8_t* rdram) {
    g_map.cursor_begin = rdram == nullptr ? 0 : rd32(rdram, GFX_HEAD);
}

extern "C" void ogre_map_zoom_cursor_end(uint8_t* rdram) {
    Map& m = g_map;
    if (rdram == nullptr || m.terrain_end == 0 || !m.active || !in_ram(m.cursor_begin)) {
        return;
    }
    const uint32_t head = rd32(rdram, GFX_HEAD);
    if (in_ram(head) && head > m.cursor_begin) {
        transform_range(rdram, m.cursor_begin, head);
    }
}
