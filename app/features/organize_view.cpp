// The new Organize screens, phase 2: read-only Formation and Character views
// in RmlUi (docs/anotherworld/design-organize-screen.md). See organize_view.hpp.
//
// Everything here runs on the render thread, inside rml_ui.cpp's draw hook.
// A snapshot of the army is read from guest RDRAM a few times a second and
// handed to RmlUi as the data model "organize"; organize.rml draws it.
//
// Guest data (read from a played save, 2026-10-29):
//   unit record      0x80197210 + u*0x19, player units 0..29
//                      +0x00 unit number, +0x02..+0x06 member refs (the first
//                      is the leader; 0 empty; 100+ a generic soldier),
//                      +0x07..+0x0B each member's square (0..8)
//   character record 0x80193BE0 + n*0x38, n 1..99
//                      +0x00 name, +0x11 class, +0x13 level, +0x16/+0x18 HP
//                      now/max, +0x1B alignment, +0x1C..+0x26 STR VIT INT MEN
//                      AGI DEX (u16)
//   class names      class table 0x80187C34 + c*0x48, +0x00 name pointer
//   goth             word 0x80196A8C
// Not decoded yet: the records behind soldier refs (100+), legions, unit
// strategy and carried items. They show as "Soldier" and "-".

#include "game_addresses.hpp"
#include "organize_view.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

#include <RmlUi/Core.h>

#include "class_rebalance.hpp"
#include "input_map.hpp"
#include "organize_edit.hpp"
#include "options.hpp"
#include "ultramodern/ultramodern.hpp"

namespace ogre::organize_view {
namespace {

constexpr uint32_t UNIT_TABLE = ::ogre::addr::kUnits;
constexpr uint32_t UNIT_STRIDE = 0x19u;
constexpr int PLAYER_UNITS = 30;
constexpr uint32_t CHAR_TABLE = ::ogre::addr::kCharacters;
constexpr uint32_t CHAR_STRIDE = 0x38u;
constexpr int CHAR_FIRST = 1;
constexpr int CHAR_COUNT = 100;
constexpr int SOLDIER_REF = 100;
constexpr uint32_t CLASS_STRIDE = 0x48u;
// The class table the game reads: the moved copy in the content arena (256
// records) when the content database moved it, else the ROM's 165 records.
extern "C" unsigned int ogre_class_table_shift;
uint32_t class_table() {
    return ::ogre::addr::kClasses + (ogre_class_table_shift << 16);
}
constexpr int CLASS_COUNT = 165;
// One past the last class id with a record: 255 with the moved table (new
// classes, 172 and up), else 165.
int class_limit() {
    return ogre_class_table_shift != 0 ? 255 : CLASS_COUNT;
}
constexpr uint32_t GOTH = ::ogre::addr::kGoth;
constexpr uint32_t CHAOS_FRAME = ::ogre::addr::kChaosFrame;  // 0..100, func_8016FDD0 clamps it
constexpr int REFRESH_PRESENTS = 15;

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

uint16_t rd16(const uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>((rd8(rdram, address) << 8) | rd8(rdram, address + 1));
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd16(rdram, address)) << 16) | rd16(rdram, address + 2);
}

Rml::String guest_string(const uint8_t* rdram, uint32_t address, int max) {
    Rml::String out;
    for (int i = 0; i < max; i++) {
        const char c = static_cast<char>(rd8(rdram, address + static_cast<uint32_t>(i)));
        if (c == '\0') {
            break;
        }
        out += (c >= 32 && c < 127) ? c : '?';
    }
    return out;
}

Rml::String class_name(const uint8_t* rdram, int cls) {
    if (cls <= 0 || cls >= class_limit()) {
        return "-";
    }
    const uint32_t pointer = rd32(rdram, class_table() + static_cast<uint32_t>(cls) * CLASS_STRIDE);
    if (pointer < 0x80000000u || pointer >= 0x80800000u) {
        return "Class " + std::to_string(cls);
    }
    return guest_string(rdram, pointer, 24);
}

// A placeholder figure colour per class until the class sprites are extracted:
// one of a fixed palette, picked by class id, so a class keeps its colour
// everywhere.
Rml::String class_colour(int cls) {
    static const char* const palette[] = {
        "#9aa3ad", "#5f86c9", "#b8402e", "#3d8f53", "#7a3fa0", "#b86a3c",
        "#2f6fb3", "#8a7a5a", "#a04a4a", "#6a7a3a", "#d0c8a0", "#4a4a6a",
    };
    return palette[static_cast<unsigned>(cls) % (sizeof(palette) / sizeof(palette[0]))];
}

struct Square {
    Rml::String label;
    Rml::String kind;  // leader, member, large, empty
    Rml::String colour = "transparent";
    bool dark = false;  // the blue squares of the checker
    int index = 0;      // square 0..8
    bool selected = false;
    int class_id = 0;   // for the sprite
    Rml::String sprite;  // "sprite:<class>:<model offset>"
};

// One row of a unit's 3x3 board. Row 0 is drawn at the back. Which edge of the
// game's square numbering faces the enemy is not verified yet.
struct Row {
    Rml::Vector<Square> squares;
};

// One row of attacks for the picked member (front, middle, back).
struct AttackRow {
    Rml::String row;     // "Front", "Middle", "Back"
    Rml::String attack;  // "Slash x2", "Magic x1", "-"
    Rml::String detail;  // what the attack does, shown when the row is open
    int index = 0;
    bool current = false;
    bool open = false;
};

// One Battle Strategy choice.
struct StrategyOption {
    int index = 0;
    Rml::String name;
    bool current = false;
};

// The member picked on a board, for the panel above the commands.
struct Picked {
    bool shown = false;
    bool soldier = false;
    int class_id = 0;
    Rml::String name;
    Rml::String cls;
    int level = 0;
    int hp = 0;
    int hp_max = 0;
    int hp_percent = 0;
    Rml::String stats;  // "STR 60  VIT 58 ..."
    Rml::String row;    // the row it stands in
    Rml::Vector<AttackRow> attacks;
};

// One class the selected character can take, for the Change Class list.
// A carried item (Unit panel) or a consumable in stock (Carry Item picker).
struct ItemEntry {
    int index = 0;  // carried slot, or the item id for a stock entry
    Rml::String name;
    int free = 0;   // stock only: owned minus in use
    int item = 0;   // the item id, for the icon
};

// One gear slot of the selected character (Equipment tab).
struct GearSlot {
    int slot = 0;          // 0..3
    int item = 0;          // the effective item, for the icon
    Rml::String name;
    Rml::String category;
    bool basic = false;    // the class's Basic Equipment
    bool locked = false;   // the class cannot change this slot
    bool open = false;     // its choice list is showing
};

// One item the open slot can take.
struct GearChoice {
    int item = 0;          // 0 = the Basic Equipment
    Rml::String name;
    int free = 0;
    bool current = false;
};

// One owned item (Items tab).
struct OwnedEntry {
    int list = 0;          // 0 equipment, 1 consumables
    int index = 0;         // the owned record
    int item = 0;
    Rml::String name;
    Rml::String category;
    int owned = 0;
    int in_use = 0;
    int sell = 0;          // what one sells for, 0 = cannot be sold
};

// One known class in the Class List, or one character for the picked class.
struct ClassListEntry {
    int id = 0;            // class id, or character record
    int class_id = 0;      // for the sprite
    Rml::String name;
    Rml::String status;
    Rml::String kind;      // "ready", "locked", "current"
    bool selected = false;
};

// One class's Basic Equipment set (Armory tab).
struct ArmorySet {
    int class_id = 0;
    Rml::String name;
    Rml::String items;     // the four items
    int price = 0;
    int members = 0;       // characters of this class
    bool affordable = false;
};

// One bar of the Change Class requirement chart: the character's value
// against the class's minimum (func_ovlS_801D2378's checks).
struct StatNeed {
    Rml::String name;
    int have = 0;
    int need = 0;        // 0 = no requirement
    int fill = 0;        // the bar, percent of the larger of the two
    int mark = 0;        // the requirement's position, percent
    bool shortfall = false;
};

struct ClassOption {
    int index = 0;
    int cost = 0;        // goth for the missing Basic Equipment
    Rml::Vector<StatNeed> needs;
    Rml::String name;
    Rml::String colour = "transparent";
    int class_id = 0;    // for the sprite
    Rml::String kind;    // current (later: the game's own candidates)
    Rml::String status;  // the badge text
    Rml::String reason;  // the prompt line
    bool selected = false;
};

struct Member {
    Rml::String name;
    Rml::String cls;
    int level = 0;
    int hp = 0;
    int hp_max = 0;
    int hp_percent = 0;
    int class_id = 0;  // for the portrait
    bool soldier = false;
};

struct Unit {
    int index = 0;
    Rml::String number;
    Rml::String name;
    Rml::String count;
    Rml::Vector<Row> rows;
    Rml::Vector<Member> members;
    Rml::String strategy;  // Battle Strategy, the game's name
    Rml::Vector<ItemEntry> items;  // carried items
    Rml::String carry;             // "n / capacity"
    bool has_soldiers = false;
    int soldier_exp = 0;           // the unit's pooled soldier experience, 0..100
    Rml::String promotes_to;       // "Fighter" or "Amazon", by the leader
    bool selected = false;
};

struct Character {
    int record = 0;
    Rml::String name;
    Rml::String cls;
    int level = 0;
    Rml::String unit;
    int hp = 0;
    int hp_max = 0;
    int hp_percent = 0;
    int class_id = 0;
    Rml::String colour = "transparent";  // class colour, the face placeholder
    Rml::String initial;                 // first letter of the name
    int alignment = 0;
    int str = 0;
    int vit = 0;
    int intel = 0;
    int men = 0;
    int agi = 0;
    int dex = 0;
    bool selected = false;
};

