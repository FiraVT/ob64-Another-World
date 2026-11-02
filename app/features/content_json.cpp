// JSON content packs. See content_db.hpp (parse_pack) and
// docs/anotherworld/design-content-database.md section 7.

#include "content_db.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>

#include <json/json.hpp>

namespace ogre::content {

namespace {

using nlohmann::json;

// "34", "0x22": a record id or a byte offset.
bool parse_number(const std::string& text, int64_t& out) {
    if (text.empty()) {
        return false;
    }
    try {
        size_t used = 0;
        out = std::stoll(text, &used, 0);
        return used == text.size();
    } catch (const std::exception&) {
        return false;
    }
}

// A field value: a JSON integer, or a string such as "0x07".
bool parse_value(const json& value, int64_t& out) {
    if (value.is_number_integer()) {
        out = value.get<int64_t>();
        return true;
    }
    if (value.is_string()) {
        return parse_number(value.get<std::string>(), out);
    }
    return false;
}

// "09 02", "0902": hex bytes; spaces are allowed between bytes.
bool parse_hex(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    int high = -1;
    for (char c : text) {
        if (c == ' ') {
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
        const int digit = std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(c) - 'a' + 10;
        if (high < 0) {
            high = digit;
        } else {
            out.push_back(static_cast<uint8_t>(high * 16 + digit));
            high = -1;
        }
    }
    return high < 0;
}

bool is_note(const std::string& key) {
    return !key.empty() && key[0] == '_';
}

// A new class's "change_class" block (class_change.hpp): "family_of", the
// standard class (0..80) whose line it joins (default its copy_of), and the
// requirements, each 0..255 with 255 none (the default).
bool read_change_class(const json& block, NewClass& nc, std::string& error) {
    if (!block.is_object()) {
        error = "expected an object";
        return false;
    }
    constexpr const char* REQUIREMENTS[] = {"str", "vit", "int", "men", "agi", "dex", "alignment_min", "alignment_max"};
    nc.change_class = true;
    nc.family_of = nc.copy_of;
    for (const auto& [key, value] : block.items()) {
        if (is_note(key)) {
            continue;
        }
        int64_t number = 0;
        if (!parse_value(value, number)) {
            error = key + ": expected a number";
            return false;
        }
        if (key == "family_of") {
            nc.family_of = static_cast<uint16_t>(number);
            continue;
        }
        const auto* found = std::find(std::begin(REQUIREMENTS), std::end(REQUIREMENTS), key);
        if (found == std::end(REQUIREMENTS)) {
            error = key + ": not a field (family_of, str, vit, int, men, agi, dex, alignment_min, alignment_max)";
            return false;
        }
        if (number < 0 || number > 255) {
            error = key + ": 0..255, 255 for none";
            return false;
        }
        nc.requirements[static_cast<size_t>(found - std::begin(REQUIREMENTS))] = static_cast<uint8_t>(number);
    }
    if (nc.family_of > 80) {
        error = "family_of must be a standard class (0..80)";
        return false;
    }
    return true;
}

// A class in a "battle_forms" rule: a standard class id (1..164) or a new
// class key.
bool read_battle_class(const json& value, int& id, std::string& key, std::string& error) {
    if (value.is_string() && is_class_key(value.get<std::string>())) {
        key = value.get<std::string>();
        return true;
    }
    int64_t number = 0;
    if (!parse_value(value, number) || number < 1 || number >= info(Table::Classes).count) {
        error = "a class id (1.." + std::to_string(info(Table::Classes).count - 1) +
                ") or a new class key like \"my-mod.dragon-knight\"";
        return false;
    }
    id = static_cast<int>(number);
    return true;
}

// The "battle_forms" list (design-battle-transform.md section 6): rules of
// "class", "strikes" (1..9 objects of "form" and "attack", both optional),
// "attacks" (1..9, optional) and "repeat" (a boolean, optional). A "form" is a
// class or a list of 1..32 classes; an "attack" is an attack id or "breath".
bool read_battle_forms(const json& list, Pack& pack, std::string& error) {
    if (!list.is_array()) {
        error = "expected a list of rules";
        return false;
    }
    for (size_t i = 0; i < list.size(); i++) {
        const json& block = list[i];
        const std::string where = "rule " + std::to_string(i + 1);
        if (!block.is_object()) {
            error = where + ": expected an object";
            return false;
        }
        BattleForm form;
        const auto cls = block.find("class");
        if (cls == block.end() || !read_battle_class(*cls, form.cls, form.cls_key, error)) {
            error = where + ": class: " + (cls == block.end() ? std::string("missing") : error);
            return false;
        }
        const auto strikes = block.find("strikes");
        if (strikes == block.end() || !strikes->is_array() || strikes->empty() ||
            strikes->size() > static_cast<size_t>(kMaxBattleStrikes)) {
            error = where + ": strikes: a list of 1.." + std::to_string(kMaxBattleStrikes) + " strikes";
            return false;
        }
        for (const auto& [key, value] : block.items()) {
            if (is_note(key) || key == "class" || key == "strikes") {
                continue;
            }
            if (key == "repeat") {
                if (!value.is_boolean()) {
                    error = where + ": repeat: true or false";
                    return false;
                }
                form.repeat = value.get<bool>();
                continue;
            }
            int64_t number = 0;
            if (key != "attacks") {
                error = where + ": " + key + ": not a field (class, strikes, attacks, repeat)";
                return false;
            }
            if (!parse_value(value, number) || number < 1 || number > kMaxBattleStrikes) {
                error = where + ": attacks: 1.." + std::to_string(kMaxBattleStrikes);
                return false;
            }
            form.attacks = static_cast<int>(number);
        }
        for (size_t s = 0; s < strikes->size(); s++) {
            const json& entry = (*strikes)[s];
            const std::string at = where + ": strike " + std::to_string(s + 1);
            if (!entry.is_object()) {
                error = at + ": expected an object";
                return false;
            }
            BattleFormStrike strike;
            for (const auto& [key, value] : entry.items()) {
                if (is_note(key)) {
                    continue;
                }
                if (key == "form") {
                    const bool list = value.is_array();
                    if (list && (value.empty() || value.size() > static_cast<size_t>(kMaxBattleForms))) {
                        error = at + ": form: a class or a list of 1.." + std::to_string(kMaxBattleForms) + " classes";
                        return false;
                    }
                    for (const json& entry : list ? value : json::array({value})) {
                        int id = -1;
                        std::string class_key;
                        if (!read_battle_class(entry, id, class_key, error)) {
                            error = at + ": form: " + error;
                            return false;
                        }
                        if (class_key.empty()) {
                            strike.forms.push_back(id);
                        } else {
                            strike.form_keys.push_back(class_key);
                        }
                    }
                    continue;
                }
                int64_t number = 0;
                if (key != "attack") {
                    error = at + ": " + key + ": not a field (form, attack)";
                    return false;
                }
                if (value.is_string() && value.get<std::string>() == "breath") {
                    strike.breath = true;
                    continue;
                }
                if (!parse_value(value, number) || number < 0 || number >= info(Table::Attacks).count) {
                    error = at + ": attack: an attack id (0.." + std::to_string(info(Table::Attacks).count - 1) +
                            ") or \"breath\"";
                    return false;
                }
                strike.attack = static_cast<int>(number);
            }
            form.strikes.push_back(std::move(strike));
        }
        pack.battle_forms.push_back(std::move(form));
    }
    return true;
}

// A new class's "art" block: "portrait", "figure_front", "figure_back", each a
// PNG file relative to the pack's folder (is_art_path), and "battle", a folder
// of battle palette swatches (is_art_folder).
bool read_art(const json& block, NewClass& nc, std::string& error) {
    if (!block.is_object()) {
        error = "expected an object";
        return false;
    }
    for (const auto& [key, value] : block.items()) {
        if (is_note(key)) {
            continue;
        }
        if (key == "battle") {
            if (!value.is_string() || !is_art_folder(value.get<std::string>())) {
                error = "battle: a folder inside the pack's folder, like \"art/battle\"";
                return false;
            }
            nc.art_battle = value.get<std::string>();
            continue;
        }
        std::string* slot = key == "portrait" ? &nc.art_portrait
                            : key == "figure_front" ? &nc.art_figure_front
                            : key == "figure_back" ? &nc.art_figure_back
                                                   : nullptr;
        if (slot == nullptr) {
            error = key + ": not a field (portrait, figure_front, figure_back, battle)";
            return false;
        }
        if (!value.is_string() || !is_art_path(value.get<std::string>())) {
            error = key + ": a .png file inside the pack's folder, like \"art/portrait.png\"";
            return false;
        }
        *slot = value.get<std::string>();
    }
    return true;
}

// A LordlyCaliber project (rempred/LordlyCaliber, native/engine/project.cpp,
// Project::manifest): {"format": "LordlyCaliberProject", "version": 1,
// "sourceSha256": ..., "records": {"classes"|"items"|"consumables": {"<id>":
// "<whole record as hex>"}}, ...}. Only changed records are listed. The
// tables, strides and counts are the database's (its revision 1 bases are ROM
// 0x5DB34, 0x6232C and 0x645EC). LordlyCaliber never edits a record's first 4
// bytes, the name pointer, which is 0x20 lower in a project made from
// revision 0; every other record byte is the same on both revisions. So the
// pack takes the bytes after the name pointer that differ from the ROM's
// record, and a project from either revision applies, and only its real
// edits can conflict with another pack.
std::optional<Pack> parse_lordlycaliber(const json& root, Pack pack, std::string& error) {
    if (root.value("version", 0) != 1) {
        error = "LordlyCaliber project version " + root.value("version", json(0)).dump() + " is not supported (1 is)";
        return std::nullopt;
    }
    if (!loaded()) {
        error = "the content database has no ROM records to compare with";
        return std::nullopt;
    }
    constexpr uint16_t NAME_POINTER = 4;
    const TableInfo& class_table = info(Table::Classes);
    // "addedClasses": {"record", "appearance", "name", "description"} as hex,
    // class id 165 + index (Project::manifest). Each becomes a new class with a
    // key; every other section that names one of these ids goes through
    // `added_keys`. LordlyCaliber does not store the class an added class was
    // cloned from, so copy_of is the original class whose record matches the
    // most bytes after the name pointer (the lowest id on a tie).
    std::vector<std::string> added_keys;  // [LordlyCaliber id - 165]
    if (const auto added = root.find("addedClasses"); added != root.end() && !added->is_null()) {
        if (!added->is_array()) {
            error = "addedClasses is not an array";
            return std::nullopt;
        }
        // Ids 172..254 are free for new classes (content_db.hpp class_id).
        constexpr size_t MAX_ADDED = 83;
        if (added->size() > MAX_ADDED) {
            error = "addedClasses has " + std::to_string(added->size()) + " classes; the port has room for " +
                    std::to_string(MAX_ADDED);
            return std::nullopt;
        }
        int appearances = 0;
        int descriptions = 0;
        for (size_t i = 0; i < added->size(); i++) {
            const json& entry = (*added)[i];
            const int lc_id = class_table.count + static_cast<int>(i);
            const std::string where = "addedClasses " + std::to_string(i) + " (class " + std::to_string(lc_id) + ")";
            std::vector<uint8_t> record;
            const auto hex = entry.is_object() ? entry.find("record") : entry.end();
            if (!entry.is_object() || hex == entry.end() || !hex->is_string() ||
                !parse_hex(hex->get<std::string>(), record) || record.size() != class_table.stride) {
                error = where + ": \"record\" must be " + std::to_string(class_table.stride) + " bytes of hex";
                return std::nullopt;
            }
            const std::string key = lordlycaliber_class_key(pack.id, lc_id);
            uint16_t copy_of = 0;
            int best = -1;
            for (uint16_t c = 0; c < class_table.count; c++) {
                const std::span<const uint8_t> rom = original(Table::Classes, c).bytes();
                int same = 0;
                for (uint16_t b = NAME_POINTER; b < class_table.stride; b++) {
                    same += record[b] == rom[b] ? 1 : 0;
                }
                if (same > best) {
                    best = same;
                    copy_of = c;
                }
            }
            pack.new_classes.push_back(NewClass{key, copy_of});
            const size_t edits_before = pack.edits.size();
            const size_t names_before = pack.names.size();
            pack.set_bytes(Table::Classes, 0, NAME_POINTER, std::span<const uint8_t>(record).subspan(NAME_POINTER));
            if (const auto name = entry.find("name"); name != entry.end() && !name->is_null()) {
                std::vector<uint8_t> bytes;
                if (!name->is_string() || !parse_hex(name->get<std::string>(), bytes) ||
                    !pack.set_name_bytes(Table::Classes, 0, bytes)) {
                    error = where + ": \"name\" must be a name in the game's encoding as hex";
                    return std::nullopt;
                }
            }
            for (size_t e = edits_before; e < pack.edits.size(); e++) {
                pack.edits[e].key = key;
            }
            for (size_t n = names_before; n < pack.names.size(); n++) {
                pack.names[n].key = key;
            }
            const auto filled = [&](const char* field) {
                const auto found = entry.find(field);
                return found != entry.end() && found->is_string() && !found->get<std::string>().empty();
            };
            appearances += filled("appearance") ? 1 : 0;
            descriptions += filled("description") ? 1 : 0;
            std::fprintf(stderr, "[content] %s: LordlyCaliber class %d is %s, starting as class %u (%d of %d bytes the same)\n",
                         pack.id.c_str(), lc_id, key.c_str(), copy_of, best, class_table.stride - NAME_POINTER);
            added_keys.push_back(key);
        }
        if (appearances > 0) {
            pack.not_applied.push_back("appearances of " + std::to_string(appearances) + " added classes");
        }
        if (descriptions > 0) {
            pack.not_applied.push_back("descriptions of " + std::to_string(descriptions) + " added classes");
        }
    }
    // The key of an added class id, or null for any other id.
    const auto added_key = [&](int64_t id) -> const std::string* {
        const int64_t index = id - class_table.count;
        return index >= 0 && index < static_cast<int64_t>(added_keys.size()) ? &added_keys[static_cast<size_t>(index)]
                                                                              : nullptr;
    };
    const auto records = root.find("records");
    if (records != root.end()) {
        if (!records->is_object()) {
            error = "\"records\" is not an object";
            return std::nullopt;
        }
        for (const auto& [table_name, rows] : records->items()) {
            const std::optional<Table> table = table_by_name(table_name);
            if (!table || *table == Table::Attacks || *table == Table::Rewards || !rows.is_object()) {
                error = "records: unknown table \"" + table_name + "\"";
                return std::nullopt;
            }
            const TableInfo& t = info(*table);
            int added = 0;
            for (const auto& [id_text, hex] : rows.items()) {
                const std::string where = "records " + table_name + " " + id_text;
                int64_t record_id = 0;
                std::vector<uint8_t> bytes;
                if (!parse_number(id_text, record_id) || record_id < 0 || !hex.is_string() ||
                    !parse_hex(hex.get<std::string>(), bytes)) {
                    error = where + ": not an id and a hex record";
                    return std::nullopt;
                }
                if (bytes.size() != t.stride) {
                    error = where + ": " + std::to_string(bytes.size()) + " bytes, the record is " +
                            std::to_string(t.stride);
                    return std::nullopt;
                }
                if (record_id >= t.count) {
                    // An added class edited after it was added: its whole
                    // record, on the class's key. An added item has no port
                    // id yet (modding plan M4).
                    const std::string* key = *table == Table::Classes ? added_key(record_id) : nullptr;
                    if (key == nullptr) {
                        added++;
                        continue;
                    }
                    pack.set_bytes(*table, 0, NAME_POINTER, std::span<const uint8_t>(bytes).subspan(NAME_POINTER));
                    pack.edits.back().key = *key;
                    continue;
                }
                const uint16_t rid = static_cast<uint16_t>(record_id);
                const std::span<const uint8_t> rom = original(*table, rid).bytes();
                // One edit per run of changed bytes.
                for (uint16_t i = NAME_POINTER; i < t.stride;) {
                    if (bytes[i] == rom[i]) {
                        i++;
                        continue;
                    }
                    uint16_t end = i;
                    while (end < t.stride && bytes[end] != rom[end]) {
                        end++;
                    }
                    pack.set_bytes(*table, rid, i, std::span<const uint8_t>(bytes).subspan(i, end - i));
                    i = end;
                }
            }
            if (added > 0) {
                pack.not_applied.push_back(std::to_string(added) + " added " + table_name);
            }
        }
    }
    // "classNames" and "itemNames": id -> the name's bytes in the game's
    // encoding as hex, for names that differ from the ROM's (Project::manifest).
    for (const auto& [section, table] : {std::pair{"classNames", Table::Classes}, {"itemNames", Table::Items}}) {
        const auto names = root.find(section);
        if (names == root.end() || names->is_null()) {
            continue;
        }
        if (!names->is_object()) {
            error = std::string(section) + " is not an object";
            return std::nullopt;
        }
        int added = 0;
        for (const auto& [id_text, hex] : names->items()) {
            int64_t name_id = 0;
            std::vector<uint8_t> bytes;
            if (!parse_number(id_text, name_id) || name_id < 0 || !hex.is_string() ||
                !parse_hex(hex.get<std::string>(), bytes)) {
                error = std::string(section) + " " + id_text + ": not an id and a hex name";
                return std::nullopt;
            }
            if (name_id >= info(table).count) {
                const std::string* key = table == Table::Classes ? added_key(name_id) : nullptr;
                if (key == nullptr) {
                    added++;
                    continue;
                }
                if (!pack.set_name_bytes(table, 0, bytes)) {
                    error = std::string(section) + " " + id_text + ": the name is empty or too long";
                    return std::nullopt;
                }
                pack.names.back().key = *key;
                continue;
            }
            if (!pack.set_name_bytes(table, static_cast<uint16_t>(name_id), bytes)) {
                error = std::string(section) + " " + id_text + ": the name is empty or too long";
                return std::nullopt;
            }
        }
        if (added > 0) {
            pack.not_applied.push_back(std::to_string(added) + " names of added " + info(table).name);
        }
    }
    // Every other section with content is listed, never dropped silently
    // (design-ecosystem.md section 5).
    for (const auto& [key, value] : root.items()) {
        if (key == "format" || key == "version" || key == "sourceSha256" || key == "sourcePath" ||
            key == "records" || key == "addedClasses" || key == "classNames" || key == "itemNames" || value.is_null() ||
            ((value.is_object() || value.is_array()) && value.empty())) {
            continue;
        }
        pack.not_applied.push_back(key);
    }
    return pack;
}

}  // namespace

std::string lordlycaliber_class_key(std::string_view pack_id, int lc_id) {
    const std::filesystem::path path{std::string(pack_id)};
    const std::filesystem::path stem = path.filename() == "project.json" ? path.parent_path().filename() : path.stem();
    const std::string suffix = ".class-" + std::to_string(lc_id);
    // 64 characters in all (is_class_key).
    const size_t room = 64 - std::string_view("lc.").size() - suffix.size();
    std::string name;
    for (char c : stem.generic_string()) {
        if (name.size() == room) {
            break;
        }
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        name += (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9') || lower == '-' || lower == '_' ? lower : '-';
    }
    if (name.empty()) {
        name = "project";
    }
    return "lc." + name + suffix;
}

std::string dump_table(Table table) {
    const TableInfo& t = info(table);
    const std::string comment = std::string("The original game's ") + t.name +
                       ", written by the port at every start. Editing this file changes nothing: copy a record "
                       "into a .json file in user-content/ under \"" + t.name + "\" and change its fields there.";
    nlohmann::ordered_json records = nlohmann::ordered_json::object();
    static const char* const HEX = "0123456789abcdef";
    for (uint16_t id = 0; id < t.count; id++) {
        const Record record = original(table, id);
        nlohmann::ordered_json fields = nlohmann::ordered_json::object();
        if (t.named) {
            fields["name"] = record_name(table, id);
        }
        for (const Field& f : content::fields(table)) {
            if (std::string_view(f.name) != "name_ptr") {
                fields[f.name] = *record.get(f.name);
            }
        }
        if (table == Table::Rewards) {
            // The class and the outcomes by name, as notes.
            const uint16_t cls = record.u16(0);
            fields["_class"] = cls < info(Table::Classes).count ? record_name(Table::Classes, cls) : "";
            for (int slot = 0; slot < 3; slot++) {
                const uint16_t drop = record.u16(static_cast<uint16_t>(2 + 2 * slot));
                const uint16_t drop_id = drop & 0x7FFFu;
                std::string text = "nothing";
                if ((drop & 0x8000u) != 0 && drop_id < info(Table::Items).count) {
                    text = "item " + std::to_string(drop_id) + " " + record_name(Table::Items, drop_id);
                } else if (drop != 0 && drop < info(Table::Consumables).count) {
                    text = "consumable " + std::to_string(drop) + " " + record_name(Table::Consumables, drop);
                }
                fields["_drop_" + std::to_string(slot)] = text;
            }
        }
        std::string hex;
        for (uint8_t b : record.bytes()) {
            hex += HEX[b >> 4];
            hex += HEX[b & 0xF];
        }
        fields["_hex"] = hex;
        records[std::to_string(id)] = std::move(fields);
    }
    nlohmann::ordered_json out;
    out["_comment"] = comment;
    out[t.name] = std::move(records);
    return out.dump(2) + "\n";
}

namespace {

// A pack nests four levels (tables, records, fields, bytes); a LordlyCaliber
// project nests deeper in sections the port skips. Anything past this is
// refused before parsing, because a very deep document is destroyed
// recursively and could overflow the stack.
constexpr int MAX_DEPTH = 64;

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

std::optional<Pack> parse_pack_checked(std::string_view text, std::string id, std::string& error);

}  // namespace

// Every JSON exception (a parse error, or a value of the wrong type where the
// code reads one) refuses the file instead of reaching the game.
std::optional<Pack> parse_pack(std::string_view text, std::string id, std::string& error) {
    if (text.size() > kMaxPackFileSize) {
        error = "the file is larger than " + std::to_string(kMaxPackFileSize / (1024 * 1024)) + " MiB";
        return std::nullopt;
    }
    if (too_deep(text)) {
        error = "the file nests deeper than " + std::to_string(MAX_DEPTH) + " levels";
        return std::nullopt;
    }
    try {
        return parse_pack_checked(text, std::move(id), error);
    } catch (const std::exception& e) {
        error = e.what();
        return std::nullopt;
    }
}

namespace {

std::optional<Pack> parse_pack_checked(std::string_view text, std::string id, std::string& error) {
    json root;
    try {
        root = json::parse(text.begin(), text.end());
    } catch (const json::parse_error& e) {
        error = e.what();
        return std::nullopt;
    }
    if (!root.is_object()) {
        error = "the file is not a JSON object";
        return std::nullopt;
    }
    if (const auto format = root.find("format"); format != root.end()) {
        if (*format != "LordlyCaliberProject") {
            error = "unknown format " + format->dump();
            return std::nullopt;
        }
        Pack lc;
        lc.id = std::move(id);
        lc.source = lc.id;
        return parse_lordlycaliber(root, std::move(lc), error);
    }

    Pack pack;
    pack.id = std::move(id);
    pack.source = pack.id;
    for (const auto& [table_name, records] : root.items()) {
        if (is_note(table_name)) {
            continue;
        }
        if (table_name == "battle_forms") {
            if (!read_battle_forms(records, pack, error)) {
                error = "battle_forms: " + error;
                return std::nullopt;
            }
            continue;
        }
        const std::optional<Table> table = table_by_name(table_name);
        if (!table) {
            error = "unknown table \"" + table_name + "\"";
            return std::nullopt;
        }
        if (!records.is_object()) {
            error = table_name + ": expected an object of records by id";
            return std::nullopt;
        }
        const TableInfo& t = info(*table);
        for (const auto& [id_text, record] : records.items()) {
            const std::string where = table_name + " " + id_text;
            int64_t record_id = 0;
            // A new class: a key instead of an id, in the tables indexed by
            // class id (design-new-classes.md section 5). Its edits carry the
            // key; the database gives the id when the packs apply.
            const bool keyed = (*table == Table::Classes || *table == Table::Appearances) && is_class_key(id_text);
            if (!keyed && (!parse_number(id_text, record_id) || record_id < 0 || record_id >= t.count)) {
                error = where + ": the id is not a record of this table (0.." + std::to_string(t.count - 1) +
                        "), or a new class key like \"my-mod.dragon-knight\"";
                return std::nullopt;
            }
            if (!record.is_object()) {
                error = where + ": expected an object of fields";
                return std::nullopt;
            }
            const uint16_t rid = static_cast<uint16_t>(record_id);
            const size_t edits_before = pack.edits.size();
            const size_t names_before = pack.names.size();
            if (keyed && *table == Table::Classes) {
                const auto copy_of = record.find("copy_of");
                int64_t from = -1;
                if (copy_of == record.end() || !parse_value(*copy_of, from) || from < 0 ||
                    from >= info(Table::Classes).count) {
                    error = where + ": a new class needs \"copy_of\": the id of the class it starts as (0.." +
                            std::to_string(info(Table::Classes).count - 1) + ")";
                    return std::nullopt;
                }
                NewClass nc{id_text, static_cast<uint16_t>(from)};
                if (const auto change = record.find("change_class"); change != record.end()) {
                    if (!read_change_class(*change, nc, error)) {
                        error = where + ": change_class: " + error;
                        return std::nullopt;
                    }
                }
                if (const auto art = record.find("art"); art != record.end()) {
                    if (!read_art(*art, nc, error)) {
                        error = where + ": art: " + error;
                        return std::nullopt;
                    }
                }
                pack.new_classes.push_back(std::move(nc));
            }
            // The whole record first, so the named fields and bytes below
            // apply on top of it.
            if (const auto whole = record.find("record"); whole != record.end()) {
                std::vector<uint8_t> bytes;
                if (!whole->is_string() || !parse_hex(whole->get<std::string>(), bytes) ||
                    !pack.set_record(*table, rid, bytes)) {
                    error = where + ": \"record\" must be " + std::to_string(t.stride) + " bytes of hex";
                    return std::nullopt;
                }
            }
            for (const auto& [key, value] : record.items()) {
                if (is_note(key) || key == "record" || (keyed && (key == "copy_of" || key == "change_class" || key == "art"))) {
                    continue;
                }
                if (key == "name") {
                    std::string name_error;
                    if (!value.is_string() || !pack.set_name(*table, rid, value.get<std::string>(), name_error)) {
                        error = where + ": name: " + (value.is_string() ? name_error : "expected a string");
                        return std::nullopt;
                    }
                    continue;
                }
                if (key == "bytes") {
                    if (!value.is_object()) {
                        error = where + ": \"bytes\" must map offsets to hex";
                        return std::nullopt;
                    }
                    for (const auto& [offset_text, hex] : value.items()) {
                        int64_t offset = 0;
                        std::vector<uint8_t> bytes;
                        if (!parse_number(offset_text, offset) || offset < 0 || !hex.is_string() ||
                            !parse_hex(hex.get<std::string>(), bytes) ||
                            static_cast<uint64_t>(offset) + bytes.size() > t.stride) {
                            error = where + ": bytes at " + offset_text + " are not hex inside the record";
                            return std::nullopt;
                        }
                        pack.set_bytes(*table, rid, static_cast<uint16_t>(offset), bytes);
                    }
                    continue;
                }
                int64_t number = 0;
                if (field(*table, key) == nullptr) {
                    error = where + ": unknown field \"" + key + "\"";
                    return std::nullopt;
                }
                if (!parse_value(value, number) || !pack.set_field(*table, rid, key, number)) {
                    error = where + ": " + key + " is not a number that fits the field";
                    return std::nullopt;
                }
            }
            if (keyed) {
                for (size_t i = edits_before; i < pack.edits.size(); i++) {
                    pack.edits[i].key = id_text;
                }
                for (size_t i = names_before; i < pack.names.size(); i++) {
                    pack.names[i].key = id_text;
                }
            }
        }
    }
    return pack;
}

}  // namespace

}  // namespace ogre::content
