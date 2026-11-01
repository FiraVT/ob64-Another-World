// The auto commander: an AI for the player's side of a mission
// (docs/anotherworld/design-auto-commander.md). It has the shape of the enemy
// commander (enemy_commander.cpp): every DECIDE_HOURS of game time, on the field
// update (game thread), it reads the field and gives each of its units one job,
// highest priority first:
//
//   RETREAT    HP below RETREAT_LINE, or a stronger enemy close by: to the held
//              town that leads away from the threat; then REST
//   REST       HP below REST_LINE: into the nearest held town (units recover
//              everywhere and faster in a town; developer, 2026-10-31) until
//              RESUME_LINE
//   DEFEND     a shown enemy unit near a held town or the headquarters: the
//              nearest unit strong enough chases it
//   TAKE TOWN  an untaken town: the nearest healthy unit whose liberation
//              verdict (liberation.hpp) fits the AUTO ENDING setting
//   ASSAULT    an enemy stronger than one unit (one that beat a unit, a
//              garrison, a defender's target, or the boss once no town is
//              left): a relay of units from a held town near it (section 4a)
//   TRAIN      during an assault's training step: units fight enemies they
//              can beat
//   COLLECT    a hidden item (read_spots): one idle unit at a time while towns
//              are left, then one unit per item before the boss
//   GUARD      nothing else: the held town nearest the enemy
//
// The AI never gives up an assault (developer, 2026-10-31). When three
// battles in a row take less than a tenth of the target's HP, it escalates one
// step at a time and keeps attacking: (1) the waves swap Battle Strategy;
// (2) and (3) more units join the relay; (4) the assault pauses for
// TRAIN_HOURS while units train, then starts again at step 1 with the bigger
// relay.
//
// HEAL: before anything else, a unit with a member below HEAL_LINE uses a Heal
// Leaf or Heal Seed it carries, the way the game's item screen does (heal()).
//
// Not built yet, each waiting on research (the design doc's section 7): Heal
// Pack and the revive items (their item-screen paths are not read), shopping
// and the Witch's Den (R10 town entry, R12 the shop's buy routine), waiting for
// the time of day (R18). Battles are seen from the HP of
// the attacker and the target between decisions, until R16 gives the battle's
// own result.
//
// Power. A unit's power is its members' current HP. An enemy's is scaled by the
// extra members boss_escorts.cpp gives it (the field does not show them). Enemy
// levels are not read: the enemy record's level offset is not verified.
//
// Orders go through the game's move routine func_ovlN_801CC5D0, after
// func_ovlN_801D45F8 ends any pursuit (field_select.cpp), and a chase is
// re-aimed every REAIM_EVERY field updates. Battle Strategy is unit record
// +0x17 (0 Autonomous, 1 Attack Strongest, 2 Attack Leader, 3 Attack Weakest;
// field_view.cpp kStrategies).

#include "auto_commander.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "alerts.hpp"
#include "boss_escorts.hpp"
#include "field_select.hpp"
#include "game_addresses.hpp"
#include "game_events.hpp"
#include "liberation.hpp"
#include "options.hpp"
#include "recomp.h"

