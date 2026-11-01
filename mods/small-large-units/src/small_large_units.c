// Small Large Units - an Ogre Battle 64: Recomp mod.
//
// Large characters (Ogre, the Dragons, Wyvern, Griffin, Cockatrice, Sphinx,
// Hellhound, Cerberus, the Golems, ...) take two squares of a unit's 3x3 grid
// and count as two of its five members. This mod makes every one of them a
// one-square, one-member character like everybody else, so a unit can hold
// five characters whatever their classes.
//
// The size is a byte in the class table. Class record `c` starts at
// `0x80187C34 + c * 0x48` (`.streamedB` data, ROM `0x5DAB4` for class 0); its
// `+0x00` word points at the class name and its `+0x04` byte is the size:
// 1 for an ordinary class, 2 for every large one. Class 1 is Soldier.
//
// The game asks for the size through one accessor, `func_8016DE3C`
// (`lbu 0x7C38(class * 0x48 + 0x80180000)` at `0x8016DE78`/`0x8016DE84`), and
// no other code reads the byte, the base unit or any bank unit. Rewriting the
// table therefore changes the answer everywhere: the Organize Screen's member
// count, the grid placement and battle.
//
// The table is part of a streamed segment, so it is rewritten every frame from
// the per-frame hook the other example mods use rather than once at startup.

#include "modding.h"

#define CLASS_TABLE  0x80187C34u
#define CLASS_STRIDE 0x48u
#define CLASS_SIZE   0x04u
#define CLASS_COUNT  165u

#define SIZE_SMALL 1u
#define SIZE_LARGE 2u

RECOMP_HOOK("func_80072944")
void small_large_units_tick(void) {
    unsigned int c;

    for (c = 1; c < CLASS_COUNT; c++) {
        volatile unsigned char* size =
            (volatile unsigned char*)(CLASS_TABLE + c * CLASS_STRIDE + CLASS_SIZE);
        if (*size == SIZE_LARGE) {
            *size = SIZE_SMALL;
        }
    }
}

