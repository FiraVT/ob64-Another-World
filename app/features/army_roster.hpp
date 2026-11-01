// The army's characters, read from guest RDRAM, for the overlay's ROSTER tab.
//
// Character record 0x80193BE0 + n*0x38 (n 1..99): +0x00 name, +0x11 class,
// +0x13 level. Class names come from the class table 0x80187C34 + c*0x48,
// +0x00 name pointer.
#pragma once

#include <string>
#include <vector>

namespace ogre::army_roster {

struct Character {
    int record = 0;  // character record index (1..99)
    std::string name;
    std::string class_name;
    int level = 0;
};

// Every character with a class, in record order. Empty before the game runs.
std::vector<Character> read();

}  // namespace ogre::army_roster

