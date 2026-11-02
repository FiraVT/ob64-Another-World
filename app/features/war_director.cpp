// War Director observer. See war_director.hpp and docs/anotherworld/design-war-director.md.

#include "game_addresses.hpp"
#include "war_director.hpp"
#include "game_events.hpp"
#include "alerts.hpp"
#include "class_rebalance.hpp"
#include "content_db.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include "feature_flags.hpp"
#include "ultramodern/ultramodern.hpp"

namespace ogre::war_director {

namespace {

constexpr uint32_t SCENE_ADDRESS = ::ogre::addr::kActiveScene;
constexpr int SCENE_MISSION = 0x03;
constexpr uint32_t BASE_LEVEL = 0x801F1055u;
constexpr uint32_t MISSION_INDEX = ::ogre::addr::kMissionIndex;

constexpr uint32_t UNIT_TABLE = ::ogre::addr::kUnits;
constexpr uint32_t UNIT_STRIDE = 0x19u;
constexpr int UNIT_FIRST = 30;
constexpr int UNIT_COUNT = 60;
constexpr int BOSS_SLOT = 30;

constexpr uint32_t ENEMY_CHARS = ::ogre::addr::kEnemyCharacters;
constexpr uint32_t ENEMY_STRIDE = 0x34u;
constexpr int SOLDIER_REF = 100;
constexpr int CLASS_SOLDIER = 1;
constexpr int STORY_CLASS_MIN = 81;

constexpr uint32_t CLASS_TABLE = ::ogre::addr::kClasses;
constexpr uint32_t CLASS_STRIDE = 0x48u;
constexpr int CLASS_COUNT = 165;

// Class ids 81 and up are mostly story characters: one-mission leaders, boss
// variants and the duplicate story ids of normal classes. Three of them are the
// enemy's own army classes, used by ordinary units across missions (ROM
// enemydat.bin with the eset spawn lists, non-boss entries, 2026-10-30):
// Grappler 114 (78 members, 5 missions), Knight Templar 115 (53 leaders, 73
// members, 10 missions) and Daemon 116 (13 leaders, 3 missions). They count as
// generic, so their units get the enemy features and join the pools. The
// Knight Templars 147 and 155 are story units.
bool enemy_army_class(int cls) {
    return cls == 114 || cls == 115 || cls == 116;
}

bool story_class(int cls) {
    return cls >= CLASS_COUNT || (cls >= STORY_CLASS_MIN && !enemy_army_class(cls));
}

// Design section 4.2-4.5. How fast income grows with time on the mission is
// set by DIFFICULTY (time_weight below); it used to be a fixed 2.0 whatever
// the setting.
constexpr double TIME_FACTOR = 0.02 / 24.0;  // per in-game hour (0.02 a day)
constexpr double MISSION_BASE = 1.10;   // per mission index
constexpr double INCOME_K = 0.5;          // credits per in-game hour at coeff 0 (12 a day)
constexpr double CAMPAIGN_SHARE = 0.6;
constexpr double PATROL_SHARE = 0.4;
constexpr double LEADER_MULT = 1.25;
// The game's clock (captures 1-57, 2026-10-30): u32 at 0x800E7D34, in-game hours
// since the campaign began in 16.16 fixed point; about 120 game frames an hour.
// The director's hours are these hours, so every timing below is in-game time.
constexpr uint32_t GAME_CLOCK = ::ogre::addr::kGameClock;
// The game's frame counter (`D_800AEFA4`, written once per frame by the frame
// pump `func_80072398`). The director's clock advances by its change, so it
// stops while the game is paused and follows the game's own frame rate. It
// used to count the app's main-loop passes, which run faster than the game and
// keep running in the Esc menu.
constexpr uint32_t GAME_FRAME_COUNTER = 0x800AEFA4u;
// A jump larger than this (a load, a reset) is not counted.
constexpr uint32_t MAX_FRAME_STEP = 8;

struct Plan {
    std::vector<int> classes;  // leader first
    int cost = 0;
    double ready_at = 0.0;  // director hour it may deploy (v2 5.2: it forms first)
    int kind = 0;           // 0 patrol, 1 large unit, 2 supply train (v2 5.3)
};

struct Rng {
    uint32_t state;
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int pick(const std::map<int, int>& weights, int exclude) {
        int total = 0;
        for (const auto& [cls, w] : weights) {
            total += cls == exclude ? 0 : w;
        }
        if (total == 0) {
            return 0;
        }
        int roll = static_cast<int>(next() % static_cast<uint32_t>(total));
        for (const auto& [cls, w] : weights) {
            if (cls == exclude) {
                continue;
            }
            if (roll < w) {
                return cls;
            }
            roll -= w;
        }
        return 0;
    }
};

std::mutex g_mutex;
int g_mission = -1;
int g_base_level = 0;
bool g_on_mission = false;
uint64_t g_frames = 0;
double g_hours = 0.0;           // in-game hours on this mission, from the game's clock
uint32_t g_last_clock = 0;
bool g_have_clock = false;
uint32_t g_last_game_frame = 0;
bool g_have_game_frame = false;
double g_campaign_bank = 0.0;
double g_patrol_bank = 0.0;
int g_sent_campaign = 0;
int g_sent_patrol = 0;
// Phase 3: bought units waiting for the field update to deploy them
// (app/features/director_spawn.cpp), and how many have been deployed.
// Bought units mass into a wave and attack together (developer playtest
// 2026-10-30: one unit at a time read as a constant trickle). Supply caravans
// are queued too but leave on their own.
constexpr size_t MAX_PENDING = 6;
constexpr double WAVE_MAX_WAIT = 48.0;  // in-game hours a partial wave waits before it attacks
std::deque<Plan> g_pending;
bool g_wave_release = false;
int g_spawned = 0;
// War Director v2 (docs/anotherworld/design-war-director-v2.md 4.1): one seed per new game,
// mixed into every director roll, so each playthrough is a different war.
uint32_t g_war_seed = 1;
std::filesystem::path g_seed_path;
int g_last_scene = -1;
constexpr int SCENE_NAME_ENTRY = 0x07;  // only a new game reaches the name-entry form
// The next units the director will buy, rolled once and kept until bought, so
// the DEBUG tab shows what will actually come.
Plan g_next_campaign;
Plan g_next_patrol;

// v2 phase 2 (design-war-director-v2.md 5.1-5.4): towns, intensity, grudge.
// Location state u16 at 0x801951EC + i*0x24 (0x04 enemy-held, 0x02 player-held),
// count word 0x801F1090 (docs/anotherworld/design-war-director.md 6a, R1).
constexpr uint32_t LOCATION_COUNT = ::ogre::addr::kLocationCount;
constexpr uint32_t LOCATION_STATE = ::ogre::addr::kLocationState;
constexpr uint32_t LOCATION_STRIDE = 0x24u;
constexpr int MAX_LOCATIONS = 64;
constexpr uint32_t OBJECTS = ::ogre::addr::kMapObjects;
constexpr uint32_t PLAYER_CHAR_TABLE = ::ogre::addr::kCharacters;
constexpr int PLAYER_UNIT_COUNT = 30;
constexpr double CAMPAIGN_FORMING = 12.0;  // in-game hours between the alert and deployment
constexpr double PATROL_FORMING = 6.0;
constexpr double INTENSITY_RELAX = 60.0;   // above this the director relaxes
constexpr double INTENSITY_DECAY = 0.4;    // per in-game hour (about 10 a day)
constexpr double TOWN_LOST_INTENSITY = 25.0;
constexpr double PLAYER_HOLD_STOP = 0.7;   // no income while the player holds this share
uint16_t g_town_state[MAX_LOCATIONS] = {};
int g_town_count = 0;
int g_enemy_towns_start = -1;
int g_enemy_towns = 0;
int g_player_towns = 0;
double g_grudge[MAX_LOCATIONS] = {};
double g_intensity = 0.0;
// Pacing (Left 4 Dead's director, all four phases; developer, 2026-10-30):
// BUILD-UP buys and masses a wave; its release starts the PEAK, which lasts
// until intensity reaches INTENSITY_RELAX, the wave is gone or PEAK_MAX_HOURS
// pass; FADE halves income and buys nothing for FADE_HOURS; RELAX buys nothing
// for one to two days, then the next BUILD-UP starts. Heavy losses during a
// BUILD-UP (the mission's own garrisons) go straight to FADE.
enum class Phase { BuildUp, Peak, Fade, Relax };
constexpr double PEAK_MAX_HOURS = 72.0;
constexpr double FADE_HOURS = 12.0;
constexpr double FADE_INCOME = 0.5;
// A PEAK whose intensity stayed below this was easy for the player: on HARD
// and VETERAN the next BUILD-UP forms in half the time with one more unit.
constexpr double COAST_INTENSITY = 20.0;
constexpr double MEMBER_DOWN_INTENSITY = 5.0;  // per player character brought to 0 HP
Phase g_phase = Phase::BuildUp;
double g_phase_until = 0.0;  // director hour the timed phases (PEAK cap, FADE, RELAX) end
double g_phase_peak = 0.0;   // the highest intensity of the current PEAK
bool g_push = false;         // the coming BUILD-UP pushes (above)
// Momentum: how fast and how cleanly the player clears missions, a running
// score from 0 to 1 kept across missions in war_momentum.cfg (developer,
// 2026-10-30: a player who snowballs through missions met less of the
// director, since income comes from time and freed towns starve it). A mission
// opens with a war chest from it, and on HARD and VETERAN a high momentum adds
// a unit to every wave. A slower mission lowers it again.
constexpr double MISSION_PAR_HOURS = 120.0;  // five in-game days
constexpr double MOMENTUM_KEEP = 0.6;        // weight of the old score per mission
double g_momentum = 0.0;
std::filesystem::path g_momentum_path;
double g_mission_stress = 0.0;  // HP lost on this mission, % of the units' total, summed
int g_members_down = 0;
bool g_chest_given = false;
double g_chest = 0.0;
// Last stand: when the player takes PLAYER_HOLD_STOP of the towns the enemy's
// income stops; before it does, the enemy spends what it has (at least a
// floor by difficulty) on one wave sent after the town it most wants back.
bool g_last_stand_done = false;
bool g_last_stand = false;   // from the trigger until that wave's PEAK ends
// A player who goes straight for the boss never takes 70% of the towns, so a
// player unit near the boss (seen by the enemy commander) also triggers the
// last stand. That one forms at the stronghold in a quarter of the forming
// time and rallies to the boss.
bool g_boss_threat = false;  // the commander saw a player unit near the boss
bool g_stand_boss = false;   // the current last stand defends the boss
uint16_t g_player_hp[100] = {};
bool g_have_player_hp = false;
// v2 6.1: units the director deployed (slot -> campaign unit), the spoils won
// from them waiting to be granted on the game thread, and the pity counter.
struct DirectorUnit {
    int kind;
    int target;  // the liberated town it was sent after, or -1
};
std::map<int, DirectorUnit> g_director_units;
std::vector<uint16_t> g_spoils;
int g_dry = 0;
uint32_t g_goth_bonus = 0;           // Goth won from supply trains, granted on the game thread
bool g_loyal[MAX_LOCATIONS] = {};    // v2 6.3: towns that held against a counterattack
bool g_gift_given[MAX_LOCATIONS] = {};
double g_next_supply = 0.0;          // director hour of the next supply train
std::map<int, int> g_leaders;   // class -> generic units it leads
std::map<int, int> g_members;   // class -> generic units it is a member of
bool g_seen_unit[UNIT_COUNT] = {};
double g_unit_cost[UNIT_COUNT] = {};  // every live enemy unit, story units too
double g_generic_cost_sum = 0.0;
int g_generic_units = 0;
double g_first_average = 0.0;  // the generic average when the mission opened

// Bestiary: every class the player has fought, as a leader and as a member,
// kept in bestiary.cfg in the config directory. Training draws from it.
std::map<int, int> g_fought_leaders;
std::map<int, int> g_fought_members;
std::filesystem::path g_bestiary_path;

void loot_tick(uint8_t* rdram);
void spoils_tick(const uint8_t* rdram);
int band_max(int mission);
void record_fought(const std::vector<int>& classes);
std::string loot_text();

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

uint16_t rd16(const uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>((rd8(rdram, address) << 8) | rd8(rdram, address + 1));
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd16(rdram, address)) << 16) | rd16(rdram, address + 2);
}

uint32_t class_record(int cls) {
    return CLASS_TABLE + static_cast<uint32_t>(cls) * CLASS_STRIDE;
}

std::string class_name(const uint8_t* rdram, int cls) {
    const uint32_t pointer = rd32(rdram, class_record(cls));
    std::string name;
    if (pointer >= 0x80000000u && pointer < 0x80800000u) {
        for (uint32_t i = 0; i < 20; i++) {
            const uint8_t c = rd8(rdram, pointer + i);
            if (c == 0) {
                break;
            }
            if (c == 0x81) {
                continue;
            }
            name += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ';
        }
    }
    return name.empty() ? "Class " + std::to_string(cls) : name;
}

// power(class, L) = (sum of base stats + L * sum of growth) * (0.75 + 0.25 * best
// row attack count); cost = power / 20 (design section 4.5).
// The class value (attack-priced, defined with the refit below) and the elite
// rules built on it.
double class_value(const uint8_t* rdram, int cls, int level);
bool is_elite(const uint8_t* rdram, int cls, int level);
int elite_cap(int mission);
int tactics_level();
double reward_scale();
uint16_t roll_item(const uint8_t* rdram, bool equipment, Rng& rng);

// The old price: the stat total at the level, scaled by the best row's attack
// count. It compresses real strength (Goblin to Tiamat is about 2x in this
// price and about 7x in class_value), so it now only sets the average.
double stat_cost(const uint8_t* rdram, int cls, int level) {
    const uint32_t record = class_record(cls);
    int base = 0;
    int growth = 0;
    for (uint32_t i = 0; i < 7; i++) {
        base += rd8(rdram, record + 0x08 + 4 * i + 1);
        growth += rd8(rdram, record + 0x08 + 4 * i + 2);
    }
    int best = 0;
    for (uint32_t row = 0; row < 3; row++) {
        const int n = rd8(rdram, record + 0x38 + 2 * row);
        if (n <= 9) {
            best = std::max(best, n);
        }
    }
    return (base + level * growth) * (0.75 + 0.25 * best) / 20.0;
}

// The generic classes the averages run over (the enemy's own: no story ids
// except the army classes, no Giant placeholder).
bool priced_class(int cls) {
    return (cls > 1 && cls < 81 && cls != 77) || cls == 114 || cls == 115 || cls == 116;
}

struct LevelAverages {
    double stat = 0.0;
    double value = 0.0;
};
std::map<int, LevelAverages> g_level_averages;  // by level; cleared at each mission load

const LevelAverages& level_averages(const uint8_t* rdram, int level) {
    level = std::clamp(level, 1, 99);
    auto it = g_level_averages.find(level);
    if (it != g_level_averages.end()) {
        return it->second;
    }
    LevelAverages a;
    int n = 0;
    for (int c = 1; c < CLASS_COUNT; c++) {
        if (priced_class(c)) {
            a.stat += stat_cost(rdram, c, level);
            a.value += class_value(rdram, c, level);
            n++;
        }
    }
    if (n > 0) {
        a.stat /= n;
        a.value /= n;
    }
    return g_level_averages[level] = a;
}

// What one member costs the director: its class value against the average
// class at that level, times the old average price, so the director's total
// spending per level stays where it was while each class pays for its real
// strength (developer, 2026-10-30: "how does it balance out spending on normal
// units or large or generally powerful"). At level 30 that is about 0.5 of the
// average for a Goblin, 1.7 for a Paladin and 3.3 for a Tiamat.
double card_cost(const uint8_t* rdram, int cls, int level) {
    const LevelAverages& a = level_averages(rdram, level);
    if (a.value <= 0.0 || cls <= 0 || cls >= CLASS_COUNT) {
        return stat_cost(rdram, cls, level);
    }
    return a.stat * class_value(rdram, cls, level) / a.value;
}

// The row a class attacks best from: 2 front, 1 middle, 0 back.
int best_row(const uint8_t* rdram, int cls) {
    const uint32_t record = class_record(cls);
    const int front = rd8(rdram, record + 0x38);
    const int middle = rd8(rdram, record + 0x3A);
    const int back = rd8(rdram, record + 0x3C);
    if (front >= middle && front >= back) {
        return 2;
    }
    return middle >= back ? 1 : 0;
}

int member_class(const uint8_t* rdram, int ref) {
    if (ref >= SOLDIER_REF) {
        return CLASS_SOLDIER;
    }
    return rd8(rdram, ENEMY_CHARS + static_cast<uint32_t>(ref) * ENEMY_STRIDE + 0x11);
}

