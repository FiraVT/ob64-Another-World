# Dialogue auto-play

AUTO-PLAY DIALOGUE (Esc menu, GAMEPLAY tab, PACE section; key F2; OFF by
default) presses A for the game's message window once the window's page is fully
shown and a read delay has passed. It stops at a choice. Code:
`app/features/dialogue_autoplay.cpp`; hook: `config/hooks.toml` "dialogue auto-play
(8017F578)".

Status: built, not played. Coverage of the scene-`0x0D` cutscene dialogue is
not verified (see "Open").

## The message window system

All addresses are revision 1 (USA Rev A), read in `build/ogrebattle64.elf`. The
code is resident (streamedB).

| address | what | evidence |
|---|---|---|
| `0x8018FDE0` | window manager root (pointer) | `func_8017F4B0` loads it (`0x8017F4E4`) and walks `root+0x04` (first window), `win+0x04` (next) |
| `root+0x04` | first window in the list | same |
| `root+0xD2` | u8 input lock; non-zero = the handler returns at once | `0x8017F5B0` |
| `root+0xD4` | the handler's result (`-1` none, `-2` closed by A/B, else the picked row) | `0x8017FAFC` |
| `0x80072674` | `frame_update` calls `func_8017F4B0` every frame, after the scene update (`0x8007265C`) and the task queue (`0x8007266C`) | disassembly |
| `func_8017F4B0` | picks the focused window (the last in the list, walking back past windows with `+0x14` bit `0x10`) and calls `func_8017F574(window)` | `0x8017F4E0`-`0x8017F560` |
| `func_8017F574` | the message-window input handler; state byte `text+0x5F0`, jump table `0x80190E50` | see below |
| `func_80180814(window, id)` | sets a window's text: `malloc(0x604)` into `win+0xD0`, splits the text into lines, sets the fields below | `0x80180840`-`0x80180B18` |
| `func_8017C5A4` | draws the text, line `top+i` for `i < page_lines` | `0x8017C6B8`-`0x8017C784` |
| `func_801814B8` / `func_801814CC` | line `n`'s pointer (`text+0x530+4n`) / copy of its first `2*k` bytes for the typed draw | disassembly |

Window fields: `+0x14` u8 flags (bit `0x04` = typed text), `+0x16` u16 kind,
`+0x22` cursor row, `+0x24` cancel row, `+0x25` row count, `+0x28` u16 (with kind
`0x100`/`0x200`/`0x1000`, non-zero means A and B do not close the window),
`+0xD0` text state.

Text state (`malloc(0x604)`):

| offset | field |
|---|---|
| `+0x000` | the text, 0x500 bytes |
| `+0x530` | `u32[48]` line pointers |
| `+0x5F0` | handler state |
| `+0x5F1` | top line of the page |
| `+0x5F3` | units per typed line (128) |
| `+0x5F4` | lines per page (3; 2 for text type 11) |
| `+0x5F5` | line count |
| `+0x5F7` | choice flag |
| `+0x5FC` | typed line (0 .. page lines) |
| `+0x5FD` | typed units of that line (a unit is 2 bytes) |
| `+0x5FE` | frames per typed unit (2) |
| `+0x600` | the text id |

Line markers in the text (`0x80180A14`-`0x80180AE8`): `@n` and the two-byte
`0x8197` end a line; `@\` and the two-byte `0x818F` end a line and set the choice
flag `+0x5F7 = 1` (and the cursor `+0x22 = 0`).

## Wait for A, and how a choice differs

`func_8017F574`, state 0 (`0x8017F6AC`), reads the newly-pressed word
`0x800F8100` (controller 1, computed from `cur & ~prev` by `func_8007297C` at
`0x80072A54`):

1. With typed text, advance the typing counters (`0x8017F6B0`-`0x8017F740`).
2. R (`0x800C4B28 & 0x10`) with `top > 0` goes to state 1, the scroll-back.
3. While `top + page_lines < line_count`: A turns the page (`top += page_lines`,
   typed line = 0, sound 3; `0x8017F788`-`0x8017F7C0`). The game does not wait for
   the typing to finish.
4. On the last page: if `+0x5F7` is set, go to state 5 (`0x8017F7D0`). State 5
   builds the menu (`func_8017F134`) and goes to state 6, the choice menu, where A
   picks the cursor row and B the cancel row (`0x8017F988`-`0x8017FA34`).
5. Otherwise A or B (`0xC000`) closes the window: sound 25, state 3, result `-2`
   (`0x8017F7D8`-`0x8017F860`), unless the kind/`+0x28` rule above keeps it open.

A plain line is state 0 with `+0x5F7 == 0`. A choice is a window whose text
carried the choice marker (`+0x5F7 == 1`), and it reaches state 5/6 instead of
closing. When the focused window's kind has bit `0x0004`, the handler does not
work on that window: it walks the list and uses the last window whose kind has
high byte `0x01` (`0x8017F5BC`-`0x8017F664`); state, choice flag and typing are
read from that window's text state.

## What auto-play does

`ogre_dialogue_autoplay_tick(window)` runs at `func_8017F574`'s entry
(`0x8017F578`, `$a0` = the window) every frame. It presses A when all hold:

- the root is valid, the list is not empty, and `root+0xD2 == 0`;
- if the window's kind has bit `0x0004`, the checks below use the last window
  whose kind has high byte `0x01`, as the handler does (none: no press);
- the state is 0 and `+0x5F7 == 0` (no choice anywhere in the window's text);
- with typed text, the last line on the page is shown in full
  (`typed line == shown lines - 1` and `2 * typed units >= its byte length`, or
  `typed line >= shown lines`);
- the page has been fully shown for `OGRE_AUTOPLAY_BASE_MS` (1000) +
  `OGRE_AUTOPLAY_CHAR_MS` (50) x the page's character count, clamped to
  0.3..15 s. Time while the game thread is held (the Esc menu) does not count.

The press is OR'd into controller 1 by `get_input`
(`ogre::dialogue_autoplay::injected_buttons`), so the game's own pad code makes
the edge, and every reader of the pad words sees one ordinary A. It is released
when `0x800F8100` shows A at the next tick, or after 200 ms. One press per page:
a page that ignores A (a script-held window) stays until the player acts.

`OGRE_AUTOPLAY=1`/`0` forces the feature for a run. `OGRE_AUTOPLAY_LOG=1` prints
one `[autoplay]` line per change of the focused window's verdict (`hold` with a
reason, `typing`, `ready` with the delay) and one per press.

## Open

- **Scene-`0x0D` cutscene dialogue.** The step engine's interpreter
  `func_ovlC_802282D8` has its own button opcode (`0x802294F4`: `func_ovlC_8022A548`
  tests `0x800C4B28` against a mask from the descriptor) and an A test that sets
  `0x8022A9B0` (`0x80229D18`). Bank C never calls the text setter's wrappers
  (`func_8017EE84`, `func_8017EF44`, `func_8017F030`, `func_8017F234`); their
  callers are units H, I, J, M, N, P, Q, V, Y, Z, AA and AC and the main unit. The
  window draw `func_80180BFC` runs in every scene from `frame_update`
  (`0x80072584`), so a drawn window does not prove which engine made it. A run
  with `OGRE_AUTOPLAY_LOG=1` through the cathedral shows whether its lines are
  message windows: `[autoplay] window ...` lines appear if they are.
- The read delay constants are a first guess.
- Revision 0 gets the same hook at `0x8017F558`; it is not built by `build-dist`.

