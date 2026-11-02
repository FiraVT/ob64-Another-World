#include "game_addresses.hpp"
#include "field_view.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "auto_commander.hpp"
#include "boss_escorts.hpp"
#include "field_select.hpp"
#include "input_map.hpp"
#include "liberation.hpp"
#include "organize_edit.hpp"
#include "game_events.hpp"
#include "recomp.h"
#include "settings.hpp"
#include "ultramodern/ultramodern.hpp"

namespace ogre::field_view {

namespace {

// Guest memory, revision 1 (docs/anotherworld/design-field-controls.md section 4, captures
// of 2026-10-30).
constexpr uint32_t SCENE = ::ogre::addr::kActiveScene;             // u16, 0x03 = mission field
constexpr int SCENE_FIELD = 0x03;
constexpr uint32_t OBJECTS = ::ogre::addr::kMapObjects;           // map object pointer per unit slot
constexpr uint32_t UNITS = ::ogre::addr::kUnits;             // unit records, 0x19 each
constexpr uint32_t UNIT_STRIDE = 0x19u;
constexpr uint32_t CHARACTERS = ::ogre::addr::kCharacters;        // player characters, 0x38 each
constexpr uint32_t CHARACTER_STRIDE = 0x38u;
constexpr uint32_t CLASSES = ::ogre::addr::kClasses;           // class table, 0x48 each, name pointer at +0
constexpr int PLAYER_UNITS = 30;

// Object flags: 0x01 out on the map, 0x02 marching, 0x00800000 inside a location.
constexpr uint32_t ON_MAP = 0x01u;
constexpr uint32_t MARCHING = 0x02u;
constexpr uint32_t IN_LOCATION = 0x00800000u;

// The game's world-to-screen projection (R2): func_801A9DD0(camera, &world xyz,
// &screen x, &screen y), camera = *(0x8018F5AC). The unit pick func_ovlN_801B8070
// calls it to put the cursor on a unit (0x801B81AC); results are framebuffer
// pixels of the 320x240 picture.
constexpr uint32_t PROJECT = 0x801A9DD0u;
constexpr uint32_t CAMERA = 0x8018F5ACu;
constexpr int SLOTS = 60;

struct Screen {
    bool valid = false;
    float x = 0.0f, y = 0.0f;
    bool has_dest = false;       // marching: its destination (object +0x28..+0x30)
    float dx = 0.0f, dy = 0.0f;
};
std::mutex g_screen_mutex;
Screen g_screen[SLOTS];

// Locations (towns, temples, strongholds): count word 0x801F1090; state u16
// (0x02 player, 0x04 enemy, 0x10 headquarters), the name at +0x08 (ASCII, 20
// bytes; captures 14-15 read Boolem, Movae, Kiefer, Baldera, Fort Tuatha, and
// none for the headquarters) and the town's own alignment byte at +0x02 (0xFF
// cannot be taken, bit 0x80 adds 40 to the liberation score) at 0x801951EC +
// i*0x24; position x, z floats at map_data + 0x1C + i*8, map_data = the word at
// 0x800E7A90 + 4 * byte 0x800E7AC5.
constexpr uint32_t LOCATION_COUNT = ::ogre::addr::kLocationCount;
constexpr uint32_t LOCATION_STATE = ::ogre::addr::kLocationState;
constexpr uint32_t LOCATION_STRIDE = 0x24u;
constexpr uint32_t MAP_DATA_TABLE = ::ogre::addr::kMapDataTable;
constexpr uint32_t MAP_ID = ::ogre::addr::kMapId;
constexpr int MAX_PLACES = 64;
constexpr float TOWN_RADIUS = 14.0f;  // framebuffer pixels

struct Place {
    bool valid = false;
    float x = 0.0f, y = 0.0f;    // screen
    float wx = 0.0f, wz = 0.0f;  // world
    uint16_t state = 0;
    uint8_t alignment = 0;
    std::string name;  // record +0x08, up to 20 bytes; empty for the headquarters
};
Place g_places[MAX_PLACES];

// The rectangles of every clickable element (class "hit": strip tiles, bar
// cards, command buttons) as fractions of the window, with the element's id,
// for the click test on the SDL event thread. Read after layout, so they lag
// the markup by one frame.
struct Rect {
    std::string id;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
};
std::vector<Rect> g_hit_rects;
Rect g_strip_rect;  // the strip's own box, for the wheel

Rml::ElementDocument* g_document = nullptr;
Rml::Element* g_top = nullptr;
Rml::Element* g_strip = nullptr;
Rml::Element* g_marks = nullptr;
Rml::Element* g_help = nullptr;  // the pad's controls
std::string g_help_shown;
Rml::Element* g_bar = nullptr;
std::string g_top_shown, g_strip_shown, g_marks_shown, g_bar_shown;
bool g_visible = false;

uint8_t rd8(const uint8_t* rdram, uint32_t a) {
    return rdram[(a - 0x80000000u) ^ 3u];
}

uint16_t rd16(const uint8_t* rdram, uint32_t a) {
    return static_cast<uint16_t>((rd8(rdram, a) << 8) | rd8(rdram, a + 1));
}

uint32_t rd32(const uint8_t* rdram, uint32_t a) {
    return (static_cast<uint32_t>(rd16(rdram, a)) << 16) | rd16(rdram, a + 2);
}

void wr32(uint8_t* rdram, uint32_t a, uint32_t v) {
    for (uint32_t i = 0; i < 4; i++) {
        rdram[(a + i - 0x80000000u) ^ 3u] = static_cast<uint8_t>(v >> (24 - 8 * i));
    }
}

bool in_ram(uint32_t a) {
    return a >= 0x80000000u && a < 0x80800000u;
}

std::string class_name(const uint8_t* rdram, int cls) {
    const uint32_t pointer = rd32(rdram, CLASSES + static_cast<uint32_t>(cls) * 0x48u);
    std::string name;
    if (in_ram(pointer)) {
        for (uint32_t i = 0; i < 20; i++) {
            const uint8_t c = rd8(rdram, pointer + i);
            if (c == 0) {
                break;
            }
            name += c == 0x81 ? ' ' : (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : ' ';
        }
    }
    return name;
}

std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string(1, c);
    }
    return out;
}

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

void set_arg(gpr& reg, uint32_t value) {
    reg = static_cast<gpr>(static_cast<int32_t>(value));
}

// On the field update (game thread, bank A and N resident): every unit on the
// map through the game's projection. The field update's own locals at sp+0x10
// and sp+0x14 are dead at this point (its epilogue), and the game's pick uses
// the same two slots.
void project_units(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t camera = rd32(rdram, CAMERA);
    if (!in_ram(camera)) {
        return;
    }
    Screen out[SLOTS];
    const recomp_context saved = *ctx;
    const uint32_t sp = static_cast<uint32_t>(ctx->r29);
    for (int slot = 0; slot < SLOTS; slot++) {
        const uint32_t object = rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
        if (!in_ram(object) || (rd32(rdram, object) & 0x11u) != 0x11u) {
            continue;
        }
        // An enemy the game does not show yet (object flag 0x20 clear: garrisons
        // read 0x111 until they come into view, then 0x131; captures 12 and 14
        // of 2026-10-30 04:37) gets no marker and cannot be targeted.
        if (slot >= PLAYER_UNITS && (rd32(rdram, object) & 0x20u) == 0) {
            continue;
        }
        set_arg(ctx->r4, camera);
        set_arg(ctx->r5, object + 0x08u);
        set_arg(ctx->r6, sp + 0x10u);
        set_arg(ctx->r7, sp + 0x14u);
        LOOKUP_FUNC(PROJECT)(rdram, ctx);
        out[slot] = {true, as_float(rd32(rdram, sp + 0x10u)), as_float(rd32(rdram, sp + 0x14u))};
        *ctx = saved;
        // A marching unit of the player's: its destination as well, for the
        // path line.
        if (slot < PLAYER_UNITS && (rd32(rdram, object) & MARCHING)) {
            set_arg(ctx->r4, camera);
            set_arg(ctx->r5, object + 0x28u);
            set_arg(ctx->r6, sp + 0x10u);
            set_arg(ctx->r7, sp + 0x14u);
            LOOKUP_FUNC(PROJECT)(rdram, ctx);
            out[slot].dx = as_float(rd32(rdram, sp + 0x10u));
            out[slot].dy = as_float(rd32(rdram, sp + 0x14u));
            out[slot].has_dest = std::isfinite(out[slot].dx) && std::isfinite(out[slot].dy);
            *ctx = saved;
        }
    }
    // The locations, at ground height 0 through the same routine, with the
    // input vector in the dead locals at sp+0x18..0x23.
    Place places[MAX_PLACES];
    const int count = static_cast<int>(rd32(rdram, LOCATION_COUNT));
    const uint32_t map_data = rd32(rdram, MAP_DATA_TABLE + 4u * rd8(rdram, MAP_ID));
    if (count > 0 && count <= MAX_PLACES && in_ram(map_data)) {
        for (int i = 0; i < count; i++) {
            const uint32_t wx = rd32(rdram, map_data + 0x1Cu + static_cast<uint32_t>(i) * 8u);
            const uint32_t wz = rd32(rdram, map_data + 0x20u + static_cast<uint32_t>(i) * 8u);
            wr32(rdram, sp + 0x18u, wx);
            wr32(rdram, sp + 0x1Cu, 0);
            wr32(rdram, sp + 0x20u, wz);
            set_arg(ctx->r4, camera);
            set_arg(ctx->r5, sp + 0x18u);
            set_arg(ctx->r6, sp + 0x10u);
            set_arg(ctx->r7, sp + 0x14u);
            LOOKUP_FUNC(PROJECT)(rdram, ctx);
            const uint32_t s = LOCATION_STATE + static_cast<uint32_t>(i) * LOCATION_STRIDE;
            std::string name;
            for (uint32_t k = 0; k < 20; k++) {
                const char c = static_cast<char>(rd8(rdram, s + 0x08 + k));
                if (c == '\0') {
                    break;
                }
                name += (c >= 32 && c < 127) ? c : '?';
            }
            places[i] = {true, as_float(rd32(rdram, sp + 0x10u)), as_float(rd32(rdram, sp + 0x14u)),
                         as_float(wx), as_float(wz), rd16(rdram, s), rd8(rdram, s + 2), name};
            *ctx = saved;
        }
    }
    std::lock_guard<std::mutex> lock(g_screen_mutex);
    std::copy(std::begin(out), std::end(out), std::begin(g_screen));
    std::copy(std::begin(places), std::end(places), std::begin(g_places));
}

// --- Unit summaries and markup (render thread) --------------------------------

// The unit colours of the battlefield mockup (docs/anotherworld/design-field-controls.md
// section 7), one per slot, shared by the strip chip, the field marker and the
// selection card.
const char* const kUnitColours[8] = {"#3B4BA8", "#7A3EA0", "#A8502E", "#3C6E50",
                                     "#8C6A1E", "#2E7B86", "#5A5AA6", "#8A3E5C"};

const char* unit_colour(int slot) {
    return kUnitColours[slot % 8];
}

struct UnitInfo {
    bool out = false;
    uint32_t flags = 0;
    std::string name;   // the leader's name
    std::string cls;    // the leader's class
    int class_id = 0;   // the leader's class id, for the portrait
    int level = 0;
    int alignment = 0;  // average over the members (the liberation rule's input)
    int hp = 0, hp_max = 0, percent = 0;
    int strategy = 0;   // unit record +0x17
    float wx = 0.0f, wz = 0.0f;
    float dest_x = 0.0f, dest_z = 0.0f;  // object +0x28 / +0x30
    int target = -1;    // object +0x84: the pursued slot
    bool chasing = false;  // the overlay's chase (field_select::chase_target)
    int location = -1;  // object +0x74: the location it stands in
    uint8_t items[10] = {};  // unit record +0x0D..+0x16, consumable ids (0 = empty)
    std::string activity;    // filled in update(): what the unit is doing
};

std::string guest_text(const uint8_t* rdram, uint32_t address, int max) {
    std::string out;
    for (int i = 0; i < max; i++) {
        const char c = static_cast<char>(rd8(rdram, address + static_cast<uint32_t>(i)));
        if (c == '\0') {
            break;
        }
        out += (c >= 32 && c < 127) ? c : '?';
    }
    return out;
}

UnitInfo read_unit(const uint8_t* rdram, int slot, bool on_map = true) {
    UnitInfo u;
    const uint32_t object = rd32(rdram, OBJECTS + static_cast<uint32_t>(slot) * 4u);
    if (!in_ram(object)) {
        return u;
    }
    u.flags = rd32(rdram, object);
    if (on_map && (u.flags & 0x11u) != 0x11u) {
        return u;
    }
    const uint32_t unit = UNITS + static_cast<uint32_t>(slot) * UNIT_STRIDE;
    int members = 0;
    for (uint32_t i = 0; i < 5; i++) {
        const int ref = rd8(rdram, unit + 0x02 + i);
        if (ref < 1 || ref >= 100) {
            continue;
        }
        const uint32_t c = CHARACTERS + static_cast<uint32_t>(ref) * CHARACTER_STRIDE;
        if (members == 0) {
            u.name = guest_text(rdram, c, 16);
            u.class_id = rd8(rdram, c + 0x11);
            u.cls = class_name(rdram, u.class_id);
            u.level = rd8(rdram, c + 0x13);
        }
        members++;
        u.alignment += rd8(rdram, c + 0x1B);
        u.hp_max += rd16(rdram, c + 0x16);  // +0x16 maximum, +0x18 current
        u.hp += rd16(rdram, c + 0x18);
    }
    if (members == 0) {
        return u;
    }
    u.out = true;
    u.alignment /= members;
    u.percent = u.hp_max > 0 ? std::clamp(u.hp * 100 / u.hp_max, 0, 100) : 0;
    u.strategy = rd8(rdram, unit + 0x17);
    u.wx = as_float(rd32(rdram, object + 0x08));
    u.wz = as_float(rd32(rdram, object + 0x10));
    u.dest_x = as_float(rd32(rdram, object + 0x28));
    u.dest_z = as_float(rd32(rdram, object + 0x30));
    u.target = static_cast<int>(rd32(rdram, object + 0x84));
    u.location = static_cast<int>(rd32(rdram, object + 0x74));
    for (uint32_t i = 0; i < 10; i++) {
        u.items[i] = rd8(rdram, unit + 0x0D + i);
    }
    return u;
}

// The marker shape and colour class for an alignment: high, middle, low. The
// liberation rule compares the value with each town's own (R6); the marker is
// a reading aid.
const char* align_mark(int a) {
    return a >= 60 ? "&#9650;" : a >= 40 ? "&#9670;" : "&#9660;";
}
const char* align_class(int a) {
    return a >= 60 ? "high" : a >= 40 ? "mid" : "low";
}

// Object flag 0x2000: set by the pursue and hold routines (func_ovlN_801D44D4,
// func_ovlN_801D4630), cleared by func_ovlN_801D45F8.
constexpr uint32_t PURSUING = 0x2000u;

const char* status_text(const UnitInfo& u) {
    if (u.percent < 35) {
        return "LOW";
    }
    if (u.chasing || ((u.flags & PURSUING) && u.target >= 0)) {
        return "CHASE";
    }
    return (u.flags & MARCHING) ? "MARCH" : (u.flags & IN_LOCATION) ? "IN TOWN" : (u.flags & PURSUING) ? "HOLD" : "IDLE";
}

// The carried items as icons, one per kind with a count (the game's menu icons,
// `icon:<item>` in rml_ui.cpp).
std::string item_icons(const UnitInfo& u) {
    std::string out = "<div class=\"carry\">";
    int kinds = 0;
    for (int i = 0; i < 10; i++) {
        const int item = u.items[i];
        bool seen = false;
        for (int j = 0; j < i; j++) {
            seen |= u.items[j] == item;
        }
        if (item == 0 || seen) {
            continue;
        }
        int count = 0;
        for (int j = 0; j < 10; j++) {
            count += u.items[j] == item;
        }
        kinds++;
        out += "<div class=\"carryitem\"><img class=\"icon\" src=\"icon:" + std::to_string(item) + "\"/>" +
               (count > 1 ? "<span class=\"carrycount\">" + std::to_string(count) + "</span>" : std::string()) + "</div>";
    }
    if (kinds == 0) {
        out += "<span class=\"carrynone\">Carries nothing</span>";
    }
    return out + "</div>";
}

// One army strip tile: the number on the unit's colour, status, alignment.
std::string strip_tile(const UnitInfo& u, int slot, bool picked) {
    return "<div class=\"tile hit" + std::string(picked ? " picked" : "") + "\" id=\"t" + std::to_string(slot) +
           "\"><div class=\"tileface\" style=\"background-color: " + unit_colour(slot) + "\"><img class=\"portrait\" src=\"portrait:" +
           std::to_string(u.class_id) + "\"/><div class=\"chip\" style=\"background-color: " + unit_colour(slot) + "\">" +
           std::to_string(slot + 1) + "</div></div><div class=\"status " + (u.percent < 35 ? "lowhp" : "") + "\">" +
           status_text(u) + "</div><div class=\"align " + align_class(u.alignment) + "\">ALN " +
           align_mark(u.alignment) + " " + std::to_string(u.alignment) + "</div><div class=\"hp\"><div class=\"hpfill" +
           (u.percent < 35 ? " lowfill" : "") + "\" style=\"width: " + std::to_string(u.percent) + "%\"></div></div></div>";
}

// A unit still in reserve (not dispatched): greyed, and a click sends it out
// through the Dispatch command (id d<slot>).
std::string waiting_tile(const UnitInfo& u, int slot) {
    return "<div class=\"tile waiting hit\" id=\"d" + std::to_string(slot) + "\"><div class=\"tileface\" style=\"background-color: " +
           unit_colour(slot) + "\"><img class=\"portrait\" src=\"portrait:" + std::to_string(u.class_id) +
           "\"/><div class=\"chip\" style=\"background-color: " + unit_colour(slot) + "\">" + std::to_string(slot + 1) +
           "</div></div><div class=\"status\">SEND</div></div>";
}

// The liberation verdict of a unit's alignment against a town (R6,
// liberation.hpp).
struct Verdict {
    const char* word = "";
    const char* cls = "";
};
Verdict verdict_for(int unit, const Place& place) {
    switch (liberation::verdict(unit, place.alignment)) {
        case liberation::Verdict::Captures: return {"captures", "bad"};
        case liberation::Verdict::Liberates: return {"liberates", "good"};
        default: return {"may capture", "risk"};
    }
}

// One selection card: initials on the unit's colour, unit and leader, class
// and level, HP, alignment (with the verdict for the town under the pointer).
std::string selection_card(const UnitInfo& u, int slot, const Place* target) {
    std::string verdict;
    if (target != nullptr && target->alignment != 0xFF && !(target->state & 0x02u)) {
        const Verdict v = verdict_for(u.alignment, *target);
        verdict = " &#183; <span class=\"" + std::string(v.cls) + "\">" + v.word + "</span>";
    }
    return "<div class=\"card hit\" id=\"s" + std::to_string(slot) + "\"><div class=\"face\" style=\"background-color: " +
           unit_colour(slot) + "\"><img class=\"portrait\" src=\"portrait:" + std::to_string(u.class_id) +
           "\"/></div><div class=\"cardtext\">" +
           "<div class=\"cardtitle\">Unit " + std::to_string(slot + 1) + " &#183; " + escape(u.name) +
           "<span class=\"x\">&#215;</span></div><div class=\"cardsub\">" + escape(u.cls) + " &#183; Lv " +
           std::to_string(u.level) + "</div><div class=\"hp\"><div class=\"hpfill\" style=\"width: " +
           std::to_string(u.percent) + "%\"></div></div><div class=\"hptext\">HP " + std::to_string(u.hp) + " / " +
           std::to_string(u.hp_max) + "</div><div class=\"activity\">" + escape(u.activity) +
           "</div><div class=\"align " + align_class(u.alignment) + "\">Alignment " + align_mark(u.alignment) + " " +
           std::to_string(u.alignment) + verdict + "</div>" + item_icons(u) + "</div></div>";
}

const char* kStrategies[4] = {"Autonomous", "Attack Strongest", "Attack Leader", "Attack Weakest"};

// Prompts follow the device used last (input_map.hpp last_input_device): a
// key or a pad button as its image from ui/prompts, or its name where the
// pack has none.
std::string glyph(const std::string& image, const std::string& text) {
    if (image.empty()) {
        return escape(text);
    }
    return "<img class=\"glyph\" src=\"" + image + "\"/>";
}
std::string key_hint(const char* key) {
    return glyph(key_prompt_image(key), key);
}
std::string pad_hint(int button) {
    return glyph(pad_prompt_image(button), pad_button_name(button));
}

// The command grid of the selection bar. Commands the port cannot run yet are
// drawn greyed with what they wait for.
std::string command_grid(const UnitInfo& first, int first_slot) {
    auto button = [](const char* id, const char* label, const char* key, bool on) {
        return "<div class=\"cmd" + std::string(on ? " hit" : " off") + "\" id=\"" + id + "\"><div class=\"cmdname\">" +
               label + "</div><div class=\"cmdkey\">" + key + "</div></div>";
    };
    const std::string strategy = first.strategy >= 0 && first.strategy < 4 ? kStrategies[first.strategy] : "?";
    // The pad reaches every button through the Y menu (field_select.cpp); a
    // move is A on the target.
    const bool pad = pad_in_use();
    const std::string menu = pad_hint(SDL_CONTROLLER_BUTTON_Y) + " menu";
    return "<div class=\"cmds\">" +
           button("cmd-move", "Move", (pad ? pad_hint(SDL_CONTROLLER_BUTTON_A) + " on target" : key_hint("mouse_right")).c_str(), true) +
           button("cmd-stop", "Stop", (pad ? menu : key_hint("Delete")).c_str(), true) +
           button("cmd-hq", "Return", (pad ? menu : key_hint("H")).c_str(), true) +
           "<div class=\"cmd hit\" id=\"cmd-strategy\"><div class=\"cmdname\">Strategy</div><div class=\"cmdkey\">" +
           strategy + "</div></div>" + button("cmd-patrol", "Patrol", "not yet", false) +
           button("cmd-town", "Enter town", "not yet (R10)", false) +
           button("cmd-item", "Use item", (pad ? menu : key_hint("I") + " list").c_str(), true) +
           (auto_commander::on()
                ? button("cmd-ai", "AI", auto_commander::commands(first_slot) ? "on &#183; take back" : "off &#183; hand back", true)
                : std::string()) +
           "</div>";
}

std::string place_name(const Place& place, int index) {
    if (!place.name.empty()) {
        return place.name;
    }
    return (place.state & 0x10u) ? "Headquarters" : "Location " + std::to_string(index + 1);
}

// The move-to card for a location under the pointer: the selected units in
// arrival order (straight-line distance; the game paths around terrain), each
// with the game's verdict, and a warning when a later unit would capture what
// the first liberates.
std::string move_card(const Place& place, int index, const std::vector<std::pair<int, UnitInfo>>& units) {
    std::string out = "<div class=\"movetitle\">Move to " + escape(place_name(place, index));
    if (place.state & 0x02u) {
        return out + "</div><div class=\"movetext\">Held by the army.</div>";
    }
    if (place.alignment == 0xFF) {
        return out + "</div><div class=\"movetext\">This place cannot be liberated by entering it.</div>";
    }
    std::vector<std::pair<float, int>> order;
    for (size_t i = 0; i < units.size(); i++) {
        const float dx = units[i].second.wx - place.wx, dz = units[i].second.wz - place.wz;
        order.push_back({dx * dx + dz * dz, static_cast<int>(i)});
    }
    std::sort(order.begin(), order.end());
    const Verdict first = order.empty() ? Verdict{} : verdict_for(units[order[0].second].second.alignment, place);
    const char* badge = std::strcmp(first.cls, "good") == 0 ? "&#9650; Will be liberated"
                        : std::strcmp(first.cls, "bad") == 0 ? "&#9660; Will be captured"
                                                              : "&#9670; May be captured";
    const int town = place.alignment & 0x7F;
    out += "<span class=\"badge " + std::string(first.cls) + "\">" + badge + "</span></div>" +
           "<div class=\"movetext\">The first unit to enter decides. The town's alignment is " +
           std::to_string(town) + ((place.alignment & 0x80) ? " (+40, hard to win over)" : "") +
           "; a unit whose average alignment is 31 or more away (plus 0-7 at random) captures it.</div>";
    static const char* const kOrd[] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
    int capturer = -1;
    for (size_t k = 0; k < order.size(); k++) {
        const auto& [slot, u] = units[order[k].second];
        const Verdict v = verdict_for(u.alignment, place);
        if (k > 0 && capturer < 0 && std::strcmp(v.cls, "bad") == 0) {
            capturer = slot;
        }
        out += "<div class=\"moverow\"><span class=\"ord\">" + std::string(k < 8 ? kOrd[k] : "") +
               "</span><span class=\"who\">Unit " + std::to_string(slot + 1) + " &#183; " + escape(u.name) +
               "</span><span class=\"align " + align_class(u.alignment) + "\">" + align_mark(u.alignment) + " " +
               std::to_string(u.alignment) + "</span><span class=\"" + v.cls + "\">" + v.word + "</span></div>";
    }
    if (capturer >= 0 && std::strcmp(first.cls, "bad") != 0) {
        out += "<div class=\"warn\">If the units ahead are held up by a battle, Unit " + std::to_string(capturer + 1) +
               " gets there first and the town is captured.</div>";
    }
    return out;
}

// Projected once per frame after the camera pan (game event 4), so the markers
// sit on the units whatever the camera does.
ogre::events::Subscribe g_project{ogre::events::Event::FieldFrame, "field overlay projection",
                                  [](const ogre::events::Args& e) { project_units(e.rdram, e.ctx); }};

}  // namespace

