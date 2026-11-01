// Player-editable controller bindings (see input_map.hpp).

#include "input_map.hpp"

#include "settings.hpp"

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <vector>

namespace ogre {
namespace {

const N64ButtonInfo kButtons[kN64ButtonCount] = {
    {"CONFIRM", "a", "Selects the highlighted object.", N64_BTN_A, "A"},
    {"CANCEL", "b", "Cancels a selection.", N64_BTN_B, "B"},
    {"UNIT COMMANDS", "r", "Brings up the action menu.", N64_BTN_R, "R"},
    {"PAUSE", "start", "Pauses the game.", N64_BTN_START, "START"},
    {"CURSOR UP", "up", "Moves the cursor.", N64_BTN_UP, "D-UP"},
    {"CURSOR DOWN", "down", "Moves the cursor.", N64_BTN_DOWN, "D-DOWN"},
    {"CURSOR LEFT", "left", "Moves the cursor.", N64_BTN_LEFT, "D-LEFT"},
    {"CURSOR RIGHT", "right", "Moves the cursor.", N64_BTN_RIGHT, "D-RIGHT"},
    {"CAMERA UP", "cu", "Raises the camera angle.", N64_BTN_C_UP, "C-UP"},
    {"CAMERA DOWN", "cd", "Lowers the camera angle.", N64_BTN_C_DOWN, "C-DOWN"},
    {"OVERVIEW MAP", "cl", "Toggles the overview map.", N64_BTN_C_LEFT, "C-LEFT"},
    {"SCROLL MODE", "cr", "Toggles scroll modes, and pauses the game while held.",
     N64_BTN_C_RIGHT, "C-RIGHT"},
    {"EXTRA 1", "z", "No field action listed.", N64_BTN_Z, "Z"},
    {"EXTRA 2", "l", "No field action listed.", N64_BTN_L, "L"},
};

// Gamepad button names. The tags are the file/parse spellings; the labels are
// what the CONTROLS tab shows. SDL's own names are close, but its availability
// and exact spelling have changed across versions, so the table is local.
// SDL names buttons by position: A is the bottom face button on every pad,
// so a PlayStation pad's A is Cross and a Nintendo pad's A is B.
struct PadName {
    int code;
    const char* tag;
    const char* label;        // Xbox
    const char* playstation;
    const char* nintendo;
    const char* generic;
};

const PadName kPadNames[] = {
    {SDL_CONTROLLER_BUTTON_A, "a", "A", "Cross", "B", "South"},
    {SDL_CONTROLLER_BUTTON_B, "b", "B", "Circle", "A", "East"},
    {SDL_CONTROLLER_BUTTON_X, "x", "X", "Square", "Y", "West"},
    {SDL_CONTROLLER_BUTTON_Y, "y", "Y", "Triangle", "X", "North"},
    {SDL_CONTROLLER_BUTTON_BACK, "back", "View", "Create", "Minus", "Select"},
    {SDL_CONTROLLER_BUTTON_GUIDE, "guide", "Guide", "PS", "Home", "Guide"},
    {SDL_CONTROLLER_BUTTON_START, "start", "Menu", "Options", "Plus", "Start"},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, "lstick", "LS", "L3", "L-Stick", "L-Stick"},
    {SDL_CONTROLLER_BUTTON_RIGHTSTICK, "rstick", "RS", "R3", "R-Stick", "R-Stick"},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "lb", "LB", "L1", "L", "L1"},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "rb", "RB", "R1", "R", "R1"},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, "dup", "D-Up", "D-Up", "D-Up", "D-Up"},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, "ddown", "D-Down", "D-Down", "D-Down", "D-Down"},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, "dleft", "D-Left", "D-Left", "D-Left", "D-Left"},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, "dright", "D-Right", "D-Right", "D-Right", "D-Right"},
};

std::atomic<int> g_detected_style{static_cast<int>(PadStyle::Xbox)};