struct Model {
    Rml::String tab = "formation";
    Rml::String goth;
    int chaos_frame = 0;
    Rml::Vector<Unit> units;
    Rml::Vector<Character> characters;
    int selected_unit = -1;
    int selected_character = 0;
    Unit unit;            // the selected unit, for the information panel
    Character character;  // the selected character, for the detail panel
    Rml::String sort = "level";
    Rml::Vector<ClassOption> classes;  // the selected character's options
    int selected_class = 0;
    ClassOption picked;
    int picked_number = 0;
    int class_count = 0;
    Rml::Vector<Character> unassigned;  // characters in no unit
    Rml::String notice;                  // the last command's answer
    bool editable = false;               // the game's Organize screen is open
    bool carry_open = false;             // the Carry Item picker is showing
    bool strategy_open = false;          // the Battle Strategy picker is showing
    Rml::Vector<GearSlot> gear;          // the selected character's four slots
    Rml::Vector<GearChoice> gear_choices;
    int open_slot = -1;
    Rml::Vector<OwnedEntry> owned_items;
    Rml::String item_filter = "equipment";
    Rml::Vector<ArmorySet> armory;
    Rml::String rename_text;             // the Rename field
    bool discharge_confirm = false;      // Discharge asks once more
    bool class_list_mode = false;        // the Class tab lists classes
    int class_list_pick = 0;
    Rml::Vector<ClassListEntry> known_classes;
    Rml::Vector<ClassListEntry> class_holders;
    Rml::String item_sort = "kind";
    Rml::Vector<StrategyOption> strategies;
    int open_attack = -1;                // the attack row opened in the picked panel
    bool new_unit_mode = false;          // the next Unassigned click leads a new unit
    int soldier_reserve = 0;             // soldiers waiting in the reserve
    Picked picked_member;                // the member picked on a board
    Rml::Vector<ItemEntry> stock;        // consumables with one or more free
    int sel_slot = -1;                   // the picked square, for edits
    int sel_square = -1;
    // The on-screen keyboard for Rename, so a pad can enter a name.
    bool keyboard_open = false;
    bool keyboard_upper = true;
    Rml::Vector<Rml::String> keyboard_keys;
    Rml::String keyboard_help;
};

struct State {
    Model model;
    Rml::DataModelHandle handle;
    Rml::ElementDocument* document = nullptr;
    Rml::Context* context = nullptr;
    std::string path;  // organize.rml, reloaded each time the view opens
    std::atomic<bool> visible{false};  // main thread toggles, render thread reads
    int presents = 0;
    bool want_focus = false;          // focus a control once the lists are built
    std::atomic<int> tab_step{0};     // shoulder-button tab steps from the main thread
    bool auto_opened = false;         // opened over this visit of the game's Organize
    // The pad's B and the on-screen keyboard's pad keys, from the main thread.
    std::atomic<bool> closable{false};    // the keyboard or a picker is open
    std::atomic<int> pad_key{0};          // a PadKey to apply on the render thread
    bool focus_keyboard = false;          // focus the first key once it is laid out
};

enum PadKey { PAD_KEY_NONE = 0, PAD_KEY_BACK, PAD_KEY_DELETE, PAD_KEY_SPACE, PAD_KEY_DONE };

State& state() {
    static State s;
    return s;
}

// How many soldiers the game draws for soldier slot `ref` (100+): the game's
// func_ovlS_801CBCB8. Each slot has a strength, u16 at 0x80190EDC + ref*2,
// against the maximum at 0x80190FA2; one figure per started third, 1..3.
int soldier_figures(const uint8_t* rdram, int ref) {
    const int max = rd16(rdram, 0x80190FA2u);
    const int third = max / 3;
    int value = rd16(rdram, 0x80190EDCu + static_cast<uint32_t>(ref) * 2);
    int count = 0;
    do {
        count++;
        value -= third;
    } while (value > 0 && count < 3 && third > 0);
    return count < 1 ? 1 : count > 3 ? 3 : count;
}

constexpr uint32_t ATTACK_TABLE = ::ogre::addr::kAttacks;  // 0x10 per attack: +0 name, +4 kind, +5 element
constexpr uint32_t ITEM_TABLE = ::ogre::addr::kItems;    // 0x20 per equipment item: +4 category, +5 element

// The class a character's default gear and fallback attack come from: its own
// when the class's key byte (+0x45) matches the character's +0x12, else the
// class that key names (func_8016E250, func_8016E52C).
int gear_class(const uint8_t* rdram, int cls, int key) {
    const uint32_t record = class_table() + static_cast<uint32_t>(cls) * CLASS_STRIDE;
    return rd8(rdram, record + 0x45) == key ? cls : key;
}

// A port of the game's spell pick, func_8016E52C. Class rows hold a generic
// "magic" attack (45..48, element 8) that the game turns into a real spell
// from the caster's gear: the first item of category 0x17 (func_8016FB70),
// else the first of category 1..13 or 0x18 (func_8016FA54), and that item's
// element (+5; 0x10 reads 0x80193C32, func_8016F600) indexes an 8-byte table
// per generic attack at 0x8018B48C. An empty gear slot uses the class's
// default gear (class +0x2E). With no elemental item the class's front-row
// attack is used instead. Character bytes as the Organize screen passes them
// (func_ovlS_8019CB28): class +0x11, key +0x12, variant +0x1A, gear +0x2A..+0x30.
int resolve_attack(const uint8_t* rdram, int ref, int attack) {
    const uint32_t c = CHAR_TABLE + static_cast<uint32_t>(ref) * CHAR_STRIDE;
    const int cls = rd8(rdram, c + 0x11);
    const int key = rd8(rdram, c + 0x12);
    const int variant = rd8(rdram, c + 0x1A);
    const int kind = rd8(rdram, ATTACK_TABLE + static_cast<uint32_t>(attack) * 0x10 + 4);
    if (kind < 3 || kind == 5) {
        return attack;
    }
    const uint32_t defaults = class_table() + static_cast<uint32_t>(gear_class(rdram, cls, key)) * CLASS_STRIDE + 0x2E;
    int gear[4];
    for (int i = 0; i < 4; i++) {
        gear[i] = rd16(rdram, c + 0x2A + static_cast<uint32_t>(i) * 2);
        if (gear[i] == 0) {
            gear[i] = rd16(rdram, defaults + static_cast<uint32_t>(i) * 2);
        }
        if (gear[i] == 0xF3) {
            gear[i] += variant;
        }
    }
    auto category = [&](int item) { return rd8(rdram, ITEM_TABLE + static_cast<uint32_t>(item) * 0x20 + 4); };
    int item = 0;
    for (int i = 0; i < 4 && item == 0; i++) {
        item = category(gear[i]) == 0x17 ? gear[i] : 0;
    }
    for (int i = 0; i < 4 && item == 0; i++) {
        const int cat = category(gear[i]);
        item = (cat != 0 && (cat < 0x0E || cat == 0x18)) ? gear[i] : 0;
    }
    int element = 0;
    if (item != 0) {
        element = rd8(rdram, ITEM_TABLE + static_cast<uint32_t>(item) * 0x20 + 5);
        if (element == 0x10) {
            element = rd8(rdram, 0x80193C32u);
        }
    }
    if (attack >= 45 && attack <= 48) {
        if (item == 0 || element == 0 || element >= 8) {
            const uint32_t own = class_table() + static_cast<uint32_t>(gear_class(rdram, cls, key)) * CLASS_STRIDE;
            const int front = rd8(rdram, own + 0x37);
            return front != 0xFF ? front : rd8(rdram, class_table() + static_cast<uint32_t>(key) * CLASS_STRIDE + 0x37);
        }
        if (element == 7) {
            return item >= 0xF9 && item <= 0xFC ? 99 + (item - 0xF9) : attack;
        }
        return rd8(rdram, 0x8018B48Cu + static_cast<uint32_t>(attack - 45) * 8 + static_cast<uint32_t>(element));
    }
    switch (attack) {
    case 0x33: return 0x37;
    case 0x34: return 0x57;
    case 0x35: return 0x59;
    case 0x36: return 0x5F;
    case 0x91: return rd8(rdram, 0x8018B4ACu + static_cast<uint32_t>(element));
    default: return attack;
    }
}

// What an attack does, from its record (docs/anotherworld/design-class-rebalance.md):
// +0x04 kind, +0x05 element, +0x06 target pattern, +0x07 effect, +0x09
// formula. Target patterns beyond "one" are named by what they reach; their
// exact shapes are not decoded.
Rml::String attack_detail(const uint8_t* rdram, int attack, int count) {
    const uint32_t entry = ATTACK_TABLE + static_cast<uint32_t>(attack) * 0x10;
    const int kind = rd8(rdram, entry + 4);
    const int element = rd8(rdram, entry + 5);
    const int target = rd8(rdram, entry + 6);
    const int effect = rd8(rdram, entry + 7);
    const int formula = rd8(rdram, entry + 9);
    static const char* const kKinds[] = {"Melee", "Ranged", "Attack", "Spell", "Special", "Breath"};
    static const char* const kElements[] = {"Physical", "Wind", "Flame", "Earth", "Water", "Virtue", "Bane",
                                            "All elements", "By equipment", "Wind and Water", "Wind and Flame",
                                            "Flame and Earth", "Earth and Water", "Bane and Wind", "Bane and Flame"};
    Rml::String out = kind >= 0 && kind < 6 ? kKinds[kind] : "Attack";
    out += " | ";
    out += element >= 0 && element < 15 ? kElements[element] : "Special";
    out += " | ";
    out += target == 1 ? "One target" : (target == 2 || target == 5) ? "All targets" : "Several targets";
    out += " | ";
    switch (effect) {
    case 1: out += "Damage"; break;
    case 2: out += "Heals or helps allies"; break;
    case 4: out += "Puts to sleep"; break;
    case 8: out += "Paralyzes"; break;
    case 10: out += "Poisons"; break;
    default: out += "Special effect"; break;
    }
    switch (formula) {
    case 1: out += " | Power from STR"; break;
    case 7: out += " | Power from INT"; break;
    case 8: out += " | Chance to land"; break;
    case 9: out += " | Heal power from MEN"; break;
    default: break;
    }
    out += " | " + std::to_string(count) + (count == 1 ? " use" : " uses") + " per battle";
    return out;
}

