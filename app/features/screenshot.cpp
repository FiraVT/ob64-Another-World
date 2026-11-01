// See screenshot.hpp.

#include "screenshot.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "alerts.hpp"
#include "game_host.hpp"

#ifdef _WIN32
#include <SDL_syswm.h>
#include <windows.h>

// A static copy of the PNG writer; hd_backgrounds.cpp keeps its own.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb/stb_image_write.h"

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif
#endif

namespace ogre::screenshot {
namespace {

// OB64-YYYYMMDD-HHMMSS-mmm.png, so two presses in one second do not collide.
std::string file_name() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const int ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    char name[64];
    std::snprintf(name, sizeof(name), "OB64-%s-%03d.png", stamp, ms);
    return name;
}

}  // namespace

std::filesystem::path capture(SDL_Window* window) {
#ifdef _WIN32
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    if (window == nullptr || !SDL_GetWindowWMInfo(window, &info)) {
        std::fprintf(stderr, "[screenshot] no game window\n");
        return {};
    }
    const HWND hwnd = info.info.win.window;
    RECT rect{};
    GetClientRect(hwnd, &rect);
    const int w = rect.right - rect.left;
    const int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) {
        return {};
    }

    HDC window_dc = GetDC(hwnd);
    HDC memory_dc = CreateCompatibleDC(window_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(window_dc, w, h);
    HGDIOBJ previous = SelectObject(memory_dc, bitmap);
    // PW_RENDERFULLCONTENT asks DWM for the composed picture; a plain BitBlt
    // of a flip-model swap chain reads black.
    const BOOL printed = PrintWindow(hwnd, memory_dc, PW_CLIENTONLY | PW_RENDERFULLCONTENT);

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;  // top-down rows
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    const int rows = printed ? GetDIBits(memory_dc, bitmap, 0, static_cast<UINT>(h), pixels.data(), &bmi,
                                         DIB_RGB_COLORS)
                             : 0;

    SelectObject(memory_dc, previous);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(hwnd, window_dc);
    if (rows != h) {
        std::fprintf(stderr, "[screenshot] the window could not be captured (PrintWindow %d, rows %d)\n",
                     printed ? 1 : 0, rows);
        alerts::post(alerts::Kind::Info, "The screenshot could not be taken.");
        return {};
    }
    // BGRA to RGBA, opaque.
    for (size_t i = 0; i < pixels.size(); i += 4) {
        std::swap(pixels[i], pixels[i + 2]);
        pixels[i + 3] = 255;
    }

    const std::filesystem::path dir = host::pref_dir() / "screenshots";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / file_name();
    if (stbi_write_png(path.string().c_str(), w, h, 4, pixels.data(), w * 4) == 0) {
        std::fprintf(stderr, "[screenshot] could not write %s\n", path.string().c_str());
        alerts::post(alerts::Kind::Info, "The screenshot could not be saved.");
        return {};
    }
    std::fprintf(stderr, "[screenshot] saved %s (%dx%d)\n", path.string().c_str(), w, h);
    alerts::post(alerts::Kind::Good, "Screenshot saved: screenshots/" + path.filename().string());
    return path;
#else
    (void)window;
    std::fprintf(stderr, "[screenshot] not supported on this platform yet\n");
    return {};
#endif
}

}  // namespace ogre::screenshot

