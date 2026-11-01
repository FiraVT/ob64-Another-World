// Save backups and imports (see save_backup.hpp).
//
// The battery save is `<config>/saves/<game id>.bin`. The runtime reads it once
// when the game first touches SRAM and keeps it in memory after that, writing
// the file back after every in-game save. A backup can therefore be taken at
// any time (it copies the last save the game wrote), but a restore or an import
// is only offered on the start screen, before the runtime has read the file:
// written while the game runs, the next in-game save would overwrite it.

#include "save_backup.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <system_error>

#include "game.hpp"
#include "game_host.hpp"
#include "save_import.hpp"

namespace ogre {
namespace {

constexpr size_t kListed = 4;

std::filesystem::path battery_path() {
    return host::pref_dir() / "saves" / (std::string(reinterpret_cast<const char*>(GAME_ID)) + ".bin");
}

std::filesystem::path backup_dir() {
    return host::pref_dir() / "save-backups";
}

std::string timestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &local);
    return text;
}

bool write_file(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
    out.close();
    return out.good();
}

// Copies the battery to `save-backups/<stamp><suffix>.bin`. Empty on failure.
std::string copy_battery(const std::string& suffix) {
    std::error_code ec;
    const std::filesystem::path src = battery_path();
    if (!std::filesystem::is_regular_file(src, ec)) {
        return {};
    }
    const std::string name = timestamp() + suffix + ".bin";
    std::filesystem::create_directories(backup_dir(), ec);
    std::filesystem::copy_file(src, backup_dir() / name,
                               std::filesystem::copy_options::overwrite_existing, ec);
    return ec ? std::string() : name;
}

}  // namespace

std::string back_up_save() {
    const std::string name = copy_battery("");
    const std::string message = name.empty() ? "NO SAVE TO BACK UP YET" : "BACKED UP AS " + name;
    std::fprintf(stderr, "[save] backup: %s\n", message.c_str());
    return message;
}

std::vector<std::string> list_save_backups() {
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(backup_dir(), ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin") {
            names.push_back(entry.path().filename().string());
        }
    }
    // The names start with the time stamp, so the newest sort last.
    std::sort(names.rbegin(), names.rend());
    if (names.size() > kListed) {
        names.resize(kListed);
    }
    return names;
}

std::string restore_save_backup(const std::string& name) {
    std::error_code ec;
    const std::filesystem::path src = backup_dir() / name;
    if (!std::filesystem::is_regular_file(src, ec)) {
        return "THAT BACKUP IS GONE";
    }
    // Keep what is being replaced, so a restore can be undone.
    copy_battery("-before-restore");
    std::filesystem::create_directories(battery_path().parent_path(), ec);
    std::filesystem::copy_file(src, battery_path(),
                               std::filesystem::copy_options::overwrite_existing, ec);
    const std::string message = ec ? "RESTORE FAILED" : "RESTORED " + name;
    std::fprintf(stderr, "[save] restore: %s\n", message.c_str());
    return message;
}

std::filesystem::path save_import_dir() {
    return host::pref_dir() / "save-import";
}

std::string import_save() {
    // A file the player dropped in `save-import/` wins; otherwise the port's
    // own Controller Pak, which the game's copy/backup screen writes to.
    std::vector<std::filesystem::path> candidates;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(save_import_dir(), ec)) {
        if (entry.is_regular_file()) {
            candidates.push_back(entry.path());
        }
    }
    std::sort(candidates.begin(), candidates.end());
    const std::filesystem::path pak = battery_path().replace_extension(".mpk");
    if (std::filesystem::is_regular_file(pak, ec)) {
        candidates.push_back(pak);
    }
    if (candidates.empty()) {
        return "NOTHING TO IMPORT: PUT A SAVE IN save-import/";
    }
    for (const std::filesystem::path& src : candidates) {
        std::vector<uint8_t> image;
        std::string description;
        if (!convert_save_file(src, image, description)) {
            std::fprintf(stderr, "[save] import: %s: %s\n", src.string().c_str(), description.c_str());
            continue;
        }
        copy_battery("-before-import");
        if (!write_file(battery_path(), image)) {
            return "IMPORT FAILED: COULD NOT WRITE THE SAVE";
        }
        std::fprintf(stderr, "[save] import: %s (%s) -> %s\n", src.string().c_str(),
                     description.c_str(), battery_path().string().c_str());
        return "IMPORTED " + src.filename().string();
    }
    return "NO USABLE SAVE IN save-import/ OR THE CONTROLLER PAK";
}

}  // namespace ogre