void reset_mission(int mission) {
    g_mission = mission;
    g_frames = 0;
    g_hours = 0.0;
    g_campaign_bank = 0.0;
    g_patrol_bank = 0.0;
    g_sent_campaign = 0;
    g_sent_patrol = 0;
    g_pending.clear();
    g_wave_release = false;
    g_spawned = 0;
    g_leaders.clear();
    g_members.clear();
    std::fill(std::begin(g_seen_unit), std::end(g_seen_unit), false);
    std::fill(std::begin(g_unit_cost), std::end(g_unit_cost), 0.0);
    g_generic_cost_sum = 0.0;
    g_generic_units = 0;
    g_first_average = 0.0;
    g_next_campaign = {};
    g_next_patrol = {};
    std::fill(std::begin(g_town_state), std::end(g_town_state), uint16_t{0});
    g_town_count = 0;
    g_enemy_towns_start = -1;
    std::fill(std::begin(g_grudge), std::end(g_grudge), 0.0);
    g_intensity = 0.0;
    g_phase = Phase::BuildUp;
    g_phase_until = 0.0;
    g_phase_peak = 0.0;
    g_push = false;
    g_mission_stress = 0.0;
    g_members_down = 0;
    g_chest_given = false;
    g_chest = 0.0;
    g_last_stand_done = false;
    g_last_stand = false;
    g_boss_threat = false;
    g_stand_boss = false;
    g_have_player_hp = false;
    g_director_units.clear();
    g_spoils.clear();
    g_goth_bonus = 0;
    std::fill(std::begin(g_loyal), std::end(g_loyal), false);
    std::fill(std::begin(g_gift_given), std::end(g_gift_given), false);
    g_next_supply = 0.0;
}

// What a live unit is worth, from its members' own classes and levels (generic
// soldiers have no record and use the base level).
double live_unit_cost(const uint8_t* rdram, uint32_t unit) {
    double cost = 0.0;
    for (uint32_t i = 0; i < 5; i++) {
        const int ref = rd8(rdram, unit + 0x02 + i);
        if (ref == 0) {
            continue;
        }
        const int cls = member_class(rdram, ref);
        if (cls <= 0 || cls >= CLASS_COUNT) {
            continue;
        }
        const int level = ref >= SOLDIER_REF
                              ? g_base_level
                              : rd8(rdram, ENEMY_CHARS + static_cast<uint32_t>(ref) * ENEMY_STRIDE + 0x13);
        cost += card_cost(rdram, cls, level) * (i == 0 ? LEADER_MULT : 1.0);
    }
    return cost;
}

// Adds every enemy unit not seen yet to the card pool, if it is generic. A slot
// that empties is forgotten, so a unit that later takes it is seen anew.
void observe_units(const uint8_t* rdram) {
    for (int u = UNIT_FIRST; u < UNIT_COUNT; u++) {
        const uint32_t unit = UNIT_TABLE + static_cast<uint32_t>(u) * UNIT_STRIDE;
        std::vector<int> classes;
        if ((rd8(rdram, unit + 0x01) & 0x80u) != 0) {
            for (uint32_t i = 0; i < 5; i++) {
                const int ref = rd8(rdram, unit + 0x02 + i);
                if (ref != 0) {
                    classes.push_back(member_class(rdram, ref));
                }
            }
        }
        if (classes.empty() || classes[0] == 0) {
            g_seen_unit[u] = false;
            g_unit_cost[u] = 0.0;
            continue;
        }
        if (g_seen_unit[u]) {
            continue;
        }
        g_seen_unit[u] = true;
        g_unit_cost[u] = live_unit_cost(rdram, unit);
        record_fought(classes);
        const bool story = u == BOSS_SLOT ||
                           std::any_of(classes.begin(), classes.end(),
                                       [](int c) { return story_class(c); });
        if (story) {
            continue;
        }
        g_leaders[classes[0]]++;
        double cost = card_cost(rdram, classes[0], g_base_level + 1) * LEADER_MULT;
        std::map<int, bool> counted;
        for (size_t i = 1; i < classes.size(); i++) {
            cost += card_cost(rdram, classes[i], g_base_level);
            if (!counted[classes[i]]) {
                counted[classes[i]] = true;
                g_members[classes[i]]++;
            }
        }
        g_generic_cost_sum += cost;
        g_generic_units++;
    }
}

int top(const std::map<int, int>& pool) {
    int best = 0;
    int weight = 0;
    for (const auto& [cls, n] : pool) {
        if (n > weight) {
            best = cls;
            weight = n;
        }
    }
    return best;
}

// Members allowed per unit at this point of the campaign (v2 4.3): small
// units early, full units late. The mission index is the loader's scenario id;
// its order follows the campaign closely enough for these bands.
int stage_cap(int mission) {
    return mission <= 9 ? 2 : mission <= 18 ? 3 : 4;
}

// The DIFFICULTY level (GAMEPLAY > WAR DIRECTOR): 0 EASY, 1 NORMAL, 2 HARD,
// 3 VETERAN (enemy_commander.cpp reads the same row).
int tactics_level() {
    return std::clamp(choice_index(Choice::EnemyTactics), 0, 3);
}

// Members (besides the leader) a unit the director builds may have: the stage
// cap, one fewer on EASY, one more on HARD and VETERAN, and one more after 5
// and after 15 in-game days on the mission, at most four (a unit record holds
// five; developer, 2026-10-30: full units should become the norm as the war
// goes on, and "15 days is a long time to still be fighting 3 as a max").
int member_cap(int mission, double hours) {
    const int level = tactics_level();
    int cap = stage_cap(mission) + (level == 0 ? -1 : level >= 2 ? 1 : 0);
    cap += (hours >= 120.0 ? 1 : 0) + (hours >= 360.0 ? 1 : 0);
    return std::clamp(cap, 1, 4);
}

// Members a patrol may have: two fewer than a campaign unit, at least one, so a
// patrol is never a lone leader.
int patrol_cap(int mission, double hours) {
    return std::max(1, member_cap(mission, hours) - 2);
}

// What the director may spend on a unit of `members` members, in average
// generic units: 1.6 for two members (the old campaign budget) and 0.35 per
// member more or less, so a growing member cap stays affordable.
double unit_budget(int members) {
    return 0.9 + 0.35 * members;
}

// The base human classes join every mission's pools (developer, 2026-10-30:
// mission 1's groups were Wizard or Witch with Knights, Griffins and
// Hellhounds only; "Fighter amazon soldiers", no higher tiers and no families
// the mission has not shown). Soldier 1, Fighter 2 and Amazon 4 join the
// members; Fighter and Amazon also lead. Each has weight 1, below a class the
// mission's own units use more than once.
constexpr int BASE_HUMAN_MEMBERS[] = {1, 2, 4};
constexpr int BASE_HUMAN_LEADERS[] = {2, 4};

std::map<int, int> with_base_humans(const std::map<int, int>& pool, bool leaders) {
    std::map<int, int> out = pool;
    if (leaders) {
        for (int cls : BASE_HUMAN_LEADERS) {
            out.emplace(cls, 1);
        }
    }
    else {
        for (int cls : BASE_HUMAN_MEMBERS) {
            out.emplace(cls, 1);
        }
    }
    return out;
}

// Units per attack wave, by the same bands.
int wave_target(int mission) {
    return mission <= 9 ? 2 : mission <= 18 ? 3 : 4;
}

// A queued unit may leave: a caravan once formed, an attack unit once formed
// and its wave released.
bool eligible(const Plan& p, double now, bool wave_release) {
    return p.ready_at <= now && (p.kind == 2 || wave_release);
}

// The unit the director buys with `budget` (v2 4.4): a leader drawn by weight
// from the mission's leaders, a shape, and members drawn by weight with the
// shape's row preferred. Line wants front-row members, Battery back-row ones,
// Mixed takes any. A template has two member groups of up to three, so a plan
// holds at most two member classes, three of each (take_spawn writes it as is).
Plan plan_unit(const uint8_t* rdram, double budget, int max_members, Rng& rng) {
    Plan plan;
    const std::map<int, int> leaders = with_base_humans(g_leaders, true);
    const std::map<int, int> members = with_base_humans(g_members, false);
    const int leader = rng.pick(leaders, -1);
    if (leader == 0) {
        return plan;
    }
    double cost = card_cost(rdram, leader, g_base_level + 1) * LEADER_MULT;
    plan.classes.push_back(leader);
    enum { Line, Battery, Mixed } shape = Mixed;
    const uint32_t roll = rng.next() % 4;
    shape = roll < 2 ? (best_row(rdram, leader) == 0 ? Battery : Line) : roll == 2 ? Mixed : Battery;
    // About one unit in four is built a member short, so not every unit is full.
    if (max_members > 1 && rng.next() % 4 == 0) {
        max_members--;
    }
    int elites = 0;
    std::map<int, int> taken;
    for (int tries = 0; tries < 8 && static_cast<int>(plan.classes.size()) - 1 < max_members; tries++) {
        std::map<int, int> weights;
        for (const auto& [cls, n] : members) {
            const int row = best_row(rdram, cls);
            const bool wanted = shape == Mixed || (shape == Line ? row == 2 : row == 0);
            if (is_elite(rdram, cls, g_base_level) && elites >= elite_cap(g_mission)) {
                continue;
            }
            const auto have = taken.find(cls);
            if (have == taken.end() ? taken.size() >= 2 : have->second >= 3) {
                continue;
            }
            if (cost + card_cost(rdram, cls, g_base_level) <= budget) {
                weights[cls] = n * (wanted ? 3 : 1);
            }
        }
        const int cls = rng.pick(weights, -1);
        if (cls == 0) {
            break;
        }
        elites += is_elite(rdram, cls, g_base_level) ? 1 : 0;
        cost += card_cost(rdram, cls, g_base_level);
        plan.classes.push_back(cls);
        taken[cls]++;
    }
    plan.cost = static_cast<int>(std::lround(cost));
    return plan;
}

// The weight of time in coeff, by DIFFICULTY: EASY 1, NORMAL 2 (the old fixed
// value), HARD 3, VETERAN 4. Income per day on the mission therefore grows by
// 2% / 4% / 6% / 8% of the mission's starting coeff.
double time_weight() {
    return 1.0 + tactics_level();
}

double coeff() {
    const double hours = g_hours;
    return (1.0 + hours * TIME_FACTOR * time_weight()) * std::pow(MISSION_BASE, std::max(0, g_mission));
}

double hours_on_mission() {
    return g_hours;
}

// v2 5.4: towns the enemy holds feed it; liberating towns starves it, and it
// earns nothing while the player holds most of the mission's towns.
double town_factor() {
    if (g_town_count <= 0 || g_enemy_towns_start < 0) {
        return 1.0;
    }
    if (g_player_towns >= PLAYER_HOLD_STOP * g_town_count) {
        return 0.0;
    }
    return (1.0 + 0.15 * g_enemy_towns) / (1.0 + 0.15 * g_enemy_towns_start);
}

double rate_per_hour() {
    return INCOME_K * (1.0 + 0.4 * coeff()) * town_factor();
}

bool relaxing() {
    return g_phase == Phase::Relax;
}

const char* phase_name(Phase p) {
    switch (p) {
        case Phase::BuildUp: return "BUILD-UP";
        case Phase::Peak: return "PEAK";
        case Phase::Fade: return "FADE";
        default: return "RELAX";
    }
}

void enter_phase(Phase p, double hours) {
    g_phase = p;
    g_phase_until = hours_on_mission() + hours;
    g_phase_peak = 0.0;
    std::fprintf(stderr, "[director] phase %s (%.0f h)%s\n", phase_name(p), hours,
                 p == Phase::BuildUp && g_push ? ", pushing" : "");
}

// Units added to each wave: on HARD one for a push or a high momentum, on
// VETERAN one for each.
int wave_extra() {
    const int level = tactics_level();
    if (level < 2) {
        return 0;
    }
    const int extra = (g_push ? 1 : 0) + (g_momentum >= 0.5 ? 1 : 0);
    return std::min(extra, level >= 3 ? 2 : 1);
}

// Forming times and the partial-wave wait: halved for a push and a last stand,
// a quarter for a last stand at the boss.
double forming_scale() {
    if (g_last_stand && g_stand_boss) {
        return 0.25;
    }
    return g_push || g_last_stand ? 0.5 : 1.0;
}

// A mission ended: its speed against MISSION_PAR_HOURS (0.5 at par, 1 when
// instant, 0 at twice par) times how cleanly it went (HP lost, as % of the
// units' total and summed over the mission; 300 = three whole armies), folded
// into the running momentum.
void score_mission() {
    const double speed = std::clamp(1.0 - g_hours / (2.0 * MISSION_PAR_HOURS), 0.0, 1.0);
    const double clean = std::clamp(1.0 - g_mission_stress / 300.0, 0.0, 1.0);
    const double score = speed * (0.5 + 0.5 * clean);
    const double before = g_momentum;
    g_momentum = MOMENTUM_KEEP * g_momentum + (1.0 - MOMENTUM_KEEP) * score;
    std::fprintf(stderr, "[director] mission %d scored %.2f (%.0f h, %.0f%% HP lost): momentum %.2f -> %.2f\n",
                 g_mission, score, g_hours, g_mission_stress, before, g_momentum);
    if (!g_momentum_path.empty()) {
        std::ofstream out(g_momentum_path, std::ios::trunc);
        out << g_momentum << "\n";
    }
}

uint32_t object_at(const uint8_t* rdram, int slot) {
    return rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
}

// Towns: count them, note liberations (grudge) and losses (intensity).
void observe_towns(const uint8_t* rdram) {
    const int count = static_cast<int>(rd32(rdram, LOCATION_COUNT));
    if (count <= 0 || count > MAX_LOCATIONS) {
        return;
    }
    const bool first = g_town_count != count;
    g_town_count = count;
    g_enemy_towns = 0;
    g_player_towns = 0;
    for (int i = 0; i < count; i++) {
        const uint16_t now = rd16(rdram, LOCATION_STATE + static_cast<uint32_t>(i) * LOCATION_STRIDE);
        const uint16_t was = g_town_state[i];
        g_enemy_towns += (now & 0x04u) != 0;
        g_player_towns += (now & 0x02u) != 0;
        if (!first && (was & 0x04u) && (now & 0x02u)) {
            // Liberated: the enemy wants it back; a stronghold (0x2000) more so.
            g_grudge[i] += (now & 0x2000u) ? 60.0 : 30.0;
            // No alert: the game shows the liberation, and the commander's
            // counterattack alert names the town when the enemy comes for it.
            std::fprintf(stderr, "[director] location %d liberated, grudge %.0f\n", i, g_grudge[i]);
        }
        if (!first && (was & 0x02u) && (now & 0x04u)) {
            g_intensity += TOWN_LOST_INTENSITY;
            g_grudge[i] = 0.0;
            std::fprintf(stderr, "[director] location %d retaken by the enemy\n", i);
            alerts::post(alerts::Kind::Threat, "The enemy retook a town");
        }
        g_town_state[i] = now;
    }
    if (g_enemy_towns_start < 0) {
        g_enemy_towns_start = g_enemy_towns;
    }
}

// Intensity (v2 5.1, Left 4 Dead's director): HP the player's units on the
// field lose raises it, as a percentage of their total HP.
void observe_player(const uint8_t* rdram) {
    double lost = 0.0, total = 0.0;
    bool seen[100] = {};
    for (int u = 0; u < PLAYER_UNIT_COUNT; u++) {
        const uint32_t object = object_at(rdram, u);
        if (object < 0x80000000u || object >= 0x80800000u || (rd32(rdram, object) & 0x11u) != 0x11u) {
            continue;
        }
        const uint32_t unit = UNIT_TABLE + static_cast<uint32_t>(u) * UNIT_STRIDE;
        for (uint32_t m = 0; m < 5; m++) {
            const int ref = rd8(rdram, unit + 0x02 + m);
            if (ref < 1 || ref >= 100) {
                continue;
            }
            const uint32_t c = PLAYER_CHAR_TABLE + static_cast<uint32_t>(ref) * 0x38u;
            const uint16_t hp = rd16(rdram, c + 0x18);  // +0x18 current, +0x16 maximum
            total += rd16(rdram, c + 0x16);
            if (g_have_player_hp && g_player_hp[ref] > hp) {
                lost += g_player_hp[ref] - hp;
                if (hp == 0) {
                    // A character brought down counts on top of the HP.
                    g_intensity += MEMBER_DOWN_INTENSITY;
                    g_members_down++;
                }
            }
            g_player_hp[ref] = hp;
            seen[ref] = true;
        }
    }
    for (int r = 0; r < 100; r++) {
        if (!seen[r]) {
            g_player_hp[r] = 0;
        }
    }
    g_have_player_hp = true;
    if (total > 0.0 && lost > 0.0) {
        g_intensity += 100.0 * lost / total;
        g_mission_stress += 100.0 * lost / total;
    }
}

