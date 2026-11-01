// See save_profiles.hpp and docs/anotherworld/design-save-system.md.
//
// Battery layout (docs/notes/save-items.md, func_8007599C): slot 14, the
// 16-byte control record, at device offset 0; slots 0 and 1 (0x1850 bytes) at
// 0x10 + n*0x1850; slot 15, the suspend record (0x4AE8 bytes), at 0x30B0. Every
// record's checksums are seeded with its own offset, so a record goes back
// into the slot it came from.

#include "game_addresses.hpp"
#include "save_profiles.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <system_error>

#include "game_host.hpp"
#include "recomp.h"

// The runtime's battery buffer (tools/N64ModernRuntime/librecomp/src/pi.cpp):
// save_read copies device bytes into guest memory, save_write_ptr host bytes
// into the buffer, which the runtime then writes to the battery file.
void save_read(uint8_t* rdram, int32_t rdram_address, uint32_t offset, uint32_t count);
void save_write_ptr(const void* in, uint32_t offset, uint32_t count);

namespace ogre::save_profiles {
namespace {

constexpr uint32_t SCENE = ::ogre::addr::kActiveScene;          // u16
constexpr uint32_t FUNC_SAVE = 0x8007485Cu;      // (slot)
constexpr uint32_t FUNC_MALLOC = 0x80070F30u;    // (size) -> pointer
constexpr uint32_t FUNC_FREE = 0x800712C4u;      // (pointer)
constexpr uint32_t CLOCK = ::ogre::addr::kGameClock;          // hours since the campaign began, 16.16
constexpr uint32_t CHAOS = ::ogre::addr::kChaosFrame;
constexpr uint32_t GOTH = ::ogre::addr::kGoth;

std::mutex g_mutex;
std::atomic<bool> g_pending{false};
std::atomic<bool> g_changed{false};
std::atomic<bool> g_close{false};
bool g_load_pending = false;   // under g_mutex
Entry g_load_entry;            // under g_mutex
bool g_title_pending = false;  // under g_mutex: RETURN TO TITLE
std::string g_message;

uint32_t slot_offset(int slot) {
    return slot == 15 ? 0x30B0u : slot == 14 ? 0u : 0x10u + static_cast<uint32_t>(slot) * 0x1850u;
}

uint32_t slot_size(int slot) {
    return slot == 15 ? 0x4AE8u : slot == 14 ? 0x10u : 0x1850u;
}

uint8_t rd8(const uint8_t* rdram, uint32_t a) {
    return rdram[(a - 0x80000000u) ^ 3u];
}

uint32_t rd32(const uint8_t* rdram, uint32_t a) {
    return (static_cast<uint32_t>(rd8(rdram, a)) << 24) | (static_cast<uint32_t>(rd8(rdram, a + 1)) << 16) |
           (static_cast<uint32_t>(rd8(rdram, a + 2)) << 8) | rd8(rdram, a + 3);
}

uint32_t call(uint8_t* rdram, recomp_context* ctx, uint32_t vram, uint32_t a0) {
    ctx->r4 = static_cast<gpr>(static_cast<int32_t>(a0));
    LOOKUP_FUNC(vram)(rdram, ctx);
    return static_cast<uint32_t>(ctx->r2);
}

// The battery bytes of `slot`, through a guest buffer from the game's own
// allocator.
std::vector<uint8_t> read_slot(uint8_t* rdram, recomp_context* ctx, int slot) {
    const uint32_t size = slot_size(slot);
    const uint32_t buffer = call(rdram, ctx, FUNC_MALLOC, size);
    std::vector<uint8_t> out;
    if (buffer < 0x80000000u || buffer >= 0x80800000u) {
        return out;
    }
    save_read(rdram, static_cast<int32_t>(buffer), slot_offset(slot), size);
    out.resize(size);
    for (uint32_t i = 0; i < size; i++) {
        out[i] = rd8(rdram, buffer + i);
    }
    call(rdram, ctx, FUNC_FREE, buffer);
    return out;
}

std::filesystem::path profiles_dir() {
    return host::pref_dir() / "saves" / "profiles";
}

std::string now_stamp() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
    return text;
}

// The leader's name in a record's summary (record +0x10 + 0x08, 17 bytes).
std::string leader_name(const std::vector<uint8_t>& record) {
    std::string name;
    for (size_t i = 0x18; i < 0x18 + 17 && i < record.size(); i++) {
        const char c = static_cast<char>(record[i]);
        if (c == '\0') {
            break;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '-') {
            name += c;
        }
    }
    return name.empty() ? std::string("Unnamed") : name;
}

// The profile the running campaign saves into (game thread only). A profile was
// the leader's name alone, so every new game named Magnus saved into one folder
// (developer report 2026-10-31). Now each new game gets its own profile on its
// first save: the name, or "<name> 2", "<name> 3" when taken. Loading a profile
// save continues that profile. A game loaded through the game's own Load Game
// is not traced to a profile and saves under the leader's name, as before.
std::string g_profile;
bool g_new_campaign = false;

constexpr int SCENE_NAME_ENTRY = 0x07;  // only a new game reaches the name-entry form
constexpr int SCENE_LOAD_GAME = 0x12;

// True when `profile` is `name` or `name` plus " <number>".
bool profile_of(const std::string& profile, const std::string& name) {
    if (profile == name) {
        return true;
    }
    if (profile.size() <= name.size() + 1 || profile.compare(0, name.size(), name) != 0 ||
        profile[name.size()] != ' ') {
        return false;
    }
    return std::all_of(profile.begin() + static_cast<std::ptrdiff_t>(name.size()) + 1, profile.end(),
                       [](char c) { return c >= '0' && c <= '9'; });
}

std::string choose_profile(const std::string& name) {
    if (!g_profile.empty() && profile_of(g_profile, name)) {
        return g_profile;
    }
    if (!g_new_campaign) {
        return name;
    }
    std::error_code ec;
    std::string profile = name;
    for (int n = 2; std::filesystem::exists(profiles_dir() / profile, ec); n++) {
        profile = name + " " + std::to_string(n);
    }
    g_profile = profile;
    g_new_campaign = false;
    return profile;
}

bool write_file(const std::filesystem::path& path, const void* data, size_t size) {
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

void set_message(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_message = text;
    g_changed.store(true);
    std::fprintf(stderr, "[saves] %s\n", text.c_str());
}

void run_save(uint8_t* rdram, recomp_context* ctx) {
    const uint16_t scene = static_cast<uint16_t>((rd8(rdram, SCENE) << 8) | rd8(rdram, SCENE + 1));
    // The world map writes a normal slot; the mission field and battles the
    // suspend record (the game's own Save and Suspend).
    const int slot = scene == 0x05 ? 1 : (scene == 0x03 || scene == 0x0D) ? 15 : -1;
    if (slot < 0) {
        char text[96];
        std::snprintf(text, sizeof(text), "Saving works on the world map, on the mission field and in battle (scene 0x%02X).",
                      scene);
        set_message(text);
        return;
    }
    const recomp_context saved = *ctx;
    const std::vector<uint8_t> before = read_slot(rdram, ctx, slot);
    call(rdram, ctx, FUNC_SAVE, static_cast<uint32_t>(slot));
    const std::vector<uint8_t> record = read_slot(rdram, ctx, slot);
    *ctx = saved;
    if (record.size() != slot_size(slot) || before.size() != record.size()) {
        set_message("The save could not be read back.");
        return;
    }
    // The player's own record in that slot goes back.
    save_write_ptr(before.data(), slot_offset(slot), static_cast<uint32_t>(before.size()));

    const std::string profile = choose_profile(leader_name(record));
    const std::filesystem::path dir = profiles_dir() / profile;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string stamp = now_stamp();
    const std::filesystem::path base = dir / (stamp + "-s" + std::to_string(slot));
    const uint32_t hours = rd32(rdram, CLOCK) >> 16;
    char details[160];
    std::snprintf(details, sizeof(details), "%s, day %u %02u:00, Chaos Frame %u, %u Goth",
                  slot == 15 ? (scene == 0x0D ? "Battle suspend" : "Field suspend") : "World map", hours / 24 + 1,
                  hours % 24, static_cast<unsigned>(rd8(rdram, CHAOS)), rd32(rdram, GOTH));
    if (!write_file(base.string() + ".rec", record.data(), record.size()) ||
        !write_file(base.string() + ".txt", details, std::strlen(details))) {
        set_message("The save could not be written to " + dir.string());
        return;
    }
    set_message("Saved to " + profile + ": " + details);
}

}  // namespace

void request_save() {
    g_pending.store(true);
    set_message("Saving when the game resumes...");
}

bool save_pending() {
    return g_pending.load();
}

std::string last_message() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_message;
}

