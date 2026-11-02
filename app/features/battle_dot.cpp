// Damage over time (a pack's "dots", design-content-database.md section 7.5):
// an effect that a status or a hit starts on a fighter and that deals damage
// at its ticks for the rest of the fight, can grow, can lower the damage the
// fighter deals, and plays a battle effect on it; with the Lua events
// battle_dot and battle_round_end (lua_runtime.hpp).
//
// The battle bank computes the whole fight in one pass, func_ovlX_801EFACC,
// then plays it back from an event timeline (battle_form.cpp). Each fighter
// spends its attacks (+0x70, +0x6C counts those made) in time order and the
// pass ends at 0x801EFDE0. Round n ends when every fighter still in the fight
// has made n attacks or has none left (developer, 2026-11-02). config/hooks.toml
// calls in at:
//
//   0x801EFAF0  the pass starts: the state is cleared and the effects read
//   0x801EFD34  an action's actors are chosen: the list at sp+0xF0, up to
//               three fighters, goes to the strike routines (the first word is
//               the attacker, func_ovlX_801EC2D0's input[0])
//   0x801EFBF0  the loop top, after every action and before the "is the
//               battle over" test func_801CC46C: the actors of the action just
//               made take their "turn" ticks, then a round that has ended
//               gives its "round" ticks, so a tick that ends the fight ends it
//               the game's way
//   0x801EFDE0  the pass is over (through battle_form.cpp's compute-end hook):
//               the last round's ticks when it had begun
//   0x801E7DBC  func_ovlX_801E7DBC(target, &status) applies an ailment; the
//   0x801E7F30  exit gives the status it set (0 when refused), which starts
//               the effects that follow that status
//   0x801EAC14  the generic attack routine func_ovlX_801E9E9C after a
//               target's hit roll: $s2 hit, $s3 target, $s6 attacker, the
//               strike's hit color at sp+0x1C7 (the param of its damage
//               events, 0x801EA884) and the hit's time $s7 + *(sp+0xA8) (its
//               damage call at 0x801EAC48)
//   0x801ED01C  the combined routine func_ovlX_801EC2D0, the same: $s2 hit,
//               $s3 target, the attacker the action's first actor, the hit
//               color at sp+0x21F (func_8020B170(attack) at 0x801EC94C), the
//               time $s7 + *(sp+0x120) (0x801ED198)
//   0x801F4468  the one exit of func_ovlX_801F36CC, the game's damage amount:
//               $s2 the source, $v0 the amount, which damage_down lowers
//
// A tick: the damage is func_ovlX_801F44A0(source, target, 0, 0, 0), the
// game's own amount for the source's action (kind +0x78, attack +0x7C, set to
// the effect's attack for the call), times the effect's power and stacks; it
// is applied with func_ovlX_801E7508(target, 0, time, damage, 0, 0), which
// handles a defeat and the count of fighters left, and shown with event 12,
// the number popup (func_ovlJ_801DA010(12, time, fighter, 255, hi, lo,
// color), as the display routine func_ovlJ_801E0D54 schedules it at
// 0x801E0E74): its playback shows the number, plays the hit pose, tints the
// figure and lowers the shown HP (LordlyCaliber func_0021EBBC case 12). The
// strikes' event 31 only queues a number until an attack's impact shows it,
// so a tick's number never showed with it (played, 2026-11-02). After the
// popup the tick calls func_ovlJ_801E08C8(fighter, time + the length of the
// hit pose 17, 1, 0), which schedules the end of the hit reaction (event 13,
// or 0x2C for a sleeping or petrified fighter), as func_ovlJ_801E0D54 does
// after its own popup at 0x801E0FE4; without it a fighter whose last event was
// a tick's popup stayed in the hit state and the playback waited for it
// (played: a freeze when the fight ended on a round's ticks, 2026-11-02). The
// pose's length is func_ovlJ_801DF2CC(*(fighter) + 0x44, 17), as
// battle_form.cpp measures it. A tick that defeats a fighter ends the same
// way: the game's defeat routine func_ovlJ_801E0AEC belongs to the playback
// (it sets HP and calls the main engine), so the fall is not shown. The visual is event
// 0x36 (func_ovlJ_801D9FDC(0x36, time, fighter, kind, hi, lo)), whose playback
// calls func_002267A8(fighter, kind, hi << 8 | lo) (LordlyCaliber
// func_0021EBBC); the melee routine plays an attack's visual with it at
// 0x801E8144 (kind 14, the attack id), and event 0x27's poison effect is kind
// 8. The damage event's param is the strike's hit color: the playback tints
// the fighter's figure with it (COPY_TRIPLE in LordlyCaliber's
// func_0021EBBC case 12), so a tick flashes the color of the strike that
// started the effect (the "tint" setting fixes one), which the fight has
// loaded.
// A turn tick is at the fighter's cursor (+0x94), which moves on by
// TICK_FRAMES; a round tick is at the latest cursor, when the round's last
// action is done. A label (the effect's "label" or "label_image") is event
// 0x36 with battle_labels.hpp kLabelKind, which the port draws.
//
// OGRE_DOT_TRACE=1 logs a [dot] line per start, stack, tick, round end and
// fight end.

