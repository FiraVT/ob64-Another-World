// Edits from the F3 Organize view (docs/anotherworld/design-organize-screen.md, phase 3).
//
// The game's Organize screen does not edit the unit table (0x80197210) while
// it is open. Its enter, func_ovlS_801D4BB0, copies every unit into a working
// copy inside the Organize state (word 0x80196B18), and its commit,
// func_ovlS_801D5328, writes the working copy back when the screen closes:
//
//   working unit  state + 0x117C + slot*0x36, slots 0..29
//     +0x00 unit number          -> record +0x00
//     +0x01 flags                   (bit 0x10 -> record +0x01 |= 5)
//     +0x02 legion (0xFF none)
//     +0x03 leader's square (0xFF = slot unused)
//     +0x04..+0x0C character ref per square 0..8 (0xFF empty)
//                                -> record members +0x02.. (leader first) and
//                                   squares +0x07..
//     +0x0D record index (0xFF = allocate a free one)
//     +0x1D -> record +0x18, +0x1E -> +0x17 (Battle Strategy), +0x1F -> +0x0C
//     +0x22 10 u16 carried items -> record +0x0D..
//   The commit also clears and sets the leader bit (char +0x33 bit 1).
//
// Verified 2026-10-29 against the developer's RAM snapshots before and after
// vanilla Formation, Remove/Add and Battle Strategy: the working squares match
// what the game committed on exit.
//
// So the view edits only the working copy, with the game threads parked, and
// the game's own commit writes the unit table. The checks here are the ones
// the working copy states directly: at most five members, the leader cannot be
// removed, a square holds one character. A new leader must pass the
// game's own leader test (func_8016DEE4, class byte +0x06), and Battle Strategy
// takes the game's four values (its menu stores the selected index; the store
// is at 0x801E9594).

#include "class_change.hpp"
#include "game_addresses.hpp"
#include "organize_edit.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <mutex>

#include "class_rebalance.hpp"
#include "librecomp/overlays.hpp"
#include "recomp.h"
#include "save_profiles.hpp"
#include "options.hpp"
#include "ultramodern/ultramodern.hpp"

namespace ogre::organize_edit {
namespace {

constexpr uint32_t ORGANIZE_STATE = ::ogre::addr::kOrganizeState;
constexpr uint32_t WORKING_UNITS = 0x117Cu;
constexpr uint32_t WORKING_STRIDE = 0x36u;
constexpr uint32_t CHAR_TABLE = ::ogre::addr::kCharacters;
constexpr uint32_t CHAR_STRIDE = 0x38u;
constexpr int SOLDIER_REF = 100;

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    rdram[(address & 0x1FFFFFFFu) ^ 3u] = value;
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd8(rdram, address)) << 24) | (static_cast<uint32_t>(rd8(rdram, address + 1)) << 16) |
           (static_cast<uint32_t>(rd8(rdram, address + 2)) << 8) | rd8(rdram, address + 3);
}

uint32_t slot_base(const uint8_t* rdram, int slot) {
    return rd32(rdram, ORGANIZE_STATE) + WORKING_UNITS + static_cast<uint32_t>(slot) * WORKING_STRIDE;
}

enum class Kind { Move, Remove, Add, Leader, Strategy, Carry, Drop, Equip, Basic, Sell, Buy, Rename, UnequipAll };

void wr16(uint8_t* rdram, uint32_t address, uint16_t value) {
    wr8(rdram, address, static_cast<uint8_t>(value >> 8));
    wr8(rdram, address + 1, static_cast<uint8_t>(value));
}

uint16_t rd16(const uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>((rd8(rdram, address) << 8) | rd8(rdram, address + 1));
}

void wr32(uint8_t* rdram, uint32_t address, uint32_t value) {
    wr16(rdram, address, static_cast<uint16_t>(value >> 16));
    wr16(rdram, address + 2, static_cast<uint16_t>(value));
}

constexpr uint32_t OWNED_EQUIPMENT = 0x80196B20u;
constexpr uint32_t ITEM_TABLE = ::ogre::addr::kItems;
constexpr uint32_t GOTH_WORD = ::ogre::addr::kGoth;  // War Funds, the same word the view shows
constexpr int OWNED_MAX = 99;

constexpr uint32_t STOCK_TABLE = 0x80193AE0u;  // consumables, 4 bytes per record
// Consumables have their own table, right after the 278-entry equipment table
// (0x8018C42C + 278*0x20): 0x0C per item, {name pointer, u32 whose top byte is
// the category and low bits the price, u32 flags}; entry 0 is "None".
// Categories: 0 expendables (Heal Leaf...), 1 stat items (Champion
// Statuette...), 2 Silver Hourglass / Dowsing Rod / Love and Peace, 3 carried
// charms (Medal of Vigor, Ansate Cross...), 4 quest items (letters, Pedras).
constexpr uint32_t CONSUMABLE_TABLE = 0x8018E6ECu;
constexpr uint32_t CONSUMABLE_STRIDE = 0x0Cu;
constexpr int CONSUMABLE_COUNT = 44;

constexpr uint32_t CLASS_STRIDE = 0x48u;
// The class table the game reads: the moved copy in the content arena (256
// records) when the content database moved it, else the ROM's 165 records.
extern "C" unsigned int ogre_class_table_shift;
uint32_t class_table() {
    return ::ogre::addr::kClasses + (ogre_class_table_shift << 16);
}

struct Request {
    Kind kind;
    int slot;
    int a;  // square
    int b;  // target square, or character
    std::string text = {};
};

std::mutex g_mutex;
std::deque<Request> g_queue;
std::string g_result;

// With LARGE UNITS on VANILLA a large class takes two of the five places and
// the square rules for it are not decoded, so such characters are not moved.
bool blocked_large(const uint8_t* rdram, int ref) {
    if (ref <= 0 || ref >= SOLDIER_REF || !choice_on(Choice::LargeUnits)) {
        return false;
    }
    return class_rebalance::original_large(rd8(rdram, CHAR_TABLE + static_cast<uint32_t>(ref) * CHAR_STRIDE + 0x11));
}

void mark_placed(uint8_t* rdram, int ref);

// True when `character` already stands in some working unit.
bool in_any_unit(const uint8_t* rdram, int character) {
    for (int slot = 0; slot < UNIT_SLOTS; slot++) {
        Unit unit;
        if (!read_unit(rdram, slot, unit)) {
            continue;
        }
        for (int sq : unit.squares) {
            if (sq == character) {
                return true;
            }
        }
    }
    return false;
}

std::string apply_items(uint8_t* rdram, const Request& r);

