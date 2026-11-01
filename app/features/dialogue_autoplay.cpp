// AUTO-PLAY DIALOGUE: a message window that waits for A advances by itself.
//
// The game's message windows are one system in the resident code (streamedB).
// Every frame, frame_update calls func_8017F4B0 (0x80072674), which picks the
// focused window from the list at *(0x8018FDE0) + 0x04 and runs its input
// handler func_8017F574(window). config/hooks.toml calls
// ogre_dialogue_autoplay_tick(window) at that handler's entry (0x8017F578), so
// the app sees the same window, once per frame, before the game reads A.
//
// The handler's state byte is text+0x5F0 (jump table 0x80190E50):
//   0  page shown. A (0x800F8100 & 0x8000) turns the page while
//      top + page_lines < line_count (0x8017F788); on the last page a window
//      with text+0x5F7 set goes to state 5 (0x8017F7D0), any other closes on A
//      or B (0x8017F7E0, state 3, result -2).
//   1  R held: scrolling back through earlier lines (0x8017F880).
//   5  builds the choice menu (func_8017F134), then 6.
//   6  the choice menu: A picks the cursor row, B the cancel row (0x8017F988).
// text+0x5F7 is the choice flag. The text setter func_80180814 sets it when the
// text holds the choice marker "@\" or its two-byte form 0x818F (0x80180AD8);
// "@n" and 0x8197 are line breaks. When the focused window's kind (+0x16) has
// bit 0x0004, the handler works on the last window in the list whose kind has
// high byte 0x01 instead (0x8017F5BC-0x8017F664); auto-play follows it the same
// way, so a dialogue line shown beside such a window still advances.
//
// With +0x14 bit 0x04 set the text is typed out: text+0x5FC is the line being
// typed (0 .. page_lines) and text+0x5FD how many two-byte units of it are shown
// (func_801814CC copies 2 * 5FD bytes of the line for the draw). The game turns
// the page on A without waiting for the typing, so auto-play waits until the last
// line on the page is shown in full.
//
// Auto-play presses A when the window is in state 0, has no choice flag, the
// page is fully shown, the input lock root+0xD2 is clear, and the read delay
// (OGRE_AUTOPLAY_BASE_MS + OGRE_AUTOPLAY_CHAR_MS per character on the page) has
// passed since the page became fully shown. The press goes through get_input
// (injected_buttons), so the game's own pad code makes the edge and every reader
// of the pad words sees one ordinary A press. It presses once per page; a page
// that ignores A stays until the player acts.
//
// OGRE_AUTOPLAY=1/0 forces the feature for one run. OGRE_AUTOPLAY_LOG=1 prints a
// line whenever the focused window's verdict changes.

#include "dialogue_autoplay.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "options.hpp"
#include "ultramodern/ultramodern.hpp"

