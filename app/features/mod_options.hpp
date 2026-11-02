#pragma once

// Mod options (docs/anotherworld/design-mods.md section 6): settings a mod
// declares in its mod.json "options" list, set by the player under the mod's
// row in the MODS tab, and read by the mod's own scripts with options.get(key)
// (lua_runtime.hpp). A mod without the list has no options.
//
//   "options": [
//     {"key": "shared_exp", "name": "SHARED EXP", "type": "toggle", "default": true,
//      "help": "A battle's EXP is shared across the unit."},
//     {"key": "pace", "name": "PACE", "type": "choice", "choices": ["SLOW", "FAST"], "default": "SLOW"},
//     {"key": "bonus", "name": "BONUS", "type": "number", "min": 0, "max": 50, "step": 5, "default": 10,
//      "restart": true}
//   ]
//
// The player's values are mod-options/<mod id>.json in the settings folder,
// one small file per mod, so one mod's options can be backed up or reset on
// their own. A value that is missing, of the wrong type or out of range reads
// as the option's default. An option with "restart": true keeps the value it
// had when the game started until the next start.
//
// The declaration and the values are data: they are checked against the limits
// below, compared with the declared keys and never used as paths or code
// (docs/anotherworld/security.md).

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <json/json.hpp>

namespace ogre::mod_options {

enum class Type { Toggle, Choice, Number };

// Limits of a declaration.
constexpr size_t kMaxOptions = 32;
constexpr size_t kMaxKey = 32;      // a-z, 0-9 and '_'
constexpr size_t kMaxName = 40;
constexpr size_t kMaxHelp = 300;
constexpr size_t kMaxChoices = 8;
constexpr size_t kMaxChoice = 24;
constexpr int64_t kMaxMagnitude = 1000000;  // number limits and defaults
constexpr int64_t kMaxSteps = 1000;         // (max - min) / step
// A values file larger than this is ignored unread.
constexpr uintmax_t kMaxValuesSize = 16u * 1024u;

struct Option {
    std::string key;
    std::string name;
    std::string help;
    Type type = Type::Toggle;
    std::vector<std::string> choices;  // Choice
    int64_t min = 0;                   // Number
    int64_t max = 1;
    int64_t step = 1;
    // Every value is held as an integer: a toggle 0 or 1, a choice the index
    // into `choices`, a number itself.
    int64_t fallback = 0;  // the declared default
    bool restart = false;
};

// Reads a mod.json "options" list. False with `error` set when any entry is
// wrong; the mod is then refused.
bool parse(const nlohmann::json& list, std::vector<Option>& out, std::string& error);

// True when `value` is one the option allows.
bool valid_value(const Option& option, int64_t value);

// mod-options/<file> in the settings folder for a mod id (content_files.hpp
// valid_id). A name Windows keeps for a device (con, nul, com1, ...) or one
// that starts with '.' gets a leading '_'.
std::filesystem::path values_path(std::string_view mod_id);

// The stored values of a mod's options, every key present: the file's value
// when it is valid for the option, else the default.
std::map<std::string, int64_t> read_values(std::string_view mod_id, const std::vector<Option>& options);

// Stores one option's value (a temporary file, then a rename). False when the
// key is not declared, the value is not valid or the write fails.
bool write_value(std::string_view mod_id, const std::vector<Option>& options, std::string_view key, int64_t value);

// The value an option has in this session: the stored value, except for a
// "restart" option, which keeps the value first asked for in this process.
// The Lua runtime asks for every option when it first loads the scripts, which
// fixes the session's values at the start.
int64_t session_value(std::string_view mod_id, const Option& option, int64_t stored);
// The same without fixing anything: `stored` for a restart option nothing has
// asked for yet (the MODS tab before the game starts).
int64_t session_peek(std::string_view mod_id, const Option& option, int64_t stored);

// A fingerprint of the mod-options folder (names, times, sizes): equal
// fingerprints mean no values file changed.
std::string fingerprint();

// The text of a value: "ON"/"OFF", the choice, or the number.
std::string value_text(const Option& option, int64_t value);

}  // namespace ogre::mod_options
