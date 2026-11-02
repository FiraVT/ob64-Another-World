// Lua scripts. See lua_runtime.hpp and docs/anotherworld/design-lua.md.

#include "lua_runtime.hpp"

#include "content_db.hpp"
#include "content_files.hpp"
#include "game_addresses.hpp"
#include "game_events.hpp"
#include "game_host.hpp"
#include "lua_sandbox.hpp"
#include "mod_options.hpp"
#include "recruit.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include "lauxlib.h"
#include "lua.h"
#include "librecomp/overlays.hpp"
#include "lualib.h"
#include "ultramodern/ultramodern.hpp"

namespace ogre::lua {

namespace {

// The folder check runs every this many ticks (about once a second).
constexpr int RELOAD_CHECK_TICKS = 60;
// A handler that fails this many times is switched off; one that runs past
// the sandbox's instruction limit is switched off at once.
constexpr int MAX_HANDLER_ERRORS = 10;
// A script file larger than this is not loaded.
constexpr uintmax_t MAX_SCRIPT_SIZE = 1024u * 1024u;

constexpr std::array<const char*, 14> EVENTS = {"mission_load",  "field_update",     "item_granted",
                                                "field_frame",   "frame",            "battle_start",
                                                "battle_end",    "battle_attack",    "battle_attack_count",
                                                "battle_status", "battle_dot",       "battle_round_end",
                                                "class_alignment", "unit_size"};

// A battle on the mission field runs as scene 0x0D with the battle bank (ROM
// 0x22A250 -> RAM 0x801E6FD0) resident; the field's code loads over that RAM
// on the way back, so the bank's residency brackets the battle.
constexpr int SCENE_BATTLE = 0x0D;
constexpr uint32_t BATTLE_BANK_ROM = 0x22A250u;
// The battle's two unit index bytes: the side that started it, and the other.
constexpr uint32_t BATTLE_UNIT_FIRST = 0x801976FCu;
constexpr uint32_t BATTLE_UNIT_OTHER = 0x80197708u;
constexpr int PLAYER_UNITS = 30;
constexpr uint32_t UNIT_STRIDE = 0x19u;
// The battle state (bank C helpers func_ovlC_801C9008, func_ovlC_801C8B88):
// participant n at state + 0x1C4 + n*0xF8, live while +0x48 != 0 (low byte:
// the class), flags +0x40 (bit 0 out of the fight, bit 1 held, bit 8 enemy
// side), HP +0x20 and maximum +0x22.
constexpr uint32_t BATTLE_STATE = 0x801CE8DCu;
constexpr uint32_t FIGHTERS = 0x1C4u;
constexpr uint32_t FIGHTER_STRIDE = 0xF8u;
constexpr int FIGHTER_COUNT = 20;

struct Script {
    std::string id;  // "content/drops.lua"
    std::string mod;  // the mod id of its folder, empty for a loose file in content/
    content::Pack pack;
    bool dirty = false;
    int log_lines = 0;  // in the current second
    int handlers = 0;
    std::string error;  // the first error while loading, for the MODS tab
};

// A script writes at most this many log lines a second, and each line is cut
// at MAX_LOG_LINE characters, so a script cannot fill the disk through the log.
constexpr int MAX_LOG_LINES = 20;
constexpr size_t MAX_LOG_LINE = 500;

struct Handler {
    size_t script;
    int ref;  // the function in the registry
    int errors = 0;
};

// Every use of the state goes through this lock: the events fire on the game
// thread, tick runs on the main thread.
std::recursive_mutex g_mutex;
lua_State* g_state = nullptr;
std::vector<Script> g_scripts;
std::map<std::string, std::vector<Handler>> g_handlers;
std::vector<std::string> g_published;  // runtime pack ids published by tick
std::vector<std::filesystem::path> g_paths;  // per script
std::string g_fingerprint;  // of the content folders at the last load
bool g_started = false;
int g_check_countdown = RELOAD_CHECK_TICKS;
bool g_in_battle = false;
int g_battle_unit = -1;
// The options each loaded mod declares (mod_options.hpp), by mod id, and their
// stored values; re-read when the mod-options folder changes.
std::map<std::string, std::vector<mod_options::Option>> g_mod_options;
std::map<std::string, std::map<std::string, int64_t>> g_option_values;
std::string g_options_print;

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

uint16_t rd16(const uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>((rd8(rdram, address) << 8) | rd8(rdram, address + 1));
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd8(rdram, address)) << 24) |
           (static_cast<uint32_t>(rd8(rdram, address + 1)) << 16) |
           (static_cast<uint32_t>(rd8(rdram, address + 2)) << 8) | rd8(rdram, address + 3);
}

