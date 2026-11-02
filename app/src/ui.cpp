// The app's ASCII UI (see ui.hpp).

#include "ui.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <vector>

#include "army_roster.hpp"
#include "content_db.hpp"
#include "lua_runtime.hpp"
#include "settings.hpp"
#include "save_backup.hpp"
#include "war_director.hpp"
#include "ultramodern/ultramodern.hpp"

namespace ogre::ui {
namespace {

// The launcher's scale and colours. The overlay reuses them so the two screens
// look like one program.
constexpr int kTextScale = 2;
constexpr int kTabScale = 2;

// Not const: HIGH CONTRAST (settings.hpp -> Choice::Contrast) swaps them in
// `apply_contrast`, which draw_panel calls before it draws.
SDL_Color kTitleColor{238, 238, 232, 255};
SDL_Color kSubtitleColor{196, 200, 208, 255};
SDL_Color kHintColor{122, 128, 138, 255};
SDL_Color kWarmColor{198, 160, 92, 220};
SDL_Color kPanelColor{20, 22, 27, 240};
SDL_Color kSelectColor{52, 58, 70, 255};

void apply_contrast() {
    const bool high = choice_on(Choice::Contrast);
    kTitleColor = high ? SDL_Color{255, 255, 255, 255} : SDL_Color{238, 238, 232, 255};
    kSubtitleColor = high ? SDL_Color{255, 255, 255, 255} : SDL_Color{196, 200, 208, 255};
    kHintColor = high ? SDL_Color{210, 210, 210, 255} : SDL_Color{122, 128, 138, 255};
    kWarmColor = high ? SDL_Color{255, 214, 0, 255} : SDL_Color{198, 160, 92, 220};
    kPanelColor = high ? SDL_Color{0, 0, 0, 255} : SDL_Color{20, 22, 27, 240};
    kSelectColor = high ? SDL_Color{40, 60, 150, 255} : SDL_Color{52, 58, 70, 255};
}

// Pads `text` with spaces on the right so a column of rows lines up. The font is
// fixed-cell, so space padding is exact.
std::string pad_to(const std::string& text, size_t width) {
    if (text.size() >= width) {
        return text;
    }
    return text + std::string(width - text.size(), ' ');
}

// Which AUDIO slider a VOLUME row drives (settings.hpp -> Slider). The row's
// `binding` carries it; -1 (the default) is the output VOLUME.
Slider slider_of(const Panel::Row& row) {
    return row.binding == 1 ? Slider::Music : row.binding == 2 ? Slider::Effects : Slider::Volume;
}

// The displayed value of one mod config option. Enum values show the option
// name, not the number, so the panel reads like the mod's own description.
std::string config_value_text(const recomp::config::ConfigOption& option,
                              const recomp::config::ConfigValueVariant& value) {
    using recomp::config::ConfigOptionType;
    switch (option.type) {
        case ConfigOptionType::Enum: {
            const auto& enumeration =
                std::get<recomp::config::ConfigOptionEnum>(option.variant);
            const uint32_t current = std::holds_alternative<uint32_t>(value)
                                         ? std::get<uint32_t>(value)
                                         : enumeration.default_value;
            const auto found = enumeration.find_option_from_value(current);
            return found != enumeration.options.end() ? found->name
                                                      : std::to_string(current);
        }
        case ConfigOptionType::Bool: {
            const bool current = std::holds_alternative<bool>(value)
                                     ? std::get<bool>(value)
                                     : std::get<recomp::config::ConfigOptionBool>(option.variant).default_value;
            return current ? "on" : "off";
        }
        case ConfigOptionType::Number: {
            const auto& number =
                std::get<recomp::config::ConfigOptionNumber>(option.variant);
            const double current = std::holds_alternative<double>(value)
                                       ? std::get<double>(value)
                                       : number.default_value;
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.*f", number.precision, current);
            return buffer;
        }
        case ConfigOptionType::String: {
            return std::holds_alternative<std::string>(value) ? std::get<std::string>(value)
                                                              : std::string{};
        }
        default:
            return {};
    }
}

// --- The game's own Settings menu, on the GAMEPLAY tab -------------------------
//
// The game's Settings screen (bank V, func_ovlV_80214FA0; its switch at
// 0x80215580..0x80215668) writes each row's choice to one global. The labels,
// choice words and help lines are the game's own (the module's data at
// 0x802192C0.., the help block it decompresses, 0x802B5280..; developer captures
// 15-46 of 2026-10-30 09:47, one row changed per capture). Row 12, Sound
// settings (Stereo / Mono, 0x800E9C11 and func_800EA8CC), is left out: the
// port's SOUNDS and VOLUME rows own sound.
struct GameSetting {
    const char* name;
    uint32_t address;
    int type;  // 0 a byte holding the choice, 1 message speed, 2 cursor speed (halfwords)
    std::vector<const char*> labels;
    const char* help;
};

const std::vector<GameSetting>& game_settings() {
    static const std::vector<GameSetting> table = {
        {"MESSAGE SPEED", 0x800E9C0Cu, 1, {"SLOW", "NORMAL", "FAST"},
         "Sets the text display speed. You can select from 3 different speed settings."},
        {"CURSOR SPEED", 0x800E9C0Au, 2, {"SLOWER", "SLOW", "NORMAL", "FAST", "FASTER"},
         "Sets the cursor speed. You can select from five different speed settings."},
        {"FIELD SPEED", 0x800E9C13u, 0, {"SLOW", "NORMAL", "FAST"},
         "Sets the rate at which time passes on the Field Map (the game's Game speed). The port marches units "
         "5x, 9x or 18x per frame for these three."},
        {"UNIT REPORT TYPE", 0x800E9C16u, 0, {"NONE", "THIRD PERSON", "FIRST PERSON"},
         "Sets the report type of the unit's actions on the Field Map. NONE turns the reports off."},
        {"HELP DISPLAY", 0x800E9C10u, 0, {"OFF", "ON"}, "Toggles the Auto-Help display On and Off."},
        {"ICON NAME DISPLAY", 0x800E9C0Eu, 0, {"OFF", "ON"}, "Toggles the name display under each icon On and Off."},
        {"LEGION INDICATOR", 0x800E9C14u, 0, {"OFF", "ON"},
         "Toggles the legion icon display on the Field Map On and Off."},
        {"DESTINATION DISPLAY", 0x800E9C15u, 0, {"OFF", "ON"},
         "Toggles the name display of a destination on the Field Map On and Off."},
        {"BATTLE ACTION NAME", 0x800E9C0Fu, 0, {"OFF", "ON"},
         "Toggles the name display of magic spells and special attacks on the Battle Screen On and Off."},
        {"BATTLE ANIMATION", 0x800E9C12u, 0, {"OFF", "ON"}, "Toggles the battle animations On and Off."},
        {"QUICK EXIT", 0x800E9C17u, 0, {"OFF", "ON"},
         "Adds Quick Exit functionality to the L Button to be used in Organize Screen."},
        {"CANCEL ALL", 0x800E9C18u, 0, {"OFF", "ON"},
         "Adds Cancel All functionality to the R Button to be used in Organize Screen."},
    };
    return table;
}

constexpr uint16_t kMessageSpeeds[3] = {256, 512, 1536};  // the table at 0x80219574

// The setting's current choice, or -1 with no game running.
int game_setting_current(const GameSetting& g) {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram == nullptr) {
        return -1;
    }
    const uint32_t offset = g.address - 0x80000000u;
    if (g.type == 0) {
        const int v = rdram[offset ^ 3u];
        return v < static_cast<int>(g.labels.size()) ? v : -1;
    }
    const int v = (rdram[offset ^ 3u] << 8) | rdram[(offset + 1) ^ 3u];
    if (g.type == 1) {
        for (int i = 0; i < 3; i++) {
            if (kMessageSpeeds[i] == v) {
                return i;
            }
        }
        return -1;
    }
    // Cursor speed: (4 - choice) * 128 + 256 (0x802155A0).
    const int choice = 4 - (v - 256) / 128;
    return choice >= 0 && choice < 5 ? choice : -1;
}

// Writes the choice where the game's own menu writes it.
bool game_setting_set(const GameSetting& g, int choice) {
    uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram == nullptr || choice < 0 || choice >= static_cast<int>(g.labels.size())) {
        return false;
    }
    const uint32_t offset = g.address - 0x80000000u;
    if (g.type == 0) {
        rdram[offset ^ 3u] = static_cast<uint8_t>(choice);
        return true;
    }
    const int v = g.type == 1 ? kMessageSpeeds[choice] : (4 - choice) * 128 + 256;
    rdram[offset ^ 3u] = static_cast<uint8_t>(v >> 8);
    rdram[(offset + 1) ^ 3u] = static_cast<uint8_t>(v);
    return true;
}

const char* kLauncherHint =
    "TAB SWITCHES TAB   UP/DOWN SELECT   SPACE ACTIVATE   LEFT/RIGHT CHANGE";
const char* kOverlayHint =
    "TAB SWITCHES TAB   UP/DOWN SELECT   LEFT/RIGHT CHANGE   ESC RESUMES THE GAME";
const char* kCaptureHint =
    "PRESS A KEY OR GAMEPAD BUTTON   BACKSPACE CLEARS KEY   DELETE CLEARS PAD   ESC CANCELS";

// --- radio rows (GAME SPEED, WIDESCREEN) --------------------------------------
// A radio row draws its options as markers in the second column instead of one
// text run, so measure_panel (the row height) and draw_panel (the markers and
// their click rects) lay the same group out with this helper. The result is a
// pure function of its arguments, so the measured and the drawn row agree.

struct RadioToken {
    std::string text;
    int x = 0;
    int y = 0;
    int option = -1;
};

struct RadioLayout {
    std::vector<RadioToken> tokens;
    std::vector<SDL_Rect> option_rects;  // relative to the row's top-left
    int height = 0;
};

// `labels` in display order; `current` is the index of the live option, or -1
// when the live value is not one the row offers.
RadioLayout layout_radio(const Font& font, int scale, int max_width,
                         const std::vector<std::string>& labels, int current) {
    RadioLayout layout;
    const int line_height = scale * Font::kCellHeight;
    const int space = font.width(" ", scale);
    int x = 0;
    int y = 0;
    for (size_t i = 0; i < labels.size(); i++) {
        const std::string marker = static_cast<int>(i) == current ? "[x]" : "[ ]";
        const int marker_width = font.width(marker, scale);
        const int option_width = marker_width + space + font.width(labels[i], scale);
        // A narrow window wraps the group onto another line; the option itself
        // never splits.
        if (x > 0 && x + option_width > max_width) {
            x = 0;
            y += line_height;
        }
        layout.tokens.push_back(
            RadioToken{marker, x, y, static_cast<int>(i)});
        layout.tokens.push_back(
            RadioToken{labels[i], x + marker_width + space, y, static_cast<int>(i)});
        layout.option_rects.push_back(SDL_Rect{x, y, option_width, line_height});
        x += option_width + space;
    }
    layout.height = y + line_height;
    return layout;
}

std::vector<std::string> game_speed_labels() {
    std::vector<std::string> labels;
    for (int i = 0; i < kGameSpeedCount; i++) {
        labels.push_back(std::to_string(game_speed_value(i)));
    }
    return labels;
}

RadioLayout layout_game_speed(const Font& font, int scale, int max_width, int current) {
    return layout_radio(font, scale, max_width, game_speed_labels(),
                        game_speed_index(current));
}

std::vector<std::string> widescreen_labels() {
    std::vector<std::string> labels;
    for (int i = 0; i < kWidescreenModeCount; i++) {
        labels.push_back(widescreen_mode_label(i));
    }
    return labels;
}

RadioLayout layout_widescreen(const Font& font, int scale, int max_width, int current) {
    return layout_radio(font, scale, max_width, widescreen_labels(), current);
}

std::vector<std::string> frame_rate_labels() {
    std::vector<std::string> labels;
    for (int i = 0; i < kFrameRateModeCount; i++) {
        labels.push_back(frame_rate_mode_label(i));
    }
    return labels;
}

RadioLayout layout_frame_rate(const Font& font, int scale, int max_width, int current) {
    return layout_radio(font, scale, max_width, frame_rate_labels(), current);
}

std::vector<std::string> resolution_labels() {
    std::vector<std::string> labels;
    for (int i = 0; i < kResolutionModeCount; i++) {
        labels.push_back(resolution_mode_label(i));
    }
    return labels;
}

RadioLayout layout_resolution(const Font& font, int scale, int max_width, int current) {
    return layout_radio(font, scale, max_width, resolution_labels(), current);
}

std::vector<std::string> antialias_labels() {
    std::vector<std::string> labels;
    for (int i = 0; i < kAntialiasModeCount; i++) {
        labels.push_back(antialias_mode_label(i));
    }
    return labels;
}

RadioLayout layout_antialias(const Font& font, int scale, int max_width, int current) {
    return layout_radio(font, scale, max_width, antialias_labels(), current);
}

std::vector<std::string> display_labels() {
    std::vector<std::string> labels;
    for (int i = 0; i < kDisplayModeCount; i++) {
        labels.push_back(display_mode_label(i));
    }
    return labels;
}

RadioLayout layout_display(const Font& font, int scale, int max_width, int current) {
    return layout_radio(font, scale, max_width, display_labels(), current);
}

std::vector<std::string> sound_labels() {
    std::vector<std::string> labels;
    for (int i = 0; i < kSoundModeCount; i++) {
        labels.push_back(sound_mode_label(i));
    }
    return labels;
}

RadioLayout layout_sound(const Font& font, int scale, int max_width, int current) {
    return layout_radio(font, scale, max_width, sound_labels(), current);
}

std::vector<std::string> mouse_labels() {
    std::vector<std::string> labels;
    for (int i = 0; i < kMouseModeCount; i++) {
        labels.push_back(mouse_mode_label(i));
    }
    return labels;
}

RadioLayout layout_mouse(const Font& font, int scale, int max_width, int current) {
    return layout_radio(font, scale, max_width, mouse_labels(), current);
}

// The row kind of one entry of this project's option table (options.hpp).
Panel::RowKind choice_row(Choice choice) {
    return static_cast<Panel::RowKind>(static_cast<int>(Panel::RowKind::ChoiceRow) + static_cast<int>(choice));
}