const char* styled_label(const PadName& name) {
    switch (pad_style()) {
        case PadStyle::PlayStation: return name.playstation;
        case PadStyle::Nintendo:    return name.nintendo;
        case PadStyle::Generic:     return name.generic;
        case PadStyle::Xbox:
        default:                    return name.label;
    }
}

// The right-stick directions the C buttons can bind to. `axis` is the SDL axis
// the sign of `dir` is read on.
struct StickDir {
    const char* tag;
    const char* label;
    int axis;
    int dir;
};

const StickDir kStickDirs[] = {
    {"rstick-up", "Right Stick Up", SDL_CONTROLLER_AXIS_RIGHTY, -1},
    {"rstick-down", "Right Stick Down", SDL_CONTROLLER_AXIS_RIGHTY, 1},
    {"rstick-left", "Right Stick Left", SDL_CONTROLLER_AXIS_RIGHTX, -1},
    {"rstick-right", "Right Stick Right", SDL_CONTROLLER_AXIS_RIGHTX, 1},
};

// Keyboard defaults for a PC game: Enter or Space confirms, Backspace cancels,
// WASD or the arrows move the cursor, E opens the unit commands, P pauses, R/F
// tilt the camera, M is the overview map and Shift the scroll mode. Escape,
// Tab and F1-F3 are the app's own (is_reserved_key). The mouse is separate:
// with MOUSE ENABLED a left click confirms, a right click cancels and the wheel
// moves the cursor (sdl_platform.cpp handle_menu_mouse).
const SDL_Scancode kDefaultKeys[kN64ButtonCount] = {
    SDL_SCANCODE_RETURN, SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_E,    SDL_SCANCODE_P,
    SDL_SCANCODE_W,      SDL_SCANCODE_S,         SDL_SCANCODE_A,    SDL_SCANCODE_D,
    SDL_SCANCODE_R,      SDL_SCANCODE_F,         SDL_SCANCODE_M,    SDL_SCANCODE_LSHIFT,
    SDL_SCANCODE_Z,      SDL_SCANCODE_Q,
};
const SDL_Scancode kDefaultKeys2[kN64ButtonCount] = {
    SDL_SCANCODE_SPACE,   SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UP,      SDL_SCANCODE_DOWN,    SDL_SCANCODE_LEFT,    SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_PAGEUP,  SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
};

// Gamepad defaults in the modern layout (Xbox names): A confirms, B cancels, Y
// opens the unit commands, X is the scroll mode, Menu pauses, the D-pad moves
// the cursor (the left stick is the N64 analog stick) and the right stick
// moves the camera (up/down) and opens the overview map (left). The right
// stick layer is fixed and always on (sdl_platform.cpp), so the camera and map
// rows bind it explicitly only so the CONTROLS tab can show it. View opens the
// Esc menu and RS cycles the game speed, so neither is bound here.
struct PadDefault {
    PadBinding::Source source;
    int code;
    int dir;
    int axis;
};

const PadDefault kDefaultPads[kN64ButtonCount] = {
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_A, 0, -1},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_B, 0, -1},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_Y, 0, -1},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_START, 0, -1},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_DPAD_UP, 0, -1},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0, -1},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0, -1},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0, -1},
    {PadBinding::Source::RightStick, -1, -1, SDL_CONTROLLER_AXIS_RIGHTY},
    {PadBinding::Source::RightStick, -1, 1, SDL_CONTROLLER_AXIS_RIGHTY},
    {PadBinding::Source::RightStick, -1, -1, SDL_CONTROLLER_AXIS_RIGHTX},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_X, 0, -1},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0, -1},
    {PadBinding::Source::Button, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, 0, -1},
};

const PadName* find_pad_name(const std::string& tag) {
    for (const PadName& name : kPadNames) {
        if (tag == name.tag) {
            return &name;
        }
    }
    return nullptr;
}

const StickDir* find_stick_dir(const std::string& tag) {
    for (const StickDir& dir : kStickDirs) {
        if (tag == dir.tag) {
            return &dir;
        }
    }
    return nullptr;
}

