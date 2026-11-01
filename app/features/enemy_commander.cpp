// The enemy commander: gives the war director's units jobs on the mission field
// (docs/anotherworld/design-war-director-v2.md, "The enemy commander").
//
// Story safety. It orders only the units the director deployed itself
// (war_director::director_units, supply trains excluded). The mission's own
// units (the boss, named characters, garrisons, units whose spawn entries carry
// script triggers) are never moved: some set off scenes where they stand, and
// which trigger values mean a scene is not decoded yet.
//
// Every decision interval (in-game hours) it reads the field and gives each of
// its units one job:
//
//   COUNTERATTACK  a town the player just took: the nearest units go back to it
//   DEFEND         an enemy town with a seen player unit near it and no enemy
//                  unit at it: the nearest free unit goes there
//   RALLY          a seen player unit near the boss's stronghold: free units fall
//                  back to it (NORMAL and up)
//   RAID           the player's headquarters with no player unit near it while
//                  the army is away: one unit goes for it (HARD and up)
//   STRIKE         the weakest seen player unit: several units converge on it
//   RECOVER        a unit below 35% HP goes to the nearest enemy town (NORMAL up)
//
// What it knows. On EASY and NORMAL a player unit counts only when one of its
// units or one of its towns is within sight of it (a fifth of the map's width);
// on HARD and VETERAN it knows every unit on the map.
//
// Orders go through the game's own move routine, func_ovlN_801CC5D0(object,
// x, y, z) (the overlay's move, field_select.cpp), on the field update (game
// thread). A unit chasing a moving target is re-aimed every 24 field updates.
// Each order is logged, and a unit whose destination (object +0x28/+0x30) no
// longer matches the order on the next decision is logged as "drifted", so a
// play session shows whether the game's own AI (object +0xBA/+0xBB) overrides
// the orders.
//
// DIFFICULTY (GAMEPLAY > WAR DIRECTOR) and the mission both scale it; see
// params() below and the design doc's table.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "game_addresses.hpp"
#include "alerts.hpp"
#include "feature_flags.hpp"
#include "game_events.hpp"
#include "recomp.h"
#include "war_director.hpp"