int location_at(float fx, float fy, float& wx, float& wz) {
    std::lock_guard<std::mutex> lock(g_screen_mutex);
    int best = -1;
    float best_d = TOWN_RADIUS * TOWN_RADIUS;
    for (int i = 0; i < MAX_PLACES; i++) {
        if (!g_places[i].valid) {
            continue;
        }
        const float d = (g_places[i].x - fx) * (g_places[i].x - fx) + (g_places[i].y - fy) * (g_places[i].y - fy);
        if (d <= best_d) {
            best = i;
            best_d = d;
            wx = g_places[i].wx;
            wz = g_places[i].wz;
        }
    }
    return best;
}

std::string hit_step(const std::string& from, int dx, int dy) {
    std::lock_guard<std::mutex> lock(g_screen_mutex);
    const Rect* source = nullptr;
    for (const Rect& r : g_hit_rects) {
        if (r.id == from) {
            source = &r;
        }
    }
    if (source == nullptr) {
        return g_hit_rects.empty() ? std::string() : g_hit_rects.front().id;
    }
    if (dx == 0 && dy == 0) {
        return from;
    }
    // The nearest rectangle whose centre lies in the direction, preferring
    // ones that overlap the source across it (RmlUi's own heuristic, with the
    // centres breaking ties).
    const float scx = (source->x0 + source->x1) * 0.5f, scy = (source->y0 + source->y1) * 0.5f;
    std::string best = from;
    float best_score = 1e30f;
    for (const Rect& r : g_hit_rects) {
        if (r.id == from) {
            continue;
        }
        const float cx = (r.x0 + r.x1) * 0.5f, cy = (r.y0 + r.y1) * 0.5f;
        const float along = dx != 0 ? (cx - scx) * dx : (cy - scy) * dy;
        if (along <= 0.001f) {
            continue;
        }
        const float gap = std::max(0.0f, dx > 0 ? r.x0 - source->x1 : dx < 0 ? source->x0 - r.x1
                                         : dy > 0 ? r.y0 - source->y1 : source->y0 - r.y1);
        const float cross = dx != 0 ? std::max(0.0f, std::max(r.y0 - source->y1, source->y0 - r.y1))
                                    : std::max(0.0f, std::max(r.x0 - source->x1, source->x0 - r.x1));
        const float off = dx != 0 ? std::fabs(cy - scy) : std::fabs(cx - scx);
        const float score = gap + cross * 50.0f + off * 0.1f + along * 0.01f;
        if (score < best_score) {
            best_score = score;
            best = r.id;
        }
    }
    return best;
}