std::string apply(uint8_t* rdram, const Request& r) {
    if (r.kind == Kind::Equip || r.kind == Kind::Basic || r.kind == Kind::Sell || r.kind == Kind::Buy ||
        r.kind == Kind::Rename || r.kind == Kind::UnequipAll) {
        return apply_items(rdram, r);
    }
    Unit unit;
    if (!read_unit(rdram, r.slot, unit)) {
        return "Open the game's Organize screen first.";
    }
    const uint32_t base = slot_base(rdram, r.slot);
    auto square_ok = [](int sq) { return sq >= 0 && sq < SQUARES; };
    switch (r.kind) {
        case Kind::Move: {
            if (!square_ok(r.a) || !square_ok(r.b) || r.a == r.b) {
                return "Pick two different squares.";
            }
            const int from = unit.squares[r.a];
            const int to = unit.squares[r.b];
            if (from == EMPTY) {
                return "That square is empty.";
            }
            if (blocked_large(rdram, from) || blocked_large(rdram, to)) {
                return "Large characters keep their place while LARGE UNITS is VANILLA.";
            }
            wr8(rdram, base + 0x04 + static_cast<uint32_t>(r.b), static_cast<uint8_t>(from));
            wr8(rdram, base + 0x04 + static_cast<uint32_t>(r.a), static_cast<uint8_t>(to));
            // The leader square follows the leader.
            if (unit.leader_square == r.a) {
                wr8(rdram, base + 0x03, static_cast<uint8_t>(r.b));
            }
            else if (unit.leader_square == r.b) {
                wr8(rdram, base + 0x03, static_cast<uint8_t>(r.a));
            }
            return {};
        }
        case Kind::Remove:
            return "Remove runs on the game thread.";
        case Kind::Leader: {
            if (!square_ok(r.a) || unit.squares[r.a] == EMPTY) {
                return "Pick a character first.";
            }
            if (r.a == unit.leader_square) {
                return "That character already leads the unit.";
            }
            if (!can_lead(rdram, unit.squares[r.a])) {
                return "That class cannot lead a unit.";
            }
            wr8(rdram, base + 0x03, static_cast<uint8_t>(r.a));
            return {};
        }
        case Kind::Strategy:
            if (r.a < 0 || r.a >= STRATEGIES) {
                return "Pick a strategy.";
            }
            wr8(rdram, base + 0x1E, static_cast<uint8_t>(r.a));
            return {};
        case Kind::Carry: {
            // The game's rules: the unit's carry capacity, and a free item in
            // the consumable stock, whose in-use count goes up by one (the
            // Organize code keeps it live; func_ovlS_801CA5A8 takes it back
            // when a unit is disbanded).
            int carried = 0;
            int free_slot = -1;
            for (int i = 0; i < CARRY_SLOTS; i++) {
                if (unit.items[i] != 0) {
                    carried++;
                }
                else if (free_slot < 0) {
                    free_slot = i;
                }
            }
            if (carried >= carry_capacity(rdram, r.slot) || free_slot < 0) {
                return "This unit cannot carry more items.";
            }
            for (int s = 0; s < STOCK_RECORDS; s++) {
                Stock stock;
                if (!read_stock(rdram, s, stock) || stock.item != r.a) {
                    continue;
                }
                if (stock.owned - stock.in_use <= 0) {
                    return "None of that item is left.";
                }
                wr8(rdram, base + 0x22 + static_cast<uint32_t>(free_slot) * 2, static_cast<uint8_t>(r.a >> 8));
                wr8(rdram, base + 0x23 + static_cast<uint32_t>(free_slot) * 2, static_cast<uint8_t>(r.a & 0xFF));
                wr8(rdram, STOCK_TABLE + static_cast<uint32_t>(s) * 4 + 2, static_cast<uint8_t>(stock.in_use + 1));
                return {};
            }
            return "That item is not in the stock.";
        }
        case Kind::Drop: {
            if (r.a < 0 || r.a >= CARRY_SLOTS || unit.items[r.a] == 0) {
                return "Pick a carried item.";
            }
            const int item = unit.items[r.a];
            for (int s = 0; s < STOCK_RECORDS; s++) {
                Stock stock;
                if (read_stock(rdram, s, stock) && stock.item == item && stock.in_use > 0) {
                    wr8(rdram, STOCK_TABLE + static_cast<uint32_t>(s) * 4 + 2, static_cast<uint8_t>(stock.in_use - 1));
                    break;
                }
            }
            // Close the gap, as the game's own lists are packed ([1,4,1] -> [1,4]).
            for (int i = r.a; i < CARRY_SLOTS; i++) {
                const int next = i + 1 < CARRY_SLOTS ? unit.items[i + 1] : 0;
                wr8(rdram, base + 0x22 + static_cast<uint32_t>(i) * 2, static_cast<uint8_t>(next >> 8));
                wr8(rdram, base + 0x23 + static_cast<uint32_t>(i) * 2, static_cast<uint8_t>(next & 0xFF));
            }
            return {};
        }
        case Kind::Add: {
            if (!square_ok(r.a) || unit.squares[r.a] != EMPTY) {
                return "Pick an empty square.";
            }
            int members = 0;
            for (int sq : unit.squares) {
                members += sq != EMPTY ? 1 : 0;
            }
            if (members >= MAX_MEMBERS) {
                return "A unit holds at most five.";
            }
            if (r.b <= 0 || r.b >= SOLDIER_REF || rd8(rdram, CHAR_TABLE + static_cast<uint32_t>(r.b) * CHAR_STRIDE + 0x11) == 0) {
                return "That is not a character.";
            }
            if (in_any_unit(rdram, r.b)) {
                return "That character is already in a unit.";
            }
            if (blocked_large(rdram, r.b)) {
                return "Large characters are added in the game's menu while LARGE UNITS is VANILLA.";
            }
            wr8(rdram, base + 0x04 + static_cast<uint32_t>(r.a), static_cast<uint8_t>(r.b));
            mark_placed(rdram, r.b);
            return {};
        }
    }
    return "Unknown request.";
}

// The game's own bookkeeping when a character joins a unit, as the member loop
// of func_ovlS_801CAB88 does it: the character's Organize flag byte (state +
// 0x1872 + ref*2) gets bit 3 ("placed"); a character without bit 6 also lowers
// the count of unplaced characters at state + 0x5D4.
void mark_placed(uint8_t* rdram, int ref) {
    const uint32_t state = rd32(rdram, ORGANIZE_STATE);
    const uint32_t flags_at = state + 0x1872u + static_cast<uint32_t>(ref) * 2;
    const uint8_t flags = rd8(rdram, flags_at);
    if ((flags & 0x08) != 0) {
        return;
    }
    const uint8_t base_flags = flags & 0x7F;
    if ((flags & 0x40) != 0) {
        wr8(rdram, flags_at, static_cast<uint8_t>(base_flags | 0x48));
        return;
    }
    wr8(rdram, flags_at, static_cast<uint8_t>(base_flags | 0x08));
    const uint32_t count = rd32(rdram, state + 0x5D4u);
    const uint32_t lowered = count - 1;
    wr8(rdram, state + 0x5D4u, static_cast<uint8_t>(lowered >> 24));
    wr8(rdram, state + 0x5D5u, static_cast<uint8_t>(lowered >> 16));
    wr8(rdram, state + 0x5D6u, static_cast<uint8_t>(lowered >> 8));
    wr8(rdram, state + 0x5D7u, static_cast<uint8_t>(lowered));
}

// --- Edits through the game's own routines --------------------------------------
//
// Release, disband and new unit change more than the working squares (the
// reserve, item stock, legion, counters at state + 0x2C0/0x2C4, per-character
// flags), so they call the game's functions. Those need a game thread with a
// guest stack: ogre_organize_frame runs on the frame pump once per frame
// (config/hooks.toml), and only while bank S (ROM 0x87220, the Organize
// screen) is resident.
enum class GameKind { Release, Disband, NewUnit, SetMember, ClassList, ChangeClass, Discharge, Replenish, Exchange,
                      ClassHolders };
struct GameRequest {
    GameKind kind;
    int slot;
    int square;
    int character;
    int slot_b = 0;
    int square_b = 0;
};
std::deque<GameRequest> g_game_queue;

constexpr uint32_t BANK_S_ROM = 0x087220u;
constexpr uint32_t FUNC_RELEASE = 0x801CADB0u;  // release one member (ref in a0)
constexpr uint32_t FUNC_DISBAND = 0x801CA5A8u;  // disband working slot a0
constexpr uint32_t FUNC_NEW_UNIT = 0x801CAB88u; // commit working slot 30 as a new unit
// Place a member: a0 slot, a1 square, a2 member (0 = a soldier slot from the
// reserve). For a soldier it takes a free ref (func_ovlS_801CBC84), moves up to
// three from the reserve 0x80190FA1, sets the strength (0x80190EDC + ref*2) to
// a third of the maximum per soldier (the maximum for three) and refreshes the
// unit (func_ovlS_801D04A4, func_ovlS_801D0714, func_ovlS_801C67D0).
constexpr uint32_t FUNC_SET_MEMBER = 0x801CB23Cu;
constexpr uint32_t SOLDIER_RESERVE = 0x80190FA1u;
constexpr int SCRATCH_SLOT = 30;

void call_game(uint8_t* rdram, recomp_context* ctx, uint32_t vram, uint32_t a0) {
    ctx->r4 = static_cast<gpr>(static_cast<int32_t>(a0));
    LOOKUP_FUNC(vram)(rdram, ctx);
}

uint32_t call_game2(uint8_t* rdram, recomp_context* ctx, uint32_t vram, uint32_t a0, uint32_t a1) {
    ctx->r5 = static_cast<gpr>(static_cast<int32_t>(a1));
    call_game(rdram, ctx, vram, a0);
    return static_cast<uint32_t>(ctx->r2);
}

void wr16_(uint8_t* rdram, uint32_t address, uint16_t value) {
    wr8(rdram, address, static_cast<uint8_t>(value >> 8));
    wr8(rdram, address + 1, static_cast<uint8_t>(value));
}

uint16_t rd16_(const uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>((rd8(rdram, address) << 8) | rd8(rdram, address + 1));
}

// Change Class, from the game's Change Class sub-screen (func_ovlS_801E13B0).
constexpr uint32_t FUNC_LOAD_ASSET = 0x8009DD38u;   // asset id -> new buffer
constexpr uint32_t FUNC_FREE = 0x800712C4u;
constexpr uint32_t CLASS_REQUIREMENTS = 0x01E398D2u; // 8 bytes per class, state +0x80
constexpr uint32_t FUNC_CLASS_LIST = 0x801D2808u;   // state +0x5E8 -> +0x1C0E list, +0x1BFC count
constexpr uint32_t FUNC_CLASS_CHECK = 0x801D2378u;  // (char, class) -> 0 greyed; reason +0x5ED
constexpr uint32_t FUNC_CLASS_COST = 0x801D2F88u;   // (char, class) -> goth for basic gear
constexpr uint32_t FUNC_ITEM_RECORD = 0x8016B6FCu;  // item -> owned record, 0x1FF none
constexpr uint32_t FUNC_FREE_RECORD = 0x801D347Cu;  // first empty owned record
constexpr uint32_t FUNC_UNIT_OF = 0x801CDE68u;      // char -> working slot
constexpr uint32_t FUNC_LEGION_OF = 0x801CDED8u;
constexpr uint32_t GEAR_GETTERS = 0x801EF2A8u;      // 4 pointers: (class, key) -> basic item
constexpr uint32_t GOTH = ::ogre::addr::kGoth;
constexpr uint32_t OWNED = 0x80196B20u;

std::mutex g_class_mutex;
int g_class_character = 0;
std::vector<ClassCandidate> g_class_list;

