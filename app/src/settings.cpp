// Player-editable app settings (see settings.hpp).

#include "settings.hpp"

#include "launcher.hpp"
#include "map_cursor.hpp"
#include "mission_cursor.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <ultramodern/config.hpp>
#include <ultramodern/ultramodern.hpp>

namespace ogre {
namespace {

// The GAME SPEED radio group. The values are a subset of what the port's guides
// use for `OGRE_SPEED`. 6 and 8 were dropped in session 93 because the game's
// own cursor aiming cannot keep up at those speeds; 8 is back at the
// developer's request (session 137), and the speed hotkeys
// (`step_game_speed`) walk this same list.
const int kGameSpeeds[kGameSpeedCount] = {1, 2, 4, 8};

// The config directory the file is written to; empty until load_settings runs,
// in which case a change still applies to the runtime but is not persisted.
std::filesystem::path g_pref_dir;

// The live values. Only the main thread (the launcher loop and the overlay's
// update_gfx callback) reads and writes them, so they need no lock.
int g_game_speed = 1;
WidescreenMode g_widescreen = WidescreenMode::Off;
FrameRateMode g_frame_rate = FrameRateMode::Original;
ResolutionMode g_resolution = ResolutionMode::Native;
AntialiasMode g_antialias = AntialiasMode::Off;
DisplayMode g_display = DisplayMode::Windowed;
SoundMode g_sound = SoundMode::On;
int g_volume = kVolumeDefaultPercent;
// MUSIC and EFFECTS, 0..100: libmus's two master volumes (see
// `apply_mix_volumes`).
int g_music = kVolumeDefaultPercent;
int g_effects = kVolumeDefaultPercent;
MouseMode g_mouse = MouseMode::On;


// Defined below, after the gate that decides whether the two experimental values
// are applied at all.
ResolutionMode applied_resolution();
AntialiasMode applied_antialias();
// The settings.cfg spelling of a mode (stable across ASPECT RATIO).
std::string resolution_key(ResolutionMode mode) {
    switch (mode) {
        case ResolutionMode::P480:  return "480p";
        case ResolutionMode::P576:  return "576p";
        case ResolutionMode::P720:  return "720p";
        case ResolutionMode::P1080: return "1080p";
        case ResolutionMode::P1440: return "1440p";
        case ResolutionMode::P2160: return "4k";
        case ResolutionMode::P4320: return "8k";
        case ResolutionMode::Auto:  return "auto";
        default:                    return "native";
    }
}


void publish_graphics() {
    const ultramodern::renderer::GraphicsConfig config =
        ultramodern::renderer::get_graphics_config();
    ultramodern::renderer::GraphicsConfig next = config;

    switch (applied_resolution()) {
        case ResolutionMode::Native:
            next.res_option = ultramodern::renderer::Resolution::Original;
            break;
        case ResolutionMode::Auto:
            next.res_option = ultramodern::renderer::Resolution::Auto;
            break;
        default:
            // A fixed multiple: RT64's Manual mode, the multiplier carried in
            // tenths so 2.4x (576) and 4.5x (1080) fit an int (renderer.cpp).
            next.res_option = ultramodern::renderer::Resolution::Original2x;
            next.ds_option = static_cast<int>(resolution_multiplier(applied_resolution()) * 10.0 + 0.5);
            break;
    }

    switch (applied_antialias()) {
        case AntialiasMode::Off:
            next.msaa_option = ultramodern::renderer::Antialiasing::None;
            break;
        case AntialiasMode::Msaa2X:
            next.msaa_option = ultramodern::renderer::Antialiasing::MSAA2X;
            break;
        case AntialiasMode::Msaa4X:
            next.msaa_option = ultramodern::renderer::Antialiasing::MSAA4X;
            break;
        case AntialiasMode::Msaa8X:
            next.msaa_option = ultramodern::renderer::Antialiasing::MSAA8X;
            break;
    }

    next.wm_option = g_display == DisplayMode::Fullscreen
                         ? ultramodern::renderer::WindowMode::Fullscreen
                         : ultramodern::renderer::WindowMode::Windowed;

    // `GraphicsConfig` compares all of its fields, so an unchanged publish is a
    // no-op and does not enqueue a config action.
    if (!(next == config)) {
        ultramodern::renderer::set_graphics_config(next);
    }
}

// The audio callbacks' gain, derived from the two values above. The game's
// audio thread reads it through `audio_gain_percent()` while the main thread
// writes it, hence the atomic.
std::atomic<int> g_audio_gain{kVolumeDefaultPercent};

void publish_audio_gain() {
    g_audio_gain.store(g_sound == SoundMode::On ? g_volume : 0, std::memory_order_relaxed);
}

// Publishes MOUSE ENABLED to the two cursor modules. Both read the value on
// their next frame, so a change under the Esc overlay takes effect without a
// restart; `enabled()` in each module still lets `OGRE_MAP_CURSOR` /
// `OGRE_MISSION_CURSOR` override it for one run.
void publish_mouse() {
    const bool on = g_mouse == MouseMode::On;
    ogre_map_cursor_set_enabled(on);
    ogre_mission_cursor_set_enabled(on);
}

// `OGRE_SPEED`'s value, or 0 when unset or unparsable. The runtime reads the
// same variable itself for its initial value, so this is only used to decide
// precedence and to show the value the run started with.
int env_game_speed() {
    const char* env = std::getenv("OGRE_SPEED");
    if (env == nullptr || env[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const long value = std::strtol(env, &end, 10);
    if (end == env || value < 1 || value > 64) {
        return 0;
    }
    return static_cast<int>(value);
}

std::string lowercase(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return text;
}

// Whether RESOLUTION and MSAA are live this run. A player run showed visual
// artifacts with them (developer, session 121) that are not characterised yet,
// so the rows say EXPERIMENTAL, and both default to native/off in the saved
// settings. They are shown unless `OGRE_DISPLAY_EXPERIMENTS=0` hides them. One
// value per process, so it is read once.
bool experiments_enabled() {
    static const bool enabled = [] {
        const char* flag = std::getenv("OGRE_DISPLAY_EXPERIMENTS");
        if (flag != nullptr && flag[0] != '\0') {
            const std::string text = lowercase(flag);
            return !(text == "0" || text == "off" || text == "false" || text == "no");
        }
        return true;
    }();
    return enabled;
}

// The mode RESOLUTION is in this run: the configured one when the experiments are
// on, `Native` otherwise.
ResolutionMode applied_resolution() {
    return experiments_enabled() ? g_resolution : ResolutionMode::Native;
}

AntialiasMode applied_antialias() {
    return experiments_enabled() ? g_antialias : AntialiasMode::Off;
}

// The three DISPLAY values above live in the runtime's `GraphicsConfig` beside
// the aspect ratio `widescreen.cpp` owns. Every writer starts from the live
// config and changes only its own fields, so the two do not overwrite each
// other. The runtime applies a change through `RT64Renderer::update_config`; the
// call before the renderer exists still counts, because the renderer is created
// from this config (`renderer.cpp`, `create_render_context`).

std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

// The value of `wanted` in a `key = value` file: comments start at `#`, the key
// is matched case-insensitively after trimming, and the first match wins.
std::string setting_value(const std::string& text, const char* wanted) {
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t end = text.find('\n', pos);
        std::string line = text.substr(pos, end == std::string::npos ? std::string::npos
                                                                    : end - pos);
        const size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        const size_t equals = line.find('=');
        if (equals != std::string::npos &&
            lowercase(trim(line.substr(0, equals))) == wanted) {
            return trim(line.substr(equals + 1));
        }
        if (end == std::string::npos) {
            break;
        }
        pos = end + 1;
    }
    return {};
}

// `game_speed = <n>`; a bare integer on its own line is accepted too, so a file
// edited by hand before WIDESCREEN existed keeps working.
int parse_game_speed(const std::string& text) {
    std::string value = setting_value(text, "game_speed");
    if (value.empty()) {
        size_t pos = 0;
        while (pos <= text.size()) {
            const size_t end = text.find('\n', pos);
            std::string line = text.substr(pos, end == std::string::npos ? std::string::npos
                                                                        : end - pos);
            const size_t hash = line.find('#');
            if (hash != std::string::npos) {
                line = line.substr(0, hash);
            }
            value = trim(line);
            if (!value.empty() && value.find('=') == std::string::npos) {
                break;
            }
            if (end == std::string::npos) {
                return 1;
            }
            pos = end + 1;
        }
    }
    char* value_end = nullptr;
    const long parsed = std::strtol(value.c_str(), &value_end, 10);
    if (value_end != value.c_str() && parsed >= 1 && parsed <= 64) {
        return static_cast<int>(parsed);
    }
    return 1;
}

// `off` / `on`, or 0 / 1. The retired `missions` / `always` spellings were the
// two on states of the old three-option row; both are read as `on`.
WidescreenMode parse_widescreen(const std::string& text) {
    const std::string value = lowercase(setting_value(text, "widescreen"));
    if (value == "16:10") {
        return WidescreenMode::W16x10;
    }
    if (value == "21:9") {
        return WidescreenMode::W21x9;
    }
    if (value == "on" || value == "16:9" || value == "1" || value == "true" || value == "missions" ||
        value == "mission" || value == "always" || value == "2") {
        return WidescreenMode::On;
    }
    return WidescreenMode::Off;
}

// `frame_rate = original|display`. A missing or unparsable value is the
// default, `original`.
FrameRateMode parse_frame_rate(const std::string& text) {
    const std::string value = lowercase(setting_value(text, "frame_rate"));
    if (value == "display" || value == "high" || value == "1" || value == "on") {
        return FrameRateMode::Display;
    }
    return FrameRateMode::Original;
}

// `sounds = off` / `on`, or 0 / 1. A file written before the row existed has no
// key, and the value is then the default `on`; a typo is read as `on` too,
// because silently starting muted is worse than ignoring a bad spelling.
SoundMode parse_sound(const std::string& text) {
    const std::string value = lowercase(setting_value(text, "sounds"));
    if (value == "off" || value == "0" || value == "false" || value == "no") {
        return SoundMode::Off;
    }
    return SoundMode::On;
}

// `resolution = native|2x|auto`. A missing or unparsable value is the default,
// `native`, which is what every build before the row drew.
ResolutionMode parse_resolution(const std::string& text) {
    const std::string value = lowercase(setting_value(text, "resolution"));
    if (value == "2x" || value == "2" || value == "double" || value == "480p") {
        return ResolutionMode::P480;
    }
    if (value.rfind("576", 0) == 0) {
        return ResolutionMode::P576;
    }
    if (value.rfind("8k", 0) == 0 || value == "4320p") {
        return ResolutionMode::P4320;
    }
    if (value.rfind("4k", 0) == 0 || value.find("2160") != std::string::npos) {
        return ResolutionMode::P2160;
    }
    if (value.find("1440") != std::string::npos) {
        return ResolutionMode::P1440;
    }
    if (value.find("1080") != std::string::npos) {
        return ResolutionMode::P1080;
    }
    if (value.find("720") != std::string::npos) {
        return ResolutionMode::P720;
    }
    if (value.find("480") != std::string::npos) {
        return ResolutionMode::P480;
    }
    if (value == "1080p") {
        return ResolutionMode::P1080;
    }
    if (value == "1440p") {
        return ResolutionMode::P1440;
    }
    if (value == "4k" || value == "2160p") {
        return ResolutionMode::P2160;
    }
    if (value == "auto" || value == "window" || value == "integer") {
        return ResolutionMode::Auto;
    }
    return ResolutionMode::Native;
}

// `msaa = off|2x|4x|8x`. A missing or unparsable value is the default, `off`.
AntialiasMode parse_antialias(const std::string& text) {
    const std::string value = lowercase(setting_value(text, "msaa"));
    if (value == "2x" || value == "2") {
        return AntialiasMode::Msaa2X;
    }
    if (value == "4x" || value == "4") {
        return AntialiasMode::Msaa4X;
    }
    if (value == "8x" || value == "8") {
        return AntialiasMode::Msaa8X;
    }
    return AntialiasMode::Off;
}

// `window = windowed|borderless fullscreen` (older files say `fullscreen`). A
// missing or unparsable value is the default, `windowed`.
DisplayMode parse_display(const std::string& text) {
    const std::string value = lowercase(setting_value(text, "window"));
    if (value == "borderless fullscreen" || value == "borderless" || value == "fullscreen" || value == "full" ||
        value == "1" || value == "true") {
        return DisplayMode::Fullscreen;
    }
    return DisplayMode::Windowed;
}

// `mouse = off` / `on`, or 0 / 1. A missing or unparsable value is the default
// `on` (the maintainer, 2026-11-01): a file written before the row existed has
// no key and loads with the mouse enabled.
MouseMode parse_mouse(const std::string& text) {
    const std::string value = lowercase(setting_value(text, "mouse"));
    if (value == "off" || value == "0" || value == "false" || value == "no") {
        return MouseMode::Off;
    }
    return MouseMode::On;
}

// `volume = <n>` in percent, clamped to 0..100. A missing or unparsable value
// is the default, 100.
int parse_volume(const std::string& text, const char* key = "volume") {
    const std::string value = setting_value(text, key);
    char* value_end = nullptr;
    const long parsed = std::strtol(value.c_str(), &value_end, 10);
    if (value_end == value.c_str()) {
        return kVolumeDefaultPercent;
    }
    return static_cast<int>(std::clamp(parsed, 0L, 100L));
}

std::string read_file(const std::filesystem::path& path, bool& ok) {
    FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        ok = false;
        return {};
    }
    std::string text;
    char buffer[512];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, got);
    }
    std::fclose(file);
    ok = true;
    return text;
}

