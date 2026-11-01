#pragma once

// Art for new classes (docs/anotherworld/design-class-art.md). At start, before
// the game loads any of it, the port builds a new class's images (its pack's "art" block)
// into the free end of the ROM image as game assets, and points the game's
// tables at them, so the game's own loaders read them.
//
// Built: the portrait, the formation figures of the ordinary lane, and the
// battle sprite's palettes.
//
// Portrait: a new portrait asset (40x48 CI8 and one 80-colour palette) and a
// copy of the portrait id table (asset 0x01DFB36E, 142 entries, with the new
// portraits appended) go into the ROM's free end; the three places that name
// the table (0x8016FC1C, 0x8016FC8C, 0x8016FD78) read its id from
// ogre_portrait_table_hi/lo (tools/relocate_tables.py); a runtime pack sets
// the class's appearance row (+0 and +2) to the new portrait.
//
// Figures: copies of the ordinary lane's two atlases with the new figures
// appended (model 58 up) go into the ROM's free end; the asset loaders'
// entries redirect the atlases to them; the runtime pack sets appearance +4.
//
// Battle: copies of the template's battle descriptors naming recolored palette
// resources, a copy of the descriptor table (asset 0x003B6CD0) with them
// appended and redirected to, and the class's handles in the class animation
// resource naming them (class_animation::set_handle_override).

#include <cstdint>

namespace ogre::class_art {

// Builds the art into the ROM image, once: the first call after the content
// database has loaded does it, later calls return. The main loop calls it
// after the database's tick (sdl_platform.cpp), on the thread that loads the
// database, while the game is still at its boot screens; the game loads the
// portraits, figures and battle tables later.
void build_once();

// The portrait id table's asset id: the copy once build() made one, else the
// ROM's.
uint32_t portrait_table();

// An asset id, or its copy when new class art replaced it (a formation atlas
// with new figures). The asset loaders' entries call it (config/hooks.toml).
uint32_t redirect(uint32_t asset);

}  // namespace ogre::class_art
