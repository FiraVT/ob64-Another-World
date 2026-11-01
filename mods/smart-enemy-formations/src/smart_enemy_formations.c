// Smart Enemy Formations - an Ogre Battle 64: Recomp mod.
//
// The enemy units keep their members, classes and levels, but each member is
// moved to the row its class fights best from: a Knight to the front, a Wizard
// to the back, an Archer to the middle. The game's own class data decides it,
// so no class is special-cased.
//
// Unit records. `0x80197210 + u * 0x19`, 60 records; 0..29 are the player's,
// 30..59 the enemy's (bankR's constructor `func_ovlR_80215360` sets `+0x01` to
// `0x81` for them). `+0x02..+0x06` are the five member references, the leader
// first, and `+0x07..+0x0B` each member's square. A reference of 1..99 is an
// enemy character record, 100 and up a generic soldier, 0 an empty slot.
//
// Enemy character records. `0x80195580 + r * 0x34`; the class is `+0x11` and
// the class key `+0x12`, the same pair the player's records at `0x80193BE0`
// carry and every class accessor takes (`func_8016E250(class, key, row)`).
//
// Squares. A 3x3 grid, square = row * 3 + column, with row 0 the BACK
// (`func_ovlAA` at `0x802164CC`: square < 3 -> row argument 2, < 6 -> 1,
// else 0, and the row argument 0 reads the front attack). The Organize Screen's
// neighbour table (`0x801EF494`/`0x801EF4A0`, offsets +-1 +-2 +-3 +-4) is the
// same row-major layout.
//
// Class records. `0x80187C34 + c * 0x48`: `+0x04` size (2 = large), `+0x45`
// key, `+0x38/+0x3A/+0x3C` the number of attacks from the front, middle and
// back row. The best row is the one with the most attacks.
//
// A large character's second square is a neighbour the Organize Screen picks,
// so a large member is given a row to itself; with the Small Large Units mod
// on, every class reads size 1 and this never happens.
//
// When: from the per-frame hook the other mods use, each enemy unit is laid out
// once when members appear in it that were not there before (the map loader
// creating it, a reinforcement reusing the slot, or a save being loaded). A
// member dying only removes a reference, so a unit is never rearranged during
// a battle. The layout depends only on the members, so applying it again after
// a save load gives the same formation.

#include "modding.h"

#define UNIT_TABLE    0x80197210u
#define UNIT_STRIDE   0x19u
#define UNIT_FIRST    30u
#define UNIT_COUNT    60u
#define UNIT_FLAGS    0x01u
#define UNIT_MEMBERS  0x02u
#define UNIT_SQUARES  0x07u
#define UNIT_ENEMY    0x80u
#define MEMBER_MAX    5u

#define ENEMY_CHARS   0x80195580u
#define ENEMY_STRIDE  0x34u
#define CHAR_CLASS    0x11u
#define CHAR_KEY      0x12u
#define SOLDIER_REF   100u

#define CLASS_TABLE   0x80187C34u
#define CLASS_STRIDE  0x48u
#define CLASS_COUNT   165u
#define CLASS_SIZE    0x04u
#define CLASS_FRONT_N 0x38u
#define CLASS_MID_N   0x3Au
#define CLASS_BACK_N  0x3Cu
#define CLASS_KEY     0x45u
#define CLASS_SOLDIER 1u

#define ROW_BACK  0u
#define ROW_MID   1u
#define ROW_FRONT 2u

static unsigned char rd8(unsigned int addr) {
    return *(volatile unsigned char*)addr;
}

static void wr8(unsigned int addr, unsigned char value) {
    *(volatile unsigned char*)addr = value;
}

// The member references each enemy unit had when it was last laid out.
static unsigned char s_seen[UNIT_COUNT - UNIT_FIRST][MEMBER_MAX];

// The class a member fights as, resolved like the game's accessors: the key
// selects the key class's record unless the class's own record carries it.
static unsigned int member_class(unsigned int ref) {
    unsigned int rec;
    unsigned int cls;
    unsigned int key;

    if (ref >= SOLDIER_REF) {
        return CLASS_SOLDIER;
    }
    rec = ENEMY_CHARS + ref * ENEMY_STRIDE;
    cls = rd8(rec + CHAR_CLASS);
    key = rd8(rec + CHAR_KEY);
    if (cls >= CLASS_COUNT) {
        return CLASS_SOLDIER;
    }
    if (rd8(CLASS_TABLE + cls * CLASS_STRIDE + CLASS_KEY) != key && key != 0u && key < CLASS_COUNT) {
        cls = key;
    }
    return cls;
}

static unsigned int class_attacks(unsigned int cls, unsigned int row) {
    unsigned int rec = CLASS_TABLE + cls * CLASS_STRIDE;
    unsigned int n;

    if (row == ROW_FRONT) {
        n = rd8(rec + CLASS_FRONT_N);
    } else if (row == ROW_MID) {
        n = rd8(rec + CLASS_MID_N);
    } else {
        n = rd8(rec + CLASS_BACK_N);
    }
    return n > 9u ? 0u : n;
}

