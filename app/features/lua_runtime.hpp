#pragma once

// Lua scripts (docs/anotherworld/design-lua.md; roadmap phase R0, modding plan
// M3).
//
// Scripts hold rules; content stays in the content database (content_db.hpp).
// The runtime loads content/*.lua beside the executable, then
// user-content/*.lua in the settings folder, each in file-name order, into one
// sandboxed Lua 5.4 state, and loads them again about a second after a file
// changes. Each script gets its own environment with:
//
//   log(...)                               a line in the log, tagged with the script
//   on(event, handler)                     an event below
//   content.get(table, id, field)          a current value; field "name" gives the name
//   content.set(table, id, field, value)   the script's own change ("name" renames)
//   content.clear()                        drops the script's changes
//   content.count(table)                   records in a table
//   game.mission(), game.scene(), game.goth()
//   game.character(n)                      {class, level, exp} of character 1..99
//   game.set_exp(n, exp)                   a character's EXP, 0..99
//   game.unit(u)                           the five member bytes of unit 0..59
//   battle.fighter(n)                      {class, hp, hp_max, side, out, ready}
//                                          of participant 0..19, or nil
//   options.get(key)                       one of the script's own mod's options
//                                          (mod_options.hpp): true/false, the
//                                          choice's text, or the number
//
// Events. The first five run with no arguments except item_granted:
//   mission_load, field_update, field_frame, frame
//   item_granted(item)              the item word (bit 15 set: equipment id,
//                                   else consumable id); may return another
//   battle_start(unit), battle_end(unit)
//                                   the player's unit in the battle (0..29),
//                                   or -1; on the main thread
// Filter events, run on the game thread at a decision of the game's code. The
// handler gets the arguments, then the game's value, and may return another
// value; out-of-range returns are ignored (filter below):
//   battle_attack(fighter, attack)  the attack a participant makes (0..158)
//   battle_attack_count(fighter, n) a participant's attacks this fight (0..15)
//   class_alignment(limit)          a class change's alignment limit byte;
//                                   255 skips the test
//   unit_size(size)                 the places a class of this size (1 or 2)
//                                   takes in a unit, for the Organize rules
//                                   and the war director's enemy units
//
// A script's content.set calls become its runtime pack ("script/<file>"),
// published by tick on the main thread; a reload removes them.

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace ogre::lua {

// The MODS tab's lines, as (left, right) pairs: each script with its handlers
// and changes, or the error that stopped it.
std::vector<std::pair<std::string, std::string>> status();

// Called once per frame on the main thread, after content::tick: loads or
// reloads the scripts, runs the "frame" handlers and publishes the scripts'
// content changes.
void tick(uint8_t* rdram);

// The argument checks of the game and battle API (security.md section 3.1;
// cases in content_pack_test). A call outside them raises a Lua error.
constexpr bool valid_character(int64_t n) {
    return n >= 1 && n <= 99;  // army character records 1..99
}
constexpr bool valid_exp(int64_t exp) {
    return exp >= 0 && exp <= 99;  // EXP inside one level
}
constexpr bool valid_unit(int64_t unit) {
    return unit >= 0 && unit <= 59;  // 0..29 the player's, 30..59 the enemy's
}
constexpr bool valid_fighter(int64_t n) {
    return n >= 0 && n <= 19;  // battle participants
}

// Runs the handlers of a filter event on the calling thread with `args`, then
// `value`. Returns the last handler's integer result, or `value` when no
// handler returns one or the result is outside [low, high].
int64_t filter(const char* event, std::initializer_list<int64_t> args, int64_t value, int64_t low, int64_t high);

// The participant index (0..19) of a battle participant's guest address, or
// -1.
int64_t battle_fighter(uint32_t address);

}  // namespace ogre::lua