// The Choice a row kind stands for, if it is an option table row.
bool choice_of(Panel::RowKind kind, Choice& choice) {
    const int index = static_cast<int>(kind) - static_cast<int>(Panel::RowKind::ChoiceRow);
    if (index < 0 || index >= kChoiceCount) {
        return false;
    }
    choice = static_cast<Choice>(index);
    return true;
}

// The option table rows of one Esc menu section, in the table's order.
void append_choices(std::vector<Panel::Row>& rows, const char* tab, const char* section) {
    for (Choice choice : options::in_section(tab, section)) {
        rows.push_back(Panel::Row{choice_row(choice), {}, 0, 0, -1});
    }
}

// --- radio rows ---------------------------------------------------------------
// Eight row kinds draw as a radio group (GAME SPEED, WIDESCREEN, FRAME RATE,
// RESOLUTION, MSAA, WINDOW, SOUNDS, MOUSE ENABLED). The measure, draw, step, click and pad
// paths each need the layout and the live option, so both are looked up in one
// place: a kind added to the table is handled by all of them.

bool is_radio_row(Panel::RowKind kind) {
    if (Choice choice; choice_of(kind, choice)) {
        return true;
    }
    switch (kind) {
        case Panel::RowKind::GameSpeed:
        case Panel::RowKind::Widescreen:
        case Panel::RowKind::FrameRate:
        case Panel::RowKind::Resolution:
        case Panel::RowKind::Antialias:
        case Panel::RowKind::Display:
        case Panel::RowKind::Sound:
        case Panel::RowKind::Mouse:
            return true;
        default:
            return false;
    }
}

// The index of the row's live option, or -1 when the live value is not one the
// row offers.
int radio_row_current(Panel::RowKind kind) {
    if (Choice choice; choice_of(kind, choice)) {
        return choice_index(choice);
    }
    switch (kind) {
        case Panel::RowKind::GameSpeed:
            return game_speed_index(game_speed());
        case Panel::RowKind::Widescreen:
            return widescreen_mode_index();
        case Panel::RowKind::FrameRate:
            return frame_rate_mode_index();
        case Panel::RowKind::Resolution:
            return resolution_mode_index();
        case Panel::RowKind::Antialias:
            return antialias_mode_index();
        case Panel::RowKind::Display:
            return display_mode_index();
        case Panel::RowKind::Sound:
            return sound_mode_index();
        case Panel::RowKind::Mouse:
            return mouse_mode_index();
        default:
            return -1;
    }
}

int radio_row_count(Panel::RowKind kind) {
    if (Choice choice; choice_of(kind, choice)) {
        return choice_option_count(choice);
    }
    switch (kind) {
        case Panel::RowKind::GameSpeed:   return kGameSpeedCount;
        case Panel::RowKind::Widescreen:  return kWidescreenModeCount;
        case Panel::RowKind::FrameRate:   return kFrameRateModeCount;
        case Panel::RowKind::Resolution:  return kResolutionModeCount;
        case Panel::RowKind::Antialias:   return kAntialiasModeCount;
        case Panel::RowKind::Display:     return kDisplayModeCount;
        case Panel::RowKind::Sound:       return kSoundModeCount;
        case Panel::RowKind::Mouse:       return kMouseModeCount;
        default:                          return 0;
    }
}

RadioLayout radio_row_layout(Panel::RowKind kind, const Font& font, int scale, int max_width,
                             int current) {
    if (Choice choice; choice_of(kind, choice)) {
        std::vector<std::string> labels;
        for (int i = 0; i < choice_option_count(choice); i++) {
            labels.push_back(choice_label(choice, i));
        }
        return layout_radio(font, scale, max_width, labels, current);
    }
    switch (kind) {
        case Panel::RowKind::GameSpeed:
            return layout_game_speed(font, scale, max_width, game_speed());
        case Panel::RowKind::Widescreen:
            return layout_widescreen(font, scale, max_width, current);
        case Panel::RowKind::FrameRate:
            return layout_frame_rate(font, scale, max_width, current);
        case Panel::RowKind::Resolution:
            return layout_resolution(font, scale, max_width, current);
        case Panel::RowKind::Antialias:
            return layout_antialias(font, scale, max_width, current);
        case Panel::RowKind::Display:
            return layout_display(font, scale, max_width, current);
        case Panel::RowKind::Mouse:
            return layout_mouse(font, scale, max_width, current);
        default:
            return layout_sound(font, scale, max_width, sound_mode_index());
    }
}

// Stores option `option` of a radio row. Returns the action the caller's
// `activate` or `choose_option` reports; an option that is already live is a
// no-op.
RowAction store_radio_row(Panel::RowKind kind, int option) {
    if (option < 0 || option >= radio_row_count(kind) || option == radio_row_current(kind)) {
        return RowAction::None;
    }
    if (Choice choice; choice_of(kind, choice)) {
        set_choice(choice, option);
        return RowAction::SettingChanged;
    }
    switch (kind) {
        case Panel::RowKind::GameSpeed:
            set_game_speed(game_speed_value(option));
            break;
        case Panel::RowKind::Widescreen:
            set_widescreen_mode(static_cast<WidescreenMode>(option));
            break;
        case Panel::RowKind::FrameRate:
            set_frame_rate_mode(static_cast<FrameRateMode>(option));
            break;
        case Panel::RowKind::Resolution:
            set_resolution_mode(static_cast<ResolutionMode>(option));
            break;
        case Panel::RowKind::Antialias:
            set_antialias_mode(static_cast<AntialiasMode>(option));
            break;
        case Panel::RowKind::Display:
            set_display_mode(static_cast<DisplayMode>(option));
            break;
        case Panel::RowKind::Sound:
            set_sound_mode(static_cast<SoundMode>(option));
            break;
        case Panel::RowKind::Mouse:
            set_mouse_mode(static_cast<MouseMode>(option));
            break;
        default:
            return RowAction::None;
    }
    return RowAction::SettingChanged;
}

// Steps a radio row by `direction`, wrapping at the ends, and stores the new
// option.
RowAction step_radio_row(Panel::RowKind kind, int direction) {
    const int count = radio_row_count(kind);
    if (count <= 0) {
        return RowAction::None;
    }
    int current = radio_row_current(kind);
    if (current < 0) {
        // A value that came from an environment override and is not one of the
        // offered steps starts the walk at the first step.
        current = 0;
    }
    current = ((current + direction) % count + count) % count;
    return store_radio_row(kind, current);
}

// --- slider rows (VOLUME) -----------------------------------------------------
// The volume row draws one text run, the bar with its handle and the percent,
// and then lays a click rect over each bar cell. The layout is a pure function
// of its arguments, so the measured and the drawn row agree, as with the radio
// rows above.

struct SliderLayout {
    std::string text;
    std::vector<SDL_Rect> cell_rects;  // relative to the row's top-left
    int height = 0;
};

SliderLayout layout_volume(const Font& font, int scale, int percent) {
    SliderLayout layout;
    layout.text = volume_text(percent);
    layout.height = scale * Font::kCellHeight;
    const int cell_width = font.width("-", scale);
    for (int cell = 0; cell < kVolumeCells; cell++) {
        layout.cell_rects.push_back(SDL_Rect{cell * cell_width, 0, cell_width, layout.height});
    }
    return layout;
}

// The GAME SPEED group as one string, for right_text (the drawn row uses the
// layout above; this is what a caller outside the panel reads).
std::string game_speed_text(int current) {
    std::string text;
    for (int i = 0; i < kGameSpeedCount; i++) {
        if (!text.empty()) {
            text += "  ";
        }
        text += std::string(game_speed_value(i) == current ? "[x] " : "[ ] ") +
                std::to_string(game_speed_value(i));
    }
    return text;
}

// The band a Chaos Frame value falls in, which is what it means: the endings
// are chosen by these ranges (developer, this session): 0-35 low, 36-64
// neutral, 65-100 high.
const char* chaos_frame_band(int value) {
    if (value <= 35) {
        return "LOW";
    }
    if (value <= 64) {
        return "NEUTRAL";
    }
    return "HIGH";
}

// --- row heights --------------------------------------------------------------
// One tab's row heights at the measured scale. `panel` supplies the right-column
// text, which depends on the panel's ROM, mods and input map. `right_lines` is
// filled only when `keep_lines` is set: only the active tab is drawn, so the
// layout reference tab needs its total height alone.

struct RowsLayout {
    std::vector<int> heights;
    // The wrapped lines of a section row's title (the left column of a section
    // spans the whole panel, because a section has no description) and of a
    // row's right column.
    std::vector<std::vector<std::string>> left_lines;
    std::vector<std::vector<std::string>> right_lines;
    int total = 0;
};

RowsLayout layout_rows(const Font& font, const Panel& panel, const std::vector<Panel::Row>& rows,
                       int scale, int row_height, int section_height, int description_width,
                       int full_width, int wrap_pad, bool keep_lines) {
    const int line_height = scale * Font::kCellHeight;
    RowsLayout layout;
    layout.heights.assign(rows.size(), row_height);
    if (keep_lines) {
        layout.left_lines.assign(rows.size(), {});
        layout.right_lines.assign(rows.size(), {});
    }
    for (size_t i = 0; i < rows.size(); i++) {
        const Panel::Row& row = rows[i];
        if (row.kind == Panel::RowKind::Section) {
            // A section row's title is drawn in the left column and has no
            // description, so it may use the whole panel and wrap there. A
            // one-line section keeps the height it had before this could wrap.
            std::vector<std::string> lines =
                wrap_text(row.title, chars_per_line(font, scale, full_width));
            const int count = static_cast<int>(lines.size());
            layout.heights[i] = std::max(section_height, count * line_height + (section_height - line_height));
            if (keep_lines) {
                layout.left_lines[i] = std::move(lines);
            }
        }
        else if (is_radio_row(row.kind)) {
            // The radio group is laid out by option, not wrapped as one string;
            // its height is what the row must reserve.
            const RadioLayout radio = radio_row_layout(
                row.kind, font, scale, description_width, radio_row_current(row.kind));
            layout.heights[i] = std::max(row_height, radio.height);
        }
        else if (row.kind == Panel::RowKind::Volume) {
            const SliderLayout slider = layout_volume(font, scale, slider_percent(slider_of(row)));
            layout.heights[i] = std::max(row_height, slider.height);
        }
        else {
            const std::string right = panel.right_text_for(row);
            if (!right.empty()) {
                std::vector<std::string> lines =
                    wrap_text(right, chars_per_line(font, scale, description_width));
                const int count = static_cast<int>(lines.size());
                layout.heights[i] = std::max(row_height, count * line_height + wrap_pad);
                if (keep_lines) {
                    layout.right_lines[i] = std::move(lines);
                }
            }
        }
        layout.total += layout.heights[i];
    }
    return layout;
}

}  // namespace

// --- TextLayer ----------------------------------------------------------------

TextLayer::TextLayer(TextLayer&& other) noexcept
    : texture_(other.texture_), width_(other.width_), height_(other.height_) {
    other.texture_ = nullptr;
}

TextLayer& TextLayer::operator=(TextLayer&& other) noexcept {
    if (this != &other) {
        destroy();
        texture_ = other.texture_;
        width_ = other.width_;
        height_ = other.height_;
        other.texture_ = nullptr;
    }
    return *this;
}

TextLayer::~TextLayer() {
    destroy();
}

void TextLayer::destroy() {
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

void TextLayer::build(SDL_Renderer* renderer, const Font& font, const std::string& text,
                      int scale, SDL_Color color, int supersample) {
    destroy();
    if (text.empty() || scale <= 0) {
        return;
    }
    // The drawing resolution has nothing to do with the window size, so
    // rounding the supersample factor to a multiple of `scale` costs nothing on
    // screen.
    if (supersample < scale) {
        supersample = scale;
    }
    supersample = (supersample / scale) * scale;
    const int render_scale = scale + supersample;

    const int base_width = font.width(text, render_scale);
    const int base_height = font.height(render_scale);
    SDL_Texture* target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_TARGET, base_width,
                                           base_height);
    if (target == nullptr) {
        return;
    }
    SDL_SetTextureBlendMode(target, SDL_BLENDMODE_BLEND);

    SDL_Texture* previous = SDL_GetRenderTarget(renderer);
    if (SDL_SetRenderTarget(renderer, target) != 0) {
        SDL_DestroyTexture(target);
        SDL_SetRenderTarget(renderer, previous);
        return;
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    font.draw(renderer, text, 0, 0, render_scale, color);
    SDL_SetRenderTarget(renderer, previous);

    width_ = (base_width * scale) / render_scale;
    height_ = (base_height * scale) / render_scale;
    SDL_SetTextureScaleMode(target, SDL_ScaleModeLinear);
    texture_ = target;
}

void TextLayer::draw(SDL_Renderer* renderer, int center_x, int left, int top,
                     bool centered) const {
    if (texture_ == nullptr || width_ <= 0) {
        return;
    }
    const float x = centered ? static_cast<float>(center_x - width_ / 2)
                             : static_cast<float>(left);
    const SDL_FRect dst{x, static_cast<float>(top), static_cast<float>(width_),
                        static_cast<float>(height_)};
    SDL_RenderCopyF(renderer, texture_, nullptr, &dst);
}

// --- text helpers -------------------------------------------------------------

std::vector<std::string> wrap_text(const std::string& text, size_t max_chars) {
    std::vector<std::string> lines;
    std::string line;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ') {
            ++pos;
        }
        size_t end = text.find(' ', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::string word = text.substr(pos, end - pos);
        pos = end;
        while (word.size() > max_chars) {
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
            lines.push_back(word.substr(0, max_chars));
            word.erase(0, max_chars);
        }
        if (word.empty()) {
            continue;
        }
        if (line.empty()) {
            line = word;
        }
        else if (line.size() + 1 + word.size() <= max_chars) {
            line += ' ';
            line += word;
        }
        else {
            lines.push_back(line);
            line = word;
        }
    }
    if (!line.empty()) {
        lines.push_back(line);
    }
    return lines;
}