void write_settings() {
    if (g_pref_dir.empty()) {
        return;
    }
    const std::filesystem::path path = settings_path(g_pref_dir);
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        std::fprintf(stderr, "[settings] could not write %s\n", path.string().c_str());
        return;
    }
    std::string text =
        "# OB64: Another World settings.\n"
        "# Edited from the launcher's SETTINGS tab, or the in-game Esc overlay.\n"
        "# game_speed: the emulated clock's multiplier (1, 2, 4 or 8).\n"
        "game_speed = " + std::to_string(g_game_speed) + "\n"
        "# widescreen: off or on.\n"
        "widescreen = " + lowercase(widescreen_mode_label(static_cast<int>(g_widescreen))) + "\n"
        "# frame_rate: original or display. display interpolates the mission field\n"
        "# up to the display's refresh rate.\n"
        "frame_rate = " + lowercase(frame_rate_mode_label(static_cast<int>(g_frame_rate))) + "\n"
        "# resolution: native, 2x or auto.\n"
        "resolution = " + resolution_key(g_resolution) + "\n"
        "# msaa: off, 2x, 4x or 8x.\n"
        "msaa = " + lowercase(antialias_mode_label(static_cast<int>(g_antialias))) + "\n"
        "# window: windowed or fullscreen.\n"
        "window = " + lowercase(display_mode_label(static_cast<int>(g_display))) + "\n"
        "# sounds: off or on.\n"
        "sounds = " + lowercase(sound_mode_label(static_cast<int>(g_sound))) + "\n"
        "# volume: the output gain in percent (0..100).\n"
        "volume = " + std::to_string(g_volume) + "\n"
        "# music and effects: the game's own song and sound-effect volumes in percent (0..100).\n"
        "music = " + std::to_string(g_music) + "\n"
        "effects = " + std::to_string(g_effects) + "\n"
        "# mouse: off or on. on lets the pointer drive the world map's and the\n"
        "# mission field's cursors, including the click and the edge scroll.\n"
        "mouse = " + lowercase(mouse_mode_label(static_cast<int>(g_mouse))) + "\n";
    // This project's options (app/features/options.hpp).
    text += options::settings_lines();
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
}

}  // namespace

