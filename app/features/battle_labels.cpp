// Battle labels: a word or an image the port draws over a fighter during the
// battle's playback. A damage over time tick (battle_dot.cpp) registers its
// effect's label with add() and schedules event 0x36 with kind kLabelKind and
// the label's number. The playback's case 0x36 calls the battle effect routine
// func_ovlJ_801E34F8(fighter, kind, value) (LordlyCaliber func_002267A8, from
// func_0021EBBC case 0x36); config/hooks.toml calls ogre_battle_label at its
// entry, which takes a label kind, queues the label at the fighter's screen
// position and returns before the routine makes a game effect. The playback
// reaches the event at the tick's own time, so the label shows with its number.
//
// The screen position is func_ovlC_801C9048(x, y, z, &sx, &sy) on the
// fighter's world position (+0x5C, +0x60, +0x64): a pixel of the 320x240
// picture, as the playback's case 0x32 places a number (LordlyCaliber
// func_0021EBBC, func_0020C4B8). The view maps it to the window as
// field_view.cpp does: the picture is aspect-fit and centered.

#include "battle_labels.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#if OGRE_HAVE_RMLUI
#include <RmlUi/Core.h>
#endif

#include "recomp.h"

namespace {

constexpr int MAX_LABELS = 256;  // per fight
constexpr uint32_t WORLD_TO_SCREEN = 0x801C9048u;  // func_ovlC_801C9048(x, y, z, &sx, &sy)
constexpr uint32_t P_X = 0x5Cu;
constexpr uint32_t P_Y = 0x60u;
constexpr uint32_t P_Z = 0x64u;
constexpr double SHOW_SECONDS = 1.2;
constexpr float RISE = 18.0f;  // picture pixels over the label's time
constexpr float ABOVE = 30.0f;  // picture pixels above the fighter's feet

struct Label {
    std::string text;
    uint32_t rgb;
    std::string image;
    int width = 0;  // the image's size in picture pixels (its PNG header)
    int height = 0;
};

// A PNG's width and height from its IHDR chunk (bytes 16..23, big-endian);
// false for a file that is not a PNG.
bool png_size(const std::string& path, int& width, int& height) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
        return false;
    }
    unsigned char head[24] = {};
    const bool read = std::fread(head, 1, sizeof(head), f) == sizeof(head);
    std::fclose(f);
    if (!read || head[1] != 'P' || head[2] != 'N' || head[3] != 'G') {
        return false;
    }
    width = head[16] << 24 | head[17] << 16 | head[18] << 8 | head[19];
    height = head[20] << 24 | head[21] << 16 | head[22] << 8 | head[23];
    return width > 0 && height > 0 && width <= 512 && height <= 512;
}

struct Shown {
    size_t label;
    float x;  // picture pixels
    float y;
    std::chrono::steady_clock::time_point start;
};

std::mutex g_mutex;
std::vector<Label> g_labels;  // game thread writes during the compute pass; read at playback
std::vector<Shown> g_shown;   // playback (game thread) adds; the view (main thread) draws

#if OGRE_HAVE_RMLUI
Rml::ElementDocument* g_document = nullptr;
Rml::Element* g_layer = nullptr;
std::string g_layer_rml;
#endif

// OGRE_DOT_TRACE=1 also logs each label the playback reaches and draws.
bool tracing() {
    static const bool on = [] {
        const char* value = std::getenv("OGRE_DOT_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return on;
}

int32_t rd32(uint8_t* rdram, uint32_t address) {
    return MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address)));
}

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

// The fighter's position in the 320x240 picture.
bool screen_of(uint8_t* rdram, recomp_context* ctx, uint32_t fighter, float& x, float& y) {
    if (fighter < 0x80000000u || fighter >= 0x80800000u) {
        return false;
    }
    const recomp_context saved = *ctx;
    const uint32_t frame = static_cast<uint32_t>(ctx->r29) - 0x40u;
    const uint32_t out_x = frame + 0x20u;
    const uint32_t out_y = frame + 0x24u;
    MEM_W(0, static_cast<gpr>(static_cast<int32_t>(out_x))) = 0;
    MEM_W(0, static_cast<gpr>(static_cast<int32_t>(out_y))) = 0;
    MEM_W(0x10, static_cast<gpr>(static_cast<int32_t>(frame))) = static_cast<int32_t>(out_y);
    set_arg(ctx->r29, frame);
    set_arg(ctx->r4, static_cast<uint32_t>(rd32(rdram, fighter + P_X)));
    set_arg(ctx->r5, static_cast<uint32_t>(rd32(rdram, fighter + P_Y)));
    set_arg(ctx->r6, static_cast<uint32_t>(rd32(rdram, fighter + P_Z)));
    set_arg(ctx->r7, out_x);
    LOOKUP_FUNC(WORLD_TO_SCREEN)(rdram, ctx);
    x = static_cast<float>(rd32(rdram, out_x));
    y = static_cast<float>(rd32(rdram, out_y));
    *ctx = saved;
    return x > -64.0f && x < 384.0f && y > -64.0f && y < 304.0f;
}

[[maybe_unused]] std::string hex_color(uint32_t rgb, int alpha) {
    char text[16];
    std::snprintf(text, sizeof(text), "#%06X%02X", rgb & 0xFFFFFFu, std::clamp(alpha, 0, 255));
    return text;
}

}  // namespace

