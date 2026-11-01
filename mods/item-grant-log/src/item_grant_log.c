// Item Grant Log - an example mod for Ogre Battle 64: Recomp.
//
// It is the reference for a hook on streamed-overlay bank code. The hooked
// function `func_ovlN_801DC740` is the item grant routine: bank unit N, record 7
// (`bankRec7`, ROM 0x101D00 -> RAM 0x801AD5C0, the mission scene 0x03's
// module). Every item the player receives in a mission passes through it once.
//
// The routine takes one argument, the item word in `$a0`. Bit 15 selects the
// list and the low 15 bits are the id (`history-archive:docs/history/DECISIONS.md`, session 122):
//
//   bit 15 set    the 278-slot equipment list at 0x80196B20
//   bit 15 clear  the 40-slot consumable list at 0x80193AE0
//
// The entry hook runs before the routine with the same arguments, and it only
// writes one `[mod] item grant ...` line to the port's output. It does not
// change what the game does.
//
// The hook resolves because `make mod-syms` merges every bank unit's sections
// into `mods/reference/dump.toml` and the app registers the bank records with
// the runtime's hook path (`docs/guides/app-build.md` -> "Hooking bank code").

#include "modding.h"

RECOMP_IMPORT("*", void recomp_log(const char* message));

#define ITEM_LIST_FLAG 0x8000u
#define ITEM_ID_MASK 0x7FFFu

static char s_log_buffer[64];
static unsigned int s_log_offset;

static void log_append_text(const char* text) {
    while (*text != '\0' && s_log_offset < sizeof(s_log_buffer) - 1u) {
        s_log_buffer[s_log_offset] = *text;
        s_log_offset++;
        text++;
    }
    s_log_buffer[s_log_offset] = '\0';
}

static void log_append_u32(unsigned int value) {
    char digits[12];
    unsigned int count = 0u;

    do {
        digits[count] = (char)('0' + (value % 10u));
        count++;
        value /= 10u;
    } while (value != 0u && count < sizeof(digits));
    while (count != 0u && s_log_offset < sizeof(s_log_buffer) - 1u) {
        count--;
        s_log_buffer[s_log_offset++] = digits[count];
    }
    s_log_buffer[s_log_offset] = '\0';
}

RECOMP_HOOK("func_ovlN_801DC740")
void log_item_grant(unsigned int item) {
    s_log_offset = 0u;
    log_append_text("item grant ");
    log_append_text((item & ITEM_LIST_FLAG) != 0u ? "equipment " : "consumable ");
    log_append_u32(item & ITEM_ID_MASK);
    recomp_log(s_log_buffer);
}