uint32_t character_record(lua_Integer character) {
    return addr::kCharacters + static_cast<uint32_t>(character) * addr::kCharacterSize;
}

// The address of battle participant `n`, or 0 outside a battle.
uint32_t fighter_address(const uint8_t* rdram, int n) {
    if (rdram == nullptr || n < 0 || n >= FIGHTER_COUNT || !recomp::overlays::is_function_bank_loaded(BATTLE_BANK_ROM)) {
        return 0;
    }
    const uint32_t state = rd32(rdram, BATTLE_STATE);
    const uint32_t address = state + FIGHTERS + static_cast<uint32_t>(n) * FIGHTER_STRIDE;
    if (state < 0x80000000u || address + FIGHTER_STRIDE > 0x80800000u) {
        return 0;
    }
    return address;
}

// The player's unit in the battle that just started (0..29), or -1. When an
// enemy unit starts the battle the first side holds the enemy and the
// player's unit is the other one.
int player_battle_unit(const uint8_t* rdram) {
    int unit = rd8(rdram, BATTLE_UNIT_FIRST);
    if (unit >= PLAYER_UNITS) {
        unit = rd8(rdram, BATTLE_UNIT_OTHER);
    }
    return unit < PLAYER_UNITS ? unit : -1;
}

std::string pack_id(const Script& script) {
    return "script/" + script.id;
}

// Calls the function under `nargs` arguments in the sandbox and logs an error
// with `who`.
bool call(lua_State* state, int nargs, int nresults, const std::string& who) {
    std::string error;
    if (!sandbox::call(state, nargs, nresults, error)) {
        std::fprintf(stderr, "[lua] %s: %s\n", who.c_str(), error.c_str());
        return false;
    }
    return true;
}

size_t script_index(lua_State* state) {
    return static_cast<size_t>(lua_tointeger(state, lua_upvalueindex(1)));
}

content::Table check_table(lua_State* state, int arg) {
    const std::optional<content::Table> table = content::table_by_name(luaL_checkstring(state, arg));
    if (!table) {
        luaL_argerror(state, arg, "unknown table (classes, attacks, spells, items, consumables, rewards)");
    }
    return *table;
}

// A record id, or for the tables indexed by class id a new class's key
// ("my-mod.dragon-knight"), which a pack must have made.
uint16_t check_id(lua_State* state, int arg, content::Table table) {
    if (lua_type(state, arg) == LUA_TSTRING &&
        (table == content::Table::Classes || table == content::Table::Appearances)) {
        const std::optional<uint16_t> id = content::class_id(lua_tostring(state, arg));
        if (!id) {
            luaL_argerror(state, arg, "no pack makes a class with this key");
        }
        return *id;
    }
    const lua_Integer id = luaL_checkinteger(state, arg);
    if (id < 0 || id >= content::info(table).count) {
        luaL_argerror(state, arg, "not a record of this table");
    }
    return static_cast<uint16_t>(id);
}

int api_log(lua_State* state) {
    Script& script = g_scripts[script_index(state)];
    if (++script.log_lines > MAX_LOG_LINES) {
        if (script.log_lines == MAX_LOG_LINES + 1) {
            std::fprintf(stderr, "[lua] %s: more than %d log lines this second; the rest are dropped\n",
                         script.id.c_str(), MAX_LOG_LINES);
        }
        return 0;
    }
    std::string line;
    for (int i = 1; i <= lua_gettop(state) && line.size() < MAX_LOG_LINE; i++) {
        line += (i > 1 ? " " : "") + std::string(luaL_tolstring(state, i, nullptr));
        lua_pop(state, 1);
    }
    if (line.size() > MAX_LOG_LINE) {
        line.resize(MAX_LOG_LINE);
        line += "...";
    }
    std::fprintf(stderr, "[lua] %s: %s\n", script.id.c_str(), line.c_str());
    return 0;
}

