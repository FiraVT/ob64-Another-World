// Battle forms: a fighter changes class and attack per strike
// (docs/anotherworld/design-battle-transform.md sections 6 and 7).
//
// A pack's "battle_forms" rule names a class and a list of strikes; strike i
// of a battle can name a form (a class to fight as) and an attack.
//
// A battle runs twice. First bank X computes the whole fight with nothing
// drawn (func_ovlX_801EFACC): each attack-kind handler starts a strike by
// recording an event (func_ovlJ_801D9F50), setting the fighter's time cursor
// +0x94 and advancing its strikes used +0x6C, and every step is recorded as a
// timed event in the timeline at *(0x801CE8E0) + 0x10 (func_ovlJ_801D9914).
// Then the fighters are reset and bank J plays the timeline back
// (func_ovlJ_801DB90C); that is what the player sees (play test, 2026-11-01).
//
// So a form is switched in the compute pass, through the game's own visible
// class change func_ovlX_801F106C(participant, time, class), which the game
// uses for its transformations (Golem to Stone Golem, Zombie to Skeleton;
// table 0x801F6A98). It records event 57 at the fighter's cursor plus its
// transformation animation, and event 58 35 ticks later, changes the class,
// key, equipment pictures, strike count and figure handle for the rest of the
// computation, and moves the cursor past the transformation. In the playback,
// event 57 shows the new class in a free participant slot (func_ovlJ_801E1680)
// and event 58 copies it back over the fighter (0x801DDB10).
//
// Event 58 also makes the change permanent: it writes the new class and key
// into the character's record and clears its four equipment slots
// (0x801DDC6C..0x801DDC88). For a port form those writes are undone right
// after (ogre_battle_form_record_post).
//
// Where the port acts (config/hooks.toml):
//   strike start  before each handler reads +0x6C and +0x94 after recording
//                 the strike's start (16 sites in bank X). The strike is +0x6C
//                 (0-based). A new strike of anyone sends every other fighter
//                 in a form back to its class, recorded at that fighter's own
//                 cursor, which is the end of its strike. Then the fighter
//                 takes this strike's form.
//   compute end   0x801EFDE0 in func_ovlX_801EFACC: every fighter still in a
//                 form goes back, before the game takes the fight's length
//                 from the cursors.
//   attack        after the attack lookups: the pickers (func_ovlC_801C898C
//                 through ogre_battle_pick_attack, func_ovlX_801EBCA0) run
//                 before +0x6C advances, so their strike is +0x6C; the strike
//                 routine's lookups (func_ovlX_801E9E9C, func_ovlX_801EC2D0)
//                 run after, so theirs is +0x6C - 1.
//   event 58      0x801DDC6C (the record's address in $v1) and 0x801DDCD0.
//   round reset   0x801AFF84, and the battle's end 0x801B00D0: the words are
//                 put back if a form is still set.
//
// A transformation needs a free participant slot (+0x48 == 0) for the
// playback, and the form's figure (loaded first through
// battle_figures::ensure, because the game's loader spins on a full cache); a
// form that cannot have either is skipped.
//
// Participants whose class word has more than the low byte set (the game's
// special figures, 0x101 and the like) are left alone.
//
// The playback's event 57 shows "<name> has changed class."; the port closes
// that message for its own forms (ogre_battle_form_message).
//
// OGRE_BATTLE_FORMS=0 turns the rules off. OGRE_BATTLE_FORM_MESSAGE=1 keeps the
// message. OGRE_BATTLE_FORM_TRACE=1 logs every strike start, change, attack
// override, event 58 and hidden message.

#include "battle_form.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <vector>

#include <librecomp/overlays.hpp>

#include "battle_figures.hpp"
#include "content_db.hpp"
#include "recomp.h"

