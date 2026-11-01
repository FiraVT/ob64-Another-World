#pragma once

// The Lua sandbox (docs/anotherworld/security.md). Everything that keeps a
// script from reaching the computer, or from holding the game, lives here, so
// lua_sandbox_test.cpp can check it on every build.
//
//   * Libraries: base (without dofile, loadfile, load, require, collectgarbage),
//     table, string (without string.dump), math, utf8. No io, os, package,
//     debug or coroutine.
//   * Text chunks only; a precompiled chunk is refused.
//   * Each environment has its own copies of the library tables, and no
//     path to another environment or to the state's globals.
//   * Memory: the state's allocator refuses to go past kMemoryLimit.
//   * Time: a call stops after kInstructionLimit instructions, and pcall and
//     xpcall cannot catch that stop.

#include <cstddef>
#include <string>
#include <string_view>

struct lua_State;

namespace ogre::lua::sandbox {

constexpr size_t kMemoryLimit = 64u * 1024u * 1024u;
constexpr int kInstructionStep = 10000;
constexpr int kInstructionLimit = 1000000;

// A new state with the limits set, or nullptr. Close it with close().
lua_State* open();
void close(lua_State* state);

// Pushes a new environment table: copies of the safe library functions and
// tables. The caller adds its own API to it.
void push_environment(lua_State* state);

// Loads text as a function whose _ENV is the table at the top of the stack
// (which is popped). On success the function is pushed and true returned.
bool load(lua_State* state, std::string_view text, const std::string& chunk_name, std::string& error);

// Calls the function under `nargs` arguments with the instruction limit and a
// traceback. On an error nothing is left on the stack, `error` holds the
// message and false is returned.
bool call(lua_State* state, int nargs, int nresults, std::string& error);

// True when the last call stopped at the instruction limit.
bool stopped_for_time(lua_State* state);

// Bytes the state has allocated now.
size_t memory_used(lua_State* state);

}  // namespace ogre::lua::sandbox