int api_on(lua_State* state) {
    const std::string event = luaL_checkstring(state, 1);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    if (std::find(EVENTS.begin(), EVENTS.end(), event) == EVENTS.end()) {
        luaL_argerror(state, 1,
                      "unknown event (mission_load, field_update, item_granted, field_frame, frame, battle_start, "
                      "battle_end, battle_attack, battle_attack_count, battle_status, battle_dot, battle_round_end, "
                      "class_alignment, unit_size)");
    }
    lua_pushvalue(state, 2);
    g_handlers[event].push_back({script_index(state), luaL_ref(state, LUA_REGISTRYINDEX)});
    g_scripts[script_index(state)].handlers++;
    return 0;
}

int api_content_get(lua_State* state) {
    std::lock_guard lock(content::records_mutex());
    const content::Table table = check_table(state, 1);
    const uint16_t id = check_id(state, 2, table);
    const std::string field = luaL_checkstring(state, 3);
    if (field == "name") {
        lua_pushstring(state, content::record_name(table, id).c_str());
        return 1;
    }
    const std::optional<int64_t> value = content::current(table, id).get(field);
    if (!value) {
        luaL_argerror(state, 3, "unknown field");
    }
    lua_pushinteger(state, static_cast<lua_Integer>(*value));
    return 1;
}

// A script's pack keeps one edit per field and one name per record
// (Pack::keep_one_edit), and a write of the value it already holds does not
// mark it dirty, so a script that writes every frame neither grows the pack
// nor republishes it.
int api_content_set(lua_State* state) {
    const content::Table table = check_table(state, 1);
    const uint16_t id = check_id(state, 2, table);
    const std::string field = luaL_checkstring(state, 3);
    Script& script = g_scripts[script_index(state)];
    if (field == "name") {
        std::string error;
        if (!script.pack.set_name(table, id, luaL_checkstring(state, 4), error)) {
            luaL_argerror(state, 4, error.c_str());
        }
        if (script.pack.keep_one_name()) {
            script.dirty = true;
        }
        return 0;
    }
    if (content::field(table, field) == nullptr) {
        luaL_argerror(state, 3, "unknown field");
    } else if (table == content::Table::Spells && !content::spell_target(luaL_checkinteger(state, 4))) {
        luaL_argerror(state, 4, "an attack 1..158 that is not a generic magic attack (45..54, 145)");
    } else if (!script.pack.set_field(table, id, field, luaL_checkinteger(state, 4))) {
        luaL_argerror(state, 4, "the value does not fit the field");
    }
    if (script.pack.keep_one_edit()) {
        script.dirty = true;
    }
    return 0;
}

int api_content_clear(lua_State* state) {
    Script& script = g_scripts[script_index(state)];
    script.pack.edits.clear();
    script.pack.names.clear();
    script.dirty = true;
    return 0;
}

int api_content_count(lua_State* state) {
    lua_pushinteger(state, content::info(check_table(state, 1)).count);
    return 1;
}

int api_game_mission(lua_State* state) {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    lua_pushinteger(state, rdram != nullptr ? rd8(rdram, addr::kMissionIndex) : 0);
    return 1;
}

int api_game_scene(lua_State* state) {
    lua_pushinteger(state, host::scene());
    return 1;
}

int api_content_class_id(lua_State* state) {
    const std::optional<uint16_t> id = content::class_id(luaL_checkstring(state, 1));
    if (id) {
        lua_pushinteger(state, *id);
    } else {
        lua_pushnil(state);
    }
    return 1;
}

