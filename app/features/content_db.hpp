#pragma once

// The content database (docs/anotherworld/design-content-database.md; roadmap
// phase R0, modding plan M1).
//
// The database holds the game's content records. At start it loads the
// original records from the ROM's tables, then applies the content packs in
// load order. A record is kept as the game's own bytes, in the game's byte
// order (big-endian), and read through named fields.
//
//   table        RAM (US revision 1)       records  stride  verified reader
//   classes      0x80187C34 + c*0x48        165      0x48    func_8016DE3C (+0x04, +0x45)
//   attacks      0x8018AA9C + a*0x10        159      0x10    the class rows (+0x37)
//   spells       0x8018B48C + r*0x08          5      0x08    func_8016E52C (0x8016E94C..)
//   items        0x8018C42C + i*0x20        278      0x20    func_8016F5D0..func_8016F8A4
//   consumables  0x8018E6EC + i*0x0C         45      0x0C    func_8016F500..func_8016F580
//
// The five tables sit back to back in the streamed segment B data (ROM
// 0x40E80 + vram - 0x8016AF80); game_addresses.hpp cites each.
//
// While the recompiled game code still reads those tables, the bridge
// (tick) writes every byte a pack changed into them, every frame, because the
// segment is DMA'd again and brings the ROM bytes back. Bytes no pack changes
// are never written, so a code mod that writes the same table still works.

#include "content_files.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ogre::content {

// The tables in address order (the bridge relies on it).
enum class Table : uint8_t { Classes, Attacks, Spells, Items, Consumables, Appearances, Rewards };
constexpr int kTableCount = 7;

struct TableInfo {
    const char* name;   // "classes", "attacks", "spells", "items", "consumables", "appearances", "rewards"
    uint32_t address;   // record 0 in guest RAM
    uint32_t stride;    // bytes per record
    uint16_t count;     // records in the ROM
    uint32_t rom;       // record 0 in the ROM
    bool named;         // +0x00 is a name pointer
    uint16_t capacity;  // records the database holds: 256 for the tables indexed
                        // by class id (new classes, design-new-classes.md), else count
};

const TableInfo& info(Table table);
std::optional<Table> table_by_name(std::string_view name);

// A named field: `width` bytes (1, 2 or 4), big-endian, at `offset` in the
// record. A JSON pack names fields; a LordlyCaliber record replaces the bytes.
// A bit field (`bits` > 0) is `bits` bits at `shift` inside a 1-byte field.
// docs/anotherworld/design-content-database.md section 2 gives each field's
// reader and source.
struct Field {
    const char* name;
    uint16_t offset;
    uint8_t width;
    bool is_signed;
    uint8_t bits = 0;
    uint8_t shift = 0;
};

std::span<const Field> fields(Table table);
const Field* field(Table table, std::string_view name);

// The generic magic attack of each spells row (game_addresses.hpp kSpellMaps).
constexpr std::array<uint16_t, 5> kSpellRowAttacks = {45, 46, 47, 48, 145};
// True for an attack a spells field may name: an attack of the ROM's table
// that is not itself generic (45..54 or 145; LordlyCaliber refuses the same,
// attack_data.cpp validateSpellLinks). Pack::set_field checks it.
bool spell_target(int64_t attack);

// An attack's status after the hit (record +0x0A..+0x0D). The battle's
// strikes (func_ovlX_801EC2D0 at 0x801ECEA4, func_ovlX_801EF1A8 at 0x801EF598)
// roll it once per target when +0x0A holds kStatusRuleAfterHit: +0x0B is the
// status, +0x0D the rule that decides whether it lands, and
// func_ovlX_801F46FC then applies the target's immunity. The ids are
// LordlyCaliber's (attack_data.cpp statuses(), attack_editor.cpp
// nativeStatusRule) and match the ROM's own uses (Petrify 3, Sacred Breath 7,
// Radiant Gale 8, Crimson Note 6, Evil Dead 4).
constexpr uint8_t kStatusRuleAfterHit = 8;
constexpr uint8_t kNoStatus = 0xFF;  // +0x0A, +0x0B and +0x0D of an attack with none
// A status a pack or a script may give: 3 petrify, 4 sleep, 5 power up,
// 6 power down, 7 undead slaying (a killing blow on undead), 8 paralysis,
// 10 poison, 11 remove ailments. 0 is none.
constexpr bool valid_attack_status(int64_t status) {
    return status == 0 || (status >= 3 && status <= 8) || status == 10 || status == 11;
}
// A status a damage over time may follow (Dot::status): the four the game
// keeps in the fighter's flags (+0x40): 3 petrify (0x02), 4 sleep (0x04),
// 8 paralysis (0x08), 10 poison (0x10) (func_ovlX_801E7DBC).
constexpr bool valid_dot_status(int64_t status) {
    return status == 3 || status == 4 || status == 8 || status == 10;
}
// A status rule (+0x0D): 2 every target, 4 half the hit chance, 5 every hit,
// 6 a killing blow, 11 a quarter of the hit chance.
constexpr bool valid_status_rule(int64_t rule) {
    return rule == 2 || rule == 4 || rule == 5 || rule == 6 || rule == 11;
}
// The JSON names of the statuses and rules ("poison", "half_hit"); nullopt
// for another word.
std::optional<int> attack_status_by_name(std::string_view name);
std::optional<int> status_rule_by_name(std::string_view name);