// The sub-screen's own setup: the requirement table at state +0x80 (loaded on
// entry at 0x801E1418, freed on exit at 0x801E2BA4), the character at +0x18B
// and the class at +0x5E8. Everything it touches is put back afterwards.
struct ClassScreen {
    uint8_t* rdram;
    recomp_context* ctx;
    uint32_t state;
    uint32_t saved_table;
    uint8_t saved_char;
    uint16_t saved_class;
    uint8_t saved_reason;
    uint32_t table;
    ClassScreen(uint8_t* r, recomp_context* c, int character) : rdram(r), ctx(c) {
        state = rd32(rdram, ORGANIZE_STATE);
        saved_table = rd32(rdram, state + 0x80);
        saved_char = rd8(rdram, state + 0x18B);
        saved_class = rd16_(rdram, state + 0x5E8);
        saved_reason = rd8(rdram, state + 0x5ED);
        call_game(rdram, ctx, FUNC_LOAD_ASSET, CLASS_REQUIREMENTS);
        table = static_cast<uint32_t>(ctx->r2);
        wr32_(state + 0x80, table);
        wr8(rdram, state + 0x18B, static_cast<uint8_t>(character));
    }
    ~ClassScreen() {
        wr32_(state + 0x80, saved_table);
        wr8(rdram, state + 0x18B, saved_char);
        wr16_(rdram, state + 0x5E8, saved_class);
        wr8(rdram, state + 0x5ED, saved_reason);
        if (table >= 0x80000000u && table < 0x80800000u) {
            call_game(rdram, ctx, FUNC_FREE, table);
        }
    }
    void wr32_(uint32_t address, uint32_t value) {
        wr16_(rdram, address, static_cast<uint16_t>(value >> 16));
        wr16_(rdram, address + 2, static_cast<uint16_t>(value));
    }
    bool ready() const { return table >= 0x80000000u && table < 0x80800000u; }
};

uint32_t character_record(int character) {
    return CHAR_TABLE + static_cast<uint32_t>(character) * CHAR_STRIDE;
}

std::string run_class_list(uint8_t* rdram, recomp_context* ctx, int character) {
    ClassScreen screen(rdram, ctx, character);
    if (!screen.ready()) {
        std::fprintf(stderr, "[organize] class list for character %d: the requirements load gave 0x%08X\n",
                     character, screen.table);
        return "Could not load the class requirements.";
    }
    const uint32_t record = character_record(character);
    // The class the list is built around: the key byte, which is the class for
    // every ordinary character (the apply writes the new class to both).
    wr16_(rdram, screen.state + 0x5E8, rd8(rdram, record + 0x12));
    call_game(rdram, ctx, FUNC_CLASS_LIST, 0);
    const int count = rd16_(rdram, screen.state + 0x1BFC);
    std::vector<ClassCandidate> list;
    for (int i = 0; i < count && i < 64; i++) {
        const uint16_t entry = rd16_(rdram, screen.state + 0x1C0E + static_cast<uint32_t>(i) * 2);
        ClassCandidate c;
        c.cls = entry & 0xFF;  // the class; 0x8000 is "not selectable" (tools/relocate_tables.py)
        wr8(rdram, screen.state + 0x5ED, 0);
        c.ok = (call_game2(rdram, ctx, FUNC_CLASS_CHECK, static_cast<uint32_t>(character),
                           static_cast<uint32_t>(c.cls)) & 0xFF) != 0;
        c.reason = c.ok ? 0 : rd8(rdram, screen.state + 0x5ED);
        c.cost = static_cast<int>(call_game2(rdram, ctx, FUNC_CLASS_COST, static_cast<uint32_t>(character),
                                             static_cast<uint32_t>(c.cls)));
        for (uint32_t k = 0; k < 8; k++) {
            c.req[k] = rd8(rdram, screen.table + static_cast<uint32_t>(c.cls) * 8 + k);
        }
        list.push_back(c);
    }
    // New classes a pack offers in Change Class (class_change.hpp) that the
    // game's list does not hold: the ones of the character's gender (class
    // +0x05 against +0x14, as func_ovlS_801D6EC0 tests) that the game's check
    // does not refuse for the line (reason 1). The game's list holds them
    // once the army has seen them (class_change.cpp), so this adds none then.
    const int gender = rd8(rdram, record + 0x14);
    for (const class_change::Standard& s : class_change::standard_classes()) {
        if (std::any_of(list.begin(), list.end(), [&](const ClassCandidate& c) { return c.cls == s.id; })) {
            continue;
        }
        const int class_gender = rd8(rdram, class_table() + static_cast<uint32_t>(s.id) * CLASS_STRIDE + 0x05);
        if (class_gender != gender) {
            std::fprintf(stderr, "[organize] class %u not listed for character %d: class gender %d, character %d\n",
                         s.id, character, class_gender, gender);
            continue;
        }
        ClassCandidate c;
        c.cls = s.id;
        c.in_game_list = false;
        wr8(rdram, screen.state + 0x5ED, 0);
        c.ok = (call_game2(rdram, ctx, FUNC_CLASS_CHECK, static_cast<uint32_t>(character),
                           static_cast<uint32_t>(c.cls)) & 0xFF) != 0;
        c.reason = c.ok ? 0 : rd8(rdram, screen.state + 0x5ED);
        if (!c.ok && c.reason == 1) {
            std::fprintf(stderr, "[organize] class %u not listed for character %d (class %d): outside its line\n",
                         s.id, character, rd8(rdram, record + 0x11));
            continue;
        }
        c.cost = static_cast<int>(call_game2(rdram, ctx, FUNC_CLASS_COST, static_cast<uint32_t>(character),
                                             static_cast<uint32_t>(c.cls)));
        for (uint32_t k = 0; k < 8; k++) {
            c.req[k] = rd8(rdram, screen.table + static_cast<uint32_t>(c.cls) * 8 + k);
        }
        list.push_back(c);
    }
    std::lock_guard<std::mutex> lock(g_class_mutex);
    g_class_character = character;
    g_class_list = std::move(list);
    return {};
}

void adjust_use(uint8_t* rdram, recomp_context* ctx, int item, int delta) {
    if (item == 0) {
        return;
    }
    const uint32_t index = call_game2(rdram, ctx, FUNC_ITEM_RECORD, static_cast<uint32_t>(item), 0) & 0xFFFF;
    if (index >= 0x116) {
        return;
    }
    const uint32_t base = OWNED + index * 4;
    wr8(rdram, base + 2, static_cast<uint8_t>(rd8(rdram, base + 2) + delta));
}

int basic_for(uint8_t* rdram, recomp_context* ctx, int slot, int cls, int key) {
    const uint32_t getter = rd32(rdram, GEAR_GETTERS + static_cast<uint32_t>(slot) * 4);
    return static_cast<int>(call_game2(rdram, ctx, getter, static_cast<uint32_t>(cls), static_cast<uint32_t>(key)) & 0xFFFF);
}

std::string run_change_class(uint8_t* rdram, recomp_context* ctx, int character, int target) {
    ClassScreen screen(rdram, ctx, character);
    if (!screen.ready()) {
        return "Could not load the class requirements.";
    }
    const uint32_t record = character_record(character);
    wr16_(rdram, screen.state + 0x5E8, static_cast<uint16_t>(target));
    wr8(rdram, screen.state + 0x5ED, 0);
    if ((call_game2(rdram, ctx, FUNC_CLASS_CHECK, static_cast<uint32_t>(character), static_cast<uint32_t>(target)) & 0xFF) == 0) {
        return class_reason(rd8(rdram, screen.state + 0x5ED));
    }
    // The confirm (0x801E20F0): the basic equipment the class needs is bought.
    const uint32_t cost = call_game2(rdram, ctx, FUNC_CLASS_COST, static_cast<uint32_t>(character), static_cast<uint32_t>(target));
    if (cost != 0) {
        const uint32_t goth = rd32(rdram, GOTH);
        if (goth < cost) {
            return class_reason(4);
        }
        screen.wr32_(GOTH, goth - cost);
    }
    // The apply (0x801E2414..0x801E2664). The old gear goes back: each slot's
    // own item and the old class's basic item lose one use.
    const int old_class = rd8(rdram, record + 0x11);
    const int old_key = rd8(rdram, record + 0x12);
    for (int slot = 0; slot < 4; slot++) {
        adjust_use(rdram, ctx, rd16_(rdram, record + 0x2A + static_cast<uint32_t>(slot) * 2), -1);
        adjust_use(rdram, ctx, basic_for(rdram, ctx, slot, old_class, old_key), -1);
    }
    for (int slot = 0; slot < 4; slot++) {
        wr16_(rdram, record + 0x2A + static_cast<uint32_t>(slot) * 2, 0);
    }
    // A class with a class key (+0x45) keeps its class byte and changes the
    // key; the table at 0x801EF3D8 names the key's own class.
    const int class_key = rd8(rdram, class_table() + static_cast<uint32_t>(old_class) * CLASS_STRIDE + 0x45);
    if (class_key != 0) {
        const int own = rd8(rdram, 0x801EF3D8u + static_cast<uint32_t>(old_class));
        wr8(rdram, record + 0x12, static_cast<uint8_t>(target != own ? target : class_key));
    }
    else {
        wr8(rdram, record + 0x11, static_cast<uint8_t>(target));
        wr8(rdram, record + 0x12, static_cast<uint8_t>(target));
    }
    // The new basic gear: a spare one is taken, otherwise one is added (it was
    // paid for above). A missing record is created (func_ovlS_801D347C).
    for (int slot = 0; slot < 4; slot++) {
        const int item = basic_for(rdram, ctx, slot, target, target);
        if (item == 0) {
            continue;
        }
        uint32_t index = call_game2(rdram, ctx, FUNC_ITEM_RECORD, static_cast<uint32_t>(item), 0) & 0xFFFF;
        if (index == 0x1FF) {
            index = call_game2(rdram, ctx, FUNC_FREE_RECORD, 0, 0) & 0xFFFF;
            if (index >= 0x116) {
                continue;
            }
            screen.wr32_(OWNED + index * 4, static_cast<uint32_t>(item) << 16);
        }
        const uint32_t base = OWNED + index * 4;
        const int in_use = rd8(rdram, base + 2);
        const int owned = rd8(rdram, base + 3);
        wr8(rdram, base + 2, static_cast<uint8_t>(in_use + 1));
        if (in_use >= owned) {
            wr8(rdram, base + 3, static_cast<uint8_t>(owned + 1));
        }
    }
    // The unit refresh when the character stands in one (0x801E2664..0x801E2734).
    if ((rd8(rdram, screen.state + 0x1872 + static_cast<uint32_t>(character) * 2) & 0x8) != 0) {
        const int slot = static_cast<int>(call_game2(rdram, ctx, FUNC_UNIT_OF, static_cast<uint32_t>(character), 0) & 0xFF);
        if (slot < UNIT_SLOTS) {
            call_game(rdram, ctx, 0x801D04A4u, static_cast<uint32_t>(slot));
            call_game(rdram, ctx, 0x801D0714u, static_cast<uint32_t>(slot));
            const int before = static_cast<int>(ctx->r2 & 0xFF);
            call_game(rdram, ctx, 0x801D0714u, static_cast<uint32_t>(slot));
            const int after = static_cast<int>(ctx->r2 & 0xFF);
            const uint32_t unit = slot_base(rdram, slot);
            wr8(rdram, unit + 0x1D, static_cast<uint8_t>(after));
            if (before != after && (rd8(rdram, unit + 0x01) & 0x8) != 0) {
                const int legion = static_cast<int>(call_game2(rdram, ctx, FUNC_LEGION_OF, static_cast<uint32_t>(slot), 0) & 0xFF);
                call_game(rdram, ctx, 0x801D082Cu, static_cast<uint32_t>(legion));
                wr8(rdram, screen.state + 0x10DE + static_cast<uint32_t>(legion) * 14, static_cast<uint8_t>(ctx->r2));
            }
        }
    }
    call_game(rdram, ctx, 0x801CFC44u, 0);
    call_game(rdram, ctx, 0x801D2A18u, 0);
    return {};
}

