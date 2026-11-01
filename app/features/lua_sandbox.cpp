// The Lua sandbox. See lua_sandbox.hpp and docs/anotherworld/security.md.

#include "lua_sandbox.hpp"

#include <cstdlib>
#include <cstring>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

namespace ogre::lua::sandbox {

namespace {

// The registry key of the table that holds the safe library, built once per
// state; push_environment copies from it.
constexpr const char* TEMPLATE_KEY = "ogre.sandbox.template";

struct Limits {
    size_t used = 0;
    int steps = 0;
    bool stopped = false;
};

Limits* limits(lua_State* state) {
    void* user = nullptr;
    lua_getallocf(state, &user);
    return static_cast<Limits*>(user);
}

// The allocator: refuses a block that would take the state past kMemoryLimit.
// Lua turns the refusal into a memory error.
void* allocate(void* user, void* block, size_t old_size, size_t new_size) {
    Limits* l = static_cast<Limits*>(user);
    const size_t old_bytes = block != nullptr ? old_size : 0;
    if (new_size == 0) {
        l->used -= old_bytes;
        std::free(block);
        return nullptr;
    }
    if (new_size > old_bytes && l->used - old_bytes + new_size > kMemoryLimit) {
        return nullptr;
    }
    void* grown = std::realloc(block, new_size);
    if (grown != nullptr) {
        l->used = l->used - old_bytes + new_size;
    }
    return grown;
}

// Every kInstructionStep instructions. Past the limit it raises an error, and
// raises it again at every later step of the same call, so a script that
// catches the first one still stops.
void count_hook(lua_State* state, lua_Debug* /*ar*/) {
    Limits* l = limits(state);
    if (++l->steps * kInstructionStep > kInstructionLimit) {
        l->stopped = true;
        luaL_error(state, "the script ran too long (more than %d instructions in one call)", kInstructionLimit);
    }
}

// pcall and xpcall that pass the instruction-limit stop on instead of
// catching it.
int safe_pcall(lua_State* state) {
    luaL_checkany(state, 1);
    const int status = lua_pcall(state, lua_gettop(state) - 1, LUA_MULTRET, 0);
    if (limits(state)->stopped) {
        return lua_error(state);
    }
    lua_pushboolean(state, status == LUA_OK);
    lua_insert(state, 1);
    return lua_gettop(state);
}

int safe_xpcall(lua_State* state) {
    const int n = lua_gettop(state);
    luaL_checktype(state, 2, LUA_TFUNCTION);
    // [f, handler, args...] -> [handler, f, args...]
    lua_pushvalue(state, 1);
    lua_pushvalue(state, 2);
    lua_replace(state, 1);
    lua_replace(state, 2);
    const int status = lua_pcall(state, n - 2, LUA_MULTRET, 1);
    if (limits(state)->stopped) {
        return lua_error(state);
    }
    lua_pushboolean(state, status == LUA_OK);
    lua_insert(state, 2);
    return lua_gettop(state) - 1;
}

// setmetatable that refuses a metatable with __gc. Lua runs a finalizer with
// debug hooks off (lgc.c GCTM), so a finalizer would escape the instruction
// limit; an object is only marked for finalization when its metatable has
// __gc at setmetatable time, and scripts have no other way to set a table's
// metatable.
int safe_setmetatable(lua_State* state) {
    if (lua_type(state, 2) == LUA_TTABLE) {
        lua_pushliteral(state, "__gc");
        const int kind = lua_rawget(state, 2);
        lua_pop(state, 1);
        if (kind != LUA_TNIL) {
            return luaL_error(state, "__gc metamethods are not allowed in scripts");
        }
    }
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, lua_gettop(state) - 1, LUA_MULTRET);
    return lua_gettop(state);
}

int traceback(lua_State* state) {
    luaL_traceback(state, state, lua_tostring(state, 1), 1);
    return 1;
}

// The base functions a script keeps. Left out: dofile, loadfile, load,
// require, collectgarbage, warn, print (the runtime adds its own), and _G
// (the environment gets itself).
constexpr const char* BASE_FUNCTIONS[] = {"assert", "error", "getmetatable", "ipairs", "next",   "pairs",
                                          "rawequal", "rawget", "rawlen",   "rawset", "select", "setmetatable",
                                          "tonumber", "tostring", "type",   "_VERSION"};
constexpr const char* LIBRARIES[] = {LUA_TABLIBNAME, LUA_STRLIBNAME, LUA_MATHLIBNAME, LUA_UTF8LIBNAME};

// Copies the table at `from` into a new table on the top.
void push_copy(lua_State* state, int from) {
    from = lua_absindex(state, from);
    lua_newtable(state);
    lua_pushnil(state);
    while (lua_next(state, from) != 0) {
        lua_pushvalue(state, -2);
        lua_insert(state, -2);
        lua_settable(state, -4);
    }
}

}  // namespace