std::string trim(const std::string& text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::mutex g_map_mutex;
InputMap g_map;             // guarded by g_map_mutex
std::filesystem::path g_pref_dir;

}  // namespace

const N64ButtonInfo& n64_button_info(int index) {
    if (index < 0 || index >= kN64ButtonCount) {
        return kButtons[0];
    }
    return kButtons[index];
}

InputMap::InputMap() {
    reset_defaults();
}

void InputMap::reset_defaults() {
    for (int i = 0; i < kN64ButtonCount; i++) {
        rows_[i].key = kDefaultKeys[i];
        rows_[i].key2 = kDefaultKeys2[i];
        rows_[i].pad.source = kDefaultPads[i].source;
        rows_[i].pad.code = kDefaultPads[i].code;
        rows_[i].pad.dir = kDefaultPads[i].dir;
        rows_[i].pad.axis = kDefaultPads[i].axis;
    }
}

const ButtonBinding& InputMap::row(int index) const {
    if (index < 0 || index >= kN64ButtonCount) {
        return rows_[0];
    }
    return rows_[index];
}

namespace {
std::string scancode_text(SDL_Scancode code) {
    if (code == SDL_SCANCODE_UNKNOWN) {
        return {};
    }
    const char* name = SDL_GetScancodeName(code);
    return (name != nullptr && name[0] != '\0') ? name : std::string();
}
}  // namespace

std::string InputMap::key_text(int index) const {
    const std::string first = scancode_text(row(index).key);
    const std::string second = scancode_text(row(index).key2);
    if (first.empty() && second.empty()) {
        return "-";
    }
    if (first.empty() || second.empty()) {
        return first + second;
    }
    return first + " / " + second;
}

std::string InputMap::pad_text(int index) const {
    return pad_source_text(row(index).pad);
}

void InputMap::set_key(int index, SDL_Scancode code) {
    if (index >= 0 && index < kN64ButtonCount) {
        rows_[index].key = code;
    }
}

void InputMap::set_pad_button(int index, int pad_code) {
    if (index < 0 || index >= kN64ButtonCount) {
        return;
    }
    rows_[index].pad.source = PadBinding::Source::Button;
    rows_[index].pad.code = pad_code;
    rows_[index].pad.dir = 0;
    rows_[index].pad.axis = -1;
}

void InputMap::clear_key(int index) {
    if (index >= 0 && index < kN64ButtonCount) {
        rows_[index].key = SDL_SCANCODE_UNKNOWN;
        rows_[index].key2 = SDL_SCANCODE_UNKNOWN;
    }
}

void InputMap::clear_pad(int index) {
    if (index >= 0 && index < kN64ButtonCount) {
        rows_[index].pad = PadBinding{};
    }
}

std::string InputMap::serialize() const {
    std::string out =
        "# OB64: Another World controller bindings.\n"
        "# <n64 button>.<key|key2|pad> = <value>; `-` and `none` are unbound.\n"
        "# Edited from the launcher's CONTROLS tab, or the in-game Esc overlay.\n";
    for (int i = 0; i < kN64ButtonCount; i++) {
        const N64ButtonInfo& info = kButtons[i];
        const std::string first = scancode_text(rows_[i].key);
        const std::string second = scancode_text(rows_[i].key2);
        out += std::string(info.tag) + ".key = " + (first.empty() ? "-" : first) + "\n";
        out += std::string(info.tag) + ".key2 = " + (second.empty() ? "-" : second) + "\n";
        out += std::string(info.tag) + ".pad = ";
        const PadBinding& pad = rows_[i].pad;
        switch (pad.source) {
            case PadBinding::Source::Button: {
                std::string tag = "none";
                for (const PadName& name : kPadNames) {
                    if (name.code == pad.code) {
                        tag = name.tag;
                        break;
                    }
                }
                out += "button:" + tag;
                break;
            }
            case PadBinding::Source::RightStick: {
                std::string tag = "rstick-up";
                for (const StickDir& dir : kStickDirs) {
                    if (dir.axis == pad.axis && dir.dir == pad.dir) {
                        tag = dir.tag;
                        break;
                    }
                }
                out += tag;
                break;
            }
            case PadBinding::Source::None:
            default:
                out += "none";
                break;
        }
        out += "\n";
    }
    return out;
}