// A unit's classes for an alert: "Witch + Knight, Knight".
std::string names(const uint8_t* rdram, const Plan& plan) {
    std::string text;
    for (size_t i = 0; i < plan.classes.size(); i++) {
        text += (i == 0 ? "" : i == 1 ? " + " : ", ") + class_name(rdram, plan.classes[i]);
    }
    return text;
}

std::string describe(const uint8_t* rdram, const Plan& plan) {
    if (plan.classes.empty()) {
        return "-";
    }
    std::string text = class_name(rdram, plan.classes[0]);
    for (size_t i = 1; i < plan.classes.size(); i++) {
        text += (i == 1 ? " + " : ", ") + class_name(rdram, plan.classes[i]);
    }
    return text + "  (" + std::to_string(plan.cost) + ")";
}

std::string pool_text(const uint8_t* rdram, const std::map<int, int>& pool) {
    std::string text;
    for (const auto& [cls, n] : pool) {
        if (!text.empty()) {
            text += ", ";
        }
        text += class_name(rdram, cls) + " " + std::to_string(n);
    }
    return text.empty() ? "-" : text;
}

}  // namespace

namespace {

// A new war: a fresh seed, saved so a restart of the app keeps the same war.
void roll_war_seed(const char* why) {
    const uint64_t now = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) ^
                         static_cast<uint64_t>(std::time(nullptr));
    g_war_seed = static_cast<uint32_t>(now ^ (now >> 32)) | 1u;
    if (!g_seed_path.empty()) {
        std::ofstream out(g_seed_path, std::ios::trunc);
        out << std::hex << g_war_seed << "\n";
    }
    std::fprintf(stderr, "[director] war seed %08X (%s)\n", g_war_seed, why);
}

}  // namespace

void tick(uint8_t* rdram) {
    if (rdram == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    const int scene = rd16(rdram, SCENE_ADDRESS);
    const int mission = rd8(rdram, MISSION_INDEX);
    // OGRE_DIRECTOR_FORCE=1 runs the observer on any screen, for testing it
    // with units planted through the live console.
    static const bool force = [] {
        const char* value = std::getenv("OGRE_DIRECTOR_FORCE");
        return value != nullptr && value[0] == '1';
    }();
    if (scene == SCENE_NAME_ENTRY && g_last_scene != SCENE_NAME_ENTRY) {
        roll_war_seed("new game");
        g_momentum = 0.0;
        if (!g_momentum_path.empty()) {
            std::ofstream out(g_momentum_path, std::ios::trunc);
            out << g_momentum << "\n";
        }
    }
    g_last_scene = scene;
    g_on_mission = scene == SCENE_MISSION || force;
    if (!g_on_mission) {
        return;
    }
    if (mission != g_mission) {
        // The last mission counts toward momentum when it was played (12
        // in-game hours or more) and the player held half its towns at the end;
        // a load into another mission is not scored.
        if (g_mission >= 0 && g_hours >= 12.0 && g_town_count > 0 && 2 * g_player_towns >= g_town_count) {
            score_mission();
        }
        reset_mission(mission);
        std::fprintf(stderr, "[director] mission %d\n", mission);
    }
    g_base_level = rd8(rdram, BASE_LEVEL);
    observe_units(rdram);
    spoils_tick(rdram);
    if (g_first_average == 0.0 && g_generic_units > 0) {
        g_first_average = g_generic_cost_sum / g_generic_units;
    }
    const uint32_t game_frame = (static_cast<uint32_t>(rd8(rdram, GAME_FRAME_COUNTER)) << 24) |
                                (static_cast<uint32_t>(rd8(rdram, GAME_FRAME_COUNTER + 1)) << 16) |
                                (static_cast<uint32_t>(rd8(rdram, GAME_FRAME_COUNTER + 2)) << 8) |
                                rd8(rdram, GAME_FRAME_COUNTER + 3);
    const uint32_t step = g_have_game_frame ? game_frame - g_last_game_frame : 0;
    g_last_game_frame = game_frame;
    g_have_game_frame = true;
    if (step == 0 || step > MAX_FRAME_STEP) {
        // No new game frame since the last pass (or a jump): nothing accrues.
        return;
    }
    g_frames += step;
    // In-game hours since the last pass, from the game's own clock; a jump (a
    // load, a mission change) adds nothing.
    const uint32_t clock = rd32(rdram, GAME_CLOCK);
    double dh = 0.0;
    if (g_have_clock && clock > g_last_clock && clock - g_last_clock < (4u << 16)) {
        dh = static_cast<double>(clock - g_last_clock) / 65536.0;
    }
    g_last_clock = clock;
    g_have_clock = true;
    g_hours += dh;
    loot_tick(rdram);
    observe_towns(rdram);
    observe_player(rdram);
    g_intensity = std::max(0.0, g_intensity - INTENSITY_DECAY * dh);
    const double average = g_generic_units > 0 ? g_generic_cost_sum / g_generic_units : 0.0;
    const int level = tactics_level();

    // The war chest: once the mission's unit prices are known, momentum times
    // 0 / 1 / 2 / 3 campaign units (EASY / NORMAL / HARD / VETERAN).
    if (!g_chest_given && average > 0.0) {
        g_chest_given = true;
        static const double kChestUnits[4] = {0.0, 1.0, 2.0, 3.0};
        g_chest = g_momentum * kChestUnits[std::clamp(level, 0, 3)] * average * 1.2;
        if (g_chest >= 1.0) {
            g_campaign_bank += g_chest;
            // No alert: a warning that good play is being answered reads as a
            // penalty. The DEBUG tab shows it.
            std::fprintf(stderr, "[director] war chest %.0f (momentum %.2f)\n", g_chest, g_momentum);
        }
    }

    // The last stand (NORMAL and up): the player has just taken enough towns to
    // stop the enemy's income, or a player unit reached the boss first.
    const bool towns_taken = g_town_count > 0 && g_player_towns >= PLAYER_HOLD_STOP * g_town_count && g_enemy_towns > 0;
    if (!g_last_stand_done && level >= 1 && average > 0.0 && (towns_taken || g_boss_threat)) {
        g_last_stand_done = true;
        g_last_stand = true;
        g_stand_boss = !towns_taken;
        // The floor is in campaign budgets (plan_unit spends up to 1.6 x the
        // average), so it always buys that many units.
        static const double kFloorUnits[4] = {0.0, 1.0, 2.0, 2.0};
        g_campaign_bank = std::max(g_campaign_bank, kFloorUnits[std::clamp(level, 0, 3)] * average * 1.6);
        g_push = false;
        if (g_phase != Phase::BuildUp) {
            enter_phase(Phase::BuildUp, 0.0);
        }
        // The enemy commander posts the one alert, with the target.
        std::fprintf(stderr, "[director] last stand%s: bank %.0f\n", g_stand_boss ? " at the boss" : "",
                     g_campaign_bank);
    }

    // Pacing.
    bool wave_alive = std::any_of(g_pending.begin(), g_pending.end(), [](const Plan& p) { return p.kind != 2; });
    for (const auto& [slot, unit] : g_director_units) {
        wave_alive |= unit.kind != 2;
    }
    switch (g_phase) {
        case Phase::BuildUp:
            if (g_intensity >= INTENSITY_RELAX && !g_last_stand) {
                // Hurt before the wave went out: ease off now.
                g_push = false;
                enter_phase(Phase::Fade, FADE_HOURS);
            }
            break;
        case Phase::Peak:
            g_phase_peak = std::max(g_phase_peak, g_intensity);
            if (g_intensity >= INTENSITY_RELAX || !wave_alive || hours_on_mission() >= g_phase_until) {
                g_push = level >= 2 && g_phase_peak < COAST_INTENSITY;
                g_last_stand = false;
                enter_phase(Phase::Fade, FADE_HOURS);
            }
            break;
        case Phase::Fade:
            if (hours_on_mission() >= g_phase_until) {
                Rng rng{(g_war_seed ^ static_cast<uint32_t>(g_frames)) | 1u};
                enter_phase(Phase::Relax, 24.0 + static_cast<double>(rng.next() % 25));  // one to two days
                g_intensity = 0.0;
                // The cycle's one calm alert: the player's breathing room.
                alerts::post(alerts::Kind::Good, "The enemy pulls back to regroup");
            }
            break;
        case Phase::Relax:
            if (hours_on_mission() >= g_phase_until) {
                enter_phase(Phase::BuildUp, 0.0);
            }
            break;
    }

    // Income: half during FADE.
    const double per_frame = rate_per_hour() * dh * (g_phase == Phase::Fade ? FADE_INCOME : 1.0);
    g_campaign_bank += per_frame * CAMPAIGN_SHARE;
    g_patrol_bank += per_frame * PATROL_SHARE;
    // v2 5.3 supply trains: one every 8..16 director hours while the enemy holds a
    // town and is not resting. A leader alone, free to the enemy, worth spoils.
    // Days apart and never two at once (developer playtest 2026-10-30): the first
    // after one to two days, the next two to four days after the last one ends.
    bool caravan_out = std::any_of(g_pending.begin(), g_pending.end(), [](const Plan& p) { return p.kind == 2; });
    for (const auto& [slot, unit] : g_director_units) {
        caravan_out |= unit.kind == 2;
    }
    if (g_next_supply == 0.0) {
        Rng first{(g_war_seed ^ static_cast<uint32_t>(g_mission) ^ 0xCA7Au) | 1u};
        g_next_supply = hours_on_mission() + 24.0 + static_cast<double>(first.next() % 25);
    }
    if (caravan_out && g_next_supply < hours_on_mission() + 48.0) {
        g_next_supply = hours_on_mission() + 48.0;  // the clock restarts when this one ends
    }
    if (!caravan_out && hours_on_mission() >= g_next_supply && !relaxing() && g_enemy_towns > 0 &&
        g_pending.size() < MAX_PENDING && !g_leaders.empty()) {
        // It stops at an enemy town (the garrisons' hold-position AI) until taken.
        Rng rng{(g_war_seed ^ static_cast<uint32_t>(g_frames) ^ 0x5A5Au) | 1u};
        g_next_supply = hours_on_mission() + 48.0 + static_cast<double>(rng.next() % 49);
        Plan supply = plan_unit(rdram, 1.0e9, 0, rng);
        if (!supply.classes.empty()) {
            supply.kind = 2;
            supply.ready_at = hours_on_mission() + 6.0;
            g_pending.push_back(supply);
            // The alert comes when it arrives (director_spawn.cpp).
        }
    }
    // Credits keep accruing during a relax phase, up to three campaign units' worth.
    const double bank_cap = 3.0 * std::max(1.0, average * 1.2);
    g_campaign_bank = std::min(g_campaign_bank, bank_cap);
    g_patrol_bank = std::min(g_patrol_bank, bank_cap);
    int wave = 0;
    double oldest = -1.0;
    for (const Plan& p : g_pending) {
        if (p.kind != 2) {
            wave++;
            oldest = oldest < 0.0 ? p.ready_at : std::min(oldest, p.ready_at);
        }
    }
    const int wave_size = wave_target(g_mission) + wave_extra();
    // A last stand attacks with what it could buy: it releases once the bank
    // cannot pay for the next campaign unit.
    const bool stand_spent = g_last_stand && !g_next_campaign.classes.empty() &&
                             g_campaign_bank < g_next_campaign.cost;
    if (!g_wave_release && wave > 0 && g_phase == Phase::BuildUp &&
        (wave >= wave_size || hours_on_mission() - oldest >= WAVE_MAX_WAIT * forming_scale() || stand_spent)) {
        g_wave_release = true;
        std::fprintf(stderr, "[director] wave of %d of %d released (%.0f h)%s\n", wave, wave_size,
                     hours_on_mission(), g_last_stand ? ", last stand" : "");
        // No size or classes (developer, 2026-10-30: the alert need not say
        // what is attacking).
        alerts::post(alerts::Kind::Threat, "The enemy attacks");
        g_push = false;
        enter_phase(Phase::Peak, PEAK_MAX_HOURS);
    }
    if (g_wave_release && wave == 0) {
        g_wave_release = false;
    }
    const bool can_buy = !g_wave_release && wave < wave_size && g_pending.size() < MAX_PENDING;
    if (average > 0.0 && g_phase == Phase::BuildUp) {
        const uint32_t bought = static_cast<uint32_t>(g_sent_campaign * 2 + g_sent_patrol);
        if (g_next_campaign.classes.empty()) {
            Rng rng{(g_war_seed ^ static_cast<uint32_t>(g_mission * 7919) ^ (bought * 2654435761u)) | 1u};
            const int cap = member_cap(g_mission, g_hours);
            g_next_campaign = plan_unit(rdram, average * unit_budget(cap), cap, rng);
        }
        if (g_next_patrol.classes.empty()) {
            Rng rng{(g_war_seed ^ static_cast<uint32_t>(g_mission * 104729) ^ (bought * 40503u)) | 1u};
            // A patrol used to get 0.6 of an average unit, which on mission 1
            // paid for a lone Wizard (29 of about 50) and no member.
            const int cap = patrol_cap(g_mission, g_hours);
            g_next_patrol = plan_unit(rdram, average * unit_budget(cap), cap, rng);
        }
        const Plan campaign = g_next_campaign;
        // Buy only when the unit can be queued; otherwise the credits stay banked.
        if (campaign.cost > 0 && g_campaign_bank >= campaign.cost && can_buy) {
            g_campaign_bank -= campaign.cost;
            g_sent_campaign++;
            Plan queued = campaign;
            queued.kind = 1;
            queued.ready_at = hours_on_mission() + CAMPAIGN_FORMING * forming_scale();
            g_pending.push_back(queued);
            g_next_campaign = {};
            // One alert per wave, for its first unit (developer, 2026-10-30:
            // too many alerts read as background noise); the DEBUG tab lists
            // every unit.
            if (wave == 0) {
                alerts::post(alerts::Kind::Threat, "Enemy forces are gathering for an attack");
            }
            std::fprintf(stderr, "[director] bought campaign unit: %s (%.0f h)\n",
                         describe(rdram, campaign).c_str(), hours_on_mission());
            wave++;  // the patrol bought on the same pass counts after it
        }
        const Plan patrol = g_next_patrol;
        // Buy only when the unit can be queued; otherwise the credits stay banked.
        // A patrol fills any open place in the wave. It used to be allowed only
        // as the first unit (wave + 1 < wave_size), so the second place waited
        // for a campaign unit the bank rarely reached within WAVE_MAX_WAIT and
        // most waves attacked with one unit (developer playtest 2026-10-30:
        // 8 of 11 waves on mission 3). A later place goes to a patrol only once
        // the wave has waited half of WAVE_MAX_WAIT for a campaign unit.
        const bool patrol_place =
            wave == 0 || (oldest >= 0.0 && hours_on_mission() - oldest >= WAVE_MAX_WAIT * forming_scale() * 0.5);
        if (patrol.cost > 0 && g_patrol_bank >= patrol.cost && !g_wave_release && wave < wave_size && patrol_place &&
            g_pending.size() < MAX_PENDING) {
            g_patrol_bank -= patrol.cost;
            g_sent_patrol++;
            Plan queued = patrol;
            queued.ready_at = hours_on_mission() + PATROL_FORMING * forming_scale();
            g_pending.push_back(queued);
            g_next_patrol = {};
            if (wave == 0) {
                alerts::post(alerts::Kind::Threat, "Enemy forces are gathering for an attack");
            }
            std::fprintf(stderr, "[director] bought patrol unit: %s (%.0f h)\n",
                         describe(rdram, patrol).c_str(), hours_on_mission());
        }
    }
}

std::vector<std::pair<std::string, std::string>> report() {
    std::vector<std::pair<std::string, std::string>> rows;
    const uint8_t* rdram = ultramodern::get_rdram_base();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (rdram == nullptr || g_mission < 0) {
        rows.emplace_back("Director", "NO MISSION YET");
        return rows;
    }
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "id %d  base level %d%s", g_mission, g_base_level,
                  g_on_mission ? "" : "  (paused)");
    rows.emplace_back("Mission", buffer);
    std::snprintf(buffer, sizeof(buffer), "%08X  units up to %d members", g_war_seed, member_cap(g_mission, g_hours));
    rows.emplace_back("War seed", buffer);
    std::snprintf(buffer, sizeof(buffer), "%d enemy / %d player of %d (start %d), income x%.2f",
                  g_enemy_towns, g_player_towns, g_town_count, g_enemy_towns_start, town_factor());
    rows.emplace_back("Towns", buffer);
    std::snprintf(buffer, sizeof(buffer), "%.0f  (%d characters down)", g_intensity, g_members_down);
    rows.emplace_back("Intensity", buffer);
    const double left = g_phase_until - hours_on_mission();
    std::snprintf(buffer, sizeof(buffer), "%s%s%s%s", phase_name(g_phase),
                  g_phase != Phase::BuildUp && left > 0.0 ? ("  " + std::to_string(static_cast<int>(left)) + " h left").c_str() : "",
                  g_push ? "  pushing" : "", g_last_stand ? "  LAST STAND" : "");
    rows.emplace_back("Phase", buffer);
    std::snprintf(buffer, sizeof(buffer), "%.2f  war chest %.0f  wave +%d%s", g_momentum, g_chest, wave_extra(),
                  g_last_stand_done ? "  last stand used" : "");
    rows.emplace_back("Momentum", buffer);
    std::snprintf(buffer, sizeof(buffer), "day %.1f of the mission  coeff %.2f", g_hours / 24.0,
                  coeff());
    rows.emplace_back("Time", buffer);
    std::snprintf(buffer, sizeof(buffer), "%.0f/h  camp %.0f  patrol %.0f  towns n/a",
                  rate_per_hour(), g_campaign_bank, g_patrol_bank);
    rows.emplace_back("Credits", buffer);
    rows.emplace_back("Leaders", pool_text(rdram, g_leaders));
    rows.emplace_back("Members", pool_text(rdram, g_members));
    const double average = g_generic_units > 0 ? g_generic_cost_sum / g_generic_units : 0.0;
    (void)average;
    rows.emplace_back("Next campaign", describe(rdram, g_next_campaign));
    rows.emplace_back("Next patrol", describe(rdram, g_next_patrol));
    std::snprintf(buffer, sizeof(buffer), "%d campaign, %d patrol, %d spawned, %zu waiting",
                  g_sent_campaign, g_sent_patrol, g_spawned, g_pending.size());
    rows.emplace_back("Bought", buffer);
    rows.emplace_back("Rewards", loot_text());
    return rows;
}