std::vector<Entry> list(size_t max) {
    std::vector<Entry> out;
    std::error_code ec;
    for (const auto& profile : std::filesystem::directory_iterator(profiles_dir(), ec)) {
        if (!profile.is_directory()) {
            continue;
        }
        for (const auto& file : std::filesystem::directory_iterator(profile.path(), ec)) {
            if (file.path().extension() != ".rec") {
                continue;
            }
            const std::string stem = file.path().stem().string();  // <stamp>-s<slot>
            const size_t dash = stem.rfind("-s");
            if (dash == std::string::npos) {
                continue;
            }
            Entry e;
            e.profile = profile.path().filename().string();
            e.record = file.path();
            e.stamp = stem.substr(0, dash);
            e.slot = std::atoi(stem.c_str() + dash + 2);
            std::ifstream details(std::filesystem::path(file.path()).replace_extension(".txt"));
            std::getline(details, e.where);
            out.push_back(e);
        }
    }
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) { return a.stamp > b.stamp; });
    if (out.size() > max) {
        out.resize(max);
    }
    return out;
}

std::string load(const Entry& entry) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_load_entry = entry;
        g_load_pending = true;
    }
    g_close.store(true);
    return "Loading " + entry.profile + " (" + entry.stamp + ")...";
}

bool load_file(const std::filesystem::path& record, std::string& error) {
    const std::string stem = record.stem().string();
    const size_t dash = stem.rfind("-s");
    if (record.extension() != ".rec" || dash == std::string::npos) {
        error = "not a profile record (<stamp>-s<slot>.rec): " + record.string();
        return false;
    }
    Entry e;
    e.profile = record.parent_path().filename().string();
    e.record = record;
    e.stamp = stem.substr(0, dash);
    e.slot = std::atoi(stem.c_str() + dash + 2);
    std::error_code ec;
    if ((e.slot != 0 && e.slot != 1 && e.slot != 15) || std::filesystem::file_size(record, ec) != slot_size(e.slot)) {
        error = "not a slot 0, 1 or 15 record of the right size: " + record.string();
        return false;
    }
    std::ifstream details(std::filesystem::path(record).replace_extension(".txt"));
    std::getline(details, e.where);
    if (e.where.empty()) {
        e.where = e.stamp;
    }
    load(e);
    return true;
}

