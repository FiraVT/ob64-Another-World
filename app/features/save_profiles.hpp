// The port's save profiles (docs/anotherworld/design-save-system.md, phase 1).
//
// A save made from the port's menu is a record the game itself writes: the
// game thread calls the game's save routine func_8007485C for a battery slot
// (slot 1 on the world map, the suspend record 15 on the mission field and in
// battle), the port copies the record it wrote into
// `<config>/saves/profiles/<leader name>/<stamp>-s<slot>.rec`, and puts the
// slot's previous bytes back, so the player's own three records are untouched.
// Loading writes a normal record back into its slot of the battery buffer;
// the game's Load Game then reads it.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ogre::save_profiles {

struct Entry {
    std::string profile;          // the leader's name, the folder
    std::filesystem::path record; // the record file
    std::string stamp;            // YYYYMMDD-HHMMSS
    int slot = 0;                 // 0/1 a normal save, 15 a suspend
    std::string where;            // the details line
};

// Main thread: asks for a save; it runs on the game thread on the next frame
// the game runs (after the Esc menu closes).
void request_save();
bool save_pending();

// The last result line ("Saved to Magnus", or why not).
std::string last_message();

// Every profile's records, newest first (at most `max`).
std::vector<Entry> list(size_t max);

// Main thread: loads a profile save straight into the game. The game thread
// puts the record into its battery slot, runs the game's loader
// func_800749C0, puts the slot's previous bytes back and changes scene the way
// the Load Game screen's confirm does (func_801A9B3C). Returns the line the
// menu shows; the menu then closes (take_close_request).
std::string load(const Entry& entry);

// True once after a load or a title return was started, so the menu closes and
// the game runs.
bool take_close_request();

// Main thread: the MAIN tab's RETURN TO TITLE. The game thread leaves the
// current scene for the title the way a load from outside the title does
// (frame()): it frees the mission module's pool objects and the scene-script
// buffers, writes the game's own move to the title (0x8004), and resets the
// scene stack once the title runs. Unsaved progress is lost.
void request_title();

// Deletes a profile save (its record and details files).
std::string remove(const Entry& entry);

// True once after a save finished or failed, so the menu rebuilds its list.
bool take_changed();

// Game thread, once per frame (organize_edit.cpp's frame hook); `ctx` is the
// hook's recomp_context.
void frame(uint8_t* rdram, void* ctx);

}  // namespace ogre::save_profiles