std::string hit_at(float nx, float ny) {
    std::lock_guard<std::mutex> lock(g_screen_mutex);
    // The last match is the topmost: the bar and the card are drawn after the
    // strip.
    std::string id;
    for (const Rect& r : g_hit_rects) {
        if (nx >= r.x0 && nx < r.x1 && ny >= r.y0 && ny < r.y1) {
            id = r.id;
        }
    }
    return id;
}

bool over_strip(float nx, float ny) {
    std::lock_guard<std::mutex> lock(g_screen_mutex);
    return nx >= g_strip_rect.x0 && nx < g_strip_rect.x1 && ny >= g_strip_rect.y0 && ny < g_strip_rect.y1;
}

bool hq_point(float& wx, float& wz) {
    std::lock_guard<std::mutex> lock(g_screen_mutex);
    for (const Place& p : g_places) {
        if (p.valid && (p.state & 0x10u)) {
            wx = p.wx;
            wz = p.wz;
            return true;
        }
    }
    return false;
}

bool screen_of(int slot, float& x, float& y) {
    if (slot < 0 || slot >= SLOTS) {
        return false;
    }
    std::lock_guard<std::mutex> lock(g_screen_mutex);
    x = g_screen[slot].x;
    y = g_screen[slot].y;
    return g_screen[slot].valid;
}

