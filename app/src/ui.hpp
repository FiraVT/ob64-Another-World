// The app's ASCII UI: the launcher screen and the in-game overlay share it.
//
// The launcher (launcher.cpp) and the Esc overlay (overlay.cpp) show the same
// content in the same style, so the panel model, its rows, the tab bar and the
// text rendering live here. A Panel is either in Launcher mode (every tab
// active) or Overlay mode (CONTROLS, SETTINGS, DEBUG and MAIN are active; the
// rest are shown disabled, because loading a ROM and toggling mods make no sense
// while the game is running). MAIN is active in both modes because it carries
// EXIT GAME: the overlay's way to end the game with the mouse or the keyboard,
// which is what a window manager that offers no close button path needs.
//
// Rendering is the bitmap font (font.hpp) through an SDL_Renderer, into
// supersampled TextLayer textures, exactly as the launcher drew before.
#pragma once

#include "save_profiles.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <SDL.h>

#include <librecomp/mods.hpp>

#include "content_files.hpp"
#include "font.hpp"
#include "input_map.hpp"

namespace ogre::ui {

// A text block rasterised once into a supersampled texture and scaled down by
// the GPU, so the bitmap glyphs come out anti-aliased at any integer scale.
class TextLayer {
public:
    TextLayer() = default;
    TextLayer(const TextLayer&) = delete;
    TextLayer& operator=(const TextLayer&) = delete;
    TextLayer(TextLayer&& other) noexcept;
    TextLayer& operator=(TextLayer&& other) noexcept;
    ~TextLayer();

    void destroy();

    // Safe to call repeatedly (every frame): each call replaces the texture.
    void build(SDL_Renderer* renderer, const Font& font, const std::string& text,
               int scale, SDL_Color color, int supersample = 4);

    void draw(SDL_Renderer* renderer, int center_x, int left, int top,
              bool centered) const;

