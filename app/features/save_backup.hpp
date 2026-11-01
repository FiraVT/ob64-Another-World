// Save backups and imports, the MAIN tab's SAVES rows (see save_backup.cpp).
// This is the port's stand-in for the game's Controller Pak copy/backup screen:
// the battery save is copied to and from files instead of pak notes.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ogre {

// Copies the battery save to `<config>/save-backups/<YYYYMMDD-HHMMSS>.bin`.
// Safe while the game runs. Returns the line the panel shows.
std::string back_up_save();

// The newest backups' file names, newest first (at most four).
std::vector<std::string> list_save_backups();

// Replaces the battery save with a backup, after backing the current one up as
// `<stamp>-before-restore.bin`. Start screen only: the game must not be running.
std::string restore_save_backup(const std::string& name);

// `<config>/save-import/`: where a player puts a save to import.
std::filesystem::path save_import_dir();

// Imports the first usable save from `save-import/` (a battery image, an
// emulator save, a DexDrive .N64 dump or a Controller Pak), or else from the
// port's own Controller Pak `<config>/saves/<game id>.mpk`, converting pak notes
// to battery slots. The current save is backed up first. Start screen only.
std::string import_save();

}  // namespace ogre

