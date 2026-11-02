#pragma once

// The content folders and mod folders (docs/anotherworld/design-mods.md). The
// content database and the Lua runtime both load from this one listing, so
// they agree on which files are loaded and in which order.
//
// Two roots, in this order: content/ beside the executable (the project's) and
// user-content/ in the settings folder (the player's). In each root:
//   *.json, *.lua              loose files
//   <folder>/mod.json          a mod: its *.json, *.lua and project.json
//   <folder>/project.json      a LordlyCaliber project without a mod.json
// Other folders (content/examples/) are not loaded.
//
// Within a root, loose files and mods are ordered by priority (a mod's
// mod.json "priority", 0 for anything else), then by name; the player's order
// from mod-settings.json moves the mods it lists; a mod loads after the mods it
// "requires". A later file wins a conflict.
//
// mod-settings.json in the settings folder holds the player's choices from the
// MODS tab: the load order and on/off per mod id. A mod the player has not
// switched uses its mod.json "enabled". A broken file is ignored as a whole.

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "mod_options.hpp"

namespace ogre::content::files {

enum class Kind { Pack, Script };

struct Entry {
    std::filesystem::path path;
    std::string id;   // "user-content/my-mod/drops.lua"
    std::string mod;  // the item's id (Mod::id), or empty for a file in content/
    Kind kind;
};

// What a listed item is. In user-content/ every loose file and LordlyCaliber
// folder is an item of its own, so the player can switch, order and delete it
// like a mod folder; its id is "file:" plus its file or folder name.
enum class ModKind { Folder, Pack, Script, Project };

struct Mod {
    ModKind kind = ModKind::Folder;
    std::filesystem::path path;  // the folder or the file
    std::string id;       // mod.json "id", or "file:<name>"
    std::string name;     // mod.json "name", or the id; the file name for a loose item
    std::string version;  // mod.json "version"
    std::string author;   // mod.json "author"
    std::string description;  // mod.json "description"
    std::string url;          // mod.json "url": the mod's page, checked by valid_url
    std::string folder;   // "user-content/my-mod"
    int priority = 0;
    std::vector<std::string> required;  // mod.json "requires"
    bool enabled_default = true;  // mod.json "enabled"
    bool chosen = false;          // the player set on/off (mod-settings.json)
    bool wanted = true;           // the player's choice, or the default
    // What loads now: `wanted`, unless the caller held this mod at another
    // state (scan's `held`); then `pending` is set.
    bool enabled = true;
    bool pending = false;
    std::string error;  // why it is not loaded; empty when it is
    int files = 0;
    int order = 0;  // place in the load order among all mods, from 0
    bool shipped = false;  // in content/ beside the executable
    std::vector<mod_options::Option> options;  // mod.json "options" (mod_options.hpp)
};

struct Listing {
    std::vector<Entry> entries;  // in load order
    // Every mod.json found, and every loose file and LordlyCaliber folder in
    // user-content/, loaded or not, in load order.
    std::vector<Mod> mods;
    // Content files of installed mods that do not load now (off, or held off),
    // so a caller can see what turning one on would load.
    std::vector<Entry> idle;
    std::string settings_error;  // why mod-settings.json was ignored; empty when read or absent
};

// Reads both roots. Cheap enough to call about once a second. `held` maps mod
// ids to the on/off state they keep whatever the player chose (the content
// database holds mods that add new classes at their state from the start).
Listing scan(const std::map<std::string, bool>& held = {});

// A fingerprint of every file the listing depends on (names, times, sizes,
// mod.json and mod-settings.json included): equal fingerprints mean nothing
// changed.
std::string fingerprint();

// --- mod-settings.json -------------------------------------------------------

struct Settings {
    std::vector<std::string> order;      // item ids (valid_key), first loads first
    std::map<std::string, bool> enabled;  // item id -> on, for items the player switched
};

// A mod-settings.json larger than this is ignored unread.
constexpr uintmax_t kMaxSettingsSize = 64u * 1024u;
// At most this many ids in "order" and in "enabled".
constexpr size_t kMaxSettingsIds = 1024;

// Parses the file's text. False with `error` set when any part is wrong; then
// nothing of it applies.
bool parse_settings(std::string_view text, Settings& out, std::string& error);

std::filesystem::path settings_path();

// Reads mod-settings.json. A missing file is an empty Settings and no error.
Settings read_settings(std::string& error);

// Writes mod-settings.json (a temporary file, then a rename). False on a
// write error.
bool write_settings(const Settings& settings);

bool valid_id(std::string_view id);

// A mod's page (mod.json "url"): http:// or https://, then 1 to 200 more
// characters of printable ASCII with no space, quote, backslash or angle
// bracket. Only a player's click opens it (the MODS tab), in the web browser.
constexpr size_t kMaxUrlSize = 200;
bool valid_url(std::string_view url);

// A key mod-settings.json may hold: a mod id, or "file:" and a file or folder
// name (1 to 200 bytes, no '/', '\' or control characters). Keys are only
// compared with the listed items, never used as paths.
bool valid_key(std::string_view key);

// Erases an item for good: a player's item (a mod folder with everything in it,
// a loose file or a LordlyCaliber folder) directly inside user-content/, or a
// shipped mod folder (with mod.json) directly inside content/. Anything else is
// refused. The MODS tab keeps a deleted shipped mod OFF in mod-settings.json,
// so an update that puts the folder back leaves it off. False with `error` set
// on a refusal or a failure.
bool erase(const Mod& mod, std::string& error);

}  // namespace ogre::content::files