std::mutex g_holder_mutex;
int g_holder_class = 0;
std::vector<ClassHolder> g_holders;

std::string run_class_holders(uint8_t* rdram, recomp_context* ctx, int cls) {
    ClassScreen screen(rdram, ctx, 1);
    if (!screen.ready()) {
        return "Could not load the class requirements.";
    }
    std::vector<ClassHolder> list;
    for (int character = 1; character < SOLDIER_REF; character++) {
        const uint32_t record = character_record(character);
        if (rd8(rdram, record + 0x11) == 0) {
            continue;
        }
        ClassHolder h;
        h.character = character;
        if (rd8(rdram, record + 0x12) == cls || rd8(rdram, record + 0x11) == cls) {
            h.ok = true;
            h.reason = -1;  // already this class
        }
        else {
            wr8(rdram, screen.state + 0x18B, static_cast<uint8_t>(character));
            wr8(rdram, screen.state + 0x5ED, 0);
            h.ok = (call_game2(rdram, ctx, FUNC_CLASS_CHECK, static_cast<uint32_t>(character), static_cast<uint32_t>(cls)) & 0xFF) != 0;
            h.reason = h.ok ? 0 : rd8(rdram, screen.state + 0x5ED);
        }
        list.push_back(h);
    }
    std::lock_guard<std::mutex> lock(g_holder_mutex);
    g_holder_class = cls;
    g_holders = std::move(list);
    return {};
}

// Where a character stands: working slot and square, or false.
bool find_member(const uint8_t* rdram, int character, int& slot_out, int& square_out) {
    for (int slot = 0; slot < UNIT_SLOTS; slot++) {
        Unit unit;
        if (!read_unit(rdram, slot, unit)) {
            continue;
        }
        for (int sq = 0; sq < SQUARES; sq++) {
            if (unit.squares[sq] == character) {
                slot_out = slot;
                square_out = sq;
                return true;
            }
        }
    }
    return false;
}

std::string run_discharge(uint8_t* rdram, recomp_context* ctx, int character) {
    const uint32_t record = character_record(character);
    if (character <= 0 || character >= SOLDIER_REF || rd8(rdram, record + 0x11) == 0) {
        return "Pick a character.";
    }
    if (named_character(rdram, character)) {
        return "This character will not leave the army.";
    }
    const uint32_t state = rd32(rdram, ORGANIZE_STATE);
    int slot = 0;
    int square = 0;
    if (find_member(rdram, character, slot, square)) {
        Unit unit;
        read_unit(rdram, slot, unit);
        if (unit.leader_square == square) {
            call_game(rdram, ctx, FUNC_DISBAND, static_cast<uint32_t>(slot));
        }
        else {
            call_game(rdram, ctx, FUNC_RELEASE, static_cast<uint32_t>(character));
            wr8(rdram, slot_base(rdram, slot) + 0x04 + static_cast<uint32_t>(square), EMPTY);
        }
    }
    // Unplaced now (the release counts it at +0x5D4); the game's not-in-unit
    // branch (0x801DB0F0..0x801DB1E8).
    auto add32 = [&](uint32_t address, int delta) {
        const uint32_t value = rd32(rdram, address) + static_cast<uint32_t>(delta);
        wr16_(rdram, address, static_cast<uint16_t>(value >> 16));
        wr16_(rdram, address + 2, static_cast<uint16_t>(value));
    };
    add32(state + 0x5D4, -1);
    add32(state + 0x5D8, -1);
    call_game2(rdram, ctx, 0x8016BA6Cu, 0, static_cast<uint32_t>(character));
    wr8(rdram, state + 0x1872 + static_cast<uint32_t>(character) * 2, 0);
    call_game(rdram, ctx, 0x801CE3F8u, 0);
    call_game(rdram, ctx, 0x801CE094u, 0);
    call_game(rdram, ctx, 0x801CDD50u, state + 0x134);
    return {};
}

std::string run_replenish(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t state = rd32(rdram, ORGANIZE_STATE);
    // The list area is shared with other Organize lists; it is put back.
    std::vector<uint8_t> saved(0x200 + 2);
    for (uint32_t i = 0; i < 0x200; i++) {
        saved[i] = rd8(rdram, state + 0x1BEE + i);
    }
    saved[0x200] = rd8(rdram, state + 0x1DEC);
    saved[0x201] = rd8(rdram, state + 0x1DED);
    call_game2(rdram, ctx, 0x80093380u, state + 0x1BEE, 0x1FE);
    call_game(rdram, ctx, 0x801CF818u, 0);
    std::string result;
    if (rd8(rdram, state + 0x1DED) == 0) {
        result = "No soldiers are missing.";
    }
    else if (rd8(rdram, SOLDIER_RESERVE) == 0) {
        result = "No soldiers in the reserve.";
    }
    else {
        call_game(rdram, ctx, 0x801CF1B0u, 0);
    }
    for (uint32_t i = 0; i < 0x200; i++) {
        wr8(rdram, state + 0x1BEE + i, saved[i]);
    }
    wr8(rdram, state + 0x1DEC, saved[0x200]);
    wr8(rdram, state + 0x1DED, saved[0x201]);
    return result;
}

std::string run_game_request(uint8_t* rdram, recomp_context* ctx, const GameRequest& r);

// --- Driving the game's own Organize screens ---------------------------------
//
// Captures 1-8 of 2026-10-30 08:49-08:50 (developer): the Organize update
// func_ovlS_801C214C keeps a state +0x120, step +0x122 and sub-step +0x124, and
// a stack of (state, step, sub-step) at +0xA8 + i*6 with depth +0xA6. A
// sub-screen opens with the current triple pushed, +0x120 = 6, the screen id in
// +0x1BD4, step and sub-step 0 (0x801C3408..0x801C35F8). Change Class is screen
// 20 (func_ovlS_801E13B0): steps 0..6 load and zoom into the wardrobe, step 7
// sub-step 0 is the class list (cursor +0x5EA into the list +0x1C0E, count
// +0x1BFC; func_ovlS_801DC760 reads the pressed word 0x800E8100: A 0x8000
// confirms, B 0x4000 backs out), and sub-steps 6..0xC are the change with the
// clothes flying (captures 2-6). State 999 tears Organize down
// (func_ovlS_801C1EC8: commit, free) and returns to the previous screen.

enum class Drive { None, Open, Pick, Animating, Leaving };
Drive g_drive = Drive::None;
int g_drive_character = 0;
int g_drive_class = 0;
int g_drive_frames = 0;
bool g_drive_started = false;     // the change's sub-steps have begun
std::atomic<int> g_press_a{0};    // input polls left with A held
std::atomic<int> g_press_b{0};
bool g_exit_pending = false;

uint16_t s16(const uint8_t* rdram, uint32_t state, uint32_t offset) {
    return rd16_(rdram, state + offset);
}