namespace {

constexpr uint32_t BATTLE_STATE = 0x801CE8DCu;
constexpr uint32_t PARTICIPANTS = 0x1C4u;
constexpr uint32_t PARTICIPANT_STRIDE = 0xF8u;
constexpr int PARTICIPANT_COUNT = 20;
constexpr uint32_t P_EQUIPMENT = 0x36u;  // four halves
constexpr uint32_t P_FLAGS = 0x40u;
constexpr uint32_t P_CLASS = 0x48u;
constexpr uint32_t P_KEY = 0x4Cu;
constexpr uint32_t P_SQUARE = 0x58u;
constexpr uint32_t P_STRIKES_USED = 0x6Cu;
constexpr uint32_t P_STRIKES = 0x70u;
constexpr uint32_t P_CURSOR = 0x94u;

// The character record event 58 writes (0x801DDC6C..0x801DDC88).
constexpr uint32_t R_CLASS = 0x11u;
constexpr uint32_t R_KEY = 0x12u;
constexpr uint32_t R_EQUIPMENT = 0x2Au;  // four halves

constexpr uint32_t TRANSFORM = 0x801F106Cu;  // func_ovlX_801F106C(participant, time, class)
constexpr uint32_t BANK_X_ROM = 0x22A250u;  // the battle bank that holds it
constexpr uint32_t GEAR_PICTURE[4] = {0x8016DFA8u, 0x8016DFFCu, 0x8016E050u, 0x8016E0A4u};
constexpr uint32_t EQUIPMENT_VARIANT = 0x8016FA54u;
constexpr uint32_t SQUARE_ROW = 0x801C9FC4u;
constexpr uint32_t ANIMATION_LENGTH = 0x801DF2CCu;  // func_ovlJ_801DF2CC(animations, id, 0)
constexpr uint32_t EVENT_58_DELAY = 35u;
constexpr uint32_t MESSAGE_WINDOW = 0x57D4u;  // battle state: the message window
constexpr uint32_t MESSAGE_TIMER = 0x57D0u;   // byte: frames left
constexpr uint32_t CLOSE_WINDOW = 0x8017C31Cu;

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

void wr16(uint8_t* rdram, uint32_t address, uint16_t value) {
    MEM_H(0, static_cast<gpr>(static_cast<int32_t>(address))) = static_cast<int16_t>(value);
}

void wr8(uint8_t* rdram, uint32_t address, uint8_t value) {
    MEM_B(0, static_cast<gpr>(static_cast<int32_t>(address))) = static_cast<int8_t>(value);
}

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

// Calls a game function with up to four arguments and restores every register.
uint32_t call(uint8_t* rdram, recomp_context* ctx, uint32_t function, uint32_t a0, uint32_t a1 = 0,
              uint32_t a2 = 0, uint32_t a3 = 0) {
    const recomp_context saved = *ctx;
    set_arg(ctx->r4, a0);
    set_arg(ctx->r5, a1);
    set_arg(ctx->r6, a2);
    set_arg(ctx->r7, a3);
    LOOKUP_FUNC(function)(rdram, ctx);
    const uint32_t result = static_cast<uint32_t>(ctx->r2);
    *ctx = saved;
    return result;
}

bool in_rdram(uint32_t address) {
    return address >= 0x80000000u && address < 0x80800000u;
}

bool env_on(const char* name, bool fallback) {
    const char* value = std::getenv(name);
    return value == nullptr || value[0] == '\0' ? fallback : value[0] != '0';
}

bool enabled() {
    static const bool on = env_on("OGRE_BATTLE_FORMS", true);
    return on;
}

// OGRE_BATTLE_FORM_MESSAGE=1 keeps the game's "has changed class." message.
bool show_message_hidden() {
    static const bool hidden = !env_on("OGRE_BATTLE_FORM_MESSAGE", false);
    return hidden;
}

bool tracing() {
    static const bool on = env_on("OGRE_BATTLE_FORM_TRACE", false);
    return on;
}

// A fighter's form during the compute pass.
struct Slot {
    bool in_form = false;
    uint32_t base_class = 0;  // the words +0x48 and +0x4C before the first switch
    uint32_t base_key = 0;
    uint16_t base_equipment[4] = {};
    int strike = -1;  // the strike the hooks last saw start
    // The form picked for strike `chosen_strike` (a list picks at random),
    // and the last form this fighter took, which a list avoids.
    int chosen_strike = -1;
    int chosen_form = -1;
    int last_form = -1;
    // The same for the attack list.
    int attack_strike = -1;
    int chosen_attack = -1;
    int last_attack = -1;
};

// A fighter the port transformed in this battle, for event 58 in the playback.
struct Played {
    bool ours = false;
    uint32_t base_class = 0;
    uint16_t base_equipment[4] = {};
    bool saved = false;  // the character record below was read before event 58's first write
    uint32_t record = 0;
    uint8_t record_class = 0;
    uint8_t record_key = 0;
    uint16_t record_equipment[4] = {};
};

uint32_t g_state = 0;
std::array<Slot, PARTICIPANT_COUNT> g_slots;
std::array<Played, PARTICIPANT_COUNT> g_played;
std::vector<std::pair<uint32_t, uint32_t>> g_windows;  // see free_window

int index_of(uint8_t* rdram, uint32_t participant) {
    const uint32_t state = rd32(rdram, BATTLE_STATE);
    if (!in_rdram(state) || participant < state + PARTICIPANTS) {
        return -1;
    }
    const uint32_t offset = participant - (state + PARTICIPANTS);
    if (offset % PARTICIPANT_STRIDE != 0 || offset / PARTICIPANT_STRIDE >= PARTICIPANT_COUNT) {
        return -1;
    }
    if (state != g_state) {
        g_state = state;
        g_slots = {};
        g_played = {};
        g_windows.clear();
    }
    return static_cast<int>(offset / PARTICIPANT_STRIDE);
}

uint32_t participant_at(uint8_t* rdram, int index) {
    return rd32(rdram, BATTLE_STATE) + PARTICIPANTS + static_cast<uint32_t>(index) * PARTICIPANT_STRIDE;
}

void trace(uint8_t* rdram, const char* what, uint32_t site, int index, uint32_t participant) {
    if (!tracing() || index < 0) {
        return;
    }
    std::fprintf(stderr, "[battle-form] trace %s %08X p%d used=%u count=%u cursor=%u class=%08X key=%08X\n", what,
                 site, index, rd32(rdram, participant + P_STRIKES_USED), rd32(rdram, participant + P_STRIKES),
                 rd32(rdram, participant + P_CURSOR), rd32(rdram, participant + P_CLASS),
                 rd32(rdram, participant + P_KEY));
}

uint32_t base_class(uint8_t* rdram, int index, uint32_t participant) {
    const Slot& s = g_slots[static_cast<size_t>(index)];
    return s.in_form ? s.base_class : rd32(rdram, participant + P_CLASS);
}

bool find_rule(uint32_t cls, ogre::content::BattleFormRule& out) {
    if (!ogre::content::loaded()) {
        return false;
    }
    for (ogre::content::BattleFormRule& rule : ogre::content::battle_form_rules()) {
        if (rule.cls == cls) {
            out = std::move(rule);
            return true;
        }
    }
    return false;
}

// The rule's strike `strike` (0-based) for the participant's own class.
bool rule_strike(uint8_t* rdram, int index, uint32_t participant, int strike,
                 ogre::content::BattleFormRule::Strike& out) {
    const uint32_t base = base_class(rdram, index, participant);
    ogre::content::BattleFormRule rule;
    if ((base & ~0xFFu) != 0 || !find_rule(base & 0xFFu, rule)) {
        return false;
    }
    if (strike < 0 || rule.strikes.empty() || (!rule.repeat && strike >= static_cast<int>(rule.strikes.size()))) {
        return false;
    }
    out = rule.strikes[static_cast<size_t>(strike) % rule.strikes.size()];
    return true;
}

// The form for the fighter's strike: -1 keeps the class. A list is drawn once
// per strike, so the picker and the strike agree, and never repeats the
// fighter's last form when it has another.
// One of `choices` at random, avoiding `last` when there is another; -1 for
// none.
int draw(const std::vector<int>& choices, int last) {
    if (choices.empty()) {
        return -1;
    }
    std::vector<int> pool;
    for (const int choice : choices) {
        if (choice != last) {
            pool.push_back(choice);
        }
    }
    if (pool.empty()) {
        pool = choices;
    }
    static std::mt19937 random{std::random_device{}()};
    return pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(random)];
}

