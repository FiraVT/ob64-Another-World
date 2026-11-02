// See options.hpp.

#include "options.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "game_host.hpp"

namespace ogre {
namespace {

struct ChoiceInfo {
    const char* key;  // the settings.cfg key
    const char* labels[4];
    int count;
    int fallback;     // the default, and the value an unknown label reads as
    const char* title;
    const char* help;
};

// Indexed by Choice.
constexpr ChoiceInfo kChoices[kChoiceCount] = {
    {"ui_scale", {"NORMAL", "LARGE"}, 2, 0, "UI SCALE",
     "LARGE makes this menu bigger, with larger text. Applies at once."},
    {"contrast", {"NORMAL", "HIGH"}, 2, 0, "HIGH CONTRAST",
     "HIGH keeps the parchment menu but raises its contrast: darker text, dark outlines, a deep blue selection. "
     "Applies at once."},
    {"pixel_filter", {"SMOOTH", "SHARP"}, 2, 0, "PIXEL FILTER",
     "SMOOTH scales the picture with filtering; SHARP keeps hard pixel edges. Applies at once."},
    {"turbo_a", {"OFF", "ON"}, 2, 0, "TURBO A", ""},
    {"war_director", {"OFF", "ON"}, 2, 1, "WAR DIRECTOR",
     "The enemy builds and sends units during a mission, with telegraphed attacks and spoils."},
    {"loot_upgrades", {"OFF", "ON"}, 2, 1, "LOOT UPGRADES", "Battle drops are upgraded by the mission's band."},
    {"training_catch_up", {"OFF", "ON"}, 2, 1, "TRAINING CATCH-UP", ""},
    {"pause_unfocused", {"OFF", "ON"}, 2, 0, "PAUSE WHEN UNFOCUSED", ""},
    {"skip_intro", {"OFF", "ON"}, 2, 0, "SKIP INTRO", "Boot straight to the title menu."},
    {"button_prompts", {"AUTO", "XBOX", "PLAYSTATION", "NINTENDO"}, 4, 0, "BUTTON PROMPTS",
     "How pad buttons are named. AUTO names them after the pad in use. The prompts switch between the keyboard "
     "and the pad on their own: whichever was touched last is shown."},
    {"hd_textures", {"OFF", "ON"}, 2, 1, "HD TEXTURES",
     "Loads the HD texture packs in texture-packs/ beside the game. Takes effect after a restart."},
    {"text_font", {"ORIGINAL"}, 1, 0, "TEXT FONT",
     "The font the game's text is drawn with (texture-packs/text/). Takes effect after a restart."},
    {"field_controls", {"MODERN", "CLASSIC"}, 2, 0, "PORT MENUS",
     "MODERN: the port's battlefield overlay owns the mouse and replaces the game's command, Organize and "
     "Settings menus. CLASSIC: the game's own cursor and menus."},
    {"unit_reports", {"LOG", "PAUSE"}, 2, 0, "UNIT REPORTS",
     "LOG: a unit's report on the field goes to the alert log and closes by itself. PAUSE: the game stops for "
     "each report. UNIT REPORT TYPE NONE turns them off in the game."},
    {"battle_speed", {"SAME", "X1", "X2", "X4"}, 4, 0, "BATTLE SPEED",
     "The speed of battles. SAME follows GAME SPEED; X1 to X4 set battles on their own, so the field and the "
     "battles can run at different speeds. Holding Tab still fast-forwards both."},
    {"select_pause", {"OFF", "ON"}, 2, 1, "PAUSE ON SELECT",
     "ON: selecting a unit on the field pauses the game so you can give it orders. A command resumes the game "
     "while the unit stays selected; clearing the selection resumes it too. MODERN only."},
    {"enemy_weapons", {"VARIED", "ORIGINAL"}, 2, 0, "ENEMY WEAPONS",
     "VARIED: generic enemy units may carry a stronger weapon of their class's own type, picked from the "
     "mission's price band, so units differ. Leaders are likelier to have one. Bosses and story units keep "
     "their gear. Applies from the next mission."},
    {"enemy_formations", {"TACTICAL", "ORIGINAL"}, 2, 0, "ENEMY FORMATIONS",
     "TACTICAL: generic enemy units are re-seated by where each class fights best, with the front row "
     "screening the casters; leaders of three or more, and bosses, stand behind a member. Some members stay a "
     "row off, more so early in the war, so units are not perfect. Large members take one square and may "
     "stand side by side. Applies from the next mission."},
    {"enemy_ranks", {"VETERANS", "ORIGINAL"}, 2, 0, "ENEMY RANKS",
     "VETERANS: as the war goes on, more enemy groups take the next class of their line (Valkyrie to Freya, "
     "Ghost to Lich): none early, a growing share mid-game, three in four late. A unit of one repeated class "
     "gives half its members a second class from the mission. Boss escorts promote too; the boss keeps its "
     "class. Applies from the next mission."},
    {"confine_pointer", {"OFF", "ON"}, 2, 1, "KEEP POINTER IN WINDOW",
     "ON: while the game window has focus and no menu is open, the pointer cannot leave it, so pushing "
     "against an edge scrolls instead of slipping off. Opening the menu or switching away lets it go."},
    {"enemy_tactics", {"EASY", "NORMAL", "HARD", "VETERAN"}, 4, 1, "DIFFICULTY",
     "How the war director's units fight. EASY: slow decisions, it only knows what it sees, lone attacks. "
     "NORMAL: defends towns, retakes them, rests hurt units, pairs up. HARD: knows where you are, raids your "
     "headquarters, strikes in threes. VETERAN: decides hourly, hunts stragglers, no warnings. Story units "
     "are never moved. Needs WAR DIRECTOR."},
    {"dialogue_autoplay", {"OFF", "ON"}, 2, 0, "AUTO-PLAY DIALOGUE",
     "A dialogue line advances by itself once it is shown, after a pause that grows with its length. Choices "
     "still wait for you. F2 turns it on and off."},
    {"auto_commander", {"OFF", "ON"}, 2, 0, "AUTO COMMANDER",
     "An AI commands the units you send out on a mission: it takes towns, defends them, rests hurt units in "
     "towns, and attacks strong enemies and the boss in waves. Any order you give a unit takes that unit back "
     "(the AI button on its card hands it back); a selected unit is left alone. F4 turns it on and off."},
    {"auto_ending", {"LIBERATE", "CAPTURE", "EITHER"}, 3, 0, "AUTO ENDING",
     "How the auto commander takes towns. LIBERATE: only units whose alignment liberates the town (raises the "
     "Chaos Frame). CAPTURE: only units that capture it. EITHER: any unit with a sure result."},
    {"stick_x", {"NORMAL", "INVERTED"}, 2, 0, "STICK LEFT / RIGHT",
     "The left stick moves the game's cursor and the N64 analog stick. INVERTED swaps left and right."},
    {"stick_y", {"NORMAL", "INVERTED"}, 2, 0, "STICK UP / DOWN",
     "NORMAL: pushing the left stick up moves the cursor up. INVERTED swaps up and down."},
};

// Where each option shows in the Esc menu, in menu order.
struct Placement {
    const char* tab;
    const char* section;
    Choice choice;
};
constexpr Placement kLayout[] = {
    {"CONTROLS", "MOUSE", Choice::ConfinePointer},
    {"CONTROLS", "GAMEPAD", Choice::ButtonPrompts},
    {"CONTROLS", "GAMEPAD", Choice::StickX},
    {"CONTROLS", "GAMEPAD", Choice::StickY},
    {"GRAPHICS", "DISPLAY", Choice::PauseUnfocused},
    {"GRAPHICS", "RENDERING", Choice::PixelFilter},
    {"GRAPHICS", "HD ASSETS", Choice::HdTextures},
    {"GRAPHICS", "HD ASSETS", Choice::TextFont},
    {"GRAPHICS", "MENUS", Choice::UiScale},
    {"GRAPHICS", "MENUS", Choice::Contrast},
    {"GAMEPLAY", "INTERFACE", Choice::FieldControls},
    {"GAMEPLAY", "INTERFACE", Choice::UnitReports},
    {"GAMEPLAY", "INTERFACE", Choice::SelectPause},
    {"GAMEPLAY", "START", Choice::SkipIntro},
    {"GAMEPLAY", "PACE", Choice::BattleSpeed},
    {"GAMEPLAY", "PACE", Choice::TurboA},
    {"GAMEPLAY", "PACE", Choice::DialogueAutoPlay},
    {"GAMEPLAY", "AUTO COMMANDER", Choice::AutoCommander},
    {"GAMEPLAY", "AUTO COMMANDER", Choice::AutoEnding},
    {"GAMEPLAY", "WAR DIRECTOR", Choice::WarDirector},
    {"GAMEPLAY", "WAR DIRECTOR", Choice::EnemyTactics},
    {"GAMEPLAY", "WAR DIRECTOR", Choice::LootUpgrades},
    {"GAMEPLAY", "WAR DIRECTOR", Choice::TrainingCatchUp},
    {"GAMEPLAY", "ENEMY UNITS", Choice::EnemyWeapons},
    {"GAMEPLAY", "ENEMY UNITS", Choice::EnemyFormations},
    {"GAMEPLAY", "ENEMY UNITS", Choice::EnemyRanks},
};

int g_choices[kChoiceCount] = {};
// Each option starts at its default, before settings.cfg is read.
[[maybe_unused]] const bool g_choices_seeded = [] {
    for (int i = 0; i < kChoiceCount; i++) {
        g_choices[i] = kChoices[i].fallback;
    }
    return true;
}();
std::string g_text_font = "original";  // TEXT FONT, saved by name
void (*g_change_handler)(Choice) = nullptr;

std::string lowercase(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

// TEXT FONT's options are the text packs that exist: texture-packs/text/<name>
// folders with an rt64.json, built by tools/hd_textures.py. "original" is
// always first. Scanned once per run.
const std::vector<std::string>& text_font_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> list{"original"};
        std::error_code ec;
        const std::filesystem::path root = host::exe_dir() / "texture-packs" / "text";
        std::vector<std::string> found;
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            const std::string name = entry.path().filename().string();
            if (name != "original" && std::filesystem::is_regular_file(entry.path() / "rt64.json", ec)) {
                found.push_back(name);
            }
        }
        std::sort(found.begin(), found.end());
        list.insert(list.end(), found.begin(), found.end());
        return list;
    }();
    return names;
}

