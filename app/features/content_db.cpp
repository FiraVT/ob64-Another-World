// The content database. See content_db.hpp and
// docs/anotherworld/design-content-database.md.

#include "content_db.hpp"

#include "class_animation.hpp"
#include "class_change.hpp"
#include "content_files.hpp"

#include "game_addresses.hpp"
#include "game_host.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>

#include <librecomp/game.hpp>
#include <librecomp/overlays.hpp>

#include <json/json.hpp>

// The upper-half shift every class-table reader adds (tools/relocate_tables.py
// patches the generated code): 0 reads the ROM's table, addr::kClassTableShift
// the moved copy. Set once, by tick, after the copy is filled.
extern "C" unsigned int ogre_class_table_shift = 0;
extern "C" unsigned int ogre_appearance_shift = 0;
// The portrait id table's asset id, which tools/relocate_tables.py makes the
// game read from here; class_art.cpp points it at a copy with new portraits.
extern "C" unsigned int ogre_portrait_table_hi = 0x01DFu;
extern "C" unsigned int ogre_portrait_table_lo = 0xB36Eu;

namespace ogre::content {

namespace {

// Segment B data: ROM = vram - 0x8016AF80 + 0x40E80 (tools/class_catalog.py,
// tools/scenemap.py).
constexpr uint32_t SEGMENT_B_VRAM = 0x8016AF80u;
constexpr uint32_t SEGMENT_B_ROM = 0x40E80u;
// Record names lie in segment B data below this address (LordlyCaliber,
// attack_data.cpp: retail names 0x8016AF80..0x80190F10).
constexpr uint32_t SEGMENT_B_NAMES_END = 0x80190F10u;

// A two-line name is 0x0E, line 1, 0x10 'c', line 2, 0x0F (the ROM's own
// names, e.g. "Altar of Resurrection"; LordlyCaliber encodeClassName).
constexpr uint8_t NAME_OPEN = 0x0Eu;
constexpr uint8_t NAME_BREAK = 0x10u;
constexpr uint8_t NAME_CLOSE = 0x0Fu;
// LordlyCaliber's limit for an item name; used for every line here.
constexpr size_t MAX_NAME_LINE = 32;

// Class 1's name pointer (0x80190388, "Soldier") is a fixed ROM value. It
// tells that the ROM is revision 1 (revision 0 has every table 0x20 lower,
// docs/anotherworld/notes-revisions.md) and, in RAM, that segment B is loaded.
constexpr uint32_t CHECK_ADDRESS = addr::kClasses + addr::kClassSize;
constexpr uint32_t CHECK_VALUE = 0x80190388u;

constexpr std::array<TableInfo, kTableCount> TABLES = {{
    {"classes", addr::kClasses, addr::kClassSize, addr::kClassCount, 0x5DB34u, true, 256},
    {"attacks", addr::kAttacks, addr::kAttackSize, addr::kAttackCount, 0x6099Cu, true, addr::kAttackCount},
    {"spells", addr::kSpellMaps, addr::kSpellMapSize, addr::kSpellMapCount, 0x6138Cu, false, addr::kSpellMapCount},
    {"items", addr::kItems, addr::kItemSize, addr::kItemCount, 0x6232Cu, true, addr::kItemCount},
    {"consumables", addr::kConsumables, addr::kConsumableSize, addr::kConsumableCount, 0x645ECu, true,
     addr::kConsumableCount},
    {"appearances", addr::kAppearances, addr::kAppearanceSize, addr::kAppearanceCount, 0x64C00u, false, 256},
    {"rewards", addr::kRewards, addr::kRewardSize, addr::kRewardCount, addr::kRewardsRom, false, addr::kRewardCount},
}};

// New class ids: 172..254 (design-new-classes.md 5). The game uses 165..171
// for soldier figures: the class animation resource (asset 0x00315736) has
// rows for 172 class ids, 165..171 holding soldier animation variants. 255
// stays a marker.
constexpr uint16_t FIRST_NEW_CLASS = 172;
constexpr uint16_t LAST_NEW_CLASS = 254;

// Field names are the ones a JSON pack uses. A field is named when a game
// reader of its offset and width is known (the accessors in segment B,
// func_8016D220..func_8016F9F0; design-content-database.md section 2); the
// meanings past the first fields come from LordlyCaliber (project.cpp
// fields()). Any other byte is reached with Pack::set_bytes.
constexpr Field CLASS_FIELDS[] = {
    {"name_ptr", 0x00, 4, false},
    {"size", 0x04, 1, false},
    {"body", 0x05, 1, false},
    {"leadership", 0x06, 1, false},
    {"hp_base", 0x08, 2, false},
    {"hp_growth", 0x0A, 1, false},
    {"str_base", 0x0C, 2, false},
    {"str_growth", 0x0E, 1, false},
    {"vit_base", 0x10, 2, false},
    {"vit_growth", 0x12, 1, false},
    {"int_base", 0x14, 2, false},
    {"int_growth", 0x16, 1, false},
    {"men_base", 0x18, 2, false},
    {"men_growth", 0x1A, 1, false},
    {"agi_base", 0x1C, 2, false},
    {"agi_growth", 0x1E, 1, false},
    {"dex_base", 0x20, 2, false},
    {"dex_growth", 0x22, 1, false},
    {"luck", 0x23, 1, false},
    {"alignment", 0x24, 1, false},
    {"res_physical", 0x25, 1, false},
    {"res_wind", 0x26, 1, false},
    {"res_fire", 0x27, 1, false},
    {"res_earth", 0x28, 1, false},
    {"res_water", 0x29, 1, false},
    {"res_virtue", 0x2A, 1, false},
    {"res_bane", 0x2B, 1, false},
    {"movement", 0x2C, 1, false},
    {"equipment_0", 0x2E, 2, false},
    {"equipment_1", 0x30, 2, false},
    {"equipment_2", 0x32, 2, false},
    {"equipment_3", 0x34, 2, false},
    {"fixed_equipment", 0x36, 1, false},
    {"front_attack", 0x37, 1, false},
    {"front_count", 0x38, 1, false},
    {"middle_attack", 0x39, 1, false},
    {"middle_count", 0x3A, 1, false},
    {"back_attack", 0x3B, 1, false},
    {"back_count", 0x3C, 1, false},
    {"phys_attack", 0x3D, 1, false},
    {"mag_attack", 0x3E, 1, false},
    {"phys_defense", 0x3F, 1, false},
    {"mag_defense", 0x40, 1, false},
    {"base_class", 0x41, 1, false},
    {"first_transition_level", 0x42, 1, false},
    {"intermediate_class", 0x43, 1, false},
    {"second_transition_level", 0x44, 1, false},
    {"key", 0x45, 1, false},
    {"element", 0x46, 1, false},
    {"item_limit", 0x47, 1, false},
};

constexpr Field ATTACK_FIELDS[] = {
    {"name_ptr", 0x00, 4, false},
    {"kind", 0x04, 1, false},
    {"element", 0x05, 1, false},
    {"formula", 0x09, 1, false},
};

// A spell map row: the attack each element turns the row's generic attack
// into (game_addresses.hpp kSpellMaps). The element names are the item
// resistances' (res_wind ..).
constexpr Field SPELL_FIELDS[] = {
    {"physical", 0x00, 1, false},
    {"wind", 0x01, 1, false},
    {"fire", 0x02, 1, false},
    {"earth", 0x03, 1, false},
    {"water", 0x04, 1, false},
    {"virtue", 0x05, 1, false},
    {"bane", 0x06, 1, false},
};

constexpr Field ITEM_FIELDS[] = {
    {"name_ptr", 0x00, 4, false},
    {"category", 0x04, 1, false},
    {"element", 0x05, 1, false},
    {"grade", 0x06, 1, false},
    {"price", 0x08, 2, false},
    {"str", 0x0A, 1, true},
    {"int", 0x0B, 1, true},
    {"agi", 0x0C, 1, true},
    {"dex", 0x0D, 1, true},
    {"vit", 0x0E, 1, true},
    {"men", 0x0F, 1, true},
    {"res_physical", 0x11, 1, true},
    {"res_wind", 0x12, 1, true},
    {"res_fire", 0x13, 1, true},
    {"res_earth", 0x14, 1, true},
    {"res_water", 0x15, 1, true},
    {"res_virtue", 0x16, 1, true},
    {"res_bane", 0x17, 1, true},
    {"growth_hp_str", 0x18, 1, false, 2, 6},
    {"growth_int", 0x18, 1, false, 2, 2},
    {"growth_agi", 0x18, 1, false, 2, 0},
    {"growth_dex", 0x19, 1, false, 2, 6},
    {"growth_vit", 0x19, 1, false, 2, 4},
    {"growth_men", 0x19, 1, false, 2, 2},
    {"growth_luck", 0x19, 1, false, 2, 0},
    {"flags", 0x1A, 1, false},
};

// Outcomes: equipment id | 0x8000 (32769 = item 1), or a consumable id.
constexpr Field REWARD_FIELDS[] = {
    {"class", 0x00, 2, false},
    {"drop_0", 0x02, 2, false},
    {"drop_1", 0x04, 2, false},
    {"drop_2", 0x06, 2, false},
};

constexpr Field CONSUMABLE_FIELDS[] = {
    {"name_ptr", 0x00, 4, false},
    {"category", 0x04, 1, false},
    {"price", 0x06, 2, false},
    {"tier", 0x08, 1, false},
};

struct TableData {
    std::vector<uint8_t> original;  // the ROM's records, back to back
    std::vector<uint8_t> current;   // with every pack applied
    std::vector<uint8_t> base;      // with the code and JSON packs, not the runtime ones
    std::vector<int16_t> owner;     // per byte: the last pack that wrote it, -1 none
    std::vector<uint8_t> owned;     // per byte: the bits some pack wrote
    std::vector<std::vector<uint8_t>> names;  // the ROM's encoded name per record
    std::vector<std::vector<uint8_t>> current_names;
};

struct Write {
    uint32_t address;
    uint8_t value;
    int8_t table;  // the table it belongs to, -1 for the content arena
    // A record past the ROM's count (a new class): it exists only in the moved
    // copy, because the original address holds the next table.
    bool moved_only = false;
};


// The change check runs every this many ticks (about once a second).
constexpr int RELOAD_CHECK_TICKS = 60;

bool g_loaded = false;
bool g_failed = false;
std::array<TableData, kTableCount> g_tables;
std::vector<Pack> g_code_packs;
std::vector<Pack> g_file_packs;
std::vector<Pack> g_runtime_packs;
std::vector<Pack> g_packs;  // g_code_packs, g_file_packs, then g_runtime_packs
std::vector<FileError> g_file_errors;
std::string g_fingerprint;  // of the content folders at the last read
std::vector<files::Mod> g_mods;
std::string g_settings_error;  // why mod-settings.json was ignored at the last read
int g_generation = 0;
// The held mods, and every mod's load state at the first read: a held mod
// keeps that state until the next start (held_mods). A mod is held when it
// adds new classes (their ids go into saves) or ships with the game in
// content/ (the Another World pack's rules change saved state,
// design-another-world-pack.md section 3.3).
std::set<std::string> g_held;
std::map<std::string, int> g_class_counts;
std::map<std::string, bool> g_start_loaded;
bool g_start_known = false;
int g_check_countdown = RELOAD_CHECK_TICKS;
std::vector<Conflict> g_conflicts;
// Every byte a pack changed, as guest addresses; rebuilt by apply_packs.
std::vector<Write> g_writes;
// Bytes a reload released: their ROM value, written once on the next tick.
std::vector<Write> g_restores;
bool g_announce = true;
// The class table's readers use the copy at addr::kMovedClasses
// (docs/anotherworld/design-new-classes.md); OGRE_CLASS_TABLE_MOVE=0 keeps
// them on the ROM's table.
bool g_move = true;
bool g_move_filled = false;
// New class key -> id, kept in saves/class-ids.json.
std::map<std::string, uint16_t, std::less<>> g_class_ids;
std::vector<std::string> g_class_errors;

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd8(rdram, address)) << 24) |
           (static_cast<uint32_t>(rd8(rdram, address + 1)) << 16) |
           (static_cast<uint32_t>(rd8(rdram, address + 2)) << 8) | rd8(rdram, address + 3);
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    uint8_t& byte = rdram[(address & 0x1FFFFFFFu) ^ 3u];
    if (byte != value) {
        byte = value;
    }
}