int chosen_form(int index, int strike, const ogre::content::BattleFormRule::Strike& want) {
    Slot& s = g_slots[static_cast<size_t>(index)];
    if (want.forms.empty()) {
        return -1;
    }
    if (s.chosen_strike == strike) {
        return s.chosen_form;
    }
    const int form = draw(want.forms, s.last_form);
    s.chosen_strike = strike;
    s.chosen_form = form;
    if (form >= 0) {
        s.last_form = form;
    }
    return form;
}

// The attack for the fighter's strike: -1 when the rule names none. A list is
// drawn once per strike, as chosen_form does.
int chosen_attack(int index, int strike, const ogre::content::BattleFormRule::Strike& want) {
    Slot& s = g_slots[static_cast<size_t>(index)];
    if (want.attacks.empty()) {
        return -1;
    }
    if (s.attack_strike == strike) {
        return s.chosen_attack;
    }
    const int attack = draw(want.attacks, s.last_attack);
    s.attack_strike = strike;
    s.chosen_attack = attack;
    s.last_attack = attack;
    return attack;
}

// The form's breath attack: the first breath (attack kind 5) in its rows,
// front first; -1 when it has none.
int breath_attack(int form) {
    if (form < 0 || !ogre::content::loaded()) {
        return -1;
    }
    std::lock_guard lock(ogre::content::records_mutex());
    const ogre::content::ClassRecord record = ogre::content::class_record(static_cast<uint16_t>(form));
    for (int row = 0; row < 3; row++) {
        const uint8_t attack = record.row_attack(row);
        if (record.row_count(row) != 0 && attack < ogre::content::info(ogre::content::Table::Attacks).count &&
            ogre::content::attack_record(attack).kind() == 5u) {
            return attack;
        }
    }
    return -1;
}