namespace ogre::battle_labels {

void new_fight() {
    std::lock_guard lock(g_mutex);
    g_labels.clear();
}

int add(const std::string& text, uint32_t rgb, const std::string& image) {
    std::lock_guard lock(g_mutex);
    for (size_t i = 0; i < g_labels.size(); i++) {
        if (g_labels[i].text == text && g_labels[i].rgb == rgb && g_labels[i].image == image) {
            return static_cast<int>(i);
        }
    }
    if (g_labels.size() >= MAX_LABELS) {
        return -1;
    }
    // An image that is not a readable PNG falls back to the text.
    Label label{text, rgb, image};
    if (!image.empty() && !png_size(image, label.width, label.height)) {
        std::fprintf(stderr, "[battle-labels] %s is not a PNG it can read; the label shows its text\n", image.c_str());
        label.image.clear();
    }
    g_labels.push_back(label);
    return static_cast<int>(g_labels.size() - 1);
}

#if OGRE_HAVE_RMLUI
bool init(Rml::Context* context, const std::filesystem::path& asset_dir) {
    g_document = context->LoadDocument((asset_dir / "battle_labels.rml").string());
    if (g_document == nullptr) {
        std::fprintf(stderr, "[battle-labels] could not load battle_labels.rml\n");
        return false;
    }
    g_layer = g_document->GetElementById("labels");
    g_document->Show();
    return g_layer != nullptr;
}

void update() {
    if (g_layer == nullptr) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const Rml::Vector2i size = g_document->GetContext()->GetDimensions();
    const float scale = std::min(size.x / 320.0f, size.y / 240.0f);
    const float ox = (size.x - 320.0f * scale) * 0.5f;
    const float oy = (size.y - 240.0f * scale) * 0.5f;
    std::string rml;
    {
        std::lock_guard lock(g_mutex);
        g_shown.erase(std::remove_if(g_shown.begin(), g_shown.end(),
                                     [&](const Shown& s) {
                                         return std::chrono::duration<double>(now - s.start).count() > SHOW_SECONDS ||
                                                s.label >= g_labels.size();
                                     }),
                      g_shown.end());
        for (const Shown& s : g_shown) {
            const Label& label = g_labels[s.label];
            const float t = static_cast<float>(std::chrono::duration<double>(now - s.start).count() / SHOW_SECONDS);
            const int alpha = static_cast<int>(255.0f * std::min(1.0f, 3.0f * (1.0f - t)));
            const int center = static_cast<int>(ox + s.x * scale);
            const int top = static_cast<int>(oy + (s.y - ABOVE - RISE * t) * scale);
            // The render interface has no transforms (organize.rcss), so the
            // size and the centering are worked out here.
            if (!label.image.empty()) {
                // Pixel art at whole picture pixels: the image's own size times
                // the scale (the texture is enlarged with nearest sampling,
                // rml_ui.cpp `pixel:`), its center over the fighter.
                const int w = static_cast<int>(label.width * scale);
                const int h = static_cast<int>(label.height * scale);
                rml += "<img class=\"art\" src=\"pixel:" + label.image + "\" style=\"left: " +
                       std::to_string(center - w / 2) + "px; top: " + std::to_string(top) + "px; width: " +
                       std::to_string(w) + "px; height: " + std::to_string(h) +
                       "px; opacity: " + std::to_string(alpha / 255.0f) + ";\"/>";
            } else {
                const int box = static_cast<int>(160.0f * scale);
                rml += "<div class=\"word\" style=\"left: " + std::to_string(center - box / 2) + "px; top: " +
                       std::to_string(top) + "px; width: " + std::to_string(box) + "px; color: " +
                       hex_color(label.rgb, alpha) + "; font-size: " + std::to_string(static_cast<int>(9.0f * scale)) +
                       "px;\">" + label.text + "</div>";
            }
        }
    }
    if (rml != g_layer_rml) {
        if (tracing() && !rml.empty() && g_layer_rml.empty()) {
            std::fprintf(stderr, "[battle-labels] drawing: %s\n", rml.substr(0, 240).c_str());
        }
        g_layer_rml = rml;
        g_layer->SetInnerRML(rml);
    }
}
#endif

}  // namespace ogre::battle_labels

// The battle effect routine's entry (config/hooks.toml, 801E34F8): true when
// `kind` is a label, which this queues; the routine then returns at once.
extern "C" int ogre_battle_label(uint8_t* rdram, recomp_context* ctx, uint32_t fighter, uint32_t kind,
                                 uint32_t value) {
    if ((kind & 0xFFu) != ogre::battle_labels::kLabelKind) {
        return 0;
    }
    float x = 0.0f;
    float y = 0.0f;
    const bool on_screen = screen_of(rdram, ctx, fighter, x, y);
    std::lock_guard lock(g_mutex);
    const bool known = value < g_labels.size();
    if (on_screen && known) {
        g_shown.push_back({value, x, y, std::chrono::steady_clock::now()});
    }
    if (tracing()) {
        std::fprintf(stderr, "[battle-labels] label %u on %08X at picture %.0f,%.0f: %s\n", value, fighter, x, y,
                     !known ? "unknown label" : on_screen ? "shown" : "off the picture");
    }
    return 1;
}