// The class's attack for one row (class +0x37 + 2*row: attack id, count; row
// 0 front, 1 middle, 2 back), named from the attack table. For a character
// (ref > 0) a generic spell is resolved from their gear (resolve_attack).
Rml::String attack_text(const uint8_t* rdram, int cls, int row, int ref, Rml::String* detail = nullptr) {
    if (cls <= 0 || cls >= class_limit()) {
        return "-";
    }
    const uint32_t record = class_table() + static_cast<uint32_t>(cls) * CLASS_STRIDE;
    int attack = rd8(rdram, record + 0x37 + static_cast<uint32_t>(row) * 2);
    const int count = rd8(rdram, record + 0x38 + static_cast<uint32_t>(row) * 2);
    if (count == 0 || count > 9) {
        return "-";
    }
    if (ref > 0 && ref < SOLDIER_REF) {
        attack = resolve_attack(rdram, ref, attack);
    }
    if (detail != nullptr) {
        *detail = attack_detail(rdram, attack, count);
    }
    const uint32_t entry = ATTACK_TABLE + static_cast<uint32_t>(attack) * 0x10;
    const uint32_t pointer = rd32(rdram, entry);
    Rml::String name;
    if (pointer >= 0x80000000u && pointer < 0x80800000u) {
        // "Ray of Paralysis" is stored with text codes: 0x0E/0x0F wrap it and
        // 0x10 'c' breaks the line.
        for (uint32_t i = 0; i < 24; i++) {
            const uint8_t ch = rd8(rdram, pointer + i);
            if (ch == 0) {
                break;
            }
            if (ch == 0x10) {
                name += ' ';
                i += rd8(rdram, pointer + i + 1) == 'c' ? 1 : 0;
            }
            else if (ch >= 0x20 && ch < 0x7F) {
                name += static_cast<char>(ch);
            }
        }
    }
    if (name.empty()) {
        name = rd8(rdram, entry + 4) == 3 ? "Magic" : "Special";
    }
    return name + " x" + std::to_string(count);
}

Member read_member(const uint8_t* rdram, int ref) {
    Member m;
    if (ref >= SOLDIER_REF) {
        m.name = "Soldier";
        m.cls = "Soldier";
        m.soldier = true;
        m.class_id = 1;
        return m;
    }
    const uint32_t c = CHAR_TABLE + static_cast<uint32_t>(ref) * CHAR_STRIDE;
    m.name = guest_string(rdram, c, 16);
    m.class_id = rd8(rdram, c + 0x11);
    m.cls = class_name(rdram, m.class_id);
    m.level = rd8(rdram, c + 0x13);
    // Character +0x16 is maximum HP and +0x18 current HP (battle captures 51-55,
    // 2026-10-30; LordlyCaliber hpMax 22 / hpCur 24).
    m.hp = rd16(rdram, c + 0x18);
    m.hp_max = rd16(rdram, c + 0x16);
    m.hp_percent = m.hp_max > 0 ? std::clamp(m.hp * 100 / m.hp_max, 0, 100) : 0;
    return m;
}

// The Change Class list for the selected character. Class changes follow the
// game's own rules (with no alignment requirement, class_rebalance.cpp) and are
// made in the game's Organize menu, so the list holds the current class. The
// game's candidate list (func_ovlS_801D2808) replaces it once this view edits.
void refresh_classes(Model& model, const uint8_t* rdram) {
    const Character& ch = model.character;
    model.classes.clear();
    if (ch.record == 0) {
        model.picked = ClassOption{};
        model.picked_number = 0;
        model.class_count = 0;
        return;
    }

    ClassOption current;
    current.name = ch.cls;
    current.colour = class_colour(ch.class_id);
    current.class_id = ch.class_id;
    current.kind = "current";
    current.status = "Current";
    current.reason = ch.name + " is a " + ch.cls + " now.";

    // With the game's Organize screen open, the list is the game's own
    // (organize_edit: func_ovlS_801D2808 and its checks), and Change Class runs
    // the game's apply step.
    std::vector<organize_edit::ClassCandidate> game_list;
    if (model.editable && rdram != nullptr) {
        if (!organize_edit::class_candidates(ch.record, game_list)) {
            organize_edit::request_class_list(ch.record);
            current.reason += " Reading the classes the game offers...";
        }
    }
    if (!game_list.empty()) {
        const int now = rd8(rdram, CHAR_TABLE + static_cast<uint32_t>(ch.record) * CHAR_STRIDE + 0x12);
        bool listed_current = false;
        for (const organize_edit::ClassCandidate& c : game_list) {
            ClassOption option;
            option.class_id = c.cls;
            option.name = class_name(rdram, c.cls);
            option.colour = class_colour(c.cls);
            const bool is_current = c.cls == now || c.cls == ch.class_id;
            if (is_current) {
                option = current;
                listed_current = true;
            }
            else {
                option.cost = c.cost;
                // The requirement chart: one bar per stat, red where the
                // character is short (the same comparisons as the game's check).
                const int have[6] = {ch.str, ch.vit, ch.intel, ch.men, ch.agi, ch.dex};
                static const char* const kStat[6] = {"STR", "VIT", "INT", "MEN", "AGI", "DEX"};
                for (int k = 0; k < 6; k++) {
                    StatNeed n;
                    n.name = kStat[k];
                    n.have = have[k];
                    n.need = c.req[k] == 0xFF ? 0 : c.req[k];
                    const int top = std::max({n.have, n.need, 1});
                    n.fill = n.have * 100 / top;
                    n.mark = n.need * 100 / top;
                    n.shortfall = n.need > 0 && n.have < n.need;
                    option.needs.push_back(n);
                }
                // Alignment only counts with CLASS ALIGNMENT = VANILLA
                // (class_rebalance.cpp ogre_alignment_requirement).
                if (choice_index(Choice::ClassAlignment) == 1 && (c.req[6] != 0xFF || c.req[7] != 0xFF)) {
                    StatNeed n;
                    n.name = "ALN";
                    n.have = ch.alignment;
                    n.need = c.req[6] == 0xFF ? 0 : c.req[6];
                    n.fill = std::clamp(n.have, 0, 100);
                    n.mark = std::clamp(n.need, 0, 100);
                    n.shortfall = (c.req[6] != 0xFF && n.have < c.req[6]) || (c.req[7] != 0xFF && n.have > c.req[7]);
                    option.needs.push_back(n);
                }
            }
            if (is_current) {
                // keeps the current class's line
            }
            else if (c.ok) {
                option.kind = "ready";
                option.status = c.cost > 0 ? std::to_string(c.cost) + " Goth" : Rml::String("Ready");
                option.reason = ch.name + " can become a " + option.name +
                                (c.cost > 0 ? ". Change Class buys the missing Basic Equipment for " +
                                                  std::to_string(c.cost) + " Goth."
                                            : Rml::String("."));
            }
            else {
                static const char* const kShort[] = {"Stats", "Line", "Special", "Gear", "Goth", "Shop"};
                option.kind = "locked";
                option.status = c.reason >= 1 && c.reason <= 5 ? kShort[c.reason] : kShort[0];
                option.reason = option.name + ": " + organize_edit::class_reason(c.reason);
                if (c.reason == 0) {
                    Rml::String missing;
                    for (const StatNeed& n : option.needs) {
                        if (n.shortfall) {
                            missing += (missing.empty() ? " Short on " : ", ") + n.name + " " + std::to_string(n.have) +
                                       "/" + std::to_string(n.need);
                        }
                    }
                    option.reason += missing.empty() ? Rml::String() : missing + ".";
                }
            }
            model.classes.push_back(std::move(option));
        }
        if (!listed_current) {
            model.classes.insert(model.classes.begin(), current);
        }
        model.selected_class = std::clamp(model.selected_class, 0, static_cast<int>(model.classes.size()) - 1);
        for (size_t i = 0; i < model.classes.size(); i++) {
            model.classes[i].index = static_cast<int>(i);
            model.classes[i].selected = static_cast<int>(i) == model.selected_class;
        }
        model.picked = model.classes[static_cast<size_t>(model.selected_class)];
        model.picked_number = model.selected_class + 1;
        model.class_count = static_cast<int>(model.classes.size());
        return;
    }
    model.classes.push_back(std::move(current));

    // The classes this one changes into (class +0x43 names the class it comes
    // from, +0x44 the level it needs; tools/class_catalog.py). The game
    // repeats a class under several ids, so each name is listed once. Only the
    // level is checked here; the game's own menu applies its full rules.
    if (rdram != nullptr) {
        Rml::Vector<Rml::String> seen;
        for (int c = 1; c < CLASS_COUNT; c++) {
            const uint32_t record = class_table() + static_cast<uint32_t>(c) * CLASS_STRIDE;
            if (rd8(rdram, record + 0x43) != ch.class_id) {
                continue;
            }
            const Rml::String name = class_name(rdram, c);
            if (std::find(seen.begin(), seen.end(), name) != seen.end()) {
                continue;
            }
            seen.push_back(name);
            const int gate = rd8(rdram, record + 0x44);
            ClassOption option;
            option.name = name;
            option.colour = class_colour(c);
            option.class_id = c;
            const bool ready = ch.level >= gate;
            option.kind = ready ? "ready" : "locked";
            option.status = ready ? "Ready" : "Lv " + std::to_string(gate);
            option.reason = ready ? ch.name + " can become a " + name + " (level " + std::to_string(gate) +
                                        " reached). Other requirements are checked by the game's menu."
                                  : ch.name + " needs level " + std::to_string(gate) + " to become a " + name +
                                        " (now level " + std::to_string(ch.level) + ").";
            model.classes.push_back(std::move(option));
        }
        if (seen.empty()) {
            model.classes[0].reason = ch.name + " is a " + ch.cls + ". This class has no further class change.";
        }
    }

    model.selected_class = std::clamp(model.selected_class, 0, static_cast<int>(model.classes.size()) - 1);
    for (size_t i = 0; i < model.classes.size(); i++) {
        model.classes[i].index = static_cast<int>(i);
        model.classes[i].selected = static_cast<int>(i) == model.selected_class;
    }
    model.picked = model.classes[static_cast<size_t>(model.selected_class)];
    model.picked_number = model.selected_class + 1;
    model.class_count = static_cast<int>(model.classes.size());
}

// The Class List: the classes the army has seen (func_ovlS_801D736C's bits),
// and for the picked one, who the game would let take it.
void refresh_class_list(Model& model, const uint8_t* rdram) {
    model.known_classes.clear();
    model.class_holders.clear();
    if (!model.class_list_mode) {
        return;
    }
    for (int c = 1; c < CLASS_COUNT; c++) {
        if (!organize_edit::class_known(rdram, c)) {
            continue;
        }
        ClassListEntry e;
        e.id = c;
        e.class_id = c;
        e.name = class_name(rdram, c);
        e.selected = c == model.class_list_pick;
        model.known_classes.push_back(std::move(e));
    }
    if (model.class_list_pick == 0 && !model.known_classes.empty()) {
        model.class_list_pick = model.known_classes.front().id;
        model.known_classes.front().selected = true;
    }
    if (model.class_list_pick == 0 || !model.editable) {
        return;
    }
    std::vector<organize_edit::ClassHolder> holders;
    if (!organize_edit::class_holders(model.class_list_pick, holders)) {
        organize_edit::request_class_holders(model.class_list_pick);
        return;
    }
    for (const organize_edit::ClassHolder& h : holders) {
        ClassListEntry e;
        e.id = h.character;
        e.class_id = rd8(rdram, CHAR_TABLE + static_cast<uint32_t>(h.character) * CHAR_STRIDE + 0x11);
        e.name = guest_string(rdram, CHAR_TABLE + static_cast<uint32_t>(h.character) * CHAR_STRIDE, 16);
        e.kind = h.reason == -1 ? "current" : h.ok ? "ready" : "locked";
        e.status = h.reason == -1 ? Rml::String("Is one")
                   : h.ok         ? Rml::String("Eligible")
                                  : Rml::String(organize_edit::class_reason(h.reason));
        model.class_holders.push_back(std::move(e));
    }
    std::stable_sort(model.class_holders.begin(), model.class_holders.end(),
                     [](const ClassListEntry& a, const ClassListEntry& b) { return a.kind > b.kind; });
}