// --- Phase 2: the garrison ------------------------------------------------
//
// At mission load, bank R's loader (func_ovlR_80215C38) opens enemydat.bin
// (0x802164D8) and then builds one unit per spawn entry from its template. The
// call below runs between the two (config/hooks.toml), so the game's own
// constructor builds whatever the templates say.
//
// Every generic unit's template is rebuilt from the mission's own pool: a new
// leader from the classes that lead generic units on this mission, and up to
// two member groups from the classes that serve in them, within +-10% of the
// original unit's cost. The boss (entry 0) and story units (any class 81+) are
// left alone. The template buffer is cached by the game's file system between
// missions, so the original bytes are saved once and restored before each
// rewrite.
//
// Template layout (35 bytes, tools/mission_data.py): +0x00 leader class,
// +0x01 leader level offset, +0x02..+0x05 leader items, +0x06 leader square
// (1-based); group 1 at +0x07 (class, 1 = generic soldiers), +0x08 level
// offset, +0x09..+0x0C items, +0x0D..+0x0F squares; group 2 at +0x10..+0x18.
// Squares 1..3 are the back row, 4..6 the middle, 7..9 the front.
//
// OGRE_DIRECTOR_GARRISON=0 turns it off.

namespace {

constexpr int TEMPLATE_COUNT = 556;
constexpr uint32_t TEMPLATE_SIZE = 35u;
constexpr uint32_t SPAWN_ENTRY = 18u;

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    rdram[(address & 0x1FFFFFFFu) ^ 3u] = value;
}

uint32_t g_template_buffer = 0;
std::vector<uint8_t> g_template_original;
// The mission's rebuilt generic templates, kept for phase 3 (take_spawn).
std::vector<std::vector<uint8_t>> g_spawn_templates;


struct Template {
    int leader = 0;
    int leader_level = 0;
    int square = 0;
    struct Group {
        int cls = 0;
        int level = 0;
        int count = 0;
    } groups[2];
};

int signed_byte(uint8_t value) {
    return value < 128 ? value : value - 256;
}

Template read_template(const uint8_t* rdram, uint32_t t) {
    Template out;
    out.leader = rd8(rdram, t);
    out.leader_level = signed_byte(rd8(rdram, t + 1));
    out.square = rd8(rdram, t + 6);
    for (int g = 0; g < 2; g++) {
        const uint32_t base = t + (g == 0 ? 0x07u : 0x10u);
        int count = 0;
        for (uint32_t i = 0; i < 3; i++) {
            count += rd8(rdram, base + 6 + i) != 0;
        }
        if (count > 0) {
            out.groups[g] = {rd8(rdram, base), signed_byte(rd8(rdram, base + 1)), count};
        }
    }
    return out;
}

bool is_story(const Template& t) {
    if (story_class(t.leader)) {
        return true;
    }
    for (const auto& g : t.groups) {
        if (g.count > 0 && story_class(g.cls)) {
            return true;
        }
    }
    return false;
}

double template_cost(const uint8_t* rdram, const Template& t, int base) {
    double cost = card_cost(rdram, t.leader, base + t.leader_level) * LEADER_MULT;
    for (const auto& g : t.groups) {
        if (g.count > 0) {
            cost += card_cost(rdram, g.cls, base + g.level) * g.count;
        }
    }
    return cost;
}

// Free squares in the order a class wants them: its best row first (centre,
// then the sides), then the other rows.
std::vector<int> squares_for(const uint8_t* rdram, int cls, const bool used[10]) {
    static const int rows[3][3] = {{2, 1, 3}, {5, 4, 6}, {8, 7, 9}};  // back, middle, front
    const int best = best_row(rdram, cls);
    const int order[3] = {best, best == 0 ? 1 : (best == 2 ? 1 : 2), best == 0 ? 2 : 0};
    std::vector<int> squares;
    for (int r : order) {
        for (int s : rows[r]) {
            if (!used[s]) {
                squares.push_back(s);
            }
        }
    }
    return squares;
}

void write_group(uint8_t* rdram, uint32_t base, int cls, int level, int count, int leader_square,
                 bool used[10], const uint8_t* rdram_ro) {
    wr8(rdram, base, static_cast<uint8_t>(cls));
    wr8(rdram, base + 1, static_cast<uint8_t>(level));
    for (uint32_t i = 0; i < 4; i++) {
        wr8(rdram, base + 2 + i, 0);  // items: 0 = the class's defaults
    }
    std::vector<int> squares = count > 0 ? squares_for(rdram_ro, cls, used) : std::vector<int>{};
    for (uint32_t i = 0; i < 3; i++) {
        uint8_t square = 0;
        if (static_cast<int>(i) < count && i < squares.size()) {
            square = static_cast<uint8_t>(squares[i]);
            used[square] = true;
        }
        wr8(rdram, base + 6 + i, square);
    }
    (void)leader_square;
}

struct Built {
    int leader = 0;
    int a = 0;
    int na = 0;
    int b = 0;
    int nb = 0;
    double cost = 0.0;
};

// Rewrites the template at `address`: a new leader from `leaders`, and up to
// two member groups from `members`, while the unit stays within 110% of
// `budget` and has room (five members, three per group). Level offsets and the
// leader's square are kept. Returns leader 0 when the pools are empty.
// `exact`: `members` holds counts (class -> members) that are written as they
// are, at most two classes of three; take_spawn uses it for a plan already
// priced, which used to be rolled again here and could lose a member.
Built rebuild(uint8_t* rdram, uint32_t address, const Template& old, const std::map<int, int>& leaders,
              const std::map<int, int>& members, int base, double budget, int cap, Rng& rng,
              bool exact = false) {
    Built built;
    built.leader = rng.pick(leaders, -1);
    if (built.leader == 0) {
        return built;
    }
    const int member_level = old.groups[0].count > 0 ? old.groups[0].level : 0;
    built.cost = card_cost(rdram, built.leader, base + old.leader_level) * LEADER_MULT;
    if (exact) {
        for (const auto& [cls, n] : members) {
            if (built.a == 0) {
                built.a = cls;
                built.na = std::min(n, 3);
            }
            else if (built.b == 0) {
                built.b = cls;
                built.nb = std::min(n, 3);
            }
        }
        cap = 0;
    }
    else {
        built.a = rng.pick(members, -1);
        built.b = rng.pick(members, built.a);
    }
    // About one unit in four is built a member short; elite members stop at the
    // difficulty's cap.
    if (!exact && cap > 1 && rng.next() % 4 == 0) {
        cap--;
    }
    int elites = 0;
    for (int step = 0; step < 8 && built.na + built.nb < cap; step++) {
        const bool use_a = built.b == 0 || step % 2 == 0;
        int& n = use_a ? built.na : built.nb;
        const int cls = use_a ? built.a : built.b;
        if (cls == 0 || n >= 3) {
            continue;
        }
        const bool elite = is_elite(rdram, cls, base + member_level);
        if (elite && elites >= elite_cap(rd8(rdram, MISSION_INDEX))) {
            continue;
        }
        elites += elite ? 1 : 0;
        const double c = card_cost(rdram, cls, base + member_level);
        if (built.cost + c > budget * 1.10) {
            continue;
        }
        built.cost += c;
        n++;
    }
    if (built.nb == 0) {
        built.b = 0;
    }

    wr8(rdram, address + 0x00, static_cast<uint8_t>(built.leader));
    if (built.leader != old.leader) {
        for (uint32_t i = 0; i < 4; i++) {
            wr8(rdram, address + 0x02 + i, 0);
        }
    }
    bool used[10] = {};
    if (old.square >= 1 && old.square <= 9) {
        used[old.square] = true;
    }
    write_group(rdram, address + 0x07, built.a, member_level, built.na, old.square, used, rdram);
    write_group(rdram, address + 0x10, built.b, member_level, built.nb, old.square, used, rdram);
    return built;
}

// --- Enemy weapons and formations (docs/anotherworld/design-balance-master-plan.md) ------
//
// Both act on a generic template after the director's rebuild (or on the
// vanilla template when the director is off), before the game's constructor
// reads it.

uint32_t item_record(int id);
int item_price(const uint8_t* rdram, int id);
bool item_eligible(const uint8_t* rdram, int id);

constexpr int ITEM_COUNT_GARRISON = 278;      // template item ids run 0..0x115 (ITEM_COUNT)
constexpr uint32_t CLASS_SIZE_BYTE = 0x04u;   // 1 normal, 2 large
constexpr uint32_t CLASS_BASIC_SET = 0x2Eu;   // four u16 items: weapon, body, off-hand, head

int class_stat(const uint8_t* rdram, int cls, int stat) {  // 0 HP .. 6 DEX, the level-1 base
    return rd8(rdram, class_record(cls) + 0x08 + 4 * static_cast<uint32_t>(stat) + 1);
}

int row_attacks(const uint8_t* rdram, int cls, int row) {  // 0 back, 1 middle, 2 front
    static const uint32_t offset[3] = {0x3C, 0x3A, 0x38};
    const int n = rd8(rdram, class_record(cls) + offset[row]);
    return n <= 9 ? n : 0;
}

bool is_large(const uint8_t* rdram, int cls) {
    return cls > 0 && cls < CLASS_COUNT && rd8(rdram, class_record(cls) + CLASS_SIZE_BYTE) == 2;
}

// ENEMY WEAPONS. A group's first item choice (template +0x02 of the group,
// u16, 0 = the class's own gear) replaces the class's basic weapon in the
// battle (LordlyCaliber squads.js: a choice replaces the basic item of the same
// slot). The pick is a weapon of the basic weapon's category, priced above it
// and within the mission's band (band_max, the loot band), uniform among the
// candidates so units differ; a leader picks from the stronger half.
int pick_weapon(const uint8_t* rdram, int cls, int mission, bool leader, Rng& rng) {
    if (cls <= 1 || cls >= CLASS_COUNT) {
        return 0;
    }
    const int basic = rd16(rdram, class_record(cls) + CLASS_BASIC_SET);
    if (basic <= 0 || basic >= ITEM_COUNT_GARRISON) {
        return 0;
    }
    const int category = rd8(rdram, item_record(basic) + 0x04);
    if (category == 0 || category == 0xFF || category > 13) {  // 1..13 are the weapon categories
        return 0;
    }
    // Chance of an upgrade: leaders 75%; members from 35% rising 2% a mission to 75%.
    const int chance = leader ? 75 : std::min(75, 35 + 2 * mission);
    if (static_cast<int>(rng.next() % 100) >= chance) {
        return 0;
    }
    const int floor = item_price(rdram, basic);
    const int ceiling = std::max(floor, band_max(mission));
    std::vector<std::pair<int, int>> candidates;  // (price, id)
    for (int id = 1; id < ITEM_COUNT_GARRISON; id++) {
        if (id == basic || !item_eligible(rdram, id) || rd8(rdram, item_record(id) + 0x04) != category) {
            continue;
        }
        const int price = item_price(rdram, id);
        if (price > floor && price <= ceiling) {
            candidates.emplace_back(price, id);
        }
    }
    if (candidates.empty()) {
        return 0;
    }
    std::sort(candidates.begin(), candidates.end());
    const size_t first = leader ? candidates.size() / 2 : 0;
    return candidates[first + rng.next() % (candidates.size() - first)].second;
}

void write_u16(uint8_t* rdram, uint32_t address, int value) {
    wr8(rdram, address, static_cast<uint8_t>(value >> 8));
    wr8(rdram, address + 1, static_cast<uint8_t>(value));
}

int arm_template(uint8_t* rdram, uint32_t address, int mission, Rng& rng) {
    const Template t = read_template(rdram, address);
    int armed = 0;
    if (const int w = pick_weapon(rdram, t.leader, mission, true, rng); w != 0) {
        write_u16(rdram, address + 0x02, w);
        armed++;
    }
    for (int g = 0; g < 2; g++) {
        if (t.groups[g].count == 0) {
            continue;
        }
        if (const int w = pick_weapon(rdram, t.groups[g].cls, mission, false, rng); w != 0) {
            write_u16(rdram, address + (g == 0 ? 0x09u : 0x12u), w);
            armed++;
        }
    }
    return armed;
}

// ENEMY FORMATIONS. Squares 1..3 are the back row, 4..6 the middle, 7..9 the
// front; the centre square of a row is filled first. Each member gets the row
// where its class has the most attacks (a tie: front for sturdy classes, back
// for frail ones, VIT below 45), a leader of a unit with two or more members
// leans away from the front, and a share of members takes its second row
// instead (vanilla puts 52% of members outside their best row; this keeps a
// smaller, stage-based share). When the front row would stay empty, the
// sturdiest member moves up to screen the rest. Large members take one square
// and may stand next to anything, other large members included: the builder
// func_ovlR_80215360 copies the template's squares into the unit record without
// reading a class size, so the vanilla "no neighbour" rule lives only in the
// game's data and the Organize rules (R1 in the plan). They are seated first,
// since they need the room most.
struct Piece {
    int cls = 0;
    bool leader = false;
    int group = -1;       // -1 the leader, else 0 or 1
    int rows[3] = {};     // preferred rows, best first
    int gap = 0;          // how much the best row matters
    bool large = false;
    int square = 0;
};

int off_row_percent(int mission) {
    return mission <= 9 ? 35 : mission <= 29 ? 25 : 15;
}

void rank_rows(const uint8_t* rdram, Piece& p, bool lead_from_back, int mission, Rng& rng) {
    const bool frail = class_stat(rdram, p.cls, 2) < 45;
    double score[3];
    for (int row = 0; row < 3; row++) {
        score[row] = row_attacks(rdram, p.cls, row);
    }
    score[2] += frail ? -0.25 : 0.25;
    score[0] += frail ? 0.25 : -0.25;
    if (p.leader && lead_from_back) {
        score[2] -= 0.5;
    }
    int order[3] = {2, 1, 0};
    std::sort(order, order + 3, [&](int a, int b) { return score[a] > score[b]; });
    p.gap = static_cast<int>((score[order[0]] - score[order[1]]) * 4);
    if (static_cast<int>(rng.next() % 100) < off_row_percent(mission)) {
        std::swap(order[0], order[1]);
    }
    std::copy(order, order + 3, p.rows);
}

// ENEMY RANKS, part 1: promotion. A generic group may take the next class of
// its line. Two limits apply, and the lower one wins:
//
// - The campaign: the share of promoted groups grows with the mission, 0 up to
//   mission 6, then 2.5% a mission, at most 75% (mission 36 on). The war
//   moves from base classes through a mix to mostly elite troops, and a quarter
//   of the late army stays base class (developer, 2026-10-30: promotion must
//   follow the player's own progression, not switch on at once).
// - The level: the group's level (base level 0x801F1055 + the template's
//   offset) must reach the line's level. The levels are the class record's
//   +0x44 byte next to +0x43 "promotes from" (tools/class_catalog.py); no game
//   code reading it is confirmed, and the game's class changes run on stats and
//   alignment, so it serves only as a floor. The beast, golem, hawk and raider
//   lines (docs/anotherworld/design-class-rebalance.md section 3) use 16.
//
// A promoted class may promote again (Fighter -> Knight -> Paladin) under the
// same limits. The themed late forms at the end of the table give classes with
// no line of their own a stronger form of the same kind: a Ghost becomes a Lich
// (the undead caster: two spells front and middle, three back, against the
// Ghost's one, one and two; no generic enemy unit uses the Lich).
bool flies(const uint8_t* rdram, int cls);
bool air_unit(const uint8_t* rdram, const Template& t);
int elite_members(const uint8_t* rdram, const Template& t, int base);

