// Screenshots: Print Screen saves the game window as a PNG (sdl_platform.cpp).
#pragma once

#include <filesystem>

#include <SDL.h>

namespace ogre::screenshot {

// Saves what the game window shows (the RT64 picture and the RmlUi screens
// drawn into it) as `<config>/screenshots/OB64-<date>-<time>.png`, and posts
// an alert with the file name. Windows only: the window is captured with
// PrintWindow(PW_RENDERFULLCONTENT), which reads the composed D3D12/Vulkan
// swap chain. Elsewhere it logs that screenshots are not supported. Returns
// the file written, or an empty path.
std::filesystem::path capture(SDL_Window* window);

}  // namespace ogre::screenshot

