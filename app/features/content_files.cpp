// The content folders and mod folders. See content_files.hpp and
// docs/anotherworld/design-mods.md.

#include "content_files.hpp"

#include "game_host.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>

#include <json/json.hpp>

namespace ogre::content::files {

namespace {

// A mod.json larger than this is refused unread.
constexpr uintmax_t MAX_MANIFEST_SIZE = 64u * 1024u;

struct Root {
    std::filesystem::path dir;
    const char* name;  // the id prefix
};

std::array<Root, 2> roots() {
    return {Root{host::exe_dir() / "content", "content"}, Root{host::pref_dir() / "user-content", "user-content"}};
}

bool is_content_file(const std::filesystem::path& path) {
    const std::filesystem::path ext = path.extension();
    return (ext == ".json" || ext == ".lua") && path.filename() != "mod.json";
}

// mod.json and mod-settings.json nest two levels. A deeper document is
// refused before parsing, because a very deep document is destroyed
// recursively and could overflow the stack (content_json.cpp does the same for
// packs).
constexpr int MAX_DEPTH = 16;

bool too_deep(std::string_view text) {
    int depth = 0;
    bool in_string = false;
    for (size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') {
                i++;
            } else if (c == '"') {
                in_string = false;
            }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            if (++depth > MAX_DEPTH) {
                return true;
            }
        } else if (c == '}' || c == ']') {
            depth--;
        }
    }
    return false;
}

// Reads <folder>/mod.json into `mod`; on a problem `mod.error` says what.
void read_manifest(const std::filesystem::path& folder, Mod& mod) {
    const std::filesystem::path path = folder / "mod.json";
    std::error_code ec;
    if (std::filesystem::file_size(path, ec) > MAX_MANIFEST_SIZE) {
        mod.error = "mod.json is larger than 64 KiB";
        return;
    }
    std::ifstream file(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (too_deep(text)) {
        mod.error = "mod.json nests deeper than 16 levels";
        return;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(text);
        if (!root.is_object()) {
            mod.error = "mod.json is not a JSON object";
            return;
        }
        if (!root.contains("id") || !root["id"].is_string() || !valid_id(root["id"].get<std::string>())) {
            mod.error = "mod.json needs an \"id\" of a-z, 0-9, '-', '_' and '.' (at most 64)";
            return;
        }
        mod.id = root["id"].get<std::string>();
        mod.name = root.value("name", mod.id).substr(0, 64);
        mod.version = root.value("version", std::string()).substr(0, 32);
        mod.author = root.value("author", std::string()).substr(0, 64);
        mod.description = root.value("description", std::string()).substr(0, 1000);
        for (char& c : mod.description) {
            c = static_cast<unsigned char>(c) < 0x20 ? ' ' : c;  // one paragraph
        }
        if (root.contains("url")) {
            mod.url = root["url"].get<std::string>();
            if (!valid_url(mod.url)) {
                mod.url.clear();
                mod.error = "\"url\" must be an http:// or https:// address of at most 200 characters";
                return;
            }
        }
        const int priority = root.value("priority", 0);
        mod.priority = std::clamp(priority, -1000, 1000);
        mod.enabled_default = root.value("enabled", true);
        if (root.contains("requires")) {
            for (const auto& required : root["requires"]) {
                if (!required.is_string() || !valid_id(required.get<std::string>())) {
                    mod.error = "\"requires\" must list mod ids";
                    return;
                }
                mod.required.push_back(required.get<std::string>());
            }
        }
        if (root.contains("options")) {
            std::string error;
            if (!mod_options::parse(root["options"], mod.options, error)) {
                mod.options.clear();
                mod.error = "mod.json: " + error;
                return;
            }
        }
    } catch (const std::exception& e) {
        mod.error = std::string("mod.json: ") + e.what();
    }
}

// A loose file, a LordlyCaliber folder or a mod, in one root.
struct Item {
    std::string name;  // what it sorts by: the file or folder name
    int priority = 0;
    std::filesystem::path path;  // the file, or the folder
    int mod = -1;                // index into the listing's mods
    bool project = false;        // a LordlyCaliber folder without mod.json
};

std::string relative_id(const Root& root, const std::filesystem::path& path) {
    return std::string(root.name) + "/" + path.lexically_relative(root.dir).generic_string();
}

// The index of the first mod `items[i]` requires that comes after it, or -1.
int later_requirement(const std::vector<Item>& items, const std::vector<Mod>& mods, size_t i) {
    for (const std::string& required : mods[items[i].mod].required) {
        for (size_t j = i + 1; j < items.size(); j++) {
            if (items[j].mod >= 0 && mods[items[j].mod].id == required) {
                return static_cast<int>(j);
            }
        }
    }
    return -1;
}

// Puts the mods the player ordered (mod-settings.json "order") into the places
// those mods hold after the priority sort, in the player's order. Mods the
// player never ordered keep their places.
void apply_player_order(std::vector<Item>& items, const std::vector<Mod>& mods, const std::vector<std::string>& order) {
    const auto rank = [&](const Item& item) -> int {
        if (item.mod < 0 || mods[item.mod].id.empty()) {
            return -1;
        }
        const auto found = std::find(order.begin(), order.end(), mods[item.mod].id);
        return found == order.end() ? -1 : static_cast<int>(found - order.begin());
    };
    std::vector<size_t> slots;
    std::vector<Item> ordered;
    for (size_t i = 0; i < items.size(); i++) {
        if (rank(items[i]) >= 0) {
            slots.push_back(i);
            ordered.push_back(items[i]);
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(), [&](const Item& a, const Item& b) { return rank(a) < rank(b); });
    for (size_t k = 0; k < slots.size(); k++) {
        items[slots[k]] = ordered[k];
    }
}

// Moves each mod after the mods it requires in the same root. Only loadable
// mods take part. When the moves do not settle, the mods still ahead of a
// requirement are in a cycle and get an error.
void order_requirements(std::vector<Item>& items, std::vector<Mod>& mods) {
    const auto loadable = [&](const Item& item) {
        return item.mod >= 0 && mods[item.mod].error.empty() && mods[item.mod].enabled;
    };
    const size_t limit = items.size() * items.size() + 1;
    for (size_t pass = 0; pass < limit; pass++) {
        bool moved = false;
        for (size_t i = 0; i < items.size() && !moved; i++) {
            if (!loadable(items[i])) {
                continue;
            }
            const int j = later_requirement(items, mods, i);
            if (j >= 0) {
                const Item item = items[i];
                items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
                items.insert(items.begin() + j, item);
                moved = true;
            }
        }
        if (!moved) {
            return;
        }
    }
    std::vector<int> cyclic;
    for (size_t i = 0; i < items.size(); i++) {
        if (loadable(items[i]) && later_requirement(items, mods, i) >= 0) {
            cyclic.push_back(items[i].mod);
        }
    }
    for (int mod : cyclic) {
        mods[mod].error = "its \"requires\" form a cycle";
    }
}

}  // namespace

bool valid_id(std::string_view id) {
    if (id.empty() || id.size() > 64) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}

bool valid_url(std::string_view url) {
    std::string_view rest;
    if (url.starts_with("https://")) {
        rest = url.substr(8);
    } else if (url.starts_with("http://")) {
        rest = url.substr(7);
    } else {
        return false;
    }
    if (rest.empty() || url.size() > kMaxUrlSize) {
        return false;
    }
    return std::all_of(rest.begin(), rest.end(), [](char c) {
        return c > 0x20 && c < 0x7F && c != '"' && c != '\\' && c != '<' && c != '>' && c != '`';
    });
}

bool valid_key(std::string_view key) {
    if (!key.starts_with("file:")) {
        return valid_id(key);
    }
    const std::string_view name = key.substr(5);
    if (name.empty() || name.size() > 200 || name == "." || name == "..") {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](char c) {
        return static_cast<unsigned char>(c) < 0x20 || c == '/' || c == '\\';
    });
}