bool free_slot(uint8_t* rdram) {
    for (int n = 0; n < PARTICIPANT_COUNT; n++) {
        if (rd32(rdram, participant_at(rdram, n) + P_CLASS) == 0) {
            return true;
        }
    }
    return false;
}

// The figure func_ovlX_801F106C and the playback will ask for when the
// participant becomes `to`: its class is `to` when the class and key words are
// equal, its key `to`, and its variant comes from that class's gear.
bool figure_ready(uint8_t* rdram, recomp_context* ctx, uint32_t participant, uint32_t to) {
    const uint32_t class_word = rd32(rdram, participant + P_CLASS);
    const uint32_t key_word = rd32(rdram, participant + P_KEY);
    const uint32_t cls = key_word == class_word ? to : class_word;
    uint32_t gear[4];
    for (int i = 0; i < 4; i++) {
        gear[i] = call(rdram, ctx, GEAR_PICTURE[i], cls & 0xFFu, to) & 0xFFFFu;
    }
    const uint32_t variant = call(rdram, ctx, EQUIPMENT_VARIANT, gear[0], gear[1], gear[2], gear[3]) & 0xFFFFu;
    const uint32_t flags = rd32(rdram, participant + P_FLAGS);
    return ogre::battle_figures::ensure(rdram, ctx, cls, to, (flags >> 8) & 1u, (flags >> 10) & 1u, variant, 0) > 0;
}

// The playback's events 57 and 58 of every change recorded in this battle,
// by time. All of them use the same spare participant slot (the first with
// +0x48 == 0, which the computation never fills), so two changes whose 57..58
// windows overlap overwrite each other's copy, and a fighter can be copied
// back from an emptied slot (play test, 2026-11-01: event 58 left p7 class 0).
// g_windows holds them.

// The first start at or after `earliest` whose window [start, start + 35]
// overlaps no recorded one.
uint32_t free_window(uint32_t earliest) {
    uint32_t start = earliest;
    for (bool moved = true; moved;) {
        moved = false;
        for (const auto& [from, to] : g_windows) {
            if (start <= to && start + EVENT_58_DELAY >= from) {
                start = to + 1;
                moved = true;
            }
        }
    }
    return start;
}

// func_ovlX_801F106C(participant, time, to), keeping the strike count. The
// time is the fighter's cursor, or later when the change would overlap
// another one in the playback. False when the change could not be recorded.
bool transform(uint8_t* rdram, recomp_context* ctx, int index, uint32_t participant, uint32_t to) {
    if (!recomp::overlays::is_function_bank_loaded(BANK_X_ROM) || (rd32(rdram, participant + P_FLAGS) & 1u) != 0) {
        return false;
    }
    if (!free_slot(rdram)) {
        std::fprintf(stderr, "[battle-form] participant %d: no free participant slot for the change to class %u\n",
                     index, to);
        return false;
    }
    if (!figure_ready(rdram, ctx, participant, to)) {
        std::fprintf(stderr, "[battle-form] participant %d: class %u's figure is not loaded; no change\n", index, to);
        return false;
    }
    // func_ovlX_801F106C puts event 57 at time + the length of the fighter's
    // animations 17 and 18 (0x801F1088..0x801F10C4) and event 58 35 later.
    const uint32_t animations = rd32(rdram, participant) + 0x44u;
    const uint32_t lead = call(rdram, ctx, ANIMATION_LENGTH, animations, 17) +
                          call(rdram, ctx, ANIMATION_LENGTH, animations, 18);
    const uint32_t cursor = rd32(rdram, participant + P_CURSOR);
    const uint32_t start = free_window(cursor + lead);
    const uint32_t strikes = rd32(rdram, participant + P_STRIKES);
    call(rdram, ctx, TRANSFORM, participant, start - lead, to);
    wr32(rdram, participant + P_STRIKES, strikes);
    g_windows.emplace_back(start, start + EVENT_58_DELAY);
    if (tracing()) {
        std::fprintf(stderr, "[battle-form] trace change p%d to %u: cursor %u, events 57/58 at %u/%u, cursor now %u\n",
                     index, to, cursor, start, start + EVENT_58_DELAY, rd32(rdram, participant + P_CURSOR));
    }
    return true;
}

