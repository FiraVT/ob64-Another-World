#pragma once

// New classes in Change Class (docs/anotherworld/design-new-classes.md
// sections 2c..2e).
//
// The game's Change Class check func_ovlS_801D2378 reads two things per target
// class that exist only for the standard classes 0..80: the family entry (a
// u16 in bank S data at 0x801EF2EC, read through func_ovlS_801CF310 and
// func_ovlS_801CF340, which answer 0 for a class of 81 or more) and the
// requirement record (8 bytes per class in asset 0x01E398D2, 81 records). A
// pack marks a new class standard with a "change_class" block. For those
// classes the two family accessors answer the class's family, and the
// requirements asset grows to 256 records when it is loaded
// (func_8009DD38). Hooks in config/hooks.toml call the functions below.
//
// The game's own class list (func_ovlS_801D2808) keeps a class in 7 bits and
// walks the original family table, so it never lists a new class. The port's
// Change Class view adds the standard new classes to its list itself
// (organize_edit.cpp).

#include <array>
#include <cstdint>
#include <vector>

namespace ogre::class_change {

// A new class that Change Class offers.
struct Standard {
    uint16_t id;
    uint16_t family_of;                 // the standard class whose family entry it takes (0..80)
    std::array<uint8_t, 8> requirements;  // STR VIT INT MEN AGI DEX minimums, alignment min, max; 0xFF none
};

// The content database calls this after the packs apply.
void set_standard_classes(std::vector<Standard> classes);

// The standard new classes, for the Change Class view.
std::vector<Standard> standard_classes();

// The family entry of a standard class (0..80) as the ROM holds it, or 0.
uint16_t original_family(uint16_t cls);

// True for a standard new class (one a pack offers in Change Class).
bool is_standard(uint16_t cls);

// Whether any new class exists (the class key table's copy is needed then).
void set_new_classes_present(bool present);

// Writes the class line and class key table copies into the content arena
// and sets their shifts (tools/relocate_tables.py). The content database
// calls it every frame.
void write_tables(uint8_t* rdram);

}  // namespace ogre::class_change