struct Promotion {
    int from;
    int level;
    int to[3];  // 0 ends the list; one is picked at random
};

constexpr Promotion PROMOTIONS[] = {
    {2, 10, {5, 6, 7}},     // Fighter -> Knight, Berserker, Fencer
    {4, 10, {15, 13, 18}},  // Amazon -> Valkyrie, Archer, Cleric
    {5, 15, {19, 20, 0}},   // Knight -> Paladin, Dragoon
    {6, 15, {21, 0, 0}},    // Berserker -> Black Knight
    {7, 17, {22, 0, 0}},    // Fencer -> Sword Master
    {8, 19, {23, 0, 0}},    // Phalanx -> Cataphract
    {9, 15, {24, 0, 0}},    // Beast Tamer -> Beast Master
    {10, 17, {25, 0, 0}},   // Doll Master -> Enchanter
    {11, 15, {26, 0, 0}},   // Ninja -> Ninja Master
    {12, 17, {27, 0, 0}},   // Wizard -> Archmage
    {13, 17, {28, 0, 0}},   // Archer -> Diana
    {14, 17, {29, 0, 0}},   // Dragon Tamer -> Dragon Master
    {15, 17, {30, 0, 0}},   // Valkyrie -> Freya
    {17, 19, {31, 0, 0}},   // Sorceress -> Siren
    {18, 15, {32, 0, 0}},   // Cleric -> Priest
    {39, 16, {40, 41, 0}},  // Hawkman -> Vultan, Raven
    {53, 16, {54, 0, 0}},   // Goblin -> Saturos
    {57, 16, {63, 0, 0}},   // Thunder Dragon -> Quetzalcoatl
    {58, 16, {64, 0, 0}},   // Red Dragon -> Flarebrass
    {59, 16, {65, 0, 0}},   // Earth Dragon -> Ahzi Dahaka
    {60, 16, {66, 0, 0}},   // Blue Dragon -> Hydra
    {61, 17, {67, 0, 0}},   // Platinum Dragon -> Bahamut
    {62, 17, {68, 0, 0}},   // Black Dragon -> Tiamat
    {69, 16, {70, 0, 0}},   // Wyrm -> Wyvern
    {71, 16, {72, 0, 0}},   // Griffin -> Opinincus
    {73, 16, {74, 0, 0}},   // Cockatrice -> Sphinx
    {75, 16, {76, 0, 0}},   // Hellhound -> Cerberus
    {78, 16, {79, 0, 0}},   // Golem -> Stone Golem
    {79, 17, {80, 0, 0}},   // Stone Golem -> Baldr Golem
    // Themed late forms.
    {48, 20, {38, 0, 0}},   // Ghost -> Lich
};

int promote_share(int mission) {
    return std::clamp(static_cast<int>((mission - 6) * 2.5), 0, 75);
}

int promote_class(int cls, int level, int mission, Rng& rng) {
    for (int step = 0; step < 2; step++) {
        const Promotion* found = nullptr;
        for (const Promotion& p : PROMOTIONS) {
            if (p.from == cls) {
                found = &p;
            }
        }
        if (found == nullptr || level < found->level) {
            return cls;
        }
        const int chance = promote_share(mission);
        if (static_cast<int>(rng.next() % 100) >= chance) {
            return cls;
        }
        int n = 0;
        while (n < 3 && found->to[n] != 0) {
            n++;
        }
        cls = found->to[rng.next() % static_cast<uint32_t>(n)];
    }
    return cls;
}

// Promotes the template's generic groups; the leader too unless `keep_leader`
// (a boss). A changed group's item choices are cleared (ENEMY WEAPONS picks
// again for the new class). Returns how many groups changed.
int promote_template(uint8_t* rdram, uint32_t address, int base, int mission, bool keep_leader, Rng& rng) {
    const Template t = read_template(rdram, address);
    const bool air = air_unit(rdram, t);
    int changed = 0;
    auto apply = [&](uint32_t group, int cls, int offset) {
        if (cls <= 1 || story_class(cls)) {
            return;
        }
        const int to = promote_class(cls, base + offset, mission, rng);
        if (to == cls || (air && !flies(rdram, to))) {  // an all-flying unit stays airborne
            return;
        }
        // The elite cap: count this group's members as elite after the change.
        if (is_elite(rdram, to, base + offset) && !is_elite(rdram, cls, base + offset)) {
            const Template now = read_template(rdram, address);
            int members_here = 0;
            for (const auto& g : now.groups) {
                if (g.count > 0 && g.cls == cls) {
                    members_here = g.count;
                }
            }
            if (elite_members(rdram, now, base) + std::max(1, members_here) > elite_cap(mission)) {
                return;
            }
        }
        wr8(rdram, group, static_cast<uint8_t>(to));
        for (uint32_t i = 0; i < 4; i++) {
            wr8(rdram, group + 2 + i, 0);
        }
        changed++;
    };
    if (!keep_leader) {
        apply(address, t.leader, t.leader_level);
    }
    for (int g = 0; g < 2; g++) {
        if (t.groups[g].count > 0) {
            apply(address + (g == 0 ? 0x07u : 0x10u), t.groups[g].cls, t.groups[g].level);
        }
    }
    return changed;
}

// ENEMY RANKS, part 2: mixed groups. A unit whose members are one class (one
// group of three or more) gives half of them a second class from the mission's
// own member pool, preferring a class that fights best from another row, so a
// unit of five Sword Masters becomes Sword Masters with casters or archers
// behind them. A large class is taken only while the unit stays within five
// places (a large member takes two).
// The class value (plan L1-b): what one member of a class is worth at a level,
// pricing its attacks. card_cost counts stats and the best row's attack count
// only, so a Gremlin's Kiss counted like a sword blow; this prices the kind and
// reach of each row's attack and the stat it runs on. The war director's
// credit economy keeps card_cost (the two do not scale together over levels);
// the refit, the mixed groups and the boss escorts choose with this.
//
//   survival = (HP + 0.6 VIT + 0.3 MEN + 0.2 AGI) / 100 at the level
//   damage   = the best row's count x weight x the attack's stat / 50
//   value    = 10 x survival x damage
//
// Weights by the attack table (0x8018AA9C, 0x10 per attack: +4 kind, +6 target
// pattern, +7 effect, +9 formula, +0x0A.. an extra effect): melee and bows 1.0
// (STR, DEX), a spell cast through gear 1.5 (INT; most hit several targets),
// breath 1.2 (the mean of STR and INT), a helpful special 0.9 (MEN), a sleep
// special 0.6, a debuff kiss 0.5, petrify 0.8, other damaging specials 1.0
// (INT). Reach: pattern 2 x1.5, 3 x1.6, 5 x1.4, 6 x2. At level 33: Gremlin 124,
// Ghost 252, Sphinx 511, Lich 574, Hawkman 193, Raven 415, Wizard 411,
// Archmage 817 (tools/class_value.py prints the same numbers from the ROM).
constexpr uint32_t ATTACK_TABLE_RAM = ::ogre::addr::kAttacks;

double class_value(const uint8_t* rdram, int cls, int level) {
    if (cls <= 0 || cls >= CLASS_COUNT) {
        return 0.0;
    }
    const uint32_t record = class_record(cls);
    double st[7];
    for (uint32_t i = 0; i < 7; i++) {
        st[i] = rd8(rdram, record + 0x08 + 4 * i + 1) + static_cast<double>(level) * rd8(rdram, record + 0x08 + 4 * i + 2);
    }
    const double survive = (st[0] + 0.6 * st[2] + 0.3 * st[4] + 0.2 * st[5]) / 100.0;
    double best = 0.0;
    for (uint32_t row = 0; row < 3; row++) {
        const int attack = rd8(rdram, record + 0x37 + 2 * row);
        const int count = rd8(rdram, record + 0x38 + 2 * row);
        if (count > 9) {
            continue;
        }
        const uint32_t a = ATTACK_TABLE_RAM + static_cast<uint32_t>(attack) * 0x10u;
        const int kind = rd8(rdram, a + 4);
        const int target = rd8(rdram, a + 6);
        const int effect = rd8(rdram, a + 7);
        const int formula = rd8(rdram, a + 9);
        const int extra0 = rd8(rdram, a + 0x0A);
        const int extra1 = rd8(rdram, a + 0x0B);
        double reach = 1.0;
        switch (target) {
            case 2: reach = 1.5; break;
            case 3: reach = 1.6; break;
            case 5: reach = 1.4; break;
            case 6: reach = 2.0; break;
            default: break;
        }
        double weight = 0.0;
        double power = st[3];  // INT
        if (kind == 0xFF || (kind == 0 && formula == 0)) {
            weight = 0.0;
        }
        else if (kind == 0 || kind == 1) {
            weight = reach;
            power = kind == 0 ? st[1] : st[6];  // STR, DEX
        }
        else if (kind == 3) {
            weight = 1.5;
        }
        else if (kind == 5) {
            weight = 1.2 * reach;
            power = (st[1] + st[3]) / 2.0;
        }
        else if (effect == 2) {
            weight = 0.9;
            power = st[4];  // MEN
        }
        else if (extra0 == 0x08 && extra1 == 0x04) {
            weight = 0.6 * reach;  // sleep
        }
        else if (extra0 == 0x08 && (extra1 == 0x05 || extra1 == 0x06) && target >= 9) {
            weight = 0.5;  // a debuff kiss
        }
        else if (effect == 3) {
            weight = 0.8;  // petrify
        }
        else {
            weight = reach;
        }
        best = std::max(best, count * weight * power / 50.0);
    }
    return 10.0 * survive * best;
}

// Elite classes: a class value at least twice the average class at that level.
// The ranking breaks cleanly there: the elder dragons (Ahzi Dahaka, Bahamut,
// Tiamat, Flarebrass, Hydra, Quetzalcoatl) stand at 3.0-3.5x, Baldr Golem at
// 2.1-2.3x, and every other class at 1.9x or less (Dragoon, Cerberus,
// Paladin). A unit may hold this many elite members (developer, 2026-10-30:
// NORMAL still sees them late, not in extreme teams; a leader with four
// Tiamats is a VETERAN thing): EASY none (one from mission 30), NORMAL one,
// HARD two, VETERAN four.
bool is_elite(const uint8_t* rdram, int cls, int level) {
    const LevelAverages& a = level_averages(rdram, level);
    return a.value > 0.0 && class_value(rdram, cls, level) >= 2.0 * a.value;
}

int elite_cap(int mission) {
    switch (tactics_level()) {
        case 0: return mission >= 30 ? 1 : 0;
        case 1: return 1;
        case 2: return 2;
        default: return 4;
    }
}

int elite_members(const uint8_t* rdram, const Template& t, int base) {
    int n = 0;
    for (const auto& g : t.groups) {
        if (g.count > 0 && is_elite(rdram, g.cls, base + g.level)) {
            n += g.count;
        }
    }
    return n;
}

// ENEMY RANKS, part 3: the refit. Late in the war a unit is kept true to what
// it is and given a plan (developer, 2026-10-30: a late flying unit of a
// Raven, Gremlins and Hawkmen is "a flying team with 0 strategy behind the
// picks"; a one-for-one swap does not fix it).
//
// - Theme. A unit whose leader and members all fly (class record +0x2C
//   movement type 1) stays all-flying: only flyers may join it. Any other unit
//   takes classes that share a family with its leader (the table below).
// - Roles. F fights from the front, R deals damage from the back (bows, spells,
//   breath), S heals or boosts. A late unit with no R gets one first; a unit of
//   four or more with no S gets one next; otherwise a weak group (a base-tier
//   class) takes a stronger class of its own role.
// - Story order. A class joins only from the mission where the game first
//   fields it in a generic unit (docs/mission-enemies.md debut table; Lich,
//   which the game never fields, from 30).
// - Places. Two small members may become one large member (a large member
//   takes two of the five places), so a flying unit's two Gremlins can become
//   one Sphinx, a flying caster.
//
// Each weak group is refit with the campaign share (promote_share), so the
// refit grows over the war like the promotions.
enum Family : uint16_t {
    HUMAN = 1, BEAST = 2, DRAGON = 4, UNDEAD = 8, DARK = 16, HOLY = 32,
    RAIDER = 64, LODIS = 128, FAERIE = 256, GOLEM = 512, HAWK = 1024,
};

struct ClassRole {
    int id;
    uint16_t family;
    char role;   // 'F', 'R' or 'S'
    int debut;   // first mission it may join a unit
};

constexpr ClassRole CLASS_ROLES[] = {
    {2, HUMAN, 'F', 0},              // Fighter
    {4, HUMAN, 'R', 6},              // Amazon
    {5, HUMAN, 'F', 1},              // Knight
    {6, HUMAN | DARK, 'F', 0},       // Berserker
    {7, HUMAN, 'F', 5},              // Fencer
    {8, HUMAN, 'F', 7},              // Phalanx
    {9, HUMAN | BEAST, 'F', 0},      // Beast Tamer
    {10, HUMAN | GOLEM, 'F', 4},     // Doll Master
    {11, HUMAN, 'F', 3},             // Ninja
    {12, HUMAN, 'R', 1},             // Wizard
    {13, HUMAN, 'R', 4},             // Archer
    {14, HUMAN | DRAGON, 'F', 3},    // Dragon Tamer
    {15, HUMAN, 'F', 3},             // Valkyrie
    {16, HUMAN | DARK, 'R', 1},      // Witch
    {17, HUMAN, 'R', 3},             // Sorceress
    {18, HUMAN | HOLY, 'S', 2},      // Cleric
    {19, HUMAN | HOLY, 'F', 10},     // Paladin
    {20, HUMAN | DRAGON, 'F', 25},   // Dragoon (the game: one unit, mission 43)
    {21, HUMAN | DARK, 'F', 10},     // Black Knight
    {22, HUMAN, 'F', 17},            // Sword Master
    {23, HUMAN, 'F', 17},            // Cataphract
    {24, HUMAN | BEAST, 'F', 11},    // Beast Master
    {25, HUMAN | GOLEM, 'F', 15},    // Enchanter
    {26, HUMAN, 'F', 11},            // Ninja Master
    {27, HUMAN, 'R', 13},            // Archmage
    {28, HUMAN, 'R', 17},            // Diana
    {29, HUMAN | DRAGON, 'F', 17},   // Dragon Master
    {30, HUMAN, 'F', 17},            // Freya
    {31, HUMAN, 'R', 17},            // Siren
    {32, HUMAN | HOLY, 'S', 15},     // Priest
    {34, HUMAN, 'F', 18},            // Centurion
    {35, HUMAN, 'F', 18},            // Centurion
    {36, HOLY, 'F', 24},             // Angel Knight
    {37, HOLY, 'F', 30},             // Seraph
    {38, UNDEAD | DARK, 'R', 30},    // Lich
    {39, HAWK, 'F', 4},              // Hawkman
    {40, HAWK, 'F', 11},             // Vultan
    {41, HAWK, 'F', 6},              // Raven
    {43, UNDEAD | DARK, 'F', 22},    // Vampire
    {45, UNDEAD, 'F', 22},           // Zombie
    {46, UNDEAD, 'F', 22},           // Zombie
    {47, UNDEAD, 'F', 9},            // Skeleton
    {48, UNDEAD, 'R', 9},            // Ghost
    {49, DARK, 'R', 30},             // Gorgon
    {50, DARK | RAIDER, 'F', 8},     // Pumpkinhead
    {51, FAERIE, 'S', 4},            // Faerie
    {52, FAERIE | DARK, 'R', 3},     // Gremlin
    {53, RAIDER, 'F', 8},            // Goblin
    {54, RAIDER, 'F', 20},           // Saturos
    {55, RAIDER | BEAST, 'F', 8},    // Ogre
    {56, DRAGON, 'F', 2},            // Young Dragon
    {57, DRAGON, 'F', 6},            // Thunder Dragon
    {58, DRAGON, 'F', 4},            // Red Dragon
    {59, DRAGON, 'F', 3},            // Earth Dragon
    {60, DRAGON, 'F', 4},            // Blue Dragon
    {61, DRAGON, 'F', 6},            // Platinum Dragon
    {62, DRAGON, 'F', 6},            // Black Dragon
    {63, DRAGON, 'F', 21},           // Quetzalcoatl
    {64, DRAGON, 'F', 19},           // Flarebrass
    {65, DRAGON, 'F', 23},           // Ahzi Dahaka
    {66, DRAGON, 'F', 23},           // Hydra
    {67, DRAGON, 'F', 18},           // Bahamut
    {68, DRAGON, 'F', 17},           // Tiamat
    {69, BEAST | DRAGON, 'F', 5},    // Wyrm
    {70, BEAST | DRAGON, 'F', 15},   // Wyvern
    {71, BEAST, 'F', 1},             // Griffin
    {72, BEAST, 'F', 20},            // Opinincus
    {73, BEAST, 'R', 17},            // Cockatrice (breath from the back)
    {74, BEAST, 'R', 25},            // Sphinx (spells from the middle and back)
    {75, BEAST | DARK, 'F', 1},      // Hellhound
    {76, BEAST | DARK, 'F', 17},     // Cerberus
    {78, GOLEM, 'F', 3},             // Golem
    {79, GOLEM, 'F', 15},            // Stone Golem
    {80, GOLEM, 'F', 17},            // Baldr Golem
    {114, LODIS, 'F', 11},           // Grappler
    {115, LODIS | HOLY, 'F', 16},    // Knight Templar
    {116, LODIS | DARK, 'F', 38},    // Daemon
};