    bool empty() const { return texture_ == nullptr; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    SDL_Texture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// Splits `text` so no line is wider than `max_chars` characters. Words are kept
// whole when they fit; a single over-long word (a file name) is cut.
std::vector<std::string> wrap_text(const std::string& text, size_t max_chars);

int chars_per_line(const Font& font, int scale, int max_width_px);

// `text` shortened with a trailing "..." so it fits `max_width` pixels at
// `scale`.
std::string truncate_to_width(const Font& font, const std::string& text, int scale,
                              int max_width);

// --- the tabbed panel ---------------------------------------------------------

enum class Tab { Main, Mods, Controls, Graphics, Audio, Gameplay, Saves, Roster, Debug, Count };

constexpr int kTabCount = static_cast<int>(Tab::Count);

// The tab whose content sets the panel's on-screen position. CONTROLS is the
// tallest tab, so the header, the tab bar and the first row sit where they do
// on CONTROLS whatever tab is active.
constexpr Tab kLayoutReferenceTab = Tab::Controls;

// What activating a row asks the caller to do. Everything that touches the
// runtime (the file picker, the mod system) is handled outside the panel.
enum class RowAction {
    None,
    BrowseRom,
    Play,
    // The EXIT GAME row of the MAIN tab. The launcher stops its own loop on it;
    // the overlay asks the runtime to quit.
    QuitGame,
    ModToggled,
    OptionChanged,
    BindingChanged,
    SettingChanged,
};

class Panel {
public:
    enum class Mode { Launcher, Overlay };

    enum class RowKind {
        Section,
        StartGame,
        Rom,
        ExitGame,
        // MAIN tab in the game: back to the title screen (save_profiles.hpp
        // request_title). Asks twice, because unsaved progress is lost.
        ReturnToTitle,
        // MAIN tab, SAVES (save_backup.hpp): back up the battery save, restore
        // one of the newest backups (its file name in `title`), import a save.
        SaveBackup,
        SaveRestore,
        SaveImport,
        // MAIN tab in the game, PROFILE SAVES (save_profiles.hpp): save into the
        // leader's profile, and one row per profile save (`option` indexes
        // profile_entries_).
        SaveHere,
        ProfileLoad,
        ProfileHeader,  // a profile's name: opens and closes its saves
        Mod,
        Option,
        Binding,
        ResetBindings,
        GameSpeed,
        Widescreen,
        FrameRate,
        Resolution,
        Antialias,
        Display,
        Sound,
        Mouse,
        // GAMEPLAY tab: one of the game's own Settings rows (ui.cpp game_settings(),
        // `option` is the index), read from and written to the game's memory.
        GameOption,
        Volume,
        ChaosFrame,
        // ROSTER tab: the sort switch, one read-only row per character, and the
        // page switch.
        RosterSort,
        RosterChar,
        RosterPage,
        // DEBUG tab: one line of the War Director observer (war_director.hpp);
        // `record` is the line index.
        DirectorLine,
        // MODS tab: one read-only line about the content packs or the scripts
        // (content_db.hpp and lua_runtime.hpp status()); `title` on the left,
        // `detail` on the right.
        ContentLine,
        // MODS tab, the player's mods (content_files.hpp; `mod` indexes
        // content_mods_, `title` is the item's folder or file). ContentMod is
        // an item's row with five cells (switch, details, up, down, delete);
        // ContentModInfo is a read-only details line (`title` left, `detail`
        // right); ContentModsHeader names the columns. Neither of the last two
        // is selectable.
        ContentMod,
        ContentModInfo,
        ContentModsHeader,
        // An open mod's page (mod.json "url"): ENTER, A or a click opens it in
        // the web browser.
        ContentModLink,
        // Turns every content mod on, or off.
        ContentModsAll,
        ContentModsNone,
        // One row per entry of this project's option table (options.hpp):
        // ChoiceRow + the Choice id (ui.cpp choice_row / choice_of).
        ChoiceRow = 1000,
    };

    struct Row {
        RowKind kind = RowKind::Section;
        std::string title;  // Section rows; the left column of ROSTER rows
        size_t mod = 0;     // index into the mod list
        size_t option = 0;  // index into that mod's schema options
        int binding = -1;   // index into the input map (RowKind::Binding)
        // ROSTER rows: the character record, the page count (RosterPage), and
        // the right column.
        int record = 0;
        int target = 0;
        std::string detail;
    };

    Panel(Mode mode, std::string game_id, std::filesystem::path rom);

    Mode mode() const { return mode_; }

    // Rebuilds the rows for the active tab. Mods can change underneath the panel
    // (enabling one can pull in a dependency), so this is called after a toggle.
    void reload();
    void set_rom(std::filesystem::path rom);

    // --- DEBUG tab readouts ---------------------------------------------------
    // The DEBUG tab shows live game state, so the panel does not read guest
    // memory itself: the caller samples the running game and pushes the value in
    // before draw_panel. The launcher has no running game and never calls the
    // setter, which is what makes its DEBUG tab say "GAME NOT RUNNING".
    void set_chaos_frame(int value) {
        chaos_frame_ = value;
        chaos_frame_known_ = true;
    }
    void clear_chaos_frame() { chaos_frame_known_ = false; }

    Tab tab() const { return tab_; }
    void set_tab(Tab tab);
    void switch_tab(int delta);
    bool tab_enabled(Tab tab) const;
    std::string tab_label(Tab tab) const;

    size_t row_count() const { return rows_.size(); }
    size_t selected() const { return selected_; }
    void set_selected(size_t index);
    // Puts the selection on the first selectable row of the active tab.
    void select_first();
    bool selectable(size_t index) const;
    bool selected_is_rom() const;
    bool selected_is_exit() const;
    RowKind row_kind(size_t index) const;
    bool move(int delta);

    // The text in the left column of a row (the name plus the keyboard or
    // selected value).
    std::string left_text(size_t index) const;
    // The text in the right column (a description, a file name, a hint).
    std::string right_text(size_t index) const;
    // The right column of an arbitrary row. measure_panel sizes the layout
    // reference tab with this, and that tab is not the active one.
    std::string right_text_for(const Row& row) const;

    // The active tab's rows, and the layout reference tab's rows. Both are
    // rebuilt by reload().
    const std::vector<Row>& rows() const { return rows_; }
    const std::vector<Row>& layout_rows() const { return layout_rows_; }

    // `direction` is +1 forward, -1 backward.
    RowAction activate(size_t index, int direction);
    // Picks option `option` of a radio row (GAME SPEED, WIDESCREEN, RESOLUTION,
    // MSAA, WINDOW, SOUNDS) or a cell of the VOLUME slider directly (a click on
    // it).
    RowAction choose_option(size_t index, size_t option);

    // The choices a row offers, for a front end that draws them itself (the
    // RmlUi menu, options_view.cpp): the labels of a radio row with the live one
    // marked, or the 11 cells of a VOLUME slider (`slider` set, `current` the
    // last filled cell). Empty `labels` for any other row.
    struct RowOptions {
        std::vector<std::string> labels;
        int current = -1;
        bool slider = false;
    };
    RowOptions row_options(size_t index) const;

    // A line explaining the row, shown in the menu's hint line while the row is
    // selected; empty for rows without one.
    std::string row_tooltip(size_t index) const;

    // True for a row that LEFT/RIGHT steps: a radio row, the volume slider, or a
    // mod option. The three callers that need this (the launcher's key handler,
    // the overlay's and the pad's) share it so a new row kind cannot be added to
    // one and forgotten in another.
    bool row_is_steppable(size_t index) const;

    // --- pad navigation -------------------------------------------------------
    // A gamepad drives the same panel the keyboard does. The launcher and the
    // overlay translate SDL controller buttons into these and hand them to
    // `pad_command`, so both panels navigate the same way.
    enum class PadCommand {
        None,
        Up,
        Down,
        Left,
        Right,
        Accept,
        Back,
        TabPrev,
        TabNext,
    };

    // The command an SDL controller button maps to, or `None`. The D-pad moves
    // and steps, A and Start accept, B is Back, and the shoulder buttons switch
    // tab. `SDL_CONTROLLER_BUTTON_BACK` is deliberately not here: the overlay
    // uses it to open and close itself, and the launcher ignores it.
    static PadCommand pad_command_for_button(int button);

    // Applies one command and returns the action the caller must run (Play,
    // QuitGame, BrowseRom, ...). `Back` returns `None`: the overlay closes on it
    // and the launcher ignores it.
    RowAction pad_command(PadCommand command);

    // --- rebinding capture ----------------------------------------------------
    // Activating a Binding row starts capture; the caller then feeds key and pad
    // events until one is accepted or the capture is cancelled.
    bool capturing() const { return capture_row_ >= 0; }
    void begin_capture(size_t index);
    void cancel_capture();
    // Returns true when the event was consumed by the capture.
    bool capture_key(SDL_Scancode code);
    bool capture_pad_button(int code);
    // Clears one slot of the capturing row (Backspace = keyboard, Delete = pad).
    bool capture_clear(bool keyboard);

    // --- hit testing ----------------------------------------------------------
    // Filled by draw_panel() each frame; clicks use the previous frame's
    // geometry, which is one frame of lag and not visible.
    struct Geometry {
        // One clickable radio marker of a radio row. `rect` is absolute, and
        // the entry is only present for markers draw_panel actually laid out.
        struct Option {
            int row = -1;
            int option = -1;
            SDL_Rect rect{};
        };
        std::vector<SDL_Rect> tabs;
        std::vector<SDL_Rect> rows;
        std::vector<Option> options;
    };
    struct Hit {
        enum class Kind { None, Tab, Row, RowOption } kind = Kind::None;
        int index = -1;   // tab or row index
        int option = -1;  // option index, for RowOption
    };
    void set_geometry(Geometry geometry) { geometry_ = std::move(geometry); }
    Hit hit_test(int x, int y) const;

private:
    void rebuild_rows();
    std::vector<Row> build_rows(Tab tab) const;
    void append_profile_rows(std::vector<Row>& rows) const;
    size_t first_selectable() const;

    Mode mode_;
    Tab tab_ = Tab::Main;
    std::string game_id_;
    std::filesystem::path rom_;
    std::vector<recomp::mods::ModDetails> mods_;
    std::vector<Row> rows_;
    std::vector<Row> layout_rows_;
    Geometry geometry_;
    size_t selected_ = 0;
    int capture_row_ = -1;
    bool chaos_frame_known_ = false;
    int chaos_frame_ = 0;
    // ROSTER tab: 0 level (highest first), 1 class, 2 name; and the page.
    int roster_sort_ = 0;
    int roster_page_ = 0;
    // The SAVES rows' last result, shown on the BACK UP row.
    std::string save_message_;
    mutable std::vector<save_profiles::Entry> profile_entries_;  // refilled with the rows
    int delete_armed_ = -1;  // the profile save whose DELETE was chosen once
    bool title_armed_ = false;  // RETURN TO TITLE was chosen once
    // Profiles whose saves are shown (MAIN tab). The newest profile opens the
    // first time the rows are built; the player opens and closes the rest.
    mutable std::set<std::string> open_profiles_;
    mutable bool profiles_seeded_ = false;

public:
    // A ProfileHeader row whose saves are shown.
    bool profile_open(size_t index) const;
    // A ContentMod row whose details are shown.
    bool content_mod_open(size_t index) const;
    // What the RmlUi menu draws in a ContentMod row's columns. `tone` is
    // "on", "off", "pending", "warn" (loaded with conflicts or errors) or
    // "bad" (refused); `cell` is the cell in focus on the selected row, or -1.
    struct ModCells {
        std::string number;
        std::string name;
        std::string note;  // the start of the description
        std::string status;
        std::string tone;
        bool on = false;
        bool open = false;
        bool armed = false;  // DELETE was chosen once
        bool first = false;
        bool last = false;
        int cell = -1;
    };
    ModCells content_mod_cells(size_t index) const;
    // LEFT/RIGHT on a row: moves the cell focus on a ContentMod row, and
    // steps any other steppable row. The launcher, the overlay and the pad
    // all call this for LEFT/RIGHT.
    RowAction step(size_t index, int direction);
    // A mouse drag of a ContentMod row dropped on another row (options.rml).
    RowAction drag_content_mod(size_t from_row, size_t to_row);
    // A DELETE waiting for its confirmation: the dialog's text and the item's
    // row, or an empty text and -1. The dialog's Delete is cell 4 of that row
    // again, Cancel is cell 5.
    std::string delete_prompt() const;
    int delete_prompt_row() const;
    // False when the dialog only explains why the item cannot be deleted now
    // (it adds new classes and is loaded); the dialog then offers OK alone.
    bool delete_allowed() const { return !delete_blocked_; }
    // Cancels a waiting DELETE; false when none was waiting. Esc and the pad's
    // B call it before they close anything.
    bool cancel_prompt();
    // Rebuilds the MODS tab when the content database read the mods again (a
    // change applied, or a file changed). The overlay calls it every frame.
    void poll_content();

private:
    // The content mods (docs/anotherworld/design-mods.md), in load order,
    // with what the MODS tab says about each.
    struct ContentModView {
        content::files::Mod mod;
        std::string status;  // the row's right column
        std::vector<std::pair<std::string, std::string>> info;  // the open rows
    };
    void load_content_mods();
    void select_content_row(const std::string& folder);
    // Applies `change` to mod-settings.json and reloads the mods. A broken
    // file is replaced by one holding this change only.
    void change_mod_settings(const std::function<void(content::files::Settings&)>& change);
    static void set_content_mod(content::files::Settings& settings, const content::files::Mod& mod, bool on);
    void reorder_content_mod(size_t from, size_t to);
    void delete_content_mod(size_t which);
    RowAction use_content_cell(size_t index, int cell);
    std::vector<ContentModView> content_mods_;
    std::string content_settings_error_;
    std::vector<std::string> content_class_errors_;
    std::map<std::string, bool> content_held_;
    std::set<std::string> open_content_mods_;
    int content_generation_ = -1;
    bool content_live_ = false;  // the game is running: packs apply while it runs
    int mod_cell_ = 1;           // the cell in focus on a ContentMod row
    std::string erase_armed_;    // the item whose DELETE was chosen once
    bool delete_blocked_ = false;  // ...and it cannot be deleted while the game runs
    std::string mod_message_;    // the last delete result, in the hint line
};

// The panel's on-screen geometry from the last frame. `top_y` is where the panel
// background starts; the tab bar is drawn above the rows.
struct PanelDraw {
    int panel_left = 0;
    int panel_width = 0;
    int height = 0;   // tab bar + rows + hint
    int text_scale = 2;
};

// The panel's layout for one frame, computed without drawing so a caller can
// centre the whole screen before it commits to a top edge.
struct PanelMetrics {
    int panel_left = 0;
    int panel_width = 0;
    int scale = 2;
    int line_height = 0;
    int row_height = 0;
    int rows_top = 0;   // absolute y of the first row
    int hint_y = 0;     // absolute y of the hint line
    int height = 0;     // total panel height for the active tab, background included
    // The height the block reserves: the taller of the active tab's panel and
    // the layout reference tab's (kLayoutReferenceTab). A caller centres the
    // whole block and places its footer with this, so the header, the tab bar and
    // the footer stay put when the active tab changes. A tab that is taller than
    // the reference still grows the block, because it genuinely needs the room.
    int layout_height = 0;
    std::vector<int> row_heights;
    // The wrapped lines of a section row's title (a section row's left column
    // spans the whole panel, because a section has no description) and of a
    // row's right column.
    std::vector<std::vector<std::string>> left_lines;
    std::vector<std::vector<std::string>> right_lines;
};

// `output_height` (0 = unknown) lets UI SCALE LARGE fall back to the normal size
// for a tab that would not fit.
PanelMetrics measure_panel(const Font& font, Panel& panel, int output_width,
                           int top_y, float ui_scale, int output_height = 0);

// Writes the renderer's current output as a binary PPM. Development aid for the
// screenshots in `docs/proofs`: it reads back exactly what the UI drew.
bool write_ppm(SDL_Renderer* renderer, const std::filesystem::path& path);

// Draws the tab bar and the active tab's rows, and stores the hit-test geometry
// in `panel`. `top_y` is the top of the tab bar.
PanelDraw draw_panel(SDL_Renderer* renderer, const Font& font, Panel& panel,
                     int output_width, int output_height, int top_y, float ui_scale);

}  // namespace ogre::ui

