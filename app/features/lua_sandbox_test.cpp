// The Lua sandbox escape test. app/CMakeLists.txt builds it and runs it after
// every build; a failure fails the build. Each case is a script a hostile mod
// could ship; docs/anotherworld/security.md lists what each one guards.
//
// A case passes when the script returns true, or when it fails with an error
// that contains the expected text.

#include "lua_sandbox.hpp"

#include <cstdio>
#include <string>

#include "lua.h"

namespace {

using namespace ogre::lua;

struct Case {
    const char* name;
    const char* script;
    const char* error;  // nullptr: the script must return true
};

const Case CASES[] = {
    {"no file, OS, module or debug access",
     "return io == nil and os == nil and package == nil and debug == nil and require == nil "
     "and coroutine == nil",
     nullptr},
    {"no way to load code", "return load == nil and loadfile == nil and dofile == nil and string.dump == nil",
     nullptr},
    {"no string.dump through a string's methods", "return (\"\").dump == nil", nullptr},
    {"the string metatable is hidden", "return getmetatable(\"\") == false", nullptr},
    {"no garbage collector control", "return collectgarbage == nil", nullptr},
    {"the environment has no path to the state's globals",
     "return _G == _ENV and getmetatable(_ENV) == nil and rawget(_ENV, 'load') == nil", nullptr},
    {"a precompiled chunk is refused", "\x1bLua\x54\x00", "binary chunk"},
    {"an endless loop stops", "while true do end", "ran too long"},
    {"pcall cannot catch the time limit", "while true do pcall(function() while true do end end) end",
     "ran too long"},
    {"xpcall cannot catch the time limit",
     "while true do xpcall(function() while true do end end, function(e) return e end) end", "ran too long"},
    {"the time limit stops a __tostring loop",
     "local t = setmetatable({}, {__tostring = function() while true do end end}) return tostring(t)",
     "ran too long"},
    {"one large string is refused", "local s = string.rep('x', 1 << 30) return false", "not enough memory"},
    {"a growing string is refused", "local s = 'x' while true do s = s .. s end", "not enough memory"},
    {"a growing table stops", "local t = {} local i = 0 while true do i = i + 1 t[i] = i end", ""},
    // Either the instruction limit or Lua's stack limit stops it; both are
    // errors, neither a crash.
    {"deep recursion stops", "local function f() return 1 + f() end return f()", ""},
    // pcall inside pcall uses the C stack; Lua stops it at its C call limit
    // (about 200) with an error instead of overflowing the process's stack.
    {"deep C recursion stops at Lua's limit",
     "local depth = 0 local function f() depth = depth + 1 pcall(f) end f() return depth > 50 and depth < 1000",
     nullptr},
    {"pcall still works for ordinary errors", "local ok, e = pcall(error, 'x') return ok == false and e == 'x'",
     nullptr},
    {"xpcall still works for ordinary errors",
     "local ok, e = xpcall(function() error('x', 0) end, function(m) return 'h:' .. m end) "
     "return ok == false and e == 'h:x'",
     nullptr},
    {"an error object that is not a string", "error({})", "not a string"},
    // Lua runs finalizers with hooks off (lgc.c GCTM), so a __gc loop would
    // escape the time limit.
    {"no finalizers", "setmetatable({}, {__gc = function() while true do end end})", "__gc"},
    {"no finalizer through a field added later",
     "local mt = {} local t = setmetatable({}, mt) mt.__gc = function() while true do end end "
     "t = nil local junk = {} for i = 1, 20000 do junk[i % 100] = {} end return true",
     nullptr},
};

bool run(const Case& c) {
    lua_State* state = sandbox::open();
    if (state == nullptr) {
        std::printf("FAIL  %s: cannot open a state\n", c.name);
        return false;
    }
    std::string error;
    bool returned_true = false;
    sandbox::push_environment(state);
    bool ok = sandbox::load(state, c.script, std::string("=") + c.name, error);
    if (ok) {
        ok = sandbox::call(state, 0, 1, error);
        if (ok) {
            returned_true = lua_toboolean(state, -1) != 0;
            lua_pop(state, 1);
        }
    }
    const size_t used = sandbox::memory_used(state);
    sandbox::close(state);
    bool pass = c.error == nullptr ? ok && returned_true : !ok && error.find(c.error) != std::string::npos;
    pass = pass && used <= sandbox::kMemoryLimit;
    std::printf("%s  %s%s%s\n", pass ? "pass" : "FAIL", c.name, ok ? "" : ": ", ok ? "" : error.substr(0, 90).c_str());
    return pass;
}

// A change one script makes to its library copy is not seen by another.
bool run_isolation() {
    lua_State* state = sandbox::open();
    std::string error;
    sandbox::push_environment(state);
    bool ok = sandbox::load(state, "string.rep = nil table.insert = nil x = 1", "=first", error) &&
              sandbox::call(state, 0, 0, error);
    sandbox::push_environment(state);
    ok = ok && sandbox::load(state, "return string.rep ~= nil and table.insert ~= nil and x == nil", "=second", error) &&
         sandbox::call(state, 0, 1, error);
    const bool pass = ok && lua_toboolean(state, -1) != 0;
    sandbox::close(state);
    std::printf("%s  one script's changes to its library are its own\n", pass ? "pass" : "FAIL");
    return pass;
}


}  // namespace

int main() {
    // Each line reaches the build log even if a case hangs.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int failed = 0;
    for (const Case& c : CASES) {
        failed += run(c) ? 0 : 1;
    }
    failed += run_isolation() ? 0 : 1;
    std::printf("Lua sandbox escape test: %d failed\n", failed);
    return failed == 0 ? 0 : 1;
}