namespace ogre::dialogue_autoplay {
namespace {

constexpr uint32_t kWindowRoot = 0x8018FDE0;  // window manager (pointer)
constexpr uint32_t kPadPressed = 0x800F8100;  // controller 1, newly pressed (u16)
constexpr uint16_t kButtonA = 0x8000;

// Window fields.
constexpr uint32_t kWinFlags = 0x14;  // u8; bit 0x04 = typed text
constexpr uint32_t kWinKind = 0x16;   // u16; bit 0x0004 = handler uses the 0x01xx window
constexpr uint32_t kWinNext = 0x04;   // next window in the list
constexpr uint32_t kWinText = 0xD0;   // pointer to the text state
// Root fields.
constexpr uint32_t kRootFirst = 0x04;  // first window
constexpr uint32_t kRootLock = 0xD2;   // u8; non-zero = windows ignore the pad
// Text state fields (malloc(0x604) by func_80180814).
constexpr uint32_t kTextLines = 0x530;  // u32[48] line pointers
constexpr int kMaxLines = 48;
constexpr uint32_t kTextState = 0x5F0;
constexpr uint32_t kTextTop = 0x5F1;
constexpr uint32_t kTextPage = 0x5F4;
constexpr uint32_t kTextCount = 0x5F5;
constexpr uint32_t kTextChoice = 0x5F7;
constexpr uint32_t kTextTypedLine = 0x5FC;
constexpr uint32_t kTextTypedUnits = 0x5FD;

// The press is held until the game's pressed word shows it, or this long.
constexpr int64_t kPressHoldMs = 200;
// A gap between two ticks longer than this is a pause (the Esc menu holds the
// game thread), and does not count toward the read delay.
constexpr int64_t kPauseGapMs = 250;

std::atomic<bool> g_pressing{false};
std::atomic<int64_t> g_press_until_ms{0};

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

long env_long(const char* name, long fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }
    return std::strtol(value, nullptr, 0);
}

int forced() {
    static const int value = [] {
        const char* env = std::getenv("OGRE_AUTOPLAY");
        if (env == nullptr || env[0] == '\0') {
            return -1;
        }
        return env[0] == '0' ? 0 : 1;
    }();
    return value;
}

bool log_on() {
    static const bool value = env_long("OGRE_AUTOPLAY_LOG", 0) != 0;
    return value;
}

bool valid(uint32_t address) {
    return address >= 0x80000000u && address < 0x80800000u;
}

uint8_t rd8(const uint8_t* rdram, uint32_t address) {
    return rdram[(address & 0x1FFFFFFFu) ^ 3u];
}

uint16_t rd16(const uint8_t* rdram, uint32_t address) {
    return static_cast<uint16_t>((rd8(rdram, address) << 8) | rd8(rdram, address + 1));
}

uint32_t rd32(const uint8_t* rdram, uint32_t address) {
    return (static_cast<uint32_t>(rd16(rdram, address)) << 16) | rd16(rdram, address + 2);
}

enum class Verdict { None, Hold, Typing, Ready };

const char* verdict_name(Verdict verdict) {
    switch (verdict) {
        case Verdict::Hold:   return "hold";
        case Verdict::Typing: return "typing";
        case Verdict::Ready:  return "ready";
        default:              return "none";
    }
}

struct Page {
    Verdict verdict = Verdict::None;
    const char* why = "";
    uint64_t key = 0;
    int chars = 0;
    uint32_t window = 0;  // the window the handler works on
    uint32_t text = 0;
    int state = 0;
    int top = 0;
    int count = 0;
};

uint64_t mix(uint64_t hash, uint32_t value) {
    for (int i = 0; i < 4; i++) {
        hash ^= (value >> (i * 8)) & 0xFFu;
        hash *= 0x100000001B3ull;
    }
    return hash;
}

// The byte length of a line, and its character count (a two-byte character
// counts once). Hashes the bytes into `hash`.
int line_length(const uint8_t* rdram, uint32_t line, int& chars, uint64_t& hash) {
    if (!valid(line)) {
        return 0;
    }
    int at = 0;
    while (at < 256) {
        const uint8_t b = rd8(rdram, line + at);
        if (b == 0) {
            break;
        }
        hash = mix(hash, b);
        if ((b >= 0x81 && b < 0xA0) || (b >= 0xE0 && b < 0xFD)) {
            at += 2;
            chars++;
        }
        else {
            at += 1;
            if (b >= 0x20) {
                chars++;
            }
        }
    }
    return at;
}

Page evaluate(const uint8_t* rdram, uint32_t window) {
    Page page;
    const uint32_t root = rd32(rdram, kWindowRoot);
    if (!valid(window) || !valid(root) || rd32(rdram, root + kRootFirst) == 0) {
        return page;
    }
    if ((rd16(rdram, window + kWinKind) & 0x0004) != 0) {
        // The handler's redirect (0x8017F5CC): the last window whose kind has
        // high byte 0x01.
        uint32_t paired = 0;
        uint32_t at = rd32(rdram, root + kRootFirst);
        for (int guard = 0; valid(at) && guard < 64; guard++) {
            if ((rd16(rdram, at + kWinKind) & 0xFF00) == 0x0100) {
                paired = at;
            }
            at = rd32(rdram, at + kWinNext);
        }
        if (paired == 0) {
            return page;
        }
        window = paired;
    }
    page.window = window;
    const uint32_t text = rd32(rdram, window + kWinText);
    if (!valid(text)) {
        return page;
    }
    page.text = text;
    page.state = rd8(rdram, text + kTextState);
    page.top = rd8(rdram, text + kTextTop);
    page.count = rd8(rdram, text + kTextCount);
    page.key = mix(mix(mix(0xCBF29CE484222325ull, window), text),
                   static_cast<uint32_t>(page.top << 8 | page.count));
    page.verdict = Verdict::Hold;
    if (rd8(rdram, root + kRootLock) != 0) {
        page.why = "windows locked";
        return page;
    }
    if (page.state != 0) {
        page.why = page.state >= 5 ? "choice menu" : "not waiting";
        return page;
    }
    if (rd8(rdram, text + kTextChoice) != 0) {
        page.why = "choice";
        return page;
    }
    const int page_lines = rd8(rdram, text + kTextPage);
    const int shown = std::clamp(std::min(page_lines, page.count - page.top), 0, kMaxLines);
    int last_length = 0;
    for (int i = 0; i < shown && page.top + i < kMaxLines; i++) {
        const uint32_t line = rd32(rdram, text + kTextLines + 4u * static_cast<uint32_t>(page.top + i));
        last_length = line_length(rdram, line, page.chars, page.key);
    }
    if ((rd8(rdram, window + kWinFlags) & 0x04) != 0 && shown > 0) {
        const int typed_line = rd8(rdram, text + kTextTypedLine);
        const int typed_units = rd8(rdram, text + kTextTypedUnits);
        if (typed_line < shown - 1 || (typed_line == shown - 1 && 2 * typed_units < last_length)) {
            page.verdict = Verdict::Typing;
            page.why = "typing";
            return page;
        }
    }
    page.verdict = Verdict::Ready;
    page.why = "shown";
    return page;
}

struct Tracker {
    Verdict verdict = Verdict::None;
    uint64_t key = 0;
    bool fired = false;
    int64_t ready_since = 0;
    int64_t delay = 0;
    int64_t last_tick = 0;
};

Tracker g_track;

int64_t read_delay_ms(int chars) {
    static const long base = env_long("OGRE_AUTOPLAY_BASE_MS", 1000);
    static const long per_char = env_long("OGRE_AUTOPLAY_CHAR_MS", 50);
    return std::clamp<int64_t>(base + per_char * chars, 300, 15000);
}

}  // namespace