bool InputMap::parse(const std::string& text) {
    InputMap parsed;  // defaults for anything the file does not mention
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) {
            line = line.substr(0, hash);
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        const std::string field = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        const size_t dot = field.rfind('.');
        if (dot == std::string::npos) {
            continue;
        }
        const std::string tag = field.substr(0, dot);
        const std::string slot = field.substr(dot + 1);
        int index = -1;
        for (int i = 0; i < kN64ButtonCount; i++) {
            if (tag == kButtons[i].tag) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            continue;
        }
        if (slot == "key" || slot == "key2") {
            SDL_Scancode& target = slot == "key" ? parsed.rows_[index].key : parsed.rows_[index].key2;
            target = (value.empty() || value == "-") ? SDL_SCANCODE_UNKNOWN
                                                     : SDL_GetScancodeFromName(value.c_str());
        }
        else if (slot == "pad") {
            if (value.empty() || value == "none") {
                parsed.rows_[index].pad = PadBinding{};
            }
            else if (value.rfind("button:", 0) == 0) {
                const PadName* name = find_pad_name(value.substr(7));
                if (name != nullptr) {
                    parsed.set_pad_button(index, name->code);
                }
            }
            else {
                const StickDir* dir = find_stick_dir(value);
                if (dir != nullptr) {
                    parsed.rows_[index].pad.source = PadBinding::Source::RightStick;
                    parsed.rows_[index].pad.dir = dir->dir;
                    parsed.rows_[index].pad.axis = dir->axis;
                    parsed.rows_[index].pad.code = -1;
                }
            }
        }
    }
    *this = parsed;
    return true;
}

std::string pad_button_name(int code) {
    for (const PadName& name : kPadNames) {
        if (name.code == code) {
            return styled_label(name);
        }
    }
    return "?";
}

void note_pad_type(SDL_GameController* controller) {
    PadStyle style = PadStyle::Xbox;
#if SDL_VERSION_ATLEAST(2, 0, 12)
    if (controller != nullptr) {
        switch (SDL_GameControllerGetType(controller)) {
            case SDL_CONTROLLER_TYPE_PS3:
            case SDL_CONTROLLER_TYPE_PS4:
#if SDL_VERSION_ATLEAST(2, 0, 14)
            case SDL_CONTROLLER_TYPE_PS5:
#endif
                style = PadStyle::PlayStation;
                break;
            case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
                style = PadStyle::Nintendo;
                break;
            case SDL_CONTROLLER_TYPE_XBOX360:
            case SDL_CONTROLLER_TYPE_XBOXONE:
                style = PadStyle::Xbox;
                break;
            default:
                style = PadStyle::Generic;
                break;
        }
    }
#else
    (void)controller;
#endif
    g_detected_style.store(static_cast<int>(style));
}

PadStyle pad_style() {
    switch (choice_index(Choice::ButtonPrompts)) {
        case 1: return PadStyle::Xbox;
        case 2: return PadStyle::PlayStation;
        case 3: return PadStyle::Nintendo;
        default: return static_cast<PadStyle>(g_detected_style.load());
    }
}

namespace {
std::atomic<int> g_last_device{static_cast<int>(InputDevice::Keyboard)};
SDL_JoystickID g_last_pad = -1;
int g_mouse_travel = 0;  // points moved since the pad was last used
}  // namespace

