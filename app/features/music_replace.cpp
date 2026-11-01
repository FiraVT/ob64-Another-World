// Song replacement. See music_replace.hpp.

#include "music_replace.hpp"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace ogre::music_replace {

namespace {

// libmus state (docs/anotherworld/notes-audio-mix.md).
constexpr uint32_t CHANNEL_COUNT = 0x800B1804u;  // s32
constexpr uint32_t CHANNEL_ARRAY = 0x800B180Cu;  // pointer
constexpr uint32_t CHANNEL_STRIDE = 0x13Cu;
constexpr uint32_t CH_DATA = 0x04u;         // current data pointer; 0 = idle
constexpr uint32_t CH_FADE = 0x10u;         // stop fade counter; -1 = none
constexpr uint32_t CH_FADE_LENGTH = 0x1Cu;  // stop fade length
constexpr uint32_t CH_HANDLE = 0x44u;       // MusStartSong's handle
constexpr uint32_t CH_SONG = 0x74u;         // song data the channel came from
constexpr uint32_t CH_EFFECT = 0x78u;       // non-zero = sound effect
constexpr uint32_t CH_VOLUME = 0x9Eu;       // u16 per-handle volume, 0x80 = 1.0
constexpr uint16_t VOLUME_UNITY = 0x80u;
constexpr uint32_t SONGS_MASTER = 0x800B181Eu;  // u16, 0x7FFF = 1.0
constexpr int MAX_CHANNELS = 64;

// The game's sound slots (boot overlay A, func_800EA0E4's callers).
constexpr uint32_t SLOT_TABLE = 0x800EB0B0u;
constexpr uint32_t SLOT_STRIDE = 0x50u;
constexpr int SLOTS = 7;
constexpr uint32_t SLOT_SONG = 0x2Cu;    // 1-based song number
constexpr uint32_t SLOT_BUFFER = 0x48u;  // where the song was DMA'd
constexpr uint32_t SONG_ROM_TABLE = 0x800EB4F0u;  // {rom start, rom end} per song
constexpr int MAX_SONG = 256;

constexpr uint32_t RDRAM_SIZE = 0x800000u;
constexpr size_t TRACK_CACHE = 4;

enum class TrackState { Loading, Ready, Failed };

struct Track {
    std::vector<int16_t> pcm;  // interleaved stereo s16 at `frequency`
    uint32_t frequency = 0;
    std::atomic<TrackState> state{TrackState::Loading};
};

struct Song {
    uint32_t handle = 0;
    int id = -1;
    std::shared_ptr<Track> track;  // null = the game's own song plays
    size_t frame = 0;
    uint16_t volume = VOLUME_UNITY;  // the game's last per-handle volume
    float fade = 1.0f;
    float gain = 0.0f;
    float mixed_gain = 0.0f;  // gain at the end of the last mixed buffer
    bool seen = false;
};

std::filesystem::path g_music_dir;
std::vector<Song> g_songs;
std::map<int, std::shared_ptr<Track>> g_tracks;

bool in_rdram(uint32_t address, uint32_t size) {
    const uint32_t offset = address & 0x1FFFFFFFu;
    return (address & 0xE0000000u) == 0x80000000u && offset + size <= RDRAM_SIZE;
}

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    rdram[(address & 0x1FFFFFFFu) ^ 3u] = value;
}

uint16_t rd16(const uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>((rd8(rdram, address) << 8) | rd8(rdram, address + 1));
}

void wr16(uint8_t* rdram, uint32_t address, uint16_t value) {
    wr8(rdram, address, static_cast<uint8_t>(value >> 8));
    wr8(rdram, address + 1, static_cast<uint8_t>(value));
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd16(rdram, address)) << 16) | rd16(rdram, address + 2);
}