lua_State* open() {
    Limits* l = new Limits();
    lua_State* state = lua_newstate(allocate, l);
    if (state == nullptr) {
        delete l;
        return nullptr;
    }
    luaL_requiref(state, LUA_GNAME, luaopen_base, 1);
    luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
    luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
    luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(state, 5);
    // Strings reach the original string library through their metatable
    // (("").dump), so dump goes from the original as well as from the copies.
    lua_getglobal(state, LUA_STRLIBNAME);
    lua_pushnil(state);
    lua_setfield(state, -2, "dump");
    lua_pop(state, 1);

    // The template: the safe base functions and library copies.
    lua_newtable(state);
    for (const char* name : BASE_FUNCTIONS) {
        lua_getglobal(state, name);
        lua_setfield(state, -2, name);
    }
    lua_pushcfunction(state, safe_pcall);
    lua_setfield(state, -2, "pcall");
    lua_pushcfunction(state, safe_xpcall);
    lua_setfield(state, -2, "xpcall");
    lua_getglobal(state, "setmetatable");
    lua_pushcclosure(state, safe_setmetatable, 1);
    lua_setfield(state, -2, "setmetatable");
    for (const char* name : LIBRARIES) {
        lua_getglobal(state, name);
        push_copy(state, -1);
        lua_remove(state, -2);
        if (std::strcmp(name, LUA_STRLIBNAME) == 0) {
            lua_pushnil(state);
            lua_setfield(state, -2, "dump");
        }
        lua_setfield(state, -2, name);
    }
    lua_setfield(state, LUA_REGISTRYINDEX, TEMPLATE_KEY);

    // The string metatable is shared by every string; getmetatable("") now
    // answers false, so no script reaches it or the string library behind it.
    lua_pushliteral(state, "");
    lua_getmetatable(state, -1);
    lua_pushboolean(state, 0);
    lua_setfield(state, -2, "__metatable");
    lua_pop(state, 2);

    lua_sethook(state, count_hook, LUA_MASKCOUNT, kInstructionStep);
    return state;
}

void close(lua_State* state) {
    if (state == nullptr) {
        return;
    }
    Limits* l = limits(state);
    lua_close(state);
    delete l;
}

void push_environment(lua_State* state) {
    lua_getfield(state, LUA_REGISTRYINDEX, TEMPLATE_KEY);
    lua_newtable(state);
    lua_pushnil(state);
    while (lua_next(state, -3) != 0) {
        // key at -2, value at -1; a library table is copied so the script's
        // changes stay its own.
        if (lua_istable(state, -1)) {
            push_copy(state, -1);
            lua_remove(state, -2);
        }
        lua_pushvalue(state, -2);
        lua_insert(state, -2);
        lua_settable(state, -4);
    }
    lua_remove(state, -2);
    lua_pushvalue(state, -1);
    lua_setfield(state, -2, "_G");
}

bool load(lua_State* state, std::string_view text, const std::string& chunk_name, std::string& error) {
    // Text only: a precompiled chunk could break the sandbox.
    if (luaL_loadbufferx(state, text.data(), text.size(), chunk_name.c_str(), "t") != LUA_OK) {
        error = lua_tostring(state, -1);
        lua_pop(state, 2);  // the message and the environment
        return false;
    }
    lua_insert(state, -2);
    lua_setupvalue(state, -2, 1);  // the chunk's _ENV
    return true;
}

bool call(lua_State* state, int nargs, int nresults, std::string& error) {
    Limits* l = limits(state);
    const int base = lua_gettop(state) - nargs;
    lua_pushcfunction(state, traceback);
    lua_insert(state, base);
    l->steps = 0;
    l->stopped = false;
    const int status = lua_pcall(state, nargs, nresults, base);
    lua_remove(state, base);
    if (status != LUA_OK) {
        error = lua_tostring(state, -1) != nullptr ? lua_tostring(state, -1) : "(error object is not a string)";
        lua_pop(state, 1);
        return false;
    }
    return true;
}

bool stopped_for_time(lua_State* state) {
    return limits(state)->stopped;
}

size_t memory_used(lua_State* state) {
    return limits(state)->used;
}

}  // namespace ogre::lua::sandbox