void note_input_event(const SDL_Event& event) {
    bool pad = false;
    bool keyboard = false;
    SDL_JoystickID which = -1;
    switch (event.type) {
        case SDL_CONTROLLERBUTTONDOWN:
            pad = true;
            which = event.cbutton.which;
            break;
        case SDL_CONTROLLERAXISMOTION:
            // Triggers rest at 0 and sticks near it; a push past the dead zone
            // counts, the drift of a resting stick does not.
            if (event.caxis.value > 12000 || event.caxis.value < -12000) {
                pad = true;
                which = event.caxis.which;
            }
            break;
        case SDL_KEYDOWN:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEWHEEL:
            keyboard = true;
            break;
        case SDL_MOUSEMOTION:
            // A pointer warp (KEEP POINTER IN WINDOW) or a bumped desk moves a
            // point or two; a hand on the mouse moves more.
            g_mouse_travel += std::abs(event.motion.xrel) + std::abs(event.motion.yrel);
            keyboard = g_mouse_travel > 12;
            break;
        default:
            break;
    }
    if (pad) {
        g_mouse_travel = 0;
        if (g_last_device.exchange(static_cast<int>(InputDevice::Pad)) != static_cast<int>(InputDevice::Pad)) {
            std::fprintf(stderr, "[input] prompts: pad\n");
        }
        if (which != g_last_pad) {
            g_last_pad = which;
            note_pad_type(SDL_GameControllerFromInstanceID(which));
        }
    }
    else if (keyboard) {
        g_mouse_travel = 0;
        if (g_last_device.exchange(static_cast<int>(InputDevice::Keyboard)) != static_cast<int>(InputDevice::Keyboard)) {
            std::fprintf(stderr, "[input] prompts: keyboard and mouse\n");
        }
    }
}

InputDevice last_input_device() {
    return static_cast<InputDevice>(g_last_device.load());
}

namespace {
// Image names (without .png) per pad button: Xbox, PlayStation, Nintendo.
struct PadImage {
    int code;
    const char* xbox;
    const char* playstation;
    const char* nintendo;
};
const PadImage kPadImages[] = {
    {SDL_CONTROLLER_BUTTON_A, "xbox_button_a", "playstation_button_cross", "switch_button_b"},
    {SDL_CONTROLLER_BUTTON_B, "xbox_button_b", "playstation_button_circle", "switch_button_a"},
    {SDL_CONTROLLER_BUTTON_X, "xbox_button_x", "playstation_button_square", "switch_button_y"},
    {SDL_CONTROLLER_BUTTON_Y, "xbox_button_y", "playstation_button_triangle", "switch_button_x"},
    {SDL_CONTROLLER_BUTTON_BACK, "xbox_button_view", "playstation5_button_create", "switch_button_minus"},
    {SDL_CONTROLLER_BUTTON_GUIDE, "xbox_guide", "xbox_guide", "switch_button_home"},
    {SDL_CONTROLLER_BUTTON_START, "xbox_button_menu", "playstation5_button_options", "switch_button_plus"},
    {SDL_CONTROLLER_BUTTON_LEFTSTICK, "xbox_ls", "playstation_button_l3", "switch_stick_l_press"},
    {SDL_CONTROLLER_BUTTON_RIGHTSTICK, "xbox_rs", "playstation_button_r3", "switch_stick_r_press"},
    {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "xbox_lb", "playstation_trigger_l1", "switch_button_l"},
    {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "xbox_rb", "playstation_trigger_r1", "switch_button_r"},
    {SDL_CONTROLLER_BUTTON_DPAD_UP, "xbox_dpad_up", "playstation_dpad_up", "switch_dpad_up"},
    {SDL_CONTROLLER_BUTTON_DPAD_DOWN, "xbox_dpad_down", "playstation_dpad_down", "switch_dpad_down"},
    {SDL_CONTROLLER_BUTTON_DPAD_LEFT, "xbox_dpad_left", "playstation_dpad_left", "switch_dpad_left"},
    {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, "xbox_dpad_right", "playstation_dpad_right", "switch_dpad_right"},
};
const PadImage kAxisImages[] = {
    {SDL_CONTROLLER_AXIS_TRIGGERLEFT, "xbox_lt", "playstation_trigger_l2", "switch_button_zl"},
    {SDL_CONTROLLER_AXIS_TRIGGERRIGHT, "xbox_rt", "playstation_trigger_r2", "switch_button_zr"},
    {SDL_CONTROLLER_AXIS_LEFTX, "xbox_stick_l", "playstation_stick_l", "switch_stick_l"},
    {SDL_CONTROLLER_AXIS_RIGHTX, "xbox_stick_r", "playstation_stick_r", "switch_stick_r"},
};
// The keys and mouse images shipped in ui/prompts, by SDL key name.
struct KeyImage {
    const char* key;
    const char* image;
};
const KeyImage kKeyImages[] = {
    {"Delete", "keyboard_delete"}, {"H", "keyboard_h"}, {"I", "keyboard_i"}, {"E", "keyboard_e"},
    {"F1", "keyboard_f1"}, {"F2", "keyboard_f2"}, {"F3", "keyboard_f3"}, {"F4", "keyboard_f4"},
    {"Backspace", "keyboard_backspace"}, {".", "keyboard_period"}, {",", "keyboard_comma"},
    {"Escape", "keyboard_escape"}, {"Return", "keyboard_enter"}, {"Space", "keyboard_space"},
    {"Tab", "keyboard_tab"}, {"PageUp", "keyboard_page_up"}, {"PageDown", "keyboard_page_down"},
    {"arrows", "keyboard_arrows"}, {"mouse_left", "mouse_left"}, {"mouse_right", "mouse_right"},
    {"mouse_scroll", "mouse_scroll"}, {"mouse_move", "mouse_move"},
};

std::string styled_image(const PadImage& image) {
    switch (pad_style()) {
        case PadStyle::PlayStation: return std::string("prompts/") + image.playstation + ".png";
        case PadStyle::Nintendo:    return std::string("prompts/") + image.nintendo + ".png";
        default:                    return std::string("prompts/") + image.xbox + ".png";
    }
}
}  // namespace