namespace ogre::auto_commander {

namespace {

constexpr uint32_t SCENE = ::ogre::addr::kActiveScene;
constexpr int SCENE_FIELD = 0x03;
constexpr uint32_t FIELD_MODE = 0x801F0E00u;  // 0: the open field, no menu
constexpr uint32_t OBJECTS = ::ogre::addr::kMapObjects;
constexpr uint32_t UNITS = ::ogre::addr::kUnits;
constexpr uint32_t UNIT_STRIDE = ::ogre::addr::kUnitSize;
constexpr uint32_t PLAYER_CHARS = ::ogre::addr::kCharacters;
constexpr uint32_t ENEMY_CHARS = ::ogre::addr::kEnemyCharacters;
constexpr uint32_t MISSION_INDEX = ::ogre::addr::kMissionIndex;
constexpr uint32_t GAME_CLOCK = ::ogre::addr::kGameClock;  // hours, 16.16
constexpr uint32_t LOCATION_COUNT = ::ogre::addr::kLocationCount;
constexpr uint32_t LOCATION_STATE = ::ogre::addr::kLocationState;
constexpr uint32_t LOCATION_STRIDE = ::ogre::addr::kLocationSize;
constexpr uint32_t MAP_DATA_TABLE = ::ogre::addr::kMapDataTable;
constexpr uint32_t MAP_ID = ::ogre::addr::kMapId;
constexpr uint32_t MOVE_TO = 0x801CC5D0u;
constexpr uint32_t END_PURSUIT = 0x801D45F8u;
constexpr int PLAYER_UNITS = 30;
constexpr int BOSS_SLOT = boss_escorts::BOSS_SLOT;
constexpr int SLOTS = 60;
constexpr int MAX_LOCATIONS = 64;
constexpr int REAIM_EVERY = 24;

// Object flags (field_view.cpp): 0x11 on the map, 0x20 shown, 0x80000000
// beaten.
constexpr uint32_t SHOWN = 0x20u;
constexpr uint32_t BEATEN = 0x80000000u;

constexpr double DECIDE_HOURS = 2.0;
// Between two waves of an assault. Enemy units keep lost HP between battles
// but recover it fast (developer, 2026-10-31), so the waves follow closely.
constexpr double WAVE_GAP_HOURS = 1.0;
constexpr double ITEM_WAIT_HOURS = 48.0; // the boss waits this long for hidden items
constexpr double TRAIN_HOURS = 48.0;     // escalation step 4
constexpr float RETREAT_LINE = 0.30f;
constexpr float REST_LINE = 0.50f;
constexpr float RESUME_LINE = 0.90f;
constexpr float HEAL_LINE = 0.60f;       // a member below this share gets an item
constexpr float MARGIN = 1.2f;           // power needed to fight alone
constexpr float STALL_SHARE = 0.10f;     // three battles below this escalate
constexpr int STALL_BATTLES = 3;

enum Strategy : uint8_t { Autonomous = 0, Strongest = 1, Leader = 2, Weakest = 3 };

uint8_t rd8(const uint8_t* rdram, uint32_t a) {
    return rdram[(a - 0x80000000u) ^ 3u];
}

uint16_t rd16(const uint8_t* rdram, uint32_t a) {
    return static_cast<uint16_t>((rd8(rdram, a) << 8) | rd8(rdram, a + 1));
}

uint32_t rd32(const uint8_t* rdram, uint32_t a) {
    return (static_cast<uint32_t>(rd16(rdram, a)) << 16) | rd16(rdram, a + 2);
}

void wr8(uint8_t* rdram, uint32_t a, uint8_t v) {
    rdram[(a - 0x80000000u) ^ 3u] = v;
}

float rdf(const uint8_t* rdram, uint32_t a) {
    const uint32_t bits = rd32(rdram, a);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

bool in_ram(uint32_t a) {
    return a >= 0x80000000u && a < 0x80800000u;
}

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

float dist2(float ax, float az, float bx, float bz) {
    return (ax - bx) * (ax - bx) + (az - bz) * (az - bz);
}

uint32_t object_of(const uint8_t* rdram, int slot) {
    return rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
}

// --- The field --------------------------------------------------------------

struct Unit {
    int slot = -1;
    float x = 0.0f, z = 0.0f;
    int hp = 0, hp_max = 0;
    int members = 0;
    int alignment = 0;  // the members' average (the liberation rule's input)
    float power = 0.0f;
    float share() const { return hp_max > 0 ? static_cast<float>(hp) / hp_max : 1.0f; }
};

struct Place {
    int index = -1;
    float x = 0.0f, z = 0.0f;
    uint16_t state = 0;     // 0x02 held, 0x04 enemy-held, 0x10 headquarters
    uint8_t alignment = 0;  // record +0x02; 0xFF cannot be taken
    std::string name;
    bool held() const { return (state & 0x12u) != 0; }
    bool takeable() const { return !held() && alignment != 0xFF; }
};

bool on_map(const uint8_t* rdram, int slot) {
    const uint32_t o = object_of(rdram, slot);
    if (!in_ram(o)) {
        return false;
    }
    const uint32_t flags = rd32(rdram, o);
    return (flags & 0x11u) == 0x11u && (flags & BEATEN) == 0;
}

Unit read_unit(const uint8_t* rdram, int slot) {
    Unit u;
    u.slot = slot;
    const uint32_t o = object_of(rdram, slot);
    u.x = rdf(rdram, o + 0x08);
    u.z = rdf(rdram, o + 0x10);
    const uint32_t record = UNITS + static_cast<uint32_t>(slot) * UNIT_STRIDE;
    const bool enemy = slot >= PLAYER_UNITS;
    for (uint32_t i = 0; i < 5; i++) {
        const int ref = rd8(rdram, record + 2 + i);
        if (ref < 1 || ref >= 100) {
            continue;
        }
        const uint32_t c = enemy ? ENEMY_CHARS + static_cast<uint32_t>(ref) * 0x34u
                                 : PLAYER_CHARS + static_cast<uint32_t>(ref) * 0x38u;
        u.hp_max += rd16(rdram, c + 0x16);
        u.hp += rd16(rdram, c + 0x18);
        if (!enemy) {
            u.alignment += rd8(rdram, c + 0x1B);
        }
        u.members++;
    }
    if (u.members > 0) {
        u.alignment /= u.members;
    }
    u.power = static_cast<float>(u.hp);
    if (enemy && u.members > 0) {
        const int extra = boss_escorts::extra_members(rdram, slot);
        u.power *= static_cast<float>(u.members + extra) / static_cast<float>(u.members);
    }
    return u;
}

std::vector<Place> read_places(const uint8_t* rdram) {
    std::vector<Place> out;
    const int count = static_cast<int>(rd32(rdram, LOCATION_COUNT));
    const uint32_t map_data = rd32(rdram, MAP_DATA_TABLE + 4u * rd8(rdram, MAP_ID));
    if (count <= 0 || count > MAX_LOCATIONS || !in_ram(map_data)) {
        return out;
    }
    for (int i = 0; i < count; i++) {
        const uint32_t s = LOCATION_STATE + static_cast<uint32_t>(i) * LOCATION_STRIDE;
        Place p;
        p.index = i;
        p.state = rd16(rdram, s);
        p.alignment = rd8(rdram, s + 2);
        p.x = rdf(rdram, map_data + 0x1Cu + static_cast<uint32_t>(i) * 8u);
        p.z = rdf(rdram, map_data + 0x20u + static_cast<uint32_t>(i) * 8u);
        for (uint32_t k = 0; k < 20; k++) {
            const uint8_t c = rd8(rdram, s + 8 + k);
            if (c == 0) {
                break;
            }
            p.name += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ';
        }
        if (p.name.empty()) {
            p.name = (p.state & 0x10u) ? "the headquarters" : "location " + std::to_string(i + 1);
        }
        out.push_back(p);
    }
    return out;
}

// The map's hidden items (table P, docs/notes/drop-table.md 2), read the way
// the field's own pick-up check reads them (bank N 0x801B5848..0x801B59B0, run
// for each player unit): count u8 0x801F1002; for item i, a found-flag index
// u8 0x801F1044[i], whose bit (index & 7) of byte 0x80196A58 + index / 8 is
// set once it is picked up; its position is the bytes 0x801F1024[i] (x) and
// 0x801F1034[i] (z) as 256ths of the field rectangle at 0x801F0DB8 (min x,
// min z, max x, max z floats): x = min_x + (max_x - min_x) * byte / 256. A unit
// picks the item up when its position (object +0x08, +0x10) is inside a square
// of half-width *(0x801F3A58) / 2 around that point (the c.lt.s tests at
// 0x801B5938..0x801B597C), so walking over it is enough. Item ids are
// 0x801F1004[i] (u16).
constexpr uint32_t ITEM_COUNT = 0x801F1002u;
constexpr uint32_t ITEM_X = 0x801F1024u;
constexpr uint32_t ITEM_Z = 0x801F1034u;
constexpr uint32_t ITEM_FLAG = 0x801F1044u;
constexpr uint32_t ITEM_FOUND = 0x80196A58u;
constexpr uint32_t FIELD_RECT = 0x801F0DB8u;
constexpr int MAX_ITEMS = 16;  // the arrays are 0x10 bytes apart

struct Spot {
    int index = -1;
    float x = 0.0f, z = 0.0f;
};

std::vector<Spot> read_spots(const uint8_t* rdram) {
    std::vector<Spot> out;
    const int count = std::min<int>(rd8(rdram, ITEM_COUNT), MAX_ITEMS);
    const float min_x = rdf(rdram, FIELD_RECT), min_z = rdf(rdram, FIELD_RECT + 4);
    const float max_x = rdf(rdram, FIELD_RECT + 8), max_z = rdf(rdram, FIELD_RECT + 12);
    if (!(max_x > min_x) || !(max_z > min_z)) {
        return out;
    }
    for (int i = 0; i < count; i++) {
        const int flag = rd8(rdram, ITEM_FLAG + static_cast<uint32_t>(i));
        if ((rd8(rdram, ITEM_FOUND + static_cast<uint32_t>(flag / 8)) >> (flag % 8)) & 1) {
            continue;
        }
        Spot sp;
        sp.index = i;
        sp.x = min_x + (max_x - min_x) * rd8(rdram, ITEM_X + static_cast<uint32_t>(i)) / 256.0f;
        sp.z = min_z + (max_z - min_z) * rd8(rdram, ITEM_Z + static_cast<uint32_t>(i)) / 256.0f;
        out.push_back(sp);
    }
    return out;
}

// --- State --------------------------------------------------------------------

enum class Job { None, Retreat, Rest, Defend, TakeTown, Assault, Stage, Train, Collect, Guard };

const char* job_name(Job j) {
    switch (j) {
        case Job::Retreat: return "retreat";
        case Job::Rest: return "rest";
        case Job::Defend: return "defend";
        case Job::TakeTown: return "take town";
        case Job::Assault: return "assault";
        case Job::Stage: return "stage";
        case Job::Train: return "train";
        case Job::Collect: return "collect";
        case Job::Guard: return "guard";
        default: return "none";
    }
}

struct Order {
    Job job = Job::None;
    int place = -1;   // a location; for COLLECT, -(1000 + the hidden item's index)
    int target = -1;  // an enemy slot (chased)
    float x = 0.0f, z = 0.0f;
    std::string text;  // for the unit card
};

struct Assault {
    int target = -1;
    int base = -1;              // the held place the relay rests at
    int escalation = 0;         // 0 none yet, 1..4 (see the file comment)
    int wave_size = 1;          // 2 for the boss
    int battles = 0;            // battles in the current window
    int window_hp = 0;          // the target's HP when the window began
    double last_wave = -1e9;
    double train_until = -1.0;  // step 4: training until this hour
    std::map<int, int> attacker_hp;  // attacker slot -> its HP at the last decision
    int target_hp = 0;               // the target's HP at the last decision
};

struct State {
    int mission = -1;
    double next_decision = 0.0;
    int reaim = 0;
    std::map<int, Order> orders;
    std::set<int> released;  // units the player took back
    Assault assault;
    double towns_done_at = -1.0;  // when no town was left to take
};

std::mutex g_mutex;  // guards g (the game thread decides; the render thread reads job texts)
State g;

double now_hours(const uint8_t* rdram) {
    return rd32(rdram, GAME_CLOCK) / 65536.0;
}

void move_to(uint8_t* rdram, recomp_context* ctx, int slot, float x, float z) {
    const uint32_t object = object_of(rdram, slot);
    const recomp_context saved = *ctx;
    set_arg(ctx->r4, object);
    LOOKUP_FUNC(END_PURSUIT)(rdram, ctx);
    *ctx = saved;
    uint32_t bx, bz;
    std::memcpy(&bx, &x, sizeof(bx));
    std::memcpy(&bz, &z, sizeof(bz));
    set_arg(ctx->r4, object);
    set_arg(ctx->r5, bx);
    set_arg(ctx->r6, rd32(rdram, object + 0x0C));  // keep the unit's own height
    set_arg(ctx->r7, bz);
    LOOKUP_FUNC(MOVE_TO)(rdram, ctx);
    *ctx = saved;
}

void set_strategy(uint8_t* rdram, int slot, uint8_t strategy) {
    wr8(rdram, UNITS + static_cast<uint32_t>(slot) * UNIT_STRIDE + 0x17u, strategy);
}

const char* strategy_name(uint8_t s) {
    static const char* const kNames[4] = {"Autonomous", "Attack Strongest", "Attack Leader", "Attack Weakest"};
    return s < 4 ? kNames[s] : "?";
}

int ending_choice() {
    return choice_index(Choice::AutoEnding);  // 0 LIBERATE, 1 CAPTURE, 2 EITHER
}

bool fits_ending(int alignment, const Place& p) {
    const liberation::Verdict v = liberation::verdict(alignment, p.alignment);
    switch (ending_choice()) {
        case 0: return v == liberation::Verdict::Liberates;
        case 1: return v == liberation::Verdict::Captures;
        default: return v != liberation::Verdict::MayCapture;
    }
}

// --- Healing items --------------------------------------------------------------
//
// The game's item screen (bank AE, the R menu's item entry) uses an item on a
// character in three steps, and HEAL repeats them:
//
// 1. amount = func_8016B198(0, 0, item, character) (0x80216844, kept at
//    0x8021E384). For Heal Leaf (1) it is 100 and for Heal Seed (2) 300
//    (0x8016B1EC..0x8016B204).
// 2. func_8016B198(1, amount, item, character) (0x802172B0): for a player
//    character (index < 100) HP +0x18 += amount, capped at +0x16, and nothing
//    for a character at 0 HP (0x8016B30C..0x8016B338).
// 3. The item leaves the unit (0x80216FC8..0x80217058): k = func_8016B738(item),
//    the item's record in the 40-slot list 0x80193AE0 (4 bytes: u16 id, u8 in
//    use, u8 count; 511 when absent); the unit's carried slot (unit record
//    +0x0D + slot) is cleared and the record's in-use byte (+2) and count (+3)
//    each go down by one. (An item from the army's stock instead of a unit's
//    hands, flag 0x8021E3E3 set, only lowers the count; the AI uses carried
//    items, which the player chooses in Organize.)
//
// Heal Pack (3, every member) takes another path through the item screen
// (0x80216860, 0x80217228) and is left out until it is read.
constexpr uint32_t USE_ITEM = 0x8016B198u;
constexpr uint32_t ITEM_LIST = 0x80193AE0u;
constexpr uint32_t CARRIED = 0x0Du;  // unit record: 10 carried consumable ids
constexpr int HEAL_LEAF = 1;
constexpr int HEAL_SEED = 2;

int list_record(const uint8_t* rdram, int item) {
    for (int k = 0; k < 40; k++) {
        if (rd16(rdram, ITEM_LIST + static_cast<uint32_t>(k) * 4u) == item) {
            return k;
        }
    }
    return 511;
}

int use_item(uint8_t* rdram, recomp_context* ctx, int item, int character, bool apply, int amount) {
    const recomp_context saved = *ctx;
    set_arg(ctx->r4, apply ? 1u : 0u);
    set_arg(ctx->r5, static_cast<uint32_t>(amount));
    set_arg(ctx->r6, static_cast<uint32_t>(item));
    set_arg(ctx->r7, static_cast<uint32_t>(character));
    LOOKUP_FUNC(USE_ITEM)(rdram, ctx);
    const int result = static_cast<int>(static_cast<int32_t>(ctx->r2));
    *ctx = saved;
    return result;
}

// One healing item from the unit's own items on its most hurt living member,
// when one is below HEAL_LINE. Heal Seed when the member misses 200 HP or more
// and the unit carries one, else Heal Leaf, else Heal Seed. Returns the text
// for the alert, or "".
std::string heal(uint8_t* rdram, recomp_context* ctx, int slot) {
    const uint32_t record = UNITS + static_cast<uint32_t>(slot) * UNIT_STRIDE;
    int worst = -1, worst_missing = 0;
    for (uint32_t i = 0; i < 5; i++) {
        const int ref = rd8(rdram, record + 2 + i);
        if (ref < 1 || ref >= 100) {
            continue;
        }
        const uint32_t c = PLAYER_CHARS + static_cast<uint32_t>(ref) * 0x38u;
        const int hp_max = rd16(rdram, c + 0x16), hp = rd16(rdram, c + 0x18);
        if (hp == 0 || hp_max == 0 || hp >= hp_max * HEAL_LINE) {
            continue;
        }
        if (hp_max - hp > worst_missing) {
            worst = ref;
            worst_missing = hp_max - hp;
        }
    }
    if (worst < 0) {
        return "";
    }
    int leaf = -1, seed = -1;
    for (uint32_t i = 0; i < 10; i++) {
        const int item = rd8(rdram, record + CARRIED + i);
        leaf = (item == HEAL_LEAF && leaf < 0) ? static_cast<int>(i) : leaf;
        seed = (item == HEAL_SEED && seed < 0) ? static_cast<int>(i) : seed;
    }
    const int carried = (worst_missing >= 200 && seed >= 0) ? seed : (leaf >= 0 ? leaf : seed);
    if (carried < 0) {
        return "";
    }
    const int item = rd8(rdram, record + CARRIED + static_cast<uint32_t>(carried));
    const int k = list_record(rdram, item);
    if (k >= 40 || rd8(rdram, ITEM_LIST + static_cast<uint32_t>(k) * 4u + 3u) == 0) {
        return "";
    }
    const uint32_t c = PLAYER_CHARS + static_cast<uint32_t>(worst) * 0x38u;
    const int before = rd16(rdram, c + 0x18);
    const int amount = use_item(rdram, ctx, item, worst, false, 0);
    use_item(rdram, ctx, item, worst, true, amount);
    wr8(rdram, record + CARRIED + static_cast<uint32_t>(carried), 0);
    const uint32_t entry = ITEM_LIST + static_cast<uint32_t>(k) * 4u;
    wr8(rdram, entry + 2u, static_cast<uint8_t>(rd8(rdram, entry + 2u) - 1));
    wr8(rdram, entry + 3u, static_cast<uint8_t>(rd8(rdram, entry + 3u) - 1));
    const int after = rd16(rdram, c + 0x18);
    std::fprintf(stderr, "[autocmd] unit %d: item %d on character %d, HP %d -> %d (amount %d)\n", slot + 1, item,
                 worst, before, after, amount);
    return std::string(item == HEAL_LEAF ? "Heal Leaf" : "Heal Seed") + " (+" + std::to_string(after - before) + " HP)";
}

// --- Decisions ----------------------------------------------------------------

void decide(uint8_t* rdram, recomp_context* ctx, double hours, const std::vector<int>& selection) {
    auto selected = [&](int slot) { return std::find(selection.begin(), selection.end(), slot) != selection.end(); };

    std::vector<Unit> mine;
    std::vector<std::string> healed;
    for (int s = 0; s < PLAYER_UNITS; s++) {
        if (on_map(rdram, s) && g.released.count(s) == 0 && !selected(s)) {
            // HEAL first: an item is cheaper than a walk back to a town.
            if (const std::string used = heal(rdram, ctx, s); !used.empty()) {
                healed.push_back("Unit " + std::to_string(s + 1) + " used a " + used + ".");
            }
            const Unit u = read_unit(rdram, s);
            if (u.members > 0) {
                mine.push_back(u);
            }
        }
    }
    std::vector<Unit> foes;
    for (int s = PLAYER_UNITS; s < SLOTS; s++) {
        if (!on_map(rdram, s)) {
            continue;
        }
        // Only enemies the game shows, as the player sees them; the boss is
        // always known (its stronghold is on the map).
        if (s != BOSS_SLOT && (rd32(rdram, object_of(rdram, s)) & SHOWN) == 0) {
            continue;
        }
        const Unit u = read_unit(rdram, s);
        if (u.members > 0) {
            foes.push_back(u);
        }
    }
    const std::vector<Place> places = read_places(rdram);
    auto foe_by_slot = [&](int slot) -> const Unit* {
        for (const Unit& f : foes) {
            if (f.slot == slot) {
                return &f;
            }
        }
        return nullptr;
    };

    // Map scale: the spread of the mission's towns (enemy_commander.cpp).
    float lo_x = 1e30f, hi_x = -1e30f, lo_z = 1e30f, hi_z = -1e30f;
    for (const Place& pl : places) {
        lo_x = std::min(lo_x, pl.x);
        hi_x = std::max(hi_x, pl.x);
        lo_z = std::min(lo_z, pl.z);
        hi_z = std::max(hi_z, pl.z);
    }
    const float width = places.empty() ? 20.0f : std::max(4.0f, std::max(hi_x - lo_x, hi_z - lo_z));
    const float near2 = (0.15f * width) * (0.15f * width);
    const float town2 = (0.05f * width) * (0.05f * width);

    std::map<int, Order> next;
    std::vector<std::string> alerts = healed;
    auto is_free = [&](const Unit& u) { return next.count(u.slot) == 0; };
    auto assign = [&](const Unit& u, Job job, int place, int target, float x, float z, std::string text) {
        next[u.slot] = Order{job, place, target, x, z, std::move(text)};
    };
    auto nearest_held = [&](float x, float z, const Unit* threat) -> const Place* {
        const Place* best = nullptr;
        float best_score = 0.0f;
        for (const Place& pl : places) {
            if (!pl.held()) {
                continue;
            }
            float score = std::sqrt(dist2(x, z, pl.x, pl.z));
            if (threat != nullptr) {
                // A town the threat is closer to than the unit is a town it
                // would run into.
                const float mine_d = std::sqrt(dist2(x, z, pl.x, pl.z));
                const float its_d = std::sqrt(dist2(threat->x, threat->z, pl.x, pl.z));
                score += 2.0f * std::max(0.0f, mine_d - its_d);
            }
            if (best == nullptr || score < best_score) {
                best = &pl;
                best_score = score;
            }
        }
        return best;
    };
    auto threat_near = [&](const Unit& u) -> const Unit* {
        const Unit* worst = nullptr;
        for (const Unit& f : foes) {
            if (dist2(u.x, u.z, f.x, f.z) <= near2 * 0.5f && f.power > u.power &&
                (worst == nullptr || f.power > worst->power)) {
                worst = &f;
            }
        }
        return worst;
    };

    // Battles the assault fought since the last decision: an attacker close to
    // the target whose HP, or the target's, fell.
    Assault& a = g.assault;
    const Unit* target = a.target >= 0 ? foe_by_slot(a.target) : nullptr;
    if (a.target >= 0 && target == nullptr && !on_map(rdram, a.target)) {
        alerts.push_back(a.target == BOSS_SLOT ? "The boss is defeated." : "The assault's target is defeated.");
        std::fprintf(stderr, "[autocmd] assault on slot %d over: target gone\n", a.target);
        a = Assault{};
    }
    if (target != nullptr) {
        for (const auto& [slot, hp_before] : a.attacker_hp) {
            const auto it = std::find_if(mine.begin(), mine.end(), [&](const Unit& u) { return u.slot == slot; });
            const bool hurt = it != mine.end() && it->hp < hp_before;
            const bool close = it != mine.end() && dist2(it->x, it->z, target->x, target->z) <= near2 * 0.25f;
            if ((hurt || target->hp < a.target_hp) && (close || it == mine.end())) {
                a.battles++;
            }
        }
        if (a.battles >= STALL_BATTLES) {
            const float taken = static_cast<float>(a.window_hp - target->hp) / std::max(1, target->hp_max);
            if (taken < STALL_SHARE) {
                a.escalation = a.escalation % 4 + 1;
                std::string what;
                switch (a.escalation) {
                    case 1: what = "the next waves change their Battle Strategy"; break;
                    case 2:
                    case 3:
                        a.wave_size = std::min(a.wave_size + 1, 4);
                        what = "more units join the attack";
                        break;
                    default:
                        a.train_until = hours + TRAIN_HOURS;
                        what = "units train on weaker enemies first";
                        break;
                }
                char text[200];
                std::snprintf(text, sizeof(text), "%d battles took %d%% of the target's HP: %s.", a.battles,
                              static_cast<int>(taken * 100.0f), what.c_str());
                alerts.push_back(text);
                std::fprintf(stderr, "[autocmd] escalation %d: %s\n", a.escalation, text);
            }
            a.battles = 0;
            a.window_hp = target->hp;
        }
    }

    // RETREAT and REST first: a hurt unit goes nowhere else.
    for (const Unit& u : mine) {
        const Unit* threat = threat_near(u);
        const auto old = g.orders.find(u.slot);
        const bool resting = old != g.orders.end() && (old->second.job == Job::Rest || old->second.job == Job::Retreat);
        const bool attacking = old != g.orders.end() && old->second.job == Job::Assault;
        // An attacker accepts the danger of its own target; others run from it.
        const bool flee = threat != nullptr && !(attacking && threat->slot == a.target);
        if (u.share() < RETREAT_LINE || flee) {
            if (const Place* pl = nearest_held(u.x, u.z, threat)) {
                assign(u, Job::Retreat, pl->index, -1, pl->x, pl->z, "Retreating to " + pl->name);
            }
        }
        else if (u.share() < REST_LINE || (resting && u.share() < RESUME_LINE)) {
            const Place* pl = nullptr;
            if (a.base >= 0 && static_cast<size_t>(a.base) < places.size() && places[a.base].held()) {
                pl = &places[a.base];
            }
            if (pl == nullptr || dist2(u.x, u.z, pl->x, pl->z) > near2 * 4.0f) {
                pl = nearest_held(u.x, u.z, nullptr);
            }
            if (pl != nullptr) {
                assign(u, Job::Rest, pl->index, -1, pl->x, pl->z, "Resting at " + pl->name);
            }
        }
    }
    auto healthy = [&](const Unit& u) { return is_free(u) && u.share() >= RESUME_LINE * 0.75f; };
    auto nearest_healthy = [&](float x, float z, float min_power) -> const Unit* {
        const Unit* best = nullptr;
        for (const Unit& u : mine) {
            if (healthy(u) && u.power >= min_power &&
                (best == nullptr || dist2(u.x, u.z, x, z) < dist2(best->x, best->z, x, z))) {
                best = &u;
            }
        }
        return best;
    };
    auto start_assault = [&](const Unit& foe, const std::string& why) {
        if (a.target >= 0) {
            return;
        }
        a = Assault{};
        a.target = foe.slot;
        a.wave_size = foe.slot == BOSS_SLOT ? 2 : 1;
        a.window_hp = foe.hp;
        a.target_hp = foe.hp;
        alerts.push_back(why);
        std::fprintf(stderr, "[autocmd] assault on slot %d: %s\n", foe.slot, why.c_str());
    };

    // DEFEND: a shown enemy near a held town. A unit strong enough chases it;
    // with none, the enemy becomes an assault.
    for (const Place& pl : places) {
        if (!pl.held()) {
            continue;
        }
        for (const Unit& f : foes) {
            if (f.slot == a.target || dist2(f.x, f.z, pl.x, pl.z) > near2 * 1.5f) {
                continue;
            }
            const bool taken = std::any_of(next.begin(), next.end(), [&](const auto& o) {
                return o.second.job == Job::Defend && o.second.target == f.slot;
            });
            if (taken) {
                continue;
            }
            if (const Unit* u = nearest_healthy(f.x, f.z, f.power * MARGIN)) {
                assign(*u, Job::Defend, pl.index, f.slot, f.x, f.z, "Defending " + pl.name);
            }
            else {
                start_assault(f, "An enemy too strong for one unit threatens " + pl.name + ": attacking in waves.");
            }
        }
    }

    // TAKE TOWN: each untaken town once, to the nearest healthy unit whose
    // verdict fits the ending. A town with a stronger enemy at it, or the
    // boss's own, waits for an assault.
    const Unit* boss = foe_by_slot(BOSS_SLOT);
    int towns_left = 0;
    for (const Place& pl : places) {
        if (!pl.takeable()) {
            continue;
        }
        if (boss != nullptr && dist2(boss->x, boss->z, pl.x, pl.z) <= town2 * 4.0f) {
            continue;  // the boss's stronghold: the BOSS assault takes it
        }
        // A town no unit of the army can take the way the ending wants is left.
        const bool possible = std::any_of(mine.begin(), mine.end(), [&](const Unit& u) { return fits_ending(u.alignment, pl); });
        if (!possible) {
            continue;
        }
        towns_left++;
        const Unit* guard = nullptr;
        for (const Unit& f : foes) {
            if (dist2(f.x, f.z, pl.x, pl.z) <= town2 * 4.0f && (guard == nullptr || f.power > guard->power)) {
                guard = &f;
            }
        }
        const Unit* best = nullptr;
        for (const Unit& u : mine) {
            if (!healthy(u) || !fits_ending(u.alignment, pl) || (guard != nullptr && u.power < guard->power * MARGIN)) {
                continue;
            }
            if (best == nullptr || dist2(u.x, u.z, pl.x, pl.z) < dist2(best->x, best->z, pl.x, pl.z)) {
                best = &u;
            }
        }
        if (best != nullptr) {
            const char* verb = liberation::verdict(best->alignment, pl.alignment) == liberation::Verdict::Liberates
                                   ? "Liberating "
                                   : "Capturing ";
            assign(*best, Job::TakeTown, pl.index, -1, pl.x, pl.z, verb + pl.name);
        }
        else if (guard != nullptr) {
            start_assault(*guard, "The enemy at " + pl.name + " is too strong for one unit: attacking in waves.");
        }
    }

    // COLLECT: the hidden items (developer, 2026-10-31: the AI may know the
    // spots; collect them all before the boss, one unit per item). While towns
    // are left, one idle unit at a time walks to the nearest item; once none
    // is left, every item gets its own unit and the boss waits for them, up to
    // ITEM_WAIT_HOURS.
    if (towns_left == 0 && g.towns_done_at < 0.0) {
        g.towns_done_at = hours;
    }
    else if (towns_left > 0) {
        g.towns_done_at = -1.0;
    }
    const std::vector<Spot> spots = read_spots(rdram);
    {
        int collectors = 0;
        const int allowed = towns_left > 0 ? 1 : static_cast<int>(spots.size());
        std::vector<int> claimed;
        // A unit already on its way keeps its item.
        for (const Unit& u : mine) {
            const auto old = g.orders.find(u.slot);
            if (!is_free(u) || old == g.orders.end() || old->second.job != Job::Collect) {
                continue;
            }
            const int index = -1000 - old->second.place;
            const auto sp = std::find_if(spots.begin(), spots.end(), [&](const Spot& s) { return s.index == index; });
            if (sp != spots.end() && collectors < allowed) {
                assign(u, Job::Collect, old->second.place, -1, sp->x, sp->z, old->second.text);
                claimed.push_back(index);
                collectors++;
            }
        }
        for (const Spot& sp : spots) {
            if (collectors >= allowed) {
                break;
            }
            if (std::find(claimed.begin(), claimed.end(), sp.index) != claimed.end()) {
                continue;
            }
            if (const Unit* u = nearest_healthy(sp.x, sp.z, 0.0f)) {
                assign(*u, Job::Collect, -1000 - sp.index, -1, sp.x, sp.z, "Collecting a hidden item");
                claimed.push_back(sp.index);
                collectors++;
            }
        }
    }

    // BOSS: once no town is left to take, and the hidden items are collected
    // (or ITEM_WAIT_HOURS have passed).
    const bool items_done = spots.empty() || (g.towns_done_at >= 0.0 && hours - g.towns_done_at >= ITEM_WAIT_HOURS);
    if (towns_left == 0 && items_done && boss != nullptr && a.target < 0) {
        start_assault(*boss, spots.empty() ? "Every town is taken and every hidden item found: the army moves on the boss."
                                           : "Every town is taken; some hidden items were not reached: the army moves on the boss.");
    }

    // ASSAULT: the relay.
    target = a.target >= 0 ? foe_by_slot(a.target) : nullptr;
    if (target != nullptr) {
        const Place* base = nearest_held(target->x, target->z, nullptr);
        a.base = base != nullptr ? base->index : -1;
        if (a.train_until > hours) {
            // Step 4: units fight enemies they can beat, then the relay starts
            // again at step 1.
            for (const Unit& u : mine) {
                if (!healthy(u)) {
                    continue;
                }
                const Unit* prey = nullptr;
                for (const Unit& f : foes) {
                    if (f.slot != a.target && f.slot != BOSS_SLOT && f.power * MARGIN <= u.power &&
                        (prey == nullptr || dist2(u.x, u.z, f.x, f.z) < dist2(u.x, u.z, prey->x, prey->z))) {
                        prey = &f;
                    }
                }
                if (prey != nullptr) {
                    assign(u, Job::Train, -1, prey->slot, prey->x, prey->z, "Training on a weaker enemy");
                }
            }
        }
        else {
            if (a.train_until >= 0.0) {
                a.train_until = -1.0;
                a.escalation = 0;
                a.battles = 0;
                a.window_hp = target->hp;
                alerts.push_back("Training is over: the attack starts again.");
            }
            // Attackers already on their way keep going.
            int attacking = 0;
            for (const Unit& u : mine) {
                const auto old = g.orders.find(u.slot);
                if (is_free(u) && old != g.orders.end() && old->second.job == Job::Assault) {
                    assign(u, Job::Assault, -1, a.target, target->x, target->z, old->second.text);
                    attacking++;
                }
            }
            // A new wave every WAVE_GAP_HOURS: the strongest healthy unit. The
            // early waves strip members (Attack Weakest, or Attack Strongest
            // after escalation step 1); below 40% the leader is the target.
            std::vector<const Unit*> ready;
            for (const Unit& u : mine) {
                if (healthy(u) && u.share() >= RESUME_LINE) {
                    ready.push_back(&u);
                }
            }
            std::sort(ready.begin(), ready.end(), [](const Unit* l, const Unit* r) { return l->power > r->power; });
            if (attacking < a.wave_size && !ready.empty() && hours - a.last_wave >= WAVE_GAP_HOURS) {
                const Unit& u = *ready.front();
                const float left = target->hp_max > 0 ? static_cast<float>(target->hp) / target->hp_max : 1.0f;
                const uint8_t strategy = left < 0.4f ? Leader : (a.escalation % 2 == 1 ? Strongest : Weakest);
                set_strategy(rdram, u.slot, strategy);
                assign(u, Job::Assault, -1, a.target, target->x, target->z,
                       std::string("Attacking (") + strategy_name(strategy) + ")");
                a.last_wave = hours;
                ready.erase(ready.begin());
                std::fprintf(stderr, "[autocmd] wave: slot %d on slot %d, %s, target at %d%%\n", u.slot, a.target,
                             strategy_name(strategy), static_cast<int>(left * 100.0f));
            }
            // The next waves wait at the base: one per wave the relay is
            // allowed, plus one.
            int staged = 0;
            for (const Unit* u : ready) {
                if (staged >= a.wave_size + 1 || base == nullptr || !is_free(*u)) {
                    break;
                }
                assign(*u, Job::Stage, base->index, -1, base->x, base->z, "Waiting at " + base->name + " to attack");
                staged++;
            }
        }
        a.attacker_hp.clear();
        for (const auto& [slot, order] : next) {
            if (order.job == Job::Assault) {
                const auto it = std::find_if(mine.begin(), mine.end(), [&](const Unit& u) { return u.slot == slot; });
                a.attacker_hp[slot] = it != mine.end() ? it->hp : 0;
            }
        }
        a.target_hp = target->hp;
    }

    // GUARD: the rest hold the held town nearest the enemy.
    for (const Unit& u : mine) {
        if (!is_free(u)) {
            continue;
        }
        const Place* best = nullptr;
        float best_d = 0.0f;
        for (const Place& pl : places) {
            if (!pl.held()) {
                continue;
            }
            float d = 1e30f;
            for (const Unit& f : foes) {
                d = std::min(d, dist2(f.x, f.z, pl.x, pl.z));
            }
            if (best == nullptr || d < best_d) {
                best = &pl;
                best_d = d;
            }
        }
        if (best != nullptr) {
            assign(u, Job::Guard, best->index, -1, best->x, best->z, "Guarding " + best->name);
        }
    }

    std::fprintf(stderr, "[autocmd] decide at %.1f h: %zu units, %zu foes, %zu places, %zu orders\n", hours,
                 mine.size(), foes.size(), places.size(), next.size());
    // Issue the new or changed orders.
    for (const auto& [slot, order] : next) {
        const auto old = g.orders.find(slot);
        const bool same = old != g.orders.end() && old->second.job == order.job && old->second.place == order.place &&
                          old->second.target == order.target;
        if (same) {
            continue;
        }
        move_to(rdram, ctx, slot, order.x, order.z);
        std::fprintf(stderr, "[autocmd] unit %d: %s (%s)\n", slot + 1, job_name(order.job), order.text.c_str());
        if (order.job == Job::Retreat || order.job == Job::TakeTown) {
            alerts.push_back("Unit " + std::to_string(slot + 1) + ": " + order.text);
        }
    }
    g.orders = std::move(next);
    for (const std::string& text : alerts) {
        alerts::post(alerts::Kind::Info, "Auto commander: " + text);
    }
}

// A chase follows its target: re-aimed every REAIM_EVERY field updates.
void reaim(uint8_t* rdram, recomp_context* ctx) {
    for (auto& [slot, order] : g.orders) {
        if (order.target < 0 || !on_map(rdram, slot)) {
            continue;
        }
        if (!on_map(rdram, order.target)) {
            order.target = -1;
            continue;
        }
        const uint32_t o = object_of(rdram, order.target);
        order.x = rdf(rdram, o + 0x08);
        order.z = rdf(rdram, o + 0x10);
        move_to(rdram, ctx, slot, order.x, order.z);
    }
}

void tick(uint8_t* rdram, recomp_context* ctx) {
    // One line each time the gate opens or closes, with the values it read.
    static int last_gate = -1;
    const int scene = rd16(rdram, SCENE);
    const uint32_t mode = rd32(rdram, FIELD_MODE);
    const int gate = on() && scene == SCENE_FIELD && mode == 0 ? 1 : 0;
    if (gate != last_gate) {
        std::fprintf(stderr, "[autocmd] gate %s (setting %d, scene 0x%02X, field mode 0x%X)\n",
                     gate ? "open" : "closed", on() ? 1 : 0, scene, mode);
        last_gate = gate;
    }
    if (gate == 0) {
        return;
    }
    // The selection first: field_select's lock is taken before this file's
    // (its commands call toggle_units under it), never after.
    const std::vector<int> selection = field_select::selected();
    std::lock_guard<std::mutex> lock(g_mutex);
    const double hours = now_hours(rdram);
    const int mission = rd8(rdram, MISSION_INDEX);
    if (g.mission != mission) {
        const std::set<int> released = g.released;
        g = State{};
        g.mission = mission;
        g.released = released;  // a unit taken back stays the player's
        g.next_decision = hours + 1.0;  // the field settles first
    }
    // A unit the player took back or selected is dropped from the orders, so a
    // chase never re-aims it.
    for (auto it = g.orders.begin(); it != g.orders.end();) {
        const bool picked = std::find(selection.begin(), selection.end(), it->first) != selection.end();
        it = (picked || g.released.count(it->first)) ? g.orders.erase(it) : std::next(it);
    }
    if (hours >= g.next_decision || hours + DECIDE_HOURS * 4 < g.next_decision) {
        g.next_decision = hours + DECIDE_HOURS;
        decide(rdram, ctx, hours, selection);
    }
    if (++g.reaim % REAIM_EVERY == 0) {
        reaim(rdram, ctx);
    }
}

ogre::events::Subscribe g_tick{ogre::events::Event::FieldUpdate, "auto commander",
                               [](const ogre::events::Args& e) {
                                   if (e.rdram != nullptr && e.ctx != nullptr) {
                                       tick(e.rdram, e.ctx);
                                   }
                               }};

}  // namespace

bool on() {
    return choice_on(Choice::AutoCommander);
}

bool toggle() {
    const bool now = !on();
    set_choice(Choice::AutoCommander, now ? 1 : 0);
    if (!now) {
        // Off: every unit keeps its current destination and nothing more is
        // ordered. The next start decides afresh.
        std::lock_guard<std::mutex> lock(g_mutex);
        g.orders.clear();
        g.assault = Assault{};
        g.next_decision = 0.0;
    }
    std::fprintf(stderr, "[autocmd] %s\n", now ? "on" : "off");
    alerts::post(alerts::Kind::Info, now ? "Auto commander on." : "Auto commander off: the army is yours.");
    return now;
}

void player_ordered(int slot) {
    if (!on()) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g.released.insert(slot).second) {
        g.orders.erase(slot);
        alerts::post(alerts::Kind::Info, "Unit " + std::to_string(slot + 1) + " follows your orders now (AI button hands it back).");
    }
}

void toggle_units(const std::vector<int>& slots) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool all_ai = std::none_of(slots.begin(), slots.end(), [](int s) { return g.released.count(s) != 0; });
    for (int s : slots) {
        if (all_ai) {
            g.released.insert(s);
            g.orders.erase(s);
        }
        else {
            g.released.erase(s);
        }
    }
    g.next_decision = 0.0;  // decide at the next field update
}

bool commands(int slot) {
    if (!on()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    return g.released.count(slot) == 0;
}

std::string job_text(int slot) {
    if (!on()) {
        return "";
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g.released.count(slot) != 0) {
        return "";
    }
    const auto it = g.orders.find(slot);
    return it != g.orders.end() ? it->second.text : "";
}

}  // namespace ogre::auto_commander