bool enabled() {
    const int force = forced();
    return force >= 0 ? force != 0 : choice_on(Choice::DialogueAutoPlay);
}

bool toggle() {
    set_choice(Choice::DialogueAutoPlay, choice_on(Choice::DialogueAutoPlay) ? 0 : 1);
    if (forced() >= 0) {
        std::fprintf(stderr, "[autoplay] OGRE_AUTOPLAY=%d overrides the setting for this run\n", forced());
    }
    return enabled();
}

uint16_t injected_buttons() {
    if (!g_pressing.load(std::memory_order_relaxed)) {
        return 0;
    }
    if (now_ms() >= g_press_until_ms.load(std::memory_order_relaxed)) {
        g_pressing.store(false, std::memory_order_relaxed);
        return 0;
    }
    return kButtonA;
}

}  // namespace ogre::dialogue_autoplay

// config/hooks.toml: the entry of the message-window input handler
// func_8017F574 (0x8017F578), with the focused window in $a0.
extern "C" void ogre_dialogue_autoplay_tick(uint8_t* /*generated_rdram*/, uint32_t window) {
    using namespace ogre::dialogue_autoplay;
    const uint8_t* rdram = ultramodern::get_rdram_base();
    const int64_t now = now_ms();

    // The press has landed once the game's pressed word shows A; release it so
    // the next press is a new edge.
    if (g_pressing.load(std::memory_order_relaxed) &&
        ((rd16(rdram, kPadPressed) & kButtonA) != 0 ||
         now >= g_press_until_ms.load(std::memory_order_relaxed))) {
        g_pressing.store(false, std::memory_order_relaxed);
    }
    if (!enabled()) {
        g_track = Tracker{};
        return;
    }

    if (g_track.last_tick != 0 && now - g_track.last_tick > kPauseGapMs) {
        g_track.ready_since += now - g_track.last_tick;
    }
    g_track.last_tick = now;

    const Page page = evaluate(rdram, window);
    if (page.verdict != g_track.verdict || page.key != g_track.key) {
        if (log_on() && page.verdict != Verdict::None) {
            std::fprintf(stderr,
                         "[autoplay] window 0x%08X text 0x%08X state %d lines %d-%d of %d: %s (%s)",
                         page.window, page.text, page.state, page.top,
                         page.top + rd8(rdram, page.text + kTextPage) - 1, page.count,
                         verdict_name(page.verdict), page.why);
        }
        g_track.verdict = page.verdict;
        g_track.key = page.key;
        g_track.fired = false;
        g_track.ready_since = now;
        g_track.delay = read_delay_ms(page.chars);
        if (log_on() && page.verdict != Verdict::None) {
            if (page.verdict == Verdict::Ready) {
                std::fprintf(stderr, ", %d characters, press in %lld ms", page.chars,
                             static_cast<long long>(g_track.delay));
            }
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
        }
    }
    if (page.verdict != Verdict::Ready || g_track.fired ||
        g_pressing.load(std::memory_order_relaxed) || now - g_track.ready_since < g_track.delay) {
        return;
    }
    g_track.fired = true;
    g_press_until_ms.store(now + kPressHoldMs, std::memory_order_relaxed);
    g_pressing.store(true, std::memory_order_relaxed);
    if (log_on()) {
        std::fprintf(stderr, "[autoplay] press A (window 0x%08X, lines %d of %d)\n", page.window,
                     page.top, page.count);
        std::fflush(stderr);
    }
}