// The seven class stats, in record order (class +0x08 + 4*stat).
enum Stat : uint8_t { HP, STR, VIT, INT, MEN, AGI, DEX };
constexpr int kStatCount = 7;

// One change to one record: `bytes` written at `offset` in record `id`. Only
// the bits set in `mask` change in each byte (a bit field); the rest keep the
// value below them.
struct Edit {
    Table table;
    uint16_t id;
    uint16_t offset;
    std::vector<uint8_t> bytes;
    uint8_t mask = 0xFFu;
    std::string key;  // a new class's key ("my-mod.dragon-knight"): `id` is resolved when packs apply
    // A new class's key whose id is this one-byte edit's value, resolved when
    // packs apply (a LordlyCaliber class link to an added class).
    std::string value_key;
};

// A new name for a record, in the game's encoding (encode_name). The bridge
// puts it in the content arena and points the record's name pointer at it.
struct NameEdit {
    Table table;
    uint16_t id;
    std::vector<uint8_t> bytes;
    std::string key;  // as Edit::key
};

// A new class: a key, and the existing class its class record and appearance
// row start as a copy of (design-new-classes.md section 5).
struct NewClass {
    std::string key;  // "<mod id>.<name>", e.g. "my-mod.dragon-knight"
    uint16_t copy_of;
    // A "change_class" block: Change Class offers the class (class_change.hpp)
    // to the classes of `family_of`'s line, with these requirements (STR VIT
    // INT MEN AGI DEX minimums, alignment min and max; 0xFF none).
    bool change_class = false;
    uint16_t family_of = 0;
    std::array<uint8_t, 8> requirements{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    // An "art" block: PNG files relative to the pack's folder
    // (docs/anotherworld/design-class-art.md); empty for none.
    std::string art_portrait;
    std::string art_figure_front;
    std::string art_figure_back;
    std::string art_battle;  // a folder of battle palette swatches
};

// A "battle_forms" rule (docs/anotherworld/design-battle-transform.md section
// 6): a fighter of `cls` (or the new class `cls_key`) changes form and attack
// per strike of a battle. Strike i of the battle uses strikes[i]; a strike past
// the list keeps the class.
struct BattleFormStrike {
    // The classes to fight as for this strike: one, or a list the strike picks
    // from at random (never the fighter's previous form when the list has
    // another). Each is an id (`forms`) or a new class key (`form_keys`); both
    // empty keeps the class.
    std::vector<int> forms;
    std::vector<std::string> form_keys;
    // Attack ids for this strike: one, or a list the strike picks from at
    // random (never the fighter's previous pick when the list has another);
    // empty takes the form's attack for the row.
    std::vector<int> attacks;
    bool breath = false;  // "attack": "breath": the form's breath attack, from any row
};

struct BattleForm {
    int cls = -1;
    std::string cls_key;
    int attacks = 0;      // strikes in a battle; 0 keeps the class's count
    bool repeat = false;  // strike i uses strikes[i % strikes.size()]
    std::vector<BattleFormStrike> strikes;
};
constexpr int kMaxBattleStrikes = 9;
constexpr int kMaxBattleForms = 32;    // classes in one strike's "form" list
constexpr int kMaxBattleAttacks = 32;  // attacks in one strike's "attack" list

// A damage over time (a "dots" entry; battle_dot.cpp, design-content-database.md
// section 7.5). A tick deals `power` percent of the game's own damage formula
// (func_ovlX_801F44A0) for the attack that started it, times the stacks.
struct Dot {
    std::string name;         // "poison": 1..32 of a-z, 0-9, '_', '-'
    int power = 20;           // 1..100
    int max_stacks = 1;       // 1..9
    int stacks_on_hit = 1;    // 0..9: stacks a new start adds to a running effect
    int stacks_per_tick = 0;  // 0..9: stacks each tick adds after it (a growing effect)
    bool round = true;        // true: a tick at the end of each round; false: after each of the victim's actions
    int status = 0;           // valid_dot_status: starts when that status lands, from any attack, and ends when it is cured; 0 none
    int attack = 0;           // 1..158: the formula a tick uses when the starting attack deals no damage; 0 none
    int damage_down = 0;      // 0..100: percent less damage the fighter deals, per stack (at most 90 in all)
    int visual = 0;           // 0..255: the battle effect a tick plays on the fighter (func_002267A8's kind); 0 none
    int visual_value = 0;     // 0..65535: its value (kind 14: an attack id, whose visual plays)
    int tint = -1;            // 0..255: the color the fighter flashes at a tick (the damage event's param); -1 the starting strike's own
    // A label the port draws over the fighter at each tick (battle_labels.cpp):
    // the image when there is one, else the text in `label_color`.
    std::string label;             // is_label_text; empty none
    uint32_t label_color = 0xFFFFFFu;  // 0xRRGGBB
    std::string label_image;       // a PNG in the pack's folder (is_art_path); dot_rules gives it as a full path
};
// A label's text: 1..16 of A-Z, a-z, 0-9, space and ! ' . - (no markup).
bool is_label_text(std::string_view text);
constexpr int kMaxDotStacks = 9;
constexpr int kMaxDots = 32;  // effects in one pack

// A story unit: an enemy spawn the enemy features leave as the ROM has it
// (war_director.cpp's garrison, boss_escorts.cpp). `mission` is the mission
// index (0x8018F4A1: 1 Tenne Plains), `slot` the spawn entry's unit slot
// (31..59; 30 is the boss, which is always kept).
struct StoryUnit {
    int mission = 0;
    int slot = 0;
};
constexpr int kMaxStoryUnits = 256;  // in one pack
bool is_dot_name(std::string_view name);

// An attack's "dot" and "dot_chance": a hit starts that effect, or adds a stack.
struct AttackDot {
    std::string dot;
    int chance = 100;  // 0..100
};

// True for an art file name a pack may give: a relative path of 1 to 128
// characters inside the pack's folder (letters, digits, '-', '_', '.', '/'),
// with no ".." part and ending in ".png".
bool is_art_path(std::string_view path);
// The same for a folder of art: no ".png" ending required.
bool is_art_folder(std::string_view path);

// True for a new class key: 3 to 64 of a-z, 0-9, '-', '_', '.' with a '.' in it.
bool is_class_key(std::string_view key);

// The key of a class a LordlyCaliber project added: "lc.<name>.class-<id>",
// where <name> is the project's folder (the file's name without ".json" for a
// loose file), lower-cased, with any other character as '-'. LordlyCaliber
// numbers added classes from 165 by their place in "addedClasses"; the key
// gives each one a port id from 172 that another project's classes cannot
// take (docs/anotherworld/design-lordlycaliber-import.md step 2).
std::string lordlycaliber_class_key(std::string_view pack_id, int lc_id);

// A content pack: a named list of edits. A code pack, a JSON pack and a
// LordlyCaliber project all become one of these.
struct Pack {
    std::string id;      // "content/another-world/class-rebalance.json"
    std::string source;  // where it came from, for the log and the MODS tab
    std::filesystem::path folder;  // the file's folder, which art paths are relative to
    std::vector<Edit> edits;
    std::vector<NameEdit> names;
    std::vector<NewClass> new_classes;
    std::vector<BattleForm> battle_forms;
    // Attack id -> "status_chance" (0..100): the status lands on this share of
    // the target's hits, in place of the attack's status rule (battle_status.cpp).
    std::map<uint16_t, int> status_chances;
    // The "dots" section, and attack id -> its "dot" (battle_dot.cpp).
    std::vector<Dot> dots;
    std::map<uint16_t, AttackDot> attack_dots;
    // The "story_units" section.
    std::vector<StoryUnit> story_units;
    // Parts of the source the port does not apply yet (a LordlyCaliber
    // project's other sections, its added classes and items).
    std::vector<std::string> not_applied;