// The Equipment, Items and Armory tabs (organize_edit.hpp has the tables).
void refresh_gear(Model& model, const uint8_t* rdram) {
    namespace oe = organize_edit;
    const int ref = model.character.record;
    model.gear.clear();
    model.gear_choices.clear();
    if (ref > 0 && ref < SOLDIER_REF) {
        for (int slot = 0; slot < oe::GEAR_SLOTS; slot++) {
            GearSlot g;
            g.slot = slot;
            g.item = oe::gear_item(rdram, ref, slot);
            g.name = g.item != 0 ? oe::equipment_name(rdram, g.item) : Rml::String("-");
            g.category = oe::category_name(oe::equipment_category(rdram, g.item));
            g.basic = g.item == oe::basic_item(rdram, ref, slot);
            g.locked = oe::gear_slot_locked(rdram, ref, slot);
            g.open = slot == model.open_slot;
            model.gear.push_back(std::move(g));
        }
        // The game's candidate list (func_ovlS_801CFCD8): owned items of the
        // slot's category; the Basic Equipment is always offered first.
        if (model.open_slot >= 0 && model.open_slot < oe::GEAR_SLOTS) {
            const int current = oe::gear_item(rdram, ref, model.open_slot);
            const int basic = oe::basic_item(rdram, ref, model.open_slot);
            const int category = oe::equipment_category(rdram, current);
            if (basic != 0) {
                model.gear_choices.push_back(GearChoice{0, "Basic: " + oe::equipment_name(rdram, basic), 0, current == basic});
            }
            for (int i = 0; i < oe::EQUIP_RECORDS; i++) {
                oe::Owned owned;
                if (!oe::read_owned(rdram, i, owned) || owned.item == basic || !oe::equippable(rdram, owned.item) ||
                    oe::equipment_category(rdram, owned.item) != category) {
                    continue;
                }
                model.gear_choices.push_back(GearChoice{owned.item, oe::equipment_name(rdram, owned.item),
                                                        owned.owned - owned.in_use, owned.item == current});
            }
        }
    }

    model.owned_items.clear();
    if (model.item_filter == "consumables") {
        for (int i = 0; i < organize_edit::STOCK_RECORDS; i++) {
            organize_edit::Stock stock;
            if (!organize_edit::read_stock(rdram, i, stock) || stock.owned == 0) {
                continue;
            }
            OwnedEntry e;
            e.list = 1;
            e.index = i;
            e.item = stock.item;
            e.name = oe::item_name(rdram, stock.item);
            const int category = oe::consumable_category(rdram, stock.item);
            e.category = category == 0 ? "Expendable" : category == 1 ? "Stat item" : category == 2 ? "Tool"
                         : category == 3 ? "Charm" : "Quest item";
            e.owned = stock.owned;
            e.in_use = stock.in_use;
            e.sell = category < 3 ? oe::consumable_price(rdram, stock.item) / 4 : 0;
            model.owned_items.push_back(std::move(e));
        }
    }
    else {
        for (int i = 0; i < oe::EQUIP_RECORDS; i++) {
            oe::Owned owned;
            if (!oe::read_owned(rdram, i, owned) || owned.owned == 0) {
                continue;
            }
            OwnedEntry e;
            e.index = i;
            e.item = owned.item;
            e.name = oe::equipment_name(rdram, owned.item);
            e.category = oe::category_name(oe::equipment_category(rdram, owned.item));
            e.owned = owned.owned;
            e.in_use = owned.in_use;
            e.sell = oe::equipment_price(rdram, owned.item) / 4;
            model.owned_items.push_back(std::move(e));
        }
    }
    // Sort (the game's Item > Sort criteria: kind, quantity, cost, name).
    const Rml::String sort = model.item_sort;
    std::stable_sort(model.owned_items.begin(), model.owned_items.end(), [&sort](const OwnedEntry& a, const OwnedEntry& b) {
        if (sort == "name") return a.name < b.name;
        if (sort == "owned") return a.owned > b.owned;
        if (sort == "price") return a.sell > b.sell;
        return a.category < b.category;
    });

    // Buy Basic Equipment: one set per class the army has.
    model.armory.clear();
    const uint32_t goth = rd32(rdram, GOTH);
    for (const Character& ch : model.characters) {
        auto found = std::find_if(model.armory.begin(), model.armory.end(),
                                  [&](const ArmorySet& a) { return a.class_id == ch.class_id; });
        if (found != model.armory.end()) {
            found->members++;
            continue;
        }
        ArmorySet set;
        set.class_id = ch.class_id;
        set.name = ch.cls;
        set.price = oe::basic_set_price(rdram, ch.class_id);
        if (set.price <= 0) {
            continue;
        }
        for (int slot = 0; slot < oe::GEAR_SLOTS; slot++) {
            const int item = oe::basic_set_item(rdram, ch.class_id, slot);
            if (item != 0) {
                set.items += (set.items.empty() ? "" : ", ") + oe::equipment_name(rdram, item);
            }
        }
        set.members = 1;
        set.affordable = goth >= static_cast<uint32_t>(set.price);
        model.armory.push_back(std::move(set));
    }
}

