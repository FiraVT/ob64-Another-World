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
//   on(event, handler)                     "mission_load", "field_update",
//                                          "item_granted", "field_frame", "frame"
//   content.get(table, id, field)          a current value; field "name" gives the name
//   content.set(table, id, field, value)   the script's own change ("name" renames)
//   content.clear()                        drops the script's changes
//   content.count(table)                   records in a table
//   game.mission(), game.scene(), game.goth()
//
// A script's content.set calls become its runtime pack ("script/<file>"),
// published by tick on the main thread; a reload removes them. An
// "item_granted" handler receives the item word (bit 15 set: equipment id,
// else consumable id) and may return another to grant instead.

#include <cstdint>
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

}  // namespace ogre::lua
