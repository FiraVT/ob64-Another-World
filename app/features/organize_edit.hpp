// Edits from the F3 Organize view, applied to the game's own Organize working
// copy while the game's Organize screen is open. See organize_edit.cpp.
#pragma once

#include <cstdint>
#include <vector>
#include <string>

namespace ogre::organize_edit {

constexpr int UNIT_SLOTS = 30;
constexpr int SQUARES = 9;
constexpr int EMPTY = 0xFF;
constexpr int MAX_MEMBERS = 5;

// One working unit, as the game's Organize screen holds it.
struct Unit {
    int slot = -1;           // 0..29, the working slot
    int number = 0;          // the unit's number
    int leader_square = EMPTY;
    int squares[SQUARES] = {};  // character ref per square (EMPTY = none; 100+ = soldier)
    int strategy = 0;        // +0x1E, committed to unit record +0x17
    int items[10] = {};      // +0x22 carried item ids (0 = empty slot)
    int soldier_exp = 0;     // +0x1F, committed to unit record +0x0C
};

constexpr int CARRY_SLOTS = 10;

// One consumable stock record (0x80193AE0, 40 records of {u16 item, u8 in use,
// u8 owned}): what Carry Item can hand out.
struct Stock {
    int item = 0;
    int in_use = 0;
    int owned = 0;
};
constexpr int STOCK_RECORDS = 40;
bool read_stock(const uint8_t* rdram, int index, Stock& out);

// The game's name for consumable `item` (the consumable table 0x8018E6EC, 0x0C
// per item, +0x00 name pointer), and whether Carry Item offers it: every
// category but 4, the quest items (letters, Pedras).
std::string item_name(const uint8_t* rdram, int item);
bool carriable(const uint8_t* rdram, int item);

// How many expendable items the unit in working slot `slot` can carry: the sum
// of each member's class value from func_8016D220 (class byte +0x47, soldiers
// as class 1), the game's own func_ovlS_801D0398.
int carry_capacity(const uint8_t* rdram, int slot);

// The Organize state word (0x80196B18) is non-zero while the game's Organize
// screen is open; only then does a working copy exist.
bool working_copy_open(const uint8_t* rdram);

// Reads working slot `slot`. False when the slot is unused or Organize is shut.
bool read_unit(const uint8_t* rdram, int slot, Unit& out);

// Requests, queued from the render thread. The answer (empty on success) shows
// in the view once the main loop has applied the request.
void move(int slot, int from_square, int to_square);    // Reposition: move or swap
// Remove: a member goes back through the game's own release
// (func_ovlS_801CADB0; soldiers return to the reserve), and removing the leader
// disbands the unit (func_ovlS_801CA5A8). Both run on the game thread.
void remove(int slot, int square);
// New Unit: `character` (unplaced, able to lead) leads a new unit from square
// `square`, through the game's own func_ovlS_801CAB88 (game thread).
void new_unit(int character, int square);
// Add into an empty square through the game's own func_ovlS_801CB23C (game
// thread). `character` 0 adds a soldier slot from the reserve (up to three
// soldiers); otherwise an unplaced character.
void add(int slot, int square, int character);

// Soldiers waiting in the reserve (byte 0x80190FA1).
int soldier_reserve(const uint8_t* rdram);
void make_leader(int slot, int square);                  // Change Leader
void set_strategy(int slot, int strategy);               // Battle Strategy 0..3
void carry(int slot, int item);                          // Carry Item: one more of `item`
void drop(int slot, int index);                          // put back the carried item at `index`

constexpr int STRATEGIES = 4;
// The game's names for Battle Strategy 0..3: the strings at 0x80186F90,
// 0x80186FA0, 0x80186FB4 and 0x80186FC8 (ROM 0x5CE90 in streamedB), which the
// pointer table after them lists in this order.
const char* strategy_name(int strategy);

// True when the character may lead a unit: the game's func_8016DEE4 test,
// class byte +0x06 (of the class, or of the class key at character +0x12).
bool can_lead(const uint8_t* rdram, int character);

// --- Equipment, items and the armory ---------------------------------------
//
// Owned equipment: 278 records at 0x80196B20, {u16 item, u8 in use, u8 owned}.
// Owned consumables: 40 records at 0x80193AE0, the same layout (Stock above).
// Equipment items: 0x20 per item at 0x8018C42C, +0x00 name pointer, +0x04
// category, +0x05 element, +0x06 equippable when non-zero, +0x08 u16 price.
// A character's four gear slots are character +0x2A..+0x30; 0 in a slot means
// the class's Basic Equipment for it (class +0x2E + 2*slot, read from the class
// the character's key names when class +0x45 differs from it).
constexpr int EQUIP_RECORDS = 0x116;
constexpr int GEAR_SLOTS = 4;

struct Owned {
    int item = 0;
    int in_use = 0;
    int owned = 0;
};
bool read_owned(const uint8_t* rdram, int index, Owned& out);

int basic_item(const uint8_t* rdram, int character, int slot);   // slot 0..3
int gear_item(const uint8_t* rdram, int character, int slot);    // effective item, basic when 0
bool gear_slot_locked(const uint8_t* rdram, int character, int slot);
int equipment_category(const uint8_t* rdram, int item);
int equipment_price(const uint8_t* rdram, int item);
bool equippable(const uint8_t* rdram, int item);
std::string equipment_name(const uint8_t* rdram, int item);
const char* category_name(int category);
int consumable_price(const uint8_t* rdram, int item);
int consumable_category(const uint8_t* rdram, int item);

// Equip (the game's func_ovlS_801E4C90): `item` 0 puts back the Basic
// Equipment. Only an owned item of the slot's category with one free.
void equip(int character, int slot, int item);
// Basic Equipment: every slot back to the class's own.
void basic_equipment(int character);
// Sell one of an owned item for a quarter of its price. `consumable` picks the
// list; `index` is the record.
void sell(bool consumable, int index);
// Buy Basic Equipment: one set of `cls`'s four basic items.
void buy_basic(int cls);
int basic_set_price(const uint8_t* rdram, int cls);
int basic_set_item(const uint8_t* rdram, int cls, int slot);

// --- Change Class (the game's own, func_ovlS_801E13B0) ----------------------
//
// One class the game offers a character: its candidate list
// (func_ovlS_801D2808), with the eligibility check's result
// (func_ovlS_801D2378: 0 = greyed, reason code at Organize state +0x5ED) and
// the cost of the basic equipment it would buy (func_ovlS_801D2F88).
struct ClassCandidate {
    int cls = 0;
    bool ok = false;
    int reason = 0;   // 1 line, 2 special, 3 equipment, 4 goth, 5 shop, else stats
    int cost = 0;     // goth
    // The class's row of the requirement table (asset 0x01E398D2, read by
    // func_ovlS_801D2378): minimum STR, VIT, INT, MEN, AGI, DEX (character
    // +0x1C..+0x26), minimum and maximum alignment (+0x1B); 0xFF = none.
    int req[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    bool in_game_list = true;  // false: a new class the port added (class_change.hpp)
};
// Asks the game thread for `character`'s list. The answer arrives on a later
// frame; class_candidates() returns true once it is there.
void request_class_list(int character);
bool class_candidates(int character, std::vector<ClassCandidate>& out);
// True when the last class list came from the game's list with `cls` in it,
// so the game's own Change Class screen can select it.
bool in_game_class_list(int cls);
const char* class_reason(int reason);
// Change Class to `target`, through the game's checks and its apply step.
void change_class(int character, int target);

// Change Class through the game's own Change Class screen (screen 20), so the
// game plays its wardrobe animation: the screen is opened for `character`, its
// cursor put on `target`, A pressed, and B pressed when the change is done. The
// view hides while driving() is true. Game thread does the work
// (ogre_organize_frame); injected_buttons() is OR'd into the pad input.
void change_class_shown(int character, int target);
bool driving();
uint16_t injected_buttons();

// Leaves the game's Organize screen the way its Exit does (state 999: commit,
// free, back to the previous screen). Called when the view is closed over it.
void exit_game_screen();

// --- Character, unit and item commands --------------------------------------
//
// Named characters (classes 0x51 and up, the game's message set 0x1C, except
// a new class offered in Change Class) refuse Discharge and Rename, as in the
// game's Character menu.
bool named_character(const uint8_t* rdram, int character);
// Discharge (func_ovlS_801DA7A0): out of the unit first, then the game's
// removal func_8016BA6C and its list refreshes. Game thread.
void discharge(int character);
// Rename: the 16-byte name at character +0x00 (15 characters and the end).
void rename(int character, const std::string& name);
constexpr int NAME_MAX = 15;
// Replenish (func_ovlS_801C9D6C): func_ovlS_801CF818 lists the soldiers with
// missing figures in units that are not dispatched, func_ovlS_801CF1B0 fills
// them from the reserve. Game thread.
void replenish();
// Exchange: the member at (slot_a, square_a) goes to (slot_b, square_b) in
// another unit; a member there comes back the other way. Leaders and soldiers
// stay, as in the game. Game thread, through release and place.
void exchange(int slot_a, int square_a, int slot_b, int square_b);
// Unequip All: every character holding `item` goes back to the slot's Basic
// Equipment; for a consumable, every unit puts it back.
void unequip_all(bool consumable, int item);

// Class List (the game's Class menu): who the game would let take `cls`.
struct ClassHolder {
    int character = 0;
    bool ok = false;
    int reason = 0;
};
void request_class_holders(int cls);
bool class_holders(int cls, std::vector<ClassHolder>& out);
// Classes the army has seen (bitfield 0x80196A96, func_ovlS_801D736C).
bool class_known(const uint8_t* rdram, int cls);

// Applies queued requests with the game threads parked. Main loop only.
void apply_pending(uint8_t* rdram);

// The last request's result: empty while none has finished.
std::string last_result();

}  // namespace ogre::organize_edit