const ClassRole* class_role(int cls) {
    for (const ClassRole& r : CLASS_ROLES) {
        if (r.id == cls) {
            return &r;
        }
    }
    return nullptr;
}

bool flies(const uint8_t* rdram, int cls) {
    return cls > 0 && cls < CLASS_COUNT && rd8(rdram, class_record(cls) + 0x2C) == 1;
}

int class_tier(const uint8_t* rdram, int cls) {
    return rd8(rdram, class_record(cls) + 0x06);
}

bool air_unit(const uint8_t* rdram, const Template& t) {
    if (!flies(rdram, t.leader)) {
        return false;
    }
    for (const auto& g : t.groups) {
        if (g.count > 0 && !flies(rdram, g.cls)) {
            return false;
        }
    }
    return true;
}

// The places one member takes: 1, or for a large class the unit_size filter's
// answer (class_rebalance.hpp), 2 without a rule. The Another World mod pack
// makes it 1 for the enemy as for the player, so a leader with four dragons
// fits.
int place_cost(const uint8_t* rdram, int cls) {
    return is_large(rdram, cls) ? class_rebalance::unit_places(2) : 1;
}

int places(const uint8_t* rdram, int cls, int count) {
    return count * place_cost(rdram, cls);
}

int refit_template(uint8_t* rdram, uint32_t address, int base, int mission, Rng& rng) {
    Template t = read_template(rdram, address);
    const ClassRole* lead = class_role(t.leader);
    if (lead == nullptr) {
        return 0;
    }
    const bool air = air_unit(rdram, t);
    int refits = 0;
    for (int g = 0; g < 2; g++) {
        t = read_template(rdram, address);
        const Template::Group& grp = t.groups[g];
        const ClassRole* own = class_role(grp.cls);
        if (grp.count == 0 || own == nullptr) {
            continue;
        }
        bool has[128] = {};
        has[static_cast<int>(lead->role)] = true;
        int members = 0;
        for (int k = 0; k < 2; k++) {
            if (k != g && t.groups[k].count > 0) {
                if (const ClassRole* r = class_role(t.groups[k].cls)) {
                    has[static_cast<int>(r->role)] = true;
                }
            }
            members += t.groups[k].count;
        }
        // Support only takes the place of a second fighter group, never of the
        // unit's only damage.
        char want = 0;
        if (!has['R'] && own->role != 'R') {
            want = 'R';
        }
        else if (!has['S'] && own->role == 'F' && has['F'] && members >= 4) {
            want = 'S';
        }
        else if (class_tier(rdram, grp.cls) == 0) {
            want = own->role;
        }
        if (want == 0 || static_cast<int>(rng.next() % 100) >= promote_share(mission)) {
            continue;
        }
        // Places the rest of the unit takes, and what this group may use.
        const Template::Group& other = t.groups[1 - g];
        const int rest = place_cost(rdram, t.leader) + (other.count ? places(rdram, other.cls, other.count) : 0);
        // A replacement is worth at least what it replaces (class_value at the
        // group's level, all its members; 80% when it fills a missing role),
        // and a stronger one weighs more.
        const int level = base + grp.level;
        const double current = class_value(rdram, grp.cls, level) * grp.count;
        const double floor = want == own->role ? 1.0 : 0.8;
        std::map<int, int> weights;
        std::map<int, int> counts;
        for (const ClassRole& r : CLASS_ROLES) {
            if (r.role != want || r.id == grp.cls || r.id == t.leader || r.debut > mission) {
                continue;
            }
            if (air ? !flies(rdram, r.id) : (r.family & (lead->family | own->family)) == 0) {
                continue;
            }
            const int count = place_cost(rdram, r.id) == 2 ? std::max(1, grp.count / 2) : grp.count;
            if (rest + places(rdram, r.id, count) > 5) {
                continue;
            }
            if (is_elite(rdram, r.id, level) && !is_elite(rdram, grp.cls, level) &&
                elite_members(rdram, t, base) + count > elite_cap(mission)) {
                continue;
            }
            const double ratio = class_value(rdram, r.id, level) * count / std::max(1.0, current);
            if (ratio < floor) {
                continue;
            }
            weights[r.id] = std::max(1, static_cast<int>(ratio * 10.0));
            counts[r.id] = count;
        }
        const int pick = rng.pick(weights, -1);
        if (pick == 0) {
            continue;
        }
        const uint32_t base = address + (g == 0 ? 0x07u : 0x10u);
        wr8(rdram, base, static_cast<uint8_t>(pick));
        for (uint32_t i = 0; i < 4; i++) {
            wr8(rdram, base + 2 + i, 0);
        }
        // Fewer members when two small become one large: drop the last squares.
        int kept = 0;
        for (uint32_t i = 0; i < 3; i++) {
            if (rd8(rdram, base + 6 + i) != 0 && kept++ >= counts[pick]) {
                wr8(rdram, base + 6 + i, 0);
            }
        }
        std::fprintf(stderr, "[enemy] refit: %s x%d -> %s x%d (%c%s)\n", class_name(rdram, grp.cls).c_str(), grp.count,
                     class_name(rdram, pick).c_str(), counts[pick], want, air ? ", air unit" : "");
        refits++;
    }
    return refits;
}

bool mix_template(uint8_t* rdram, uint32_t address, const std::map<int, int>& members, Rng& rng) {
    const Template t = read_template(rdram, address);
    const Template::Group& a = t.groups[0];
    if (a.count < 3 || t.groups[1].count > 0 || a.cls <= 0 || a.cls >= CLASS_COUNT) {
        return false;
    }
    const int moving = a.count / 2;
    const int places_left = 5 - place_cost(rdram, t.leader) - (a.count - moving) * place_cost(rdram, a.cls);
    std::map<int, int> weights;
    for (const auto& [cls, n] : members) {
        if (cls == a.cls || cls == t.leader || cls <= 1 || story_class(cls)) {
            continue;
        }
        if (moving * place_cost(rdram, cls) > places_left) {
            continue;
        }
        // The elite cap (the half that moves becomes this class).
        const int level = rd8(rdram, BASE_LEVEL) + a.level;
        if (is_elite(rdram, cls, level) &&
            elite_members(rdram, t, rd8(rdram, BASE_LEVEL)) + moving > elite_cap(rd8(rdram, MISSION_INDEX))) {
            continue;
        }
        weights[cls] = n * (best_row(rdram, cls) != best_row(rdram, a.cls) ? 3 : 1);
    }
    const int cls = rng.pick(weights, -1);
    if (cls == 0) {
        return false;
    }
    const uint32_t ga = address + 0x07u;
    const uint32_t gb = address + 0x10u;
    wr8(rdram, gb, static_cast<uint8_t>(cls));
    wr8(rdram, gb + 1, rd8(rdram, ga + 1));
    for (uint32_t i = 0; i < 4; i++) {
        wr8(rdram, gb + 2 + i, 0);
    }
    // Move the last `moving` of group A's squares to group B.
    int moved = 0;
    for (int i = 2; i >= 0 && moved < moving; i--) {
        const uint8_t square = rd8(rdram, ga + 6 + static_cast<uint32_t>(i));
        if (square != 0) {
            wr8(rdram, gb + 6 + static_cast<uint32_t>(moved), square);
            wr8(rdram, ga + 6 + static_cast<uint32_t>(i), 0);
            moved++;
        }
    }
    for (int i = moved; i < 3; i++) {
        wr8(rdram, gb + 6 + static_cast<uint32_t>(i), 0);
    }
    // Keep group A's squares packed from the first byte.
    uint8_t packed[3] = {};
    int n = 0;
    for (uint32_t i = 0; i < 3; i++) {
        const uint8_t square = rd8(rdram, ga + 6 + i);
        if (square != 0) {
            packed[n++] = square;
        }
    }
    for (uint32_t i = 0; i < 3; i++) {
        wr8(rdram, ga + 6 + i, packed[i]);
    }
    return true;
}

// The leader's square once the members are seated: the back row first, then
// the middle, in the column with the most members in front of it (a member in
// the same column, a row or two forward); the centre wins ties. The battle
// ends when a unit's leader falls, so an exposed leader made a unit (bosses
// above all) one target deep (developer, 2026-10-30).
// Among the covered squares (a member ahead in the same column) the one where
// the leader's class has the most attacks wins, then the one with more cover;
// a tie keeps the back row (a Knight Templar slashes twice from the middle and
// casts twice from the back, as does a Black Knight). With no covered
// square, the square with the most cover. The developer confirmed in play that
// a member ahead protects the leader from melee (2026-10-30).
int guarded_square(const uint8_t* rdram, int cls, const bool used[10]) {
    static const int candidates[] = {2, 1, 3, 5, 4, 6};  // back row, then middle; centre first
    int best = 0;
    int best_score = -1;
    for (int s : candidates) {
        if (used[s]) {
            continue;
        }
        int cover = 0;
        for (int ahead = s + 3; ahead <= 9; ahead += 3) {
            cover += used[ahead] ? 1 : 0;
        }
        const int row = s <= 3 ? 0 : 1;
        const int score = cover > 0 ? 100 + row_attacks(rdram, cls, row) * 10 + cover : cover;
        if (score > best_score) {
            best = s;
            best_score = score;
        }
    }
    return best;
}

bool form_template(uint8_t* rdram, uint32_t address, int mission, bool boss, Rng& rng) {
    const Template t = read_template(rdram, address);
    std::vector<Piece> pieces;
    Piece leader;
    leader.cls = t.leader;
    leader.leader = true;
    pieces.push_back(leader);
    for (int g = 0; g < 2; g++) {
        for (int i = 0; i < t.groups[g].count; i++) {
            Piece p;
            p.cls = t.groups[g].cls;
            p.group = g;
            pieces.push_back(p);
        }
    }
    for (Piece& p : pieces) {
        if (p.cls <= 0 || p.cls >= CLASS_COUNT) {
            return false;
        }
        p.large = is_large(rdram, p.cls);
    }
    const bool lead_from_back = pieces.size() >= 3;
    const bool guard_leader = boss || pieces.size() >= 3;
    for (Piece& p : pieces) {
        rank_rows(rdram, p, lead_from_back, mission, rng);
    }
    // A unit with no front-row choice puts its sturdiest member there.
    const bool any_front = std::any_of(pieces.begin(), pieces.end(), [](const Piece& p) { return p.rows[0] == 2; });
    if (!any_front && pieces.size() >= 3) {
        Piece* sturdiest = nullptr;
        for (Piece& p : pieces) {
            if (!p.leader && (sturdiest == nullptr || class_stat(rdram, p.cls, 2) > class_stat(rdram, sturdiest->cls, 2))) {
                sturdiest = &p;
            }
        }
        if (sturdiest != nullptr) {
            std::rotate(sturdiest->rows, std::find(sturdiest->rows, sturdiest->rows + 3, 2), sturdiest->rows + 3);
        }
    }
    // Seat in order of how much the row matters; the leader wins ties.
    std::vector<Piece*> order;
    for (Piece& p : pieces) {
        if (!(p.leader && guard_leader)) {
            order.push_back(&p);
        }
    }
    std::stable_sort(order.begin(), order.end(), [](const Piece* a, const Piece* b) {
        if (a->large != b->large) {
            return a->large;
        }
        return a->gap != b->gap ? a->gap > b->gap : a->leader > b->leader;
    });
    static const int squares[3][3] = {{2, 1, 3}, {5, 4, 6}, {8, 7, 9}};  // back, middle, front
    bool used[10] = {};
    for (Piece* p : order) {
        for (int k = 0; k < 3 && p->square == 0; k++) {
            for (int s : squares[p->rows[k]]) {
                if (!used[s]) {
                    p->square = s;
                    used[s] = true;
                    break;
                }
            }
        }
        if (p->square == 0) {
            return false;
        }
    }
    if (guard_leader) {
        pieces[0].square = guarded_square(rdram, pieces[0].cls, used);
        if (pieces[0].square == 0) {
            return false;
        }
        used[pieces[0].square] = true;
    }
    wr8(rdram, address + 0x06, static_cast<uint8_t>(pieces[0].square));
    for (int g = 0; g < 2; g++) {
        if (t.groups[g].count > 0) {
            for (uint32_t i = 0; i < 3; i++) {
                wr8(rdram, address + (g == 0 ? 0x07u : 0x10u) + 6 + i, 0);
            }
        }
    }
    int next[2] = {};
    for (const Piece& p : pieces) {
        if (p.group >= 0) {
            const uint32_t base = address + (p.group == 0 ? 0x07u : 0x10u);
            wr8(rdram, base + 6 + static_cast<uint32_t>(next[p.group]++), static_cast<uint8_t>(p.square));
        }
    }
    return true;
}

}  // namespace

int class_role_index(int cls) {
    const ClassRole* r = class_role(cls);
    return r == nullptr ? -1 : r->role == 'F' ? 0 : r->role == 'R' ? 1 : 2;
}

bool elite_class(const uint8_t* rdram, int cls, int level) {
    return is_elite(rdram, cls, level);
}

int difficulty_level() {
    return tactics_level();
}

int boss_additions(int mission) {
    const int level = tactics_level();
    Rng rng{(g_war_seed ^ static_cast<uint32_t>(mission * 8191 + 0xB055)) | 1u};
    rng.next();
    if (level >= 2) {
        return 1 + static_cast<int>(rng.next() % 4u);
    }
    if (level == 1 && mission > 10) {
        return 1 + static_cast<int>(rng.next() % 2u);
    }
    return 1;
}

int elite_cap_now(int mission, double hours) {
    int cap = elite_cap(mission);
    if (tactics_level() >= 3) {
        cap += (hours >= 2160.0 ? 2 : 0) + (hours >= 4320.0 ? 2 : 0);
    }
    return cap;
}

int themed_extra_class(const uint8_t* rdram, int escort, const int roles_present[3], int level, int mission,
                       uint32_t salt, int strength) {
    const ClassRole* own = class_role(escort);
    if (rdram == nullptr || own == nullptr) {
        return 0;
    }
    const bool air = flies(rdram, escort);
    std::map<int, int> weights;
    for (const ClassRole& r : CLASS_ROLES) {
        if (r.debut > mission || story_class(r.id)) {
            continue;
        }
        if (air ? !flies(rdram, r.id) : (r.family & own->family) == 0) {
            continue;
        }
        if ((strength < 2 && is_elite(rdram, r.id, level)) || (strength == 0 && class_tier(rdram, r.id) != 0)) {
            continue;
        }
        const int role = r.role == 'F' ? 0 : r.role == 'R' ? 1 : 2;
        int w = std::max(1, static_cast<int>(class_value(rdram, r.id, level) / 50.0));
        if (roles_present[role] == 0) {
            w *= role == 1 ? 3 : 2;  // a missing damage role first, then support
        }
        weights[r.id] = w;
    }
    Rng rng{(g_war_seed ^ static_cast<uint32_t>(mission * 2971 + 0x5EED) ^ (salt * 0x9E3779B1u)) | 1u};
    return rng.pick(weights, -1);
}

