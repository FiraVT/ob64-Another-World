// ROM patches: every `.ips` file in `<pref dir>/patches/` is applied to the
// loaded ROM image before the game starts, in file-name order. This is how a
// text fix or a fan translation that ships as an IPS patch reaches the port.
//
// The runtime reads the stored ROM (`recomp::load_stored_rom`) and only then
// calls the game's `on_init_callback`, which is where this runs; every later
// cartridge DMA (`recomp::do_rom_read`) reads the patched bytes. The patch
// edits the image in place, so a record past the end of the ROM is skipped,
// not appended.
//
// Only data changes work. The game's code is recompiled ahead of time, so a
// patch that changes MIPS code changes nothing the port runs; the code span is
// reported, not refused, because the bytes are still what a DMA of that range
// returns.

#include "rom_patch.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <cctype>
#include <vector>

#include <librecomp/game.hpp>

namespace ogre {
namespace {

// The recompiled code span (PLAN.md: 99.05% of `0x001000..0x2B8B70`).
constexpr uint32_t kCodeStart = 0x001000;
constexpr uint32_t kCodeEnd = 0x2B8B70;

struct Result {
    size_t records = 0;
    size_t bytes = 0;
    size_t skipped = 0;
    size_t in_code = 0;
};

uint32_t be(const std::vector<uint8_t>& data, size_t at, int count) {
    uint32_t value = 0;
    for (int i = 0; i < count; i++) {
        value = (value << 8) | data[at + i];
    }
    return value;
}

bool apply_ips(const std::vector<uint8_t>& patch, uint8_t* rom, size_t rom_size, Result& out,
               std::string& error) {
    if (patch.size() < 8 || std::memcmp(patch.data(), "PATCH", 5) != 0) {
        error = "not an IPS patch";
        return false;
    }
    size_t at = 5;
    while (true) {
        if (at + 3 > patch.size()) {
            error = "truncated (no EOF marker)";
            return false;
        }
        if (std::memcmp(patch.data() + at, "EOF", 3) == 0) {
            return true;
        }
        if (at + 5 > patch.size()) {
            error = "truncated record header";
            return false;
        }
        const uint32_t offset = be(patch, at, 3);
        uint32_t size = be(patch, at + 3, 2);
        at += 5;
        bool rle = false;
        uint8_t fill = 0;
        if (size == 0) {
            if (at + 3 > patch.size()) {
                error = "truncated RLE record";
                return false;
            }
            size = be(patch, at, 2);
            fill = patch[at + 2];
            at += 3;
            rle = true;
        } else if (at + size > patch.size()) {
            error = "truncated record data";
            return false;
        }
        out.records++;
        if (offset + size > rom_size) {
            out.skipped++;
        } else {
            if (rle) {
                std::memset(rom + offset, fill, size);
            } else {
                std::memcpy(rom + offset, patch.data() + at, size);
            }
            out.bytes += size;
            if (offset < kCodeEnd && offset + size > kCodeStart) {
                out.in_code++;
            }
        }
        if (!rle) {
            at += size;
        }
    }
}

}  // namespace

void apply_rom_patches(const std::filesystem::path& pref_dir) {
    const std::filesystem::path dir = pref_dir / "patches";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        return;
    }
    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (entry.is_regular_file() && ext == ".ips") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    // The runtime owns the image as a mutable vector and hands it out as a
    // const span; patching it here, before any game thread runs, is the one
    // write it ever sees.
    const std::span<const uint8_t> rom = recomp::get_rom();
    uint8_t* bytes = const_cast<uint8_t*>(rom.data());
    for (const std::filesystem::path& file : files) {
        std::ifstream in(file, std::ios::binary);
        const std::vector<uint8_t> patch((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Result result;
        std::string error;
        const bool ok = apply_ips(patch, bytes, rom.size(), result, error);
        std::fprintf(stderr,
                     "[patch] %s: %s, %zu record(s), %zu byte(s) written, %zu past the ROM end skipped%s\n",
                     file.filename().string().c_str(), ok ? "applied" : error.c_str(), result.records,
                     result.bytes, result.skipped,
                     result.in_code != 0 ? " (some records are in recompiled code and have no effect there)"
                                         : "");
    }
    std::fflush(stderr);
}

}  // namespace ogre