int chars_per_line(const Font& font, int scale, int max_width_px) {
    const int advance = font.width("M", scale);
    if (advance <= 0) {
        return 40;
    }
    return std::max(12, max_width_px / advance);
}

std::string truncate_to_width(const Font& font, const std::string& text, int scale,
                              int max_width) {
    if (text.empty() || max_width <= 0 || font.width(text, scale) <= max_width) {
        return text;
    }
    const int advance = font.width("M", scale);
    if (advance <= 0) {
        return text;
    }
    const size_t max_chars = static_cast<size_t>(max_width / advance);
    if (max_chars == 0) {
        return {};
    }
    if (max_chars <= 3) {
        return text.substr(0, max_chars);
    }
    return text.substr(0, max_chars - 3) + "...";
}

// --- Panel --------------------------------------------------------------------

Panel::Panel(Mode mode, std::string game_id, std::filesystem::path rom)
    : mode_(mode), game_id_(std::move(game_id)), rom_(std::move(rom)) {
    // The overlay opens on MAIN (saves, exit), like the start screen.
    reload();
    selected_ = first_selectable();
    if (selected_ >= rows_.size()) {
        selected_ = 0;
    }
}

void Panel::set_rom(std::filesystem::path rom) {
    rom_ = std::move(rom);
}

bool Panel::tab_enabled(Tab tab) const {
    // The overlay shows the same tab bar. Its MODS tab lists the content
    // packs and scripts, which load while the game runs; the code mod rows,
    // which toggle mods the runtime loads before it boots, are on the start
    // screen only. MAIN stays active because EXIT GAME lives there, so closing
    // the panel is not the only way out of the game. The save profiles live on
    // MAIN; the SAVES tab is no longer shown.
    if (tab == Tab::Saves) {
        return false;
    }
    if (mode_ == Mode::Overlay) {
        return true;
    }
    // ROSTER reads the running game's roster, so the start screen has none.
    return tab != Tab::Roster;
}

std::string Panel::tab_label(Tab tab) const {
    switch (tab) {
        case Tab::Main:
            return "MAIN";
        case Tab::Mods:
            return "MODS";
        case Tab::Controls:
            return "CONTROLS";
        case Tab::Graphics:
            return "GRAPHICS";
        case Tab::Audio:
            return "AUDIO";
        case Tab::Gameplay:
            return "GAMEPLAY";
        case Tab::Saves:
            return {};  // hidden: the saves are on MAIN
        case Tab::Roster:
            return "ROSTER";
        case Tab::Debug:
            return "DEBUG";
        default:
            return {};
    }
}

void Panel::set_tab(Tab tab) {
    if (!tab_enabled(tab)) {
        return;
    }
    cancel_prompt();
    tab_ = tab;
    reload();
    selected_ = first_selectable();
    if (selected_ >= rows_.size()) {
        selected_ = 0;
    }
}

void Panel::switch_tab(int delta) {
    if (delta == 0) {
        return;
    }
    int index = static_cast<int>(tab_);
    for (int step = 0; step < kTabCount; step++) {
        index = ((index + delta) % kTabCount + kTabCount) % kTabCount;
        if (tab_enabled(static_cast<Tab>(index))) {
            set_tab(static_cast<Tab>(index));
            return;
        }
    }
}

void Panel::reload() {
    mods_ = recomp::mods::get_all_mod_details(game_id_);
    if (tab_ == Tab::Mods) {
        load_content_mods();
    }
    // A reload can change the row list under an armed capture; drop it rather
    // than leave it pointing at a row index that may no longer exist.
    capture_row_ = -1;
    title_armed_ = false;
    rebuild_rows();
    if (selected_ >= rows_.size() || !selectable(selected_)) {
        selected_ = first_selectable();
        if (selected_ >= rows_.size()) {
            selected_ = 0;
        }
    }
    geometry_ = Geometry{};
}

void Panel::rebuild_rows() {
    rows_ = build_rows(tab_);
    // The layout reference tab's rows are built alongside the active tab's, so
    // the fixed top edge cannot drift from what that tab would draw.
    layout_rows_ = build_rows(kLayoutReferenceTab);
}

// --- content mods (MODS tab) ---------------------------------------------------
// One table of the mods in load order: first the mod folders shipped in
// content/ (the Another World pack, design-another-world-pack.md), then what the
// player put into user-content/: mod folders, loose packs and scripts, and
// LordlyCaliber folders (content_files.hpp ModKind). A shipped mod moves only
// within its group. Deleting one keeps it OFF in mod-settings.json, so an
// update that puts the folder back leaves it off. The listing is read again here rather than taken from the content
// database, so a change the player just made shows at once; the database reads
// the same file within a second and then reports its conflicts and errors, and
// poll_content rebuilds the rows. While the game runs, the database's held
// items (content_db.hpp held_mods) are passed, so the tab shows what loads.

namespace {

// The cells of a mod row, left to right after the order number: the switch,
// the name (opens the details), up, down and delete. LEFT/RIGHT move between
// them on the selected row and ENTER or A uses the one in focus.
constexpr int kCellSwitch = 0;
constexpr int kCellDetails = 1;
constexpr int kCellUp = 2;
constexpr int kCellDown = 3;
constexpr int kCellDelete = 4;
constexpr int kCellCancel = 5;  // the confirmation dialog's Cancel

const char* kind_text(content::files::ModKind kind) {
    switch (kind) {
        case content::files::ModKind::Pack:
            return "a content pack file";
        case content::files::ModKind::Script:
            return "a Lua script file";
        case content::files::ModKind::Project:
            return "a LordlyCaliber project folder";
        default:
            return "a mod folder";
    }
}

}  // namespace

void Panel::load_content_mods() {
    namespace files = content::files;
    content_live_ = content::loaded();
    content_held_ = content_live_ ? content::held_mods() : std::map<std::string, bool>{};
    const files::Listing listing = files::scan(content_held_);
    content_settings_error_ = listing.settings_error;
    content_generation_ = content::generation();
    const std::map<std::string, int> classes = content_live_ ? content::class_counts() : std::map<std::string, int>{};
    const std::vector<std::pair<std::string, std::string>> scripts =
        content_live_ ? lua::status() : std::vector<std::pair<std::string, std::string>>{};
    content_class_errors_.clear();
    if (content_live_) {
        for (const auto& [left, right] : content::status()) {
            if (left == "New class") {
                content_class_errors_.push_back(right);
            }
        }
    }
    // A file id belongs to an item when it is the item's file or inside its
    // folder ("script/" marks a script's runtime pack).
    const auto in_item = [](std::string id, const std::string& folder) {
        if (id.starts_with("script/")) {
            id.erase(0, 7);
        }
        return id == folder || id.starts_with(folder + "/");
    };
    const auto file_name = [](const std::string& id) {
        const size_t slash = id.rfind('/');
        return slash == std::string::npos ? id : id.substr(slash + 1);
    };
    content_mods_.clear();
    for (const files::Mod& mod : listing.mods) {
        ContentModView view;
        view.mod = mod;
        int conflicts = 0;
        int errors = 0;
        std::vector<std::pair<std::string, std::string>> lines;
        if (content_live_) {
            for (const content::Pack& pack : content::packs()) {
                if (in_item(pack.id, mod.folder) && !pack.id.starts_with("script/")) {
                    std::string what = std::to_string(pack.edits.size()) + " edits";
                    if (!pack.names.empty()) {
                        what += ", " + std::to_string(pack.names.size()) + " names";
                    }
                    if (!pack.new_classes.empty()) {
                        what += ", " + std::to_string(pack.new_classes.size()) + " new classes";
                    }
                    if (!pack.not_applied.empty()) {
                        what += ", not applied yet: " + std::to_string(pack.not_applied.size()) + " parts";
                    }
                    lines.emplace_back("FILE", file_name(pack.id) + ": " + what);
                }
            }
            for (const auto& [script, line] : scripts) {
                if (!in_item(script, mod.folder)) {
                    continue;
                }
                if (line.starts_with("ERROR")) {
                    lines.emplace_back("SCRIPT ERROR", file_name(script) + ": " + line.substr(7));
                    errors++;
                } else {
                    lines.emplace_back("SCRIPT", file_name(script) + ": " + line);
                }
            }
            for (const content::FileError& e : content::file_errors()) {
                if (in_item(e.file, mod.folder)) {
                    lines.emplace_back("FILE REFUSED", file_name(e.file) + ": " + e.error);
                    errors++;
                }
            }
            for (const content::Conflict& c : content::conflicts()) {
                const std::string record = std::string(content::info(c.table).name) + " " + std::to_string(c.id);
                if (in_item(c.later, mod.folder)) {
                    lines.emplace_back("CONFLICT", record + ": wins over " + c.earlier);
                    conflicts++;
                } else if (in_item(c.earlier, mod.folder)) {
                    lines.emplace_back("CONFLICT", record + ": " + c.later + " wins over it");
                    conflicts++;
                }
            }
        }
        const std::string source = mod.chosen ? " (YOUR CHOICE)" : " (DEFAULT)";
        if (!mod.error.empty()) {
            view.status = (mod.error.starts_with("requires ") ? "MISSING REQUIREMENT: " : "REFUSED: ") + mod.error;
        } else if (mod.pending) {
            view.status = std::string("PENDING: ") + (mod.wanted ? "ON" : "OFF") + " AT THE NEXT START";
        } else if (!mod.enabled) {
            view.status = "OFF" + source;
        } else {
            view.status = content_live_ ? "LOADED" : "ON";
            if (conflicts > 0) {
                view.status += ", " + std::to_string(conflicts) + (conflicts == 1 ? " CONFLICT" : " CONFLICTS");
            }
            if (errors > 0) {
                view.status += ", " + std::to_string(errors) + (errors == 1 ? " ERROR" : " ERRORS");
            }
            view.status += source;
        }
        if (!mod.description.empty()) {
            view.info.emplace_back("ABOUT", mod.description);
        }
        std::string details = kind_text(mod.kind);
        if (mod.kind == files::ModKind::Folder) {
            details += mod.id.empty() ? std::string(" with no valid id") : ", id " + mod.id;
            if (!mod.version.empty()) {
                details += ", version " + mod.version;
            }
            if (!mod.author.empty()) {
                details += ", by " + mod.author;
            }
        }
        details += ": " + mod.folder;
        view.info.emplace_back("WHAT", details);
        view.info.emplace_back(
            "ON / OFF",
            mod.chosen ? std::string("Your choice, kept in mod-settings.json. Switching it back to ") +
                             (mod.enabled_default ? "ON" : "OFF") + " returns it to the default."
                       : std::string("The default") +
                             (mod.kind == files::ModKind::Folder
                                  ? std::string(" (mod.json \"enabled\": ") + (mod.enabled_default ? "true" : "false") + ")"
                                  : std::string()) +
                             ". You have not switched it.");
        if (!mod.required.empty()) {
            std::string list;
            for (const std::string& id : mod.required) {
                list += (list.empty() ? "" : ", ") + id;
            }
            view.info.emplace_back("REQUIRES", list + ". It always loads after them.");
        }
        if (!mod.error.empty()) {
            view.info.emplace_back(mod.error.starts_with("requires ") ? "MISSING" : "REFUSED", mod.error);
        }
        if (mod.shipped) {
            std::string text = "Comes with the game in content/. Its rules change what a save holds, so turning it on "
                               "or off applies at the next start. Once it is off and the game restarted, DELETE erases "
                               "it; if an update puts it back, it stays OFF.";
            if (mod.pending) {
                text += std::string(" Until then it stays ") + (mod.enabled ? "ON." : "OFF.");
            }
            view.info.emplace_back("SHIPPED", text);
        }
        if (const auto count = classes.find(mod.id); count != classes.end()) {
            std::string text = "Adds " + std::to_string(count->second) +
                               (count->second == 1 ? " new class" : " new classes") +
                               ". Its art is built when the game starts and class ids go into the saves, so "
                               "turning it on or off applies at the next start.";
            if (mod.pending) {
                text += std::string(" Until then it stays ") + (mod.enabled ? "ON." : "OFF.");
            }
            view.info.emplace_back("NEW CLASSES", text);
        }
        view.info.insert(view.info.end(), lines.begin(), lines.end());
        if (!mod.options.empty() && !mod.id.empty()) {
            view.values = mod_options::read_values(mod.id, mod.options);
        }
        content_mods_.push_back(std::move(view));
    }
}

void Panel::poll_content() {
    if (tab_ != Tab::Mods || content::generation() == content_generation_) {
        return;
    }
    load_content_mods();
    rebuild_rows();
    if (selected_ >= rows_.size() || !selectable(selected_)) {
        select_first();
    }
}

void Panel::select_content_row(const std::string& folder) {
    for (size_t i = 0; i < rows_.size(); i++) {
        if (rows_[i].kind == RowKind::ContentMod && rows_[i].title == folder) {
            selected_ = i;
            return;
        }
    }
    if (selected_ >= rows_.size() || !selectable(selected_)) {
        select_first();
    }
}

void Panel::change_mod_settings(const std::function<void(content::files::Settings&)>& change) {
    std::string error;
    content::files::Settings settings = content::files::read_settings(error);
    change(settings);
    content::files::write_settings(settings);
    load_content_mods();
    rebuild_rows();
}

// Sets an item's switch. A choice equal to the item's default is no choice:
// the key leaves the file, so (YOUR CHOICE) means "not the default".
void Panel::set_content_mod(content::files::Settings& settings, const content::files::Mod& mod, bool on) {
    if (on == mod.enabled_default) {
        settings.enabled.erase(mod.id);
    } else {
        settings.enabled[mod.id] = on;
    }
}