bool take_close_request() {
    return g_close.exchange(false);
}

void request_title() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_title_pending = true;
    }
    g_close.store(true);
}

std::string remove(const Entry& entry) {
    std::error_code ec;
    std::filesystem::remove(entry.record, ec);
    std::filesystem::remove(std::filesystem::path(entry.record).replace_extension(".txt"), ec);
    // An empty profile folder goes too.
    const std::filesystem::path dir = entry.record.parent_path();
    if (std::filesystem::is_empty(dir, ec)) {
        std::filesystem::remove(dir, ec);
    }
    std::fprintf(stderr, "[saves] deleted %s\n", entry.record.string().c_str());
    return "Deleted the " + entry.profile + " save of " + entry.stamp + ".";
}

bool take_changed() {
    return g_changed.exchange(false);
}

namespace {

void wr8(uint8_t* rdram, uint32_t a, uint8_t v) {
    rdram[(a - 0x80000000u) ^ 3u] = v;
}

void wr16(uint8_t* rdram, uint32_t a, uint16_t v) {
    wr8(rdram, a, static_cast<uint8_t>(v >> 8));
    wr8(rdram, a + 1, static_cast<uint8_t>(v));
}

int current_scene(const uint8_t* rdram) {
    return (rd8(rdram, 0x800E810Eu) << 8) | rd8(rdram, 0x800E810Fu);
}

void wr32(uint8_t* rdram, uint32_t a, uint32_t v) {
    wr16(rdram, a, static_cast<uint16_t>(v >> 16));
    wr16(rdram, a + 2, static_cast<uint16_t>(v));
}

// The scene stack (func_80075BC0). 0x800C4BBC points at the current 8-byte
// entry (+0 the entry below, +4 the scene id) and the byte 0x800AF020 counts
// the entries. A pending scene with 0x8000 replaces the current entry's id
// (0x800760C4), one without it pushes an entry (0x800760E0..) and 0xFFFE pops
// one (0x80076044). The title's Load Game runs once per boot, so its suspend
// push (pending 3) happens once; the port's LOAD can run any number of times,
// and each suspend load pushed one more entry. The first title visit records
// its entry, and every load starts from it again.
uint32_t g_title_entry = 0;
uint8_t g_title_depth = 0;

void note_title_entry(const uint8_t* rdram) {
    if (g_title_entry == 0 && current_scene(rdram) == 0x04) {
        const uint32_t entry = rd32(rdram, 0x800C4BBCu);
        if (entry >= 0x80000000u && entry < 0x80800000u) {
            g_title_entry = entry;
            g_title_depth = rd8(rdram, 0x800AF020u);
            std::fprintf(stderr, "[saves] title stack entry 0x%08X depth %u\n", entry, g_title_depth);
        }
    }
}

void reset_scene_stack(uint8_t* rdram, int scene) {
    if (g_title_entry == 0 || rd32(rdram, 0x800C4BBCu) == g_title_entry) {
        return;
    }
    std::fprintf(stderr, "[saves] scene stack depth %u reset to %u\n", rd8(rdram, 0x800AF020u), g_title_depth);
    wr32(rdram, 0x800C4BBCu, g_title_entry);
    wr16(rdram, g_title_entry + 4, static_cast<uint16_t>(scene));
    wr8(rdram, 0x800AF020u, g_title_depth);
}

// The object pool (0x800E82C8, 6 entries of 0xA8; func_80076AE8 and the frame
// update at 0x800767E0 run an entry while its flags halfword has 0x2000 and
// 0x8000; func_80076E58 takes an entry without 0x8000 as free). A mission's
// module registers entries whose callbacks (+0x10..+0x1C, the copy at
// 0x800E7A40..0x800E7A4C) point into it (func_ovlA_8019EE70,
// func_ovlA_801A07F0). Leaving the mission early left them set, and the title
// then called them after its own module had replaced that RAM (run-field15:
// 0x8019F5FC, 0x801A365C, 0x8019F994). Entries whose callbacks point into the
// streamed modules are marked free before the move to the title.
void free_module_objects(uint8_t* rdram) {
    constexpr uint32_t POOL = 0x800E82C8u;   // lui 0x800F + -0x7D38
    constexpr uint32_t COUNT = 0x800C49D0u;  // u16: entries in the run list
    constexpr uint32_t LIST = 0x800C4C10u;   // u16 pool indices, run in order
    auto rd16 = [&](uint32_t a) { return static_cast<uint16_t>((rd8(rdram, a) << 8) | rd8(rdram, a + 1)); };
    auto owned = [&](uint32_t e) {
        for (uint32_t k = 0x10; k <= 0x1C; k += 4) {
            const uint32_t fn = rd32(rdram, e + k);
            if (fn >= 0x80197B90u && fn < 0x80400000u) {
                return true;
            }
        }
        return false;
    };
    const uint16_t count = rd16(COUNT);
    std::fprintf(stderr, "[saves] pool run list count %u\n", count);
    // func_80076AE8 runs each listed entry, even one whose flags lack 0x2000
    // while its +0x18 callback is set, so a mission entry leaves the list
    // (count and order kept for the rest) and is zeroed, as the boot reset
    // func_800764E0 zeroes the pool.
    uint16_t kept[6] = {};
    uint16_t n = 0;
    for (uint16_t i = 0; i < count && i < 6; i++) {
        const uint16_t index = rd16(LIST + i * 2u);
        const uint32_t e = POOL + (index % 6u) * 0xA8u;
        std::fprintf(stderr, "[saves] pool %u flags %04X cb %08X %08X %08X %08X\n", index, rd16(e),
                     rd32(rdram, e + 0x10), rd32(rdram, e + 0x14), rd32(rdram, e + 0x18), rd32(rdram, e + 0x1C));
        if (index < 6 && owned(e)) {
            for (uint32_t b = 0; b < 0xA8u; b++) {
                wr8(rdram, e + b, 0);
            }
            std::fprintf(stderr, "[saves] freed pool object %u\n", index);
        }
        else {
            kept[n++] = index;
        }
    }
    for (uint16_t i = 0; i < n; i++) {
        wr16(rdram, LIST + i * 2u, kept[i]);
    }
    wr16(rdram, COUNT, n);
}

// The Load Game screen's confirm, func_801A9B3C (0x801A9B68..0x801A9C58),
// minus its use of the suspend (func_80074834 at 0x801A9BCC marks the suspend
// as used; a profile suspend stays loadable).
void run_load(uint8_t* rdram, recomp_context* ctx, const Entry& entry) {
    std::ifstream in(entry.record, std::ios::binary);
    std::vector<uint8_t> record((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (record.size() != slot_size(entry.slot)) {
        set_message("That save file is damaged.");
        return;
    }
    const recomp_context saved = *ctx;
    const int slot = entry.slot;
    const std::vector<uint8_t> before = read_slot(rdram, ctx, slot);
    save_write_ptr(record.data(), slot_offset(slot), static_cast<uint32_t>(record.size()));
    call(rdram, ctx, 0x800749C0u, static_cast<uint32_t>(slot));  // the game's loader
    if (before.size() == record.size()) {
        save_write_ptr(before.data(), slot_offset(slot), static_cast<uint32_t>(before.size()));
    }
    const int scene = current_scene(rdram);
    reset_scene_stack(rdram, scene);
    const uint32_t control = rd32(rdram, 0x800C4BBCu);
    std::fprintf(stderr, "[saves] load slot %d from scene 0x%02X\n", slot, scene);
    if (slot == 15) {
        // A suspend resumes through the control value 3 (0x801A9C0C).
        wr16(rdram, 0x800C4C26u, 3);
        if (control >= 0x80000000u && control < 0x80800000u) {
            wr16(rdram, control + 4, 5);
        }
    }
    else {
        wr8(rdram, 0x80187051u, static_cast<uint8_t>(slot));  // 0x801A9BA0
        if (rd8(rdram, 0x801936CAu) != 0x35) {
            wr16(rdram, 0x800C4C26u, 0x8005);  // the world map
        }
        else {
            if (control >= 0x80000000u && control < 0x80800000u) {
                wr16(rdram, control + 4, 5);
            }
            call(rdram, ctx, 0x801723B4u, 0);
            call(rdram, ctx, 0x8017225Cu, 1);
            call(rdram, ctx, 0x80170814u, 0);
            wr16(rdram, 0x800C4C26u, 2);
        }
    }
    *ctx = saved;
    g_profile = entry.profile;
    g_new_campaign = false;
    set_message("Loaded " + entry.profile + ": " + entry.where);
}

// A load made anywhere but the title goes to the title first (a world map to
// world map load broke the heap the same way, run-field19). Loading straight from
// the field (scene 3) into the world map called functions of the mission's
// module (record 3, 0x8019F5FC and 0x801A365C) after the world map's module
// had replaced it, and the game crashed (run-field13). The game's own Load
// Game always starts from the title; 0x8004 is the game's own move to the
// title (0x80177EA8). The field's exits call 0x801723B4, 0x80172144 and
// 0x80170814 first (0x801D6404, 0x801D3A4C), but called from this hook they
// reached the same unloaded functions at once (run-field14), so they are not
// called.
int g_title_stage = 0;  // 0 none, 1 going to the title to load, 2 RETURN TO TITLE; under g_mutex
int g_title_wait = 0;

void leave_for_title(uint8_t* rdram, void* ctx, int scene) {
    free_module_objects(rdram);
    // The scene-script buffers 0x80197B38 / 0x80197B3C: the field's
    // exits free them and zero the words (0x80170814, called at
    // 0x801D3A5C and 0x801D6414). The scene change resets the heap
    // (0x80070F14), so left set they point into freed heap and a
    // later free of them broke the free list: the world map's first
    // allocation wrote through a -1 link in func_80071950
    // (run-field18, error.log).
    std::fprintf(stderr, "[saves] script buffers %08X %08X freed\n", rd32(rdram, 0x80197B38u),
                 rd32(rdram, 0x80197B3Cu));
    {
        recomp_context* c = static_cast<recomp_context*>(ctx);
        const recomp_context saved = *c;
        call(rdram, c, 0x80170814u, 0);
        *c = saved;
    }
    wr16(rdram, 0x800C4C26u, 0x8004);
    std::fprintf(stderr, "[saves] leaving scene 0x%02X for the title\n", scene);
}

}  // namespace

void frame(uint8_t* rdram, void* ctx) {
    if (rdram == nullptr || ctx == nullptr) {
        return;
    }
    // A new game starts a new profile; the game's own Load Game leaves the
    // campaign untraced (g_profile above).
    const int scene_now = current_scene(rdram);
    if (scene_now == SCENE_NAME_ENTRY) {
        g_profile.clear();
        g_new_campaign = true;
    }
    else if (scene_now == SCENE_LOAD_GAME) {
        g_profile.clear();
        g_new_campaign = false;
    }
    if (g_pending.exchange(false)) {
        run_save(rdram, static_cast<recomp_context*>(ctx));
    }
    note_title_entry(rdram);
    // OGRE_AUTOLOAD=<record.rec>: load a profile record once the title runs,
    // so a test run reaches a played state with no menu input (devtest
    // scenarios). Stage 1 lets the title run for a second first, as a load
    // from another scene does.
    static const char* autoload = std::getenv("OGRE_AUTOLOAD");
    static bool autoloaded = false;
    if (autoload != nullptr && autoload[0] != '\0' && !autoloaded && scene_now == 0x04 && g_title_entry != 0) {
        autoloaded = true;
        std::string error;
        if (load_file(autoload, error)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_title_stage = 1;
            g_title_wait = 0;
            std::fprintf(stderr, "[saves] autoload %s\n", autoload);
        }
        else {
            std::fprintf(stderr, "[saves] autoload refused: %s\n", error.c_str());
        }
    }
    Entry entry;
    bool load_now = false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        // RETURN TO TITLE: leave the scene, then put the scene stack back to
        // the title's depth once the title runs, as run_load does.
        if (g_title_pending && !g_load_pending) {
            const int scene = current_scene(rdram);
            if (g_title_stage == 0) {
                if (scene == 0x04) {
                    g_title_pending = false;
                    return;
                }
                leave_for_title(rdram, ctx, scene);
                g_title_stage = 2;
                return;
            }
            if (g_title_stage == 2 && scene == 0x04) {
                reset_scene_stack(rdram, scene);
                g_title_stage = 0;
                g_title_pending = false;
                g_profile.clear();
                g_new_campaign = false;
            }
            return;
        }
        if (g_load_pending) {
            g_title_pending = false;
            const int scene = current_scene(rdram);
            if (g_title_stage == 2) {
                g_title_stage = 1;  // already on the way to the title
                g_title_wait = 0;
            }
            if (g_title_stage == 0 && scene != 0x04 && scene != 0x12) {
                leave_for_title(rdram, ctx, scene);
                g_title_stage = 1;
                g_title_wait = 0;
                return;
            }
            if (g_title_stage == 1) {
                // Load once the title has run for a second.
                g_title_wait = scene == 0x04 ? g_title_wait + 1 : 0;
                if (g_title_wait < 60) {
                    return;
                }
                g_title_stage = 0;
            }
            g_load_pending = false;
            entry = g_load_entry;
            load_now = true;
        }
    }
    if (load_now) {
        run_load(rdram, static_cast<recomp_context*>(ctx), entry);
    }
}

}  // namespace ogre::save_profiles

