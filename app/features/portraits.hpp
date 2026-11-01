// The game's speaker portraits, decoded from the ROM for the port's own UI.
// See portraits.cpp for the format and where each table lives.
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ogre::portraits {

constexpr int WIDTH = 40;
constexpr int HEIGHT = 48;

// Decodes the portrait the game shows for class `cls` into `rgba`
// (WIDTH*HEIGHT*4 bytes, RGBA8, top row first). `variant` is the table column
// the game's func_8016FD0C picks with its second argument (0 or 1). Returns
// false when the class has no portrait or the ROM data does not decode.
bool decode(int cls, int variant, std::vector<uint8_t>& rgba);

// The class's formation sprite (the small figure on unit boards), front view in
// the player's palette, as RGBA8 upscaled SPRITE_SCALE times with nearest
// sampling so it stays crisp when drawn larger. `width`/`height` are the
// upscaled size: 16x24 or 32x28 (large classes) times SPRITE_SCALE.
constexpr int SPRITE_SCALE = 4;
// `model_offset` is added to the class's model: the game draws a soldier slot
// as model 0, 1 or 2 (one, two or three soldiers) by the slot's strength.
bool decode_sprite(int cls, std::vector<uint8_t>& rgba, int& width, int& height, int model_offset = 0);

// The game's 16x16 menu icon for a consumable (`equipment` false) or an
// equipment item, RGBA8 upscaled ICON_SCALE times with nearest sampling.
constexpr int ICON_SIZE = 16;
constexpr int ICON_SCALE = 4;
bool decode_icon(bool equipment, int item, std::vector<uint8_t>& rgba);
// The game's LZ decoder (func_8007A110's format) over `src` from `pos`, where
// the block's decoded-size word is. False when the block does not decode.
bool lz_decode(std::span<const uint8_t> src, size_t pos, std::vector<uint8_t>& out);

}  // namespace ogre::portraits

