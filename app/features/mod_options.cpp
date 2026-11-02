// Mod options. See mod_options.hpp.

#include "mod_options.hpp"

#include "game_host.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>

namespace ogre::mod_options {

namespace {

// The value a "restart" option was first asked for in this process, by
// "<mod id>/<key>".
std::mutex g_mutex;
std::map<std::string, int64_t> g_session;

bool valid_key(std::string_view key) {
    return !key.empty() && key.size() <= kMaxKey && std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

// Printable text of 1..limit bytes: no control characters, so a menu line
// stays one line.
bool valid_text(const nlohmann::json& value, size_t limit) {
    if (!value.is_string()) {
        return false;
    }
    const std::string& text = value.get_ref<const std::string&>();
    return !text.empty() && text.size() <= limit &&
           std::none_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

bool valid_number(const nlohmann::json& value) {
    return value.is_number_integer() && value.get<int64_t>() >= -kMaxMagnitude && value.get<int64_t>() <= kMaxMagnitude;
}

// The JSON value of a stored integer, as the values file holds it.
nlohmann::json to_json(const Option& option, int64_t value) {
    switch (option.type) {
        case Type::Toggle:
            return value != 0;
        case Type::Choice:
            return option.choices[static_cast<size_t>(value)];
        default:
            return value;
    }
}

// The stored integer of a JSON value, or false when it does not fit the option.
bool from_json(const Option& option, const nlohmann::json& value, int64_t& out) {
    switch (option.type) {
        case Type::Toggle:
            if (!value.is_boolean()) {
                return false;
            }
            out = value.get<bool>() ? 1 : 0;
            return true;
        case Type::Choice: {
            if (!value.is_string()) {
                return false;
            }
            const auto found = std::find(option.choices.begin(), option.choices.end(), value.get<std::string>());
            if (found == option.choices.end()) {
                return false;
            }
            out = found - option.choices.begin();
            return true;
        }
        default:
            if (!valid_number(value) || !valid_value(option, value.get<int64_t>())) {
                return false;
            }
            out = value.get<int64_t>();
            return true;
    }
}

std::filesystem::path folder() {
    return host::pref_dir() / "mod-options";
}

// The values file's JSON object, or an empty object when it is missing,
// too large or not an object.
nlohmann::json read_file(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || std::filesystem::file_size(path, ec) > kMaxValuesSize) {
        return nlohmann::json::object();
    }
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    // The file holds one flat object; a nested one is not ours.
    int depth = 0;
    bool in_string = false;
    for (size_t i = 0; i < text.size(); i++) {
        if (in_string) {
            if (text[i] == '\\') {
                i++;  // the escaped character
            } else if (text[i] == '"') {
                in_string = false;
            }
        } else if (text[i] == '"') {
            in_string = true;
        } else if (text[i] == '{' || text[i] == '[') {
            if (++depth > 2) {
                return nlohmann::json::object();
            }
        } else if (text[i] == '}' || text[i] == ']') {
            depth--;
        }
    }
    const nlohmann::json root = nlohmann::json::parse(text, nullptr, false);
    return root.is_object() ? root : nlohmann::json::object();
}

}  // namespace

bool parse(const nlohmann::json& list, std::vector<Option>& out, std::string& error) {
    out.clear();
    if (!list.is_array() || list.size() > kMaxOptions) {
        error = "\"options\" must be a list of at most " + std::to_string(kMaxOptions) + " options";
        return false;
    }
    for (size_t i = 0; i < list.size(); i++) {
        const nlohmann::json& entry = list[i];
        const std::string where = "option " + std::to_string(i + 1);
        if (!entry.is_object()) {
            error = where + ": not an object";
            return false;
        }
        Option option;
        const auto key = entry.find("key");
        if (key == entry.end() || !key->is_string() || !valid_key(key->get<std::string>())) {
            error = where + ": \"key\" must be 1.." + std::to_string(kMaxKey) + " of a-z, 0-9 and '_'";
            return false;
        }
        option.key = key->get<std::string>();
        const std::string at = "option " + option.key;
        if (std::any_of(out.begin(), out.end(), [&](const Option& o) { return o.key == option.key; })) {
            error = at + ": the key is used twice";
            return false;
        }
        const auto name = entry.find("name");
        if (name == entry.end() || !valid_text(*name, kMaxName)) {
            error = at + ": \"name\" must be 1.." + std::to_string(kMaxName) + " characters";
            return false;
        }
        option.name = name->get<std::string>();
        if (const auto help = entry.find("help"); help != entry.end()) {
            if (!valid_text(*help, kMaxHelp)) {
                error = at + ": \"help\" must be 1.." + std::to_string(kMaxHelp) + " characters";
                return false;
            }
            option.help = help->get<std::string>();
        }
        if (const auto restart = entry.find("restart"); restart != entry.end()) {
            if (!restart->is_boolean()) {
                error = at + ": \"restart\" must be true or false";
                return false;
            }
            option.restart = restart->get<bool>();
        }
        const std::string type = entry.value("type", std::string());
        const auto fallback = entry.find("default");
        if (fallback == entry.end()) {
            error = at + ": \"default\" is missing";
            return false;
        }
        if (type == "toggle") {
            option.type = Type::Toggle;
        } else if (type == "choice") {
            option.type = Type::Choice;
            const auto choices = entry.find("choices");
            if (choices == entry.end() || !choices->is_array() || choices->size() < 2 ||
                choices->size() > kMaxChoices) {
                error = at + ": \"choices\" must list 2.." + std::to_string(kMaxChoices) + " choices";
                return false;
            }
            for (const nlohmann::json& choice : *choices) {
                if (!valid_text(choice, kMaxChoice) ||
                    std::find(option.choices.begin(), option.choices.end(), choice.get<std::string>()) !=
                        option.choices.end()) {
                    error = at + ": each choice must be 1.." + std::to_string(kMaxChoice) +
                            " characters and listed once";
                    return false;
                }
                option.choices.push_back(choice.get<std::string>());
            }
        } else if (type == "number") {
            option.type = Type::Number;
            const auto min = entry.find("min");
            const auto max = entry.find("max");
            if (min == entry.end() || max == entry.end() || !valid_number(*min) || !valid_number(*max) ||
                min->get<int64_t>() >= max->get<int64_t>()) {
                error = at + ": \"min\" and \"max\" must be whole numbers, min below max, within +-" +
                        std::to_string(kMaxMagnitude);
                return false;
            }
            option.min = min->get<int64_t>();
            option.max = max->get<int64_t>();
            if (const auto step = entry.find("step"); step != entry.end()) {
                if (!valid_number(*step) || step->get<int64_t>() < 1) {
                    error = at + ": \"step\" must be a whole number of at least 1";
                    return false;
                }
                option.step = step->get<int64_t>();
            }
            if ((option.max - option.min) % option.step != 0 || (option.max - option.min) / option.step > kMaxSteps) {
                error = at + ": max - min must be a multiple of step, at most " + std::to_string(kMaxSteps) +
                        " steps";
                return false;
            }
        } else {
            error = at + ": \"type\" must be \"toggle\", \"choice\" or \"number\"";
            return false;
        }
        if (!from_json(option, *fallback, option.fallback)) {
            error = at + ": \"default\" is not a value of this option";
            return false;
        }
        out.push_back(std::move(option));
    }
    return true;
}

bool valid_value(const Option& option, int64_t value) {
    switch (option.type) {
        case Type::Toggle:
            return value == 0 || value == 1;
        case Type::Choice:
            return value >= 0 && value < static_cast<int64_t>(option.choices.size());
        default:
            return value >= option.min && value <= option.max && (value - option.min) % option.step == 0;
    }
}

std::filesystem::path values_path(std::string_view mod_id) {
    std::string name(mod_id);
    // Windows keeps these names for devices, with any extension.
    static constexpr std::array<std::string_view, 22> DEVICES = {
        "con",  "prn",  "aux",  "nul",  "com1", "com2", "com3", "com4", "com5", "com6", "com7",
        "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    const std::string_view base = std::string_view(name).substr(0, name.find('.'));
    if (name.empty() || name[0] == '.' || std::find(DEVICES.begin(), DEVICES.end(), base) != DEVICES.end()) {
        name = "_" + name;
    }
    return folder() / (name + ".json");
}

std::map<std::string, int64_t> read_values(std::string_view mod_id, const std::vector<Option>& options) {
    std::map<std::string, int64_t> values;
    const nlohmann::json root = options.empty() ? nlohmann::json::object() : read_file(values_path(mod_id));
    for (const Option& option : options) {
        int64_t value = option.fallback;
        if (const auto found = root.find(option.key); found == root.end() || !from_json(option, *found, value)) {
            value = option.fallback;
        }
        values[option.key] = value;
    }
    return values;
}

bool write_value(std::string_view mod_id, const std::vector<Option>& options, std::string_view key, int64_t value) {
    const auto option = std::find_if(options.begin(), options.end(), [&](const Option& o) { return o.key == key; });
    if (option == options.end() || !valid_value(*option, value)) {
        return false;
    }
    // Every declared key with its value; keys the mod no longer declares go.
    std::map<std::string, int64_t> values = read_values(mod_id, options);
    values[std::string(key)] = value;
    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    for (const Option& o : options) {
        root[o.key] = to_json(o, values[o.key]);
    }
    std::error_code ec;
    std::filesystem::create_directories(folder(), ec);
    const std::filesystem::path path = values_path(mod_id);
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        file << root.dump(2) << "\n";
        if (!file) {
            std::fprintf(stderr, "[mods] cannot write %s\n", temp.string().c_str());
            return false;
        }
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::fprintf(stderr, "[mods] cannot write %s: %s\n", path.string().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

int64_t session_value(std::string_view mod_id, const Option& option, int64_t stored) {
    if (!option.restart) {
        return stored;
    }
    std::lock_guard lock(g_mutex);
    return g_session.try_emplace(std::string(mod_id) + "/" + option.key, stored).first->second;
}

int64_t session_peek(std::string_view mod_id, const Option& option, int64_t stored) {
    if (!option.restart) {
        return stored;
    }
    std::lock_guard lock(g_mutex);
    const auto found = g_session.find(std::string(mod_id) + "/" + option.key);
    return found != g_session.end() ? found->second : stored;
}

std::string fingerprint() {
    std::string print;
    std::error_code ec;
    std::vector<std::filesystem::path> found;
    for (const auto& entry : std::filesystem::directory_iterator(folder(), ec)) {
        if (entry.is_regular_file(ec)) {
            found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end());
    for (const std::filesystem::path& path : found) {
        const auto time = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
        print += path.filename().generic_string() + "|" + std::to_string(time) + "|" +
                 std::to_string(std::filesystem::file_size(path, ec)) + ";";
    }
    return print;
}

std::string value_text(const Option& option, int64_t value) {
    switch (option.type) {
        case Type::Toggle:
            return value != 0 ? "ON" : "OFF";
        case Type::Choice:
            return valid_value(option, value) ? option.choices[static_cast<size_t>(value)] : std::string();
        default:
            return std::to_string(value);
    }
}

}  // namespace ogre::mod_options