std::string pad_prompt_image(int code) {
    for (const PadImage& image : kPadImages) {
        if (image.code == code) {
            return styled_image(image);
        }
    }
    return "";
}

std::string pad_axis_prompt_image(int axis) {
    for (const PadImage& image : kAxisImages) {
        if (image.code == axis) {
            return styled_image(image);
        }
    }
    return "";
}

std::string key_prompt_image(const std::string& key) {
    for (const KeyImage& image : kKeyImages) {
        if (key == image.key) {
            return std::string("prompts/") + image.image + ".png";
        }
    }
    return "";
}

int pad_button_from_name(const std::string& name) {
    std::string lowered = name;
    for (char& c : lowered) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    for (const PadName& entry : kPadNames) {
        if (lowered == entry.tag) {
            return entry.code;
        }
    }
    return -1;
}

bool pad_button_is_bound(int code) {
    const InputMap map = read_input_map();
    for (int i = 0; i < map.size(); i++) {
        const PadBinding& binding = map.row(i).pad;
        if (binding.source == PadBinding::Source::Button && binding.code == code) {
            return true;
        }
    }
    return false;
}

std::string pad_source_text(const PadBinding& binding) {
    switch (binding.source) {
        case PadBinding::Source::Button:
            return pad_button_name(binding.code);
        case PadBinding::Source::RightStick:
            for (const StickDir& dir : kStickDirs) {
                if (dir.axis == binding.axis && dir.dir == binding.dir) {
                    return dir.label;
                }
            }
            return "R-Stick";
        case PadBinding::Source::None:
        default:
            return "-";
    }
}

bool is_reserved_key(SDL_Scancode code) {
    // Escape opens and closes the in-game overlay, F1 the button legend, F2
    // toggles AUTO-PLAY DIALOGUE, F3 the Organize view, F5 and F9 quick save and
    // quick load (quick_save.hpp), Tab is the fast-forward hold and Print Screen
    // and F12 take a screenshot (sdl_platform.cpp); binding one to a game button
    // would make the two actions fight.
    return code == SDL_SCANCODE_ESCAPE || code == SDL_SCANCODE_TAB || code == SDL_SCANCODE_F1 ||
           code == SDL_SCANCODE_F2 || code == SDL_SCANCODE_F3 || code == SDL_SCANCODE_F5 ||
           code == SDL_SCANCODE_F9 ||
           code == SDL_SCANCODE_PRINTSCREEN || code == SDL_SCANCODE_F12;
}