TableData& data(Table table) {
    return g_tables[static_cast<int>(table)];
}

void note_conflict(Table table, uint16_t id, const std::string& earlier, const std::string& later) {
    for (const Conflict& c : g_conflicts) {
        if (c.table == table && c.id == id && c.earlier == earlier && c.later == later) {
            return;
        }
    }
    g_conflicts.push_back({table, id, earlier, later});
}

bool edit_fits(const Edit& edit) {
    const TableInfo& t = info(edit.table);
    return edit.id < t.capacity && edit.offset + edit.bytes.size() <= t.stride;
}

std::filesystem::path class_ids_path() {
    return host::pref_dir() / "saves" / "class-ids.json";
}

void read_class_ids() {
    g_class_ids.clear();
    std::ifstream file(class_ids_path(), std::ios::binary);
    if (!file) {
        return;
    }
    try {
        const nlohmann::json root = nlohmann::json::parse(file);
        for (const auto& [key, value] : root.items()) {
            if (is_class_key(key) && value.is_number_integer()) {
                const int id = value.get<int>();
                if (id >= FIRST_NEW_CLASS && id <= LAST_NEW_CLASS) {
                    g_class_ids[key] = static_cast<uint16_t>(id);
                }
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[content] %s is not readable (%s); new class ids start again\n",
                     class_ids_path().string().c_str(), e.what());
    }
}

void write_class_ids() {
    nlohmann::ordered_json root = nlohmann::ordered_json::object();
    root["_comment"] = "New class ids, given once and kept: a save holds these numbers. Do not edit.";
    for (const auto& [key, id] : g_class_ids) {
        root[key] = id;
    }
    std::error_code ec;
    std::filesystem::create_directories(class_ids_path().parent_path(), ec);
    std::ofstream(class_ids_path(), std::ios::binary) << root.dump(2) << "\n";
}

// The id of a new class key, given out on first sight. nullopt when all 90
// are taken.
std::optional<uint16_t> allocate_class_id(const std::string& key) {
    if (const auto found = g_class_ids.find(key); found != g_class_ids.end()) {
        return found->second;
    }
    for (uint16_t id = FIRST_NEW_CLASS; id <= LAST_NEW_CLASS; id++) {
        const bool taken = std::any_of(g_class_ids.begin(), g_class_ids.end(),
                                       [id](const auto& entry) { return entry.second == id; });
        if (!taken) {
            g_class_ids[key] = id;
            write_class_ids();
            std::fprintf(stderr, "[content] new class %s gets id %u\n", key.c_str(), id);
            return id;
        }
    }
    return std::nullopt;
}

// Rebuilds current, the owners, the conflicts and the bridge's write list from
// the originals and the packs in load order. A byte the old write list held
// and the new one does not goes on the restore list with its ROM value.
void apply_packs() {
    std::lock_guard lock(records_mutex());
    const std::vector<Write> before = std::move(g_writes);
    g_packs = g_code_packs;
    g_packs.insert(g_packs.end(), g_file_packs.begin(), g_file_packs.end());
    const size_t runtime_start = g_packs.size();
    g_packs.insert(g_packs.end(), g_runtime_packs.begin(), g_runtime_packs.end());
    g_conflicts.clear();
    g_class_errors.clear();
    std::vector<std::pair<uint16_t, uint16_t>> new_classes;  // (id, template) for the animation resource
    std::vector<class_change::Standard> standard;  // the new classes Change Class offers
    for (TableData& t : g_tables) {
        t.current_names = t.names;
        t.current = t.original;
        t.base = t.original;
        t.owner.assign(t.original.size(), -1);
        t.owned.assign(t.original.size(), 0);
    }
    for (size_t p = 0; p < g_packs.size(); p++) {
        if (p == runtime_start) {
            for (TableData& t : g_tables) {
                t.base = t.current;
            }
        }
        // New classes: an id, then the class record and the appearance row
        // copied from the class it starts from (as they stand so far).
        for (const NewClass& nc : g_packs[p].new_classes) {
            const std::optional<uint16_t> id = allocate_class_id(nc.key);
            if (!id) {
                g_class_errors.push_back(nc.key + ": all 83 new class ids are taken");
                continue;
            }
            for (const Table table : {Table::Classes, Table::Appearances}) {
                TableData& t = data(table);
                const size_t stride = info(table).stride;
                for (size_t i = 0; i < stride; i++) {
                    t.current[*id * stride + i] = t.current[nc.copy_of * stride + i];
                    t.owner[*id * stride + i] = static_cast<int16_t>(p);
                    t.owned[*id * stride + i] = 0xFFu;
                }
            }
            data(Table::Classes).current_names[*id] = data(Table::Classes).current_names[nc.copy_of];
            new_classes.emplace_back(*id, nc.copy_of);
            if (nc.change_class) {
                standard.push_back({*id, nc.family_of, nc.requirements});
            }
        }
        for (const Edit& keyed : g_packs[p].edits) {
            Edit edit_copy;
            const Edit* resolved = &keyed;
            if (!keyed.key.empty()) {
                const auto found = g_class_ids.find(keyed.key);
                if (found == g_class_ids.end()) {
                    g_class_errors.push_back(g_packs[p].id + ": no pack makes the class " + keyed.key);
                    continue;
                }
                edit_copy = keyed;
                edit_copy.id = found->second;
                resolved = &edit_copy;
            }
            const Edit& edit = *resolved;
            TableData& t = data(edit.table);
            const size_t start = static_cast<size_t>(edit.id) * info(edit.table).stride + edit.offset;
            for (size_t i = 0; i < edit.bytes.size(); i++) {
                const int16_t before = t.owner[start + i];
                // A runtime pack builds on the packs below it, so it is not a
                // conflict with them.
                if (before >= 0 && static_cast<size_t>(before) != p && p < runtime_start &&
                    (t.owned[start + i] & edit.mask) != 0) {
                    note_conflict(edit.table, edit.id, g_packs[static_cast<size_t>(before)].id, g_packs[p].id);
                }
                t.current[start + i] =
                    static_cast<uint8_t>((t.current[start + i] & ~edit.mask) | (edit.bytes[i] & edit.mask));
                t.owner[start + i] = static_cast<int16_t>(p);
                t.owned[start + i] |= edit.mask;
            }
        }
    }
    // Names: the last pack that renames a record wins. A changed name goes
    // into the content arena, NUL-terminated and 4-byte aligned, and the
    // record's name pointer is pointed at it; a name equal to the ROM's is
    // left alone.
    if (runtime_start == g_packs.size()) {
        for (TableData& t : g_tables) {
            t.base = t.current;
        }
    }
    std::vector<Write> arena;
    uint32_t cursor = addr::kNamesArena;
    for (int k = 0; k < kTableCount; k++) {
        TableData& t = g_tables[k];
        std::vector<int16_t> namer(t.names.size(), -1);
        for (size_t p = 0; p < g_packs.size(); p++) {
            for (const NameEdit& keyed : g_packs[p].names) {
                if (static_cast<int>(keyed.table) != k) {
                    continue;
                }
                NameEdit name = keyed;
                if (!keyed.key.empty()) {
                    const auto found = g_class_ids.find(keyed.key);
                    if (found == g_class_ids.end()) {
                        continue;
                    }
                    name.id = found->second;
                }
                if (namer[name.id] >= 0 && static_cast<size_t>(namer[name.id]) != p) {
                    note_conflict(name.table, name.id, g_packs[static_cast<size_t>(namer[name.id])].id, g_packs[p].id);
                }
                namer[name.id] = static_cast<int16_t>(p);
                t.current_names[name.id] = name.bytes;
            }
        }
        for (size_t id = 0; id < t.names.size(); id++) {
            // A new class with no name of its own keeps its template's
            // pointer (copied with the record).
            if (namer[id] < 0 || t.current_names[id] == t.names[id]) {
                continue;
            }
            const std::vector<uint8_t>& bytes = t.current_names[id];
            if (cursor + bytes.size() + 1 > addr::kNamesArenaEnd) {
                std::fprintf(stderr, "[content] the content arena is full; %s %zu keeps its name\n", TABLES[k].name,
                             id);
                continue;
            }
            for (size_t i = 0; i <= bytes.size(); i++) {
                arena.push_back({cursor + static_cast<uint32_t>(i), i < bytes.size() ? bytes[i] : uint8_t{0}, -1});
            }
            const size_t pointer = id * TABLES[k].stride;
            for (int i = 0; i < 4; i++) {
                t.current[pointer + i] = static_cast<uint8_t>(cursor >> (24 - 8 * i));
                t.owner[pointer + i] = namer[id];
                t.owned[pointer + i] = 0xFFu;
            }
            cursor = (cursor + static_cast<uint32_t>(bytes.size()) + 1u + 3u) & ~3u;
        }
    }
    g_writes.clear();
    for (int k = 0; k < kTableCount; k++) {
        const TableData& t = g_tables[k];
        for (size_t i = 0; i < t.current.size(); i++) {
            if (t.owner[i] >= 0) {
                const bool beyond = i >= static_cast<size_t>(TABLES[k].count) * TABLES[k].stride;
                g_writes.push_back(
                    {TABLES[k].address + static_cast<uint32_t>(i), t.current[i], static_cast<int8_t>(k), beyond});
            }
        }
    }
    // The arena lies above every table, so the list stays in address order.
    g_writes.insert(g_writes.end(), arena.begin(), arena.end());
    // New classes need battle animation rows; they exist only with the moved
    // tables (class_animation.hpp).
    class_change::set_new_classes_present(g_move && !new_classes.empty());
    class_animation::set_new_classes(g_move ? std::move(new_classes) : std::vector<std::pair<uint16_t, uint16_t>>{});
    // New classes Change Class offers (class_change.hpp), also only with them.
    class_change::set_standard_classes(g_move ? std::move(standard) : std::vector<class_change::Standard>{});
    // Both lists are in address order.
    for (const Write& w : before) {
        const auto kept = std::lower_bound(g_writes.begin(), g_writes.end(), w.address,
                                           [](const Write& a, uint32_t address) { return a.address < address; });
        if (kept == g_writes.end() || kept->address != w.address) {
            for (int k = 0; k < kTableCount; k++) {
                const uint32_t start = TABLES[k].address;
                if (w.address >= start && w.address < start + g_tables[k].original.size()) {
                    g_restores.push_back(
                        {w.address, g_tables[k].original[w.address - start], w.table, w.moved_only});
                }
            }
        }
    }
    g_announce = true;
}

// content-dump/<table>.json in the settings folder: the ROM's records, for
// reference. Rewritten at every start.
void write_dump() {
    const std::filesystem::path dir = host::pref_dir() / "content-dump";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    for (int k = 0; k < kTableCount; k++) {
        const std::filesystem::path path = dir / (std::string(TABLES[k].name) + ".json");
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << dump_table(static_cast<Table>(k));
        if (!file) {
            std::fprintf(stderr, "[content] cannot write %s\n", path.string().c_str());
            return;
        }
    }
}

// The held mods at their state from the first read (held_mods). A class mod
// installed after that is held off.
std::map<std::string, bool> held_map() {
    std::map<std::string, bool> held;
    for (const std::string& id : g_held) {
        const auto found = g_start_loaded.find(id);
        held[id] = found != g_start_loaded.end() && found->second;
    }
    return held;
}

std::optional<Pack> read_pack(const files::Entry& entry, std::string& error) {
    std::error_code ec;
    if (std::filesystem::file_size(entry.path, ec) > kMaxPackFileSize) {
        error = "the file is larger than " + std::to_string(kMaxPackFileSize / (1024 * 1024)) + " MiB";
        return std::nullopt;
    }
    std::ifstream file(entry.path, std::ios::binary);
    if (!file) {
        error = "cannot read the file";
        return std::nullopt;
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parse_pack(text, entry.id, error);
}

// `again` is false on the outer call: when the set of class mods changed, the
// listing is read once more with the new holds.
void read_files(bool again = false) {
    const files::Listing listing = files::scan(held_map());
    g_fingerprint = files::fingerprint();
    g_mods = listing.mods;
    if (listing.settings_error != g_settings_error && !listing.settings_error.empty()) {
        std::fprintf(stderr, "[content] mod-settings.json ignored: %s\n", listing.settings_error.c_str());
    }
    g_settings_error = listing.settings_error;
    if (!g_start_known) {
        g_start_known = true;
        for (const files::Mod& mod : g_mods) {
            g_start_loaded[mod.id] = mod.enabled && mod.error.empty();
        }
    }
    g_file_packs.clear();
    g_file_errors.clear();
    std::map<std::string, int> classes;  // item id -> new classes its packs add
    bool shipped = false;
    for (const files::Entry& entry : listing.entries) {
        if (entry.kind != files::Kind::Pack) {
            continue;
        }
        shipped = shipped || entry.id.starts_with("content/");
        std::string error;
        std::optional<Pack> pack = read_pack(entry, error);
        if (pack && !pack->new_classes.empty() && !entry.mod.empty()) {
            classes[entry.mod] += static_cast<int>(pack->new_classes.size());
        }
        if (pack) {
            for (const Edit& edit : pack->edits) {
                if (!edit_fits(edit)) {
                    error = "an edit is outside its table";
                    pack.reset();
                    break;
                }
            }
        }
        if (pack) {
            if (!pack->not_applied.empty()) {
                std::string parts;
                for (const std::string& part : pack->not_applied) {
                    parts += (parts.empty() ? "" : ", ") + part;
                }
                std::fprintf(stderr, "[content] %s: not applied yet: %s\n", entry.id.c_str(), parts.c_str());
            }
            pack->folder = entry.path.parent_path();
            g_file_packs.push_back(std::move(*pack));
        } else {
            g_file_errors.push_back({entry.id, error});
            std::fprintf(stderr, "[content] %s refused: %s\n", entry.id.c_str(), error.c_str());
        }
    }
    // A mod that is off now may add classes too; its packs are read only to
    // know that.
    for (const files::Entry& entry : listing.idle) {
        std::string error;
        if (entry.kind == files::Kind::Pack) {
            const std::optional<Pack> pack = read_pack(entry, error);
            if (pack && !pack->new_classes.empty()) {
                classes[entry.mod] += static_cast<int>(pack->new_classes.size());
            }
        }
    }
    g_class_counts = classes;
    std::set<std::string> held;
    for (const auto& [id, count] : classes) {
        held.insert(id);
    }
    for (const files::Mod& mod : g_mods) {
        if (mod.shipped && !mod.id.empty()) {
            held.insert(mod.id);
        }
    }
    if (held != g_held) {
        g_held = std::move(held);
        if (!again) {
            read_files(true);
            return;
        }
    }
    g_generation++;
    for (const files::Mod& mod : g_mods) {
        if (!mod.error.empty()) {
            std::fprintf(stderr, "[content] mod %s refused: %s\n", mod.folder.c_str(), mod.error.c_str());
        } else if (mod.pending) {
            std::fprintf(stderr, "[content] mod %s adds new classes; turning it %s applies at the next start\n",
                         mod.folder.c_str(), mod.wanted ? "on" : "off");
        }
    }
    if (!shipped) {
        std::fprintf(stderr, "[content] no packs in content/ beside the executable; the class rebalance is off\n");
    }
}

}  // namespace

const TableInfo& info(Table table) {
    return TABLES[static_cast<int>(table)];
}

std::optional<Table> table_by_name(std::string_view name) {
    for (int k = 0; k < kTableCount; k++) {
        if (name == TABLES[k].name) {
            return static_cast<Table>(k);
        }
    }
    return std::nullopt;
}

std::span<const Field> fields(Table table) {
    switch (table) {
        case Table::Classes:
            return CLASS_FIELDS;
        case Table::Attacks:
            return ATTACK_FIELDS;
        case Table::Spells:
            return SPELL_FIELDS;
        case Table::Items:
            return ITEM_FIELDS;
        case Table::Consumables:
            return CONSUMABLE_FIELDS;
        case Table::Rewards:
            return REWARD_FIELDS;
        case Table::Appearances:
            return {};
    }
    return {};
}

const Field* field(Table table, std::string_view name) {
    for (const Field& f : fields(table)) {
        if (name == f.name) {
            return &f;
        }
    }
    return nullptr;
}

bool Pack::set_name(Table table, uint16_t id, std::string_view text, std::string& error) {
    if (!info(table).named) {
        error = std::string("the ") + info(table).name + " table has no names";
        return false;
    }
    std::optional<std::vector<uint8_t>> bytes = encode_name(table, text, error);
    if (!bytes) {
        return false;
    }
    names.push_back({table, id, std::move(*bytes), {}});
    return true;
}

bool Pack::set_name_bytes(Table table, uint16_t id, std::span<const uint8_t> bytes) {
    if (!info(table).named || bytes.empty() || bytes.size() > 2 * MAX_NAME_LINE + 4 ||
        std::find(bytes.begin(), bytes.end(), uint8_t{0}) != bytes.end()) {
        return false;
    }
    names.push_back({table, id, std::vector<uint8_t>(bytes.begin(), bytes.end()), {}});
    return true;
}

void Pack::set_bytes(Table table, uint16_t id, uint16_t offset, std::span<const uint8_t> bytes) {
    edits.push_back({table, id, offset, std::vector<uint8_t>(bytes.begin(), bytes.end()), 0xFFu, {}});
}

bool Pack::set_record(Table table, uint16_t id, std::span<const uint8_t> bytes) {
    if (bytes.size() != info(table).stride) {
        return false;
    }
    set_bytes(table, id, 0, bytes);
    return true;
}

bool spell_target(int64_t attack) {
    return attack >= 1 && attack < info(Table::Attacks).count && !(attack >= 45 && attack <= 54) && attack != 145;
}

bool Pack::set_field(Table table, uint16_t id, std::string_view name, int64_t value) {
    const Field* f = field(table, name);
    if (f == nullptr) {
        return false;
    }
    const int bits = f->bits > 0 ? f->bits : f->width * 8;
    const int64_t low = f->is_signed ? -(int64_t{1} << (bits - 1)) : 0;
    const int64_t high = f->is_signed ? (int64_t{1} << (bits - 1)) - 1 : (int64_t{1} << bits) - 1;
    if (value < low || value > high) {
        return false;
    }
    if (table == Table::Spells && !spell_target(value)) {
        return false;
    }
    if (f->bits > 0) {
        const uint8_t mask = static_cast<uint8_t>(((1u << f->bits) - 1u) << f->shift);
        const uint8_t byte = static_cast<uint8_t>(static_cast<uint8_t>(value) << f->shift) & mask;
        edits.push_back({table, id, f->offset, {byte}, mask, {}});
        return true;
    }
    std::vector<uint8_t> bytes(f->width);
    const uint64_t raw = static_cast<uint64_t>(value);
    for (int i = 0; i < f->width; i++) {
        bytes[i] = static_cast<uint8_t>(raw >> (8 * (f->width - 1 - i)));
    }
    edits.push_back({table, id, f->offset, std::move(bytes), 0xFFu, {}});
    return true;
}

std::span<const uint8_t> Record::bytes() const {
    return {bytes_, info(table_).stride};
}

uint16_t Record::u16(uint16_t offset) const {
    return static_cast<uint16_t>((bytes_[offset] << 8) | bytes_[offset + 1]);
}

uint32_t Record::u32(uint16_t offset) const {
    return (static_cast<uint32_t>(bytes_[offset]) << 24) | (static_cast<uint32_t>(bytes_[offset + 1]) << 16) |
           (static_cast<uint32_t>(bytes_[offset + 2]) << 8) | bytes_[offset + 3];
}

std::optional<int64_t> Record::get(std::string_view name) const {
    const Field* f = field(table_, name);
    if (f == nullptr) {
        return std::nullopt;
    }
    uint64_t raw = 0;
    for (int i = 0; i < f->width; i++) {
        raw = (raw << 8) | bytes_[f->offset + i];
    }
    if (f->bits > 0) {
        return static_cast<int64_t>((raw >> f->shift) & ((1u << f->bits) - 1u));
    }
    if (f->is_signed && f->width < 8 && (raw >> (f->width * 8 - 1)) != 0) {
        return static_cast<int64_t>(raw) - (int64_t{1} << (f->width * 8));
    }
    return static_cast<int64_t>(raw);
}

bool load() {
    if (g_loaded || g_failed) {
        return g_loaded;
    }
    // The ROM image after the .ips patches (rom_patch.cpp), which is what the
    // game's DMA of segment B reads.
    const std::span<const uint8_t> rom = recomp::get_rom();
    // The first ticks can run before the runtime has loaded the ROM (the start
    // screen); that is not a failure, so try again on the next tick.
    if (rom.empty()) {
        return false;
    }
    const auto rom_word = [&](uint32_t vram) -> std::optional<uint32_t> {
        const size_t at = vram - SEGMENT_B_VRAM + SEGMENT_B_ROM;
        if (at + 4 > rom.size()) {
            return std::nullopt;
        }
        return (static_cast<uint32_t>(rom[at]) << 24) | (static_cast<uint32_t>(rom[at + 1]) << 16) |
               (static_cast<uint32_t>(rom[at + 2]) << 8) | rom[at + 3];
    };
    if (rom_word(CHECK_ADDRESS) != CHECK_VALUE) {
        g_failed = true;
        std::fprintf(stderr, "[content] the ROM is not US revision 1; the content database is off\n");
        return false;
    }
    for (const TableInfo& t : TABLES) {
        if (t.rom + static_cast<size_t>(t.stride) * t.capacity > rom.size()) {
            g_failed = true;
            std::fprintf(stderr, "[content] the ROM is too short for the %s table; the content database is off\n",
                         t.name);
            return false;
        }
    }
    for (int k = 0; k < kTableCount; k++) {
        const TableInfo& t = TABLES[k];
        // Records past the count (the room for new classes) start as the ROM
        // bytes that follow the table, which is what the game reads there.
        const size_t size = static_cast<size_t>(t.stride) * t.capacity;
        TableData& data = g_tables[k];
        data.original.assign(rom.begin() + t.rom, rom.begin() + t.rom + size);
        // The ROM's name bytes, up to the NUL. A pointer outside segment B
        // (none in the named tables) leaves the name empty.
        data.names.assign(t.capacity, {});
        for (uint16_t id = 0; id < t.count && t.named; id++) {
            const uint32_t pointer = original(static_cast<Table>(k), id).u32(0);
            if (pointer < SEGMENT_B_VRAM || pointer >= SEGMENT_B_NAMES_END) {
                continue;
            }
            for (size_t c = pointer - SEGMENT_B_VRAM + SEGMENT_B_ROM; c < rom.size() && rom[c] != 0; c++) {
                data.names[id].push_back(rom[c]);
            }
        }
        data.current_names = data.names;
    }
    g_loaded = true;
    read_class_ids();
    if (const char* move = std::getenv("OGRE_CLASS_TABLE_MOVE"); move != nullptr && move[0] == '0') {
        g_move = false;
    }
    write_dump();
    read_files();
    apply_packs();
    // Nothing was written before the first load.
    g_restores.clear();
    return true;
}

bool loaded() {
    return g_loaded;
}

std::string record_name(Table table, uint16_t id) {
    if (!g_loaded || id >= info(table).capacity) {
        return {};
    }
    return decode_name(data(table).current_names[id]);
}

std::vector<uint8_t> original_name(Table table, uint16_t id) {
    if (!g_loaded || id >= info(table).capacity) {
        return {};
    }
    return data(table).names[id];
}

std::string decode_name(std::span<const uint8_t> bytes) {
    std::string name;
    for (size_t i = 0; i < bytes.size(); i++) {
        const uint8_t c = bytes[i];
        if (c == NAME_BREAK && i + 1 < bytes.size() && bytes[i + 1] == 'c') {
            name += '\n';
            i++;
        } else if (c >= 0x20 && c < 0x7F) {
            name += static_cast<char>(c);
        }
    }
    return name;
}

std::optional<std::vector<uint8_t>> encode_name(Table /*table*/, std::string_view text, std::string& error) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (true) {
        const size_t end = text.find('\n', start);
        lines.push_back(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    if (lines.size() > 2) {
        error = "a name has at most two lines";
        return std::nullopt;
    }
    for (std::string_view line : lines) {
        if (line.empty() || line.size() > MAX_NAME_LINE) {
            error = "each line of a name has 1 to " + std::to_string(MAX_NAME_LINE) + " characters";
            return std::nullopt;
        }
        for (char c : line) {
            if (c < 0x20 || c > 0x7E) {
                error = "a name uses printable ASCII only";
                return std::nullopt;
            }
        }
    }
    std::vector<uint8_t> bytes;
    if (lines.size() == 1) {
        bytes.assign(lines[0].begin(), lines[0].end());
    } else {
        bytes.push_back(NAME_OPEN);
        bytes.insert(bytes.end(), lines[0].begin(), lines[0].end());
        bytes.push_back(NAME_BREAK);
        bytes.push_back('c');
        bytes.insert(bytes.end(), lines[1].begin(), lines[1].end());
        bytes.push_back(NAME_CLOSE);
    }
    return bytes;
}

Record original(Table table, uint16_t id) {
    return {table, id, data(table).original.data() + static_cast<size_t>(id) * info(table).stride};
}

Record current(Table table, uint16_t id) {
    return {table, id, data(table).current.data() + static_cast<size_t>(id) * info(table).stride};
}

ClassRecord class_record(uint16_t id) {
    const Record r = current(Table::Classes, id);
    return {r.table(), r.id(), r.bytes().data()};
}

AttackRecord attack_record(uint16_t id) {
    const Record r = current(Table::Attacks, id);
    return {r.table(), r.id(), r.bytes().data()};
}

ItemRecord item_record(uint16_t id) {
    const Record r = current(Table::Items, id);
    return {r.table(), r.id(), r.bytes().data()};
}

RewardRecord reward_record(uint16_t id) {
    const Record r = current(Table::Rewards, id);
    return {r.table(), r.id(), r.bytes().data()};
}

Record base(Table table, uint16_t id) {
    return {table, id, data(table).base.data() + static_cast<size_t>(id) * info(table).stride};
}

void set_runtime_pack(Pack pack) {
    const auto same = std::find_if(g_runtime_packs.begin(), g_runtime_packs.end(),
                                   [&](const Pack& p) { return p.id == pack.id; });
    for (const Edit& edit : pack.edits) {
        if (!edit_fits(edit)) {
            std::fprintf(stderr, "[content] runtime pack %s: an edit is outside its table; pack skipped\n",
                         pack.id.c_str());
            return;
        }
    }
    if (pack.edits.empty() && pack.names.empty()) {
        if (same == g_runtime_packs.end()) {
            return;
        }
        g_runtime_packs.erase(same);
    } else if (same != g_runtime_packs.end()) {
        *same = std::move(pack);
    } else {
        g_runtime_packs.push_back(std::move(pack));
    }
    if (g_loaded) {
        apply_packs();
    }
}

ConsumableRecord consumable_record(uint16_t id) {
    const Record r = current(Table::Consumables, id);
    return {r.table(), r.id(), r.bytes().data()};
}

bool add_pack(Pack pack) {
    for (const Edit& edit : pack.edits) {
        if (!edit_fits(edit)) {
            std::fprintf(stderr, "[content] pack %s: %s record %u offset 0x%X is outside the table; pack skipped\n",
                         pack.id.c_str(), info(edit.table).name, edit.id, edit.offset);
            return false;
        }
    }
    g_code_packs.push_back(std::move(pack));
    if (g_loaded) {
        apply_packs();
    }
    return true;
}

void reload_files() {
    if (!g_loaded) {
        return;
    }
    read_files();
    apply_packs();
}

std::span<const FileError> file_errors() {
    return g_file_errors;
}

std::span<const Pack> packs() {
    return g_packs;
}

std::span<const Conflict> conflicts() {
    return g_conflicts;
}

bool is_class_key(std::string_view key) {
    if (key.size() < 3 || key.size() > 64 || key.find('.') == std::string_view::npos) {
        return false;
    }
    return std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}

bool is_art_path(std::string_view path) {
    constexpr std::string_view PNG = ".png";
    return path.size() > PNG.size() && path.substr(path.size() - PNG.size()) == PNG && is_art_folder(path);
}

bool is_art_folder(std::string_view path) {
    if (path.empty() || path.size() > 128 || path.front() == '/') {
        return false;
    }
    if (!std::all_of(path.begin(), path.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                   c == '_' || c == '.' || c == '/';
        })) {
        return false;
    }
    // No empty, "." or ".." part.
    size_t at = 0;
    while (at <= path.size()) {
        const size_t end = std::min(path.find('/', at), path.size());
        const std::string_view part = path.substr(at, end - at);
        if (part.empty() || part == "." || part == "..") {
            return false;
        }
        at = end + 1;
    }
    return true;
}

std::vector<NewClassArt> new_class_art() {
    std::lock_guard lock(records_mutex());
    std::vector<NewClassArt> out;
    for (const Pack& pack : g_packs) {
        for (const NewClass& nc : pack.new_classes) {
            const auto found = g_class_ids.find(nc.key);
            if (found == g_class_ids.end() || (nc.art_portrait.empty() && nc.art_figure_front.empty() &&
                                                nc.art_figure_back.empty() && nc.art_battle.empty())) {
                continue;
            }
            const auto path = [&](const std::string& name) {
                return name.empty() ? std::filesystem::path{} : pack.folder / std::filesystem::path(name);
            };
            out.push_back({found->second, nc.copy_of, nc.key, path(nc.art_portrait), path(nc.art_figure_front),
                           path(nc.art_figure_back), path(nc.art_battle)});
        }
    }
    return out;
}

std::optional<uint16_t> class_id(std::string_view key) {
    std::lock_guard lock(records_mutex());
    const auto found = g_class_ids.find(key);
    return found == g_class_ids.end() ? std::nullopt : std::optional<uint16_t>(found->second);
}

int class_ids_used() {
    std::lock_guard lock(records_mutex());
    return static_cast<int>(g_class_ids.size());
}

std::vector<BattleFormRule> battle_form_rules() {
    std::lock_guard lock(records_mutex());
    const auto resolve = [](int id, const std::string& key) -> int {
        if (key.empty()) {
            return id;
        }
        const auto found = g_class_ids.find(key);
        return found == g_class_ids.end() ? -1 : found->second;
    };
    std::map<uint16_t, BattleFormRule> by_class;
    for (const Pack& pack : g_packs) {
        for (const BattleForm& form : pack.battle_forms) {
            const int cls = resolve(form.cls, form.cls_key);
            if (cls <= 0) {
                continue;
            }
            BattleFormRule rule{static_cast<uint16_t>(cls), form.attacks, form.repeat, {}};
            bool complete = true;
            for (const BattleFormStrike& strike : form.strikes) {
                BattleFormRule::Strike out{{}, strike.attacks, strike.breath};
                for (const int id : strike.forms) {
                    out.forms.push_back(id);
                }
                for (const std::string& key : strike.form_keys) {
                    const int to = resolve(-1, key);
                    complete = complete && to > 0;
                    out.forms.push_back(to);
                }
                rule.strikes.push_back(std::move(out));
            }
            if (complete) {
                by_class[rule.cls] = std::move(rule);
            }
        }
    }
    std::vector<BattleFormRule> rules;
    for (auto& [cls, rule] : by_class) {
        rules.push_back(std::move(rule));
    }
    return rules;
}

std::vector<std::pair<std::string, std::string>> status() {
    std::lock_guard lock(records_mutex());
    std::vector<std::pair<std::string, std::string>> lines;
    if (!g_loaded) {
        lines.emplace_back("Content packs", g_failed ? "OFF: THE ROM IS NOT US REVISION 1" : "LOAD WHEN THE GAME STARTS");
        return lines;
    }
    // The mods themselves have their own rows in the MODS tab (ui.cpp).
    for (const Pack& pack : g_packs) {
        std::string what = std::to_string(pack.edits.size()) + " EDITS";
        if (!pack.names.empty()) {
            what += ", " + std::to_string(pack.names.size()) + " NAMES";
        }
        if (!pack.battle_forms.empty()) {
            what += ", " + std::to_string(pack.battle_forms.size()) + " BATTLE FORMS";
        }
        if (!pack.not_applied.empty()) {
            what += ", NOT APPLIED: " + std::to_string(pack.not_applied.size());
        }
        lines.emplace_back(pack.id, what);
    }
    for (const FileError& error : g_file_errors) {
        lines.emplace_back(error.file, "REFUSED: " + error.error.substr(0, 60));
    }
    for (const Conflict& c : g_conflicts) {
        lines.emplace_back(std::string(info(c.table).name) + " " + std::to_string(c.id),
                           c.later + " OVER " + c.earlier);
    }
    if (!g_class_ids.empty()) {
        lines.emplace_back("New classes", std::to_string(g_class_ids.size()) + " OF 83 IDS USED");
    }
    for (const std::string& error : g_class_errors) {
        lines.emplace_back("New class", "REFUSED: " + error.substr(0, 60));
    }
    if (lines.empty()) {
        lines.emplace_back("(no packs)", "");
    }
    return lines;
}

std::map<std::string, bool> held_mods() {
    std::lock_guard lock(records_mutex());
    return held_map();
}

std::vector<files::Mod> mods() {
    std::lock_guard lock(records_mutex());
    return g_mods;
}

std::map<std::string, int> class_counts() {
    std::lock_guard lock(records_mutex());
    return g_class_counts;
}

std::string mod_settings_error() {
    std::lock_guard lock(records_mutex());
    return g_settings_error;
}

int generation() {
    std::lock_guard lock(records_mutex());
    return g_generation;
}

std::recursive_mutex& records_mutex() {
    static std::recursive_mutex mutex;
    return mutex;
}

bool segment_b_resident(const uint8_t* rdram) {
    if (rdram == nullptr) {
        return false;
    }
    const uint32_t pointer = rd32(rdram, CHECK_ADDRESS);
    return pointer == CHECK_VALUE || (g_loaded && pointer == current(Table::Classes, 1).u32(0));
}

void tick(uint8_t* rdram) {
    if (rdram == nullptr || !load()) {
        return;
    }
    class_change::write_tables(rdram);
    if (--g_check_countdown <= 0) {
        g_check_countdown = RELOAD_CHECK_TICKS;
        if (files::fingerprint() != g_fingerprint) {
            std::fprintf(stderr, "[content] a content file changed; reloading the packs\n");
            reload_files();
        }
    }
    // Which tables are in RAM. Segment B holds the first four; class 1's name
    // pointer is a fixed ROM value. The rewards are bank N data in a window
    // three other banks share, so they need bank N loaded and their first row
    // still reading a class word this database knows (the ROM's or a pack's).
    bool resident[kTableCount];
    const bool segment_b = segment_b_resident(rdram);
    for (int k = 0; k < kTableCount; k++) {
        resident[k] = segment_b;
    }
    const int rewards = static_cast<int>(Table::Rewards);
    const uint16_t first_word = static_cast<uint16_t>(rd8(rdram, addr::kRewards) << 8 | rd8(rdram, addr::kRewards + 1));
    resident[rewards] = recomp::overlays::is_function_bank_loaded(addr::kBankNRom) &&
                        (first_word == original(Table::Rewards, 0).u16(0) || first_word == current(Table::Rewards, 0).u16(0));
    // A table that is not resident gets the ROM bytes with its next DMA, so its
    // released bytes need no restore.
    // The moved class table is never DMA'd over. The first fill copies every
    // record; after that it takes the same pack and released bytes as the
    // original, so a byte the game writes itself (class 163 +0x06 on the
    // mission load, bank R 0x8021626C) stays. Then the readers switch to it.
    // The tables indexed by class id live in moved copies in the content arena
    // (design-new-classes.md section 3): the class table and the appearance
    // table. A record past the ROM's count (a new class) exists only there.
    struct Moved {
        Table table;
        uint32_t base;
        unsigned int* shift;
    };
    static const Moved MOVED[] = {
        {Table::Classes, addr::kMovedClasses, &ogre_class_table_shift},
        {Table::Appearances, addr::kMovedAppearances, &ogre_appearance_shift},
    };
    const auto moved_of = [](int table) -> const Moved* {
        for (const Moved& m : MOVED) {
            if (static_cast<int>(m.table) == table) {
                return &m;
            }
        }
        return nullptr;
    };
    for (const Write& w : g_restores) {
        if (w.table >= 0 && resident[w.table] && !w.moved_only) {
            wr8(rdram, w.address, w.value);
        }
        if (const Moved* m = g_move ? moved_of(w.table) : nullptr) {
            wr8(rdram, w.address - info(m->table).address + m->base, w.value);
        }
    }
    g_restores.clear();
    for (const Write& w : g_writes) {
        if ((w.table < 0 ? segment_b : resident[w.table]) && !w.moved_only) {
            wr8(rdram, w.address, w.value);
        }
        if (const Moved* m = g_move ? moved_of(w.table) : nullptr) {
            wr8(rdram, w.address - info(m->table).address + m->base, w.value);
        }
    }
    // A class id is a byte, so code can index records 165..255 (255 is a
    // common "none"), and a search can run past the last class. In the ROM's
    // layout those bytes are whatever follows the table (for the class table,
    // the attack and item tables); the moved copy gets the same bytes, kept
    // current, so every id reads what it read before the move, except the
    // records a new class owns. They are copied from segment B, so the first
    // fill waits until it is resident.
    if (g_move && segment_b) {
        for (const Moved& m : MOVED) {
            const TableInfo& t = info(m.table);
            const TableData& d = data(m.table);
            const uint32_t end = static_cast<uint32_t>(t.capacity) * t.stride;
            const uint32_t count_end = static_cast<uint32_t>(t.count) * t.stride;
            for (uint32_t i = g_move_filled ? count_end : 0; i < end; i++) {
                const bool owned = d.owner[i] >= 0;
                const uint8_t value = i < count_end || owned ? d.current[i] : rd8(rdram, t.address + i);
                if (!g_move_filled || !owned) {
                    wr8(rdram, m.base + i, value);
                }
            }
        }
        if (!g_move_filled) {
            g_move_filled = true;
            ogre_class_table_shift = addr::kClassTableShift;
            ogre_appearance_shift = addr::kClassTableShift;
            std::fprintf(stderr, "[content] the class and appearance tables' readers use the copies at 0x%08X and 0x%08X\n",
                         addr::kMovedClasses, addr::kMovedAppearances);
        }
    }
    if (!g_move) {
        bool new_classes = false;
        for (const Write& w : g_writes) {
            new_classes = new_classes || w.moved_only;
        }
        static bool warned = false;
        if (new_classes && !warned) {
            warned = true;
            std::fprintf(stderr, "[content] new classes need the class table move (OGRE_CLASS_TABLE_MOVE=0 is set)\n");
        }
    }
    if (!segment_b) {
        return;
    }
    if (g_announce) {
        g_announce = false;
        std::fprintf(stderr, "[content] %zu pack(s), %zu bytes written into the game's tables\n", g_packs.size(),
                     g_writes.size());
        for (const Conflict& c : g_conflicts) {
            std::fprintf(stderr, "[content] %s record %u: %s overrides %s\n", info(c.table).name, c.id,
                         c.later.c_str(), c.earlier.c_str());
        }
    }
}

}  // namespace ogre::content