bool init(Rml::Context* context, const std::filesystem::path& asset_dir) {
    g_document = context->LoadDocument((asset_dir / "field.rml").string());
    if (g_document == nullptr) {
        std::fprintf(stderr, "[field] could not load field.rml\n");
        return false;
    }
    g_top = g_document->GetElementById("top");
    g_strip = g_document->GetElementById("strip");
    g_marks = g_document->GetElementById("marks");
    g_help = g_document->GetElementById("padhelp");
    g_bar = g_document->GetElementById("bar");
    return g_top != nullptr && g_strip != nullptr && g_marks != nullptr && g_bar != nullptr && g_help != nullptr;
}

namespace {

void set_inner(Rml::Element* e, std::string& shown, const std::string& rml) {
    if (rml != shown) {
        shown = rml;
        e->SetInnerRML(rml);
    }
}

// The top bar: day and hour (u32 0x800E7D34, hours since the campaign began in
// 16.16), the Chaos Frame (0x801936C9, 0..100), war funds (0x80196A8C) and
// the speed and Organize buttons.
std::string top_bar(const uint8_t* rdram) {
    const bool pad = pad_in_use();
    const uint32_t hours = rd32(rdram, 0x800E7D34u) >> 16;
    const int chaos = std::min<int>(rd8(rdram, 0x801936C9u), 100);
    char clock[48];
    std::snprintf(clock, sizeof(clock), "Day %u &#183; %02u:00", hours / 24 + 1, hours % 24);
    return "<span class=\"clock\">" + std::string(clock) + "</span><span class=\"label\">Chaos Frame</span>" +
           "<div class=\"chaos\"><div class=\"chaosfill\" style=\"width: " + std::to_string(chaos) +
           "%\"></div></div><span class=\"chaosval\">" + std::to_string(chaos) + "</span><span class=\"spacer\"></span>" +
           "<span class=\"goth\">" + std::to_string(rd32(rdram, 0x80196A8Cu)) + " Goth</span>" +
           "<div class=\"topbtn hit\" id=\"cmd-speed\">Speed &#215;" + std::to_string(game_speed()) + "</div>" +
           "<div class=\"topbtn hit\" id=\"cmd-dispatch\">Dispatch &#183; " + std::to_string(field_select::units_out()) +
           "/" + std::to_string(field_select::field_limit()) +
           "</div><div class=\"topbtn hit\" id=\"cmd-organize\">Organize" +
           (pad ? std::string() : " " + key_hint("F3")) + "</div>" +
           "<div class=\"topbtn hit\" id=\"cmd-idle\">Next idle unit " +
           (pad ? pad_hint(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) : key_hint(".")) + "</div>" +
           "<div class=\"topbtn hit\" id=\"cmd-autocmd\">Auto commander " + (auto_commander::on() ? "ON" : "OFF") +
           (pad ? std::string() : " " + key_hint("F4")) + "</div>";
}

}  // namespace