void put_back_words(uint8_t* rdram, int index) {
    Slot& s = g_slots[static_cast<size_t>(index)];
    const uint32_t p = participant_at(rdram, index);
    wr32(rdram, p + P_CLASS, s.base_class);
    wr32(rdram, p + P_KEY, s.base_key);
    for (uint32_t i = 0; i < 4; i++) {
        wr16(rdram, p + P_EQUIPMENT + 2 * i, s.base_equipment[i]);
    }
    s.in_form = false;
}

// Sends a fighter in a form back to its class. With `ctx` the change back is
// recorded for the playback; without it only the words are put back.
void restore(uint8_t* rdram, recomp_context* ctx, int index) {
    Slot& s = g_slots[static_cast<size_t>(index)];
    if (!s.in_form) {
        return;
    }
    const uint32_t p = participant_at(rdram, index);
    const bool recorded = ctx != nullptr && transform(rdram, ctx, index, p, s.base_key & 0xFFu);
    put_back_words(rdram, index);
    std::fprintf(stderr, "[battle-form] participant %d: back to class %u%s\n", index, s.base_class & 0xFFu,
                 recorded ? "" : " (not shown)");
}

void apply(uint8_t* rdram, recomp_context* ctx, int index, uint32_t participant, uint32_t form, int strike) {
    Slot& s = g_slots[static_cast<size_t>(index)];
    if (s.in_form && (rd32(rdram, participant + P_KEY) & 0xFFu) == form) {
        return;
    }
    const uint32_t class_word = rd32(rdram, participant + P_CLASS);
    const uint32_t key_word = rd32(rdram, participant + P_KEY);
    uint16_t equipment[4];
    for (uint32_t i = 0; i < 4; i++) {
        equipment[i] = rd16(rdram, participant + P_EQUIPMENT + 2 * i);
    }
    if (!transform(rdram, ctx, index, participant, form)) {
        return;
    }
    if (!s.in_form) {
        s.base_class = class_word;
        s.base_key = key_word;
        std::copy(std::begin(equipment), std::end(equipment), std::begin(s.base_equipment));
        s.in_form = true;
    }
    Played& played = g_played[static_cast<size_t>(index)];
    if (!played.ours) {
        played.ours = true;
        played.base_class = s.base_class;
        std::copy(std::begin(s.base_equipment), std::end(s.base_equipment), std::begin(played.base_equipment));
    }
    std::fprintf(stderr, "[battle-form] participant %d strike %d: class %u -> %u\n", index, strike + 1,
                 s.base_class & 0xFFu, form);
}

}  // namespace