int game_speed_value(int index) {
    if (index < 0 || index >= kGameSpeedCount) {
        return kGameSpeeds[0];
    }
    return kGameSpeeds[index];
}

int game_speed_index(int value) {
    for (int i = 0; i < kGameSpeedCount; i++) {
        if (kGameSpeeds[i] == value) {
            return i;
        }
    }
    return -1;
}

int nearest_game_speed(int value) {
    int best = kGameSpeeds[0];
    int best_distance = -1;
    for (int i = 0; i < kGameSpeedCount; i++) {
        const int distance = kGameSpeeds[i] > value ? kGameSpeeds[i] - value : value - kGameSpeeds[i];
        if (best_distance < 0 || distance < best_distance) {
            best = kGameSpeeds[i];
            best_distance = distance;
        }
    }
    return best;
}

int game_speed() {
    return g_game_speed;
}

void apply_scene_speed(int scene, bool fast_forward) {
    static uint32_t applied = 0;
    static const uint32_t kBattle[4] = {0, 1, 2, 4};
    const int battle = choice_index(Choice::BattleSpeed);
    uint32_t want = static_cast<uint32_t>(g_game_speed);
    if (scene == 0x0D && battle > 0 && battle < 4) {
        want = kBattle[battle];
    }
    if (fast_forward) {
        want = std::max<uint32_t>(want, 8u);
    }
    // Set every frame: fast-forward's release and GAME SPEED changes write the
    // runtime's multiplier themselves, so a cached value could go stale.
    ultramodern::set_speed_multiplier(want);
    if (want != applied) {
        applied = want;
        std::fprintf(stderr, "[settings] speed x%u (%s)\n", want, scene == 0x0D ? "battle" : "field");
    }
}