void update() {
    if (g_strip == nullptr) {
        return;
    }
    const uint8_t* rdram = ultramodern::get_rdram_base();
    const bool on_field = rdram != nullptr && rd16(rdram, SCENE) == SCENE_FIELD && field_select::modern();
    if (on_field != g_visible) {
        g_visible = on_field;
        if (on_field) {
            g_document->Show();
        }
        else {
            g_document->Hide();
        }
    }
    if (!on_field) {
        std::lock_guard<std::mutex> lock(g_screen_mutex);
        g_hit_rects.clear();
        return;
    }
    const Rml::Vector2i size = g_document->GetContext()->GetDimensions();
    if (size.x > 0 && size.y > 0) {
        std::vector<Rml::Element*> hits;
        g_document->GetElementsByClassName(hits, "hit");
        std::vector<Rect> rects;
        const Rml::Vector2f strip_at = g_strip->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f strip_size = g_strip->GetBox().GetSize(Rml::BoxArea::Border);
        for (Rml::Element* e : hits) {
            if (!e->IsVisible()) {
                continue;
            }
            Rml::Vector2f at = e->GetAbsoluteOffset(Rml::BoxArea::Border);
            Rml::Vector2f end = at + e->GetBox().GetSize(Rml::BoxArea::Border);
            if (e->GetParentNode() == g_strip) {
                // A tile scrolled out of the strip is not clickable.
                at.y = std::max(at.y, strip_at.y);
                end.y = std::min(end.y, strip_at.y + strip_size.y);
                if (end.y <= at.y) {
                    continue;
                }
            }
            rects.push_back({e->GetId(), at.x / size.x, at.y / size.y, end.x / size.x, end.y / size.y});
        }
        {
            std::lock_guard<std::mutex> lock(g_screen_mutex);
            g_strip_rect = {"strip", strip_at.x / size.x, strip_at.y / size.y, (strip_at.x + strip_size.x) / size.x,
                            (strip_at.y + strip_size.y) / size.y};
        }
        std::lock_guard<std::mutex> lock(g_screen_mutex);
        g_hit_rects.swap(rects);
    }

    const std::vector<int> chosen = field_select::selected();
    auto is_picked = [&](int slot) { return std::find(chosen.begin(), chosen.end(), slot) != chosen.end(); };
    UnitInfo units[PLAYER_UNITS];
    std::string strip;
    for (int slot = 0; slot < PLAYER_UNITS; slot++) {
        units[slot] = read_unit(rdram, slot);
        if (units[slot].out) {
            strip += strip_tile(units[slot], slot, is_picked(slot));
        }
    }
    // Units still in reserve, below a divider, so it is clear who is out.
    std::string waiting;
    for (int slot = 0; slot < PLAYER_UNITS; slot++) {
        if (!units[slot].out && field_select::can_dispatch(slot)) {
            const UnitInfo reserve = read_unit(rdram, slot, false);
            if (reserve.out) {
                waiting += waiting_tile(reserve, slot);
            }
        }
    }
    if (!waiting.empty()) {
        strip += "<div class=\"divider\">IN RESERVE</div>" + waiting;
    }
    set_inner(g_strip, g_strip_shown, strip);
    // The strip stops above the selection bar and scrolls (mouse wheel over it,
    // Page Up / Page Down; field_select.cpp keeps the offset).
    g_strip->SetClass("short", !chosen.empty());
    {
        const float most = std::max(0.0f, g_strip->GetScrollHeight() - g_strip->GetClientHeight());
        const float want = std::clamp(static_cast<float>(field_select::strip_scroll()), 0.0f, most);
        field_select::set_strip_scroll(static_cast<int>(want));
        g_strip->SetScrollTop(want);
    }
    set_inner(g_top, g_top_shown, top_bar(rdram));

    // Markers at the projected positions: the 320x240 picture is aspect-fit and
    // centered in the window, so a framebuffer pixel lands at origin + pixel *
    // scale.
    const float scale = std::min(size.x / 320.0f, size.y / 240.0f);
    const float ox = (size.x - 320.0f * scale) * 0.5f;
    const float oy = (size.y - 240.0f * scale) * 0.5f;
    Screen screen[SLOTS];
    Place places[MAX_PLACES];
    {
        std::lock_guard<std::mutex> lock(g_screen_mutex);
        std::copy(std::begin(g_screen), std::end(g_screen), std::begin(screen));
        std::copy(std::begin(g_places), std::end(g_places), std::begin(places));
    }
    auto px_of = [&](float f) { return static_cast<int>(ox + f * scale); };
    auto py_of = [&](float f) { return static_cast<int>(oy + f * scale); };
    std::string marks;

    // Path lines: dots from each marching unit to its destination (object
    // +0x28..+0x30), bright for the selection.
    for (int slot = 0; slot < PLAYER_UNITS; slot++) {
        if (!screen[slot].valid || !screen[slot].has_dest) {
            continue;
        }
        const float x0 = ox + screen[slot].x * scale, y0 = oy + screen[slot].y * scale;
        const float x1 = ox + screen[slot].dx * scale, y1 = oy + screen[slot].dy * scale;
        const float length = std::hypot(x1 - x0, y1 - y0);
        const int dots = std::min(80, static_cast<int>(length / 12.0f));
        const std::string cls = is_picked(slot) ? "dot lit" : "dot";
        for (int k = 1; k <= dots; k++) {
            const float t = static_cast<float>(k) / static_cast<float>(dots);
            marks += "<div class=\"" + cls + "\" style=\"left: " + std::to_string(static_cast<int>(x0 + (x1 - x0) * t) - 3) +
                     "px; top: " + std::to_string(static_cast<int>(y0 + (y1 - y0) * t) - 3) + "px\"></div>";
        }
    }

    float bx0, by0, bx1, by1;
    if (field_select::drag_box(bx0, by0, bx1, by1)) {
        marks += "<div class=\"box\" style=\"left: " + std::to_string(px_of(bx0)) + "px; top: " +
                 std::to_string(py_of(by0)) + "px; width: " + std::to_string(static_cast<int>((bx1 - bx0) * scale)) +
                 "px; height: " + std::to_string(static_cast<int>((by1 - by0) * scale)) + "px\"></div>";
    }
    // The pad's selection circle (X held), around the game's cursor.
    float cx, cy, cr;
    if (field_select::pad_circle(cx, cy, cr)) {
        const int r = static_cast<int>(cr * scale);
        marks += "<div class=\"ring\" style=\"left: " + std::to_string(px_of(cx) - r) + "px; top: " +
                 std::to_string(py_of(cy) - r) + "px; width: " + std::to_string(2 * r) + "px; height: " +
                 std::to_string(2 * r) + "px; border-radius: " + std::to_string(r) + "px\"></div>";
    }

    // Location markers, and the location under the pointer.
    float pointer_x = -100.0f, pointer_y = -100.0f;
    field_select::pointer(pointer_x, pointer_y);
    int hover = -1;
    float hover_d = TOWN_RADIUS * TOWN_RADIUS;
    for (int i = 0; i < MAX_PLACES; i++) {
        if (!places[i].valid) {
            continue;
        }
        const char* side = (places[i].state & 0x02u) ? "place held" : (places[i].state & 0x04u) ? "place foe" : "place free";
        marks += "<div class=\"" + std::string(side) + "\" style=\"left: " + std::to_string(px_of(places[i].x) - 11) +
                 "px; top: " + std::to_string(py_of(places[i].y) - 11) + "px\"></div>";
        marks += "<div class=\"placename\" style=\"left: " + std::to_string(px_of(places[i].x) - 60) + "px; top: " +
                 std::to_string(py_of(places[i].y) + 13) + "px\">" + escape(place_name(places[i], i)) + "</div>";
        const float d = (places[i].x - pointer_x) * (places[i].x - pointer_x) +
                        (places[i].y - pointer_y) * (places[i].y - pointer_y);
        if (d <= hover_d) {
            hover = i;
            hover_d = d;
        }
    }

    // Unit markers: own units as numbered discs on their colour (selected ones
    // ringed, with a badge), enemies as dark E discs.
    for (int slot = 0; slot < SLOTS; slot++) {
        if (!screen[slot].valid) {
            continue;
        }
        const int x = px_of(screen[slot].x), y = py_of(screen[slot].y);
        if (slot == boss_escorts::BOSS_SLOT) {
            // The mission boss: a B disc, and the extra members it fights with
            // beyond its unit record's five (app/features/boss_escorts.cpp).
            const int extra = boss_escorts::extra_members(rdram, slot);
            marks += "<div class=\"mark enemy boss\" style=\"left: " + std::to_string(x - 16) + "px; top: " +
                     std::to_string(y - 16) + "px\">B</div>";
            marks += "<div class=\"bosstag\" style=\"left: " + std::to_string(x - 50) + "px; top: " +
                     std::to_string(y + 18) + "px\">BOSS" +
                     (extra > 0 ? " +" + std::to_string(extra) + (extra == 1 ? " escort" : " escorts") : std::string()) +
                     "</div>";
            continue;
        }
        if (slot >= PLAYER_UNITS) {
            marks += "<div class=\"mark enemy\" style=\"left: " + std::to_string(x - 16) + "px; top: " +
                     std::to_string(y - 16) + "px\">E</div>";
            // Members beyond the unit record's five (boss_escorts.cpp).
            if (const int extra = boss_escorts::extra_members(rdram, slot); extra > 0) {
                marks += "<div class=\"bosstag\" style=\"left: " + std::to_string(x - 50) + "px; top: " +
                         std::to_string(y + 18) + "px\">+" + std::to_string(extra) + "</div>";
            }
            continue;
        }
        const bool picked = is_picked(slot);
        marks += "<div class=\"mark own" + std::string(picked ? " picked" : "") + "\" style=\"left: " +
                 std::to_string(x - 16) + "px; top: " + std::to_string(y - 16) + "px; background-color: " +
                 unit_colour(slot) + "\">" + std::to_string(slot + 1) + "</div>";
        if (picked) {
            const int order = static_cast<int>(std::find(chosen.begin(), chosen.end(), slot) - chosen.begin()) + 1;
            marks += "<div class=\"pickbadge\" style=\"left: " + std::to_string(x + 9) + "px; top: " +
                     std::to_string(y - 24) + "px\">" + std::to_string(order) + "</div>";
        }
    }

    // What each unit is doing, in words, for its selection card.
    auto place_near = [&](float x, float z) {
        int best = -1;
        float best_d = 1.0f;  // world units squared
        for (int i = 0; i < MAX_PLACES; i++) {
            if (!places[i].valid) {
                continue;
            }
            const float d = (places[i].wx - x) * (places[i].wx - x) + (places[i].wz - z) * (places[i].wz - z);
            if (d < best_d) {
                best = i;
                best_d = d;
            }
        }
        return best;
    };
    for (int slot = 0; slot < PLAYER_UNITS; slot++) {
        UnitInfo& u = units[slot];
        if (!u.out) {
            continue;
        }
        u.chasing = field_select::chase_target(slot) >= 0;
        if (u.chasing) {
            u.activity = "Chasing an enemy unit";
        }
        else if ((u.flags & PURSUING) && u.target >= PLAYER_UNITS && u.target < SLOTS) {
            // Enemy members are in their own table (0x80195580, 0x34 each;
            // func_ovlN_801DA0C8), not read here yet.
            u.activity = "Chasing an enemy unit";
        }
        else if (u.flags & MARCHING) {
            const int to = place_near(u.dest_x, u.dest_z);
            u.activity = to >= 0 ? "Marching to " + place_name(places[to], to) : std::string("Marching");
        }
        else if ((u.flags & IN_LOCATION) && u.location >= 0 && u.location < MAX_PLACES && places[u.location].valid) {
            u.activity = "In " + place_name(places[u.location], u.location);
        }
        else if (u.flags & PURSUING) {
            u.activity = "Holding position";
        }
        else {
            u.activity = "Idle, waiting for orders";
        }
        // The auto commander's job for the unit, ahead of what it is doing.
        if (const std::string job = auto_commander::job_text(slot); !job.empty()) {
            u.activity = "AI: " + job + ". " + u.activity;
        }
    }

    // The move-to card for the location under the pointer.
    std::vector<std::pair<int, UnitInfo>> picked_units;
    for (int slot : chosen) {
        if (slot >= 0 && slot < PLAYER_UNITS && units[slot].out) {
            picked_units.push_back({slot, units[slot]});
        }
    }
    if (hover >= 0 && !picked_units.empty()) {
        const int x = px_of(places[hover].x), y = py_of(places[hover].y);
        const int left = std::min(x + 20, size.x - 470);
        marks += "<div class=\"movecard\" style=\"left: " + std::to_string(left) + "px; top: " +
                 std::to_string(std::max(80, y - 20)) + "px\">" + move_card(places[hover], hover, picked_units) + "</div>";
    }
    // The Dispatch list: the units that can leave the headquarters, each sent
    // out by a click through the game's own placement (field_select.cpp).
    if (field_select::dispatch_open()) {
        marks += "<div class=\"items dispatch\"><div class=\"itemstitle\">Dispatch &#183; " +
                 std::to_string(field_select::units_out()) + " of " +
                 std::to_string(field_select::field_limit()) + " out</div>";
        int shown = 0;
        for (int slot = 0; slot < PLAYER_UNITS; slot++) {
            if (!field_select::can_dispatch(slot)) {
                continue;
            }
            const UnitInfo u = read_unit(rdram, slot, false);
            if (!u.out) {
                continue;
            }
            shown++;
            marks += "<div class=\"itemrow hit\" id=\"d" + std::to_string(slot) + "\"><span class=\"chip\" style=\"background-color: " +
                     unit_colour(slot) + "\">" + std::to_string(slot + 1) + "</span><span class=\"itemname\">" +
                     escape(u.name) + " &#183; " + escape(u.cls) + " Lv " + std::to_string(u.level) +
                     "</span><span class=\"align " + align_class(u.alignment) + "\">" + align_mark(u.alignment) + " " +
                     std::to_string(u.alignment) + "</span></div>";
        }
        if (shown == 0) {
            marks += "<div class=\"itemrow\">No unit is waiting to be dispatched.</div>";
        }
        marks += "<div class=\"itemsnote\">Click a unit to send it to the headquarters. Legions still use the game's "
                 "R menu.</div></div>";
    }
    // Group Items: what the selected units carry (unit record +0x0D..+0x16),
    // pooled per item with who carries it. Using one waits on the game's
    // item-use routine (R11), so the panel lists only.
    if (field_select::items_open() && !picked_units.empty()) {
        marks += "<div class=\"items\"><div class=\"itemstitle\">Group items</div>";
        std::vector<int> kinds;
        for (const auto& [slot, u] : picked_units) {
            for (int i = 0; i < 10; i++) {
                if (u.items[i] != 0 && std::find(kinds.begin(), kinds.end(), u.items[i]) == kinds.end()) {
                    kinds.push_back(u.items[i]);
                }
            }
        }
        int shown = 0;
        for (int item : kinds) {
            int total = 0;
            std::string who;
            for (const auto& [slot, u] : picked_units) {
                const int n = static_cast<int>(std::count(std::begin(u.items), std::end(u.items), static_cast<uint8_t>(item)));
                if (n > 0) {
                    total += n;
                    who += (who.empty() ? "" : " &#183; ") + std::string("Unit ") + std::to_string(slot + 1) + " &#215;" +
                           std::to_string(n);
                }
            }
            shown++;
            marks += "<div class=\"itemrow\"><span class=\"itemnum\">" + std::to_string(shown) +
                     "</span><img class=\"icon\" src=\"icon:" + std::to_string(item) + "\"/><span class=\"itemname\">" +
                     escape(organize_edit::item_name(rdram, item)) + " &#215;" + std::to_string(total) +
                     "</span><span class=\"itemwho\">" + who + "</span></div>";
        }
        if (shown == 0) {
            marks += "<div class=\"itemrow\">The selected units carry no items. Items are given to units in Organize "
                     "(Carry Item).</div>";
        }
        marks += "<div class=\"btn hit\" id=\"cmd-useitem\">Use an item&#8230;</div>"
                 "<div class=\"itemsnote\">Opens the game's item screen, where you pick the unit and the item. "
                 "I closes.</div></div>";
    }
    set_inner(g_marks, g_marks_shown, marks);

    // The selection bar: a count, one card per selected unit (a click selects
    // only that unit, Shift + click removes it) and the command grid.
    std::string bar;
    if (!picked_units.empty()) {
        bar = "<div class=\"count\"><div class=\"countnum\">" + std::to_string(picked_units.size()) +
              "</div><div class=\"countword\">units selected</div><div class=\"countkey\">" +
              (pad_in_use() ? pad_hint(SDL_CONTROLLER_BUTTON_B) : key_hint("Backspace")) + " clears</div></div>";
        const Place* target = hover >= 0 ? &places[hover] : nullptr;
        for (const auto& [slot, u] : picked_units) {
            bar += selection_card(u, slot, target);
        }
        bar += command_grid(picked_units.front().second, picked_units.front().first);
    }
    set_inner(g_bar, g_bar_shown, bar);
    g_bar->SetClass("empty", bar.empty());

    // The pad's controls, while the pad is the device in use: above the bar.
    std::string help;
    if (pad_in_use()) {
        help = "<span>" + pad_hint(SDL_CONTROLLER_BUTTON_A) + (picked_units.empty() ? " select" : " select / order") +
               "</span><span>" + pad_hint(SDL_CONTROLLER_BUTTON_X) + " hold: select an area</span><span>" +
               pad_hint(SDL_CONTROLLER_BUTTON_Y) + (field_select::pad_menu_open() ? " close menu" : " menu, hold: game menu") +
               "</span><span>" + pad_hint(SDL_CONTROLLER_BUTTON_B) + (field_select::pad_menu_open() ? " close" : " clear") +
               "</span><span>" + pad_hint(SDL_CONTROLLER_BUTTON_LEFTSHOULDER) + pad_hint(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) +
               " idle units</span><span>" + glyph(pad_axis_prompt_image(SDL_CONTROLLER_AXIS_RIGHTX), "RS") +
               " camera, " + glyph(pad_axis_prompt_image(SDL_CONTROLLER_AXIS_TRIGGERLEFT), "LT") + " + it: zoom</span>";
    }
    set_inner(g_help, g_help_shown, help);
    g_help->SetClass("raised", !picked_units.empty());
    g_help->SetClass("empty", help.empty());

    // The overlay button the pad's menu has in focus. The element is rebuilt
    // whenever its text changes, so the class is set again every frame.
    static std::string focus_shown;
    const std::string focus = field_select::pad_focus();
    if (focus != focus_shown && !focus_shown.empty()) {
        if (Rml::Element* old = g_document->GetElementById(focus_shown)) {
            old->SetClass("padfocus", false);
        }
    }
    focus_shown = focus;
    if (!focus.empty()) {
        if (Rml::Element* e = g_document->GetElementById(focus)) {
            e->SetClass("padfocus", true);
        }
    }
}

}  // namespace ogre::field_view