void drive_end(const char* why) {
    std::fprintf(stderr, "[organize] change class through the game's screen: %s\n", why);
    g_drive = Drive::None;
}

void drive_tick(uint8_t* rdram) {
    if (!working_copy_open(rdram) || !recomp::overlays::is_function_bank_loaded(BANK_S_ROM)) {
        g_drive = Drive::None;
        g_exit_pending = false;
        return;
    }
    const uint32_t state = rd32(rdram, ORGANIZE_STATE);
    if (g_exit_pending && g_drive == Drive::None) {
        // The game's own exit: the update tears down and returns to the
        // previous screen at state 999.
        g_exit_pending = false;
        wr16_(rdram, state + 0x120, 0x3E7);
        wr16_(rdram, state + 0x122, 0);
        wr16_(rdram, state + 0x124, 0);
        std::fprintf(stderr, "[organize] leaving the game's Organize (state 999)\n");
        return;
    }
    if (g_drive == Drive::None) {
        return;
    }
    g_drive_frames++;
    const uint16_t now = s16(rdram, state, 0x120);
    const uint16_t step = s16(rdram, state, 0x122);
    const uint16_t sub = s16(rdram, state, 0x124);
    const bool class_screen = now == 6 && rd32(rdram, state + 0x1BD4) == 20;
    switch (g_drive) {
        case Drive::Open: {
            if (now == 6) {
                drive_end("another of the game's screens is open");
                return;
            }
            const uint16_t depth = s16(rdram, state, 0xA6);
            const uint32_t slot = state + 0xA8 + depth * 6u;
            wr16_(rdram, slot, now);
            wr16_(rdram, slot + 2, step);
            wr16_(rdram, slot + 4, sub);
            wr16_(rdram, state + 0xA6, static_cast<uint16_t>(depth + 1));
            wr8(rdram, state + 0x18B, static_cast<uint8_t>(g_drive_character));
            wr16_(rdram, state + 0x1BD4, 0);
            wr16_(rdram, state + 0x1BD6, 20);
            wr16_(rdram, state + 0x120, 6);
            wr16_(rdram, state + 0x122, 0);
            wr16_(rdram, state + 0x124, 0);
            g_drive = Drive::Pick;
            g_drive_frames = 0;
            return;
        }
        case Drive::Pick: {
            if (class_screen && step == 7 && sub == 0 && g_press_a.load() == 0) {
                const int count = s16(rdram, state, 0x1BFC);
                int index = -1;
                for (int i = 0; i < count && i < 64; i++) {
                    if ((s16(rdram, state, 0x1C0E + static_cast<uint32_t>(i) * 2) & 0xFF) == g_drive_class) {
                        index = i;
                        break;
                    }
                }
                if (index < 0) {
                    g_press_b.store(3);
                    g_drive = Drive::Leaving;
                    g_drive_frames = 0;
                    std::fprintf(stderr, "[organize] class %d is not in the game's list\n", g_drive_class);
                    return;
                }
                wr16_(rdram, state + 0x5EA, static_cast<uint16_t>(index));
                g_press_a.store(3);
                g_drive = Drive::Animating;
                g_drive_frames = 0;
                g_drive_started = false;
                return;
            }
            if (g_drive_frames > 600) {
                drive_end("the game's Change Class screen did not reach its list");
            }
            return;
        }
        case Drive::Animating: {
            if (!class_screen) {
                drive_end("done (the screen closed)");
                return;
            }
            if (step == 7 && sub != 0) {
                g_drive_started = true;
            }
            // Back at the list after the change (or the game refused it): leave
            // the screen with B, as the player would.
            if ((g_drive_started && step == 7 && sub == 0) || g_drive_frames > 1800) {
                g_press_b.store(3);
                g_drive = Drive::Leaving;
                g_drive_frames = 0;
            }
            return;
        }
        case Drive::Leaving: {
            if (!class_screen) {
                drive_end("done");
                return;
            }
            if (g_drive_frames % 90 == 89 && g_press_b.load() == 0) {
                g_press_b.store(3);
            }
            if (g_drive_frames > 600) {
                drive_end("the game's Change Class screen did not close");
            }
            return;
        }
        case Drive::None:
            return;
    }
}

std::string run_exchange(uint8_t* rdram, recomp_context* ctx, const GameRequest& r) {
    Unit a;
    Unit b;
    if (!read_unit(rdram, r.slot, a) || !read_unit(rdram, r.slot_b, b) || r.slot == r.slot_b) {
        return "Pick squares in two units.";
    }
    const int first = a.squares[r.square];
    const int second = b.squares[r.square_b];
    if (first == EMPTY) {
        return "Pick a character first.";
    }
    if (r.square == a.leader_square || (second != EMPTY && r.square_b == b.leader_square)) {
        return "Leaders stay with their unit. Change the leader first.";
    }
    if (first >= SOLDIER_REF || (second != EMPTY && second >= SOLDIER_REF)) {
        return "Soldiers stay with their unit.";
    }
    if (blocked_large(rdram, first) || (second != EMPTY && blocked_large(rdram, second))) {
        return "Large characters are moved in the game's menu while LARGE UNITS is VANILLA.";
    }
    int members_b = 0;
    for (int sq : b.squares) {
        members_b += sq != EMPTY ? 1 : 0;
    }
    if (second == EMPTY && members_b >= MAX_MEMBERS) {
        return "A unit holds at most five.";
    }
    // Out of both units, then each placed in the other's square.
    call_game(rdram, ctx, FUNC_RELEASE, static_cast<uint32_t>(first));
    wr8(rdram, slot_base(rdram, r.slot) + 0x04 + static_cast<uint32_t>(r.square), EMPTY);
    if (second != EMPTY) {
        call_game(rdram, ctx, FUNC_RELEASE, static_cast<uint32_t>(second));
        wr8(rdram, slot_base(rdram, r.slot_b) + 0x04 + static_cast<uint32_t>(r.square_b), EMPTY);
    }
    std::string result = run_game_request(rdram, ctx, GameRequest{GameKind::SetMember, r.slot_b, r.square_b, first});
    if (result.empty() && second != EMPTY) {
        result = run_game_request(rdram, ctx, GameRequest{GameKind::SetMember, r.slot, r.square, second});
    }
    return result;
}

std::string run_game_request(uint8_t* rdram, recomp_context* ctx, const GameRequest& r) {
    if (r.kind == GameKind::Discharge) {
        return run_discharge(rdram, ctx, r.character);
    }
    if (r.kind == GameKind::Replenish) {
        return run_replenish(rdram, ctx);
    }
    if (r.kind == GameKind::Exchange) {
        return run_exchange(rdram, ctx, r);
    }
    if (r.kind == GameKind::ClassHolders) {
        return run_class_holders(rdram, ctx, r.character);
    }
    if (r.kind == GameKind::ClassList) {
        return run_class_list(rdram, ctx, r.character);
    }
    if (r.kind == GameKind::ChangeClass) {
        const std::string result = run_change_class(rdram, ctx, r.character, r.square);
        run_class_list(rdram, ctx, r.character);
        return result;
    }
    if (r.kind == GameKind::NewUnit) {
        if (r.character <= 0 || r.character >= SOLDIER_REF || in_any_unit(rdram, r.character)) {
            return "Pick an unplaced character.";
        }
        if (!can_lead(rdram, r.character)) {
            return "That class cannot lead a unit.";
        }
        int used = 0;
        for (int slot = 0; slot < UNIT_SLOTS; slot++) {
            Unit unit;
            used += read_unit(rdram, slot, unit) ? 1 : 0;
        }
        if (used >= UNIT_SLOTS) {
            return "No room for another unit.";
        }
        // Slot 30 is the game's scratch unit: its New Unit screen fills it and
        // func_ovlS_801CAB88(30) takes a free slot, a unit number and a record,
        // clears strategy and items, copies the 54 bytes and counts the unit.
        const uint32_t scratch = slot_base(rdram, SCRATCH_SLOT);
        for (uint32_t i = 0; i < WORKING_STRIDE; i++) {
            wr8(rdram, scratch + i, 0);
        }
        wr8(rdram, scratch + 0x02, EMPTY);
        wr8(rdram, scratch + 0x03, static_cast<uint8_t>(r.square));
        for (int sq = 0; sq < SQUARES; sq++) {
            wr8(rdram, scratch + 0x04 + static_cast<uint32_t>(sq), sq == r.square ? static_cast<uint8_t>(r.character) : EMPTY);
        }
        wr8(rdram, scratch + 0x0D, EMPTY);
        call_game(rdram, ctx, FUNC_NEW_UNIT, SCRATCH_SLOT);
        return {};
    }
    Unit unit;
    if (!read_unit(rdram, r.slot, unit)) {
        return "That unit is gone.";
    }
    if (r.kind == GameKind::SetMember) {
        if (r.square < 0 || r.square >= SQUARES || unit.squares[r.square] != EMPTY) {
            return "Pick an empty square.";
        }
        int members = 0;
        for (int sq : unit.squares) {
            members += sq != EMPTY ? 1 : 0;
        }
        if (members >= MAX_MEMBERS) {
            return "A unit holds at most five.";
        }
        if (r.character == 0) {
            if (rd8(rdram, SOLDIER_RESERVE) == 0) {
                return "No soldiers in the reserve.";
            }
        }
        else if (r.character >= SOLDIER_REF || rd8(rdram, CHAR_TABLE + static_cast<uint32_t>(r.character) * CHAR_STRIDE + 0x11) == 0 ||
                 in_any_unit(rdram, r.character)) {
            return "Pick an unplaced character.";
        }
        else if (blocked_large(rdram, r.character)) {
            return "Large characters are added in the game's menu while LARGE UNITS is VANILLA.";
        }
        ctx->r5 = static_cast<gpr>(r.square);
        ctx->r6 = static_cast<gpr>(r.character);
        call_game(rdram, ctx, FUNC_SET_MEMBER, static_cast<uint32_t>(r.slot));
        return {};
    }
    if (r.kind == GameKind::Disband) {
        call_game(rdram, ctx, FUNC_DISBAND, static_cast<uint32_t>(r.slot));
        return {};
    }
    if (r.square < 0 || r.square >= SQUARES || unit.squares[r.square] == EMPTY) {
        return "That square is empty.";
    }
    // The member loop of the game's disband: release, then empty the square.
    call_game(rdram, ctx, FUNC_RELEASE, static_cast<uint32_t>(unit.squares[r.square]));
    wr8(rdram, slot_base(rdram, r.slot) + 0x04 + static_cast<uint32_t>(r.square), EMPTY);
    return {};
}

