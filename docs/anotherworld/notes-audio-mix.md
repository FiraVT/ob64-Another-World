# The game's sound driver (libmus) and the MUSIC / EFFECTS sliders

Code: `app/src/settings.cpp` (`apply_mix_volumes`), called every frame from
`app/src/sdl_platform.cpp` next to the War Director tick.

## What the driver is

The sound driver is `libmus`. It is recognised from two functions:

| address | libmus name | evidence |
|---|---|---|
| `func_800814C4` | `MusSetMasterVolume(flags, volume)` | bit `0x1` stores `volume` at `0x800B181C`, bit `0x2` at `0x800B181E` |
| `func_800819D4` | `MusAsk(flags)` | walks the channel array (`D_800B180C`, count `D_800B1804`, `0x13C` bytes each) and counts active channels, as effects when channel `+0x78` is non-zero and as songs when it is zero |

- **Master volumes.** `0x800B181C` is the effects master (`MUSFLAG_EFFECTS`, `1`) and `0x800B181E` the songs master (`MUSFLAG_SONGS`, `2`).
  - The boot routine `func_80071EB0` sets effects to `0x67FF` and songs to `0x7FFF` (`0x80071FD4`..`0x80071FE8`).
  - Five bank sites call the setter as well.
  - The channel update at `0x80082BCC` multiplies each channel's volume by one of the two masters and shifts the product right by 15. A changed master therefore takes effect on the next update.
- **Other libmus pieces:**
  - The song-data opcode handlers are the `.word` table that starts at `func_80083F8C`.
  - The audio frame is `func_80085908`. It builds the RSP task for the audio microcode `0x8009E050` / `D_800ABDA0` and calls `osAiSetNextBuffer`.

## The sliders

MUSIC and EFFECTS (Settings > AUDIO, saved as `music =` and `effects =` in `settings.cfg`) scale the two masters. They do not replace them.

Each frame, `apply_mix_volumes` does the following for each master:
1. Reads the halfword.
2. If it differs from the value written last frame, the game has set a new master, so the new value becomes the base.
3. Writes `base * percent / 100` back.

The game's own fades and mutes therefore still work, scaled by the slider. VOLUME is still the output gain over the whole mix.

## Song replacement

Code: `app/features/music_replace.cpp`, called from `queue_audio_samples` in `app/src/sdl_platform.cpp`.

A player puts `<pref dir>/music/<n>.wav` beside the save, and that file plays instead of song number `n`.

### Where a song starts

The game starts every song through `func_800EA488` in boot overlay A (ROM `0x3F1B0` → RAM `0x800E9C20`, resident from boot).
- It is the only caller of `MusStartSong` (`func_800814F0`). `MusStartSongFromMarker` (`func_80081520`) has no callers.
- Its argument is one of seven sound slots, `0x50` bytes each, at `0x800EB0B0` (`func_800E9E34` updates all seven through `func_800EA0E4`).
- Slot `+0x2C` is the song number, 1-based. `func_800EA488` DMAs ROM `D_800EB4F0[(n-1)*2]` .. `D_800EB4F4[(n-1)*2]` into the buffer at slot `+0x48`, sets the songs master from `D_800EB2E0[(n-1)*2]`, and calls `MusStartSong(slot+0x48)`. The handle goes to slot `+0x24`, and slot `+0x20` becomes the current song number.
- `__MusIntStartSong` (`func_80083CCC`) takes a new handle from `D_800B1820`, and writes it to each channel's `+0x44`. It writes the song-data address to each channel's `+0x74`.

The song id is the slot's song number `n`. `music_replace` matches a channel's `+0x74` to a slot's `+0x48` buffer to find it. The log line also gives the song's ROM offset.

### libmus channel fields used

| offset | meaning | evidence |
|---|---|---|
| `+0x04` | current data pointer, `0` when idle | `MusAsk` (`func_800819D4`) counts a channel as active when it is non-zero |
| `+0x10` / `+0x1C` | stop fade counter (`-1` = none) and fade length | `MusHandleStop` (`func_80081A54`) sets both to the speed; the channel update at `0x80082BEC` scales the volume by `+0x10 / +0x1C` |
| `+0x44` | song handle | `MusHandleAsk` (`func_80081B08`) |
| `+0x74` | song data | `func_80083CCC` |
| `+0x78` | non-zero = effect | `MusAsk`, the channel update at `0x80082BC0` |
| `+0x9E` | per-handle volume, `0x80` = unity | `MusHandleSetVolume` (`func_80081B60`); `func_80083738` initialises it to `0x80`; the channel update at `0x80082B98` multiplies by it |

### What the module does

It runs on the game's audio thread once per audio buffer, which is also the thread that runs libmus.
1. It scans the channel array for song channels (`+0x04` non-zero, `+0x78` zero) and groups them by handle.
2. A new handle logs `[music] song <n> started (rom 0x…, handle h)`. When `music/<n>.wav` exists, the line ends with `, replaced by its .wav`, and a worker thread loads the file with `SDL_LoadWAV` and converts it with `SDL_AudioCVT` to stereo s16 at the output rate.
3. For a replaced song, it writes `0` to `+0x9E` of each of the song's channels every buffer. A non-zero value read there is a value the game wrote since the last buffer, and it becomes the WAV's handle volume.
4. The WAV is mixed into the output buffer, looping over the whole file. Its gain is songs master (`0x800B181E`) / `0x7FFF` × handle volume / `0x80` × the stop fade. The master already carries the MUSIC slider (`apply_mix_volumes`) and the game's per-song master from `D_800EB2E0`. VOLUME is applied after the mix.
5. When no song channel carries the handle, it logs `[music] song <n> stopped` and the WAV stops.

A file that fails to load logs `[music] cannot read …` and the game's song is unmuted. Up to four loaded songs stay cached.

### Limits

- The first audio buffer of a replaced song is synthesised before the mute lands, so a few milliseconds of the original may sound at the start.
- The WAV always restarts from its beginning and loops whole. There are no loop points.
- A song number that two slots play at once gets one WAV per handle.

## Not verified

None of this has been heard in play. Things to check:
- that MUSIC at 0 silences only the music
- that EFFECTS at 0 silences only the effects
- that a scene which fades the music still fades

Song replacement, with `music/<n>.wav` files in the pref dir:
- that `[music] song <n> started` appears in the log for each song, and that the numbers are stable across runs
- that a song with a file is silent and the WAV plays and loops, with no audible blip of the original at the start
- that a song without a file still plays normally
- that the MUSIC slider scales the WAV, and that EFFECTS does not
- that a scene change which fades the music out also fades the WAV, and that the WAV stops when the song stops
- that a song which the game plays at a lower master is quieter as a WAV too