namespace ogre::battle_form {

uint32_t attack_count(uint8_t* rdram, uint32_t participant, uint32_t count) {
    if (!enabled()) {
        return count;
    }
    const int index = index_of(rdram, participant);
    if (index < 0) {
        return count;
    }
    const uint32_t base = base_class(rdram, index, participant);
    content::BattleFormRule rule;
    const bool ruled = (base & ~0xFFu) == 0 && find_rule(base & 0xFFu, rule) && rule.attacks > 0;
    const uint32_t result = ruled ? static_cast<uint32_t>(rule.attacks) : count;
    if (tracing()) {
        std::fprintf(stderr, "[battle-form] trace count p%d class=%08X %u -> %u\n", index, base, count, result);
    }
    return result;
}

uint32_t attack(uint8_t* rdram, recomp_context* ctx, uint32_t participant, uint32_t attack, bool picker) {
    if (!enabled()) {
        return attack;
    }
    const int index = index_of(rdram, participant);
    if (index < 0) {
        return attack;
    }
    const int strike = static_cast<int>(rd32(rdram, participant + P_STRIKES_USED)) - (picker ? 0 : 1);
    content::BattleFormRule::Strike want{};
    if (!rule_strike(rdram, index, participant, strike, want)) {
        return attack;
    }
    const int form = chosen_form(index, strike, want);
    int result = want.breath ? breath_attack(form) : chosen_attack(index, strike, want);
    // A picker runs before the strike's form is set, so a strike without an
    // "attack" takes the form's attack for the fighter's row here.
    if (result < 0 && picker && form >= 0 && ctx != nullptr && content::loaded()) {
        const uint32_t row = call(rdram, ctx, SQUARE_ROW, rd32(rdram, participant + P_SQUARE)) & 0xFFu;
        if (row < 3) {
            std::lock_guard lock(content::records_mutex());
            result = content::class_record(static_cast<uint16_t>(form)).row_attack(static_cast<int>(row));
        }
    }
    if (result < 0) {
        return attack;
    }
    if (tracing()) {
        std::fprintf(stderr, "[battle-form] trace attack p%d strike %d %u -> %d (%s)\n", index, strike + 1, attack,
                     result, picker ? "picker" : "strike");
    }
    return static_cast<uint32_t>(result);
}

}  // namespace ogre::battle_form

// Before a handler reads +0x6C and +0x94 at the start of a strike.
extern "C" void ogre_battle_form_strike(uint8_t* rdram, recomp_context* ctx, uint32_t participant, uint32_t site) {
    if (!enabled()) {
        return;
    }
    const int index = index_of(rdram, participant);
    if (index < 0) {
        return;
    }
    trace(rdram, "strike", site, index, participant);
    Slot& s = g_slots[static_cast<size_t>(index)];
    const int strike = static_cast<int>(rd32(rdram, participant + P_STRIKES_USED));
    if (strike > s.strike) {
        // A new strike started: the forms of the strikes before it end, each
        // at its own fighter's cursor.
        for (int other = 0; other < PARTICIPANT_COUNT; other++) {
            if (other != index) {
                restore(rdram, ctx, other);
            }
        }
    }
    s.strike = strike;
    ogre::content::BattleFormRule::Strike want{};
    const int form = rule_strike(rdram, index, participant, strike, want) ? chosen_form(index, strike, want) : -1;
    if (form < 0 || static_cast<uint32_t>(form) == (base_class(rdram, index, participant) & 0xFFu)) {
        restore(rdram, ctx, index);
        return;
    }
    apply(rdram, ctx, index, participant, static_cast<uint32_t>(form), strike);
}

// 0x801EFDE0: the computation is over; every form goes back.
extern "C" void ogre_battle_form_compute_end(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t state = rd32(rdram, BATTLE_STATE);
    if (!in_rdram(state) || state != g_state) {
        return;
    }
    for (int index = 0; index < PARTICIPANT_COUNT; index++) {
        restore(rdram, ctx, index);
        g_slots[static_cast<size_t>(index)].strike = -1;
    }
}

extern "C" uint32_t ogre_battle_form_attack(uint8_t* rdram, recomp_context* ctx, uint32_t participant,
                                            uint32_t attack, uint32_t kind) {
    return ogre::battle_form::attack(rdram, ctx, participant, attack & 0xFFu, kind != 0);
}

// Event 58 in the playback, before it writes the character record `record`.
extern "C" void ogre_battle_form_record_pre(uint8_t* rdram, uint32_t participant, uint32_t record) {
    const int index = index_of(rdram, participant);
    if (index < 0 || !in_rdram(record)) {
        return;
    }
    Played& p = g_played[static_cast<size_t>(index)];
    if (!p.ours || p.saved) {
        return;
    }
    p.saved = true;
    p.record = record;
    p.record_class = rd8(rdram, record + R_CLASS);
    p.record_key = rd8(rdram, record + R_KEY);
    for (uint32_t i = 0; i < 4; i++) {
        p.record_equipment[i] = rd16(rdram, record + R_EQUIPMENT + 2 * i);
    }
}