// Loads and converts a WAV on its own thread, so the audio thread never waits
// on the disk.
void load_track(std::shared_ptr<Track> track, std::filesystem::path path) {
    SDL_AudioSpec spec{};
    Uint8* data = nullptr;
    Uint32 length = 0;
    const std::string name = path.string();
    if (SDL_LoadWAV(name.c_str(), &spec, &data, &length) == nullptr) {
        std::fprintf(stderr, "[music] cannot read %s: %s\n", name.c_str(), SDL_GetError());
        track->state.store(TrackState::Failed);
        return;
    }
    SDL_AudioCVT cvt{};
    if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2,
                          static_cast<int>(track->frequency)) < 0) {
        std::fprintf(stderr, "[music] cannot convert %s: %s\n", name.c_str(), SDL_GetError());
        SDL_FreeWAV(data);
        track->state.store(TrackState::Failed);
        return;
    }
    std::vector<Uint8> buffer(static_cast<size_t>(length) * static_cast<size_t>(std::max(cvt.len_mult, 1)));
    std::memcpy(buffer.data(), data, length);
    SDL_FreeWAV(data);
    cvt.buf = buffer.data();
    cvt.len = static_cast<int>(length);
    if (cvt.needed && SDL_ConvertAudio(&cvt) < 0) {
        std::fprintf(stderr, "[music] cannot convert %s: %s\n", name.c_str(), SDL_GetError());
        track->state.store(TrackState::Failed);
        return;
    }
    const size_t bytes = cvt.needed ? static_cast<size_t>(cvt.len_cvt) : static_cast<size_t>(length);
    track->pcm.resize(bytes / sizeof(int16_t) / 2 * 2);
    if (track->pcm.empty()) {
        std::fprintf(stderr, "[music] %s has no samples\n", name.c_str());
        track->state.store(TrackState::Failed);
        return;
    }
    std::memcpy(track->pcm.data(), buffer.data(), track->pcm.size() * sizeof(int16_t));
    std::fprintf(stderr, "[music] loaded %s (%.1f s)\n", name.c_str(),
                 static_cast<double>(track->pcm.size() / 2) / track->frequency);
    track->state.store(TrackState::Ready);
}

// The track for song `id`, or null when there is no file. A track stays cached
// so a song that comes back does not reload.
std::shared_ptr<Track> track_for(int id, uint32_t frequency) {
    if (id <= 0 || g_music_dir.empty()) {
        return nullptr;
    }
    auto cached = g_tracks.find(id);
    if (cached != g_tracks.end() && cached->second->frequency == frequency &&
        cached->second->state.load() != TrackState::Failed) {
        return cached->second;
    }
    const std::filesystem::path path = g_music_dir / (std::to_string(id) + ".wav");
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return nullptr;
    }
    // Drop a cached track nothing plays, so a long session keeps a few songs.
    while (g_tracks.size() >= TRACK_CACHE) {
        auto unused = std::find_if(g_tracks.begin(), g_tracks.end(),
                                   [](const auto& entry) { return entry.second.use_count() == 1; });
        if (unused == g_tracks.end()) {
            break;
        }
        g_tracks.erase(unused);
    }
    auto track = std::make_shared<Track>();
    track->frequency = frequency;
    g_tracks[id] = track;
    std::thread(load_track, track, path).detach();
    return track;
}

int song_number(const uint8_t* rdram, uint32_t song_data) {
    for (int i = 0; i < SLOTS; i++) {
        const uint32_t slot = SLOT_TABLE + static_cast<uint32_t>(i) * SLOT_STRIDE;
        if (rd32(rdram, slot + SLOT_BUFFER) == song_data) {
            return static_cast<int>(rd32(rdram, slot + SLOT_SONG));
        }
    }
    return -1;
}

void start_song(const uint8_t* rdram, Song& song, uint32_t song_data, uint32_t frequency) {
    song.id = song_number(rdram, song_data);
    if (song.id <= 0 || song.id > MAX_SONG) {
        song.id = -1;
        std::fprintf(stderr, "[music] song ? started (handle %u, data 0x%08X has no slot)\n",
                     song.handle, song_data);
        return;
    }
    const uint32_t rom = rd32(rdram, SONG_ROM_TABLE + static_cast<uint32_t>(song.id - 1) * 8u);
    song.track = track_for(song.id, frequency);
    std::fprintf(stderr, "[music] song %d started (rom 0x%06X, handle %u)%s\n", song.id, rom,
                 song.handle, song.track ? ", replaced by its .wav" : "");
}

}  // namespace

void init(const std::filesystem::path& pref_dir) {
    g_music_dir = pref_dir / "music";
}