// game.set_class(character, class): puts an army character (1..99) into a
// class (an id, or a new class's key). Writes the character record's class
// byte (+0x11) and its class key (+0x12) from the class record's +0x45, the
// pair the Change Class check reads (func_ovlS_801D2378).
int api_game_set_class(lua_State* state) {
    const lua_Integer character = luaL_checkinteger(state, 1);
    if (character < 1 || character > 99) {
        luaL_argerror(state, 1, "a character is 1..99");
    }
    uint16_t cls = 0;
    if (lua_type(state, 2) == LUA_TSTRING) {
        cls = check_id(state, 2, content::Table::Classes);
    } else {
        const lua_Integer id = luaL_checkinteger(state, 2);
        if (id < 1 || id > 254 || (id >= content::info(content::Table::Classes).count && !content::loaded())) {
            luaL_argerror(state, 2, "not a class id");
        }
        cls = static_cast<uint16_t>(id);
    }
    uint8_t key = 0;
    {
        std::lock_guard lock(content::records_mutex());
        key = static_cast<uint8_t>(content::class_record(cls).key());
    }
    uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram == nullptr) {
        return 0;
    }
    const uint32_t record = addr::kCharacters + static_cast<uint32_t>(character) * addr::kCharacterSize;
    rdram[((record + 0x11u) & 0x1FFFFFFFu) ^ 3u] = static_cast<uint8_t>(cls);
    rdram[((record + 0x12u) & 0x1FFFFFFFu) ^ 3u] = key;
    return 0;
}

// game.recruit(class, level): a new army character of a class (an id, or a
// new class's key) at a level 1..50, built by the game's own character builder
// on the next update of the mission field (recruit.hpp). Returns false when
// eight recruits already wait.
int api_game_recruit(lua_State* state) {
    uint16_t cls = 0;
    if (lua_type(state, 1) == LUA_TSTRING) {
        cls = check_id(state, 1, content::Table::Classes);
    } else {
        const lua_Integer id = luaL_checkinteger(state, 1);
        if (!recruit::valid(id, recruit::MIN_LEVEL) ||
            (id >= content::info(content::Table::Classes).count && !content::loaded())) {
            luaL_argerror(state, 1, "not a class id");
        }
        cls = static_cast<uint16_t>(id);
    }
    const lua_Integer level = luaL_checkinteger(state, 2);
    if (!recruit::valid(cls, level)) {
        luaL_argerror(state, 2, "a level is 1..50");
    }
    lua_pushboolean(state, recruit::queue(cls, static_cast<int>(level)));
    return 1;
}

int api_game_class_of(lua_State* state) {
    const lua_Integer character = luaL_checkinteger(state, 1);
    if (character < 1 || character > 99) {
        luaL_argerror(state, 1, "a character is 1..99");
    }
    const uint8_t* rdram = ultramodern::get_rdram_base();
    const uint32_t record = addr::kCharacters + static_cast<uint32_t>(character) * addr::kCharacterSize;
    lua_pushinteger(state, rdram != nullptr ? rd8(rdram, record + 0x11u) : 0);
    return 1;
}

int api_game_goth(lua_State* state) {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    lua_pushinteger(state, rdram != nullptr ? rd32(rdram, addr::kGoth) : 0);
    return 1;
}

// game.character(n): {class, level, exp} of army character n (1..99), from
// the character record (class +0x11, level +0x13, EXP +0x35).
int api_game_character(lua_State* state) {
    const lua_Integer character = luaL_checkinteger(state, 1);
    if (!valid_character(character)) {
        luaL_argerror(state, 1, "a character is 1..99");
    }
    const uint8_t* rdram = ultramodern::get_rdram_base();
    const uint32_t record = character_record(character);
    lua_createtable(state, 0, 3);
    lua_pushinteger(state, rdram != nullptr ? rd8(rdram, record + 0x11u) : 0);
    lua_setfield(state, -2, "class");
    lua_pushinteger(state, rdram != nullptr ? rd8(rdram, record + 0x13u) : 0);
    lua_setfield(state, -2, "level");
    lua_pushinteger(state, rdram != nullptr ? rd8(rdram, record + 0x35u) : 0);
    lua_setfield(state, -2, "exp");
    return 1;
}