#include "battle_dot.hpp"

#include "battle_labels.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "content_db.hpp"
#include "game_addresses.hpp"
#include "lua_runtime.hpp"
#include "recomp.h"

namespace {

constexpr uint32_t BATTLE_STATE = 0x801CE8DCu;
constexpr uint32_t PARTICIPANTS = 0x1C4u;
constexpr uint32_t PARTICIPANT_STRIDE = 0xF8u;
constexpr int PARTICIPANT_COUNT = 20;
constexpr uint32_t P_HP = 0x20u;           // u16
constexpr uint32_t P_FLAGS = 0x40u;        // bit 0 out of the fight; ailments func_ovlX_801E7DBC
constexpr uint32_t P_STRIKES_USED = 0x6Cu;  // word
constexpr uint32_t P_STRIKES = 0x70u;       // word
constexpr uint32_t P_KIND = 0x78u;          // byte: the action's attack kind
constexpr uint32_t P_ATTACK = 0x7Cu;        // word: the action's attack id
constexpr uint32_t P_CURSOR = 0x94u;        // word: the fighter's time

constexpr uint32_t DAMAGE_AMOUNT = 0x801F44A0u;  // func_ovlX_801F44A0(source, target, secondary, supplied, flags)
constexpr uint32_t APPLY_DAMAGE = 0x801E7508u;   // func_ovlX_801E7508(target, 0, time, amount, 0, adjust)
constexpr uint32_t EVENT7 = 0x801DA010u;         // func_ovlJ_801DA010(event, time, fighter, a3, a4, a5, a6)
constexpr uint32_t HIT_RECOVER = 0x801E08C8u;    // func_ovlJ_801E08C8(fighter, time, flag, status)
constexpr uint32_t ANIMATION_LENGTH = 0x801DF2CCu;  // func_ovlJ_801DF2CC(animations, id, 0)
constexpr uint32_t HIT_POSE = 17u;
constexpr uint32_t EVENT6 = 0x801D9FDCu;         // func_ovlJ_801D9FDC(event, time, fighter, a3, a4, a5)
constexpr uint32_t EVENT_NUMBER = 12u;
constexpr uint32_t ALL_PARTS = 255u;  // the figure parts a number event poses and tints
constexpr uint32_t EVENT_EFFECT = 0x36u;
constexpr uint32_t TICK_FRAMES = 30u;
constexpr int MAX_DAMAGE = 9999;
constexpr int MAX_DAMAGE_DOWN = 90;

struct Active {
    size_t dot;       // index in g.dots
    uint32_t source;  // the fighter that started it (its formula)
    uint32_t attack;  // the attack whose formula a tick uses; 0 none
    int stacks;
    uint32_t tint;    // the hit color of the strike that started it
};

struct State {
    bool running = false;
    bool ticking = false;  // inside a tick's own damage call
    int round = 1;         // the round in progress
    std::vector<ogre::content::Dot> dots;
    std::array<std::vector<Active>, PARTICIPANT_COUNT> active;
    std::array<uint32_t, 3> actors{};
    uint32_t strike_tint = 0;      // the hit color of the strike in progress
    uint32_t strike_attacker = 0;  // its attacker: an action's list can hold three
    uint32_t ailment_target = 0;
    uint32_t ailment_status = 0;
    std::vector<int64_t> damage;  // per effect: the damage its ticks dealt this fight
    std::vector<int> ticks;
};

// The battle's compute pass runs on the game thread only.
State g;

uint32_t rd32(uint8_t* rdram, uint32_t address) {
    return static_cast<uint32_t>(MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

uint16_t rd16(uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>(MEM_HU(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

uint8_t rd8(uint8_t* rdram, uint32_t address) {
    return static_cast<uint8_t>(MEM_BU(0, static_cast<gpr>(static_cast<int32_t>(address))));
}

void wr32(uint8_t* rdram, uint32_t address, uint32_t value) {
    MEM_W(0, static_cast<gpr>(static_cast<int32_t>(address))) = static_cast<int32_t>(value);
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    MEM_B(0, static_cast<gpr>(static_cast<int32_t>(address))) = static_cast<int8_t>(value);
}

bool in_rdram(uint32_t address) {
    return address >= 0x80000000u && address < 0x80800000u;
}

uint32_t fighter(uint8_t* rdram, int index) {
    return rd32(rdram, BATTLE_STATE) + PARTICIPANTS + static_cast<uint32_t>(index) * PARTICIPANT_STRIDE;
}

// The participant index of a fighter address, or -1.
int index_of(uint8_t* rdram, uint32_t address) {
    const uint32_t state = rd32(rdram, BATTLE_STATE);
    if (!in_rdram(state) || address < state + PARTICIPANTS) {
        return -1;
    }
    const uint32_t offset = address - state - PARTICIPANTS;
    if (offset % PARTICIPANT_STRIDE != 0 || offset / PARTICIPANT_STRIDE >= PARTICIPANT_COUNT) {
        return -1;
    }
    return static_cast<int>(offset / PARTICIPANT_STRIDE);
}

bool out_of_fight(uint8_t* rdram, uint32_t address) {
    return (rd32(rdram, address + P_FLAGS) & 1u) != 0 || rd16(rdram, address + P_HP) == 0;
}

// The fighter flag of a status the game keeps (content_db.hpp valid_dot_status).
uint32_t status_flag(int status) {
    switch (status) {
    case 3: return 0x02u;
    case 4: return 0x04u;
    case 8: return 0x08u;
    case 10: return 0x10u;
    default: return 0;
    }
}

bool tracing() {
    static const bool on = [] {
        const char* value = std::getenv("OGRE_DOT_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return on;
}

bool roll(int percent) {
    static std::mt19937 random{std::random_device{}()};
    return percent >= 100 || std::uniform_int_distribution<int>(0, 99)(random) < percent;
}

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

// Calls a game function with its arguments, the fifth and later on the stack
// (sp+0x10..) of a frame below the caller's; every register comes back.
uint32_t call(uint8_t* rdram, recomp_context* ctx, uint32_t function, std::initializer_list<uint32_t> args) {
    const recomp_context saved = *ctx;
    ctx->r29 = ADD32(ctx->r29, -0x60);
    gpr* regs[4] = {&ctx->r4, &ctx->r5, &ctx->r6, &ctx->r7};
    size_t i = 0;
    for (const uint32_t arg : args) {
        if (i < 4) {
            set_arg(*regs[i], arg);
        } else {
            MEM_W(static_cast<int32_t>(0x10 + 4 * (i - 4)), ctx->r29) = static_cast<int32_t>(arg);
        }
        i++;
    }
    LOOKUP_FUNC(function)(rdram, ctx);
    const uint32_t result = static_cast<uint32_t>(ctx->r2);
    *ctx = saved;
    return result;
}

// Starts effect `dot` on `target`, or adds its stacks_on_hit to the one it
// has; true when it did either.
bool start(uint8_t* rdram, uint32_t target, size_t dot, uint32_t source, uint32_t attack) {
    const int index = index_of(rdram, target);
    if (index < 0 || dot >= g.dots.size() || out_of_fight(rdram, target)) {
        return false;
    }
    std::vector<Active>& list = g.active[static_cast<size_t>(index)];
    const auto same = std::find_if(list.begin(), list.end(), [&](const Active& a) { return a.dot == dot; });
    if (same == list.end()) {
        list.push_back({dot, source, attack, 1, g.strike_tint});
        if (tracing()) {
            std::fprintf(stderr, "[dot] p%d %s starts from p%d, formula attack %u\n", index, g.dots[dot].name.c_str(),
                         index_of(rdram, source), attack);
        }
        return true;
    }
    if (g.dots[dot].stacks_on_hit == 0) {
        return false;  // a running effect keeps its source and stacks
    }
    // A stack takes the latest source's formula.
    same->stacks = std::min(same->stacks + g.dots[dot].stacks_on_hit, g.dots[dot].max_stacks);
    same->source = source;
    same->attack = attack;
    same->tint = g.strike_tint;
    if (tracing()) {
        std::fprintf(stderr, "[dot] p%d %s stacks %d\n", index, g.dots[dot].name.c_str(), same->stacks);
    }
    return true;
}

// The attack whose formula `dot` uses when `attack` starts it: the attack
// itself when it deals damage (+0x07 effect 1 and a damage formula), else the
// effect's own "attack".
uint32_t formula_attack(size_t dot, uint32_t attack) {
    if (attack > 0 && attack < ogre::addr::kAttackCount) {
        const ogre::content::AttackRecord r = ogre::content::attack_record(static_cast<uint16_t>(attack));
        if (r.u8(0x07) == 1 && r.formula() != 8) {
            return attack;
        }
    }
    return static_cast<uint32_t>(g.dots[dot].attack);
}

// The effect's visual and label on `target` at `time`: at each tick, and when
// a hit starts or stacks an effect that follows no game status (a status has
// the game's own effect, as poison's event 0x27).
void show(uint8_t* rdram, recomp_context* ctx, const ogre::content::Dot& dot, uint32_t target, uint32_t time) {
    if (dot.visual != 0) {
        call(rdram, ctx, EVENT6,
             {EVENT_EFFECT, time, target, static_cast<uint32_t>(dot.visual),
              static_cast<uint32_t>(dot.visual_value) >> 8, static_cast<uint32_t>(dot.visual_value) & 0xFFu});
    }
    // The effect's own label, drawn by the port when the playback reaches it
    // (battle_labels.cpp), beside the game's visual.
    if (!dot.label.empty() || !dot.label_image.empty()) {
        const int label = ogre::battle_labels::add(dot.label, dot.label_color, dot.label_image);
        if (label >= 0) {
            call(rdram, ctx, EVENT6,
                 {EVENT_EFFECT, time, target, ogre::battle_labels::kLabelKind, static_cast<uint32_t>(label) >> 8,
                  static_cast<uint32_t>(label) & 0xFFu});
        }
    }
}

// The tick's damage before the Lua event.
int amount(uint8_t* rdram, recomp_context* ctx, const Active& a, uint32_t target) {
    int base = 1;
    if (a.attack > 0 && a.attack < ogre::addr::kAttackCount && in_rdram(a.source)) {
        const uint8_t kind = rd8(rdram, a.source + P_KIND);
        const uint32_t action = rd32(rdram, a.source + P_ATTACK);
        wr8(rdram, a.source + P_KIND, ogre::content::attack_record(static_cast<uint16_t>(a.attack)).kind());
        wr32(rdram, a.source + P_ATTACK, a.attack);
        g.ticking = true;
        base = static_cast<int>(call(rdram, ctx, DAMAGE_AMOUNT, {a.source, target, 0, 0, 0}));
        g.ticking = false;
        wr8(rdram, a.source + P_KIND, kind);
        wr32(rdram, a.source + P_ATTACK, action);
    }
    const int64_t damage = int64_t{std::max(base, 0)} * g.dots[a.dot].power / 100 * a.stacks;
    return static_cast<int>(std::clamp<int64_t>(damage, 1, MAX_DAMAGE));
}

// The ticks of `index`'s "round" or "turn" effects, at `time` (0: its cursor).
void tick(uint8_t* rdram, recomp_context* ctx, int index, bool round, uint32_t time) {
    const uint32_t target = fighter(rdram, index);
    std::vector<Active>& list = g.active[static_cast<size_t>(index)];
    for (size_t i = 0; i < list.size();) {
        Active& a = list[i];
        const ogre::content::Dot& dot = g.dots[a.dot];
        if (dot.status != 0 && (rd32(rdram, target + P_FLAGS) & status_flag(dot.status)) == 0) {
            if (tracing()) {
                std::fprintf(stderr, "[dot] p%d %s ends: the status is gone\n", index, dot.name.c_str());
            }
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));  // cured
            continue;
        }
        i++;
        if (dot.round != round || out_of_fight(rdram, target)) {
            continue;
        }
        const int64_t damage = ogre::lua::filter("battle_dot",
                                                 {index, index_of(rdram, a.source), a.attack, a.stacks},
                                                 amount(rdram, ctx, a, target), 0, MAX_DAMAGE);
        const int stacks = a.stacks;
        a.stacks = std::min(a.stacks + dot.stacks_per_tick, dot.max_stacks);
        if (damage <= 0) {
            continue;
        }
        const uint32_t at = time != 0 ? time : rd32(rdram, target + P_CURSOR);
        const uint32_t tint = dot.tint >= 0 ? static_cast<uint32_t>(dot.tint) : a.tint;
        show(rdram, ctx, dot, target, at);
        call(rdram, ctx, APPLY_DAMAGE, {target, 0, at, static_cast<uint32_t>(damage), 0, 0});
        call(rdram, ctx, EVENT7,
             {EVENT_NUMBER, at, target, ALL_PARTS, static_cast<uint32_t>(damage) >> 8,
              static_cast<uint32_t>(damage) & 0xFFu, tint});
        const uint32_t recover = at + call(rdram, ctx, ANIMATION_LENGTH, {rd32(rdram, target) + 0x44u, HIT_POSE, 0});
        call(rdram, ctx, HIT_RECOVER, {target, recover, 1, 0});
        g.damage[a.dot] += damage;
        g.ticks[a.dot]++;
        // The fighter's next action starts after the hit reaction.
        const uint32_t cursor = rd32(rdram, target + P_CURSOR);
        wr32(rdram, target + P_CURSOR, std::max(cursor, time == 0 ? std::max(at + TICK_FRAMES, recover) : recover));
        if (tracing()) {
            std::fprintf(stderr, "[dot] p%d %s %s tick: %dx, %lld damage at time %u, HP %u%s\n", index,
                         dot.name.c_str(), round ? "round" : "turn", stacks, static_cast<long long>(damage), at,
                         rd16(rdram, target + P_HP), out_of_fight(rdram, target) ? " (defeated)" : "");
        }
    }
}

// True when some fighter still in the fight has made `round` attacks.
bool round_begun(uint8_t* rdram, int round) {
    for (int i = 0; i < PARTICIPANT_COUNT; i++) {
        const uint32_t f = fighter(rdram, i);
        if (!out_of_fight(rdram, f) && rd32(rdram, f + P_STRIKES_USED) >= static_cast<uint32_t>(round)) {
            return true;
        }
    }
    return false;
}

// True when every fighter still in the fight has made `round` attacks or has
// none left.
bool round_over(uint8_t* rdram, int round) {
    for (int i = 0; i < PARTICIPANT_COUNT; i++) {
        const uint32_t f = fighter(rdram, i);
        const uint32_t used = rd32(rdram, f + P_STRIKES_USED);
        if (!out_of_fight(rdram, f) && used < static_cast<uint32_t>(round) && used < rd32(rdram, f + P_STRIKES)) {
            return false;
        }
    }
    return true;
}

// The round's ticks, at the latest time any fighter has reached, then
// battle_round_end. `last`: the fight is over, so the cursors move past the
// ticks to keep them inside the fight's length.
void end_round(uint8_t* rdram, recomp_context* ctx, bool last) {
    uint32_t end = 0;
    for (int i = 0; i < PARTICIPANT_COUNT; i++) {
        end = std::max(end, rd32(rdram, fighter(rdram, i) + P_CURSOR));
    }
    for (int i = 0; i < PARTICIPANT_COUNT; i++) {
        const std::vector<Active>& list = g.active[static_cast<size_t>(i)];
        const bool any = std::any_of(list.begin(), list.end(), [](const Active& a) { return g.dots[a.dot].round; });
        if (any && !out_of_fight(rdram, fighter(rdram, i))) {
            tick(rdram, ctx, i, true, end);
            if (last) {
                const uint32_t cursor = rd32(rdram, fighter(rdram, i) + P_CURSOR);
                wr32(rdram, fighter(rdram, i) + P_CURSOR, std::max(cursor, end + TICK_FRAMES));
            }
        }
    }
    if (tracing()) {
        std::fprintf(stderr, "[dot] round %d ends%s, time %u\n", g.round, last ? " with the fight" : "", end);
    }
    ogre::lua::event("battle_round_end", {g.round});
    g.round++;
}

}  // namespace

namespace ogre::battle_dot {

void compute_end(uint8_t* rdram, recomp_context* ctx) {
    if (g.running && round_begun(rdram, g.round)) {
        end_round(rdram, ctx, true);
    }
    if (g.running && tracing()) {
        std::fprintf(stderr, "[dot] the fight's computation ends after round %d\n", g.round - 1);
        for (size_t d = 0; d < g.dots.size(); d++) {
            std::fprintf(stderr, "[dot] fight total: %s %lld damage in %d ticks\n", g.dots[d].name.c_str(),
                         static_cast<long long>(g.damage[d]), g.ticks[d]);
        }
    }
    g = State{};
}

}  // namespace ogre::battle_dot

extern "C" void ogre_battle_dot_compute_start(uint8_t* rdram) {
    (void)rdram;
    g = State{};
    g.dots = ogre::content::dot_rules();
    ogre::battle_labels::new_fight();
    g.damage.assign(g.dots.size(), 0);
    g.ticks.assign(g.dots.size(), 0);
    g.running = true;
}

extern "C" void ogre_battle_dot_actors(uint8_t* rdram, uint32_t list) {
    g.strike_attacker = 0;
    for (size_t i = 0; i < g.actors.size(); i++) {
        g.actors[i] = rd32(rdram, list + 4u * static_cast<uint32_t>(i));
    }
}

extern "C" void ogre_battle_dot_turn_end(uint8_t* rdram, recomp_context* ctx) {
    if (!g.running) {
        return;
    }
    for (uint32_t& actor : g.actors) {
        const int index = actor != 0 ? index_of(rdram, actor) : -1;
        actor = 0;
        if (index >= 0) {
            tick(rdram, ctx, index, false, 0);
        }
    }
    while (round_begun(rdram, g.round) && round_over(rdram, g.round)) {
        end_round(rdram, ctx, false);
    }
}

extern "C" void ogre_battle_dot_ailment_begin(uint8_t* rdram, uint32_t target, uint32_t status) {
    (void)rdram;
    g.ailment_target = target;
    g.ailment_status = status;
}

extern "C" void ogre_battle_dot_ailment_end(uint8_t* rdram) {
    const uint32_t status_address = g.ailment_status;
    const uint32_t target = g.ailment_target;
    g.ailment_target = 0;
    g.ailment_status = 0;
    if (!g.running || !in_rdram(status_address) || !in_rdram(target)) {
        return;
    }
    const int status = rd8(rdram, status_address);
    // The strike's attacker from the hit hook, which runs before the strike
    // applies its ailments; an action's list can hold three attackers.
    const uint32_t source = g.strike_attacker != 0 ? g.strike_attacker : g.actors[0];
    if (status == 0 || !in_rdram(source)) {
        return;
    }
    const uint32_t attack = rd32(rdram, source + P_ATTACK);
    for (size_t d = 0; d < g.dots.size(); d++) {
        if (g.dots[d].status == status) {
            start(rdram, target, d, source, formula_attack(d, attack));
        }
    }
}

namespace ogre::battle_dot {

void status_landed(uint8_t* rdram, uint32_t attacker, uint32_t target, int status) {
    // A fighter that already has the status: the game's immunity check refuses
    // it again (func_ovlX_801E7DBC), so the running effect takes the stack here.
    // A first landing starts the effect at the ailment's exit instead.
    if (!g.running || (rd32(rdram, target + P_FLAGS) & status_flag(status)) == 0) {
        return;
    }
    attacker = attacker != 0 ? attacker : g.strike_attacker;
    const uint32_t attack = in_rdram(attacker) ? rd32(rdram, attacker + P_ATTACK) : 0;
    for (size_t d = 0; d < g.dots.size(); d++) {
        if (g.dots[d].status == status) {
            start(rdram, target, d, attacker, formula_attack(d, attack));
        }
    }
}

}  // namespace ogre::battle_dot

extern "C" void ogre_battle_dot_hit(uint8_t* rdram, recomp_context* ctx, uint32_t attacker, uint32_t target,
                                    uint32_t hit, uint32_t tint, uint32_t time) {
    // The strike's attacker and hit color, also for the ailments it applies next.
    attacker = attacker != 0 ? attacker : g.actors[0];
    g.strike_tint = tint & 0xFFu;
    g.strike_attacker = attacker;
    if (!g.running || hit == 0 || index_of(rdram, attacker) < 0) {
        return;
    }
    const uint32_t attack = rd32(rdram, attacker + P_ATTACK);
    if (attack >= ogre::addr::kAttackCount) {
        return;
    }
    const std::optional<ogre::content::AttackDot> on_hit = ogre::content::attack_dot(static_cast<uint16_t>(attack));
    if (!on_hit) {
        return;
    }
    for (size_t d = 0; d < g.dots.size(); d++) {
        if (g.dots[d].name == on_hit->dot && roll(on_hit->chance) &&
            start(rdram, target, d, attacker, formula_attack(d, attack)) && g.dots[d].status == 0) {
            show(rdram, ctx, g.dots[d], target, time);
        }
    }
}

extern "C" uint32_t ogre_battle_dot_amount(uint8_t* rdram, uint32_t source, uint32_t amount) {
    if (!g.running || g.ticking) {
        return amount;
    }
    const int index = index_of(rdram, source);
    if (index < 0) {
        return amount;
    }
    // Only damage is lowered, not a heal (the source's action, +0x07 effect 1).
    const uint32_t attack = rd32(rdram, source + P_ATTACK);
    if (attack >= ogre::addr::kAttackCount ||
        ogre::content::attack_record(static_cast<uint16_t>(attack)).u8(0x07) != 1) {
        return amount;
    }
    int down = 0;
    for (const Active& a : g.active[static_cast<size_t>(index)]) {
        down += g.dots[a.dot].damage_down * a.stacks;
    }
    down = std::min(down, MAX_DAMAGE_DOWN);
    return down == 0 ? amount : static_cast<uint32_t>(int64_t{static_cast<int32_t>(amount)} * (100 - down) / 100);
}