int first_pressed_pad_button(SDL_GameController* const* pads, int count) {
    for (int i = 0; i < count; i++) {
        SDL_GameController* pad = pads[i];
        if (pad == nullptr) {
            continue;
        }
        for (const PadName& name : kPadNames) {
            if (SDL_GameControllerGetButton(
                    pad, static_cast<SDL_GameControllerButton>(name.code)) != 0) {
                return name.code;
            }
        }
    }
    return -1;
}

// --- the live map -------------------------------------------------------------

InputMap read_input_map() {
    std::lock_guard<std::mutex> lock(g_map_mutex);
    return g_map;
}

void update_input_map(const InputMap& map) {
    {
        std::lock_guard<std::mutex> lock(g_map_mutex);
        g_map = map;
    }
    if (!g_pref_dir.empty()) {
        const std::filesystem::path path = input_map_path(g_pref_dir);
        if (FILE* file = std::fopen(path.string().c_str(), "wb")) {
            const std::string text = map.serialize();
            std::fwrite(text.data(), 1, text.size(), file);
            std::fclose(file);
        }
        else {
            std::fprintf(stderr, "[input] could not write %s\n", path.string().c_str());
        }
    }
}

std::filesystem::path input_map_path(const std::filesystem::path& pref_dir) {
    return pref_dir / "controls.cfg";
}

void load_input_map(const std::filesystem::path& pref_dir) {
    g_pref_dir = pref_dir;
    const std::filesystem::path path = input_map_path(pref_dir);
    FILE* file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        return;  // defaults
    }
    std::string text;
    char buffer[512];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        text.append(buffer, got);
    }
    std::fclose(file);
    // A file written before the action layout (no `key2` lines) holds the old
    // N64-style keys (X/Z/C, I/J/K/L); the modern defaults replace it.
    if (text.find(".key2") == std::string::npos) {
        std::fprintf(stderr, "[input] %s predates the action layout; using the defaults\n",
                     path.string().c_str());
        return;
    }
    InputMap loaded;
    loaded.parse(text);
    {
        std::lock_guard<std::mutex> lock(g_map_mutex);
        g_map = loaded;
    }
    std::fprintf(stderr, "[input] loaded %s\n", path.string().c_str());
}

uint16_t keyboard_buttons_from_map(const InputMap& map) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    uint16_t buttons = 0;
    for (int i = 0; i < map.size(); i++) {
        for (const SDL_Scancode code : {map.row(i).key, map.row(i).key2}) {
            if (code != SDL_SCANCODE_UNKNOWN && keys[code] != 0) {
                buttons |= map.info(i).bit;
            }
        }
    }
    return buttons;
}

uint16_t gamecontroller_buttons_from_map(const InputMap& map, SDL_GameController* controller, uint32_t skip) {
    if (controller == nullptr) {
        return 0;
    }
    uint16_t buttons = 0;
    for (int i = 0; i < map.size(); i++) {
        const PadBinding& pad = map.row(i).pad;
        switch (pad.source) {
            case PadBinding::Source::Button:
                if (pad.code >= 0 && (skip & (1u << pad.code)) == 0 &&
                    SDL_GameControllerGetButton(controller,
                                                static_cast<SDL_GameControllerButton>(pad.code)) != 0) {
                    buttons |= map.info(i).bit;
                }
                break;
            case PadBinding::Source::RightStick: {
                const Sint16 value = SDL_GameControllerGetAxis(
                    controller, static_cast<SDL_GameControllerAxis>(pad.axis));
                if (pad.dir < 0 ? value < -8000 : value > 8000) {
                    buttons |= map.info(i).bit;
                }
                break;
            }
            case PadBinding::Source::None:
            default:
                break;
        }
    }
    return buttons;
}

}  // namespace ogre

