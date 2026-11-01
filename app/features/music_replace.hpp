#pragma once

// Song replacement: `<pref dir>/music/<n>.wav` plays instead of the game's
// song number `n`. See docs/anotherworld/notes-audio-mix.md.
//
// The game starts every song through one function, func_800EA488 (boot
// overlay A). It takes a slot (seven slots, 0x50 bytes each, at 0x800EB0B0),
// DMAs song `slot+0x2C` (1-based; ROM range at 0x800EB4F0 + (n-1)*8) into the
// buffer `slot+0x48`, and calls libmus MusStartSong (func_800814F0 ->
// func_80083CCC). That writes the buffer address to each channel's +0x74 and a
// new handle to each channel's +0x44.
//
// This module reads that state on the game's audio thread, once per audio
// buffer:
//   - A new song handle in the libmus channel array is matched to its slot by
//     the buffer address, which gives the song number. It logs
//     `[music] song <n> started`.
//   - When the file exists, the song's channels are muted through the per-handle
//     volume (+0x9E, MusHandleSetVolume's field), and the WAV is mixed into the
//     output, looping.
//   - The WAV follows the songs master (0x800B181E, which carries the MUSIC
//     slider), the game's per-handle volume and the stop fade (+0x10 / +0x1C).
//   - When no channel carries the handle, the WAV stops.

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ogre::music_replace {

// Records `<pref_dir>/music` as the replacement folder. Called once at startup.
void init(const std::filesystem::path& pref_dir);

// Reads the sound driver's state: logs song starts and stops, and mutes the
// songs that have a replacement. Called on the audio thread once per buffer.
void update(uint8_t* rdram, uint32_t frequency);

// True when a replacement is playing.
bool active();

// Adds the playing replacements to `samples` (interleaved stereo s16, `count`
// values). `samples` must not be guest RDRAM.
void mix(int16_t* samples, size_t count);

}  // namespace ogre::music_replace