void refresh(const uint8_t* rdram) {
    Model& model = state().model;
    model.goth = std::to_string(rd32(rdram, GOTH));
    model.chaos_frame = rd8(rdram, CHAOS_FRAME);

    // Which unit each character is in.
    int unit_of[CHAR_COUNT] = {};

    // While the game's Organize screen is open its working copy is the truth
    // (organize_edit.cpp); otherwise the unit table is.
    struct Layout {
        int index = 0;
        int number = 0;
        int leader_square = organize_edit::EMPTY;
        int strategy = 0;
        int soldier_exp = 0;
        int items[organize_edit::CARRY_SLOTS] = {};
        int refs[9];
    };
    Rml::Vector<Layout> layouts;
    model.editable = organize_edit::working_copy_open(rdram);
    static int logged_editable = -1;
    if (logged_editable != static_cast<int>(model.editable)) {
        logged_editable = static_cast<int>(model.editable);
        std::fprintf(stderr, "[organize] view %s\n", model.editable ? "over the game's Organize screen (game rules)"
                                                                     : "read only (no game Organize screen)");
    }
    if (model.editable) {
        for (int slot = 0; slot < organize_edit::UNIT_SLOTS; slot++) {
            organize_edit::Unit working;
            if (!organize_edit::read_unit(rdram, slot, working)) {
                continue;
            }
            Layout layout;
            layout.index = slot;
            layout.number = working.number;
            layout.leader_square = working.leader_square;
            layout.strategy = working.strategy;
            layout.soldier_exp = working.soldier_exp;
            for (int i = 0; i < organize_edit::CARRY_SLOTS; i++) {
                layout.items[i] = working.items[i];
            }
            for (int sq = 0; sq < 9; sq++) {
                layout.refs[sq] = working.squares[sq];
            }
            layouts.push_back(layout);
        }
    }
    else {
        for (int u = 0; u < PLAYER_UNITS; u++) {
            const uint32_t r = UNIT_TABLE + static_cast<uint32_t>(u) * UNIT_STRIDE;
            if (rd8(rdram, r + 0x02) == 0) {
                continue;
            }
            Layout layout;
            layout.index = u;
            layout.number = rd8(rdram, r + 0x00);
            layout.strategy = rd8(rdram, r + 0x17);
            layout.soldier_exp = rd8(rdram, r + 0x0C);
            // The record keeps carried items as bytes at +0x0D.. (the commit,
            // func_ovlS_801D5328, packs the working u16 list into them).
            for (int i = 0; i < organize_edit::CARRY_SLOTS; i++) {
                layout.items[i] = rd8(rdram, r + 0x0D + static_cast<uint32_t>(i));
            }
            for (int& ref : layout.refs) {
                ref = organize_edit::EMPTY;
            }
            for (uint32_t i = 0; i < 5; i++) {
                const int ref = rd8(rdram, r + 0x02 + i);
                const int square = rd8(rdram, r + 0x07 + i);
                if (ref == 0 || square < 0 || square >= 9) {
                    continue;
                }
                layout.refs[square] = ref;
                if (i == 0) {
                    layout.leader_square = square;
                }
            }
            layouts.push_back(layout);
        }
    }

    model.units.clear();
    for (const Layout& layout : layouts) {
        Unit unit;
        unit.index = layout.index;
        char buffer[8];
        std::snprintf(buffer, sizeof(buffer), "%02d", layout.number);
        unit.number = buffer;
        Rml::Vector<Square> squares(9, Square{"", "empty", "transparent", false});
        int members = 0;
        // The leader first, then the others in square order (the game's own
        // member order after its commit).
        for (int pass = 0; pass < 2; pass++) {
            for (int sq = 0; sq < 9; sq++) {
                const int ref = layout.refs[sq];
                const bool leader = sq == layout.leader_square;
                if (ref == organize_edit::EMPTY || ref == 0 || leader != (pass == 0)) {
                    continue;
                }
                members++;
                Member m = read_member(rdram, ref);
                if (leader) {
                    unit.name = m.name + "'s Unit";
                }
                if (ref < CHAR_COUNT) {
                    unit_of[ref] = layout.number;
                }
                const int cls = m.class_id;
                Rml::String kind = leader ? "leader" : "member";
                if (!leader && class_rebalance::original_large(cls)) {
                    kind = "large";
                }
                squares[static_cast<size_t>(sq)] = Square{m.cls, kind, class_colour(cls), false};
                squares[static_cast<size_t>(sq)].class_id = cls;
                squares[static_cast<size_t>(sq)].sprite =
                    "sprite:" + std::to_string(cls) + ":" + std::to_string(ref >= SOLDIER_REF ? soldier_figures(rdram, ref) - 1 : 0);
                unit.members.push_back(std::move(m));
            }
        }
        for (int row = 0; row < 3; row++) {
            Row r;
            for (int col = 0; col < 3; col++) {
                // The game draws a row's squares mirrored: square row*3+2 on
                // the left (LordlyCaliber squads.js, NEUTRAL_GRID 2,1,0,...).
                const int sq_index = row * 3 + (2 - col);
                Square sq = squares[static_cast<size_t>(sq_index)];
                sq.dark = (row + col) % 2 != 0;  // the checker follows the drawn position
                sq.index = sq_index;
                sq.selected = model.editable && layout.index == model.sel_slot && sq_index == model.sel_square;
                r.squares.push_back(std::move(sq));
            }
            unit.rows.push_back(std::move(r));
        }
        unit.count = std::to_string(members) + "/5";
        unit.strategy = organize_edit::strategy_name(layout.strategy);
        // Soldier promotion. The unit's soldiers share one experience pool,
        // record +0x0C (working +0x1F): a won battle raised it 0 -> 5 for a
        // unit with three soldier slots (developer's snapshots, 2026-10-29).
        // At 100 one slot becomes a Fighter (male leader) or an Amazon (female
        // leader) when a spare equipment set of that class is in stock, and the
        // pool restarts (developer's notes from a game guide).
        unit.soldier_exp = layout.soldier_exp;
        for (int ref : layout.refs) {
            unit.has_soldiers = unit.has_soldiers || (ref >= SOLDIER_REF && ref != organize_edit::EMPTY);
        }
        if (layout.leader_square >= 0 && layout.leader_square < 9) {
            const int leader = layout.refs[layout.leader_square];
            const bool female = leader > 0 && leader < SOLDIER_REF &&
                                rd8(rdram, CHAR_TABLE + static_cast<uint32_t>(leader) * CHAR_STRIDE + 0x14) != 0;
            unit.promotes_to = female ? "Amazon" : "Fighter";
        }
        int carried = 0;
        for (int i = 0; i < organize_edit::CARRY_SLOTS; i++) {
            if (layout.items[i] != 0) {
                unit.items.push_back(ItemEntry{i, organize_edit::item_name(rdram, layout.items[i]), 0, layout.items[i]});
                carried++;
            }
        }
        unit.carry = std::to_string(carried) + " / " +
                     (model.editable ? std::to_string(organize_edit::carry_capacity(rdram, layout.index)) : Rml::String("?"));
        unit.selected = layout.index == model.selected_unit;
        model.units.push_back(std::move(unit));
    }
    model.soldier_reserve = organize_edit::soldier_reserve(rdram);
    model.strategies.clear();
    for (const Layout& layout : layouts) {
        if (layout.index != model.selected_unit) {
            continue;
        }
        for (int s = 0; s < organize_edit::STRATEGIES; s++) {
            model.strategies.push_back(StrategyOption{s, organize_edit::strategy_name(s), s == layout.strategy});
        }
    }

    // The picked member's panel: who stands on the picked square, their stats
    // and the attacks of each row, with the row they stand in marked (squares
    // 0..2 are the back row, 6..8 the front; LordlyCaliber squads.js).
    model.picked_member = Picked{};
    for (const Layout& layout : layouts) {
        if (layout.index != model.sel_slot || model.sel_square < 0 || model.sel_square >= 9) {
            continue;
        }
        const int ref = layout.refs[model.sel_square];
        if (ref == organize_edit::EMPTY || ref == 0) {
            break;
        }
        Picked& p = model.picked_member;
        p.shown = true;
        const int row_index = model.sel_square / 3;  // 0 back, 1 middle, 2 front
        static const char* const kRowNames[3] = {"Front", "Middle", "Back"};
        const int attack_row = 2 - row_index;         // class rows run front, middle, back
        if (ref >= SOLDIER_REF) {
            p.soldier = true;
            p.class_id = 1;
            p.cls = "Soldier";
            p.name = "Soldiers x" + std::to_string(soldier_figures(rdram, ref));
            p.hp = rd16(rdram, 0x80190EDCu + static_cast<uint32_t>(ref) * 2);
            p.hp_max = rd16(rdram, 0x80190FA2u);
        }
        else {
            const uint32_t c = CHAR_TABLE + static_cast<uint32_t>(ref) * CHAR_STRIDE;
            p.class_id = rd8(rdram, c + 0x11);
            p.name = guest_string(rdram, c, 16);
            p.cls = class_name(rdram, p.class_id);
            p.level = rd8(rdram, c + 0x13);
            p.hp = rd16(rdram, c + 0x18);
            p.hp_max = rd16(rdram, c + 0x16);
            char stats[96];
            std::snprintf(stats, sizeof(stats), "STR %d  VIT %d  INT %d  MEN %d  AGI %d  DEX %d", rd16(rdram, c + 0x1C),
                          rd16(rdram, c + 0x1E), rd16(rdram, c + 0x20), rd16(rdram, c + 0x22), rd16(rdram, c + 0x24),
                          rd16(rdram, c + 0x26));
            p.stats = stats;
        }
        p.hp_percent = p.hp_max > 0 ? std::clamp(p.hp * 100 / p.hp_max, 0, 100) : 0;
        p.row = kRowNames[attack_row];
        for (int r = 0; r < 3; r++) {
            AttackRow a;
            a.row = kRowNames[r];
            a.attack = attack_text(rdram, p.class_id, r, p.soldier ? 0 : ref, &a.detail);
            a.index = r;
            a.current = r == attack_row;
            a.open = r == model.open_attack;
            p.attacks.push_back(std::move(a));
        }
        break;
    }
    model.stock.clear();
    for (int s = 0; s < organize_edit::STOCK_RECORDS; s++) {
        organize_edit::Stock stock;
        if (organize_edit::read_stock(rdram, s, stock) && stock.owned > stock.in_use &&
            organize_edit::carriable(rdram, stock.item)) {
            model.stock.push_back(ItemEntry{stock.item, organize_edit::item_name(rdram, stock.item),
                                            stock.owned - stock.in_use, stock.item});
        }
    }

    if (model.selected_unit < 0 && !model.units.empty()) {
        model.selected_unit = model.units.front().index;
        model.units.front().selected = true;
    }
    model.unit = Unit{};
    for (const Unit& unit : model.units) {
        if (unit.index == model.selected_unit) {
            model.unit = unit;
        }
    }

    model.characters.clear();
    for (int n = CHAR_FIRST; n < CHAR_COUNT; n++) {
        const uint32_t c = CHAR_TABLE + static_cast<uint32_t>(n) * CHAR_STRIDE;
        const int cls = rd8(rdram, c + 0x11);
        if (cls == 0 || cls >= class_limit()) {
            continue;
        }
        Character ch;
        ch.record = n;
        ch.name = guest_string(rdram, c, 16);
        ch.cls = class_name(rdram, cls);
        ch.level = rd8(rdram, c + 0x13);
        ch.unit = unit_of[n] != 0 ? std::to_string(unit_of[n]) : Rml::String("-");
        ch.hp = rd16(rdram, c + 0x18);
        ch.hp_max = rd16(rdram, c + 0x16);
        ch.hp_percent = ch.hp_max > 0 ? std::clamp(ch.hp * 100 / ch.hp_max, 0, 100) : 0;
        ch.class_id = cls;
        ch.colour = class_colour(cls);
        ch.initial = ch.name.empty() ? Rml::String("?") : ch.name.substr(0, 1);
        ch.alignment = rd8(rdram, c + 0x1B);
        ch.str = rd16(rdram, c + 0x1C);
        ch.vit = rd16(rdram, c + 0x1E);
        ch.intel = rd16(rdram, c + 0x20);
        ch.men = rd16(rdram, c + 0x22);
        ch.agi = rd16(rdram, c + 0x24);
        ch.dex = rd16(rdram, c + 0x26);
        model.characters.push_back(std::move(ch));
    }
    if (model.sort == "level") {
        std::stable_sort(model.characters.begin(), model.characters.end(),
                         [](const Character& a, const Character& b) { return a.level > b.level; });
    }
    else if (model.sort == "name") {
        std::stable_sort(model.characters.begin(), model.characters.end(),
                         [](const Character& a, const Character& b) { return a.name < b.name; });
    }
    else if (model.sort == "class") {
        std::stable_sort(model.characters.begin(), model.characters.end(),
                         [](const Character& a, const Character& b) { return a.cls < b.cls; });
    }
    if (model.selected_character == 0 && !model.characters.empty()) {
        model.selected_character = model.characters.front().record;
    }
    model.character = Character{};
    for (Character& ch : model.characters) {
        ch.selected = ch.record == model.selected_character;
        if (ch.selected) {
            model.character = ch;
        }
    }
    model.unassigned.clear();
    for (const Character& ch : model.characters) {
        if (ch.unit == "-") {
            model.unassigned.push_back(ch);
        }
    }
    refresh_classes(model, rdram);
    refresh_gear(model, rdram);
    refresh_class_list(model, rdram);
    const std::string result = organize_edit::last_result();
    if (!result.empty() && (model.notice == "Moving..." || model.notice == "Adding..." || model.notice == "Removing..." ||
                             model.notice == "Changing leader..." || model.notice == "Changing strategy..." ||
                             model.notice == "Carrying..." || model.notice == "Putting back..." ||
                             model.notice == "Forming a new unit..." || model.notice == "Equipping..." ||
                             model.notice == "Selling..." || model.notice == "Buying..." ||
                             model.notice == "Changing class..." || model.notice == "Discharging..." ||
                             model.notice == "Renaming..." || model.notice == "Replenishing..." ||
                             model.notice == "Exchanging..." || model.notice == "Unequipping...")) {
        model.notice = result;
    }
    state().handle.DirtyAllVariables();
}

// True when square `square` of the unit in working slot `slot` holds someone,
// from the last refresh.
bool square_occupied(const Model& m, int slot, int square) {
    for (const Unit& unit : m.units) {
        if (unit.index != slot) {
            continue;
        }
        for (const Row& row : unit.rows) {
            for (const Square& sq : row.squares) {
                if (sq.index == square) {
                    return sq.kind != "empty";
                }
            }
        }
    }
    return false;
}

}  // namespace

void leave_game_screen();