// game.set_exp(n, exp): the EXP byte (+0x35) of army character n, 0..99.
// The level stays as it is.
int api_game_set_exp(lua_State* state) {
    const lua_Integer character = luaL_checkinteger(state, 1);
    if (!valid_character(character)) {
        luaL_argerror(state, 1, "a character is 1..99");
    }
    const lua_Integer exp = luaL_checkinteger(state, 2);
    if (!valid_exp(exp)) {
        luaL_argerror(state, 2, "EXP is 0..99");
    }
    uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram != nullptr) {
        rdram[((character_record(character) + 0x35u) & 0x1FFFFFFFu) ^ 3u] = static_cast<uint8_t>(exp);
    }
    return 0;
}

// game.unit(u): the five member bytes of unit u (0..29 the player's, 30..59
// the enemy's), leader first: 0 empty, 1..99 a character, 100+ a soldier.
int api_game_unit(lua_State* state) {
    const lua_Integer unit = luaL_checkinteger(state, 1);
    if (!valid_unit(unit)) {
        luaL_argerror(state, 1, "a unit is 0..59");
    }
    const uint8_t* rdram = ultramodern::get_rdram_base();
    const uint32_t record = addr::kUnits + static_cast<uint32_t>(unit) * UNIT_STRIDE;
    lua_createtable(state, 5, 0);
    for (int i = 0; i < 5; i++) {
        lua_pushinteger(state, rdram != nullptr ? rd8(rdram, record + 0x02u + static_cast<uint32_t>(i)) : 0);
        lua_rawseti(state, -2, i + 1);
    }
    return 1;
}

// battle.fighter(n): {class, hp, hp_max, side, out, ready} of battle
// participant n (0..19), or nil outside a battle or for an empty slot. side
// is 0 for the side the player's unit stands on in a player-started battle
// (flags bit 8), out is bit 0 (out of the fight), ready is bits 0 and 1 clear.
int api_battle_fighter(lua_State* state) {
    const lua_Integer n = luaL_checkinteger(state, 1);
    if (!valid_fighter(n)) {
        luaL_argerror(state, 1, "a fighter is 0..19");
    }
    const uint8_t* rdram = ultramodern::get_rdram_base();
    const uint32_t fighter = fighter_address(rdram, static_cast<int>(n));
    if (fighter == 0 || rd32(rdram, fighter + 0x48u) == 0) {
        lua_pushnil(state);
        return 1;
    }
    const uint32_t flags = rd32(rdram, fighter + 0x40u);
    lua_createtable(state, 0, 6);
    lua_pushinteger(state, rd32(rdram, fighter + 0x48u) & 0xFFu);
    lua_setfield(state, -2, "class");
    lua_pushinteger(state, rd16(rdram, fighter + 0x20u));
    lua_setfield(state, -2, "hp");
    lua_pushinteger(state, rd16(rdram, fighter + 0x22u));
    lua_setfield(state, -2, "hp_max");
    lua_pushinteger(state, (flags >> 8) & 1u);
    lua_setfield(state, -2, "side");
    lua_pushboolean(state, (flags & 1u) != 0);
    lua_setfield(state, -2, "out");
    lua_pushboolean(state, (flags & 3u) == 0);
    lua_setfield(state, -2, "ready");
    return 1;
}

// options.get(key): the value of one of the script's own mod's options
// (mod_options.hpp): true/false for a toggle, the choice's text, or the
// number. Another mod's options cannot be named.
int api_options_get(lua_State* state) {
    const Script& script = g_scripts[script_index(state)];
    const std::string key = luaL_checkstring(state, 1);
    const auto mod = g_mod_options.find(script.mod);
    if (mod == g_mod_options.end()) {
        luaL_argerror(state, 1, "this script's mod declares no options");
    }
    const auto option = std::find_if(mod->second.begin(), mod->second.end(),
                                     [&](const mod_options::Option& o) { return o.key == key; });
    if (option == mod->second.end()) {
        luaL_argerror(state, 1, "not one of this mod's options");
    }
    const int64_t value = mod_options::session_value(script.mod, *option, g_option_values[script.mod][key]);
    switch (option->type) {
        case mod_options::Type::Toggle:
            lua_pushboolean(state, value != 0);
            break;
        case mod_options::Type::Choice:
            lua_pushstring(state, mod_options::value_text(*option, value).c_str());
            break;
        default:
            lua_pushinteger(state, static_cast<lua_Integer>(value));
            break;
    }
    return 1;
}