bool erase(const Mod& mod, std::string& error) {
    std::error_code ec;
    // A player's item sits directly inside user-content/; a shipped mod is a
    // mod folder (with its mod.json) directly inside content/.
    const std::filesystem::path root =
        std::filesystem::weakly_canonical(mod.shipped ? host::exe_dir() / "content" : host::pref_dir() / "user-content", ec);
    const std::filesystem::path parent = std::filesystem::weakly_canonical(mod.path.parent_path(), ec);
    const std::string name = mod.path.filename().string();
    if (ec || mod.path.empty() || parent != root || name.empty() || name == "." || name == "..") {
        error = mod.shipped ? "only a mod folder directly inside content/ can be deleted"
                            : "only an item directly inside user-content/ can be deleted";
        return false;
    }
    if (mod.shipped && (mod.kind != ModKind::Folder || !std::filesystem::is_regular_file(mod.path / "mod.json", ec))) {
        error = "only a mod folder directly inside content/ can be deleted";
        return false;
    }
    // remove_all removes a link itself, never what it points to.
    std::filesystem::remove_all(mod.path, ec);
    if (ec) {
        error = ec.message();
        std::fprintf(stderr, "[mods] cannot delete %s: %s\n", mod.folder.c_str(), error.c_str());
        return false;
    }
    std::fprintf(stderr, "[mods] deleted %s\n", mod.folder.c_str());
    return true;
}

std::filesystem::path settings_path() {
    return host::pref_dir() / "mod-settings.json";
}