void set_game_speed(int multiplier) {
    if (multiplier < 1) {
        multiplier = 1;
    }
    if (multiplier > 64) {
        multiplier = 64;
    }
    if (multiplier == g_game_speed) {
        return;
    }
    g_game_speed = multiplier;
    ultramodern::set_speed_multiplier(static_cast<uint32_t>(multiplier));
    // One line per change, so a run's log says what the keys and the panel did.
    std::fprintf(stderr, "[settings] game speed x%d\n", multiplier);
    fflush(stderr);
    write_settings();
}

bool step_game_speed(int direction) {
    if (direction == 0) {
        return false;
    }
    // The next offered value in that direction, compared numerically rather
    // than by index: `OGRE_SPEED` can start a run on a value the row does not
    // offer (3, 6, 64), and stepping from one of those must still reach the
    // neighbouring offered value.
    int next = g_game_speed;
    if (direction > 0) {
        for (int i = 0; i < kGameSpeedCount; i++) {
            if (kGameSpeeds[i] > g_game_speed) {
                next = kGameSpeeds[i];
                break;
            }
        }
    }
    else {
        for (int i = kGameSpeedCount - 1; i >= 0; i--) {
            if (kGameSpeeds[i] < g_game_speed) {
                next = kGameSpeeds[i];
                break;
            }
        }
    }
    if (next == g_game_speed) {
        // Already at the end of the list; the key does nothing there.
        return false;
    }
    set_game_speed(next);
    return true;
}

bool cycle_game_speed() {
    // The pad's one button (R3) has no direction, so it walks the table and
    // wraps: 1 -> 2 -> 4 -> 8 -> 1. A run started on a value the table does not
    // offer joins at the first offered value above it (6 -> 8), or at 1 when
    // there is none (64 -> 1).
    const int index = game_speed_index(g_game_speed);
    int next = kGameSpeeds[0];
    if (index >= 0) {
        next = kGameSpeeds[(index + 1) % kGameSpeedCount];
    }
    else {
        for (int i = 0; i < kGameSpeedCount; i++) {
            if (kGameSpeeds[i] > g_game_speed) {
                next = kGameSpeeds[i];
                break;
            }
        }
    }
    if (next == g_game_speed) {
        // Only reachable if the table were a single value.
        return false;
    }
    set_game_speed(next);
    return true;
}

