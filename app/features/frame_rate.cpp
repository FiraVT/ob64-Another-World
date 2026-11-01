// Frame rate: the FRAME RATE setting applied to RT64's refresh rate.
//
// RT64 interpolates between the game's frames when its refresh rate is
// `Display` or `Manual`: the workload queue sets `targetRate` from the swap
// chain (`rt64_workload_queue.cpp`, "Find the target refresh rate"), and the
// present queue draws the extra frames by blending each matched transform
// between the previous and the current game frame (`rt64_game_frame.cpp`).
// A transform with the default id `G_EX_ID_AUTO` is matched by RT64's own
// heuristic; one with an explicit id is matched only to the same id.
//
// The mission field (scene `0x03`) is the one 3D scene the setting is for. The
// other scenes are 2D screens, njpeg backgrounds and movies, so they stay at
// the original rate.
//
// The value lives in `GraphicsConfig::rr_option`. This file is its only
// writer, and it starts from the live config so the other writers
// (`widescreen.cpp`, `settings.cpp`) keep their fields.

#include "frame_rate.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <ultramodern/config.hpp>
#include <ultramodern/ultramodern.hpp>

#include "game_host.hpp"
#include "settings.hpp"

// renderer.cpp forwards to the RT64 patch; null_renderer.cpp has a no-op.
extern "C" void ogre_renderer_set_field_tags(bool enabled);

namespace ogre {
namespace {

// Scene `0x03`, the mission field (descriptor `0x8018F350`; docs/scenes.md).
constexpr uint16_t kMissionScene = 0x0003;

// `OGRE_FIELD_TAGS=0` leaves the camera on RT64's own matching, for an A/B of
// the tags. Read once.
bool field_tags_enabled() {
    static const bool enabled = [] {
        const char* flag = std::getenv("OGRE_FIELD_TAGS");
        return flag == nullptr || flag[0] == '\0' || flag[0] != '0';
    }();
    return enabled;
}

const char* option_name(ultramodern::renderer::RefreshRate option) {
    switch (option) {
        case ultramodern::renderer::RefreshRate::Original:
            return "original";
        case ultramodern::renderer::RefreshRate::Display:
            return "display";
        case ultramodern::renderer::RefreshRate::Manual:
            return "manual";
        default:
            return "?";
    }
}

bool g_tags_on = false;

}  // namespace

void frame_rate_update() {
    const uint16_t scene = host::scene();
    const bool high = frame_rate_mode() == FrameRateMode::Display && scene == kMissionScene;

    const bool tags = high && field_tags_enabled();
    if (tags != g_tags_on) {
        g_tags_on = tags;
        ogre_renderer_set_field_tags(tags);
    }

    const ultramodern::renderer::RefreshRate wanted =
        high ? ultramodern::renderer::RefreshRate::Display
             : ultramodern::renderer::RefreshRate::Original;
    const ultramodern::renderer::GraphicsConfig config =
        ultramodern::renderer::get_graphics_config();
    if (config.rr_option == wanted) {
        return;
    }

    ultramodern::renderer::GraphicsConfig next = config;
    next.rr_option = wanted;
    ultramodern::renderer::set_graphics_config(next);
    std::fprintf(stderr, "[frame-rate] refresh rate %s (scene 0x%04X, field tags %s)\n",
                 option_name(wanted), scene, tags ? "on" : "off");
    std::fflush(stderr);
}

}  // namespace ogre