void read_option_values() {
    g_options_print = mod_options::fingerprint();
    g_option_values.clear();
    for (const auto& [id, options] : g_mod_options) {
        g_option_values[id] = mod_options::read_values(id, options);
        // Fixes each "restart" option's value for this session.
        for (const mod_options::Option& option : options) {
            mod_options::session_value(id, option, g_option_values[id][option.key]);
        }
    }
}

// Sets t[name] = f with the script index as upvalue 1, on the table at the top.
void set_function(lua_State* state, size_t script, const char* name, lua_CFunction function) {
    lua_pushinteger(state, static_cast<lua_Integer>(script));
    lua_pushcclosure(state, function, 1);
    lua_setfield(state, -2, name);
}

// Pushes a script's environment: the sandbox's own copy of the safe library,
// plus the port's API.
void push_environment(lua_State* state, size_t script) {
    sandbox::push_environment(state);
    set_function(state, script, "log", api_log);
    set_function(state, script, "print", api_log);
    set_function(state, script, "on", api_on);
    lua_newtable(state);
    set_function(state, script, "get", api_content_get);
    set_function(state, script, "set", api_content_set);
    set_function(state, script, "clear", api_content_clear);
    set_function(state, script, "count", api_content_count);
    set_function(state, script, "class_id", api_content_class_id);
    lua_setfield(state, -2, "content");
    lua_newtable(state);
    set_function(state, script, "mission", api_game_mission);
    set_function(state, script, "scene", api_game_scene);
    set_function(state, script, "goth", api_game_goth);
    set_function(state, script, "set_class", api_game_set_class);
    set_function(state, script, "recruit", api_game_recruit);
    set_function(state, script, "class_of", api_game_class_of);
    set_function(state, script, "character", api_game_character);
    set_function(state, script, "set_exp", api_game_set_exp);
    set_function(state, script, "unit", api_game_unit);
    lua_setfield(state, -2, "game");
    lua_newtable(state);
    set_function(state, script, "fighter", api_battle_fighter);
    lua_setfield(state, -2, "battle");
    lua_newtable(state);
    set_function(state, script, "get", api_options_get);
    lua_setfield(state, -2, "options");
}

void unpublish() {
    for (const std::string& id : g_published) {
        content::Pack empty;
        empty.id = id;
        content::set_runtime_pack(std::move(empty));
    }
    g_published.clear();
}