    // Renames a record: "Short Sword", or "Altar of\nResurrection" (two
    // lines). False with `error` set for a name encode_name refuses.
    bool set_name(Table table, uint16_t id, std::string_view text, std::string& error);
    // Renames a record with bytes already in the game's encoding (a
    // LordlyCaliber classNames/itemNames entry). False for an empty name, one
    // with a NUL, or one longer than two full lines.
    bool set_name_bytes(Table table, uint16_t id, std::span<const uint8_t> bytes);
    // Writes raw bytes into a record.
    void set_bytes(Table table, uint16_t id, uint16_t offset, std::span<const uint8_t> bytes);
    // Replaces a whole record. False when the size is not the table's stride.
    bool set_record(Table table, uint16_t id, std::span<const uint8_t> bytes);
    // Writes a named field. False for an unknown field or a value that does not
    // fit its width.
    bool set_field(Table table, uint16_t id, std::string_view field, int64_t value);
    // After a set_field or a set_name: the entry just added replaces an earlier
    // one for the same field or name, and is dropped when it holds the same
    // bytes, so the pack keeps one entry per target. No two fields share a bit,
    // so this keeps the result of the writes. True when the pack changed. A
    // script's pack uses it on every content.set (lua_runtime.cpp).
    bool keep_one_edit();
    bool keep_one_name();
};

// A read-only view of one record's bytes.
class Record {
public:
    Record(Table table, uint16_t id, const uint8_t* bytes) : table_(table), id_(id), bytes_(bytes) {}