void Panel::reorder_content_mod(size_t from, size_t to) {
    // A mod moves only within its group: shipped mods always load first.
    if (from >= content_mods_.size() || to >= content_mods_.size() || from == to ||
        content_mods_[from].mod.shipped != content_mods_[to].mod.shipped) {
        return;
    }
    std::vector<std::string> ids;
    for (const ContentModView& view : content_mods_) {
        ids.push_back(view.mod.id);
    }
    const std::string moved = ids[from];
    ids.erase(ids.begin() + static_cast<std::ptrdiff_t>(from));
    ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(to), moved);
    change_mod_settings([&ids](content::files::Settings& settings) {
        std::vector<std::string> order;
        for (const std::string& id : ids) {
            if (!id.empty() && std::find(order.begin(), order.end(), id) == order.end()) {
                order.push_back(id);
            }
        }
        // Items that are not installed now keep their place at the end, so an
        // item taken out and put back keeps its order.
        for (const std::string& id : settings.order) {
            if (std::find(order.begin(), order.end(), id) == order.end()) {
                order.push_back(id);
            }
        }
        if (order.size() > content::files::kMaxSettingsIds) {
            order.resize(content::files::kMaxSettingsIds);
        }
        settings.order = std::move(order);
    });
}

void Panel::delete_content_mod(size_t which) {
    const content::files::Mod mod = content_mods_[which].mod;
    const bool held_on = content_held_.count(mod.id) != 0 && mod.enabled;
    if (content_live_ && held_on) {
        // The dialog explains it and offers OK alone.
        erase_armed_ = mod.folder;
        delete_blocked_ = true;
        mod_message_.clear();
        return;
    }
    if (erase_armed_ != mod.folder) {
        erase_armed_ = mod.folder;  // the menu shows the confirmation dialog
        delete_blocked_ = false;
        mod_message_.clear();
        return;
    }
    erase_armed_.clear();
    std::string error;
    if (!content::files::erase(mod, error)) {
        mod_message_ = "Not deleted: " + error;
        return;
    }
    mod_message_ = "Deleted " + mod.folder + ".";
    open_content_mods_.erase(mod.folder);
    if (mod.shipped) {
        // An update puts a shipped mod back; the kept OFF leaves it off.
        mod_message_ += " If an update puts it back, it stays OFF.";
        change_mod_settings([&mod](content::files::Settings& settings) { settings.enabled[mod.id] = false; });
    } else if (!mod.id.empty()) {
        change_mod_settings([&mod](content::files::Settings& settings) {
            settings.enabled.erase(mod.id);
            settings.order.erase(std::remove(settings.order.begin(), settings.order.end(), mod.id),
                                 settings.order.end());
        });
    } else {
        load_content_mods();
        rebuild_rows();
    }
}

RowAction Panel::use_content_cell(size_t index, int cell) {
    const size_t which = rows_[index].mod;
    if (which >= content_mods_.size()) {
        return RowAction::None;
    }
    const std::string folder = rows_[index].title;
    if (cell == kCellCancel || delete_blocked_) {
        // Cancel, or any press while the dialog only explains a refusal.
        cancel_prompt();
        return RowAction::SettingChanged;
    }
    if (cell != kCellDelete) {
        erase_armed_.clear();
        mod_message_.clear();
    }
    const content::files::Mod mod = content_mods_[which].mod;
    mod_cell_ = std::clamp(cell, kCellSwitch, kCellDelete);
    switch (mod_cell_) {
        case kCellSwitch:
            if (mod.id.empty()) {
                return RowAction::None;
            }
            change_mod_settings([&](content::files::Settings& settings) { set_content_mod(settings, mod, !mod.wanted); });
            break;
        case kCellDetails:
            if (open_content_mods_.count(folder) != 0) {
                open_content_mods_.erase(folder);
            }
            else {
                open_content_mods_.insert(folder);
            }
            rebuild_rows();
            break;
        case kCellUp:
        case kCellDown: {
            const int to = static_cast<int>(which) + (mod_cell_ == kCellUp ? -1 : 1);
            if (to < 0 || to >= static_cast<int>(content_mods_.size()) ||
                content_mods_[static_cast<size_t>(to)].mod.shipped != mod.shipped) {
                return RowAction::None;
            }
            reorder_content_mod(which, static_cast<size_t>(to));
            break;
        }
        default:
            delete_content_mod(which);
            rebuild_rows();
            break;
    }
    select_content_row(folder);
    return RowAction::SettingChanged;
}

RowAction Panel::step(size_t index, int direction) {
    if (index < rows_.size() && rows_[index].kind == RowKind::ContentMod) {
        const int cell = std::clamp(mod_cell_ + (direction >= 0 ? 1 : -1), kCellSwitch, kCellDelete);
        if (cell != kCellDelete) {
            erase_armed_.clear();
        }
        mod_cell_ = cell;
        return RowAction::None;
    }
    return row_is_steppable(index) ? activate(index, direction) : RowAction::None;
}

RowAction Panel::drag_content_mod(size_t from_row, size_t to_row) {
    if (from_row >= rows_.size() || to_row >= rows_.size() || rows_[from_row].kind != RowKind::ContentMod) {
        return RowAction::None;
    }
    // A drop on a details line counts as a drop on its item.
    const size_t from = rows_[from_row].mod;
    const size_t to = rows_[to_row].kind == RowKind::ContentMod || rows_[to_row].kind == RowKind::ContentModInfo
                          ? rows_[to_row].mod
                          : from;
    const std::string folder = rows_[from_row].title;
    erase_armed_.clear();
    mod_message_.clear();
    reorder_content_mod(from, to);
    select_content_row(folder);
    return RowAction::SettingChanged;
}

std::string Panel::delete_prompt() const {
    const int row = delete_prompt_row();
    if (row < 0) {
        return {};
    }
    const content::files::Mod& mod = content_mods_[rows_[static_cast<size_t>(row)].mod].mod;
    if (delete_blocked_) {
        return mod.shipped ? mod.name + " ships with the game and is loaded this session. Turn it off, restart the "
                                        "game, then it can be deleted."
                           : mod.name + " adds new classes and is loaded, and a save may hold its classes. Turn it "
                                        "off, restart the game, then it can be deleted.";
    }
    const std::string what = mod.kind == content::files::ModKind::Folder || mod.kind == content::files::ModKind::Project
                                 ? "the folder " + mod.folder + " and everything in it"
                                 : "the file " + mod.folder;
    return mod.name + ": " + what + " will be erased from this computer. This cannot be undone." +
           (mod.shipped ? " If an update puts it back, it stays OFF." : "");
}