void load_scripts() {
    unpublish();
    sandbox::close(g_state);
    g_handlers.clear();
    g_scripts.clear();
    g_state = sandbox::open();
    if (g_state == nullptr) {
        std::fprintf(stderr, "[lua] cannot create the Lua state; scripts are off\n");
        return;
    }
    // The same files and order as the content database (content_files.hpp),
    // with the same mods held at their start state (content_db.hpp held_mods).
    g_fingerprint = content::files::fingerprint();
    g_paths.clear();
    const content::files::Listing listing = content::files::scan(content::held_mods());
    g_mod_options.clear();
    for (const content::files::Mod& mod : listing.mods) {
        if (!mod.options.empty() && !mod.id.empty()) {
            g_mod_options[mod.id] = mod.options;
        }
    }
    read_option_values();
    for (const content::files::Entry& entry : listing.entries) {
        if (entry.kind != content::files::Kind::Script) {
            continue;
        }
        Script script;
        script.id = entry.id;
        script.mod = entry.mod;
        script.pack.id = "script/" + script.id;
        script.pack.source = script.id;
        g_scripts.push_back(std::move(script));
        g_paths.push_back(entry.path);
    }
    for (size_t i = 0; i < g_scripts.size(); i++) {
        std::error_code size_error;
        if (std::filesystem::file_size(g_paths[i], size_error) > MAX_SCRIPT_SIZE) {
            std::fprintf(stderr, "[lua] %s: the file is larger than 1 MiB; not loaded\n", g_scripts[i].id.c_str());
            g_scripts[i].error = "larger than 1 MiB";
            continue;
        }
        std::ifstream file(g_paths[i], std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::string error;
        push_environment(g_state, i);
        if (!sandbox::load(g_state, text, "@" + g_scripts[i].id, error)) {
            std::fprintf(stderr, "[lua] %s: %s\n", g_scripts[i].id.c_str(), error.c_str());
            g_scripts[i].error = error;
            continue;
        }
        if (!sandbox::call(g_state, 0, 0, error)) {
            std::fprintf(stderr, "[lua] %s: %s\n", g_scripts[i].id.c_str(), error.c_str());
            g_scripts[i].error = error;
        }
    }
    std::fprintf(stderr, "[lua] %zu script(s) loaded\n", g_scripts.size());
}

// Runs the handlers of `event`. Each gets `args`, then `value` when
// `pass_value`; a handler that returns an integer replaces `value`, and the
// final value is returned.
lua_Integer dispatch(const char* event, std::span<const lua_Integer> args, lua_Integer value, bool pass_value) {
    std::lock_guard lock(g_mutex);
    if (g_state == nullptr) {
        return value;
    }
    const auto found = g_handlers.find(event);
    if (found == g_handlers.end()) {
        return value;
    }
    // A handler may call on() and grow this list, which moves its elements, so
    // each handler is found again by index after its call. Handlers added
    // during this dispatch run from the next one (review AW-01, 2026-11-01).
    std::vector<Handler>& handlers = found->second;
    const size_t count = handlers.size();
    for (size_t i = 0; i < count; i++) {
        if (handlers[i].errors >= MAX_HANDLER_ERRORS) {
            continue;
        }
        const size_t script = handlers[i].script;
        lua_rawgeti(g_state, LUA_REGISTRYINDEX, handlers[i].ref);
        for (const lua_Integer arg : args) {
            lua_pushinteger(g_state, arg);
        }
        if (pass_value) {
            lua_pushinteger(g_state, value);
        }
        const int nargs = static_cast<int>(args.size()) + (pass_value ? 1 : 0);
        if (!call(g_state, nargs, 1, g_scripts[script].id + " " + event)) {
            Handler& handler = handlers[i];
            if (sandbox::stopped_for_time(g_state)) {
                handler.errors = MAX_HANDLER_ERRORS - 1;
            }
            if (++handler.errors == MAX_HANDLER_ERRORS) {
                std::fprintf(stderr, "[lua] %s: the %s handler is off until a reload\n",
                             g_scripts[script].id.c_str(), event);
            }
            continue;
        }
        if (pass_value && lua_isinteger(g_state, -1)) {
            value = lua_tointeger(g_state, -1);
        }
        lua_pop(g_state, 1);
    }
    return value;
}

events::Subscribe g_mission_load{events::Event::MissionLoad, "lua",
                                 [](const events::Args&) { dispatch("mission_load", {}, 0, false); }};
events::Subscribe g_field_update{events::Event::FieldUpdate, "lua",
                                 [](const events::Args&) { dispatch("field_update", {}, 0, false); }};
events::Subscribe g_field_frame{events::Event::FieldFrame, "lua",
                                [](const events::Args&) { dispatch("field_frame", {}, 0, false); }};
// The item grant routine's a0 is the item word; a handler's return value
// replaces it (game_events.hpp: a subscriber may write ctx->r4).
events::Subscribe g_item_granted{events::Event::ItemGranted, "lua", [](const events::Args& args) {
                                     const lua_Integer item = dispatch("item_granted", {}, args.a0 & 0xFFFFu, true);
                                     if (item >= 0 && item <= 0xFFFF && static_cast<uint32_t>(item) != (args.a0 & 0xFFFFu)) {
                                         args.ctx->r4 = static_cast<uint32_t>(item);
                                     }
                                 }};

}  // namespace

