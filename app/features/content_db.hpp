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
//   items        0x8018C42C + i*0x20        278      0x20    func_8016F5D0..func_8016F8A4
//   consumables  0x8018E6EC + i*0x0C         45      0x0C    func_8016F500..func_8016F580
//
// The four tables sit back to back in the streamed segment B data (ROM
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
enum class Table : uint8_t { Classes, Attacks, Items, Consumables, Appearances, Rewards };
constexpr int kTableCount = 6;

struct TableInfo {
    const char* name;   // "classes", "attacks", "items", "consumables", "appearances", "rewards"
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
    std::string id;      // "content/class-rebalance.json"
    std::string source;  // where it came from, for the log and the MODS tab
    std::filesystem::path folder;  // the file's folder, which art paths are relative to
    std::vector<Edit> edits;
    std::vector<NameEdit> names;
    std::vector<NewClass> new_classes;
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
// "items", "consumables"), each an object of records by id ("34" or "0x22"),
// each an object of named fields with integer values. A record may also carry
// "record" (the whole record as hex) and "bytes" ({"0x37": "0902"}: hex at an
// offset). Keys that start with "_" are notes and are skipped. On an error
// the whole file is refused and `error` says where.
//
// A file whose "format" is "LordlyCaliberProject" is a LordlyCaliber project
// (version 1): its "records" become edits of the bytes that differ from the
// ROM's record, after the name pointer, so a project made from either US
// revision applies. Its other sections and its added classes and items are
// listed in Pack::not_applied. Needs load() to have run.
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
// changes). Bytes that no pack writes any more get their ROM value back.
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

// A record's current name: a pack's, else the ROM's. "Short Sword";
// "Altar of\nResurrection" for a two-line name.
std::string record_name(Table table, uint16_t id);

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
// A mod that adds new classes keeps the on/off state it had when the database
// first loaded: new class art is built into the ROM image once at start and new
// class ids go into the saves. The map is mod id -> that state, for every such
// mod; scan() takes it, and the Lua runtime passes the same map, so both load
// the same files. Empty before the database loads.
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