void garrison(uint32_t templates, uint32_t spawn_list) {
    uint8_t* rdram = ultramodern::get_rdram_base();
    // WAR DIRECTOR (GAMEPLAY tab); OGRE_DIRECTOR_GARRISON forces it. ENEMY
    // WEAPONS and ENEMY FORMATIONS work with or without it.
    const bool enabled = feature::enabled(OGRE_FEATURE(WarDirector), "OGRE_DIRECTOR_GARRISON");
    const bool weapons = feature::enabled(OGRE_FEATURE(EnemyWeapons), "OGRE_ENEMY_WEAPONS");
    const bool formations = feature::enabled(OGRE_FEATURE(EnemyFormations), "OGRE_ENEMY_FORMATIONS");
    const bool ranks = feature::enabled(OGRE_FEATURE(EnemyRanks), "OGRE_ENEMY_RANKS");
    if (rdram == nullptr || !(enabled || weapons || formations || ranks) || templates < 0x80000000u ||
        templates >= 0x80800000u || spawn_list < 0x80000000u || spawn_list >= 0x80800000u) {
        return;
    }

    // Restore the cached buffer to the ROM's bytes, or take the first copy.
    const uint32_t bytes = TEMPLATE_COUNT * TEMPLATE_SIZE;
    if (templates != g_template_buffer || g_template_original.size() != bytes) {
        g_template_buffer = templates;
        g_template_original.resize(bytes);
        for (uint32_t i = 0; i < bytes; i++) {
            g_template_original[i] = rd8(rdram, templates + i);
        }
    }
    else {
        for (uint32_t i = 0; i < bytes; i++) {
            wr8(rdram, templates + i, g_template_original[i]);
        }
    }

    const int mission = rd8(rdram, MISSION_INDEX);
    const int base = rd8(rdram, BASE_LEVEL);
    const int count = rd8(rdram, spawn_list);
    g_level_averages.clear();  // the class table is final by now (class_rebalance.cpp)

    // The mission's generic pool, and which templates are generic.
    int boss_id = 0;
    std::map<int, int> leaders;
    std::map<int, int> members;
    std::map<int, bool> generic;  // template id -> rewrite it
    for (int i = 0; i < count; i++) {
        const uint32_t entry = spawn_list + 1 + static_cast<uint32_t>(i) * SPAWN_ENTRY;
        const int id = (rd8(rdram, entry + 3) << 8) | rd8(rdram, entry + 4);
        if (id < 1 || id > TEMPLATE_COUNT) {
            continue;
        }
        const Template t = read_template(rdram, templates + static_cast<uint32_t>(id - 1) * TEMPLATE_SIZE);
        if (i == 0) {
            boss_id = id;
        }
        // A story unit with a generic class (Tenne Plains' talking Wizard) is
        // marked only by the story unit list (content_db::story_unit).
        const int slot = rd8(rdram, entry + 2) & 0x7F;
        if (i == 0 || is_story(t) || content::story_unit(mission, slot)) {
            generic[id] = false;  // a template the story uses is never rewritten
            continue;
        }
        if (generic.count(id) == 0) {
            generic[id] = true;
        }
        leaders[t.leader]++;
        for (const auto& g : t.groups) {
            if (g.count > 0) {
                members[g.cls]++;
            }
        }
    }

    int rewritten = 0;
    int armed = 0;
    int formed = 0;
    int mixed = 0;
    int promoted = 0;
    int refitted = 0;
    g_spawn_templates.clear();
    for (const auto& [id, rewrite] : generic) {
        if (!rewrite) {
            continue;
        }
        const uint32_t address = templates + static_cast<uint32_t>(id - 1) * TEMPLATE_SIZE;
        const Template old = read_template(rdram, address);
        if (enabled) {
            const double budget = template_cost(rdram, old, base);
            Rng rng{(g_war_seed ^ static_cast<uint32_t>(mission * 7919 + id * 104729)) | 1u};
            // Never more members than the stage allows, unless the mission's own unit had more.
            const int cap = std::max(member_cap(mission, 0.0), old.groups[0].count + old.groups[1].count);
            const Built built = rebuild(rdram, address, old, leaders, members, base, budget, cap, rng);
            if (built.leader != 0) {
                rewritten++;
                std::fprintf(stderr, "[director] garrison t%d: %s +%dx%s +%dx%s (%.0f -> %.0f)\n", id,
                             class_name(rdram, built.leader).c_str(), built.na,
                             built.a ? class_name(rdram, built.a).c_str() : "-", built.nb,
                             built.b ? class_name(rdram, built.b).c_str() : "-", budget, built.cost);
            }
        }
        // Separate streams, so switching one setting does not change the other's picks.
        if (ranks) {
            Rng rng{(g_war_seed ^ static_cast<uint32_t>(mission * 3359 + id * 51749 + 41)) | 1u};
            mixed += mix_template(rdram, address, members, rng) ? 1 : 0;
            promoted += promote_template(rdram, address, base, mission, false, rng);
            refitted += refit_template(rdram, address, base, mission, rng);
        }
        if (formations) {
            Rng rng{(g_war_seed ^ static_cast<uint32_t>(mission * 6271 + id * 92821 + 17)) | 1u};
            formed += form_template(rdram, address, mission, false, rng) ? 1 : 0;
        }
        if (weapons) {
            Rng rng{(g_war_seed ^ static_cast<uint32_t>(mission * 4513 + id * 70001 + 29)) | 1u};
            armed += arm_template(rdram, address, mission, rng);
        }
        if (weapons || formations || ranks) {
            const Template now = read_template(rdram, address);
            std::string text = class_name(rdram, now.leader) + "@" + std::to_string(now.square);
            for (int g = 0; g < 2; g++) {
                const uint32_t base_g = address + (g == 0 ? 0x07u : 0x10u);
                for (int i = 0; i < now.groups[g].count; i++) {
                    const int cls = now.groups[g].cls;
                    text += " " + class_name(rdram, cls) + (is_large(rdram, cls) ? "(L)@" : "@") +
                            std::to_string(rd8(rdram, base_g + 6 + static_cast<uint32_t>(i)));
                }
            }
            std::fprintf(stderr, "[enemy] t%d: %s; weapons %d/%d/%d\n", id, text.c_str(), rd16(rdram, address + 0x02),
                         rd16(rdram, address + 0x09), rd16(rdram, address + 0x12));
        }
        // Phase 3 builds its units from this mission's own generic templates.
        std::vector<uint8_t> bytes_of(TEMPLATE_SIZE);
        for (uint32_t i = 0; i < TEMPLATE_SIZE; i++) {
            bytes_of[i] = rd8(rdram, address + i);
        }
        g_spawn_templates.push_back(std::move(bytes_of));
    }
    // The boss unit: the boss keeps its class and gear; its generic escorts
    // may be promoted, and the unit is re-seated with the boss guarded. A
    // template that also serves another spawn entry as a generic unit was
    // handled above and is left alone here.
    if (boss_id != 0 && generic.count(boss_id) != 0 && !generic[boss_id]) {
        const uint32_t address = templates + static_cast<uint32_t>(boss_id - 1) * TEMPLATE_SIZE;
        if (ranks) {
            Rng rng{(g_war_seed ^ static_cast<uint32_t>(mission * 3359 + boss_id * 51749 + 43)) | 1u};
            promoted += promote_template(rdram, address, base, mission, true, rng);
        }
        if (formations) {
            Rng rng{(g_war_seed ^ static_cast<uint32_t>(mission * 6271 + boss_id * 92821 + 19)) | 1u};
            formed += form_template(rdram, address, mission, true, rng) ? 1 : 0;
        }
        const Template now = read_template(rdram, address);
        std::fprintf(stderr, "[enemy] boss t%d: %s@%d, groups %s x%d, %s x%d\n", boss_id,
                     class_name(rdram, now.leader).c_str(), now.square,
                     now.groups[0].count ? class_name(rdram, now.groups[0].cls).c_str() : "-", now.groups[0].count,
                     now.groups[1].count ? class_name(rdram, now.groups[1].cls).c_str() : "-", now.groups[1].count);
    }
    std::fprintf(stderr,
                 "[director] garrison: mission %d base %d, %d spawn entries, %d templates rebuilt, %d formed, "
                 "%d weapons, %d promoted, %d mixed, %d refit\n",
                 mission, base, count, rewritten, formed, armed, promoted, mixed, refitted);
}

bool spawn_pending() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_on_mission || relaxing() || g_spawn_templates.empty()) {
        return false;
    }
    for (const Plan& p : g_pending) {
        if (eligible(p, hours_on_mission(), g_wave_release)) {
            return true;
        }
    }
    return false;
}

int grudge_town() {
    std::lock_guard<std::mutex> lock(g_mutex);
    int best = -1;
    for (int i = 0; i < g_town_count; i++) {
        if (g_grudge[i] > 0.0 && (g_town_state[i] & 0x02u) && (best < 0 || g_grudge[i] > g_grudge[best])) {
            best = i;
        }
    }
    return best;
}

void reward_extras(int extras, int elites) {
    std::lock_guard<std::mutex> lock(g_mutex);
    uint8_t* rdram = ultramodern::get_rdram_base();
    if (rdram == nullptr || extras <= 0) {
        return;
    }
    Rng rng{(g_war_seed ^ static_cast<uint32_t>(g_frames * 2246822519u + extras * 97 + elites)) | 1u};
    for (int i = 0; i < extras; i++) {
        // An elite pays in equipment; a plain extra in a consumable or, half the
        // time from mission 15, equipment.
        const bool gear = i < elites || (g_mission >= 15 && rng.next() % 2 == 0);
        g_spoils.push_back(roll_item(rdram, gear, rng));
    }
    // The spoils alert (director_spawn.cpp) names what was won.
    g_goth_bonus += static_cast<uint32_t>((50.0 * extras + 200.0 * elites) * reward_scale());
}

void note_spawned(int slot, int kind, int target) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_director_units[slot] = {kind, target};
}

std::vector<std::pair<int, int>> director_units() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<std::pair<int, int>> out;
    for (const auto& [slot, unit] : g_director_units) {
        out.emplace_back(slot, unit.kind);
    }
    return out;
}

double mission_hours() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_on_mission ? hours_on_mission() : -1.0;
}

int pacing_phase() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return static_cast<int>(g_phase);
}

bool last_stand() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_last_stand;
}

bool boss_stand() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_last_stand && g_stand_boss;
}

void note_boss_threat() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_boss_threat) {
        g_boss_threat = true;
        std::fprintf(stderr, "[director] a player unit reached the boss\n");
    }
}

std::vector<uint16_t> take_spoils(uint32_t& goth) {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<uint16_t> out;
    out.swap(g_spoils);
    goth = g_goth_bonus;
    g_goth_bonus = 0;
    return out;
}