int64_t filter(const char* event, std::initializer_list<int64_t> args, int64_t value, int64_t low, int64_t high) {
    const std::vector<lua_Integer> values(args.begin(), args.end());
    const lua_Integer result = dispatch(event, values, value, true);
    if (result < low || result > high) {
        return value;
    }
    return result;
}

void event(const char* event, std::initializer_list<int64_t> args) {
    const std::vector<lua_Integer> values(args.begin(), args.end());
    dispatch(event, values, 0, false);
}

int64_t battle_fighter(uint32_t address) {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    const uint32_t first = fighter_address(rdram, 0);
    if (first == 0 || address < first || (address - first) % FIGHTER_STRIDE != 0 ||
        (address - first) / FIGHTER_STRIDE >= FIGHTER_COUNT) {
        return -1;
    }
    return (address - first) / FIGHTER_STRIDE;
}

std::vector<std::pair<std::string, std::string>> status() {
    std::lock_guard lock(g_mutex);
    std::vector<std::pair<std::string, std::string>> lines;
    if (!g_started) {
        lines.emplace_back("Scripts", "LOAD WHEN THE GAME STARTS");
        return lines;
    }
    for (size_t i = 0; i < g_scripts.size(); i++) {
        const Script& script = g_scripts[i];
        if (!script.error.empty()) {
            // The first line of the message, without the traceback.
            lines.emplace_back(script.id, "ERROR: " + script.error.substr(0, script.error.find('\n')).substr(0, 60));
            continue;
        }
        int off = 0;
        for (const auto& [event, handlers] : g_handlers) {
            for (const Handler& handler : handlers) {
                off += handler.script == i && handler.errors >= MAX_HANDLER_ERRORS ? 1 : 0;
            }
        }
        std::string what = std::to_string(script.handlers) + " HANDLERS, " +
                           std::to_string(script.pack.edits.size() + script.pack.names.size()) + " CHANGES";
        if (off > 0) {
            what += ", " + std::to_string(off) + " OFF AFTER ERRORS";
        }
        lines.emplace_back(script.id, what);
    }
    if (lines.empty()) {
        lines.emplace_back("(no scripts)", "");
    }
    return lines;
}

void tick(uint8_t* rdram) {
    if (!content::loaded()) {
        return;
    }
    std::lock_guard lock(g_mutex);
    if (!g_started) {
        g_started = true;
        load_scripts();
    } else if (--g_check_countdown <= 0) {
        g_check_countdown = RELOAD_CHECK_TICKS;
        for (Script& script : g_scripts) {
            script.log_lines = 0;
        }
        if (content::files::fingerprint() != g_fingerprint) {
            std::fprintf(stderr, "[lua] a script changed; reloading the scripts\n");
            load_scripts();
        } else if (mod_options::fingerprint() != g_options_print) {
            // A mod's options changed (the MODS tab writes them): new values,
            // the scripts keep running.
            read_option_values();
        }
    }
    if (rdram != nullptr) {
        const bool battle = rd16(rdram, addr::kActiveScene) == SCENE_BATTLE &&
                            recomp::overlays::is_function_bank_loaded(BATTLE_BANK_ROM);
        if (battle && !g_in_battle) {
            g_battle_unit = player_battle_unit(rdram);
            std::fprintf(stderr, "[battle] start: player unit %d\n", g_battle_unit);
            const lua_Integer unit = g_battle_unit;
            dispatch("battle_start", {&unit, 1}, 0, false);
        } else if (!battle && g_in_battle) {
            std::fprintf(stderr, "[battle] end: player unit %d\n", g_battle_unit);
            const lua_Integer unit = g_battle_unit;
            dispatch("battle_end", {&unit, 1}, 0, false);
        }
        g_in_battle = battle;
    }
    dispatch("frame", {}, 0, false);
    for (Script& script : g_scripts) {
        if (!script.dirty) {
            continue;
        }
        script.dirty = false;
        content::set_runtime_pack(script.pack);
        const std::string id = pack_id(script);
        if (std::find(g_published.begin(), g_published.end(), id) == g_published.end()) {
            g_published.push_back(id);
        }
    }
}

}  // namespace ogre::lua
