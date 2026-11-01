#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace ogre {

// OGRE_SAVE=<name|path>: install that save as this run's battery (SRAM) image,
// converting it first if it is not already the port's 32 KiB logical image.
//
// The game reads `<config>/saves/<game id>.bin` (32 KiB of battery SRAM, see
// `entry.save_type` in main.cpp), so `OGRE_SAVE=prologue` finds
// `assets/saves/prologue.n64`, converts the DexDrive Controller Pak dump's live
// notes into battery slots (reseeded -- a note's slot checksums are seeded 0,
// the battery validator's are seeded with the slot's own device offset) and
// writes the result there before the runtime first touches it.
//
// The conversion writes *into the config dir*, never back into the source, and
// it keeps a battery that is newer than the source unless OGRE_SAVE_RESET=1 is
// set, so play progress survives a re-run and swapping a save in re-imports it.
// `tools/run-save.sh` is the per-save-config-dir wrapper around the same idea.
//
// Returns true when a battery image was written. Diagnostics go to stderr with
// the `[save]` prefix the runtime uses.
bool apply_ogre_save(const std::filesystem::path& pref_dir,
                     const std::u8string& game_id,
                     const std::filesystem::path& rom_path);

// Converts any save the OGRE_SAVE path accepts (the port's 32 KiB image, an
// emulator wrapper of it, a DexDrive .N64 dump, or a bare Controller Pak such as
// the port's own `<config>/saves/<game id>.mpk`) into the 32 KiB battery image.
// The Controller Pak's Ogre Battle 64 notes become battery slots. Returns false,
// with the reason in `description`, when the file holds no usable save.
bool convert_save_file(const std::filesystem::path& src, std::vector<uint8_t>& image,
                       std::string& description);

} // namespace ogre

