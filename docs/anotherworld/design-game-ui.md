# In-game UI system: native menus for port features

## Goal

Features the port adds to the game itself (the roster view, later a
reorganised menu layout) belong in the game's own menus. They should be
drawn the way OB64 draws its menus, and driven by the same pad input. The Esc
panel (`app/src/overlay.cpp`) stays for PC-port settings only: graphics, audio,
controls, mods and debug (developer, 2026-10-29).

Decisions (developer, 2026-10-29):

| question | answer |
|---|---|
| look | native by default: drawn with the game's own text, window and cursor routines, with room to modernise the style later |
| first target | class changes through the game's own Change Class in the Organize screen, with no separate Promote screen |
| Esc panel | kept for port settings; game features move out of it |
| alignment (developer, 2026-10-29) | removed from every class change: bytes 6 and 7 of the requirement record are always treated as `0xFF` |
| stats | the vanilla requirement stays |
| Honors (developer, 2026-10-29, later the same day) | **Removed.** Class changes are vanilla apart from the alignment rule above. The Honors currency, Renown, the Esc PROMOTE tab and the Honors paid for destroyed enemy units are gone (`app/src/honors.{hpp,cpp}` deleted; the ROSTER tab now reads `app/features/army_roster.cpp`). The rows below that mention Honors describe the earlier plan. |
| alignment, built | `tools/director_hook.py` passes the four alignment loads through `ogre_alignment_requirement` (`app/features/class_rebalance.cpp`), which answers `0xFF`: `func_ovlS_801D2378` (the eligibility check) at `0x801D2580`/`0x801D25AC`, and `func_ovlS_801D6EC0` (the known-classes qualify pass) at `0x801D720C`/`0x801D7238`. `OGRE_CLASS_ALIGNMENT=1` keeps the vanilla test. |
| seen classes | vanilla's rule: the known-classes bitfield `0x80196A96` |
| clarity | the menu says why a class is greyed (stats, equipment, goth, shop, quest) and when Honors can cover it, with the cost |
| gear | the game's own checks stay, including "Basic Equipment for this class is unavailable." The developer: shops unlocked during play sell gear, and that is how class unlocks work when the gear is not in the inventory. The exact rule (inventory only, or inventory plus unlocked shops, and whether the game buys the gear) is read in phase 1 and reused unchanged |
| soldiers | Soldiers still become Fighters or Amazons the vanilla way, after enough wins |

## What the game provides

- **Text.** The text system lives in streamed segment B (`0x8016AF80..0x80191000`),
  which stays resident.
  - `func_8017909C` loads the font cache: two glyph sheets, assets `0x218C0D4`
    and `0x218DACC`.
  - `func_80179404` returns a glyph's advance.
  - `func_801794E8` and `func_80179F24` lay text out and draw it (session 130).
  - The most-called entry is `0x80179054`, with 230 call sites. `0x80179F24` has
    29, `0x801794E8` has 27, and `0x80179404` and `0x801793CC` have 16-17 each.
    Each one's arguments still need to be read.
- **Strings.** Menu labels and help text are LZ-compressed text assets, for
  example the block at ROM `0x237B000` with "Organize", "Training",
  "Hugo Report", "Farewell" and "Battle Strategy". `tools/ogrelz.py --asset`
  decodes them. Message strings for the field (bank N) are plain text at ROM
  `0x143400..`.
- **Organize screen.** Bank unit S. The screen state is the word `0x80196B18`.
  Its character list holds 2 bytes per character at state `+0x1872`, with the
  kind byte at `+0x1873`. Five draw routines read that byte
  (`app/features/class_rebalance.cpp`).
- **Calling game code from the app.** A native hook inserted into generated code
  (`tools/director_hook.py`) can call any recompiled function through
  `LOOKUP_FUNC(vram)` with arguments in `ctx`. `app/features/director_spawn.cpp` does
  this for twelve game functions. Mods can hook bank functions too
  (`mods/item-grant-log`, the bank-hook session).

## Phase 1 findings: vanilla Change Class (Organize, bank unit S)

All addresses are in bank S. The Organize state is the word `0x80196B18`.