const char* widescreen_mode_label(int index) {
    switch (static_cast<WidescreenMode>(index)) {
        case WidescreenMode::Off:    return "4:3";
        case WidescreenMode::On:     return "16:9";
        case WidescreenMode::W16x10: return "16:10";
        case WidescreenMode::W21x9:  return "21:9";
    }
    return "OFF";
}

WidescreenMode widescreen_mode() {
    return g_widescreen;
}

int widescreen_mode_index() {
    const int index = static_cast<int>(g_widescreen);
    return index >= 0 && index < kWidescreenModeCount ? index : -1;
}

void set_widescreen_mode(WidescreenMode mode) {
    if (mode == g_widescreen) {
        return;
    }
    g_widescreen = mode;
    write_settings();
}

std::string widescreen_mode_text() {
    std::string text;
    for (int i = 0; i < kWidescreenModeCount; i++) {
        if (!text.empty()) {
            text += "  ";
        }
        text += std::string(i == static_cast<int>(g_widescreen) ? "[x] " : "[ ] ") +
                widescreen_mode_label(i);
    }
    return text;
}

namespace {

// The options of a radio row as one string, with the live option marked.
std::string radio_text(const char* (*label)(int), int count, int current) {
    std::string text;
    for (int i = 0; i < count; i++) {
        if (!text.empty()) {
            text += "  ";
        }
        text += std::string(i == current ? "[x] " : "[ ] ") + label(i);
    }
    return text;
}

}  // namespace

const char* frame_rate_mode_label(int index) {
    switch (static_cast<FrameRateMode>(index)) {
        case FrameRateMode::Original: return "ORIGINAL";
        case FrameRateMode::Display:  return "DISPLAY";
    }
    return "ORIGINAL";
}

FrameRateMode frame_rate_mode() {
    return g_frame_rate;
}

int frame_rate_mode_index() {
    const int index = static_cast<int>(g_frame_rate);
    return index >= 0 && index < kFrameRateModeCount ? index : -1;
}

void set_frame_rate_mode(FrameRateMode mode) {
    if (mode == g_frame_rate) {
        return;
    }
    g_frame_rate = mode;
    write_settings();
}

std::string frame_rate_mode_text() {
    return radio_text(frame_rate_mode_label, kFrameRateModeCount, frame_rate_mode_index());
}

const char* resolution_mode_label(int index) {
    switch (static_cast<ResolutionMode>(index)) {
        case ResolutionMode::Native: return "NATIVE";
        case ResolutionMode::Auto:   return "AUTO";
        default: {
            // The real output size for the chosen ASPECT RATIO: 240 lines times
            // the multiplier, and that height times the aspect (even widths).
            static std::string labels[kResolutionModeCount];
            const ResolutionMode mode = static_cast<ResolutionMode>(index);
            const int height = static_cast<int>(240.0 * resolution_multiplier(mode) + 0.5);
            int width = static_cast<int>(height * aspect_of(g_widescreen) + 0.5);
            width += width & 1;
            std::string label = std::to_string(width) + "X" + std::to_string(height);
            if (mode == ResolutionMode::P2160) {
                label = "4K " + label;
            }
            else if (mode == ResolutionMode::P4320) {
                label = "8K " + label;
            }
            labels[index] = label;
            return labels[index].c_str();
        }
    }
    return "NATIVE";
}

float aspect_of(WidescreenMode mode) {
    switch (mode) {
        case WidescreenMode::On:     return 16.0f / 9.0f;
        case WidescreenMode::W16x10: return 16.0f / 10.0f;
        case WidescreenMode::W21x9:  return 21.0f / 9.0f;
        default:                     return 4.0f / 3.0f;
    }
}

double resolution_multiplier(ResolutionMode mode) {
    switch (mode) {
        case ResolutionMode::P480:  return 2.0;
        case ResolutionMode::P576:  return 2.4;
        case ResolutionMode::P720:  return 3.0;
        case ResolutionMode::P1080: return 4.5;
        case ResolutionMode::P1440: return 6.0;
        case ResolutionMode::P2160: return 9.0;
        case ResolutionMode::P4320: return 18.0;
        default:                    return 1.0;
    }
}

bool display_experiments_enabled() {
    return experiments_enabled();
}

ResolutionMode resolution_mode() {
    return applied_resolution();
}

int resolution_mode_index() {
    const int index = static_cast<int>(g_resolution);
    return index >= 0 && index < kResolutionModeCount ? index : -1;
}

void set_resolution_mode(ResolutionMode mode) {
    if (mode == g_resolution) {
        return;
    }
    g_resolution = mode;
    publish_graphics();
    write_settings();
}