void push(Request r) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_queue.push_back(r);
    g_result.clear();
}

}  // namespace

bool working_copy_open(const uint8_t* rdram) {
    if (rdram == nullptr) {
        return false;
    }
    const uint32_t state = rd32(rdram, ORGANIZE_STATE);
    return state >= 0x80000000u && state < 0x80800000u;
}

bool read_unit(const uint8_t* rdram, int slot, Unit& out) {
    if (!working_copy_open(rdram) || slot < 0 || slot >= UNIT_SLOTS) {
        return false;
    }
    const uint32_t base = slot_base(rdram, slot);
    const int leader = rd8(rdram, base + 0x03);
    if (leader == EMPTY) {
        return false;
    }
    out.slot = slot;
    out.number = rd8(rdram, base + 0x00);
    out.leader_square = leader;
    for (int sq = 0; sq < SQUARES; sq++) {
        out.squares[sq] = rd8(rdram, base + 0x04 + static_cast<uint32_t>(sq));
    }
    out.strategy = rd8(rdram, base + 0x1E);
    out.soldier_exp = rd8(rdram, base + 0x1F);
    for (int i = 0; i < CARRY_SLOTS; i++) {
        out.items[i] = (rd8(rdram, base + 0x22 + static_cast<uint32_t>(i) * 2) << 8) |
                       rd8(rdram, base + 0x23 + static_cast<uint32_t>(i) * 2);
    }
    return true;
}

void move(int slot, int from_square, int to_square) {
    push({Kind::Move, slot, from_square, to_square});
}

void remove(int slot, int square) {
    // The leader's square disbands the unit, as the developer asked; anyone
    // else is released back to the unplaced list (soldiers to the reserve).
    const uint8_t* rdram = ultramodern::get_rdram_base();
    Unit unit;
    const bool leader = read_unit(rdram, slot, unit) && unit.leader_square == square;
    std::lock_guard<std::mutex> lock(g_mutex);
    g_game_queue.push_back({leader ? GameKind::Disband : GameKind::Release, slot, square, 0});
    g_result.clear();
}

void new_unit(int character, int square) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_game_queue.push_back({GameKind::NewUnit, -1, square, character});
    g_result.clear();
}

void add(int slot, int square, int character) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_game_queue.push_back({GameKind::SetMember, slot, square, character});
    g_result.clear();
}

int soldier_reserve(const uint8_t* rdram) {
    return rdram != nullptr ? rd8(rdram, SOLDIER_RESERVE) : 0;
}

void make_leader(int slot, int square) {
    push({Kind::Leader, slot, square, 0});
}

void set_strategy(int slot, int strategy) {
    push({Kind::Strategy, slot, strategy, 0});
}

void carry(int slot, int item) {
    push({Kind::Carry, slot, item, 0});
}

void drop(int slot, int index) {
    push({Kind::Drop, slot, index, 0});
}

bool read_stock(const uint8_t* rdram, int index, Stock& out) {
    if (rdram == nullptr || index < 0 || index >= STOCK_RECORDS) {
        return false;
    }
    const uint32_t record = STOCK_TABLE + static_cast<uint32_t>(index) * 4;
    out.item = (rd8(rdram, record) << 8) | rd8(rdram, record + 1);
    out.in_use = rd8(rdram, record + 2);
    out.owned = rd8(rdram, record + 3);
    return out.item != 0;
}

bool carriable(const uint8_t* rdram, int item) {
    if (rdram == nullptr || item <= 0 || item >= CONSUMABLE_COUNT) {
        return false;
    }
    return rd8(rdram, CONSUMABLE_TABLE + static_cast<uint32_t>(item) * CONSUMABLE_STRIDE + 4) < 4;
}