// The on-screen keyboard's keys, upper or lower case, and the pad's shortcuts
// named after the pad in use.
void build_keyboard(Model& m) {
    m.keyboard_keys.clear();
    for (char c = 'A'; c <= 'Z'; c++) {
        m.keyboard_keys.push_back(Rml::String(1, m.keyboard_upper ? c : static_cast<char>(c - 'A' + 'a')));
    }
    for (char c = '0'; c <= '9'; c++) {
        m.keyboard_keys.push_back(Rml::String(1, c));
    }
    for (const char* extra : {"-", ".", "'"}) {
        m.keyboard_keys.push_back(extra);
    }
    m.keyboard_help = pad_button_name(SDL_CONTROLLER_BUTTON_A) + " types the key, " +
                      pad_button_name(SDL_CONTROLLER_BUTTON_X) + " deletes, " +
                      pad_button_name(SDL_CONTROLLER_BUTTON_Y) + " types a space, " +
                      pad_button_name(SDL_CONTROLLER_BUTTON_START) + " renames, " +
                      pad_button_name(SDL_CONTROLLER_BUTTON_B) + " closes.";
}

void rename_now(Model& m) {
    if (!m.editable) {
        m.notice = "Read only: open Organize from the map's R menu (this view opens over it) to rename.";
    }
    else {
        organize_edit::rename(m.character.record, m.rename_text);
        m.notice = "Renaming...";
    }
    state().presents = REFRESH_PRESENTS;
}

void close_keyboard(State& s) {
    s.model.keyboard_open = false;
    if (s.document != nullptr) {
        if (Rml::Element* field = s.document->QuerySelector(".namefield")) {
            field->Focus(true);
        }
    }
}