std::string resolution_mode_text() {
    return radio_text(resolution_mode_label, kResolutionModeCount, resolution_mode_index());
}

const char* antialias_mode_label(int index) {
    switch (static_cast<AntialiasMode>(index)) {
        case AntialiasMode::Off:    return "OFF";
        case AntialiasMode::Msaa2X: return "2X";
        case AntialiasMode::Msaa4X: return "4X";
        case AntialiasMode::Msaa8X: return "8X";
    }
    return "OFF";
}

AntialiasMode antialias_mode() {
    return applied_antialias();
}

int antialias_mode_index() {
    const int index = static_cast<int>(g_antialias);
    return index >= 0 && index < kAntialiasModeCount ? index : -1;
}

void set_antialias_mode(AntialiasMode mode) {
    if (mode == g_antialias) {
        return;
    }
    g_antialias = mode;
    publish_graphics();
    write_settings();
}

std::string antialias_mode_text() {
    return radio_text(antialias_mode_label, kAntialiasModeCount, antialias_mode_index());
}

const char* display_mode_label(int index) {
    switch (static_cast<DisplayMode>(index)) {
        case DisplayMode::Windowed:   return "WINDOWED";
        // RT64's full screen is a borderless window over the monitor (WS_POPUP in
        // rt64_application_window.cpp), so the row names it that way.
        case DisplayMode::Fullscreen: return "BORDERLESS FULLSCREEN";
    }
    return "WINDOWED";
}

DisplayMode display_mode() {
    return g_display;
}

int display_mode_index() {
    const int index = static_cast<int>(g_display);
    return index >= 0 && index < kDisplayModeCount ? index : -1;
}

void set_display_mode(DisplayMode mode) {
    if (mode == g_display) {
        return;
    }
    g_display = mode;
    publish_graphics();
    write_settings();
}

std::string display_mode_text() {
    return radio_text(display_mode_label, kDisplayModeCount, display_mode_index());
}

const char* sound_mode_label(int index) {
    switch (static_cast<SoundMode>(index)) {
        case SoundMode::Off: return "OFF";
        case SoundMode::On:  return "ON";
    }
    return "ON";
}

SoundMode sound_mode() {
    return g_sound;
}

int sound_mode_index() {
    const int index = static_cast<int>(g_sound);
    return index >= 0 && index < kSoundModeCount ? index : static_cast<int>(SoundMode::On);
}

void set_sound_mode(SoundMode mode) {
    if (mode == g_sound) {
        return;
    }
    g_sound = mode;
    publish_audio_gain();
    write_settings();
}

std::string sound_mode_text() {
    std::string text;
    for (int i = 0; i < kSoundModeCount; i++) {
        if (!text.empty()) {
            text += "  ";
        }
        text += std::string(i == static_cast<int>(g_sound) ? "[x] " : "[ ] ") +
                sound_mode_label(i);
    }
    return text;
}

const char* mouse_mode_label(int index) {
    switch (static_cast<MouseMode>(index)) {
        case MouseMode::Off: return "OFF";
        case MouseMode::On:  return "ON";
    }
    return "OFF";
}

MouseMode mouse_mode() {
    return g_mouse;
}

int mouse_mode_index() {
    const int index = static_cast<int>(g_mouse);
    return index >= 0 && index < kMouseModeCount ? index : static_cast<int>(MouseMode::On);
}

void set_mouse_mode(MouseMode mode) {
    if (mode == g_mouse) {
        return;
    }
    g_mouse = mode;
    publish_mouse();
    write_settings();
}

void write_settings_file() {
    write_settings();
}

}  // namespace ogre

// The renderer applies PIXEL FILTER on its next screen update (renderer.cpp,
// null_renderer.cpp).
extern "C" void ogre_renderer_filter_changed();

namespace ogre {
namespace {

// The filter is not part of the graphics config, so a publish would compare
// equal and do nothing; the renderer applies it on its next screen update
// instead. Registered with options::on_change by load_settings.
void option_changed(Choice choice) {
    if (choice == Choice::PixelFilter) {
        ogre_renderer_filter_changed();
    }
}

}  // namespace

std::string mouse_mode_text() {
    std::string text;
    for (int i = 0; i < kMouseModeCount; i++) {
        if (!text.empty()) {
            text += "  ";
        }
        text += std::string(i == static_cast<int>(g_mouse) ? "[x] " : "[ ] ") +
                mouse_mode_label(i);
    }
    return text;
}

int volume_percent() {
    return g_volume;
}

int volume_handle_cell(int percent) {
    percent = std::clamp(percent, 0, 100);
    // kVolumeCells cells span the 0..100 range, so the handle moves
    // kVolumeCells-1 times and lands on the nearest cell.
    return (percent * (kVolumeCells - 1) + 50) / 100;
}

int volume_percent_for_cell(int cell) {
    cell = std::clamp(cell, 0, kVolumeCells - 1);
    return cell * 100 / (kVolumeCells - 1);
}

std::string volume_text(int percent) {
    const int cell = volume_handle_cell(percent);
    std::string text;
    for (int i = 0; i < kVolumeCells; i++) {
        text += i == cell ? '|' : '-';
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), " %d%%", std::clamp(percent, 0, 100));
    return text + buffer;
}