std::string item_name(const uint8_t* rdram, int item) {
    if (rdram == nullptr || item <= 0 || item >= CONSUMABLE_COUNT) {
        return "?";
    }
    const uint32_t pointer = rd32(rdram, CONSUMABLE_TABLE + static_cast<uint32_t>(item) * CONSUMABLE_STRIDE);
    std::string name;
    if (pointer >= 0x80000000u && pointer < 0x80800000u) {
        // Two-line names are wrapped as 0x0E line 0x10 'c' line 0x0F
        // ("Altar of" / "Resurrection"); 0x81 is a space in some names.
        for (uint32_t i = 0; i < 32; i++) {
            const uint8_t c = rd8(rdram, pointer + i);
            if (c == 0) {
                break;
            }
            if (c == 0x0E || c == 0x0F) {
                continue;
            }
            if (c == 0x10) {
                name += ' ';
                i++;  // the 'c' after the break
                continue;
            }
            name += c == 0x81 ? ' ' : (c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?');
        }
    }
    return name.empty() ? "Item " + std::to_string(item) : name;
}

int carry_capacity(const uint8_t* rdram, int slot) {
    Unit unit;
    if (!read_unit(rdram, slot, unit)) {
        return 0;
    }
    int total = 0;
    for (int ref : unit.squares) {
        if (ref == EMPTY) {
            continue;
        }
        uint32_t cls = 1;
        uint32_t key = 1;
        if (ref >= 1 && ref < SOLDIER_REF) {
            const uint32_t record = CHAR_TABLE + static_cast<uint32_t>(ref) * CHAR_STRIDE;
            cls = rd8(rdram, record + 0x11);
            key = rd8(rdram, record + 0x12);
        }
        const uint32_t source = rd8(rdram, class_table() + cls * CLASS_STRIDE + 0x45) == key ? cls : key;
        total += rd8(rdram, class_table() + source * CLASS_STRIDE + 0x47);
    }
    return total;
}

const char* strategy_name(int strategy) {
    static const char* const kNames[STRATEGIES] = {"Autonomous", "Attack Strongest", "Attack Leader", "Attack Weakest"};
    return strategy >= 0 && strategy < STRATEGIES ? kNames[strategy] : "?";
}

bool can_lead(const uint8_t* rdram, int character) {
    if (rdram == nullptr || character <= 0 || character >= SOLDIER_REF) {
        return false;
    }
    const uint32_t record = CHAR_TABLE + static_cast<uint32_t>(character) * CHAR_STRIDE;
    const uint32_t cls = rd8(rdram, record + 0x11);
    const uint32_t key = rd8(rdram, record + 0x12);
    const uint32_t source = rd8(rdram, class_table() + cls * CLASS_STRIDE + 0x45) == key ? cls : key;
    return rd8(rdram, class_table() + source * CLASS_STRIDE + 0x06) != 0;
}

namespace {

uint32_t gear_address(int character, int slot) {
    return CHAR_TABLE + static_cast<uint32_t>(character) * CHAR_STRIDE + 0x2A + static_cast<uint32_t>(slot) * 2;
}

// The class whose table row a character's basic gear and slot locks come from
// (the getters func_8016DFA8.. and func_8016D2C8: class +0x45 against the
// character's key byte +0x12).
uint32_t gear_class_row(const uint8_t* rdram, int character) {
    const uint32_t record = CHAR_TABLE + static_cast<uint32_t>(character) * CHAR_STRIDE;
    const uint32_t cls = rd8(rdram, record + 0x11);
    const uint32_t key = rd8(rdram, record + 0x12);
    const uint32_t source = rd8(rdram, class_table() + cls * CLASS_STRIDE + 0x45) == key ? cls : key;
    return class_table() + source * CLASS_STRIDE;
}

// The owned record that holds `item`, or the first empty one when `claim`.
int owned_index(const uint8_t* rdram, int item, bool claim) {
    int empty = -1;
    for (int i = 0; i < EQUIP_RECORDS; i++) {
        const int id = rd16(rdram, OWNED_EQUIPMENT + static_cast<uint32_t>(i) * 4);
        if (id == item) {
            return i;
        }
        if (id == 0 && empty < 0) {
            empty = i;
        }
    }
    return claim ? empty : -1;
}

void adjust_in_use(uint8_t* rdram, int item, int delta) {
    const int index = owned_index(rdram, item, false);
    if (index < 0) {
        return;
    }
    const uint32_t base = OWNED_EQUIPMENT + static_cast<uint32_t>(index) * 4;
    const int value = rd8(rdram, base + 2) + delta;
    wr8(rdram, base + 2, static_cast<uint8_t>(value < 0 ? 0 : value));
}

std::string equip_one(uint8_t* rdram, int character, int slot, int item) {
    const int basic = basic_item(rdram, character, slot);
    const int current = gear_item(rdram, character, slot);
    const int target = item == 0 ? basic : item;
    if (gear_slot_locked(rdram, character, slot)) {
        return "This class cannot change that equipment.";
    }
    if (target == current) {
        return {};
    }
    if (target != basic) {
        if (!equippable(rdram, target) || equipment_category(rdram, target) != equipment_category(rdram, current)) {
            return "That item does not fit this slot.";
        }
        const int index = owned_index(rdram, target, false);
        Owned owned;
        if (index < 0 || !read_owned(rdram, index, owned) || owned.in_use >= owned.owned) {
            return "None of that item is free.";
        }
    }
    // The game's order (func_ovlS_801E4C90 at 0x801E55E8..0x801E5650): the old
    // item's in-use count down unless it was the basic one, the slot written
    // (0 for the basic item), the new item's count up unless it is basic.
    if (current != basic) {
        adjust_in_use(rdram, current, -1);
    }
    wr16(rdram, gear_address(character, slot), static_cast<uint16_t>(target == basic ? 0 : target));
    if (target != basic) {
        adjust_in_use(rdram, target, +1);
    }
    return {};
}

std::string apply_items(uint8_t* rdram, const Request& r) {
    if (!working_copy_open(rdram)) {
        return "Open the game's Organize screen first.";
    }
    switch (r.kind) {
        case Kind::Equip:
            if (r.slot <= 0 || r.slot >= SOLDIER_REF || r.a < 0 || r.a >= GEAR_SLOTS) {
                return "Pick a character and a slot.";
            }
            return equip_one(rdram, r.slot, r.a, r.b);
        case Kind::Basic:
            for (int slot = 0; slot < GEAR_SLOTS; slot++) {
                if (!gear_slot_locked(rdram, r.slot, slot)) {
                    const std::string result = equip_one(rdram, r.slot, slot, 0);
                    if (!result.empty()) {
                        return result;
                    }
                }
            }
            return {};
        case Kind::Sell: {
            const bool consumable = r.slot != 0;
            const uint32_t base = (consumable ? STOCK_TABLE : OWNED_EQUIPMENT) + static_cast<uint32_t>(r.a) * 4;
            const int item = rd16(rdram, base);
            const int in_use = rd8(rdram, base + 2);
            const int owned = rd8(rdram, base + 3);
            if (item == 0 || owned - in_use <= 0) {
                return "None of that item is free to sell.";
            }
            const int price = consumable ? consumable_price(rdram, item) : equipment_price(rdram, item);
            if (price <= 0 || (consumable && consumable_category(rdram, item) >= 3)) {
                return "That item cannot be sold.";
            }
            wr8(rdram, base + 3, static_cast<uint8_t>(owned - 1));
            if (owned - 1 == 0 && in_use == 0) {
                wr32(rdram, base, 0);  // the record is empty again
            }
            wr32(rdram, GOTH_WORD, rd32(rdram, GOTH_WORD) + static_cast<uint32_t>(price / 4));
            return {};
        }
        case Kind::Buy: {
            const int cls = r.slot;
            const int price = basic_set_price(rdram, cls);
            if (price <= 0) {
                return "That class has no Basic Equipment to buy.";
            }
            if (rd32(rdram, GOTH_WORD) < static_cast<uint32_t>(price)) {
                return "Not enough Goth.";
            }
            for (int slot = 0; slot < GEAR_SLOTS; slot++) {
                const int item = basic_set_item(rdram, cls, slot);
                if (item == 0) {
                    continue;
                }
                const int index = owned_index(rdram, item, true);
                if (index < 0) {
                    return "The equipment list is full.";
                }
                const uint32_t base = OWNED_EQUIPMENT + static_cast<uint32_t>(index) * 4;
                if (rd16(rdram, base) == 0) {
                    wr32(rdram, base, static_cast<uint32_t>(item) << 16);
                }
                const int owned = rd8(rdram, base + 3);
                if (owned >= OWNED_MAX) {
                    return "You cannot hold more of that item.";
                }
            }
            for (int slot = 0; slot < GEAR_SLOTS; slot++) {
                const int item = basic_set_item(rdram, cls, slot);
                if (item != 0) {
                    const uint32_t base = OWNED_EQUIPMENT + static_cast<uint32_t>(owned_index(rdram, item, false)) * 4;
                    wr8(rdram, base + 3, static_cast<uint8_t>(rd8(rdram, base + 3) + 1));
                }
            }
            wr32(rdram, GOTH_WORD, rd32(rdram, GOTH_WORD) - static_cast<uint32_t>(price));
            return {};
        }
        case Kind::Rename: {
            const int character = r.slot;
            if (character <= 0 || character >= SOLDIER_REF || rd8(rdram, CHAR_TABLE + static_cast<uint32_t>(character) * CHAR_STRIDE + 0x11) == 0) {
                return "Pick a character.";
            }
            if (named_character(rdram, character)) {
                return "This character keeps their name.";
            }
            std::string name;
            for (const char c : r.text) {
                if (name.size() < static_cast<size_t>(NAME_MAX) && c >= 0x20 && c < 0x7F) {
                    name += c;
                }
            }
            while (!name.empty() && name.back() == ' ') {
                name.pop_back();
            }
            if (name.empty()) {
                return "Type a name first.";
            }
            const uint32_t base = CHAR_TABLE + static_cast<uint32_t>(character) * CHAR_STRIDE;
            for (int i = 0; i <= NAME_MAX; i++) {
                wr8(rdram, base + static_cast<uint32_t>(i), i < static_cast<int>(name.size()) ? static_cast<uint8_t>(name[static_cast<size_t>(i)]) : 0);
            }
            return {};
        }
        case Kind::UnequipAll: {
            const bool consumable = r.slot != 0;
            const int item = r.a;
            if (!consumable) {
                for (int character = 1; character < SOLDIER_REF; character++) {
                    const uint32_t base = CHAR_TABLE + static_cast<uint32_t>(character) * CHAR_STRIDE;
                    if (rd8(rdram, base + 0x11) == 0) {
                        continue;
                    }
                    for (int slot = 0; slot < GEAR_SLOTS; slot++) {
                        if (rd16(rdram, base + 0x2A + static_cast<uint32_t>(slot) * 2) == item && !gear_slot_locked(rdram, character, slot)) {
                            equip_one(rdram, character, slot, 0);
                        }
                    }
                }
                return {};
            }
            for (int slot = 0; slot < UNIT_SLOTS; slot++) {
                for (int guard = 0; guard < CARRY_SLOTS; guard++) {
                    Unit unit;
                    if (!read_unit(rdram, slot, unit)) {
                        break;
                    }
                    int at = -1;
                    for (int i = 0; i < CARRY_SLOTS && at < 0; i++) {
                        at = unit.items[i] == item ? i : -1;
                    }
                    if (at < 0) {
                        break;
                    }
                    const std::string result = apply(rdram, Request{Kind::Drop, slot, at, 0});
                    if (!result.empty()) {
                        return result;
                    }
                }
            }
            return {};
        }
        default:
            return {};
    }
}

}  // namespace

bool read_owned(const uint8_t* rdram, int index, Owned& out) {
    if (rdram == nullptr || index < 0 || index >= EQUIP_RECORDS) {
        return false;
    }
    const uint32_t base = OWNED_EQUIPMENT + static_cast<uint32_t>(index) * 4;
    out.item = rd16(rdram, base);
    out.in_use = rd8(rdram, base + 2);
    out.owned = rd8(rdram, base + 3);
    return out.item != 0;
}

int basic_item(const uint8_t* rdram, int character, int slot) {
    return rd16(rdram, gear_class_row(rdram, character) + 0x2E + static_cast<uint32_t>(slot) * 2);
}

int gear_item(const uint8_t* rdram, int character, int slot) {
    const int item = rd16(rdram, gear_address(character, slot));
    return item != 0 ? item : basic_item(rdram, character, slot);
}

bool gear_slot_locked(const uint8_t* rdram, int character, int slot) {
    return ((rd8(rdram, gear_class_row(rdram, character) + 0x36) >> slot) & 1) != 0;
}

int equipment_category(const uint8_t* rdram, int item) {
    return item > 0 && item < EQUIP_RECORDS ? rd8(rdram, ITEM_TABLE + static_cast<uint32_t>(item) * 0x20 + 4) : 0;
}

int equipment_price(const uint8_t* rdram, int item) {
    return item > 0 && item < EQUIP_RECORDS ? rd16(rdram, ITEM_TABLE + static_cast<uint32_t>(item) * 0x20 + 8) : 0;
}

bool equippable(const uint8_t* rdram, int item) {
    return item > 0 && item < EQUIP_RECORDS && rd8(rdram, ITEM_TABLE + static_cast<uint32_t>(item) * 0x20 + 6) != 0;
}

std::string equipment_name(const uint8_t* rdram, int item) {
    if (rdram == nullptr || item <= 0 || item >= EQUIP_RECORDS) {
        return "-";
    }
    const uint32_t pointer = rd32(rdram, ITEM_TABLE + static_cast<uint32_t>(item) * 0x20);
    std::string name;
    if (pointer >= 0x80000000u && pointer < 0x80800000u) {
        for (uint32_t i = 0; i < 32; i++) {
            const uint8_t c = rd8(rdram, pointer + i);
            if (c == 0) {
                break;
            }
            if (c == 0x0E || c == 0x0F) {
                continue;
            }
            if (c == 0x10) {
                name += ' ';
                i++;
                continue;
            }
            name += c == 0x81 ? ' ' : (c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?');
        }
    }
    return name.empty() ? "Item " + std::to_string(item) : name;
}

// Category names from the items in each (tools: the item table dump).
const char* category_name(int category) {
    static const char* const kNames[] = {
        "-",          "Sword",        "Great Sword", "Rapier",      "Axe",          "Hammer",  "-",
        "Spear",      "Whip",         "Claw",        "Bow",         "Mace",         "Staff",   "Doll",
        "Small Shield", "Shield",     "Light Armor", "Mail",        "Heavy Armor",  "Robe",    "Clothing",
        "Helm",       "Hat",          "Book",        "Fan",         "Accessory",
    };
    return category >= 0 && category < 26 ? kNames[category] : "-";
}

int consumable_price(const uint8_t* rdram, int item) {
    if (item <= 0 || item >= CONSUMABLE_COUNT) {
        return 0;
    }
    return static_cast<int>(rd32(rdram, CONSUMABLE_TABLE + static_cast<uint32_t>(item) * CONSUMABLE_STRIDE + 4) & 0x00FFFFFFu);
}

int consumable_category(const uint8_t* rdram, int item) {
    return item > 0 && item < CONSUMABLE_COUNT
               ? rd8(rdram, CONSUMABLE_TABLE + static_cast<uint32_t>(item) * CONSUMABLE_STRIDE + 4)
               : 0;
}

int basic_set_item(const uint8_t* rdram, int cls, int slot) {
    if (cls <= 0 || cls >= 165) {
        return 0;
    }
    return rd16(rdram, class_table() + static_cast<uint32_t>(cls) * CLASS_STRIDE + 0x2E + static_cast<uint32_t>(slot) * 2);
}

int basic_set_price(const uint8_t* rdram, int cls) {
    int total = 0;
    for (int slot = 0; slot < GEAR_SLOTS; slot++) {
        total += equipment_price(rdram, basic_set_item(rdram, cls, slot));
    }
    return total;
}

void equip(int character, int slot, int item) {
    push({Kind::Equip, character, slot, item});
}

void basic_equipment(int character) {
    push({Kind::Basic, character, 0, 0});
}

void sell(bool consumable, int index) {
    push({Kind::Sell, consumable ? 1 : 0, index, 0});
}

void buy_basic(int cls) {
    push({Kind::Buy, cls, 0, 0});
}

void request_class_list(int character) {
    if (character <= 0 || character >= SOLDIER_REF) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const GameRequest& r : g_game_queue) {
        if (r.kind == GameKind::ClassList && r.character == character) {
            return;
        }
    }
    g_game_queue.push_back({GameKind::ClassList, -1, 0, character});
}

bool in_game_class_list(int cls) {
    std::lock_guard<std::mutex> lock(g_class_mutex);
    return std::any_of(g_class_list.begin(), g_class_list.end(),
                       [&](const ClassCandidate& c) { return c.cls == cls && c.in_game_list; });
}

bool class_candidates(int character, std::vector<ClassCandidate>& out) {
    std::lock_guard<std::mutex> lock(g_class_mutex);
    if (g_class_character != character) {
        return false;
    }
    out = g_class_list;
    return true;
}

// The game's failure messages (func_ovlS_801E13B0, codes at state +0x5ED).
const char* class_reason(int reason) {
    switch (reason) {
        case 1: return "That class is outside this character's class line.";
        case 2: return "This class needs a special item or event.";
        case 3: return "Basic Equipment for this class is unavailable.";
        case 4: return "Not enough Goth for the Basic Equipment.";
        case 5: return "Buying the Basic Equipment needs a shop.";
        default: return "This character's stats are too low.";
    }
}

void change_class_shown(int character, int target) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_drive != Drive::None) {
        return;
    }
    g_drive_character = character;
    g_drive_class = target;
    g_drive_frames = 0;
    g_drive = Drive::Open;
    g_result.clear();
}