bool init(Rml::Context* context, const std::filesystem::path& asset_dir) {
    State& s = state();
    Rml::DataModelConstructor ctor = context->CreateDataModel("organize");
    if (!ctor) {
        return false;
    }
    if (auto square = ctor.RegisterStruct<Square>()) {
        square.RegisterMember("label", &Square::label);
        square.RegisterMember("kind", &Square::kind);
        square.RegisterMember("colour", &Square::colour);
        square.RegisterMember("dark", &Square::dark);
        square.RegisterMember("index", &Square::index);
        square.RegisterMember("selected", &Square::selected);
        square.RegisterMember("class_id", &Square::class_id);
        square.RegisterMember("sprite", &Square::sprite);
    }
    ctor.RegisterArray<Rml::Vector<Square>>();
    if (auto row = ctor.RegisterStruct<Row>()) {
        row.RegisterMember("squares", &Row::squares);
    }
    ctor.RegisterArray<Rml::Vector<Row>>();
    if (auto item = ctor.RegisterStruct<ItemEntry>()) {
        item.RegisterMember("index", &ItemEntry::index);
        item.RegisterMember("name", &ItemEntry::name);
        item.RegisterMember("free", &ItemEntry::free);
        item.RegisterMember("item", &ItemEntry::item);
    }
    ctor.RegisterArray<Rml::Vector<ItemEntry>>();
    if (auto row = ctor.RegisterStruct<AttackRow>()) {
        row.RegisterMember("row", &AttackRow::row);
        row.RegisterMember("attack", &AttackRow::attack);
        row.RegisterMember("current", &AttackRow::current);
        row.RegisterMember("detail", &AttackRow::detail);
        row.RegisterMember("index", &AttackRow::index);
        row.RegisterMember("open", &AttackRow::open);
    }
    if (auto option = ctor.RegisterStruct<StrategyOption>()) {
        option.RegisterMember("index", &StrategyOption::index);
        option.RegisterMember("name", &StrategyOption::name);
        option.RegisterMember("current", &StrategyOption::current);
    }
    ctor.RegisterArray<Rml::Vector<StrategyOption>>();
    {
    }
    ctor.RegisterArray<Rml::Vector<AttackRow>>();
    if (auto picked = ctor.RegisterStruct<Picked>()) {
        picked.RegisterMember("shown", &Picked::shown);
        picked.RegisterMember("soldier", &Picked::soldier);
        picked.RegisterMember("class_id", &Picked::class_id);
        picked.RegisterMember("name", &Picked::name);
        picked.RegisterMember("cls", &Picked::cls);
        picked.RegisterMember("level", &Picked::level);
        picked.RegisterMember("hp", &Picked::hp);
        picked.RegisterMember("hp_max", &Picked::hp_max);
        picked.RegisterMember("hp_percent", &Picked::hp_percent);
        picked.RegisterMember("stats", &Picked::stats);
        picked.RegisterMember("row", &Picked::row);
        picked.RegisterMember("attacks", &Picked::attacks);
    }
    if (auto g = ctor.RegisterStruct<GearSlot>()) {
        g.RegisterMember("slot", &GearSlot::slot);
        g.RegisterMember("item", &GearSlot::item);
        g.RegisterMember("name", &GearSlot::name);
        g.RegisterMember("category", &GearSlot::category);
        g.RegisterMember("basic", &GearSlot::basic);
        g.RegisterMember("locked", &GearSlot::locked);
        g.RegisterMember("open", &GearSlot::open);
    }
    ctor.RegisterArray<Rml::Vector<GearSlot>>();
    if (auto g = ctor.RegisterStruct<GearChoice>()) {
        g.RegisterMember("item", &GearChoice::item);
        g.RegisterMember("name", &GearChoice::name);
        g.RegisterMember("free", &GearChoice::free);
        g.RegisterMember("current", &GearChoice::current);
    }
    ctor.RegisterArray<Rml::Vector<GearChoice>>();
    if (auto o = ctor.RegisterStruct<OwnedEntry>()) {
        o.RegisterMember("list", &OwnedEntry::list);
        o.RegisterMember("index", &OwnedEntry::index);
        o.RegisterMember("item", &OwnedEntry::item);
        o.RegisterMember("name", &OwnedEntry::name);
        o.RegisterMember("category", &OwnedEntry::category);
        o.RegisterMember("owned", &OwnedEntry::owned);
        o.RegisterMember("in_use", &OwnedEntry::in_use);
        o.RegisterMember("sell", &OwnedEntry::sell);
    }
    ctor.RegisterArray<Rml::Vector<OwnedEntry>>();
    if (auto e = ctor.RegisterStruct<ClassListEntry>()) {
        e.RegisterMember("id", &ClassListEntry::id);
        e.RegisterMember("class_id", &ClassListEntry::class_id);
        e.RegisterMember("name", &ClassListEntry::name);
        e.RegisterMember("status", &ClassListEntry::status);
        e.RegisterMember("kind", &ClassListEntry::kind);
        e.RegisterMember("selected", &ClassListEntry::selected);
    }
    ctor.RegisterArray<Rml::Vector<ClassListEntry>>();
    if (auto a = ctor.RegisterStruct<ArmorySet>()) {
        a.RegisterMember("class_id", &ArmorySet::class_id);
        a.RegisterMember("name", &ArmorySet::name);
        a.RegisterMember("items", &ArmorySet::items);
        a.RegisterMember("price", &ArmorySet::price);
        a.RegisterMember("members", &ArmorySet::members);
        a.RegisterMember("affordable", &ArmorySet::affordable);
    }
    ctor.RegisterArray<Rml::Vector<ArmorySet>>();
    if (auto need = ctor.RegisterStruct<StatNeed>()) {
        need.RegisterMember("name", &StatNeed::name);
        need.RegisterMember("have", &StatNeed::have);
        need.RegisterMember("need", &StatNeed::need);
        need.RegisterMember("fill", &StatNeed::fill);
        need.RegisterMember("mark", &StatNeed::mark);
        need.RegisterMember("shortfall", &StatNeed::shortfall);
    }
    ctor.RegisterArray<Rml::Vector<StatNeed>>();
    if (auto option = ctor.RegisterStruct<ClassOption>()) {
        option.RegisterMember("index", &ClassOption::index);
        option.RegisterMember("name", &ClassOption::name);
        option.RegisterMember("colour", &ClassOption::colour);
        option.RegisterMember("class_id", &ClassOption::class_id);
        option.RegisterMember("kind", &ClassOption::kind);
        option.RegisterMember("status", &ClassOption::status);
        option.RegisterMember("reason", &ClassOption::reason);
        option.RegisterMember("selected", &ClassOption::selected);
        option.RegisterMember("cost", &ClassOption::cost);
        option.RegisterMember("needs", &ClassOption::needs);
    }
    ctor.RegisterArray<Rml::Vector<ClassOption>>();
    if (auto member = ctor.RegisterStruct<Member>()) {
        member.RegisterMember("name", &Member::name);
        member.RegisterMember("cls", &Member::cls);
        member.RegisterMember("level", &Member::level);
        member.RegisterMember("hp", &Member::hp);
        member.RegisterMember("hp_max", &Member::hp_max);
        member.RegisterMember("hp_percent", &Member::hp_percent);
        member.RegisterMember("soldier", &Member::soldier);
        member.RegisterMember("class_id", &Member::class_id);
    }
    ctor.RegisterArray<Rml::Vector<Member>>();
    if (auto unit = ctor.RegisterStruct<Unit>()) {
        unit.RegisterMember("index", &Unit::index);
        unit.RegisterMember("number", &Unit::number);
        unit.RegisterMember("name", &Unit::name);
        unit.RegisterMember("count", &Unit::count);
        unit.RegisterMember("rows", &Unit::rows);
        unit.RegisterMember("strategy", &Unit::strategy);
        unit.RegisterMember("items", &Unit::items);
        unit.RegisterMember("carry", &Unit::carry);
        unit.RegisterMember("has_soldiers", &Unit::has_soldiers);
        unit.RegisterMember("soldier_exp", &Unit::soldier_exp);
        unit.RegisterMember("promotes_to", &Unit::promotes_to);
        unit.RegisterMember("members", &Unit::members);
        unit.RegisterMember("selected", &Unit::selected);
    }
    ctor.RegisterArray<Rml::Vector<Unit>>();
    if (auto character = ctor.RegisterStruct<Character>()) {
        character.RegisterMember("record", &Character::record);
        character.RegisterMember("name", &Character::name);
        character.RegisterMember("cls", &Character::cls);
        character.RegisterMember("level", &Character::level);
        character.RegisterMember("unit", &Character::unit);
        character.RegisterMember("hp", &Character::hp);
        character.RegisterMember("hp_max", &Character::hp_max);
        character.RegisterMember("hp_percent", &Character::hp_percent);
        character.RegisterMember("colour", &Character::colour);
        character.RegisterMember("class_id", &Character::class_id);
        character.RegisterMember("initial", &Character::initial);
        character.RegisterMember("alignment", &Character::alignment);
        character.RegisterMember("str", &Character::str);
        character.RegisterMember("vit", &Character::vit);
        character.RegisterMember("intel", &Character::intel);
        character.RegisterMember("men", &Character::men);
        character.RegisterMember("agi", &Character::agi);
        character.RegisterMember("dex", &Character::dex);
        character.RegisterMember("selected", &Character::selected);
    }
    ctor.RegisterArray<Rml::Vector<Character>>();

    Model& m = s.model;
    ctor.Bind("tab", &m.tab);
    ctor.Bind("goth", &m.goth);
    ctor.Bind("chaos_frame", &m.chaos_frame);
    ctor.Bind("units", &m.units);
    ctor.Bind("characters", &m.characters);
    ctor.Bind("unit", &m.unit);
    ctor.Bind("character", &m.character);
    ctor.Bind("sort", &m.sort);
    ctor.Bind("classes", &m.classes);
    ctor.Bind("picked", &m.picked);
    ctor.Bind("picked_number", &m.picked_number);
    ctor.Bind("class_count", &m.class_count);
    ctor.Bind("unassigned", &m.unassigned);
    ctor.Bind("notice", &m.notice);
    ctor.Bind("editable", &m.editable);
    ctor.Bind("carry_open", &m.carry_open);
    ctor.Bind("strategy_open", &m.strategy_open);
    ctor.Bind("gear", &m.gear);
    ctor.Bind("gear_choices", &m.gear_choices);
    ctor.Bind("open_slot", &m.open_slot);
    ctor.Bind("owned_items", &m.owned_items);
    ctor.Bind("item_filter", &m.item_filter);
    ctor.Bind("armory", &m.armory);
    ctor.Bind("rename_text", &m.rename_text);
    ctor.Bind("discharge_confirm", &m.discharge_confirm);
    ctor.Bind("class_list_mode", &m.class_list_mode);
    ctor.Bind("known_classes", &m.known_classes);
    ctor.Bind("class_holders", &m.class_holders);
    ctor.Bind("item_sort", &m.item_sort);
    ctor.BindEventCallback("rename_character", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList&) {
        rename_now(state().model);
        handle.DirtyAllVariables();
    });
    ctor.Bind("keyboard_open", &m.keyboard_open);
    ctor.Bind("keyboard_upper", &m.keyboard_upper);
    ctor.RegisterArray<Rml::Vector<Rml::String>>();
    ctor.Bind("keyboard_keys", &m.keyboard_keys);
    ctor.Bind("keyboard_help", &m.keyboard_help);
    // open_keyboard(1) is the Keyboard button; open_keyboard(0) is a press on
    // the name field, which opens it only for the pad (a mouse click there
    // types with the keyboard).
    ctor.BindEventCallback("open_keyboard", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        const bool asked = !args.empty() && args[0].Get<int>() != 0;
        if (!asked && !pad_in_use()) {
            return;
        }
        Model& m = state().model;
        m.keyboard_open = true;
        build_keyboard(m);
        state().focus_keyboard = true;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("type_key", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        const int index = args.empty() ? -1 : args[0].Get<int>();
        if (index >= 0 && index < static_cast<int>(m.keyboard_keys.size()) && m.rename_text.size() < 15) {
            m.rename_text += m.keyboard_keys[index];
        }
        handle.DirtyVariable("rename_text");
    });
    ctor.BindEventCallback("keyboard_special", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        const Rml::String what = args.empty() ? Rml::String() : args[0].Get<Rml::String>();
        if (what == "shift") {
            m.keyboard_upper = !m.keyboard_upper;
            build_keyboard(m);
        }
        else if (what == "space" && m.rename_text.size() < 15) {
            m.rename_text += ' ';
        }
        else if (what == "delete" && !m.rename_text.empty()) {
            m.rename_text.pop_back();
        }
        else if (what == "done") {
            close_keyboard(state());
            rename_now(m);
        }
        else if (what == "close") {
            close_keyboard(state());
        }
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("discharge", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList&) {
        Model& m = state().model;
        if (!m.editable) {
            m.notice = "Read only: open Organize from the map's R menu (this view opens over it) to discharge.";
        }
        else if (!m.discharge_confirm) {
            m.discharge_confirm = true;
            m.notice = "Discharge " + m.character.name + " for good? Click Confirm Discharge.";
        }
        else {
            organize_edit::discharge(m.character.record);
            m.discharge_confirm = false;
            m.selected_character = 0;
            m.notice = "Discharging...";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("unequip_all", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (args.size() >= 2 && m.editable) {
            organize_edit::unequip_all(args[0].Get<int>() != 0, args[1].Get<int>());
            m.notice = "Unequipping...";
        }
        else if (!m.editable) {
            m.notice = "Read only: open Organize from the map's R menu (this view opens over it) to change items.";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("set_item_sort", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            state().model.item_sort = args[0].Get<Rml::String>();
            state().presents = REFRESH_PRESENTS;
            handle.DirtyAllVariables();
        }
    });
    ctor.BindEventCallback("toggle_class_list", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList&) {
        state().model.class_list_mode = !state().model.class_list_mode;
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("pick_list_class", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            state().model.class_list_pick = args[0].Get<int>();
            state().presents = REFRESH_PRESENTS;
            handle.DirtyAllVariables();
        }
    });
    auto needs_organize = [](Model& m) {
        if (!m.editable) {
            m.notice = "Read only: open Organize from the map's R menu (this view opens over it) to change equipment and items.";
            return true;
        }
        return false;
    };
    ctor.BindEventCallback("open_slot", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (!args.empty()) {
            const int slot = args[0].Get<int>();
            m.open_slot = m.open_slot == slot ? -1 : slot;
            state().presents = REFRESH_PRESENTS;
            handle.DirtyAllVariables();
        }
    });
    ctor.BindEventCallback("equip_item", [needs_organize](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (args.empty() || m.open_slot < 0 || needs_organize(m)) {
            handle.DirtyAllVariables();
            return;
        }
        organize_edit::equip(m.character.record, m.open_slot, args[0].Get<int>());
        m.open_slot = -1;
        m.notice = "Equipping...";
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("basic_equipment", [needs_organize](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList&) {
        Model& m = state().model;
        if (!needs_organize(m)) {
            organize_edit::basic_equipment(m.character.record);
            m.open_slot = -1;
            m.notice = "Equipping...";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("set_item_filter", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            state().model.item_filter = args[0].Get<Rml::String>();
            state().presents = REFRESH_PRESENTS;
            handle.DirtyAllVariables();
        }
    });
    ctor.BindEventCallback("sell_item", [needs_organize](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (args.size() >= 2 && !needs_organize(m)) {
            organize_edit::sell(args[0].Get<int>() != 0, args[1].Get<int>());
            m.notice = "Selling...";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("buy_set", [needs_organize](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (!args.empty() && !needs_organize(m)) {
            organize_edit::buy_basic(args[0].Get<int>());
            m.notice = "Buying...";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.Bind("strategies", &m.strategies);
    ctor.BindEventCallback("set_strategy", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (args.empty() || m.selected_unit < 0) {
            return;
        }
        organize_edit::set_strategy(m.selected_unit, args[0].Get<int>());
        m.strategy_open = false;
        m.notice = "Changing strategy...";
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("open_attack", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (!args.empty()) {
            const int index = args[0].Get<int>();
            m.open_attack = m.open_attack == index ? -1 : index;
            state().presents = REFRESH_PRESENTS;
            handle.DirtyAllVariables();
        }
    });
    ctor.Bind("new_unit_mode", &m.new_unit_mode);
    ctor.Bind("soldier_reserve", &m.soldier_reserve);
    ctor.Bind("picked_member", &m.picked_member);
    ctor.Bind("stock", &m.stock);
    ctor.BindEventCallback("carry_item", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (!args.empty() && m.editable && m.selected_unit >= 0) {
            organize_edit::carry(m.selected_unit, args[0].Get<int>());
            m.notice = "Carrying...";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("drop_item", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (args.empty() || !m.editable) {
            m.notice = "Read only: open Organize from the map's R menu (this view opens over it) to edit units.";
        }
        else if (m.selected_unit >= 0) {
            organize_edit::drop(m.selected_unit, args[0].Get<int>());
            m.notice = "Putting back...";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });

    ctor.BindEventCallback("set_tab", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            state().model.tab = args[0].Get<Rml::String>();
            state().presents = REFRESH_PRESENTS;  // refresh on the next present
        }
    });
    ctor.BindEventCallback("select_unit", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            state().model.selected_unit = args[0].Get<int>();
            state().presents = REFRESH_PRESENTS;
        }
    });
    ctor.BindEventCallback("select_character", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            state().model.selected_character = args[0].Get<int>();
            state().model.open_slot = -1;
            state().model.discharge_confirm = false;
            state().model.rename_text.clear();
            state().model.selected_class = 0;
            state().presents = REFRESH_PRESENTS;
        }
    });
    ctor.BindEventCallback("change_class", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList&) {
        Model& m = state().model;
        if (!m.editable) {
            m.notice = "Read only: open Organize from the map's R menu (this view opens over it) to change class.";
        }
        else if (m.picked.kind != "ready") {
            m.notice = m.picked.reason;
        }
        else {
            // Through the game's own Change Class screen, so its wardrobe
            // animation plays (developer request 2026-10-30); the view hides
            // while it runs. A new class the game's list does not hold
            // (class_change.hpp) changes directly.
            if (m.picked.class_id >= 81 && !organize_edit::in_game_class_list(m.picked.class_id)) {
                organize_edit::change_class(m.character.record, m.picked.class_id);
            }
            else {
                organize_edit::change_class_shown(m.character.record, m.picked.class_id);
            }
            m.notice = "Changing class...";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("select_class", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) {
            state().model.selected_class = args[0].Get<int>();
            state().presents = REFRESH_PRESENTS;
        }
    });
    ctor.BindEventCallback("cycle_sort", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        Rml::String& sort = state().model.sort;
        sort = sort == "level" ? "class" : sort == "class" ? "name" : "level";
        state().presents = REFRESH_PRESENTS;
    });
    // Edits (organize_edit.cpp) work on the game's Organize working copy, so
    // they need the game's Organize screen open behind the view. Picking a
    // square and then another square of the same unit moves (or swaps) the
    // character; Remove and Unassigned work on the picked square.
    ctor.BindEventCallback("select_square", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (args.size() < 2) {
            return;
        }
        const int slot = args[0].Get<int>();
        const int square = args[1].Get<int>();
        m.selected_unit = slot;
        if (!m.editable) {
            m.notice = "Read only: open Organize from the map's R menu (this view opens over it) to edit units.";
        }
        else if (m.sel_slot == slot && m.sel_square == square) {
            m.sel_slot = m.sel_square = -1;
            m.notice.clear();
        }
        else if (m.sel_slot == slot && m.sel_square >= 0 && square_occupied(m, slot, m.sel_square)) {
            organize_edit::move(slot, m.sel_square, square);
            m.sel_slot = m.sel_square = -1;
            m.notice = "Moving...";
        }
        else if (m.sel_slot >= 0 && m.sel_slot != slot && m.sel_square >= 0 && square_occupied(m, m.sel_slot, m.sel_square)) {
            // Exchange: into another unit's square, swapping with whoever is there.
            organize_edit::exchange(m.sel_slot, m.sel_square, slot, square);
            m.sel_slot = m.sel_square = -1;
            m.notice = "Exchanging...";
        }
        else {
            m.sel_slot = slot;
            m.sel_square = square;
            m.notice = square_occupied(m, slot, square) ? "Pick a square to move to, or Remove."
                                                         : "Pick a character under Unassigned to add here.";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("add_character", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        if (!args.empty() && m.editable && m.new_unit_mode) {
            // The middle square, as the game places a new unit's leader.
            organize_edit::new_unit(args[0].Get<int>(), 4);
            m.new_unit_mode = false;
            m.notice = "Forming a new unit...";
        }
        else if (args.empty() || !m.editable || m.sel_slot < 0 || square_occupied(m, m.sel_slot, m.sel_square)) {
            m.notice = "Pick an empty square in a unit first.";
        }
        else {
            organize_edit::add(m.sel_slot, m.sel_square, args[0].Get<int>());
            m.sel_slot = m.sel_square = -1;
            m.notice = "Adding...";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("command", [](Rml::DataModelHandle handle, Rml::Event&, const Rml::VariantList& args) {
        Model& m = state().model;
        const Rml::String name = args.empty() ? Rml::String() : args[0].Get<Rml::String>();
        const bool picked = m.editable && m.sel_slot >= 0 && square_occupied(m, m.sel_slot, m.sel_square);
        if (!m.editable && (name == "Remove" || name == "Make Leader" || name == "Battle Strategy")) {
            m.notice = "Read only: open Organize from the map's R menu (this view opens over it) to edit units.";
        }
        else if (name == "Remove" && picked) {
            organize_edit::remove(m.sel_slot, m.sel_square);
            m.sel_slot = m.sel_square = -1;
            m.notice = "Removing...";
        }
        else if (name == "Make Leader" && picked) {
            organize_edit::make_leader(m.sel_slot, m.sel_square);
            m.sel_slot = m.sel_square = -1;
            m.notice = "Changing leader...";
        }
        else if (name == "Remove" || name == "Make Leader") {
            m.notice = "Pick a character on a board first.";
        }
        else if (name == "New Unit") {
            m.new_unit_mode = m.editable && !m.new_unit_mode;
            m.notice = !m.editable ? Rml::String("Read only: open Organize from the map's R menu (this view opens over it) to edit units.")
                       : m.new_unit_mode ? Rml::String("Pick a character under Unassigned to lead the new unit.")
                                         : Rml::String();
        }
        else if (name == "Carry Item") {
            m.carry_open = !m.carry_open;
            m.notice = m.carry_open ? "Pick an item to give the selected unit. Click a carried item to put it back."
                                    : Rml::String();
        }
        else if (name == "Battle Strategy" && m.selected_unit >= 0) {
            m.strategy_open = !m.strategy_open;
            m.notice = m.strategy_open ? Rml::String("Pick how the unit chooses its targets.") : Rml::String();
        }
        else if (name == "Replenish") {
            if (!m.editable) {
                m.notice = "Read only: open Organize from the map's R menu (this view opens over it) to replenish.";
            }
            else {
                organize_edit::replenish();
                m.notice = "Replenishing...";
            }
        }
        else if (name == "Set Character") {
            m.notice = "Pick an empty square, then a name under Unassigned.";
        }
        else if (name == "Reposition") {
            m.notice = "Pick a character's square, then the square to move to.";
        }
        else {
            m.notice = name + " comes later. Use the game's own Organize menu for now.";
        }
        state().presents = REFRESH_PRESENTS;
        handle.DirtyAllVariables();
    });
    ctor.BindEventCallback("close", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        state().visible.store(false);
        leave_game_screen();
    });
    s.handle = ctor.GetModelHandle();

    s.context = context;
    s.path = (asset_dir / "organize.rml").string();
    s.document = context->LoadDocument(s.path);
    std::fprintf(stderr, "[organize] view %s\n", s.document != nullptr ? "loaded" : "missing");
    return s.document != nullptr;
}

void update() {
    State& s = state();
    if (s.document == nullptr) {
        return;
    }
    // PORT MENUS = MODERN (settings.hpp -> Choice::FieldControls): the game's
    // Organize screen (scene 0x06, its state word 0x80196B18 set by the enter
    // func_ovlS_801D4BB0) opens this view over it, once per visit. Edits go to
    // the game's working copy as with F3, and the game commits them when its own
    // screen exits (func_ovlS_801C1EC8 -> func_ovlS_801D5328).
    if (const uint8_t* rdram = ultramodern::get_rdram_base(); rdram != nullptr) {
        const bool game_open = rd16(rdram, 0x800E810Eu) == 0x06 && rd32(rdram, 0x80196B18u) != 0;
        if (game_open && !s.auto_opened && choice_index(Choice::FieldControls) == 0) {
            s.auto_opened = true;
            s.visible.store(true);
            std::fprintf(stderr, "[organize] view open over the game's Organize screen\n");
        }
        if (!game_open) {
            s.auto_opened = false;
        }
    }
    const bool visible = s.visible.load() && !organize_edit::driving();
    if (visible && !s.document->IsVisible()) {
        // Re-read organize.rml and organize.rcss from disk on every open, so a
        // layout edit shows after F3 twice, without restarting the game.
        if (Rml::ElementDocument* fresh = (Rml::Factory::ClearStyleSheetCache(), s.context->LoadDocument(s.path))) {
            s.context->UnloadDocument(s.document);
            s.document = fresh;
        }
        s.presents = REFRESH_PRESENTS;
        s.want_focus = true;
        s.document->Show();
    }
    else if (!visible && s.document->IsVisible()) {
        s.document->Hide();
    }
    if (!visible) {
        return;
    }
    const uint8_t* rdram = ultramodern::get_rdram_base();
    if (const int step = s.tab_step.exchange(0); step != 0) {
        static const char* const kTabs[] = {"formation", "characters", "equipment", "items", "armory"};
        constexpr int kCount = 5;
        int now = 0;
        for (int i = 0; i < kCount; i++) {
            now = s.model.tab == kTabs[i] ? i : now;
        }
        s.model.tab = kTabs[((now + step) % kCount + kCount) % kCount];
        s.presents = REFRESH_PRESENTS;
        s.want_focus = true;
    }
    if (rdram != nullptr && ++s.presents >= REFRESH_PRESENTS) {
        s.presents = 0;
        refresh(rdram);
    }
    // A pad needs a focused control before the D-pad can move anywhere. The
    // lists exist after the first refresh has been laid out, so this retries
    // until a control is found.
    // The pad's B, X, Y and Start (sdl_platform.cpp: back, keyboard_key).
    if (const int key = s.pad_key.exchange(PAD_KEY_NONE); key != PAD_KEY_NONE) {
        Model& m = s.model;
        if (key == PAD_KEY_BACK) {
            if (m.keyboard_open) {
                close_keyboard(s);
            }
            else {
                m.carry_open = false;
                m.strategy_open = false;
                m.open_slot = -1;
            }
        }
        else if (m.keyboard_open && key == PAD_KEY_DELETE && !m.rename_text.empty()) {
            m.rename_text.pop_back();
        }
        else if (m.keyboard_open && key == PAD_KEY_SPACE && m.rename_text.size() < 15) {
            m.rename_text += ' ';
        }
        else if (m.keyboard_open && key == PAD_KEY_DONE) {
            close_keyboard(s);
            rename_now(m);
        }
        s.handle.DirtyAllVariables();
    }
    s.closable.store(s.model.keyboard_open || s.model.carry_open || s.model.strategy_open || s.model.open_slot >= 0);
    if (s.focus_keyboard) {
        if (Rml::Element* first = s.document->QuerySelector(".osk .key")) {
            first->Focus(true);
            s.focus_keyboard = false;
        }
    }
    if (s.want_focus) {
        const char* selector = (s.model.tab == "characters" || s.model.tab == "equipment") ? ".cell"
                               : s.model.tab == "items"                                      ? ".itemrow"
                               : s.model.tab == "armory"                                     ? ".setrow"
                                                                                             : ".tile";
        if (Rml::Element* first = s.document->QuerySelector(selector)) {
            first->Focus();
            s.want_focus = false;
        }
    }
}

bool back() {
    State& s = state();
    if (!s.closable.load()) {
        return false;
    }
    s.pad_key.store(PAD_KEY_BACK);
    return true;
}

void keyboard_key(const char* key) {
    const std::string k = key;
    const int code = k == "delete" ? PAD_KEY_DELETE : k == "space" ? PAD_KEY_SPACE : k == "done" ? PAD_KEY_DONE : PAD_KEY_NONE;
    state().pad_key.store(code);
}

void cycle_tab(int direction) {
    state().tab_step.fetch_add(direction >= 0 ? 1 : -1);
}

// PORT MENUS = MODERN: closing the view over the game's Organize screen also
// leaves the game's screen, as its own Exit does (developer report 2026-10-30:
// the game's screen stayed up behind; captures 7-8 of 08:50 show Organize state
// 4 then the world map).
void leave_game_screen() {
    const uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram != nullptr && choice_index(Choice::FieldControls) == 0 && rd16(rdram, 0x800E810Eu) == 0x06 &&
        rd32(rdram, 0x80196B18u) != 0) {
        organize_edit::exit_game_screen();
    }
}

void toggle() {
    State& s = state();
    const bool now = !s.visible.load();
    s.visible.store(now);
    if (!now) {
        leave_game_screen();
    }
    std::fprintf(stderr, "[organize] view %s\n", now ? "open" : "closed");
}

bool open() {
    return state().visible.load() && !organize_edit::driving();
}

}  // namespace ogre::organize_view