// Rows in the order a member wants them: most attacks first, ties going to the
// front, then the middle.
static void row_order(unsigned int cls, unsigned char order[3]) {
    static const unsigned char tie[3] = { ROW_FRONT, ROW_MID, ROW_BACK };
    unsigned int n[3];
    unsigned int i;
    unsigned int j;

    for (i = 0; i < 3u; i++) {
        order[i] = tie[i];
        n[i] = class_attacks(cls, tie[i]);
    }
    for (i = 1; i < 3u; i++) {
        for (j = i; j > 0u && n[j] > n[j - 1u]; j--) {
            unsigned int tn = n[j];
            unsigned char to = order[j];
            n[j] = n[j - 1u];
            order[j] = order[j - 1u];
            n[j - 1u] = tn;
            order[j - 1u] = to;
        }
    }
}

// How much a member cares about its best row: the gap to its second choice.
static unsigned int preference(unsigned int cls, const unsigned char order[3]) {
    return class_attacks(cls, order[0]) - class_attacks(cls, order[1]);
}

static void layout_unit(unsigned int unit) {
    static const unsigned char columns[3] = { 1u, 0u, 2u };
    unsigned int refs[MEMBER_MAX];
    unsigned int cls[MEMBER_MAX];
    unsigned int large[MEMBER_MAX];
    unsigned char order[MEMBER_MAX][3];
    unsigned int placed[MEMBER_MAX];
    unsigned int used[3];
    unsigned int row_large[3];
    unsigned int count = 0;
    unsigned int i;
    unsigned int k;

    for (i = 0; i < MEMBER_MAX; i++) {
        unsigned int ref = rd8(unit + UNIT_MEMBERS + i);
        if (ref == 0u) {
            continue;
        }
        refs[count] = i;
        cls[count] = member_class(ref);
        large[count] = rd8(CLASS_TABLE + cls[count] * CLASS_STRIDE + CLASS_SIZE) == 2u;
        row_order(cls[count], order[count]);
        placed[count] = 0;
        count++;
    }
    if (count == 0u) {
        return;
    }
    for (i = 0; i < 3u; i++) {
        used[i] = 0;
        row_large[i] = 0;
    }

    // Large members first (each takes a whole row), then the rest by how much
    // they care, the leader winning a tie.
    for (k = 0; k < count; k++) {
        unsigned int best = MEMBER_MAX;
        unsigned int best_pref = 0;
        unsigned int best_large = 0;
        unsigned int choice;
        unsigned int row = 3u;
        unsigned int col;

        for (i = 0; i < count; i++) {
            unsigned int pref;
            if (placed[i]) {
                continue;
            }
            pref = preference(cls[i], order[i]);
            if (best == MEMBER_MAX || large[i] > best_large ||
                (large[i] == best_large && pref > best_pref)) {
                best = i;
                best_pref = pref;
                best_large = large[i];
            }
        }
        placed[best] = 1;

        for (choice = 0; choice < 3u; choice++) {
            unsigned int r = order[best][choice];
            if (row_large[r]) {
                continue;
            }
            if (large[best] ? used[r] == 0u : used[r] < 3u) {
                row = r;
                break;
            }
        }
        if (row == 3u) {
            // No row fits (only possible with an unexpected member mix):
            // leave the unit as the game built it.
            return;
        }

        col = columns[used[row]];
        used[row] = large[best] ? 3u : used[row] + 1u;
        row_large[row] = large[best];
        // Squares are written after every member has a place.
        order[best][0] = (unsigned char)(row * 3u + col);
    }

    for (i = 0; i < count; i++) {
        wr8(unit + UNIT_SQUARES + refs[i], order[i][0]);
    }
}

RECOMP_HOOK("func_80072944")
void smart_enemy_formations_tick(void) {
    unsigned int u;

    for (u = UNIT_FIRST; u < UNIT_COUNT; u++) {
        unsigned int unit = UNIT_TABLE + u * UNIT_STRIDE;
        unsigned char* seen = s_seen[u - UNIT_FIRST];
        unsigned int fresh = 0;
        unsigned int members = 0;
        unsigned int i;
        unsigned int j;

        if ((rd8(unit + UNIT_FLAGS) & UNIT_ENEMY) != 0u) {
            for (i = 0; i < MEMBER_MAX; i++) {
                members += rd8(unit + UNIT_MEMBERS + i) != 0u;
            }
        }
        if (members == 0u) {
            // An empty or non-enemy slot: forget it, so a unit created here
            // later is laid out even if it reuses the same character records.
            for (i = 0; i < MEMBER_MAX; i++) {
                seen[i] = 0;
            }
            continue;
        }
        for (i = 0; i < MEMBER_MAX; i++) {
            unsigned int ref = rd8(unit + UNIT_MEMBERS + i);
            unsigned int known = 0;
            if (ref == 0u) {
                continue;
            }
            for (j = 0; j < MEMBER_MAX; j++) {
                if (seen[j] == ref) {
                    known = 1;
                }
            }
            if (!known) {
                fresh = 1;
            }
        }
        if (!fresh) {
            continue;
        }
        layout_unit(unit);
        for (i = 0; i < MEMBER_MAX; i++) {
            seen[i] = rd8(unit + UNIT_MEMBERS + i);
        }
    }
}