bool parse_settings(std::string_view text, Settings& out, std::string& error) {
    out = Settings{};
    if (text.size() > kMaxSettingsSize) {
        error = "it is larger than 64 KiB";
        return false;
    }
    if (too_deep(text)) {
        error = "it nests deeper than 16 levels";
        return false;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(text);
        if (!root.is_object()) {
            error = "it is not a JSON object";
            return false;
        }
        Settings settings;
        if (root.contains("order")) {
            const nlohmann::json& order = root["order"];
            if (!order.is_array() || order.size() > kMaxSettingsIds) {
                error = "\"order\" must be a list of at most 1024 mod ids";
                return false;
            }
            for (const nlohmann::json& id : order) {
                if (!id.is_string() || !valid_key(id.get<std::string>())) {
                    error = "\"order\" must list mod ids and file: names";
                    return false;
                }
                const std::string value = id.get<std::string>();
                if (std::find(settings.order.begin(), settings.order.end(), value) != settings.order.end()) {
                    error = "\"order\" lists " + value + " twice";
                    return false;
                }
                settings.order.push_back(value);
            }
        }
        if (root.contains("enabled")) {
            const nlohmann::json& enabled = root["enabled"];
            if (!enabled.is_object() || enabled.size() > kMaxSettingsIds) {
                error = "\"enabled\" must map at most 1024 mod ids to true or false";
                return false;
            }
            for (const auto& [id, on] : enabled.items()) {
                if (!valid_key(id) || !on.is_boolean()) {
                    error = "\"enabled\" must map mod ids and file: names to true or false";
                    return false;
                }
                settings.enabled[id] = on.get<bool>();
            }
        }
        out = std::move(settings);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

Settings read_settings(std::string& error) {
    error.clear();
    const std::filesystem::path path = settings_path();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {};
    }
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxSettingsSize) {
        error = ec ? "cannot read the file" : "it is larger than 64 KiB";
        return {};
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot read the file";
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    Settings settings;
    if (!parse_settings(text, settings, error)) {
        return {};
    }
    return settings;
}

bool write_settings(const Settings& settings) {
    nlohmann::json root = nlohmann::json::object();
    root["order"] = settings.order;
    root["enabled"] = nlohmann::json::object();
    for (const auto& [id, on] : settings.enabled) {
        root["enabled"][id] = on;
    }
    const std::filesystem::path path = settings_path();
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
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::fprintf(stderr, "[mods] cannot write %s: %s\n", path.string().c_str(), ec.message().c_str());
        return false;
    }
    return true;
}