bool driving() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_drive != Drive::None;
}

void exit_game_screen() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_exit_pending = true;
}

uint16_t injected_buttons() {
    uint16_t out = 0;
    int a = g_press_a.load();
    if (a > 0) {
        g_press_a.store(a - 1);
        out |= 0x8000u;
    }
    int b = g_press_b.load();
    if (b > 0) {
        g_press_b.store(b - 1);
        out |= 0x4000u;
    }
    return out;
}

void change_class(int character, int target) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_game_queue.push_back({GameKind::ChangeClass, -1, target, character});
    g_result.clear();
}

bool named_character(const uint8_t* rdram, int character) {
    if (rdram == nullptr || character <= 0 || character >= SOLDIER_REF) {
        return false;
    }
    // A standard new class (class_change.hpp) counts as standard, as the game's
    // own tests do with tools/relocate_tables.py.
    const uint16_t cls = rd8(rdram, CHAR_TABLE + static_cast<uint32_t>(character) * CHAR_STRIDE + 0x11);
    return cls >= 0x51 && !class_change::is_standard(cls);
}

void push_game(const GameRequest& r) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_game_queue.push_back(r);
    g_result.clear();
}

void discharge(int character) {
    push_game({GameKind::Discharge, -1, 0, character});
}

void rename(int character, const std::string& name) {
    Request r{Kind::Rename, character, 0, 0};
    r.text = name;
    push(r);
}

void replenish() {
    push_game({GameKind::Replenish, -1, 0, 0});
}

void exchange(int slot_a, int square_a, int slot_b, int square_b) {
    push_game({GameKind::Exchange, slot_a, square_a, 0, slot_b, square_b});
}

void unequip_all(bool consumable, int item) {
    push({Kind::UnequipAll, consumable ? 1 : 0, item, 0});
}

void request_class_holders(int cls) {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const GameRequest& r : g_game_queue) {
        if (r.kind == GameKind::ClassHolders && r.character == cls) {
            return;
        }
    }
    g_game_queue.push_back({GameKind::ClassHolders, -1, 0, cls});
}

bool class_holders(int cls, std::vector<ClassHolder>& out) {
    std::lock_guard<std::mutex> lock(g_holder_mutex);
    if (g_holder_class != cls) {
        return false;
    }
    out = g_holders;
    return true;
}

// func_ovlS_801D736C sets one bit per class; the masks at 0x801EF390 are
// 0x01, 0x02 ... 0x80 by (class & 7).
bool class_known(const uint8_t* rdram, int cls) {
    if (rdram == nullptr || cls <= 0 || cls >= 0x100) {
        return false;
    }
    return (rd8(rdram, 0x80196A96u + static_cast<uint32_t>(cls >> 3)) & (1u << (cls & 7))) != 0;
}

void apply_pending(uint8_t* rdram) {
    std::deque<Request> work;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        work.swap(g_queue);
    }
    if (work.empty() || rdram == nullptr) {
        return;
    }
    // Park the game threads so the Organize screen never sees a half-applied
    // edit (the same barrier the overlay's pause and checkpoints use).
    ultramodern::checkpoint_pause_begin();
    std::string result;
    for (const Request& r : work) {
        result = apply(rdram, r);
        std::fprintf(stderr, "[organize] edit %d slot %d (%d,%d): %s\n", static_cast<int>(r.kind), r.slot, r.a, r.b,
                     result.empty() ? "done" : result.c_str());
        if (!result.empty()) {
            break;
        }
    }
    ultramodern::checkpoint_pause_end();
    std::lock_guard<std::mutex> lock(g_mutex);
    g_result = result.empty() ? "Done." : result;
}

std::string last_result() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_result;
}

}  // namespace ogre::organize_edit

// Once per frame on the frame pump (config/hooks.toml, 0x8008B120): the
// queued edits that call the game's own Organize routines.
extern "C" void ogre_organize_frame(uint8_t* rdram, recomp_context* ctx) {
    using namespace ogre::organize_edit;
    // A save asked for from the port's menu (save_profiles.hpp).
    ogre::save_profiles::frame(rdram, ctx);
    std::deque<GameRequest> work;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        drive_tick(rdram);
        if (g_game_queue.empty()) {
            return;
        }
        work.swap(g_game_queue);
    }
    std::string result;
    if (!working_copy_open(rdram) || !recomp::overlays::is_function_bank_loaded(BANK_S_ROM)) {
        result = "Open the game's Organize screen first.";
    }
    else {
        const recomp_context saved = *ctx;
        for (const GameRequest& r : work) {
            result = run_game_request(rdram, ctx, r);
            std::fprintf(stderr, "[organize] game edit %d slot %d square %d char %d: %s\n", static_cast<int>(r.kind), r.slot,
                         r.square, r.character, result.empty() ? "done" : result.c_str());
            if (!result.empty()) {
                break;
            }
        }
        *ctx = saved;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_result = result.empty() ? "Done." : result;
}