namespace {

constexpr uint32_t OBJECTS = ::ogre::addr::kMapObjects;
constexpr uint32_t UNIT_TABLE = ::ogre::addr::kUnits;
constexpr uint32_t UNIT_STRIDE = 0x19u;
constexpr uint32_t PLAYER_CHARS = ::ogre::addr::kCharacters;
constexpr uint32_t ENEMY_CHARS = ::ogre::addr::kEnemyCharacters;
constexpr uint32_t MISSION_INDEX = ::ogre::addr::kMissionIndex;
constexpr uint32_t FIELD_MODE = 0x801F0E00u;
constexpr uint32_t WORLD_RECT = 0x801F0DD8u;  // min x, min z, max x, max z (floats)
constexpr uint32_t LOCATION_COUNT = ::ogre::addr::kLocationCount;
constexpr uint32_t LOCATION_STATE = ::ogre::addr::kLocationState;
constexpr uint32_t LOCATION_STRIDE = 0x24u;
constexpr uint32_t MAP_DATA_TABLE = ::ogre::addr::kMapDataTable;
constexpr uint32_t MAP_ID = ::ogre::addr::kMapId;
constexpr uint32_t MOVE_TO = 0x801CC5D0u;
constexpr uint32_t END_PURSUIT = 0x801D45F8u;
constexpr int PLAYER_UNITS = 30;
constexpr int BOSS_SLOT = 30;
constexpr int SLOTS = 60;
constexpr int MAX_LOCATIONS = 64;
constexpr int REAIM_EVERY = 24;
// A player unit below this HP share is not chosen as a new strike target while
// another is in sight (decide). A strike already on it continues.
constexpr float MERCY_HP = 0.35f;

uint8_t rd8(const uint8_t* rdram, uint32_t a) {
    return rdram[(a - 0x80000000u) ^ 3u];
}

uint16_t rd16(const uint8_t* rdram, uint32_t a) {
    return static_cast<uint16_t>((rd8(rdram, a) << 8) | rd8(rdram, a + 1));
}

uint32_t rd32(const uint8_t* rdram, uint32_t a) {
    return (static_cast<uint32_t>(rd16(rdram, a)) << 16) | rd16(rdram, a + 2);
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

// --- Difficulty -------------------------------------------------------------

struct Params {
    int level;              // 0 EASY, 1 NORMAL, 2 HARD, 3 VETERAN
    double every_hours;     // decision interval
    bool knows_all;         // sees every player unit
    int strike_size;        // units that converge on one target
    bool defend;            // DEFEND and RALLY
    bool recover;           // RECOVER
    bool raid;              // RAID the headquarters
    bool warn;              // an alert when a strike, raid or counterattack starts
};

// DIFFICULTY sets the level; the mission moves it along the war: the
// interval shortens by a quarter over the first thirty missions, and a strike
// takes one unit up to mission 11, two up to 23, then the level's own size.
Params params(int setting_index, int mission) {
    static const double kEvery[4] = {12.0, 6.0, 3.0, 1.0};
    static const int kStrike[4] = {1, 2, 3, 3};
    Params p{};
    p.level = std::clamp(setting_index, 0, 3);  // EASY, NORMAL, HARD, VETERAN
    const double war = std::min(1.0, std::max(0, mission) / 30.0);
    p.every_hours = kEvery[p.level] * (1.0 - 0.25 * war);
    p.knows_all = p.level >= 2;
    p.strike_size = std::min(kStrike[p.level], 1 + mission / 12);
    p.defend = p.level >= 1;
    p.recover = p.level >= 1;
    p.raid = p.level >= 2;
    p.warn = p.level <= 2;
    return p;
}

// --- The field --------------------------------------------------------------

struct Unit {
    int slot = -1;
    float x = 0.0f, z = 0.0f;
    int hp = 0, hp_max = 0;
    float percent() const { return hp_max > 0 ? static_cast<float>(hp) / hp_max : 1.0f; }
};

struct Place {
    int index = -1;
    float x = 0.0f, z = 0.0f;
    uint16_t state = 0;  // 0x04 enemy-held, 0x02 player-held, 0x10 the player's headquarters
    std::string name;
};

uint32_t object_of(const uint8_t* rdram, int slot) {
    return rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
}

bool on_map(const uint8_t* rdram, int slot) {
    const uint32_t o = object_of(rdram, slot);
    return in_ram(o) && (rd32(rdram, o) & 0x11u) == 0x11u;
}

// An enemy unit on the map, the mission's own (0x11) or one spawned mid-mission
// (0x10, flag 0x01 cleared by the spawn).
bool enemy_on_map(const uint8_t* rdram, int slot) {
    const uint32_t o = object_of(rdram, slot);
    return in_ram(o) && (rd32(rdram, o) & 0x10u) != 0;
}

Unit read_unit(const uint8_t* rdram, int slot) {
    Unit u;
    u.slot = slot;
    const uint32_t o = object_of(rdram, slot);
    u.x = rdf(rdram, o + 0x08);
    u.z = rdf(rdram, o + 0x10);
    const uint32_t record = UNIT_TABLE + static_cast<uint32_t>(slot) * UNIT_STRIDE;
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
        Place p;
        p.index = i;
        p.state = rd16(rdram, LOCATION_STATE + static_cast<uint32_t>(i) * LOCATION_STRIDE);
        p.x = rdf(rdram, map_data + 0x1Cu + static_cast<uint32_t>(i) * 8u);
        p.z = rdf(rdram, map_data + 0x20u + static_cast<uint32_t>(i) * 8u);
        for (uint32_t k = 0; k < 20; k++) {
            const uint8_t c = rd8(rdram, LOCATION_STATE + static_cast<uint32_t>(i) * LOCATION_STRIDE + 8 + k);
            if (c == 0) {
                break;
            }
            p.name += (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ';
        }
        out.push_back(p);
    }
    return out;
}

float dist2(float ax, float az, float bx, float bz) {
    return (ax - bx) * (ax - bx) + (az - bz) * (az - bz);
}

// --- Jobs -------------------------------------------------------------------

enum class Job { None, Counterattack, Defend, Rally, Raid, Strike, Recover, Advance };

const char* job_name(Job j) {
    switch (j) {
        case Job::Counterattack: return "counterattack";
        case Job::Defend: return "defend";
        case Job::Rally: return "rally";
        case Job::Raid: return "raid";
        case Job::Strike: return "strike";
        case Job::Recover: return "recover";
        case Job::Advance: return "advance";
        default: return "none";
    }
}

struct Order {
    Job job = Job::None;
    int target_slot = -1;   // a player unit (STRIKE)
    int place = -1;         // a location (the other jobs)
    float x = 0.0f, z = 0.0f;
};

struct State {
    int mission = -1;
    double next_decision = 0.0;
    int reaim = 0;
    bool stand_warned = false;  // the last stand's alert, once per mission
    std::map<int, Order> orders;
    std::vector<uint16_t> last_state;  // location states at the last decision
};
State g;

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

void decide(uint8_t* rdram, recomp_context* ctx, const Params& p, int mission) {
    // My units: the director's own, on the map, not supply trains. A unit the
    // game (or the director) spawns mid-mission has object flag 0x01 cleared
    // (0x801B2488: and with -2 after the placement), so 0x10 alone marks it on
    // the map; the mission's starting units keep 0x01 set.
    std::vector<Unit> mine;
    std::string summary;
    for (const auto& [slot, kind] : ogre::war_director::director_units()) {
        const uint32_t o = object_of(rdram, slot);
        if (kind == 2 || slot <= BOSS_SLOT || slot >= SLOTS || !in_ram(o)) {
            continue;
        }
        const uint32_t flags = rd32(rdram, o);
        char text[160];
        std::snprintf(text, sizeof(text), " [%d flags %X ai %02X %02X at (%.0f,%.0f) to (%.0f,%.0f)]", slot, flags,
                      rd8(rdram, o + 0xBA), rd8(rdram, o + 0xBB), static_cast<double>(rdf(rdram, o + 0x08)),
                      static_cast<double>(rdf(rdram, o + 0x10)), static_cast<double>(rdf(rdram, o + 0x28)),
                      static_cast<double>(rdf(rdram, o + 0x30)));
        summary += text;
        if ((flags & 0x10u) != 0) {
            // Activation. Every unit that starts a mission on the map reads flags
            // ...11 (player units 0x139); a unit spawned mid-mission has 0x01
            // cleared by the spawn (0x801B2488) until its trigger fires. The
            // director's units copy another unit's trigger, which never fires
            // for them: they marched and even took Karya, but no battle started
            // with the player's units in it (developer, run-commander3). Setting
            // 0x01 makes them the same as the mission's own enemies.
            if ((flags & 0x01u) == 0) {
                const uint32_t now = flags | 0x01u;
                for (uint32_t i = 0; i < 4; i++) {
                    rdram[(o + i - 0x80000000u) ^ 3u] = static_cast<uint8_t>(now >> (24 - 8 * i));
                }
                std::fprintf(stderr, "[commander] slot %d activated (flags %X -> %X)\n", slot, flags, now);
            }
            mine.push_back(read_unit(rdram, slot));
        }
    }
    std::fprintf(stderr, "[commander] decision: %zu unit(s)%s\n", mine.size(), summary.c_str());
    // Drop orders of units that are gone; report units that drifted.
    for (auto it = g.orders.begin(); it != g.orders.end();) {
        const bool alive = std::any_of(mine.begin(), mine.end(), [&](const Unit& u) { return u.slot == it->first; });
        if (!alive) {
            it = g.orders.erase(it);
            continue;
        }
        const uint32_t o = object_of(rdram, it->first);
        const float dx = rdf(rdram, o + 0x28), dz = rdf(rdram, o + 0x30);
        if (it->second.job != Job::Strike && dist2(dx, dz, it->second.x, it->second.z) > 4.0f) {
            std::fprintf(stderr, "[commander] slot %d drifted from its %s order: heading (%.0f, %.0f), ordered (%.0f, %.0f)\n",
                         it->first, job_name(it->second.job), static_cast<double>(dx), static_cast<double>(dz),
                         static_cast<double>(it->second.x), static_cast<double>(it->second.z));
        }
        ++it;
    }
    const std::vector<Place> places = read_places(rdram);
    // Map scale: the spread of the mission's own towns. Object and location
    // positions share one small coordinate space (a unit at its spawn town
    // stands at that town's x/z, run-commander2); the camera's world rect
    // (0x801F0DD8) is a different scale and made the sight radius useless.
    float lo_x = 1e30f, hi_x = -1e30f, lo_z = 1e30f, hi_z = -1e30f;
    for (const Place& pl : places) {
        lo_x = std::min(lo_x, pl.x);
        hi_x = std::max(hi_x, pl.x);
        lo_z = std::min(lo_z, pl.z);
        hi_z = std::max(hi_z, pl.z);
    }
    const float width = places.empty() ? 20.0f : std::max(4.0f, std::max(hi_x - lo_x, hi_z - lo_z));
    const float sight2 = (0.33f * width) * (0.33f * width);
    const float near2 = (0.15f * width) * (0.15f * width);

    // A player unit near the boss sets off the last stand (war_director.cpp),
    // so a player who goes straight for the boss still meets one. The boss
    // sees its own surroundings at every difficulty.
    if (on_map(rdram, BOSS_SLOT)) {
        const uint32_t bo = object_of(rdram, BOSS_SLOT);
        const float bx = rdf(rdram, bo + 0x08), bz = rdf(rdram, bo + 0x10);
        for (int s = 0; s < PLAYER_UNITS; s++) {
            if (on_map(rdram, s)) {
                const uint32_t o = object_of(rdram, s);
                if (dist2(rdf(rdram, o + 0x08), rdf(rdram, o + 0x10), bx, bz) <= near2 * 2.0f) {
                    ogre::war_director::note_boss_threat();
                    break;
                }
            }
        }
    }
    if (mine.empty()) {
        return;
    }

    // Player units it knows about.
    std::vector<Unit> seen;
    for (int s = 0; s < PLAYER_UNITS; s++) {
        if (!on_map(rdram, s)) {
            continue;
        }
        const Unit u = read_unit(rdram, s);
        bool visible = p.knows_all;
        for (const Unit& m : mine) {
            visible = visible || dist2(u.x, u.z, m.x, m.z) <= sight2;
        }
        for (int e = BOSS_SLOT; e < SLOTS && !visible; e++) {
            if (enemy_on_map(rdram, e)) {
                const uint32_t o = object_of(rdram, e);
                visible = dist2(u.x, u.z, rdf(rdram, o + 0x08), rdf(rdram, o + 0x10)) <= sight2;
            }
        }
        for (const Place& pl : places) {
            visible = visible || ((pl.state & 0x04u) && dist2(u.x, u.z, pl.x, pl.z) <= sight2);
        }
        if (visible) {
            seen.push_back(u);
        }
    }

    std::map<int, Order> next;
    auto free_units = [&]() {
        std::vector<Unit> out;
        for (const Unit& m : mine) {
            if (next.count(m.slot) == 0) {
                out.push_back(m);
            }
        }
        return out;
    };
    auto nearest_free = [&](float x, float z, int n) {
        std::vector<Unit> f = free_units();
        std::sort(f.begin(), f.end(), [&](const Unit& a, const Unit& b) {
            return dist2(a.x, a.z, x, z) < dist2(b.x, b.z, x, z);
        });
        if (static_cast<int>(f.size()) > n) {
            f.resize(static_cast<size_t>(n));
        }
        return f;
    };
    auto assign = [&](const Unit& u, Job job, int place, int target, float x, float z) {
        next[u.slot] = Order{job, target, place, x, z};
    };

    // The director's pacing (war_director.cpp): during FADE and RELAX the units
    // pull back (no STRIKE or RAID, RECOVER from 60% HP, idle units regroup at
    // an enemy town); RELAX also skips COUNTERATTACK. A last stand sends every
    // free unit after the town the enemy most wants back.
    const int phase = ogre::war_director::pacing_phase();
    const bool calm = phase == 2 || phase == 3;
    const bool stand = ogre::war_director::last_stand();

    // RECOVER first: a hurt unit is not sent anywhere else.
    if (p.recover) {
        for (const Unit& m : mine) {
            if (m.percent() >= (calm ? 0.6f : 0.35f)) {
                continue;
            }
            const Place* best = nullptr;
            for (const Place& pl : places) {
                if ((pl.state & 0x04u) && (best == nullptr || dist2(m.x, m.z, pl.x, pl.z) < dist2(m.x, m.z, best->x, best->z))) {
                    best = &pl;
                }
            }
            if (best != nullptr) {
                assign(m, Job::Recover, best->index, -1, best->x, best->z);
            }
        }
    }

    // LAST STAND: every free unit counterattacks the grudge town, or rallies to
    // the boss when the stand was set off there.
    std::string alert;
    if (stand && ogre::war_director::boss_stand() && on_map(rdram, BOSS_SLOT)) {
        const uint32_t bo = object_of(rdram, BOSS_SLOT);
        const float bx = rdf(rdram, bo + 0x08), bz = rdf(rdram, bo + 0x10);
        for (const Unit& u : free_units()) {
            assign(u, Job::Rally, -1, -1, bx, bz);
        }
        if (!g.stand_warned) {
            g.stand_warned = true;
            alert = "The enemy rallies to defend its commander.";
        }
    }
    else if (stand) {
        const int town = ogre::war_director::grudge_town();
        for (const Place& pl : places) {
            if (pl.index != town || !(pl.state & 0x02u)) {
                continue;
            }
            for (const Unit& u : free_units()) {
                assign(u, Job::Counterattack, pl.index, -1, pl.x, pl.z);
            }
            if (!g.stand_warned) {
                g.stand_warned = true;
                alert = "The enemy's last stand is heading for " + pl.name + ".";
            }
        }
    }

    // COUNTERATTACK: towns lost since the last decision.
    for (const Place& pl : places) {
        if (phase == 3) {
            break;
        }
        const bool lost = static_cast<size_t>(pl.index) < g.last_state.size() &&
                          (g.last_state[static_cast<size_t>(pl.index)] & 0x04u) && (pl.state & 0x02u);
        if (!lost) {
            continue;
        }
        for (const Unit& u : nearest_free(pl.x, pl.z, p.strike_size)) {
            assign(u, Job::Counterattack, pl.index, -1, pl.x, pl.z);
        }
        alert = "The enemy moves to retake " + pl.name + ".";
    }

    // DEFEND: an enemy town with a seen player unit near it and nobody home.
    if (p.defend) {
        for (const Place& pl : places) {
            if (!(pl.state & 0x04u)) {
                continue;
            }
            const bool threatened = std::any_of(seen.begin(), seen.end(), [&](const Unit& u) {
                return dist2(u.x, u.z, pl.x, pl.z) <= near2 * 1.5f;
            });
            bool manned = false;
            for (int e = BOSS_SLOT; e < SLOTS && !manned; e++) {
                if (enemy_on_map(rdram, e)) {
                    const uint32_t o = object_of(rdram, e);
                    manned = dist2(rdf(rdram, o + 0x08), rdf(rdram, o + 0x10), pl.x, pl.z) <= near2 * 0.25f;
                }
            }
            if (threatened && !manned) {
                for (const Unit& u : nearest_free(pl.x, pl.z, 1)) {
                    assign(u, Job::Defend, pl.index, -1, pl.x, pl.z);
                }
            }
        }
        // RALLY: a seen player unit near the boss.
        if (on_map(rdram, BOSS_SLOT)) {
            const uint32_t bo = object_of(rdram, BOSS_SLOT);
            const float bx = rdf(rdram, bo + 0x08), bz = rdf(rdram, bo + 0x10);
            const bool pressed = std::any_of(seen.begin(), seen.end(), [&](const Unit& u) {
                return dist2(u.x, u.z, bx, bz) <= near2 * 2.0f;
            });
            if (pressed) {
                for (const Unit& u : nearest_free(bx, bz, p.level >= 3 ? 4 : 2)) {
                    assign(u, Job::Rally, -1, -1, bx, bz);
                }
            }
        }
    }

    // RAID: the player's headquarters while no player unit guards it.
    if (p.raid && !calm) {
        for (const Place& pl : places) {
            if (!(pl.state & 0x10u)) {
                continue;
            }
            bool guarded = false;
            for (int s = 0; s < PLAYER_UNITS && !guarded; s++) {
                if (on_map(rdram, s)) {
                    const uint32_t o = object_of(rdram, s);
                    guarded = dist2(rdf(rdram, o + 0x08), rdf(rdram, o + 0x10), pl.x, pl.z) <= near2;
                }
            }
            if (!guarded) {
                for (const Unit& u : nearest_free(pl.x, pl.z, 1)) {
                    assign(u, Job::Raid, pl.index, -1, pl.x, pl.z);
                    alert = alert.empty() ? "Enemy raiders are heading for your headquarters." : alert;
                }
            }
        }
    }

    // STRIKE: the rest converge on the player unit in their way, the seen unit
    // nearest the free units; HARD and VETERAN prefer one pushing on an enemy
    // town. A unit below MERCY_HP is not chosen while another is in sight; a
    // strike already under way keeps its target (developer, 2026-10-30:
    // Risk of Rain 2 and Left 4 Dead 2 do not pound the weakest into dust; it
    // used to pick the lowest HP share, and VETERAN the most isolated unit).
    if (!calm && !seen.empty() && !free_units().empty()) {
        const std::vector<Unit> free = free_units();
        auto closeness = [&](const Unit& u) {
            float d = 1e30f;
            for (const Unit& m : free) {
                d = std::min(d, dist2(u.x, u.z, m.x, m.z));
            }
            return -std::sqrt(d) / width;
        };
        const bool any_healthy = std::any_of(seen.begin(), seen.end(), [](const Unit& u) {
            return u.percent() >= MERCY_HP;
        });
        const Unit* target = nullptr;
        float best = 0.0f;
        for (const Unit& u : seen) {
            if (any_healthy && u.percent() < MERCY_HP) {
                continue;
            }
            float score = closeness(u);
            if (p.level >= 2) {
                for (const Place& pl : places) {
                    if ((pl.state & 0x04u) && dist2(u.x, u.z, pl.x, pl.z) <= near2 * 1.5f) {
                        score += 0.5f;
                        break;
                    }
                }
            }
            if (target == nullptr || score > best) {
                target = &u;
                best = score;
            }
        }
        if (target != nullptr) {
            for (const Unit& u : nearest_free(target->x, target->z, p.strike_size)) {
                assign(u, Job::Strike, -1, target->slot, target->x, target->z);
            }
        }
    }

    // ADVANCE: a unit with nothing else to do marches on the nearest town the
    // player holds, else the player's headquarters, so no unit stands idle at
    // its spawn town (the copied AI pairs ending 04 hold their place).
    // During FADE and RELAX the idle unit regroups at the nearest enemy town
    // (DEFEND) instead.
    for (const Unit& u : free_units()) {
        const uint16_t want = calm ? 0x04u : 0x12u;
        const Place* best = nullptr;
        for (const Place& pl : places) {
            if ((pl.state & want) && (best == nullptr || dist2(u.x, u.z, pl.x, pl.z) < dist2(u.x, u.z, best->x, best->z))) {
                best = &pl;
            }
        }
        if (best != nullptr) {
            assign(u, calm ? Job::Defend : Job::Advance, best->index, -1, best->x, best->z);
        }
    }

    // Issue the new or changed orders.
    for (const auto& [slot, order] : next) {
        const auto old = g.orders.find(slot);
        const bool same = old != g.orders.end() && old->second.job == order.job &&
                          old->second.place == order.place && old->second.target_slot == order.target_slot;
        if (!same) {
            move_to(rdram, ctx, slot, order.x, order.z);
            std::fprintf(stderr, "[commander] slot %d: %s -> %s (%.0f, %.0f)\n", slot, job_name(order.job),
                         order.place >= 0 && static_cast<size_t>(order.place) < places.size()
                             ? places[static_cast<size_t>(order.place)].name.c_str()
                             : (order.target_slot >= 0 ? "player unit" : "stronghold"),
                         static_cast<double>(order.x), static_cast<double>(order.z));
        }
    }
    g.orders = std::move(next);
    if (p.warn && !alert.empty()) {
        ogre::alerts::post(ogre::alerts::Kind::Threat, alert);
    }
    g.last_state.clear();
    for (const Place& pl : places) {
        g.last_state.push_back(pl.state);
    }
}

// A strike follows its target: re-aimed every REAIM_EVERY field updates.
void reaim(uint8_t* rdram, recomp_context* ctx) {
    for (auto& [slot, order] : g.orders) {
        if (order.job != Job::Strike || order.target_slot < 0) {
            continue;
        }
        // A strike follows its target however hurt it gets (developer,
        // 2026-10-30: some risk is fine; players have healing and Quit Gates).
        if (!on_map(rdram, order.target_slot)) {
            order.job = Job::None;
            continue;
        }
        const uint32_t o = object_of(rdram, order.target_slot);
        order.x = rdf(rdram, o + 0x08);
        order.z = rdf(rdram, o + 0x10);
        move_to(rdram, ctx, slot, order.x, order.z);
    }
}

void tick(uint8_t* rdram, recomp_context* ctx) {
    if (!ogre::feature::enabled(OGRE_FEATURE(WarDirector), "OGRE_DIRECTOR_SPAWN") || rd32(rdram, FIELD_MODE) != 0) {
        return;
    }
    const double hours = ogre::war_director::mission_hours();
    if (hours < 0.0) {
        return;
    }
    const int mission = rd8(rdram, MISSION_INDEX);
    if (g.mission != mission) {
        g = State{};
        g.mission = mission;
        g.next_decision = hours + 2.0;  // the field settles first
    }
    const Params p = params(ogre::choice_index(ogre::Choice::EnemyTactics), mission);
    if (hours >= g.next_decision) {
        g.next_decision = hours + p.every_hours;
        decide(rdram, ctx, p, mission);
    }
    if (++g.reaim % REAIM_EVERY == 0) {
        reaim(rdram, ctx);
    }
}

ogre::events::Subscribe g_tick{ogre::events::Event::FieldUpdate, "enemy commander",
                               [](const ogre::events::Args& e) {
                                   if (e.rdram != nullptr && e.ctx != nullptr) {
                                       tick(e.rdram, e.ctx);
                                   }
                               }};

}  // namespace