// Event 58 in the playback, after its writes: a port form leaves the
// character record as it was, and the fighter's equipment pictures go back
// with its class.
extern "C" void ogre_battle_form_record_post(uint8_t* rdram, uint32_t participant) {
    const int index = index_of(rdram, participant);
    if (index < 0) {
        return;
    }
    const Played& p = g_played[static_cast<size_t>(index)];
    if (!p.ours || !p.saved) {
        return;
    }
    wr8(rdram, p.record + R_CLASS, p.record_class);
    wr8(rdram, p.record + R_KEY, p.record_key);
    for (uint32_t i = 0; i < 4; i++) {
        wr16(rdram, p.record + R_EQUIPMENT + 2 * i, p.record_equipment[i]);
    }
    const bool back = (rd32(rdram, participant + P_CLASS) & 0xFFu) == (p.base_class & 0xFFu);
    if (back) {
        for (uint32_t i = 0; i < 4; i++) {
            wr16(rdram, participant + P_EQUIPMENT + 2 * i, p.base_equipment[i]);
        }
    }
    if (tracing()) {
        std::fprintf(stderr, "[battle-form] trace event58 p%d class=%08X record kept%s\n", index,
                     rd32(rdram, participant + P_CLASS), back ? ", equipment back" : "");
    }
}

// 0x801AFBF0 in func_ovlC_801AFB2C, which event 57's playback calls
// (0x801DDB00) to show "<name> has changed class.": func_ovlC_801AF9D8 has just
// opened the message window, kept at battle state +0x57D4 with its timer at
// +0x57D0. For a port form the window is closed at once, the way the game
// closes it when the timer runs out (0x801D7FA8..0x801D7FC4).
extern "C" void ogre_battle_form_message(uint8_t* rdram, recomp_context* ctx, uint32_t participant) {
    const int index = index_of(rdram, participant);
    if (index < 0 || !g_played[static_cast<size_t>(index)].ours || !show_message_hidden()) {
        return;
    }
    const uint32_t state = rd32(rdram, BATTLE_STATE);
    const uint32_t window = rd32(rdram, state + MESSAGE_WINDOW);
    if (window != 0) {
        call(rdram, ctx, CLOSE_WINDOW, window);
        wr32(rdram, state + MESSAGE_WINDOW, 0);
    }
    wr8(rdram, state + MESSAGE_TIMER, 0);
    if (tracing()) {
        std::fprintf(stderr, "[battle-form] trace message p%d: \"has changed class\" hidden\n", index);
    }
}

extern "C" void ogre_battle_form_reset(uint8_t* rdram, recomp_context* /*ctx*/, uint32_t participant) {
    const int index = index_of(rdram, participant);
    if (index < 0) {
        return;
    }
    trace(rdram, "reset", 0x801AFF84u, index, participant);
    if (g_slots[static_cast<size_t>(index)].in_form) {
        put_back_words(rdram, index);
    }
    g_slots[static_cast<size_t>(index)].strike = -1;

    // func_ovlX_801F106C changes the class word only when the key word equals
    // it; otherwise it changes the key alone and the class stays. A character
    // put in a new class by a script has key 0 (play test, 2026-11-01: the
    // Dragon Knight kept class 172 with a dragon key, and the dragon did not
    // show, while the Fighter, key = class 2, changed both and did). For a
    // fighter with a rule, the key gets its class here, before the fight is
    // computed, as every standard class has it; the character record is not
    // touched.
    const uint32_t cls = rd32(rdram, participant + P_CLASS);
    const uint32_t key = rd32(rdram, participant + P_KEY);
    ogre::content::BattleFormRule rule;
    if (key != cls && (cls & ~0xFFu) == 0 && (key & 0xFFu) == 0 && find_rule(cls, rule)) {
        wr32(rdram, participant + P_KEY, cls);
        std::fprintf(stderr, "[battle-form] participant %d: class %u has key 0; key set to the class for its forms\n",
                     index, cls);
    }
}

extern "C" void ogre_battle_form_end(uint8_t* rdram, recomp_context* /*ctx*/) {
    const uint32_t state = rd32(rdram, BATTLE_STATE);
    if (!in_rdram(state) || state != g_state) {
        return;
    }
    for (int index = 0; index < PARTICIPANT_COUNT; index++) {
        if (g_slots[static_cast<size_t>(index)].in_form) {
            put_back_words(rdram, index);
        }
    }
    g_slots = {};
    g_played = {};
    g_windows.clear();
}
