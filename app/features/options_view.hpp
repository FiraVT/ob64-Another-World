// The Esc menu drawn with RmlUi inside the game window (app/ui/options.rml).
//
// The overlay (overlay.cpp) keeps the menu's model: a ui::Panel on the main
// thread, with the keyboard and pad handling it always had. Each frame it
// publishes a snapshot of the panel here, and this module draws it on the render
// thread from rml_ui.cpp's hook. Mouse clicks on the document come back as
// commands the overlay drains on the main thread. With RmlUi unavailable the
// overlay keeps its own SDL window.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Rml {
class Context;
}

namespace ogre::options_view {

struct Option {
    std::string label;
    bool current = false;  // the live choice (radio) or a filled cell (slider)
};

struct Row {
    std::string kind;   // "section", "row", "radio", "slider"
    std::string left;
    std::string right;
    std::string note;  // a second line under `left` (a mod's description)
    std::string tone;  // a mod's status colour: "on", "off", "pending", "warn", "bad"
    bool on = false;   // a mod's switch
    std::string more;  // a mod's MORE / LESS link
    std::string number;  // a mod's place in the load order
    bool armed = false;  // a mod's DELETE was chosen once
    bool first = false;  // a mod that cannot move up
    bool last = false;   // a mod that cannot move down
    int cell = -1;       // the cell in focus on the selected mod row
    bool selectable = false;
    bool selected = false;
    std::vector<Option> options;
};

struct Tab {
    std::string label;
    bool enabled = false;
    bool active = false;
};

struct Snapshot {
    std::vector<Tab> tabs;
    std::vector<Row> rows;
    std::string hint;
    bool capturing = false;
    std::string confirm;   // a DELETE waiting for its confirmation (MODS tab)
    int confirm_row = -1;  // that item's row
    bool confirm_allowed = true;  // false: the dialog explains why it cannot be deleted
};

struct Command {
    enum class Kind { Tab, Activate, Choose, Close, Drop };
    Kind kind = Kind::Activate;
    int a = 0;  // tab or row index; the dragged row (Drop)
    int b = 0;  // option index (Choose); the row it was dropped on (Drop)
};

// Render thread (rml_ui.cpp): registers the "options" data model and loads
// options.rml. `update` shows or hides the document and copies the latest
// snapshot into the model.
bool init(Rml::Context* context, const std::filesystem::path& asset_dir);
void update();

// Main thread.
bool available();                 // the document loaded; the overlay may use it
void set_visible(bool visible);
void publish(Snapshot snapshot);
std::vector<Command> take_commands();

// Any thread: true while the menu is up (mouse routing in sdl_platform.cpp).
bool visible();

}  // namespace ogre::options_view