int Panel::delete_prompt_row() const {
    if (erase_armed_.empty()) {
        return -1;
    }
    for (size_t i = 0; i < rows_.size(); i++) {
        if (rows_[i].kind == RowKind::ContentMod && rows_[i].title == erase_armed_ &&
            rows_[i].mod < content_mods_.size()) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Panel::cancel_prompt() {
    if (erase_armed_.empty()) {
        return false;
    }
    erase_armed_.clear();
    delete_blocked_ = false;
    mod_message_.clear();
    return true;
}

const mod_options::Option* Panel::mod_option(const Row& row) const {
    if (row.kind != RowKind::ContentModOption || row.mod >= content_mods_.size()) {
        return nullptr;
    }
    const ContentModView& view = content_mods_[row.mod];
    if (row.option >= view.mod.options.size() || view.values.count(view.mod.options[row.option].key) == 0) {
        return nullptr;
    }
    return &view.mod.options[row.option];
}

RowAction Panel::set_mod_option(size_t index, int64_t value) {
    const mod_options::Option* option = mod_option(rows_[index]);
    if (option == nullptr || !mod_options::valid_value(*option, value)) {
        return RowAction::None;
    }
    ContentModView& view = content_mods_[rows_[index].mod];
    if (!mod_options::write_value(view.mod.id, view.mod.options, option->key, value)) {
        mod_message_ = "Could not save " + option->name + " for " + view.mod.name + ".";
        return RowAction::SettingChanged;
    }
    view.values[option->key] = value;
    return RowAction::SettingChanged;
}

bool Panel::content_mod_open(size_t index) const {
    return index < rows_.size() && rows_[index].kind == RowKind::ContentMod &&
           open_content_mods_.count(rows_[index].title) != 0;
}

Panel::ModCells Panel::content_mod_cells(size_t index) const {
    ModCells out;
    if (index >= rows_.size() || rows_[index].kind != RowKind::ContentMod || rows_[index].mod >= content_mods_.size()) {
        return out;
    }
    const ContentModView& view = content_mods_[rows_[index].mod];
    out.number = std::to_string(rows_[index].mod + 1);
    out.name = view.mod.name + (view.mod.version.empty() ? "" : " " + view.mod.version);
    const std::string& text = view.mod.description;
    if (text.size() <= 110) {
        out.note = text;
    }
    else {
        size_t cut = 107;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
            cut--;  // not inside a UTF-8 sequence
        }
        out.note = text.substr(0, cut) + "...";
    }
    out.status = view.status;
    out.tone = !view.mod.error.empty() ? "bad"
               : view.mod.pending      ? "pending"
               : !view.mod.enabled     ? "off"
               : view.status.find("CONFLICT") != std::string::npos || view.status.find("ERROR") != std::string::npos
                   ? "warn"
                   : "on";
    out.on = view.mod.wanted;
    out.open = open_content_mods_.count(view.mod.folder) != 0;
    out.armed = erase_armed_ == view.mod.folder;
    // The first and last of its group: a mod moves only within it.
    const size_t which = rows_[index].mod;
    out.first = which == 0 || content_mods_[which - 1].mod.shipped != view.mod.shipped;
    out.last = which + 1 == content_mods_.size() || content_mods_[which + 1].mod.shipped != view.mod.shipped;
    out.cell = index == selected_ ? mod_cell_ : -1;
    return out;
}

// MAIN, in the overlay: the port's save profiles (docs/anotherworld/design-save-system.md).
// SAVE HERE, then one section per profile (the leader's name), newest first.
void Panel::append_profile_rows(std::vector<Row>& rows) const {
    rows.push_back(Row{RowKind::SaveHere, {}, 0, 0, -1});
    profile_entries_ = save_profiles::list(200);

    std::vector<std::string> profiles;
    for (const save_profiles::Entry& e : profile_entries_) {
        if (std::find(profiles.begin(), profiles.end(), e.profile) == profiles.end()) {
            profiles.push_back(e.profile);
        }
    }
    if (profiles.empty()) {
        rows.push_back(Row{RowKind::Section, "NO SAVES YET", 0, 0, -1});
    }
    if (!profiles_seeded_ && !profiles.empty()) {
        profiles_seeded_ = true;
        open_profiles_.insert(profiles.front());  // the newest save's profile
    }
    for (const std::string& profile : profiles) {
        size_t saves = 0;
        for (const save_profiles::Entry& e : profile_entries_) {
            saves += e.profile == profile ? 1 : 0;
        }
        rows.push_back(Row{RowKind::ProfileHeader, profile, 0, saves, -1});
        if (open_profiles_.count(profile) == 0) {
            continue;
        }
        for (size_t i = 0; i < profile_entries_.size(); i++) {
            if (profile_entries_[i].profile == profile) {
                rows.push_back(Row{RowKind::ProfileLoad, {}, 0, i, -1});
            }
        }
    }
}

std::vector<Panel::Row> Panel::build_rows(Tab tab) const {
    std::vector<Row> rows;
    switch (tab) {
        case Tab::Main:
            // The ROM row lives here: with no ROM loaded it is the first
            // selectable row, so the start screen can still pick one. EXIT GAME
            // is last, so the row a stray SPACE or ENTER lands on is Start Game
            // in the launcher and Exit Game in the overlay, whose Start Game row
            // is not selectable.
            rows.push_back(Row{RowKind::StartGame, {}, 0, 0, -1});
            rows.push_back(Row{RowKind::Rom, {}, 0, 0, -1});
            if (mode_ == Mode::Overlay) {
                rows.push_back(Row{RowKind::ReturnToTitle, {}, 0, 0, -1});
            }
            rows.push_back(Row{RowKind::ExitGame, {}, 0, 0, -1});
            // SAVES: a backup is safe at any time; a restore or an import only
            // before the game has read the save, so only on the start screen.
            rows.push_back(Row{RowKind::Section, "SAVES", 0, 0, -1});
            rows.push_back(Row{RowKind::SaveBackup, {}, 0, 0, -1});

            if (mode_ == Mode::Launcher) {
                rows.push_back(Row{RowKind::SaveImport, {}, 0, 0, -1});
                for (const std::string& name : list_save_backups()) {
                    rows.push_back(Row{RowKind::SaveRestore, name, 0, 0, -1});
                }
            }
            else {
                append_profile_rows(rows);
            }
            break;

        case Tab::Mods: {
            if (mode_ == Mode::Launcher) {
                rows.push_back(Row{RowKind::Section, "CODE MODS (mods/)", 0, 0, -1});
                if (mods_.empty()) {
                    rows.push_back(Row{RowKind::Section, "(none installed)", 0, 0, -1});
                }
                for (size_t mod = 0; mod < mods_.size(); mod++) {
                    rows.push_back(Row{RowKind::Mod, {}, mod, 0, -1});
                    const recomp::config::ConfigSchema& schema =
                        recomp::mods::get_mod_config_schema(mods_[mod].mod_id);
                    for (size_t option = 0; option < schema.options.size(); option++) {
                        if (schema.options[option].hidden) {
                            continue;
                        }
                        rows.push_back(Row{RowKind::Option, {}, mod, option, -1});
                    }
                }
            }
            // The mods (docs/anotherworld/design-mods.md section 5), in load
            // order: the shipped group (content/), then the player's
            // (user-content/).
            if (!content_settings_error_.empty()) {
                rows.push_back(Row{RowKind::Section,
                                   "YOUR MOD SETTINGS (mod-settings.json) WERE IGNORED: " + content_settings_error_ +
                                       ". EVERY MOD USES ITS DEFAULT. A CHANGE HERE WRITES A NEW FILE.",
                                   0, 0, -1});
            }
            size_t player_mods = 0;
            for (const ContentModView& view : content_mods_) {
                player_mods += view.mod.shipped ? 0 : 1;
            }
            const bool any_shipped = player_mods < content_mods_.size();
            if (any_shipped) {
                rows.push_back(Row{RowKind::Section, "SHIPPED WITH THE GAME (content/): ON OR OFF AT THE NEXT START", 0,
                                   0, -1});
                rows.push_back(Row{RowKind::ContentModsHeader, {}, 0, 0, -1});
            }
            int on = 0;
            int off = 0;
            int refused = 0;
            int pending = 0;
            for (size_t i = 0; i < content_mods_.size(); i++) {
                const content::files::Mod& mod = content_mods_[i].mod;
                if (!mod.shipped && (i == 0 || content_mods_[i - 1].mod.shipped)) {
                    rows.push_back(Row{RowKind::Section, "MODS (user-content/)", 0, 0, -1});
                    if (!any_shipped) {
                        rows.push_back(Row{RowKind::ContentModsHeader, {}, 0, 0, -1});
                    }
                }
                if (mod.shipped) {
                    rows.push_back(Row{RowKind::ContentMod, mod.folder, i, 0, -1});
                    if (open_content_mods_.count(mod.folder) != 0) {
                        if (!mod.url.empty()) {
                            rows.push_back(Row{RowKind::ContentModLink, mod.folder, i, 0, -1});
                        }
                        for (const auto& [left, right] : content_mods_[i].info) {
                            Row row{RowKind::ContentModInfo, left, i, 0, -1};
                            row.detail = right;
                            rows.push_back(row);
                        }
                        for (size_t o = 0; o < content_mods_[i].values.size() && o < mod.options.size(); o++) {
                            rows.push_back(Row{RowKind::ContentModOption, mod.folder, i, o, -1});
                        }
                    }
                    continue;
                }
                refused += mod.error.empty() ? 0 : 1;
                on += mod.error.empty() && mod.enabled ? 1 : 0;
                off += mod.error.empty() && !mod.enabled ? 1 : 0;
                pending += mod.pending ? 1 : 0;
                rows.push_back(Row{RowKind::ContentMod, mod.folder, i, 0, -1});
                if (open_content_mods_.count(mod.folder) == 0) {
                    continue;
                }
                if (!mod.url.empty()) {
                    rows.push_back(Row{RowKind::ContentModLink, mod.folder, i, 0, -1});
                }
                for (const auto& [left, right] : content_mods_[i].info) {
                    Row row{RowKind::ContentModInfo, left, i, 0, -1};
                    row.detail = right;
                    rows.push_back(row);
                }
                for (size_t o = 0; o < content_mods_[i].values.size() && o < mod.options.size(); o++) {
                    rows.push_back(Row{RowKind::ContentModOption, mod.folder, i, o, -1});
                }
            }
            if (player_mods == 0) {
                rows.push_back(Row{RowKind::Section, "MODS (user-content/)", 0, 0, -1});
                rows.push_back(Row{RowKind::Section,
                                   "NOTHING INSTALLED. PUT A MOD FOLDER, A .JSON PACK OR A .LUA SCRIPT IN user-content/ "
                                   "IN THE SETTINGS FOLDER.",
                                   0, 0, -1});
            }
            else {
                rows.push_back(Row{RowKind::ContentModsAll, {}, 0, 0, -1});
                rows.push_back(Row{RowKind::ContentModsNone, {}, 0, 0, -1});
                std::string counts = std::to_string(player_mods) + " INSTALLED, " + std::to_string(on) +
                                     " ON, " + std::to_string(off) + " OFF";
                if (refused > 0) {
                    counts += ", " + std::to_string(refused) + " REFUSED";
                }
                if (pending > 0) {
                    counts += ", " + std::to_string(pending) + " PENDING THE NEXT START";
                }
                rows.push_back(Row{RowKind::Section, counts, 0, 0, -1});
            }
            for (const std::string& error : content_class_errors_) {
                rows.push_back(Row{RowKind::Section, "NEW CLASS REFUSED: " + error, 0, 0, -1});
            }
            break;
        }

        case Tab::Controls:
            rows.push_back(Row{RowKind::Section, "MOUSE", 0, 0, -1});
            rows.push_back(Row{RowKind::Mouse, {}, 0, 0, -1});
            append_choices(rows, "CONTROLS", "MOUSE");
            rows.push_back(Row{RowKind::Section, "GAMEPAD", 0, 0, -1});
            append_choices(rows, "CONTROLS", "GAMEPAD");
            rows.push_back(Row{RowKind::Section, "CAMERA", 0, 0, -1});
            append_choices(rows, "CONTROLS", "CAMERA");
            rows.push_back(Row{RowKind::Section, "ACTIONS: KEYBOARD AND GAMEPAD", 0, 0, -1});
            for (int i = 0; i < kN64ButtonCount; i++) {
                rows.push_back(Row{RowKind::Binding, {}, 0, 0, i});
            }
            rows.push_back(Row{RowKind::ResetBindings, {}, 0, 0, -1});
            rows.push_back(Row{RowKind::Section, "LEFT STICK: MOVE THE CURSOR (ANALOG)", 0, 0, -1});
            rows.push_back(Row{RowKind::Section, "RIGHT STICK: CAMERA UP/DOWN, LEFT FOR THE OVERVIEW MAP; HOLD LT: ZOOM", 0, 0, -1});
            rows.push_back(Row{RowKind::Section,
                               "MODERN FIELD: RIGHT STICK PANS THE CAMERA; HOLD LT: RIGHT STICK ZOOMS, D-PAD IS C-UP/DOWN/LEFT/RIGHT; "
                               "CURSOR KEYS PAN THE CAMERA",
                               0, 0, -1});
            // The panel's own pad controls. They are not bindings: they act on
            // the panel, and the game does not see the pad while the overlay is
            // open. The SELECT button opens and closes the overlay only while no
            // row above binds it (docs/guides/app-build.md -> "The CONTROLS
            // tab"). One section row: a section title wraps and may use the whole
            // panel, and a section is skipped by the selection.
            rows.push_back(Row{RowKind::Section,
                               "MENU PAD NAVIGATION: D-PAD MOVE, A SELECT, SELECT MENU, "
                               "LB/RB SWITCH TAB",
                               0, 0, -1});
            break;

        case Tab::Graphics:
            rows.push_back(Row{RowKind::Section, "DISPLAY", 0, 0, -1});
            rows.push_back(Row{RowKind::Display, {}, 0, 0, -1});
            rows.push_back(Row{RowKind::FrameRate, {}, 0, 0, -1});
            append_choices(rows, "GRAPHICS", "DISPLAY");
            rows.push_back(Row{RowKind::Section, "RENDERING", 0, 0, -1});
            // RESOLUTION and ANTI-ALIASING stay hidden when a run sets
            // OGRE_DISPLAY_EXPERIMENTS=0 (settings.hpp -> display_experiments_enabled).
            if (display_experiments_enabled()) {
                rows.push_back(Row{RowKind::Resolution, {}, 0, 0, -1});
                rows.push_back(Row{RowKind::Antialias, {}, 0, 0, -1});
            }
            rows.push_back(Row{RowKind::Widescreen, {}, 0, 0, -1});
            append_choices(rows, "GRAPHICS", "RENDERING");
            rows.push_back(Row{RowKind::Section, "HD ASSETS", 0, 0, -1});
            append_choices(rows, "GRAPHICS", "HD ASSETS");
            rows.push_back(Row{RowKind::Section, "MENUS", 0, 0, -1});
            append_choices(rows, "GRAPHICS", "MENUS");
            break;

        case Tab::Audio:
            rows.push_back(Row{RowKind::Sound, {}, 0, 0, -1});
            rows.push_back(Row{RowKind::Volume, {}, 0, 0, -1});
            rows.push_back(Row{RowKind::Volume, {}, 0, 0, 1});
            rows.push_back(Row{RowKind::Volume, {}, 0, 0, 2});
            break;

        case Tab::Gameplay:
            // PORT MENUS first: CLASSIC puts back the game's own cursor and
            // menus, and the two rows under it act only with MODERN.
            rows.push_back(Row{RowKind::Section, "INTERFACE", 0, 0, -1});
            append_choices(rows, "GAMEPLAY", "INTERFACE");
            rows.push_back(Row{RowKind::Section, "START", 0, 0, -1});
            append_choices(rows, "GAMEPLAY", "START");
            rows.push_back(Row{RowKind::Section, "PACE", 0, 0, -1});
            rows.push_back(Row{RowKind::GameSpeed, {}, 0, 0, -1});
            append_choices(rows, "GAMEPLAY", "PACE");
            rows.push_back(Row{RowKind::Section, "AUTO COMMANDER", 0, 0, -1});
            append_choices(rows, "GAMEPLAY", "AUTO COMMANDER");
            rows.push_back(Row{RowKind::Section, "WAR DIRECTOR", 0, 0, -1});
            append_choices(rows, "GAMEPLAY", "WAR DIRECTOR");
            rows.push_back(Row{RowKind::Section, "ENEMY UNITS", 0, 0, -1});
            append_choices(rows, "GAMEPLAY", "ENEMY UNITS");
            // SHARED EXP, CLASS ALIGNMENT and LARGE UNITS are the Another World
            // pack's rules now (MODS tab; design-another-world-pack.md).
            rows.push_back(Row{RowKind::Section, "WAR DIRECTOR AND LOOT CHANGES APPLY FROM THE NEXT MISSION", 0, 0, -1});
            // The game's own Settings menu (stereo / mono left to the AUDIO tab).
            rows.push_back(Row{RowKind::Section, "GAME SETTINGS", 0, 0, -1});
            for (size_t i = 0; i < game_settings().size(); i++) {
                rows.push_back(Row{RowKind::GameOption, {}, 0, i, -1});
            }
            break;

        case Tab::Saves:
            break;

        case Tab::Roster: {
            rows.push_back(Row{RowKind::RosterSort, {}, 0, 0, -1});
            std::vector<army_roster::Character> list = army_roster::read();
            if (list.empty()) {
                rows.push_back(Row{RowKind::Section, "NO ROSTER YET", 0, 0, -1});
                break;
            }
            std::stable_sort(list.begin(), list.end(),
                             [this](const army_roster::Character& a, const army_roster::Character& b) {
                                 if (roster_sort_ == 1) {
                                     return a.class_name != b.class_name ? a.class_name < b.class_name
                                                                         : a.level > b.level;
                                 }
                                 if (roster_sort_ == 2) {
                                     return a.name < b.name;
                                 }
                                 return a.level > b.level;
                             });
            constexpr int kPerPage = 10;
            const int pages = (static_cast<int>(list.size()) + kPerPage - 1) / kPerPage;
            const int page = std::clamp(roster_page_, 0, pages - 1);
            const size_t first = static_cast<size_t>(page * kPerPage);
            const size_t last = std::min(list.size(), first + kPerPage);
            for (size_t i = first; i < last; i++) {
                Row row{RowKind::RosterChar, {}, 0, 0, -1};
                row.record = list[i].record;
                row.title = list[i].name;
                char detail[64];
                std::snprintf(detail, sizeof(detail), "%s  LV %d", list[i].class_name.c_str(),
                              list[i].level);
                row.detail = detail;
                rows.push_back(row);
            }
            Row next{RowKind::RosterPage, {}, 0, 0, -1};
            char text[48];
            std::snprintf(text, sizeof(text), "PAGE %d / %d  (%zu CHARACTERS)", page + 1, pages,
                          list.size());
            next.title = text;
            next.detail = pages > 1 ? "SPACE: NEXT PAGE" : "";
            next.target = pages;
            rows.push_back(next);
            break;
        }

        case Tab::Debug:
            rows.push_back(Row{RowKind::Section, "GAME STATE", 0, 0, -1});
            rows.push_back(Row{RowKind::ChaosFrame, {}, 0, 0, -1});
            rows.push_back(Row{RowKind::Section, "WAR DIRECTOR (NOTHING SPAWNS YET)", 0, 0, -1});
            for (int line = 0; line < 9; line++) {
                Row row{RowKind::DirectorLine, {}, 0, 0, -1};
                row.record = line;
                rows.push_back(row);
            }
            break;

        default:
            break;
    }
    return rows;
}

size_t Panel::first_selectable() const {
    for (size_t index = 0; index < rows_.size(); index++) {
        if (selectable(index)) {
            return index;
        }
    }
    return rows_.size();
}

bool Panel::profile_open(size_t index) const {
    return index < rows_.size() && rows_[index].kind == RowKind::ProfileHeader &&
           open_profiles_.count(rows_[index].title) != 0;
}

bool Panel::selectable(size_t index) const {
    if (index >= rows_.size()) {
        return false;
    }
    const Row& row = rows_[index];
    if (row.kind == RowKind::Section || row.kind == RowKind::ContentModInfo ||
        row.kind == RowKind::ContentModsHeader) {
        return false;
    }
    if (row.kind == RowKind::StartGame) {
        // The overlay's MAIN tab cannot start a game that is already running, so
        // its Start Game row is inert there and the selection skips it.
        return mode_ != Mode::Overlay && !rom_.empty();
    }
    return true;
}

bool Panel::selected_is_rom() const {
    return selected_ < rows_.size() && rows_[selected_].kind == RowKind::Rom;
}

bool Panel::selected_is_exit() const {
    return selected_ < rows_.size() && rows_[selected_].kind == RowKind::ExitGame;
}

Panel::RowKind Panel::row_kind(size_t index) const {
    return index < rows_.size() ? rows_[index].kind : RowKind::Section;
}

void Panel::set_selected(size_t index) {
    if (index < rows_.size()) {
        selected_ = index;
    }
}

void Panel::select_first() {
    selected_ = first_selectable();
    if (selected_ >= rows_.size()) {
        selected_ = 0;
    }
}

bool Panel::move(int delta) {
    cancel_prompt();
    mod_message_.clear();
    const int count = static_cast<int>(rows_.size());
    if (count == 0) {
        return false;
    }
    int index = static_cast<int>(selected_);
    for (int step = 0; step < count; step++) {
        index = ((index + delta) % count + count) % count;
        if (selectable(static_cast<size_t>(index))) {
            selected_ = static_cast<size_t>(index);
            return true;
        }
    }
    return false;
}

bool Panel::row_is_steppable(size_t index) const {
    if (index >= rows_.size()) {
        return false;
    }
    const RowKind kind = rows_[index].kind;
    return is_radio_row(kind) || kind == RowKind::Volume || kind == RowKind::Option || kind == RowKind::GameOption ||
           kind == RowKind::ProfileLoad || kind == RowKind::ContentMod || kind == RowKind::ContentModOption;
}

std::string Panel::row_tooltip(size_t index) const {
    if (index >= rows_.size()) {
        return {};
    }
    const Row& row = rows_[index];
    if (Choice choice; choice_of(row.kind, choice)) {
        return options::help(choice);
    }
    switch (row.kind) {
        case RowKind::GameOption:
            return row.option < game_settings().size() ? game_settings()[row.option].help : "";
        case RowKind::ContentModOption: {
            const mod_options::Option* option = mod_option(row);
            if (option == nullptr) {
                return {};
            }
            std::string text = option->help.empty() ? option->name : option->help;
            if (option->restart) {
                text += " Applies at the next start.";
            }
            return text;
        }
        case RowKind::SaveHere: {
            const std::string message = save_profiles::last_message();
            return !message.empty() ? message
                                    : "Saves into the leader's profile: a normal save on the world map, a suspend on the "
                                      "field and in battle. Your own save slots are left as they are.";
        }
        case RowKind::ProfileLoad:
            return !save_message_.empty() ? save_message_
                                          : "LOAD continues from this save right away. DELETE asks twice.";
        case RowKind::ProfileHeader:
            return "Shows or hides this character's saves.";
        case RowKind::ContentMod:
            return !erase_armed_.empty()
                       ? delete_prompt() + (delete_blocked_ ? " Any key closes this."
                                                            : " ENTER or A again deletes it; anything else cancels.")
                   : !mod_message_.empty()
                       ? mod_message_
                   : row.mod < content_mods_.size() && content_mods_[row.mod].mod.shipped
                       ? "Comes with the game. Left/Right: pick ON, DETAILS, UP, DOWN or DELETE. Enter or A: use it. ON "
                         "or OFF applies at the next start. Your own mods load after it and win when two change the "
                         "same record."
                       : "Left/Right: pick ON, DETAILS, UP, DOWN or DELETE. Enter or A: use it. Drag a row with the "
                         "mouse to move it. A later mod wins when two change the same record.";
        case RowKind::ContentModsAll:
        case RowKind::ContentModsNone:
            return "Sets every mod in user-content/; mods that add new classes change at the next start.";
        case RowKind::ContentModLink:
            return !mod_message_.empty() ? mod_message_
                                         : "ENTER, A or a click opens the mod's page in your web browser.";
        case RowKind::GameSpeed:
            return "The speed of the whole game (the field, menus and scenes). BATTLE SPEED can set battles apart.";
        default:
            return {};
    }
}

Panel::PadCommand Panel::pad_command_for_button(int button) {
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return PadCommand::Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return PadCommand::Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return PadCommand::Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return PadCommand::Right;
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_START:
            return PadCommand::Accept;
        case SDL_CONTROLLER_BUTTON_B:
            return PadCommand::Back;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            return PadCommand::TabPrev;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            return PadCommand::TabNext;
        default:
            return PadCommand::None;
    }
}