void set_volume_percent(int percent) {
    percent = std::clamp(percent, 0, 100);
    if (percent == g_volume) {
        return;
    }
    g_volume = percent;
    publish_audio_gain();
    write_settings();
}

int slider_percent(Slider slider) {
    switch (slider) {
        case Slider::Music:   return g_music;
        case Slider::Effects: return g_effects;
        case Slider::Volume:
        default:              return g_volume;
    }
}

void set_slider_percent(Slider slider, int percent) {
    if (slider == Slider::Volume) {
        set_volume_percent(percent);
        return;
    }
    percent = std::clamp(percent, 0, 100);
    int& value = slider == Slider::Music ? g_music : g_effects;
    if (percent == value) {
        return;
    }
    value = percent;
    write_settings();
}

// libmus (the game's sound driver) keeps two master volumes, written through
// `MusSetMasterVolume` (`func_800814C4`): effects at `0x800B181C` (`0x67FF` at
// boot, from `func_80071EB0`) and songs at `0x800B181E` (`0x7FFF`). Its channel
// update (`0x80082BCC`) multiplies every channel's volume by one of them, so
// scaling the two halfwords rescales music and sound effects separately on the
// next update. The banks call the setter too (five sites), so the game's own
// value is the base: a halfword that differs from what this wrote last frame
// is a new value from the game, and it is kept as the base before scaling.
void apply_mix_volumes(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }
    struct Channel {
        uint32_t address;
        const int* percent;
        int base;
        int written;
    };
    static Channel channels[2] = {
        {0x800B181Cu, &g_effects, -1, -1},
        {0x800B181Eu, &g_music, -1, -1},
    };
    for (Channel& channel : channels) {
        const uint32_t offset = channel.address & 0x1FFFFFFFu;
        const int current = (rdram[(offset + 0) ^ 3u] << 8) | rdram[(offset + 1) ^ 3u];
        if (current != channel.written) {
            channel.base = current;
        }
        const int value = channel.base * *channel.percent / 100;
        rdram[(offset + 0) ^ 3u] = static_cast<uint8_t>(value >> 8);
        rdram[(offset + 1) ^ 3u] = static_cast<uint8_t>(value);
        channel.written = value;
    }
}

int audio_gain_percent() {
    return g_audio_gain.load(std::memory_order_relaxed);
}

std::filesystem::path settings_path(const std::filesystem::path& pref_dir) {
    return pref_dir / "settings.cfg";
}