void update(uint8_t* rdram, uint32_t frequency) {
    if (rdram == nullptr || frequency == 0) {
        return;
    }
    const uint32_t array = rd32(rdram, CHANNEL_ARRAY);
    const int count = static_cast<int>(rd32(rdram, CHANNEL_COUNT));
    if (count <= 0 || count > MAX_CHANNELS || !in_rdram(array, static_cast<uint32_t>(count) * CHANNEL_STRIDE)) {
        return;
    }
    for (Song& song : g_songs) {
        song.seen = false;
    }
    for (int i = 0; i < count; i++) {
        const uint32_t channel = array + static_cast<uint32_t>(i) * CHANNEL_STRIDE;
        if (rd32(rdram, channel + CH_DATA) == 0 || rd32(rdram, channel + CH_EFFECT) != 0) {
            continue;
        }
        const uint32_t handle = rd32(rdram, channel + CH_HANDLE);
        if (handle == 0) {
            continue;
        }
        auto it = std::find_if(g_songs.begin(), g_songs.end(),
                               [handle](const Song& song) { return song.handle == handle; });
        if (it == g_songs.end()) {
            Song song;
            song.handle = handle;
            start_song(rdram, song, rd32(rdram, channel + CH_SONG), frequency);
            g_songs.push_back(std::move(song));
            it = g_songs.end() - 1;
        }
        Song& song = *it;
        song.seen = true;
        if (!song.track) {
            continue;
        }
        // Mute through the per-handle volume. A non-zero value is one the game
        // wrote since the last buffer (MusHandleSetVolume), and the WAV follows it.
        const uint16_t volume = rd16(rdram, channel + CH_VOLUME);
        if (song.track->state.load() == TrackState::Failed) {
            if (volume == 0) {
                wr16(rdram, channel + CH_VOLUME, song.volume);
            }
            continue;
        }
        if (volume != 0) {
            song.volume = volume;
            wr16(rdram, channel + CH_VOLUME, 0);
        }
        const uint32_t fade = rd32(rdram, channel + CH_FADE);
        const uint32_t fade_length = rd32(rdram, channel + CH_FADE_LENGTH);
        song.fade = fade == 0xFFFFFFFFu || fade_length == 0
                        ? 1.0f
                        : std::min(1.0f, static_cast<float>(fade) / static_cast<float>(fade_length));
    }
    const float master = static_cast<float>(rd16(rdram, SONGS_MASTER)) / 32767.0f;
    for (auto it = g_songs.begin(); it != g_songs.end();) {
        if (!it->seen) {
            if (it->id > 0) {
                std::fprintf(stderr, "[music] song %d stopped\n", it->id);
            }
            it = g_songs.erase(it);
            continue;
        }
        if (it->track && it->track->state.load() == TrackState::Failed) {
            it->track.reset();
        }
        it->gain = master * (static_cast<float>(it->volume) / VOLUME_UNITY) * it->fade;
        ++it;
    }
}

bool active() {
    return std::any_of(g_songs.begin(), g_songs.end(), [](const Song& song) {
        return song.track && song.track->state.load() == TrackState::Ready;
    });
}

void mix(int16_t* samples, size_t count) {
    const size_t frames = count / 2;
    if (frames == 0) {
        return;
    }
    for (Song& song : g_songs) {
        if (!song.track || song.track->state.load() != TrackState::Ready) {
            continue;
        }
        const std::vector<int16_t>& pcm = song.track->pcm;
        const size_t length = pcm.size() / 2;
        // Ramp from the last buffer's gain so a fade or a slider move is smooth.
        const float step = (song.gain - song.mixed_gain) / static_cast<float>(frames);
        float gain = song.mixed_gain;
        for (size_t f = 0; f < frames; f++) {
            gain += step;
            if (song.frame >= length) {
                song.frame = 0;
            }
            for (size_t c = 0; c < 2; c++) {
                const float mixed = samples[f * 2 + c] + pcm[song.frame * 2 + c] * gain;
                samples[f * 2 + c] = static_cast<int16_t>(std::clamp(mixed, -32768.0f, 32767.0f));
            }
            song.frame++;
        }
        song.mixed_gain = song.gain;
    }
}

}  // namespace ogre::music_replace

