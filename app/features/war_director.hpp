#pragma once

// War Director, Phase 1: the observer (docs/anotherworld/design-war-director.md).
//
// Reads the current mission from RAM and works out what the director would do,
// without spawning anything. The overlay's DEBUG tab shows the result.
//
//   enemy units        0x80197210 + u*0x19, u = 30..59; +0x01 & 0x80 enemy,
//                      +0x02..+0x06 members (leader first; 1..99 enemy
//                      character record, 100+ generic soldier)
//   enemy characters   0x80195580 + r*0x34; class +0x11, level +0x13
//   mission base level byte 0x801F1055 (template levels are offsets from it)
//   mission index      byte 0x8018F4A1 (the mission loader's scenario id)
//   active scene       u16 0x800E810E (0x03 = mission field)
//
// The card pool is the mission's generic units: unit slot 30 (the boss) and any
// unit with a story class (81+) are excluded, so the theme rule holds by
// construction. Card costs come from the live class table, so the class
// rebalance is priced in.
//
// Stand-ins until Phase 0's runtime research is done: minutes on the mission
// field replace in-game hours (RoR2 uses minutes too), and town income is 1.

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ogre::war_director {

// Called once per frame on the main thread.
void tick(uint8_t* rdram);

// The DIRECTOR section's rows: (left column, right column).
std::vector<std::pair<std::string, std::string>> report();

// Phase 2: rebuilds the mission's generic enemy templates before the loader
// builds the units. `templates` is enemydat.bin in RAM and `spawn_list` the
// mission's spawn list (count byte, then 18-byte entries). Called on the game
// thread from bank R's mission loader (config/hooks.toml).
void garrison(uint32_t templates, uint32_t spawn_list);

// Loads the bestiary (every class fought) from `pref_dir`/bestiary.cfg.
void init(const std::filesystem::path& pref_dir);

// Training (war_director.cpp, Training): bank M marks a bought training fight;
// bank AH's builder then passes the copied template and its level, and gets
// back the catch-up level after the template is rebuilt from the bestiary.
void training_mark();
int training_build(uint32_t templ, int level);

// Phase 3 (app/features/director_spawn.cpp). True while a bought unit waits to be
// deployed on the current mission.
bool spawn_pending();

// The player-held location the enemy most wants back (v2 5.3), or -1. Bought
// units start at the enemy-held location nearest it.
int grudge_town();

// v2 6.1 spoils. director_spawn.cpp reports each unit it deploys; when one of
// them leaves the field the director rolls its spoils, and the field update
// grants them through the game's item routine (take_spoils on the game thread).
// `target` is the liberated town a counterattack was sent after, or -1. The
// Goth won from supply trains comes back in `goth`.
void note_spawned(int slot, int kind, int target);

// A unit with members past five (boss_escorts.cpp) was beaten: one item roll
// per extra (equipment for each elite) and Goth, scaled by the difficulty.
void reward_extras(int extras, int elites);

// The units the director deployed on this mission: (slot, kind), kind 0 patrol,
// 1 large unit, 2 supply train. The enemy commander (enemy_commander.cpp)
// orders only these, so story units are never moved.
std::vector<std::pair<int, int>> director_units();

// In-game hours on the current mission, or -1 off a mission.
double mission_hours();

// The pacing phase: 0 BUILD-UP, 1 PEAK, 2 FADE, 3 RELAX. The enemy commander
// pulls its units back during FADE and RELAX.
int pacing_phase();

// True from the last stand's trigger until its wave's PEAK ends: the enemy
// commander sends every unit after grudge_town(), or to the boss when
// boss_stand() (the stand a player unit near the boss set off; its units
// deploy at the stronghold).
bool last_stand();
bool boss_stand();

// The enemy commander saw a player unit near the boss: triggers the last stand
// if this mission has not had one.
void note_boss_threat();
std::vector<uint16_t> take_spoils(uint32_t& goth);
std::string item_label(const uint8_t* rdram, uint16_t word);

// Writes the next waiting unit's 35-byte enemydat template at `address`, built
// from one of this mission's generic templates with the bought classes, and
// names it in `what`. False when nothing waits or the template cannot be built.
// `kind`: 0 patrol, 1 large unit, 2 supply train.
bool take_spawn(uint32_t address, std::string& what, int& kind);

// Boss escorts (app/features/boss_escorts.cpp): a class for one of the boss unit's
// extra members, themed on its escort class `escort` (flyers stay flyers,
// otherwise the escort's family), introduced by `mission`, filling the role the
// boss unit lacks first and weighted by class value at `level`. `salt` varies
// the pick per extra; the war seed varies it per war. 0 when nothing fits.
// `strength`: 0 weak (a base-tier class, never elite), 1 standard (never
// elite), 2 any (elite allowed).
int themed_extra_class(const uint8_t* rdram, int escort, const int roles_present[3], int level, int mission,
                       uint32_t salt, int strength = 2);

// Elite: a class value at least twice the average class at `level`.
bool elite_class(const uint8_t* rdram, int cls, int level);

// The DIFFICULTY level: 0 EASY, 1 NORMAL, 2 HARD, 3 VETERAN.
int difficulty_level();

// Elite members a unit may hold at this point (record members and battle
// extras together): the difficulty's cap, and on VETERAN two more after 90
// in-game days on the mission and two more after 180.
int elite_cap_now(int mission, double hours);

// The role (0 F, 1 R, 2 S) of a class in the refit table, or -1.
int class_role_index(int cls);

}  // namespace ogre::war_director

