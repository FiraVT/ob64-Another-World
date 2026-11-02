// This project's menu options: the table of every option the
// features add, with its settings.cfg key, choices, default, menu title, help
// text and place in the Esc menu. The base stores them in settings.cfg and
// draws them from this table (settings.cpp, ui.cpp); it holds no option of its
// own here.
//
// The ids are stable: settings.cfg stores the choice by its label, and code
// across the features names an option by its id.
#pragma once

#include <string>
#include <vector>

namespace ogre {

enum class Choice {
    UiScale = 0,          // NORMAL / LARGE: the Esc menu's size
    Contrast = 1,         // NORMAL / HIGH: the Esc menu's contrast
    PixelFilter = 2,      // SMOOTH / SHARP: how the picture is scaled (renderer.cpp)
    TurboA = 3,           // OFF / ON: holding A repeats it
    WarDirector = 4,      // OFF / ON, ON by default (war_director.cpp)
    LootUpgrades = 5,     // OFF / ON, ON by default
    TrainingCatchUp = 6,  // OFF / ON, ON by default
    PauseUnfocused = 7,   // OFF / ON: the Esc menu opens when the window loses focus
    SkipIntro = 8,        // OFF / ON: boot to the title menu
    ButtonPrompts = 9,    // AUTO / XBOX / PLAYSTATION / NINTENDO (input_map.cpp pad_style)
    HdTextures = 10,      // OFF / ON, ON by default: texture-packs/ at renderer start
    TextFont = 11,        // one of texture-packs/text/<name>, found on disk, saved by name
    FieldControls = 12,   // MODERN / CLASSIC: the port's field overlay and menus (field_select.cpp)
    UnitReports = 13,     // LOG / PAUSE: a unit's field message goes to the alert log
    BattleSpeed = 14,     // SAME / X1 / X2 / X4: the speed while a battle runs
    SelectPause = 15,     // OFF / ON, ON by default: selecting a unit on the field pauses
    EnemyWeapons = 16,    // VARIED / ORIGINAL
    EnemyFormations = 17, // TACTICAL / ORIGINAL
    EnemyRanks = 18,      // VETERANS / ORIGINAL
    ConfinePointer = 19,  // OFF / ON, ON by default: the pointer stays in the focused window (sdl_platform.cpp)
    EnemyTactics = 20,    // EASY / NORMAL / HARD / VETERAN, NORMAL by default (enemy_commander.cpp)
    DialogueAutoPlay = 21,  // OFF / ON (dialogue_autoplay.cpp)
    AutoCommander = 22,   // OFF / ON: the AI commands the player's units (auto_commander.cpp)
    AutoEnding = 23,      // LIBERATE / CAPTURE / EITHER: how the AI takes towns (auto_commander.cpp)
    StickX = 24,          // NORMAL / INVERTED: the left stick's x as the N64 stick (sdl_platform.cpp get_input)
    StickY = 25,          // NORMAL / INVERTED: the left stick's y as the N64 stick
};
constexpr int kChoiceCount = 26;

int choice_option_count(Choice choice);
const char* choice_label(Choice choice, int index);
int choice_index(Choice choice);
void set_choice(Choice choice, int index);
// Option 1 is selected (an OFF / ON option is ON).
bool choice_on(Choice choice);
// A feature that is on by default: true while the option is at its default
// (ON for an OFF / ON option, the first choice for VARIED / ORIGINAL and the
// like). The environment variable `env` ("0" off, anything else
// on) wins over the option, for developer runs.
bool feature_enabled(Choice choice, const char* env);

// TEXT FONT: the texture-packs/text folder in use ("original" is the game's
// own font). Read at renderer start.
std::string text_font();

// "[x] A  [ ] B" for a menu row.
std::string choice_text(Choice choice);

}  // namespace ogre

namespace ogre::options {

// The Esc menu title and help text of an option.
const char* title(Choice choice);
const char* help(Choice choice);

// The options shown in one section of the Esc menu, in menu order. `tab` and
// `section` are the menu's own names ("GAMEPLAY", "WAR DIRECTOR").
std::vector<Choice> in_section(const char* tab, const char* section);

// settings.cfg: the lines for every option, and reading them back. `value_of`
// returns the text after `key =` in the file, or "" when the key is missing.
std::string settings_lines();
void load(std::string (*value_of)(const std::string& text, const char* key), const std::string& text);

// Called when an option changes, after the new value is stored and before
// settings.cfg is written. The base uses it for PIXEL FILTER, whose renderer
// state is not part of the graphics config.
void on_change(void (*handler)(Choice choice));

}  // namespace ogre::options