int text_font_index() {
    const std::vector<std::string>& names = text_font_names();
    for (size_t i = 0; i < names.size(); i++) {
        if (names[i] == g_text_font) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

const ChoiceInfo& info(Choice choice) {
    return kChoices[static_cast<int>(choice)];
}

}  // namespace

int choice_option_count(Choice choice) {
    if (choice == Choice::TextFont) {
        return static_cast<int>(text_font_names().size());
    }
    return info(choice).count;
}

const char* choice_label(Choice choice, int index) {
    if (choice == Choice::TextFont) {
        static std::vector<std::string> labels;
        if (labels.empty()) {
            for (const std::string& name : text_font_names()) {
                std::string upper = name;
                std::transform(upper.begin(), upper.end(), upper.begin(),
                               [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                labels.push_back(upper);
            }
        }
        return labels[static_cast<size_t>(index >= 0 && index < static_cast<int>(labels.size()) ? index : 0)].c_str();
    }
    const ChoiceInfo& i = info(choice);
    return i.labels[index >= 0 && index < i.count ? index : 0];
}

int choice_index(Choice choice) {
    if (choice == Choice::TextFont) {
        return text_font_index();
    }
    return g_choices[static_cast<int>(choice)];
}

void set_choice(Choice choice, int index) {
    if (choice == Choice::TextFont) {
        const std::vector<std::string>& names = text_font_names();
        const std::string wanted = names[static_cast<size_t>(index >= 0 && index < static_cast<int>(names.size()) ? index : 0)];
        if (wanted != g_text_font) {
            g_text_font = wanted;
            host::save_settings();
        }
        return;
    }
    const ChoiceInfo& i = info(choice);
    index = index >= 0 && index < i.count ? index : i.fallback;
    int& value = g_choices[static_cast<int>(choice)];
    if (value == index) {
        return;
    }
    value = index;
    if (g_change_handler != nullptr) {
        g_change_handler(choice);
    }
    host::save_settings();
}

bool choice_on(Choice choice) {
    return g_choices[static_cast<int>(choice)] == 1;
}

bool feature_enabled(Choice choice, const char* env) {
    if (const char* value = std::getenv(env); value != nullptr && value[0] != '\0') {
        return value[0] != '0';
    }
    return g_choices[static_cast<int>(choice)] == kChoices[static_cast<int>(choice)].fallback;
}

std::string text_font() {
    return g_text_font;
}

std::string choice_text(Choice choice) {
    std::string text;
    for (int i = 0; i < choice_option_count(choice); i++) {
        if (!text.empty()) {
            text += "  ";
        }
        text += std::string(i == choice_index(choice) ? "[x] " : "[ ] ") + choice_label(choice, i);
    }
    return text;
}

}  // namespace ogre

namespace ogre::options {

const char* title(Choice choice) {
    return info(choice).title;
}

const char* help(Choice choice) {
    return info(choice).help;
}

std::vector<Choice> in_section(const char* tab, const char* section) {
    std::vector<Choice> out;
    for (const Placement& p : kLayout) {
        if (std::strcmp(p.tab, tab) == 0 && std::strcmp(p.section, section) == 0) {
            out.push_back(p.choice);
        }
    }
    return out;
}

std::string settings_lines() {
    std::string text =
        "# ui_scale: normal or large (the app's panels). contrast: normal or high.\n"
        "# pixel_filter: smooth or sharp (how the game picture is scaled to the window).\n"
        "# turbo_a: off or on (holding A repeats it).\n"
        "# dialogue_autoplay: off or on (a message window advances by itself after a read delay; F2).\n";
    for (int i = 0; i < kChoiceCount; i++) {
        if (static_cast<Choice>(i) == Choice::TextFont) {
            text += "# text_font: the texture-packs/text folder to use (original = the game's font).\n"
                    "text_font = " + g_text_font + "\n";
            continue;
        }
        text += std::string(kChoices[i].key) + " = " + lowercase(kChoices[i].labels[g_choices[i]]) + "\n";
    }
    return text;
}

void load(std::string (*value_of)(const std::string& text, const char* key), const std::string& text) {
    for (int i = 0; i < kChoiceCount; i++) {
        g_choices[i] = kChoices[i].fallback;
    }
    if (const std::string font = value_of(text, "text_font"); !font.empty()) {
        g_text_font = font;
    }
    for (int i = 0; i < kChoiceCount; i++) {
        const std::string value = lowercase(value_of(text, kChoices[i].key));
        for (int option = 0; option < kChoices[i].count; option++) {
            if (value == lowercase(kChoices[i].labels[option])) {
                g_choices[i] = option;
            }
        }
    }
}

void on_change(void (*handler)(Choice choice)) {
    g_change_handler = handler;
}

}  // namespace ogre::options