| what | where | finding |
|---|---|---|
| candidate list | `func_ovlS_801D2808` | Writes halfwords at state `+0x1C0E` (class id in the low 7 bits, `0x80` = listed but not selectable) and the count at `+0x1BFC`. The current class is state `+0x5E8` (u16; its low byte `+0x5E9` is read as the class). |
| class lines | table `0x801EF2EC`, u16 per class, `0xFFFF`-ended | Bits 8..11 are the class's line (group). The candidates are every class in the current class's line. Line 0 lists only the current class, which is how Soldier (class 1) gets no menu choices. There are special cases for classes `0x26` and `0x2B` with lines 1 and 2. |
| known classes | bitfield `0x80196A96`, masks at `0x801EF390` | A candidate is listed only when its bit is set. `func_ovlS_801D736C` runs when Organize opens and sets the bit for every class a character has, and for every class some character now qualifies for (`func_801D6EC0`). This is vanilla's own "seen classes" rule. |
| eligibility | `func_ovlS_801D2378(char, target)` | Any failure greys the entry, and the reason goes to state `+0x5ED`. The checks are below. |
| line compatibility | `func_ovlS_801D6DAC(current, target)` | Compares two class property masks (`func_801CF340`). Failure code 1. It does not rank tiers. |
| stats and alignment | 8-byte record at `(state+0x80) + target*8` | Bytes 0..5 are the minimum stats against the character's u16 at `+0x1C..+0x26`. Byte 6 is the minimum and byte 7 the maximum alignment (`+0x1B`). `0xFF` means no requirement. |
| basic equipment | 4 functions at `0x801EF2A8` give the target's item per slot | For each item: already equipped (`func_801D2250`), or spare stock in the table `0x80196B20` (`+2` owned, `+3` in use), or for sale (`func_801D27E8` -> `func_8016F9F4`), which counts as a purchase. Otherwise the change fails with code 3, "Basic Equipment for this class is unavailable." |
| purchase | after the item checks | Buying needs the army at a shop (code 5) and enough goth: the total from `func_801D2F88` against the goth word `0x80196A8C` (code 4). |
| special classes | targets `0x22`, `0x23` | Need the flag from `func_8016FF14(3)` and character `+0x36 == 0x63`. Failure code 2. |
| apply | `func_ovlS_801E13B0` | Writes state `+0x5E8` to the character's `+0x11`, and to `+0x12` unless the class key (class `+0x45`) applies. |

**The failure message.** On confirming a greyed class, `func_ovlS_801E13B0`
shows a message with `func_ovlS_801C8010(id, 1)`, picking the id by the code in
state `+0x5ED`:

| code | message id | meaning |
|---|---|---|
| 1 | `0x40037` | line rule |
| 2 | `0x4006B` | special quest |
| 3 | `0x40005` | basic equipment unavailable |
| 4 | `0x4003B` | not enough goth |
| 5 | `0x4007B` | no shop |
| other | `0x40039` | stats |

`func_ovlS_801C8010` calls `func_ovlS_801C7EB0(id, mode, 1)`. That function:
- stores the id at state `+0x10C8`;
- picks the window's place from `mode` (1, 2, or other);
- opens it with the generic window creator
  `func_801CC994(0x100, 0x12, handler, 0x9C, ...)`.

The geometry goes on the stack at `sp+0x10..+0x28`. The handler is `0x801A4414`,
or `0x801A48B4` for message sets `0x1C`..`0x1E` (the id's high half is the
message set, and the low half the message).

`func_801CC994` is the native window to build `game_ui` on. A port message, for
example "Missing gear: 30 Honors (you have 58)", can use an id in a set the game
does not use, with the text supplied by a hook where the handler turns the id
into a string. That conversion is the next thing to read.

**Soldier promotion.** The battle-results bank J copies each participant's class
back to its character record (`func_ovlJ_801DB90C`, `0x801DDC70`: battle object
`+0x48` -> record `+0x11`). The Change Class hooks never touch bank J, so the
Soldier-to-Fighter/Amazon promotion stays vanilla.

**What this means for the design.** Vanilla already offers sideways and backward
moves within a class line, whenever the stats, alignment and gear allow them. It
already hides classes nobody has held or qualified for, and it already buys
missing basic equipment from a shop. Before this was known, the design gave
Honors two jobs:
- paying for sideways and backward moves
- hiding unseen classes

Vanilla already does the first, and does its own version of the second. The
developer decides Honors' role again with this known.

## Design

### Layer 1: `game_ui` (native drawing and input)

`app/src/game_ui.{hpp,cpp}` wraps the game's own routines. Each call becomes a
`LOOKUP_FUNC` call with the arguments the game uses at its own call sites:

- `window(x, y, w, h)`: the game's dialog or menu frame.
- `text(x, y, string, colour)`: through the game's text routine, so the font,
  spacing and palette match.
- `cursor(x, y)`: the game's pointing-hand cursor.
- `list(...)`: a vertical menu built from the three above.

It runs at a hook in the active screen's draw routine, after the game has drawn
its own UI for the frame, so the new UI sits on top in the same display list.

Input: while a `game_ui` panel is open, the hook reads the pad from the same
state the game reads, and that frame's buttons are cleared for the game. The
game then does not act on them, and the panel owns the cursor until it closes.
This is the same idea as `get_input` suppressing game input under the Esc panel,
done per panel.

### Style

`game_ui` draws through one interface with a native implementation (the game's
routines). A modern style is a second implementation of the same calls (window,
text, cursor, list), chosen in the Esc panel's settings. Every screen built on
`game_ui` gets both styles.

### Layer 2: menu integration

The first integration is the game's own **Change Class** in the Organize
screen. These parts stay vanilla: the line, the known-classes bitfield, the line
rule, the stat requirements, the special-quest classes, and Soldiers reaching
Fighter or Amazon after enough wins. The port changes three things:

- **No alignment requirement.** When `func_ovlS_801D2378` reads the 8-byte
  requirement record, bytes 6 and 7 read as `0xFF`. This is done by a hook at
  the two loads, `0x801D2580` and `0x801D25AC`.
- **Had a full set.** When the eligibility check passes for a class, so the
  whole basic-equipment set is equipped, in stock or for sale, the class is
  recorded as "set owned once". The record is per save and stored next to the
  Honors file.
- **Honors for unmet requirements.** An entry greyed for stats, equipment
  (code 3), goth (code 4) or no shop (code 5) is offered for Honors. Stats are
  always lifted by paying. Gear, goth and shop are lifted only when the class
  has "set owned once"; otherwise the entry stays greyed and says why.
  Confirming pays the Honors, and the game's apply routine
  (`func_ovlS_801E13B0`) changes the class without taking or buying gear. The
  character keeps the equipment it has. Code 1 (line rule) and code 2 (special
  quest) stay greyed.

**What the menu shows.** Each greyed entry gets a one-line reason in the menu's
own help area:

- "Needs STR 90"
- "Basic equipment unavailable"
- "Not enough goth"
- "Visit a shop"
- "Quest not completed"

An Honors-eligible entry reads "Missing gear: 30 Honors (you have 58)". The
Honors and Renown totals show in the same area.

The Esc panel's PROMOTE tab is removed once this works.

### Layer 3: overhaul (later)

Once layers 1 and 2 exist, the menu overhaul is data plus handlers: shortcuts,
merged screens, fewer confirmation steps. The developer chooses what changes
first.

## Phases

| phase | content | done when |
|---|---|---|
| 1 | Read the game's UI routines: the text draw entry and its arguments, the window/frame draw, the cursor, and the Organize character menu (its item table, cursor, selection handler, and the routine that draws it each frame) | each routine named with instruction-level evidence in `docs/symbols.md` |
| 2 | `game_ui` layer: window, text, cursor and list, drawn from a hook in the Organize screen's draw routine; input capture | a test panel with three lines and a cursor draws on the Organize screen in the game's style and moves with the D-pad |
| 3 | Change Class: alignment removed; paying Honors lifts stat requirements, and lifts gear for classes whose full set was owned once; quest classes kept; every greyed entry explains itself | an alignment-blocked change now works, a gear-blocked change works for Honors after the set was owned once, a quest class stays locked, and the Esc PROMOTE tab is removed |
| 4 | Move ROSTER the same way, and take the game-feature tabs out of the Esc panel | Esc shows only port settings |
| 5 | Menu overhaul, item by item, as the developer chooses | |

## Open questions

- Which of the text entries draws a string at a position, and which only lays
  one out? Phase 1 reads their call sites.
- Does the Organize screen draw through one per-frame routine, which would give
  a single hook point, or one per panel?
- How does the game separate menu input from field input? This decides how
  `game_ui` captures the pad.
- Where is Change Class's candidate list built? Where does the game check
  stats, alignment and basic equipment, and where does the Soldier promotion
  count its wins?
- How does the basic-equipment check use the shops? Which shop lists count
  (unlocked shops only, or the town the army is in), and does the class change
  buy the gear and charge goth?

