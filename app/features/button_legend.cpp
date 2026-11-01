// The button legend: F1 shows a small panel in the game window's upper-left
// corner that names, for every N64 button the game's own prompts mention, the
// key and the pad button bound to it ("C-UP  I / R-STICK UP"). F1 again hides
// it. The game's prompts are drawn from its own art, so this translates them
// instead of replacing them.
//
// Like the speed badge (speed_badge.cpp), it is a borderless, always-on-top
// SDL window over the game window, because RT64 owns the game's swap chain and
// has no UI pass. It is not drawn in fullscreen, for the same reason as the
// badge.

#include "button_legend.hpp"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <ultramodern/config.hpp>

#include "font.hpp"
#include "input_map.hpp"

namespace ogre {
namespace {

constexpr int kScale = 2;
constexpr int kPadding = 10;
constexpr int kMargin = 20;
constexpr float kOpacity = 0.85f;

const SDL_Color kBackground{10, 11, 14, 255};
const SDL_Color kBorder{198, 160, 92, 255};
const SDL_Color kNameColor{198, 160, 92, 255};
const SDL_Color kTextColor{238, 238, 232, 255};

struct LegendState {
    SDL_Window* game_window = nullptr;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Font font;
    bool up = false;
};

LegendState g_legend;

struct Line {
    std::string name;
    std::string binding;
};

std::vector<Line> legend_lines() {
    const InputMap map = read_input_map();
    std::vector<Line> lines;
    // The device used last (input_map.hpp last_input_device); the panel is
    // redrawn every tick, so it switches when the other device is touched.
    const bool pad = pad_in_use();
    for (int i = 0; i < map.size(); i++) {
        lines.push_back(Line{map.info(i).name, pad ? map.pad_text(i) : map.key_text(i)});
    }
    if (pad) {
        lines.push_back(Line{"STICK", "Left Stick (cursor)"});
    }
    return lines;
}

bool create_window() {
    const uint32_t flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
                           SDL_WINDOW_SKIP_TASKBAR | SDL_WINDOW_ALLOW_HIGHDPI;
    g_legend.window = SDL_CreateWindow("Ogre Battle 64: Buttons", 0, 0, 64, 32, flags);
    if (g_legend.window == nullptr) {
        std::fprintf(stderr, "[legend] window: %s\n", SDL_GetError());
        return false;
    }
    g_legend.renderer = SDL_CreateRenderer(g_legend.window, -1, SDL_RENDERER_ACCELERATED);
    if (g_legend.renderer == nullptr) {
        g_legend.renderer = SDL_CreateRenderer(g_legend.window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (g_legend.renderer == nullptr || !g_legend.font.init(g_legend.renderer)) {
        std::fprintf(stderr, "[legend] renderer or font: %s\n", SDL_GetError());
        if (g_legend.renderer != nullptr) {
            SDL_DestroyRenderer(g_legend.renderer);
            g_legend.renderer = nullptr;
        }
        SDL_DestroyWindow(g_legend.window);
        g_legend.window = nullptr;
        return false;
    }
    SDL_SetRenderDrawBlendMode(g_legend.renderer, SDL_BLENDMODE_BLEND);
    SDL_SetWindowOpacity(g_legend.window, kOpacity);
    SDL_HideWindow(g_legend.window);
    return true;
}

int name_column_width() {
    int width = 0;
    for (int i = 0; i < kN64ButtonCount; i++) {
        width = std::max(width, g_legend.font.width(n64_button_info(i).name, kScale));
    }
    return width;
}

// The panel's size for the current bindings, in its own pixels.
void measure(int& width, int& height) {
    const std::vector<Line> lines = legend_lines();
    const int name_width = name_column_width();
    int widest = 0;
    for (const Line& line : lines) {
        widest = std::max(widest, g_legend.font.width(line.binding, kScale));
    }
    const int line_height = g_legend.font.height(kScale) + 4;
    width = kPadding * 3 + name_width + widest;
    height = kPadding * 2 + line_height * static_cast<int>(lines.size());
}

void place(int width, int height) {
    int game_x = 0;
    int game_y = 0;
    SDL_GetWindowPosition(g_legend.game_window, &game_x, &game_y);
    SDL_SetWindowSize(g_legend.window, width, height);
    SDL_SetWindowPosition(g_legend.window, game_x + kMargin, game_y + kMargin);
}

void draw() {
    int width = 0;
    int height = 0;
    measure(width, height);
    place(width, height);
    SDL_RenderSetLogicalSize(g_legend.renderer, width, height);
    SDL_SetRenderDrawColor(g_legend.renderer, kBackground.r, kBackground.g, kBackground.b, 235);
    SDL_RenderClear(g_legend.renderer);
    SDL_SetRenderDrawColor(g_legend.renderer, kBorder.r, kBorder.g, kBorder.b, 255);
    const SDL_Rect border{0, 0, width, height};
    SDL_RenderDrawRect(g_legend.renderer, &border);

    const int name_width = name_column_width();
    const int line_height = g_legend.font.height(kScale) + 4;
    int y = kPadding;
    for (const Line& line : legend_lines()) {
        g_legend.font.draw(g_legend.renderer, line.name, kPadding, y, kScale, kNameColor);
        g_legend.font.draw(g_legend.renderer, line.binding, kPadding * 2 + name_width, y, kScale,
                           kTextColor);
        y += line_height;
    }
    SDL_RenderPresent(g_legend.renderer);
}

bool fullscreen() {
    return ultramodern::renderer::get_graphics_config().wm_option ==
           ultramodern::renderer::WindowMode::Fullscreen;
}

}  // namespace

void button_legend_init(SDL_Window* game_window) {
    g_legend.game_window = game_window;
}

void button_legend_toggle() {
    if (g_legend.game_window == nullptr) {
        return;
    }
    if (g_legend.up) {
        SDL_HideWindow(g_legend.window);
        g_legend.up = false;
        std::fprintf(stderr, "[legend] hidden\n");
        return;
    }
    if (fullscreen()) {
        std::fprintf(stderr, "[legend] fullscreen: the renderer owns the display, so the legend "
                             "is not drawn\n");
        return;
    }
    if (g_legend.window == nullptr && !create_window()) {
        return;
    }
    SDL_ShowWindow(g_legend.window);
    // As with the badge: hand the keyboard straight back to the game.
    SDL_RaiseWindow(g_legend.game_window);
    g_legend.up = true;
    std::fprintf(stderr, "[legend] shown\n");
}

void button_legend_tick() {
    if (!g_legend.up || g_legend.renderer == nullptr) {
        return;
    }
    if (fullscreen()) {
        SDL_HideWindow(g_legend.window);
        g_legend.up = false;
        return;
    }
    draw();
}

void button_legend_shutdown() {
    if (g_legend.renderer != nullptr) {
        SDL_DestroyRenderer(g_legend.renderer);
        g_legend.renderer = nullptr;
    }
    if (g_legend.window != nullptr) {
        SDL_DestroyWindow(g_legend.window);
        g_legend.window = nullptr;
    }
    g_legend.up = false;
}

SDL_Window* button_legend_window() {
    return g_legend.window;
}

}  // namespace ogre