RowAction Panel::pad_command(PadCommand command) {
    const size_t index = selected_;
    switch (command) {
        case PadCommand::Up:
            move(-1);
            return RowAction::None;
        case PadCommand::Down:
            move(1);
            return RowAction::None;
        case PadCommand::Left:
        case PadCommand::Right:
            return step(index, command == PadCommand::Right ? 1 : -1);
        case PadCommand::Accept:
            if (index >= rows_.size() || !selectable(index)) {
                return RowAction::None;
            }
            return activate(index, 1);
        case PadCommand::Back:
            // B cancels a waiting DELETE. Otherwise the overlay closes on Back
            // and the launcher ignores it; neither is the panel's decision.
            return cancel_prompt() ? RowAction::SettingChanged : RowAction::None;
        case PadCommand::TabPrev:
            switch_tab(-1);
            return RowAction::None;
        case PadCommand::TabNext:
            switch_tab(1);
            return RowAction::None;
        case PadCommand::None:
        default:
            return RowAction::None;
    }
}

std::string Panel::left_text(size_t index) const {
    if (index >= rows_.size()) {
        return {};
    }
    const Row& row = rows_[index];
    if (Choice choice; choice_of(row.kind, choice)) {
        return options::title(choice);
    }
    switch (row.kind) {
        case RowKind::GameOption:
            return row.option < game_settings().size() ? game_settings()[row.option].name : "";
        case RowKind::ContentModOption: {
            const mod_options::Option* option = mod_option(row);
            if (option == nullptr) {
                return {};
            }
            const ContentModView& view = content_mods_[row.mod];
            const int64_t stored = view.values.at(option->key);
            const int64_t now = mod_options::session_peek(view.mod.id, *option, stored);
            return "  " + option->name +
                   (now != stored ? " (NOW " + mod_options::value_text(*option, now) + " UNTIL THE NEXT START)" : "");
        }
        case RowKind::Section:
            return row.title;
        case RowKind::StartGame:
            return rom_.empty() ? "[ ] Start Game" : "[x] Start Game";
        case RowKind::ExitGame:
            return "[ ] Exit Game";
        case RowKind::ReturnToTitle:
            return title_armed_ ? "[x] Return to Title?" : "[ ] Return to Title";
        case RowKind::SaveHere:
            return save_profiles::save_pending() ? "Save here (when the game resumes)" : "Save here";
        case RowKind::ProfileHeader: {
            std::string title = row.title;
            for (char& c : title) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            return title;
        }
        case RowKind::ProfileLoad: {
            if (row.option >= profile_entries_.size()) {
                return {};
            }
            const save_profiles::Entry& e = profile_entries_[row.option];
            const std::string& s = e.stamp;  // YYYYMMDD-HHMMSS
            const std::string when = s.size() >= 13 ? s.substr(0, 4) + "-" + s.substr(4, 2) + "-" + s.substr(6, 2) +
                                                          " " + s.substr(9, 2) + ":" + s.substr(11, 2)
                                                    : s;
            return when + "  " + e.where;
        }
        case RowKind::SaveBackup:
            return "Back up save";
        case RowKind::SaveImport:
            return "Import save";
        case RowKind::SaveRestore:
            return "Restore " + row.title;
        case RowKind::Rom:
            return rom_.empty() ? "[ ] No ROM" : "[x] ROM loaded";
        case RowKind::Mod: {
            const recomp::mods::ModDetails& mod = mods_[row.mod];
            const bool enabled = recomp::mods::is_mod_enabled(mod.mod_id);
            return std::string(enabled ? "[x] " : "[ ] ") + mod.display_name;
        }
        case RowKind::Option: {
            const recomp::config::ConfigSchema& schema =
                recomp::mods::get_mod_config_schema(mods_[row.mod].mod_id);
            const recomp::config::ConfigOption& option = schema.options[row.option];
            return "      " + option.name + ": " +
                   config_value_text(option, recomp::mods::get_mod_config_value(
                                                 mods_[row.mod].mod_id, option.id));
        }
        case RowKind::Binding: {
            const InputMap map = read_input_map();
            const N64ButtonInfo& info = n64_button_info(row.binding);
            if (capture_row_ == index) {
                return pad_to(info.name, 15) + "PRESS A KEY OR PAD BUTTON...";
            }
            return pad_to(info.name, 15) + map.key_text(row.binding);
        }
        case RowKind::ResetBindings:
            return "Reset bindings to defaults";
        case RowKind::GameSpeed:
            return "GAME SPEED";
        case RowKind::Widescreen:
            return "ASPECT RATIO";
        case RowKind::FrameRate:
            return "FRAME RATE";
        case RowKind::Resolution:
            return "RESOLUTION";
        case RowKind::Antialias:
            return "ANTI-ALIASING";
        case RowKind::Display:
            return "WINDOW";
        case RowKind::Sound:
            return "SOUNDS";
        case RowKind::Mouse:
            return "MOUSE ENABLED";
        case RowKind::Volume:
            return row.binding == 1 ? "MUSIC" : row.binding == 2 ? "EFFECTS" : "VOLUME";
        case RowKind::ChaosFrame:
            return "Chaos Frame";
        case RowKind::RosterChar:
        case RowKind::RosterPage:
            return row.title;
        case RowKind::RosterSort:
            return "SORT BY";
        case RowKind::DirectorLine: {
            const auto lines = war_director::report();
            return row.record < static_cast<int>(lines.size()) ? lines[row.record].first : std::string();
        }
        case RowKind::ContentLine:
        case RowKind::ContentModInfo:
            return row.title;
        case RowKind::ContentMod: {
            if (row.mod >= content_mods_.size()) {
                return {};
            }
            const content::files::Mod& mod = content_mods_[row.mod].mod;
            return pad_to(std::to_string(row.mod + 1), 4) + mod.name + (mod.version.empty() ? "" : " " + mod.version);
        }
        case RowKind::ContentModsHeader:
            return "#   NAME";
        case RowKind::ContentModLink:
            return "      PAGE";
        case RowKind::ContentModsAll:
            return "Activate all";
        case RowKind::ContentModsNone:
            return "Deactivate all";
    }
    return {};
}

std::string Panel::right_text(size_t index) const {
    if (index >= rows_.size()) {
        return {};
    }
    return right_text_for(rows_[index]);
}

std::string Panel::right_text_for(const Row& row) const {
    if (Choice choice; choice_of(row.kind, choice)) {
        return choice_text(choice);
    }
    switch (row.kind) {
        case RowKind::ContentModOption: {
            const mod_options::Option* option = mod_option(row);
            if (option == nullptr) {
                return {};
            }
            const std::string value = mod_options::value_text(*option, content_mods_[row.mod].values.at(option->key));
            return option->type == mod_options::Type::Number ? "< " + value + " >" : value;
        }
        case RowKind::GameOption: {
            if (row.option >= game_settings().size()) {
                return {};
            }
            const GameSetting& g = game_settings()[row.option];
            const int current = game_setting_current(g);
            if (current < 0) {
                return "IN GAME ONLY";
            }
            std::string text;
            for (size_t i = 0; i < g.labels.size(); i++) {
                text += std::string(text.empty() ? "" : "  ") + (static_cast<int>(i) == current ? "[x] " : "[ ] ") +
                        g.labels[i];
            }
            return text;
        }
        case RowKind::SaveHere: {
            const std::string message = save_profiles::last_message();
            return message.empty() ? std::string("Into the leader's profile; close the menu to save") : message;
        }
        case RowKind::ProfileHeader:
            return std::to_string(row.option) + (row.option == 1 ? " save" : " saves") +
                   (open_profiles_.count(row.title) != 0 ? "   [-] hide" : "   [+] show");
        case RowKind::ProfileLoad:
            if (static_cast<int>(row.option) == delete_armed_) {
                return "[ ] LOAD  [x] DELETE?  (choose DELETE again)";
            }
            return "[ ] LOAD  [ ] DELETE";
        case RowKind::SaveBackup:
            return save_message_.empty() ? std::string("Copy the cartridge save to save-backups/")
                                         : save_message_;
        case RowKind::SaveImport:
            return "From save-import/ or the Controller Pak";
        case RowKind::SaveRestore:
            return "Replace the save (it is backed up first)";
        case RowKind::StartGame:
            return mode_ == Mode::Overlay
                       ? std::string("ALREADY RUNNING")
                       : (rom_.empty() ? std::string("CHOOSE A ROM FIRST")
                                       : std::string("Press ENTER or SPACE to start"));
        case RowKind::ExitGame:
            return mode_ == Mode::Overlay
                       ? std::string("Quit the game and close the window")
                       : std::string("Quit without starting a game");
        case RowKind::ReturnToTitle:
            return title_armed_ ? std::string("Choose again to go to the title; unsaved progress is lost")
                                : std::string("Back to the title screen");
        case RowKind::Rom:
            return rom_.empty() ? std::string("PRESS SPACE TO CHOOSE A ROM, OR DROP IT IN A WINDOW")
                                : rom_.filename().string();
        case RowKind::Mod:
            return mods_[row.mod].short_description;
        case RowKind::Binding: {
            const InputMap map = read_input_map();
            const N64ButtonInfo& info = n64_button_info(row.binding);
            return "PAD " + pad_to(map.pad_text(row.binding), 18) + "- " + info.field;
        }
        case RowKind::ResetBindings:
            return "Restore the default keyboard and gamepad map.";
        case RowKind::GameSpeed:
            return game_speed_text(game_speed());
        case RowKind::Widescreen:
            return widescreen_mode_text();
        case RowKind::FrameRate:
            return frame_rate_mode_text();
        case RowKind::Resolution:
            return resolution_mode_text();
        case RowKind::Antialias:
            return antialias_mode_text();
        case RowKind::Display:
            return display_mode_text();
        case RowKind::Sound:
            return sound_mode_text();
        case RowKind::Mouse:
            return mouse_mode_text();
        case RowKind::Volume:
            return volume_text(slider_percent(slider_of(row)));
        case RowKind::RosterChar:
        case RowKind::RosterPage:
            return row.detail;
        case RowKind::RosterSort: {
            static const char* const kSorts[] = {"LEVEL", "CLASS", "NAME"};
            std::string text;
            for (int i = 0; i < 3; i++) {
                if (!text.empty()) {
                    text += "  ";
                }
                text += std::string(i == roster_sort_ ? "[x] " : "[ ] ") + kSorts[i];
            }
            return text;
        }
        case RowKind::DirectorLine: {
            const auto lines = war_director::report();
            return row.record < static_cast<int>(lines.size()) ? lines[row.record].second : std::string();
        }
        case RowKind::ContentLine:
        case RowKind::ContentModInfo:
            return row.detail;
        case RowKind::ContentMod: {
            if (row.mod >= content_mods_.size()) {
                return {};
            }
            // The text panel (start screen) shows the cells as words; the one
            // in focus on the selected row is in brackets.
            const ContentModView& view = content_mods_[row.mod];
            const bool selected_row = selected_ < rows_.size() && &rows_[selected_] == &row;
            const std::string labels[] = {view.mod.wanted ? "ON" : "OFF",
                                          open_content_mods_.count(row.title) != 0 ? "LESS" : "MORE", "UP", "DOWN",
                                          erase_armed_ == row.title ? "DELETE?" : "DELETE"};
            std::string text;
            for (int cell = 0; cell < 5; cell++) {
                const bool focus = selected_row && cell == mod_cell_;
                text += std::string(focus ? "[" : " ") + labels[cell] + (focus ? "]" : " ");
            }
            return text + "  " + view.status;
        }
        case RowKind::ContentModsHeader:
            return " ON   DETAILS  ORDER   DELETE   STATUS";
        case RowKind::ContentModLink:
            return row.mod < content_mods_.size() ? content_mods_[row.mod].mod.url : std::string();
        case RowKind::ContentModsAll:
            return "Turn every mod on";
        case RowKind::ContentModsNone:
            return "Turn every mod off";
        case RowKind::ChaosFrame: {
            if (!chaos_frame_known_) {
                return "GAME NOT RUNNING";
            }
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%d  %s", chaos_frame_,
                          chaos_frame_band(chaos_frame_));
            return buffer;
        }
        default:
            return {};
    }
}