    Table table() const { return table_; }
    uint16_t id() const { return id_; }
    std::span<const uint8_t> bytes() const;

    uint8_t u8(uint16_t offset) const { return bytes_[offset]; }
    int8_t s8(uint16_t offset) const { return static_cast<int8_t>(bytes_[offset]); }
    uint16_t u16(uint16_t offset) const;
    uint32_t u32(uint16_t offset) const;
    // A named field's value; nullopt for an unknown name.
    std::optional<int64_t> get(std::string_view field) const;

protected:
    Table table_;
    uint16_t id_;
    const uint8_t* bytes_;
};

struct ClassRecord : Record {
    using Record::Record;
    uint32_t name_ptr() const { return u32(0x00); }
    uint8_t size() const { return u8(0x04); }  // 1 normal, 2 large
    uint16_t base(Stat stat) const { return u16(0x08 + 4 * stat); }  // lhu in func_8016D4A0..
    uint8_t growth(Stat stat) const { return u8(0x0A + 4 * stat); }
    uint16_t equipment(int slot) const { return u16(0x2E + 2 * slot); }  // 0..3
    uint8_t row_attack(int row) const { return u8(0x37 + 2 * row); }     // 0 front, 1 middle, 2 back
    uint8_t row_count(int row) const { return u8(0x38 + 2 * row); }
    uint8_t key() const { return u8(0x45); }
    uint8_t item_limit() const { return u8(0x47); }
};

struct AttackRecord : Record {
    using Record::Record;
    uint32_t name_ptr() const { return u32(0x00); }
    uint8_t kind() const { return u8(0x04); }
    uint8_t element() const { return u8(0x05); }
    uint8_t formula() const { return u8(0x09); }  // 0x01 physical, 0x07 magic
    uint8_t secondary() const { return u8(0x0A); }    // kStatusRuleAfterHit: a status after the hit
    uint8_t status() const { return u8(0x0B); }       // valid_attack_status
    uint8_t status_rule() const { return u8(0x0D); }  // valid_status_rule
};

struct ItemRecord : Record {
    using Record::Record;
    uint32_t name_ptr() const { return u32(0x00); }
    uint8_t category() const { return u8(0x04); }
    uint8_t element() const { return u8(0x05); }
    uint16_t price() const { return u16(0x08); }
    // STR INT AGI DEX VIT MEN bonuses at +0x0A..+0x0F (signed).
    int8_t stat_bonus(int i) const { return s8(static_cast<uint16_t>(0x0A + i)); }
    bool unique() const { return (u8(0x1A) & 0x80u) != 0; }
};

// A battle reward row (bank N, read by 0x801ADD3C): the class it is for, and
// three outcomes picked with equal chance. An outcome is an equipment id with
// bit 15 set, a consumable id with bit 15 clear, or 0 for nothing.
struct RewardRecord : Record {
    using Record::Record;
    uint16_t class_id() const { return u16(0x00); }
    uint16_t drop(int slot) const { return u16(static_cast<uint16_t>(0x02 + 2 * slot)); }  // 0..2
};

struct ConsumableRecord : Record {
    using Record::Record;
    uint32_t name_ptr() const { return u32(0x00); }
    uint8_t category() const { return u8(0x04); }  // 0 expendable .. 4 quest item
    uint16_t price() const { return u16(0x06); }
};

// Reads one JSON pack. The file is an object of tables ("classes", "attacks",
// "spells", "items", "consumables"), each an object of records by id ("34" or "0x22"),
// each an object of named fields with integer values. A record may also carry
// "record" (the whole record as hex) and "bytes" ({"0x37": "0902"}: hex at an
// offset). Keys that start with "_" are notes and are skipped. On an error
// the whole file is refused and `error` says where.
//
// A file whose "format" is "LordlyCaliberProject" is a LordlyCaliber project
// (version 1): its "records" become edits of the bytes that differ from the
// ROM's record, after the name pointer, so a project made from either US
// revision applies. Its "attacks" section gives attack record edits, names and
// the spell links the spells table holds (read_lordlycaliber_attacks). Its
// other sections and its added classes and items are listed in
// Pack::not_applied. Needs load() to have run.
std::optional<Pack> parse_pack(std::string_view text, std::string id, std::string& error);
// A pack file larger than this is refused unread.
constexpr size_t kMaxPackFileSize = 16u * 1024u * 1024u;

// Loads the original records from the ROM, then the JSON packs: first
// content/*.json beside the executable (the project's own content), then
// user-content/*.json in the settings folder (the player's), each in file-name
// order. A subfolder that holds project.json (a LordlyCaliber project folder)
// loads in the same order, by the folder's name. Idempotent; tick calls it. False when the ROM is not US revision 1.
bool load();
bool loaded();

// Reads the JSON packs again (tick does this when a file in either folder
// changes), and saves/class-ids.json when it was not readable. Bytes that no
// pack writes any more get their ROM value back.
void reload_files();

// The JSON files refused by the last load, with the reason.
struct FileError {
    std::string file;
    std::string error;
};
std::span<const FileError> file_errors();

// The records as the ROM has them, and with every pack applied. Valid after a
// successful load; `id` must be below info(table).count.
Record original(Table table, uint16_t id);
Record current(Table table, uint16_t id);
ClassRecord class_record(uint16_t id);
AttackRecord attack_record(uint16_t id);
ItemRecord item_record(uint16_t id);
ConsumableRecord consumable_record(uint16_t id);
RewardRecord reward_record(uint16_t id);

// A record with the code and JSON packs applied and without the runtime packs.
// A feature that publishes a runtime pack derived from the content (the War
// Director's loot upgrade) builds it from these values.
Record base(Table table, uint16_t id);

// Runtime packs apply after every JSON pack and are replaced while the game
// runs: a feature computes one from the game's state and publishes it under
// its id. Publishing the same id replaces it; an empty pack removes it.
void set_runtime_pack(Pack pack);

// A new class's art files (an "art" block), as paths on disk; empty for none.
struct NewClassArt {
    uint16_t id;
    uint16_t copy_of;
    std::string key;
    std::filesystem::path portrait;
    std::filesystem::path figure_front;
    std::filesystem::path figure_back;
    std::filesystem::path battle;  // a folder: descriptor<d>-bank<b>.png
};
// Every new class with an id and an art block, in load order.
std::vector<NewClassArt> new_class_art();

// The class id a new class key has, or nullopt when it has none yet. Ids
// 172..254 are given out on first sight and kept in saves/class-ids.json
// (165..171 are the game's soldier figures).
std::optional<uint16_t> class_id(std::string_view key);
// How many of the 83 new class ids are taken.
int class_ids_used();

// The battle form rules of every loaded pack with class keys resolved to ids,
// one per class: a later pack's rule for a class replaces an earlier one's. A
// rule whose class or form key has no id is left out. Any thread.
struct BattleFormRule {
    uint16_t cls;
    int attacks;
    bool repeat;
    struct Strike {
        std::vector<int> forms;  // empty keeps the class; more than one: pick at random
        std::vector<int> attacks;  // empty: the form's own; more than one: pick at random
        bool breath;             // the form's breath attack
    };
    std::vector<Strike> strikes;
};
std::vector<BattleFormRule> battle_form_rules();

// An attack's "status_chance" (0..100) from the last loaded pack that gives
// one, or nullopt for the attack's own status rule. Any thread.
std::optional<int> status_chance(uint16_t attack);

// The damage over time effects of every loaded pack, one per name (a later
// pack's replaces an earlier one's), and an attack's "dot" from the last pack
// that gives one. Any thread.
std::vector<Dot> dot_rules();
std::optional<AttackDot> attack_dot(uint16_t attack);

// True for a story unit: the port's own list (units whose class is generic
// but who speak in a mission's dialogue) or any loaded pack's "story_units".
// Any thread.
bool story_unit(int mission, int slot);

// A record's current name: a pack's, else the ROM's. "Short Sword";
// "Altar of\nResurrection" for a two-line name.
std::string record_name(Table table, uint16_t id);
// The ROM's name of a record in the game's encoding, without the NUL; empty
// for an unnamed table or a name outside segment B.
std::vector<uint8_t> original_name(Table table, uint16_t id);

// The game's name encoding. A one-line name is its ASCII bytes. A two-line
// name is 0x0E, line 1, 0x10 'c', line 2, 0x0F (the ROM's own two-line names;
// LordlyCaliber encodeClassName). encode_name takes "line 1\nline 2" with 1
// to 32 printable ASCII characters per line (LordlyCaliber's item-name limit;
// the longest line in the ROM is 16, so a longer one may not fit a window);
// decode_name drops the framing and turns the break into '\n'. Every table
// has two-line names in the ROM (52 items, 26 consumables, 18 classes).
std::optional<std::vector<uint8_t>> encode_name(Table table, std::string_view text, std::string& error);
std::string decode_name(std::span<const uint8_t> bytes);

// The ROM's records of one table as JSON in the pack format: each record's
// "_name", every named field with its value, and "_hex" (the whole record).
// load() writes the four tables to content-dump/ in the settings folder at
// every start, so a modder can see the values and copy records into a pack.
std::string dump_table(Table table);

// Adds a pack from code. Code packs apply before the JSON packs, in the order
// they were added. False when an edit names a record or a byte past its table.
bool add_pack(Pack pack);
// Every pack in load order: the code packs, then the JSON packs.
std::span<const Pack> packs();

// Two packs that change the same record: the later one wins.
struct Conflict {
    Table table;
    uint16_t id;
    std::string earlier;
    std::string later;
};
std::span<const Conflict> conflicts();

// The MODS tab's lines, as (left, right) pairs: every pack in load order with
// what it changes and what it does not apply, every refused file with its
// reason, and every conflict. Main thread.
std::vector<std::pair<std::string, std::string>> status();

// --- mods (docs/anotherworld/design-mods.md section 5) ------------------------
// A mod that adds new classes, or ships with the game in content/, keeps the
// on/off state it had when the database first loaded: new class art is built
// into the ROM image once at start, new class ids go into the saves, and a
// shipped pack's rules change saved state (design-another-world-pack.md
// section 3.3). The map is mod id -> that state, for every such mod; scan()
// takes it, and the Lua runtime passes the same map, so both load the same
// files. Empty before the database loads.
std::map<std::string, bool> held_mods();

// The mods as of the last read, in load order, and why mod-settings.json was
// ignored then (empty when it was read or absent). `generation` counts the
// reads, so the MODS tab can tell when to rebuild.
std::vector<files::Mod> mods();
std::string mod_settings_error();
// Item id -> the number of new classes its packs add (loaded or not).
std::map<std::string, int> class_counts();
int generation();

// Held while the packs are applied (every change to the current records goes
// through that step). Packs are applied on the main thread; code on another
// thread (a Lua handler of a game event) takes this lock while it reads records.
std::recursive_mutex& records_mutex();

// True while segment B (classes, attacks, items, consumables) is in RAM: class
// 1's name pointer reads the ROM's value (0x80190388, "Soldier") or the one a
// pack renamed it to. A feature that writes or reads those tables uses this
// instead of comparing a name pointer itself, so a renamed record does not
// hide the segment.
bool segment_b_resident(const uint8_t* rdram);

// The bridge. Called once per frame on the main thread: writes every byte a
// pack changed into the game's tables while segment B is resident, and about
// once a second checks the content folders for changed files.
void tick(uint8_t* rdram);

}  // namespace ogre::content