std::string item_label(const uint8_t* rdram, uint16_t word) {
    const uint32_t id = word & 0x7FFFu;
    const uint32_t name = (word & 0x8000u) != 0 ? rd32(rdram, 0x8018C42Cu + id * 0x20u)
                                                 : rd32(rdram, 0x8018E6ECu + id * 0x0Cu);
    std::string text;
    if (name >= 0x80000000u && name < 0x80800000u) {
        for (uint32_t i = 0; i < 24; i++) {
            const uint8_t c = rd8(rdram, name + i);
            if (c == 0) {
                break;
            }
            text += c == 0x81 ? ' ' : (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ';
        }
    }
    return text.empty() ? "item " + std::to_string(id) : text;
}

bool take_spawn(uint32_t address, std::string& what, int& kind) {
    uint8_t* rdram = ultramodern::get_rdram_base();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (rdram == nullptr || g_pending.empty() || g_spawn_templates.empty()) {
        return false;
    }
    auto it = std::find_if(g_pending.begin(), g_pending.end(),
                           [](const Plan& p) { return eligible(p, hours_on_mission(), g_wave_release); });
    if (it == g_pending.end()) {
        return false;
    }
    const Plan plan = *it;
    g_pending.erase(it);
    if (plan.classes.empty()) {
        return false;
    }
    // A template of this mission sets the levels, the leader's square and the
    // layout; the plan's classes replace its units.
    const std::vector<uint8_t>& source = g_spawn_templates[static_cast<size_t>(g_spawned) % g_spawn_templates.size()];
    for (uint32_t i = 0; i < TEMPLATE_SIZE; i++) {
        wr8(rdram, address + i, source[i]);
    }
    const Template old = read_template(rdram, address);
    std::map<int, int> leaders{{plan.classes[0], 1}};
    std::map<int, int> members;
    for (size_t i = 1; i < plan.classes.size(); i++) {
        members[plan.classes[i]]++;
    }
    Rng rng{(g_war_seed ^ static_cast<uint32_t>(g_mission * 7919 + g_spawned * 104729 + 17)) | 1u};
    // The plan already chose and paid for its members, so no budget check here: at
    // the template's level offsets they can cost more than the plan priced them.
    const Built built = rebuild(rdram, address, old, leaders, members, g_base_level,
                                1.0e9, static_cast<int>(plan.classes.size()) - 1, rng, true);
    if (built.leader == 0) {
        return false;
    }
    g_spawned++;
    what = describe(rdram, plan);
    kind = plan.kind;
    return true;
}

// --- Rewards ----------------------------------------------------------------
//
// RoR2 pays for a kill with coeff x the monster's cost x a reward multiplier.
// Here the same idea improves the battle drops:
//
// Drops: battle drop table O (RAM 0x801EDB38, 35 entries {u16 type, itemA,
// itemB, itemC}, read by func_ovlN_801AD6BC with rand() % 3) is global and
// never improves in vanilla. Every equipment entry (bit 15 set) is upgraded to
// an item of the same category whose price is up to original x multiplier,
// where multiplier = coeff x (enemy average cost / the average when the
// mission opened, 1..2). One of the three dearest candidates is picked, seeded
// by mission, entry and multiplier step. Unique items (+0x1A bit 0x80),
// 50000-goth spells and quest key items are never handed out or replaced.
//
// The table belongs to record 7's image and shares its window with other
// banks (docs/notes/drop-table.md). It is the content database's "rewards"
// table: the upgrade starts from the database's rewards with the code and
// JSON packs applied, and is published as a runtime pack, so the database is
// the table's only writer and writes it only while bank N is resident
// (content_db.cpp tick).
//
// OGRE_DIRECTOR_LOOT=0 turns the drop upgrade off.

namespace {

constexpr uint32_t ITEM_TABLE = ::ogre::addr::kItems;  // ROM 0x6232C, 0x20 per item
constexpr uint32_t ITEM_STRIDE = 0x20u;
constexpr int ITEM_COUNT = 278;
constexpr int SPELL_PRICE = 50000;

constexpr int DROP_ENTRIES = ::ogre::addr::kRewardCount;
constexpr int DROP_WORDS = DROP_ENTRIES * 3;
// The upgrade is the content database's runtime pack of this id.
constexpr const char* LOOT_PACK = "runtime/director-loot";

// Quest key items (Lich, Vampire, Princess and Seraph quests), never handed
// out or replaced.
constexpr int QUEST_ITEMS[] = {124, 202, 248, 271, 217, 270, 133, 209, 268, 262};

bool g_loot_have = false;
uint16_t g_loot_original[DROP_WORDS] = {};
uint16_t g_loot_written[DROP_WORDS] = {};
int g_loot_key = -1;  // mission * 1000 + multiplier step the table was built for
double g_loot_mult = 1.0;
int g_loot_upgraded = 0;

uint32_t item_record(int id) {
    return ITEM_TABLE + static_cast<uint32_t>(id) * ITEM_STRIDE;
}

int item_price(const uint8_t* rdram, int id) {
    return rd16(rdram, item_record(id) + 0x08);
}

bool item_eligible(const uint8_t* rdram, int id) {
    if (id < 1 || id >= ITEM_COUNT) {
        return false;
    }
    for (int quest : QUEST_ITEMS) {
        if (id == quest) {
            return false;
        }
    }
    const uint32_t record = item_record(id);
    const int price = item_price(rdram, id);
    return rd8(rdram, record + 0x04) != 0xFF && (rd8(rdram, record + 0x1A) & 0x80u) == 0 && price > 0 &&
           price < SPELL_PRICE;
}

// `used` counts the picks already made for this table. The upgrade used to
// take one of the three dearest items under the ceiling, so once the
// multiplier passed the mission's band every equipment entry of a category
// became the same few items (developer playtest 2026-10-30: every drop was
// Ninja Garb). It now picks from the top 60% of the price range up to the
// ceiling, preferring the item used least so far.
uint16_t upgrade(const uint8_t* rdram, uint16_t word, double mult, Rng& rng, std::map<int, int>& used) {
    const int id = word & 0x7FFF;
    if ((word & 0x8000u) == 0 || !item_eligible(rdram, id)) {
        return word;
    }
    const int category = rd8(rdram, item_record(id) + 0x04);
    const int price = item_price(rdram, id);
    // v2 6.1: never past the mission's price band, however long the mission runs.
    const double target = std::min(price * mult, static_cast<double>(std::max(price, band_max(g_mission))));
    std::vector<std::pair<int, int>> candidates;  // (price, id)
    for (int other = 1; other < ITEM_COUNT; other++) {
        if (other == id || rd8(rdram, item_record(other) + 0x04) != category || !item_eligible(rdram, other)) {
            continue;
        }
        const int p = item_price(rdram, other);
        if (p > price && p <= target && p >= price + 0.4 * (target - price)) {
            candidates.emplace_back(p, other);
        }
    }
    if (candidates.empty()) {
        return word;
    }
    int fewest = INT32_MAX;
    for (const auto& [p, other] : candidates) {
        fewest = std::min(fewest, used[other]);
    }
    std::vector<int> least;
    for (const auto& [p, other] : candidates) {
        if (used[other] == fewest) {
            least.push_back(other);
        }
    }
    const int pick = least[rng.next() % least.size()];
    used[pick]++;
    return static_cast<uint16_t>(0x8000u | pick);
}

void loot_tick(uint8_t* rdram) {
    // LOOT UPGRADES (GAMEPLAY tab); OGRE_DIRECTOR_LOOT forces it.
    const bool enabled = feature::enabled(OGRE_FEATURE(LootUpgrades), "OGRE_DIRECTOR_LOOT");
    if (!enabled) {
        if (g_loot_have) {
            content::Pack empty;
            empty.id = LOOT_PACK;
            content::set_runtime_pack(std::move(empty));
            g_loot_have = false;
        }
        return;
    }
    // The item prices come from the item table in RAM.
    if (!content::loaded() || !content::segment_b_resident(rdram)) {
        return;
    }
    // The drops before the upgrade: the content database's rewards with the
    // code and JSON packs and without this runtime pack, so a mod's drops are
    // what gets upgraded. The database writes the result into the table.
    uint16_t original[DROP_WORDS];
    for (int i = 0; i < DROP_WORDS; i++) {
        original[i] = content::base(content::Table::Rewards, static_cast<uint16_t>(i / 3)).u16(
            static_cast<uint16_t>(2 + (i % 3) * 2));
    }
    if (!g_loot_have || !std::equal(std::begin(original), std::end(original), std::begin(g_loot_original))) {
        std::copy(std::begin(original), std::end(original), std::begin(g_loot_original));
        g_loot_have = true;
        g_loot_key = -1;
    }

    const double strength =
        g_first_average > 0.0
            ? std::clamp(g_generic_cost_sum / std::max(1, g_generic_units) / g_first_average, 1.0, 2.0)
            : 1.0;
    const double mult = coeff() * strength;
    const int key = g_mission * 1000 + static_cast<int>(mult * 10.0);
    if (key != g_loot_key) {
        g_loot_key = key;
        g_loot_mult = mult;
        g_loot_upgraded = 0;
        std::map<int, int> used;
        for (int i = 0; i < DROP_WORDS; i++) {
            Rng rng{static_cast<uint32_t>(key * 31 + i * 2654435761u) | 1u};
            g_loot_written[i] = upgrade(rdram, g_loot_original[i], mult, rng, used);
            g_loot_upgraded += g_loot_written[i] != g_loot_original[i];
        }
        std::fprintf(stderr, "[director] drops x%.2f: %d of %d upgraded, %zu different items\n", mult,
                     g_loot_upgraded, DROP_WORDS, used.size());
        content::Pack pack;
        pack.id = LOOT_PACK;
        pack.source = LOOT_PACK;
        for (int i = 0; i < DROP_WORDS; i++) {
            if (g_loot_written[i] != g_loot_original[i]) {
                pack.set_field(content::Table::Rewards, static_cast<uint16_t>(i / 3), "drop_" + std::to_string(i % 3),
                               g_loot_written[i]);
            }
        }
        content::set_runtime_pack(std::move(pack));
    }
}

std::string loot_text() {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "drops x%.2f, %d upgraded, %d spoils waiting, %d dry", g_loot_mult,
                  g_loot_have ? g_loot_upgraded : 0, static_cast<int>(g_spoils.size()), g_dry);
    return buffer;
}

// v2 6.1 bands: the dearest equipment a mission's spoils may give, by the
// loader's mission index (provisional, until the shops' stock per chapter is
// read). Consumables: the cheaper ones early, Heal Pack and Angel Fruit from
// index 10, Revive Stone from 19 (ids of the 0xC-stride table 0x8018E6EC).
// Rewards follow the difficulty (developer, 2026-10-30: a harder fight must not
// pay nothing): EASY x0.85, NORMAL x1, HARD x1.25, VETERAN x1.5 on the loot
// band's price ceiling and on the director units' drop chance (x0.8 / x1 / x1.2
// / x1.4, at most 95%) and the Goth from extras.
double reward_scale() {
    static const double kBand[4] = {0.85, 1.0, 1.25, 1.5};
    return kBand[std::clamp(tactics_level(), 0, 3)];
}

int band_max(int mission) {
    return static_cast<int>((150 + 50 * std::max(0, mission)) * reward_scale());
}

constexpr uint32_t CONSUMABLE_TABLE = 0x8018E6ECu;

uint16_t roll_item(const uint8_t* rdram, bool equipment, Rng& rng) {
    if (!equipment) {
        std::vector<int> ids = {1, 2, 4};  // Heal Leaf, Heal Seed, Power Fruit
        if (g_mission >= 10) {
            ids.insert(ids.end(), {3, 5});  // Heal Pack, Angel Fruit
        }
        if (g_mission >= 19) {
            ids.push_back(6);  // Revive Stone
        }
        return static_cast<uint16_t>(ids[rng.next() % ids.size()]);
    }
    const int hi = band_max(g_mission);
    std::vector<int> ids;
    for (int id = 1; id < ITEM_COUNT; id++) {
        const int price = item_price(rdram, id);
        if (item_eligible(rdram, id) && price >= hi / 3 && price <= hi) {
            ids.push_back(id);
        }
    }
    if (ids.empty()) {
        return static_cast<uint16_t>(2);
    }
    return static_cast<uint16_t>(0x8000u | ids[rng.next() % ids.size()]);
}

// A unit the director deployed has left the field: roll its spoils. Large units
// drop 60% of the time, patrols 30%; after three without a drop the next one is
// certain. Large units give equipment half the time.
void spoils_tick(const uint8_t* rdram) {
    for (auto it = g_director_units.begin(); it != g_director_units.end();) {
        const uint32_t unit = UNIT_TABLE + static_cast<uint32_t>(it->first) * UNIT_STRIDE;
        bool live = (rd8(rdram, unit + 0x01) & 0x80u) != 0;
        bool members = false;
        for (uint32_t i = 0; i < 5 && live; i++) {
            members |= rd8(rdram, unit + 0x02 + i) != 0;
        }
        if (live && members) {
            ++it;
            continue;
        }
        const DirectorUnit gone = it->second;
        it = g_director_units.erase(it);
        Rng rng{(g_war_seed ^ static_cast<uint32_t>(g_frames * 2654435761u)) | 1u};
        // v2 6.3: a counterattack beaten while the town is still the player's
        // makes the town loyal; it sends a gift once per mission.
        if (gone.target >= 0 && gone.target < MAX_LOCATIONS && (g_town_state[gone.target] & 0x02u) &&
            !g_gift_given[gone.target]) {
            g_loyal[gone.target] = true;
            g_gift_given[gone.target] = true;
            g_spoils.push_back(roll_item(rdram, false, rng));
            alerts::post(alerts::Kind::Good, "The town you defended is grateful and sends a gift");
        }
        if (gone.kind == 2) {
            // Supply train: always pays, and the enemy loses part of its war chest.
            g_spoils.push_back(roll_item(rdram, false, rng));
            g_spoils.push_back(roll_item(rdram, false, rng));
            g_goth_bonus += 100u + 20u * static_cast<uint32_t>(std::max(0, g_mission));
            g_campaign_bank *= 0.7;
            g_patrol_bank *= 0.7;
            continue;
        }
        const bool campaign = gone.kind == 1;
        static const double kDrop[4] = {0.8, 1.0, 1.2, 1.4};
        const uint32_t chance = static_cast<uint32_t>(
            std::min(95.0, (campaign ? 60.0 : 30.0) * kDrop[std::clamp(tactics_level(), 0, 3)]));
        if (g_dry < 3 && rng.next() % 100 >= chance) {
            g_dry++;
            continue;
        }
        g_dry = 0;
        g_spoils.push_back(roll_item(rdram, campaign && rng.next() % 2 == 0, rng));
    }
}

}  // namespace

// --- Training ---------------------------------------------------------------
//
// Vanilla training is bought from the map menu (scene 0x05). Bank M's
// func_ovlM_8019AF0C takes the fee from the goth word 0x80196A8C, picks an
// enemydat template from the per-location table at 0x801A7B00 and stores its
// id at 0x80193690/0x80193691 (0x8019D2C0). At battle setup, bank AH's
// func_ovlAH_801D0AAC copies that template to sp+24, sets the level $s4 =
// min(99, the highest member level of the player's unit at 0x801976FC) and
// builds the enemy as unit 30. Scripted encounters use the same builder with
// the template id in the u16 at 0x80197B80.
//
// config/hooks.toml inserts two calls: ogre_training_mark before
// 0x8019D2C0 (a training fight was bought) and ogre_training_build at
// 0x801D0C7C, after $s4 is final. For a marked fight with no scripted id, the
// template is rebuilt from the bestiary (every class fought so far) within
// 110% of its own cost at the new level, with no items, and the level is the
// catch-up level:
//
//   unit  = average level of the training unit's members
//   army  = average level of the roster's ten highest characters
//   level = unit + clamp(round((army - unit) / 2), 0, 6)
//
// A unit at the army's level fights its own average level. A unit behind the
// army fights up to six levels above its average, so its members earn more EXP
// per fight until the gap closes. The fight pays EXP (through the shared EXP
// split).
//
// OGRE_TRAINING=0 keeps vanilla training.

namespace {

constexpr uint32_t PLAYER_UNIT_INDEX = 0x801976FCu;
constexpr uint32_t PLAYER_CHARS = ::ogre::addr::kCharacters;
constexpr uint32_t PLAYER_STRIDE = 0x38u;
constexpr uint32_t SCRIPTED_TEMPLATE = 0x80197B80u;
constexpr int TRAINING_SLOT = 30;
constexpr int ARMY_TOP = 10;
constexpr int CATCH_UP_MAX = 6;

bool g_training_marked = false;
uint32_t g_training_count = 0;

void save_bestiary() {
    if (g_bestiary_path.empty()) {
        return;
    }
    std::ofstream out(g_bestiary_path, std::ios::trunc);
    for (const auto& [cls, n] : g_fought_leaders) {
        out << "leader " << cls << "\n";
    }
    for (const auto& [cls, n] : g_fought_members) {
        out << "member " << cls << "\n";
    }
}

int player_level(const uint8_t* rdram, int ref) {
    return rd8(rdram, PLAYER_CHARS + static_cast<uint32_t>(ref) * PLAYER_STRIDE + 0x13);
}

int catch_up_level(const uint8_t* rdram, int vanilla) {
    const int index = rd8(rdram, PLAYER_UNIT_INDEX);
    const uint32_t unit = UNIT_TABLE + static_cast<uint32_t>(index) * UNIT_STRIDE;
    int sum = 0;
    int count = 0;
    for (uint32_t i = 0; i < 5; i++) {
        const int ref = rd8(rdram, unit + 0x02 + i);
        if (ref >= 1 && ref < SOLDIER_REF) {
            sum += player_level(rdram, ref);
            count++;
        }
    }
    std::vector<int> roster;
    for (int ref = 1; ref < SOLDIER_REF; ref++) {
        const uint32_t record = PLAYER_CHARS + static_cast<uint32_t>(ref) * PLAYER_STRIDE;
        const int level = rd8(rdram, record + 0x13);
        if (rd8(rdram, record + 0x11) != 0 && level >= 1 && level <= 99) {
            roster.push_back(level);
        }
    }
    if (count == 0 || roster.empty()) {
        return vanilla;
    }
    std::sort(roster.rbegin(), roster.rend());
    const size_t top = std::min<size_t>(ARMY_TOP, roster.size());
    double army = 0.0;
    for (size_t i = 0; i < top; i++) {
        army += roster[i];
    }
    army /= static_cast<double>(top);
    const double average = static_cast<double>(sum) / count;
    const int bonus = std::clamp(static_cast<int>(std::lround((army - average) / 2.0)), 0, CATCH_UP_MAX);
    return std::clamp(static_cast<int>(std::lround(average)) + bonus, 1, 99);
}

void record_fought(const std::vector<int>& classes) {
    bool added = false;
    for (size_t i = 0; i < classes.size(); i++) {
        const int cls = classes[i];
        if (cls <= 0 || story_class(cls)) {
            continue;
        }
        auto& pool = i == 0 ? g_fought_leaders : g_fought_members;
        if (pool.emplace(cls, 1).second) {
            added = true;
        }
    }
    if (added) {
        save_bestiary();
    }
}

}  // namespace

void init(const std::filesystem::path& pref_dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_seed_path = pref_dir / "war_seed.cfg";
    std::ifstream seed_in(g_seed_path);
    if (!(seed_in >> std::hex >> g_war_seed) || g_war_seed == 0) {
        roll_war_seed("first run");
    }
    g_momentum_path = pref_dir / "war_momentum.cfg";
    std::ifstream momentum_in(g_momentum_path);
    if (!(momentum_in >> g_momentum)) {
        g_momentum = 0.0;
    }
    g_momentum = std::clamp(g_momentum, 0.0, 1.0);
    std::fprintf(stderr, "[director] war seed %08X\n", g_war_seed);
    g_bestiary_path = pref_dir / "bestiary.cfg";
    std::ifstream in(g_bestiary_path);
    std::string kind;
    int cls = 0;
    while (in >> kind >> cls) {
        if (cls > 0 && !story_class(cls)) {
            (kind == "leader" ? g_fought_leaders : g_fought_members)[cls] = 1;
        }
    }
    std::fprintf(stderr, "[director] bestiary: %zu leader classes, %zu member classes from %s\n",
                 g_fought_leaders.size(), g_fought_members.size(), g_bestiary_path.string().c_str());
}

void training_mark() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_training_marked = true;
}

int training_build(uint32_t templ, int level) {
    uint8_t* rdram = ultramodern::get_rdram_base();
    // TRAINING CATCH-UP (GAMEPLAY tab); OGRE_TRAINING forces it.
    const bool enabled = feature::enabled(OGRE_FEATURE(TrainingCatchUp), "OGRE_TRAINING");
    std::lock_guard<std::mutex> lock(g_mutex);
    const bool marked = g_training_marked;
    g_training_marked = false;
    if (rdram == nullptr || !enabled || !marked || rd16(rdram, SCRIPTED_TEMPLATE) != 0 ||
        templ < 0x80000000u || templ >= 0x80800000u) {
        return level;
    }
    const int target = catch_up_level(rdram, level);
    const Template old = read_template(rdram, templ);
    if (is_story(old)) {
        std::fprintf(stderr, "[director] training: story template kept, level %d -> %d\n", level, target);
        return target;
    }
    const double budget = template_cost(rdram, old, target);
    g_unit_cost[TRAINING_SLOT] = budget;
    if (!g_fought_leaders.empty() && !g_fought_members.empty()) {
        Rng rng{(++g_training_count * 2654435761u) ^ static_cast<uint32_t>(target * 40503) | 1u};
        const Built built = rebuild(rdram, templ, old, g_fought_leaders, g_fought_members, target, budget, 4, rng);
        if (built.leader != 0) {
            g_unit_cost[TRAINING_SLOT] = built.cost;
            std::fprintf(stderr, "[director] training: %s +%dx%s +%dx%s (cost %.0f)\n",
                         class_name(rdram, built.leader).c_str(), built.na,
                         built.a ? class_name(rdram, built.a).c_str() : "-", built.nb,
                         built.b ? class_name(rdram, built.b).c_str() : "-", built.cost);
        }
    }
    for (uint32_t i = 0; i < 4; i++) {
        wr8(rdram, templ + 0x02 + i, 0);  // no carried items
    }
    std::fprintf(stderr, "[director] training: level %d -> %d\n", level, target);
    return target;
}

}  // namespace ogre::war_director

extern "C" void ogre_training_mark(uint8_t* /*generated_rdram*/) {
    ogre::war_director::training_mark();
}

extern "C" uint32_t ogre_training_build(uint8_t* /*generated_rdram*/, uint32_t templ, uint32_t level) {
    return static_cast<uint32_t>(ogre::war_director::training_build(templ, static_cast<int>(level)));
}

namespace {

ogre::events::Subscribe g_garrison{ogre::events::Event::MissionLoad, "war director garrison",
                                   [](const ogre::events::Args& e) { ogre::war_director::garrison(e.a0, e.a1); }};

}  // namespace