RowAction Panel::activate(size_t index, int direction) {
    if (index >= rows_.size()) {
        return RowAction::None;
    }
    const Row& row = rows_[index];
    if (row.kind != RowKind::ReturnToTitle) {
        title_armed_ = false;
    }
    if (Choice choice; choice_of(row.kind, choice)) {
        // An option table row steps its choices like the other radio rows.
        return step_radio_row(row.kind, direction);
    }
    switch (row.kind) {
        case RowKind::ContentModOption: {
            // A toggle or a choice steps to the next or previous one; a number
            // moves one step, and ENTER or A wraps from its maximum to its
            // minimum.
            const mod_options::Option* option = mod_option(row);
            if (option == nullptr) {
                return RowAction::None;
            }
            const int64_t current = content_mods_[row.mod].values.at(option->key);
            if (option->type == mod_options::Type::Number) {
                int64_t next = current + (direction < 0 ? -option->step : option->step);
                if (next > option->max) {
                    next = direction == 0 ? option->min : option->max;
                }
                next = std::max(next, option->min);
                return next != current ? set_mod_option(index, next) : RowAction::None;
            }
            const int64_t count = option->type == mod_options::Type::Toggle
                                      ? 2
                                      : static_cast<int64_t>(option->choices.size());
            const int64_t step = direction == 0 ? 1 : direction;
            return set_mod_option(index, ((current + step) % count + count) % count);
        }
        case RowKind::GameOption: {
            if (row.option >= game_settings().size()) {
                return RowAction::None;
            }
            const GameSetting& g = game_settings()[row.option];
            const int count = static_cast<int>(g.labels.size());
            const int current = game_setting_current(g);
            if (current < 0) {
                return RowAction::None;
            }
            const int step = direction == 0 ? 1 : direction;
            return game_setting_set(g, ((current + step) % count + count) % count) ? RowAction::SettingChanged
                                                                                     : RowAction::None;
        }
        case RowKind::Section:
            return RowAction::None;

        case RowKind::StartGame:
            return mode_ == Mode::Overlay ? RowAction::None : RowAction::Play;

        case RowKind::ExitGame:
            return RowAction::QuitGame;

        case RowKind::ReturnToTitle:
            if (!title_armed_) {
                title_armed_ = true;
                return RowAction::SettingChanged;
            }
            title_armed_ = false;
            save_profiles::request_title();
            return RowAction::SettingChanged;

        case RowKind::SaveHere:
            save_profiles::request_save();
            return RowAction::SettingChanged;

        case RowKind::ProfileLoad:
            return choose_option(index, direction > 0 ? 1 : 0);

        case RowKind::ProfileHeader: {
            const std::string profile = row.title;
            if (open_profiles_.count(profile) != 0) {
                open_profiles_.erase(profile);
            }
            else {
                open_profiles_.insert(profile);
            }
            rebuild_rows();
            for (size_t i = 0; i < rows_.size(); i++) {
                if (rows_[i].kind == RowKind::ProfileHeader && rows_[i].title == profile) {
                    selected_ = i;
                }
            }
            return RowAction::SettingChanged;
        }

        case RowKind::SaveBackup:
            save_message_ = back_up_save();
            rebuild_rows();
            return RowAction::SettingChanged;

        case RowKind::SaveImport:
            save_message_ = import_save();
            rebuild_rows();
            return RowAction::SettingChanged;

        case RowKind::SaveRestore:
            save_message_ = restore_save_backup(row.title);
            rebuild_rows();
            return RowAction::SettingChanged;

        case RowKind::Rom:
            return mode_ == Mode::Overlay ? RowAction::None : RowAction::BrowseRom;

        case RowKind::Mod: {
            const recomp::mods::ModDetails& mod = mods_[row.mod];
            const bool enabled = recomp::mods::is_mod_enabled(mod.mod_id);
            recomp::mods::enable_mod(mod.mod_id, !enabled);
            // Enabling a mod can enable a required dependency, so rebuild the
            // list instead of assuming only one row changed.
            reload();
            return RowAction::ModToggled;
        }

        case RowKind::Option: {
            const recomp::mods::ModDetails& mod = mods_[row.mod];
            const recomp::config::ConfigSchema& schema =
                recomp::mods::get_mod_config_schema(mod.mod_id);
            const recomp::config::ConfigOption& option = schema.options[row.option];
            const recomp::config::ConfigValueVariant value =
                recomp::mods::get_mod_config_value(mod.mod_id, option.id);
            using recomp::config::ConfigOptionType;
            switch (option.type) {
                case ConfigOptionType::Enum: {
                    const auto& enumeration =
                        std::get<recomp::config::ConfigOptionEnum>(option.variant);
                    if (enumeration.options.empty()) {
                        return RowAction::None;
                    }
                    const uint32_t current = std::holds_alternative<uint32_t>(value)
                                                 ? std::get<uint32_t>(value)
                                                 : enumeration.default_value;
                    const auto found = enumeration.find_option_from_value(current);
                    size_t position = found != enumeration.options.end()
                                          ? static_cast<size_t>(found - enumeration.options.begin())
                                          : 0;
                    position = direction >= 0
                                   ? (position + 1) % enumeration.options.size()
                                   : (position + enumeration.options.size() - 1) % enumeration.options.size();
                    recomp::mods::set_mod_config_value(mod.mod_id, option.id,
                                                       enumeration.options[position].value);
                    return RowAction::OptionChanged;
                }
                case ConfigOptionType::Bool: {
                    const bool current = std::holds_alternative<bool>(value)
                                             ? std::get<bool>(value)
                                             : std::get<recomp::config::ConfigOptionBool>(option.variant).default_value;
                    recomp::mods::set_mod_config_value(mod.mod_id, option.id, !current);
                    return RowAction::OptionChanged;
                }
                case ConfigOptionType::Number: {
                    const auto& number =
                        std::get<recomp::config::ConfigOptionNumber>(option.variant);
                    double current = std::holds_alternative<double>(value)
                                         ? std::get<double>(value)
                                         : number.default_value;
                    const double step = number.step != 0.0 ? number.step : 1.0;
                    current += direction >= 0 ? step : -step;
                    if (number.max > number.min) {
                        current = std::clamp(current, number.min, number.max);
                    }
                    recomp::mods::set_mod_config_value(mod.mod_id, option.id, current);
                    return RowAction::OptionChanged;
                }
                default:
                    return RowAction::None;
            }
        }

        case RowKind::Binding:
            begin_capture(index);
            return RowAction::None;

        case RowKind::ResetBindings: {
            InputMap map;
            map.reset_defaults();
            update_input_map(map);
            return RowAction::BindingChanged;
        }

        case RowKind::GameSpeed:
        case RowKind::Widescreen:
        case RowKind::FrameRate:
        case RowKind::Resolution:
        case RowKind::Antialias:
        case RowKind::Display:
        case RowKind::Sound:
        case RowKind::Mouse:
            // Left/Right and Space step the radio group, wrapping at the ends
            // like a mod option row. Each setting applies itself (the runtime for
            // GAME SPEED, `widescreen.cpp`, `frame_rate.cpp` or the renderer for
            // the DISPLAY rows), so a change under the overlay lands on the next
            // frame.
            return step_radio_row(row.kind, direction);

        case RowKind::Volume:
            // Left/Right and Space move the handle one step; the value is
            // clamped at the ends rather than wrapped.
            set_slider_percent(slider_of(row),
                               slider_percent(slider_of(row)) + direction * kVolumeStepPercent);
            return RowAction::SettingChanged;

        case RowKind::ChaosFrame:
            // A read-only readout: selectable so the value is highlighted, but
            // activating it does nothing.
            return RowAction::None;

        case RowKind::DirectorLine:
        case RowKind::ContentLine:
        case RowKind::ContentModInfo:
            return RowAction::None;

        case RowKind::ContentModsHeader:
            return RowAction::None;

        case RowKind::ContentModLink: {
            // The address was checked when mod.json was read; it is checked
            // again here because only this click hands it to the system.
            if (row.mod >= content_mods_.size()) {
                return RowAction::None;
            }
            const std::string& url = content_mods_[row.mod].mod.url;
            if (!content::files::valid_url(url)) {
                return RowAction::None;
            }
            mod_message_ = SDL_OpenURL(url.c_str()) == 0 ? "Opened " + url + " in your web browser."
                                                         : std::string("Could not open the page: ") + SDL_GetError();
            return RowAction::SettingChanged;
        }

        case RowKind::ContentMod:
            // ENTER, A or a click on the row: the cell in focus.
            return use_content_cell(index, mod_cell_);

        case RowKind::ContentModsAll:
        case RowKind::ContentModsNone: {
            const bool on = row.kind == RowKind::ContentModsAll;
            const RowKind kind = row.kind;
            std::vector<content::files::Mod> items;
            // The player's mods only; a shipped mod has its own switch.
            for (const ContentModView& view : content_mods_) {
                if (!view.mod.id.empty() && !view.mod.shipped) {
                    items.push_back(view.mod);
                }
            }
            erase_armed_.clear();
            mod_message_.clear();
            change_mod_settings([&](content::files::Settings& settings) {
                for (const content::files::Mod& mod : items) {
                    set_content_mod(settings, mod, on);
                }
            });
            for (size_t i = 0; i < rows_.size(); i++) {
                if (rows_[i].kind == kind) {
                    selected_ = i;
                }
            }
            return RowAction::SettingChanged;
        }

        case RowKind::RosterSort:
            roster_sort_ = ((roster_sort_ + (direction >= 0 ? 1 : -1)) % 3 + 3) % 3;
            roster_page_ = 0;
            rebuild_rows();
            return RowAction::None;

        case RowKind::RosterChar:
            return RowAction::None;

        case RowKind::RosterPage: {
            const int pages = std::max(1, row.target);
            roster_page_ = ((roster_page_ + (direction >= 0 ? 1 : -1)) % pages + pages) % pages;
            rebuild_rows();
            for (size_t i = 0; i < rows_.size(); i++) {
                if (rows_[i].kind == RowKind::RosterPage) {
                    selected_ = i;
                }
            }
            return RowAction::None;
        }

    }
    return RowAction::None;
}

RowAction Panel::choose_option(size_t index, size_t option) {
    if (index >= rows_.size()) {
        return RowAction::None;
    }
    const RowKind kind = rows_[index].kind;
    if (kind == RowKind::ProfileLoad) {
        const size_t which = rows_[index].option;
        if (which >= profile_entries_.size()) {
            return RowAction::None;
        }
        const save_profiles::Entry entry = profile_entries_[which];
        if (option == 1) {
            if (delete_armed_ != static_cast<int>(which)) {
                delete_armed_ = static_cast<int>(which);
                return RowAction::SettingChanged;
            }
            delete_armed_ = -1;
            save_message_ = save_profiles::remove(entry);
        }
        else {
            delete_armed_ = -1;
            save_message_ = save_profiles::load(entry);
        }
        rebuild_rows();
        return RowAction::SettingChanged;
    }
    if (kind == RowKind::ContentModOption) {
        // A click on one of a toggle's or a choice's labels.
        const mod_options::Option* declared = mod_option(rows_[index]);
        return declared != nullptr && declared->type != mod_options::Type::Number
                   ? set_mod_option(index, static_cast<int64_t>(option))
                   : RowAction::None;
    }
    if (kind == RowKind::GameOption) {
        const size_t which = rows_[index].option;
        return which < game_settings().size() && game_setting_set(game_settings()[which], static_cast<int>(option))
                   ? RowAction::SettingChanged
                   : RowAction::None;
    }
    if (kind == RowKind::ContentMod) {
        // A click on one of the row's cells (options.rml): 0 the switch,
        // 1 details, 2 up, 3 down, 4 delete.
        return use_content_cell(index, static_cast<int>(option));
    }
    if (is_radio_row(kind)) {
        // A click on one of the group's markers sets that option directly, which
        // is not the same as stepping to it.
        if (option >= static_cast<size_t>(radio_row_count(kind))) {
            return RowAction::None;
        }
        return store_radio_row(kind, static_cast<int>(option));
    }
    switch (kind) {
        case RowKind::Volume: {
            // A click lands on one bar cell; the cell sets the percent, at the
            // 10% the 11 cells divide the range into.
            if (option >= static_cast<size_t>(kVolumeCells)) {
                return RowAction::None;
            }
            const Slider slider = slider_of(rows_[index]);
            const int percent = volume_percent_for_cell(static_cast<int>(option));
            if (percent == slider_percent(slider)) {
                return RowAction::None;
            }
            set_slider_percent(slider, percent);
            return RowAction::SettingChanged;
        }
        default:
            return RowAction::None;
    }
}