Listing scan(const std::map<std::string, bool>& held) {
    Listing listing;
    const Settings settings = read_settings(listing.settings_error);
    std::array<std::vector<Item>, 2> per_root;
    const std::array<Root, 2> all = roots();
    for (size_t r = 0; r < all.size(); r++) {
        std::error_code ec;
        // Adds `mod` as an item, with the player's switch and the caller's hold.
        const auto add = [&](Mod mod, const std::filesystem::path& path) {
            mod.path = path;
            mod.folder = relative_id(all[r], path);
            mod.shipped = r == 0;
            const auto chosen = settings.enabled.find(mod.id);
            mod.chosen = !mod.id.empty() && chosen != settings.enabled.end();
            mod.wanted = mod.chosen ? chosen->second : mod.enabled_default;
            mod.enabled = mod.wanted;
            if (const auto hold = held.find(mod.id); !mod.id.empty() && hold != held.end()) {
                mod.enabled = hold->second;
                mod.pending = mod.enabled != mod.wanted;
            }
            listing.mods.push_back(mod);
            per_root[r].push_back(
                {path.filename().string(), mod.priority, path, static_cast<int>(listing.mods.size()) - 1, false});
        };
        // A loose file or LordlyCaliber folder in user-content/.
        const auto add_loose = [&](ModKind kind, const std::filesystem::path& path) {
            Mod mod;
            mod.kind = kind;
            mod.name = path.filename().string().substr(0, 200);
            mod.id = "file:" + mod.name;
            if (!valid_key(mod.id)) {
                mod.error = "the file name is longer than 200 bytes or holds a control character";
            }
            add(mod, path);
        };
        for (const auto& entry : std::filesystem::directory_iterator(all[r].dir, ec)) {
            const std::filesystem::path path = entry.path();
            if (entry.is_regular_file(ec) && is_content_file(path)) {
                if (r == 0) {
                    per_root[r].push_back({path.filename().string(), 0, path, -1, false});
                } else {
                    add_loose(path.extension() == ".lua" ? ModKind::Script : ModKind::Pack, path);
                }
            } else if (entry.is_directory(ec)) {
                if (std::filesystem::is_regular_file(path / "mod.json", ec)) {
                    Mod mod;
                    read_manifest(path, mod);
                    add(mod, path);
                } else if (std::filesystem::is_regular_file(path / "project.json", ec)) {
                    if (r == 0) {
                        per_root[r].push_back({path.filename().string(), 0, path, -1, true});
                    } else {
                        add_loose(ModKind::Project, path);
                    }
                }
            }
        }
    }
    // Two mods with one id: neither loads, because which one a player meant is
    // not knowable (and directory order is not a rule).
    std::vector<std::string> clashes(listing.mods.size());
    for (size_t m = 0; m < listing.mods.size(); m++) {
        for (size_t other = 0; other < listing.mods.size(); other++) {
            if (other != m && !listing.mods[m].id.empty() && listing.mods[other].id == listing.mods[m].id) {
                clashes[m] = "another mod (" + listing.mods[other].folder + ") has this id";
            }
        }
    }
    for (size_t m = 0; m < listing.mods.size(); m++) {
        if (listing.mods[m].error.empty() && !clashes[m].empty()) {
            listing.mods[m].error = clashes[m];
        }
    }
    // A mod whose requirement is missing, disabled or refused is refused too;
    // repeated until nothing changes, because refusals chain.
    const auto check_requirements = [&listing]() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (Mod& mod : listing.mods) {
                for (const std::string& required : mod.required) {
                    const auto found = std::find_if(listing.mods.begin(), listing.mods.end(), [&](const Mod& other) {
                        return other.id == required && other.error.empty() && other.enabled;
                    });
                    if (mod.error.empty() && found == listing.mods.end()) {
                        mod.error = "requires " + required + ", which is not installed, enabled or loadable";
                        changed = true;
                    }
                }
            }
        }
    };
    check_requirements();
    for (std::vector<Item>& items : per_root) {
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            return a.priority != b.priority ? a.priority < b.priority : a.name < b.name;
        });
        apply_player_order(items, listing.mods, settings.order);
        order_requirements(items, listing.mods);
    }
    check_requirements();
    int order = 0;
    for (const std::vector<Item>& items : per_root) {
        for (const Item& item : items) {
            if (item.mod >= 0) {
                listing.mods[item.mod].order = order++;
            }
        }
    }
    for (size_t r = 0; r < all.size(); r++) {
        const std::vector<Item>& items = per_root[r];
        for (const Item& item : items) {
            if (item.mod < 0 && !item.project) {
                const Kind kind = item.path.extension() == ".lua" ? Kind::Script : Kind::Pack;
                listing.entries.push_back({item.path, relative_id(all[r], item.path), {}, kind});
                continue;
            }
            if (item.project) {
                listing.entries.push_back(
                    {item.path / "project.json", relative_id(all[r], item.path / "project.json"), {}, Kind::Pack});
                continue;
            }
            Mod& mod = listing.mods[item.mod];
            const bool loads = mod.error.empty() && mod.enabled;
            std::vector<std::filesystem::path> files;
            std::error_code ec;
            if (mod.kind == ModKind::Pack || mod.kind == ModKind::Script) {
                files.push_back(item.path);
            } else if (mod.kind == ModKind::Project) {
                files.push_back(item.path / "project.json");
            } else {
                for (const auto& entry : std::filesystem::directory_iterator(item.path, ec)) {
                    if (entry.is_regular_file(ec) && is_content_file(entry.path())) {
                        files.push_back(entry.path());
                    }
                }
                std::sort(files.begin(), files.end());
            }
            for (const std::filesystem::path& file : files) {
                const Kind kind = file.extension() == ".lua" ? Kind::Script : Kind::Pack;
                (loads ? listing.entries : listing.idle).push_back({file, relative_id(all[r], file), mod.id, kind});
                mod.files += loads ? 1 : 0;
            }
        }
    }
    std::stable_sort(listing.mods.begin(), listing.mods.end(),
                     [](const Mod& a, const Mod& b) { return a.order < b.order; });
    return listing;
}

std::string fingerprint() {
    std::string print;
    const auto add = [&print](const std::filesystem::path& path) {
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(path, ec).time_since_epoch().count();
        print += path.generic_string() + "|" + std::to_string(time) + "|" +
                 std::to_string(std::filesystem::file_size(path, ec)) + ";";
    };
    for (const Root& root : roots()) {
        std::error_code ec;
        std::vector<std::filesystem::path> found;
        for (const auto& entry : std::filesystem::directory_iterator(root.dir, ec)) {
            if (entry.is_regular_file(ec)) {
                found.push_back(entry.path());
            } else if (entry.is_directory(ec)) {
                for (const auto& inner : std::filesystem::directory_iterator(entry.path(), ec)) {
                    if (inner.is_regular_file(ec)) {
                        found.push_back(inner.path());
                    }
                }
            }
        }
        std::sort(found.begin(), found.end());
        for (const std::filesystem::path& path : found) {
            add(path);
        }
    }
    add(settings_path());
    return print;
}

}  // namespace ogre::content::files
