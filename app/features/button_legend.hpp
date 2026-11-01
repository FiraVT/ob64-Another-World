// The button legend (F1): the keys and pad buttons bound to each N64 button,
// over the game window's upper-left corner. See button_legend.cpp.
#pragma once

#include <SDL.h>

namespace ogre {

// Remembers the game window the legend is placed over. Call once after
// `create_window`.
void button_legend_init(SDL_Window* game_window);

// Shows or hides the legend. Called for F1 from the platform's event pump.
void button_legend_toggle();

// One frame on the main thread: redraws the legend while it is up, so a
// rebinding shows at once and the panel follows the game window.
void button_legend_tick();

// Destroys the legend window and renderer.
void button_legend_shutdown();

// The legend's own SDL window, or nullptr before it exists.
SDL_Window* button_legend_window();

}  // namespace ogre