Panel::RowOptions Panel::row_options(size_t index) const {
    RowOptions out;
    if (index >= rows_.size()) {
        return out;
    }
    const RowKind kind = rows_[index].kind;
    if (kind == RowKind::ProfileLoad) {
        out.labels = {"LOAD", static_cast<int>(rows_[index].option) == delete_armed_ ? "DELETE?" : "DELETE"};
        out.current = static_cast<int>(rows_[index].option) == delete_armed_ ? 1 : -1;
        return out;
    }
    if (kind == RowKind::ContentModOption) {
        const mod_options::Option* option = mod_option(rows_[index]);
        if (option != nullptr && option->type != mod_options::Type::Number) {
            out.labels = option->type == mod_options::Type::Toggle ? std::vector<std::string>{"OFF", "ON"}
                                                                   : option->choices;
            out.current = static_cast<int>(content_mods_[rows_[index].mod].values.at(option->key));
        }
        return out;
    }
    if (kind == RowKind::GameOption) {
        if (rows_[index].option < game_settings().size()) {
            const GameSetting& g = game_settings()[rows_[index].option];
            out.current = game_setting_current(g);
            if (out.current >= 0) {
                for (const char* label : g.labels) {
                    out.labels.push_back(label);
                }
            }
        }
        return out;
    }
    if (kind == RowKind::Volume) {
        out.slider = true;
        out.labels.assign(static_cast<size_t>(kVolumeCells), std::string());
        const int percent = slider_percent(slider_of(rows_[index]));
        for (int cell = 0; cell < kVolumeCells; cell++) {
            if (volume_percent_for_cell(cell) <= percent) {
                out.current = cell;
            }
        }
        return out;
    }
    if (!is_radio_row(kind)) {
        return out;
    }
    if (Choice choice; choice_of(kind, choice)) {
        for (int i = 0; i < choice_option_count(choice); i++) {
            out.labels.push_back(choice_label(choice, i));
        }
        out.current = radio_row_current(kind);
        return out;
    }
    switch (kind) {
        case RowKind::GameSpeed:
            out.labels = game_speed_labels();
            out.current = game_speed_index(game_speed());
            return out;
        case RowKind::Widescreen: out.labels = widescreen_labels(); break;
        case RowKind::FrameRate: out.labels = frame_rate_labels(); break;
        case RowKind::Resolution: out.labels = resolution_labels(); break;
        case RowKind::Antialias: out.labels = antialias_labels(); break;
        case RowKind::Display: out.labels = display_labels(); break;
        case RowKind::Sound: out.labels = sound_labels(); break;
        case RowKind::Mouse: out.labels = mouse_labels(); break;
        default: break;
    }
    out.current = radio_row_current(kind);
    return out;
}

void Panel::begin_capture(size_t index) {
    if (index < rows_.size() && rows_[index].kind == RowKind::Binding) {
        capture_row_ = static_cast<int>(index);
    }
}

void Panel::cancel_capture() {
    capture_row_ = -1;
}

bool Panel::capture_key(SDL_Scancode code) {
    if (!capturing() || is_reserved_key(code)) {
        return false;
    }
    InputMap map = read_input_map();
    map.set_key(rows_[capture_row_].binding, code);
    update_input_map(map);
    cancel_capture();
    return true;
}

bool Panel::capture_pad_button(int code) {
    if (!capturing()) {
        return false;
    }
    InputMap map = read_input_map();
    map.set_pad_button(rows_[capture_row_].binding, code);
    update_input_map(map);
    cancel_capture();
    return true;
}

bool Panel::capture_clear(bool keyboard) {
    if (!capturing()) {
        return false;
    }
    InputMap map = read_input_map();
    const int binding = rows_[capture_row_].binding;
    if (keyboard) {
        map.clear_key(binding);
    }
    else {
        map.clear_pad(binding);
    }
    update_input_map(map);
    return true;
}

Panel::Hit Panel::hit_test(int x, int y) const {
    auto inside = [&](const SDL_Rect& rect) {
        return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
    };
    for (size_t i = 0; i < geometry_.tabs.size(); i++) {
        if (inside(geometry_.tabs[i])) {
            return Hit{Hit::Kind::Tab, static_cast<int>(i)};
        }
    }
    // A radio marker sits inside its row, so it is tested first.
    for (const Geometry::Option& option : geometry_.options) {
        if (inside(option.rect)) {
            return Hit{Hit::Kind::RowOption, option.row, option.option};
        }
    }
    for (size_t i = 0; i < geometry_.rows.size(); i++) {
        if (inside(geometry_.rows[i])) {
            return Hit{Hit::Kind::Row, static_cast<int>(i)};
        }
    }
    return Hit{};
}

// --- drawing ------------------------------------------------------------------

bool write_ppm(SDL_Renderer* renderer, const std::filesystem::path& path) {
    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0 || width <= 0 || height <= 0) {
        return false;
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);
    if (SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_RGB24, pixels.data(),
                             width * 3) != 0) {
        return false;
    }
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        return false;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    std::fwrite(pixels.data(), 1, pixels.size(), file);
    std::fclose(file);
    return true;
}

namespace {
PanelMetrics measure_panel_at(const Font& font, Panel& panel, int output_width, int top_y,
                              float ui_scale, bool large);
}  // namespace

// UI SCALE LARGE draws the text half again as large, unless the active tab
// would then not fit in `output_height`; that tab falls back to the normal size.
PanelMetrics measure_panel(const Font& font, Panel& panel, int output_width, int top_y,
                           float ui_scale, int output_height) {
    if (choice_on(Choice::UiScale)) {
        PanelMetrics large = measure_panel_at(font, panel, output_width, top_y, ui_scale, true);
        // The active tab's own height decides, not the layout reference's, so
        // short tabs grow even when CONTROLS could not.
        if (output_height <= 0 || large.height <= output_height * 3 / 4) {
            large.layout_height = large.height;
            return large;
        }
    }
    return measure_panel_at(font, panel, output_width, top_y, ui_scale, false);
}

namespace {
PanelMetrics measure_panel_at(const Font& font, Panel& panel, int output_width, int top_y,
                              float ui_scale, bool large) {
    auto px = [ui_scale](int value) {
        return static_cast<int>(static_cast<float>(value) * ui_scale);
    };

    PanelMetrics metrics;
    // UI SCALE LARGE draws the panel text half again as large.
    metrics.scale = large ? px(kTextScale) * 3 / 2 : px(kTextScale);
    metrics.line_height = metrics.scale * Font::kCellHeight;
    metrics.row_height = metrics.line_height + px(5);
    const int section_height = metrics.line_height + px(6);
    metrics.panel_width = std::min(output_width - px(64), px(900));
    metrics.panel_left = output_width / 2 - metrics.panel_width / 2;
    // The second column starts two fifths across and wraps inside what is left;
    // the first column is truncated before it.
    const int description_x = metrics.panel_left + (metrics.panel_width * 2) / 5;
    const int description_width = metrics.panel_left + metrics.panel_width - description_x;

    // A section row's title may use the whole panel, because a section has no
    // description to its right.
    const int full_width = metrics.panel_width - px(16);
    RowsLayout rows = layout_rows(font, panel, panel.rows(), metrics.scale, metrics.row_height,
                                  section_height, description_width, full_width, px(4), true);
    metrics.row_heights = std::move(rows.heights);
    metrics.left_lines = std::move(rows.left_lines);
    metrics.right_lines = std::move(rows.right_lines);

    const int tab_bar_height = metrics.line_height + px(10);
    const int chrome = tab_bar_height + px(10) + px(12) + metrics.line_height + px(12);
    metrics.rows_top = top_y + tab_bar_height + px(10);
    metrics.hint_y = metrics.rows_top + rows.total + px(12);
    metrics.height = chrome + rows.total;

    // The same layout for the tab that fixes the panel's position. Only its
    // height is read, so the wrapped right-column lines are not kept.
    const RowsLayout reference =
        layout_rows(font, panel, panel.layout_rows(), metrics.scale, metrics.row_height,
                    section_height, description_width, full_width, px(4), false);
    metrics.layout_height = std::max(metrics.height, chrome + reference.total);
    return metrics;
}
}  // namespace

PanelDraw draw_panel(SDL_Renderer* renderer, const Font& font, Panel& panel,
                     int output_width, int output_height, int top_y, float ui_scale) {
    (void)output_height;
    auto px = [ui_scale](int value) {
        return static_cast<int>(static_cast<float>(value) * ui_scale);
    };

    apply_contrast();
    const PanelMetrics metrics =
        measure_panel(font, panel, output_width, top_y, ui_scale, output_height);
    const int scale = metrics.scale;
    const int line_height = metrics.line_height;
    const int panel_width = metrics.panel_width;
    const int panel_left = metrics.panel_left;
    const int description_x = panel_left + (panel_width * 2) / 5;
    const int description_width = panel_left + panel_width - description_x;
    const int left_width = description_x - panel_left - px(16);

    TextLayer layer;
    Panel::Geometry geometry;

    // The panel background is drawn first: the tab bar and the rows both sit on
    // top of it. It reserves the layout height, so the frame does not change
    // size when the active tab changes.
    SDL_SetRenderDrawColor(renderer, kPanelColor.r, kPanelColor.g, kPanelColor.b, kPanelColor.a);
    const SDL_Rect background{panel_left - px(18), top_y - px(12),
                              panel_width + px(36), metrics.layout_height};
    SDL_RenderFillRect(renderer, &background);

    // --- tab bar ---
    // The tab bar and the hint line stay at the normal text size under UI SCALE
    // LARGE, which enlarges the rows: at the large size they are wider than the
    // panel.
    const int chrome_scale = px(kTextScale);
    int tab_x = panel_left;
    const int tab_text_y = top_y + px(3);
    for (int i = 0; i < kTabCount; i++) {
        const Tab tab = static_cast<Tab>(i);
        const bool enabled = panel.tab_enabled(tab);
        const bool active = tab == panel.tab();
        if (panel.tab_label(tab).empty()) {
            geometry.tabs.push_back(SDL_Rect{0, 0, 0, 0});  // keeps the index; nothing drawn
            continue;
        }
        const std::string label = "[ " + panel.tab_label(tab) + " ]";
        const int text_width = font.width(label, chrome_scale);
        const SDL_Rect rect{tab_x, top_y, text_width + px(10), line_height + px(6)};
        geometry.tabs.push_back(rect);

        SDL_Color color = kHintColor;
        if (active) {
            color = kWarmColor;
        }
        else if (enabled) {
            color = kSubtitleColor;
        }
        layer.build(renderer, font, label, chrome_scale, color);
        layer.draw(renderer, 0, tab_x + px(5), tab_text_y, false);
        tab_x += rect.w + px(2);
    }

    int cursor_y = metrics.rows_top;
    for (size_t i = 0; i < panel.row_count(); i++) {
        const Panel::RowKind kind = panel.row_kind(i);
        const int height = metrics.row_heights[i];
        const bool is_selected = i == panel.selected() && panel.selectable(i);
        const SDL_Rect row_rect{panel_left - px(12), cursor_y - px(4),
                                panel_width + px(24), height};
        geometry.rows.push_back(row_rect);
        if (is_selected) {
            SDL_SetRenderDrawColor(renderer, kSelectColor.r, kSelectColor.g, kSelectColor.b,
                                   kSelectColor.a);
            SDL_RenderFillRect(renderer, &row_rect);
        }

        SDL_Color color = kTitleColor;
        if (kind == Panel::RowKind::Section || !panel.selectable(i)) {
            color = kHintColor;
        }
        else if (is_selected) {
            color = kWarmColor;
        }
        else if (kind == Panel::RowKind::Option || kind == Panel::RowKind::Binding ||
                 kind == Panel::RowKind::ResetBindings) {
            color = kSubtitleColor;
        }

        const int text_y =
            cursor_y + (kind == Panel::RowKind::Section ? px(6) : 0);
        if (kind == Panel::RowKind::Section) {
            // A section title is laid out wrapped by layout_rows; the left
            // column of a section spans the panel.
            int line_offset = 0;
            for (const std::string& line : metrics.left_lines[i]) {
                layer.build(renderer, font, line, scale, color);
                layer.draw(renderer, 0, panel_left, text_y + line_offset, false);
                line_offset += line_height;
            }
        }
        else {
            const std::string left =
                truncate_to_width(font, panel.left_text(i), scale, left_width);
            layer.build(renderer, font, left, scale, color);
            layer.draw(renderer, 0, panel_left, text_y, false);
        }

        int right_y = text_y;
        if (is_radio_row(kind)) {
            // The marker of the live option stays warm so the current value is
            // readable even when the row is not selected.
            const int current = radio_row_current(kind);
            const RadioLayout layout =
                radio_row_layout(kind, font, scale, description_width, current);
            for (const RadioToken& token : layout.tokens) {
                const bool live = token.option == current;
                layer.build(renderer, font, token.text, scale,
                            (is_selected || live) ? kWarmColor : kSubtitleColor);
                layer.draw(renderer, 0, description_x + token.x, text_y + token.y, false);
            }
            for (size_t option = 0; option < layout.option_rects.size(); option++) {
                SDL_Rect rect = layout.option_rects[option];
                rect.x += description_x;
                rect.y += text_y;
                geometry.options.push_back(Panel::Geometry::Option{
                    static_cast<int>(i), static_cast<int>(option), rect});
            }
        }
        else if (kind == Panel::RowKind::Volume) {
            // The bar and its handle are one text run, so the handle is always
            // visible; the cells under it are click targets.
            const SliderLayout slider =
                layout_volume(font, scale, slider_percent(slider_of(panel.rows()[i])));
            layer.build(renderer, font, slider.text, scale,
                        is_selected ? kWarmColor : kSubtitleColor);
            layer.draw(renderer, 0, description_x, text_y, false);
            for (size_t cell = 0; cell < slider.cell_rects.size(); cell++) {
                SDL_Rect rect = slider.cell_rects[cell];
                rect.x += description_x;
                rect.y += text_y;
                geometry.options.push_back(Panel::Geometry::Option{
                    static_cast<int>(i), static_cast<int>(cell), rect});
            }
        }
        else {
            for (const std::string& line : metrics.right_lines[i]) {
                layer.build(renderer, font, line, scale,
                            is_selected ? kWarmColor : kHintColor);
                layer.draw(renderer, 0, description_x, right_y, false);
                right_y += line_height;
            }
        }

        cursor_y += height;
    }

    const char* hint = kLauncherHint;
    if (panel.capturing()) {
        hint = kCaptureHint;
    }
    else if (panel.mode() == Panel::Mode::Overlay) {
        hint = kOverlayHint;
    }
    layer.build(renderer, font, hint, chrome_scale, kHintColor);
    layer.draw(renderer, 0, panel_left, metrics.hint_y, false);

    panel.set_geometry(std::move(geometry));

    PanelDraw result;
    result.panel_left = panel_left;
    result.panel_width = panel_width;
    result.height = metrics.layout_height;
    result.text_scale = scale;
    return result;
}

}  // namespace ogre::ui