void load_settings(const std::filesystem::path& pref_dir) {
    g_pref_dir = pref_dir;
    int speed = 1;
    WidescreenMode widescreen = WidescreenMode::Off;
    FrameRateMode frame_rate = FrameRateMode::Original;
    ResolutionMode resolution = ResolutionMode::Native;
    AntialiasMode antialias = AntialiasMode::Off;
    DisplayMode display = DisplayMode::Windowed;
    SoundMode sound = SoundMode::On;
    int volume = kVolumeDefaultPercent;
    MouseMode mouse = MouseMode::On;
    bool have_file = false;
    const std::string text = read_file(settings_path(pref_dir), have_file);
    // This project's options (app/features/options.hpp); with no file, `text` is
    // empty and every option takes its default.
    options::on_change(option_changed);
    options::load(setting_value, text);
    if (have_file) {
        // A file written by an older build can name a value the panel no longer
        // offers; the closest offered one keeps it selectable.
        speed = nearest_game_speed(parse_game_speed(text));
        widescreen = parse_widescreen(text);
        frame_rate = parse_frame_rate(text);
        resolution = parse_resolution(text);
        antialias = parse_antialias(text);
        display = parse_display(text);
        sound = parse_sound(text);
        volume = parse_volume(text);
        g_music = parse_volume(text, "music");
        g_effects = parse_volume(text, "effects");
        mouse = parse_mouse(text);
    }
    if (const int env = env_game_speed(); env != 0) {
        // A developer run names its speed on the command line; the value is
        // shown by the SETTINGS tab but not written over the player's file.
        speed = env;
        std::fprintf(stderr, "[settings] OGRE_SPEED=%d overrides the saved game speed\n", env);
    }
    // OGRE_WIDESCREEN=off|on: the same rule as OGRE_SPEED. The retired
    // `missions` / `always` spellings still parse, as on. It is also how a
    // scripted run reaches the setting without the UI.
    if (const char* env = std::getenv("OGRE_WIDESCREEN"); env != nullptr && env[0] != '\0') {
        widescreen = parse_widescreen(std::string("widescreen = ") + env);
        std::fprintf(stderr, "[settings] OGRE_WIDESCREEN=%s overrides the saved widescreen mode\n",
                     env);
    }
    // OGRE_FRAME_RATE=original|display: the same rule, for the FRAME RATE row.
    if (const char* env = std::getenv("OGRE_FRAME_RATE"); env != nullptr && env[0] != '\0') {
        frame_rate = parse_frame_rate(std::string("frame_rate = ") + env);
        std::fprintf(stderr, "[settings] OGRE_FRAME_RATE=%s overrides the saved frame rate\n", env);
    }
    // OGRE_RESOLUTION=native|2x|auto, OGRE_MSAA=off|2x|4x|8x and
    // OGRE_WINDOW=windowed|fullscreen: the same one-run rule, for the three
    // DISPLAY rows.
    if (const char* env = std::getenv("OGRE_RESOLUTION"); env != nullptr && env[0] != '\0') {
        resolution = parse_resolution(std::string("resolution = ") + env);
        std::fprintf(stderr, "[settings] OGRE_RESOLUTION=%s overrides the saved resolution\n", env);
    }
    if (const char* env = std::getenv("OGRE_MSAA"); env != nullptr && env[0] != '\0') {
        antialias = parse_antialias(std::string("msaa = ") + env);
        std::fprintf(stderr, "[settings] OGRE_MSAA=%s overrides the saved antialiasing\n", env);
    }
    if (const char* env = std::getenv("OGRE_WINDOW"); env != nullptr && env[0] != '\0') {
        display = parse_display(std::string("window = ") + env);
        std::fprintf(stderr, "[settings] OGRE_WINDOW=%s overrides the saved window mode\n", env);
    }
    // OGRE_SOUNDS=off|on and OGRE_VOLUME=<0..100>: the same rule. They let a
    // scripted run set the audio output without the overlay.
    if (const char* env = std::getenv("OGRE_SOUNDS"); env != nullptr && env[0] != '\0') {
        sound = parse_sound(std::string("sounds = ") + env);
        std::fprintf(stderr, "[settings] OGRE_SOUNDS=%s overrides the saved sound mode\n", env);
    }
    if (const char* env = std::getenv("OGRE_VOLUME"); env != nullptr && env[0] != '\0') {
        volume = parse_volume(std::string("volume = ") + env);
        std::fprintf(stderr, "[settings] OGRE_VOLUME=%s overrides the saved volume\n", env);
    }
    // OGRE_MOUSE=off|on: the same rule, for the MOUSE ENABLED row. The two
    // cursor modules have their own `OGRE_MAP_CURSOR` / `OGRE_MISSION_CURSOR`
    // overrides, which win over this one.
    if (const char* env = std::getenv("OGRE_MOUSE"); env != nullptr && env[0] != '\0') {
        mouse = parse_mouse(std::string("mouse = ") + env);
        std::fprintf(stderr, "[settings] OGRE_MOUSE=%s overrides the saved mouse mode\n", env);
    }
    g_game_speed = speed;
    g_widescreen = widescreen;
    g_frame_rate = frame_rate;
    g_resolution = resolution;
    g_antialias = antialias;
    g_display = display;
    g_sound = sound;
    g_volume = volume;
    g_mouse = mouse;
    publish_audio_gain();
    publish_graphics();
    publish_mouse();
    ultramodern::set_speed_multiplier(static_cast<uint32_t>(speed));
    // RESOLUTION and MSAA are experimental: report the value actually applied,
    // and say why a configured one is not.
    const bool experiments = experiments_enabled();
    if (!experiments &&
        (resolution != ResolutionMode::Native || antialias != AntialiasMode::Off)) {
        std::fprintf(stderr,
                     "[settings] resolution %s and msaa %s are experimental and not applied; "
                     "set OGRE_DISPLAY_EXPERIMENTS=1 to enable them\n",
                     lowercase(resolution_mode_label(static_cast<int>(resolution))).c_str(),
                     lowercase(antialias_mode_label(static_cast<int>(antialias))).c_str());
    }
    std::fprintf(stderr,
                 "[settings] game speed x%d, widescreen %s, frame rate %s, resolution %s, msaa %s, "
                 "window %s, sounds %s, volume %d%%, mouse %s%s\n",
                 speed, lowercase(widescreen_mode_label(static_cast<int>(widescreen))).c_str(),
                 lowercase(frame_rate_mode_label(static_cast<int>(frame_rate))).c_str(),
                 lowercase(resolution_mode_label(static_cast<int>(resolution_mode()))).c_str(),
                 lowercase(antialias_mode_label(static_cast<int>(antialias_mode()))).c_str(),
                 lowercase(display_mode_label(static_cast<int>(display))).c_str(),
                 lowercase(sound_mode_label(static_cast<int>(sound))).c_str(), volume,
                 lowercase(mouse_mode_label(static_cast<int>(mouse))).c_str(),
                 experiments ? " (display experiments on)" : "");
}

}  // namespace ogre

