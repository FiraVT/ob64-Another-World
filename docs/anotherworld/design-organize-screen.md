# Organize screen: the current menus and a streamlined layout

Companion to `docs/anotherworld/design-game-ui.md`. The layout references are the
developer's Unicorn Overlord screenshots (2026-10-29): Unit Formation, Unit
Details, Character List, Armorer, Provisioner and Items.

## 1. Every Organize menu today

Source: the game's own Auto-Help text, in the LZ text block at ROM `0x2376556`,
plus the prompts and messages in the blocks at ROM `0x237B2EC` and `0x237BFC4`.
Decode them with `python tools/ogrelz.py assets/ogre64.z64 2370000 20000`. The
help lines appear in the same order as the menu items. The command bar draws
icons, so no plain-text label table was found.

### Organize (from the headquarters menu: "Organizes the battalion")

| top level | help text | submenu |
|---|---|---|
| **Character** | "Once confirmed, various commands for characters will be displayed." | Equip, Change Class, Discharge, Basic Equipment, Rename (below) |
| **Unit** | "Forms a new unit." / "Once confirmed, various commands for units will be displayed." | New Unit, Add, Remove, Formation, Change Leader, Battle Strategy, Carry Item (below) |
| **Legion** | "Forms a new Legion." / "Once confirmed, various commands for legions will be displayed." | New Legion, Add Unit, Remove Unit, Legion Formation, Division Placement (below) |
| **Item** | "Once confirmed, the item list and various commands for items will be displayed." | Use, Unequip All, Buy Basic Equipment, List, Sort, Sell (below) |
| **Class** | "Once confirmed, various commands for classes will be displayed." | Class List (below) |
| **Replenish** | "Replenishes soldiers from reserves. You cannot add more than available in Soldier Reserves." | none |
| **Exchange** | "Exchanges characters between units." | pick two units, then the characters |
| **Sort** | "Sorts characters and units. New numbers can be assigned, if desired." | none |
| **Exit** | "Ends Organization and returns to the previous screen." | confirm |

### Unit submenu

| command | help text |
|---|---|
| New Unit | "Forms a new unit." The unit is registered when a leader is chosen ("A new unit led by ... has been registered."). |
| Add | "Adds a character to the unit, or exchanges characters." |
| Remove | "Removes character from unit." |
| Formation | "Changes the formation of characters in a unit." |
| Change Leader | "Changes the unit leader." |
| Battle Strategy | "Assigns the unit's Battle Strategy." The options are Autonomous, Attack, Strongest, Leader and Weak. |
| Carry Item | "Equips an item to a unit. Units can carry expendable and portable items." |

### Legion submenu

| command | help text |
|---|---|
| New Legion | "Forms a new Legion." A Legion Core unit must have a leader qualified as legion leader, and there can be up to six legions. |
| Add Unit | "Adds a unit to the legion, or exchanges units." |
| Remove Unit | "Removes units from the Legion." |
| Legion Formation | "Changes the formation of a legion." There are 8 formations: horizontal, right and left diagonal, vertical, inverse-V, inverse-W, V and W. |
| Division Placement | "Changes the placement of units in a legion." Divisions must be adjacent to the Legion Core. |

### Character submenu

| command | help text |
|---|---|
| Equip | "Equips an item for a character." |
| Change Class | "Changes class of an eligible character." See `design-game-ui.md` for its checks. |
| Discharge | "Discharges a character permanently." Named characters answer with their own lines (ROM `0x237C332`). |
| Basic Equipment | "Reduces a character's equipment to Basic Equipment." |
| Rename | "Changes character's name." Named characters refuse with their own lines (ROM `0x237D2BE`). |
| View Status | "Displays the status of ..." The per-field help is the block at ROM `0x23778B2`. |

### Item submenu

| command | help text |
|---|---|
| Use | "Uses expendable item." Not on a dispatched target. |
| Unequip All | "Unequips specific item from all characters and units." |
| Buy Basic Equipment | "Purchases Basic Equipment of a class." Pick the class, then the quantity. |
| List | "Lists all items and equipment. List can be sorted." |
| Sort | "Sorts items according to criteria." By stat modifier, quantity, cost, name or number, ascending or descending. |
| Sell | "Sells item for 1/4 of its purchase price." |

### Class submenu

| command | help text |
|---|---|
| Class List | "Lists classes that are available to your ... You can check which characters are eligible for class change. Change Class can also be done here." Pick a class, and the qualified characters are highlighted. |

Count: 9 top-level entries, 29 commands, and most commands need a separate
selection step after them ("Select the unit to ...", "Select the character to
...").

## 2. How OB64 differs from the references

| | Unicorn Overlord | OB64 |
|---|---|---|
| expendable items | one shared inventory, used from anywhere | **carried by a unit** ("Units can carry expendable and portable items") and used by that unit on the field. In Organize, Use only works on units that are not dispatched. |
| equipment | shared pool | a shared stock ("depot"), with counts owned and in use. Basic Equipment is tied to the class and bought per class. |
| classes | promotion by item | Change Class within a class line, gated by stats, alignment and gear (`design-game-ui.md`) |
| grouping | units | units, plus **legions**: a Legion Core and its Divisions, with 8 formations |
| soldiers | none | generic Soldiers with reserves (Replenish) |
| buying | shop screens in towns | Organize only buys Basic Equipment for a class; other gear is bought in town shops on the field |

The new layout has to keep unit-carried items, legions, soldier reserves and
the class lines. It can take the references' presentation: all units visible at
once, context actions on the selected thing, tabbed character details, and
stat deltas shown across characters while picking gear.

## 3. Streamlined layout

Five screens replace 9 top-level entries and 29 commands. Each one works by
selecting a thing and then acting on it, instead of picking a command and then
selecting the thing.

### A. Formation (home screen; reference: Unit Formation)

- Every unit appears as its formation board, in a grid with paging. Legions show
  their Core and Divisions grouped together with a legion banner.
- Selecting a board shows its **context actions** next to it:
  - Set Character (add or exchange)
  - Details
  - Reposition
  - Remove
  - Make Leader
  - Strategy
  - Carry Item
  - Replenish

  These replace the Unit submenu, Exchange and Replenish.
- Selecting a legion banner shows Legion actions: Add Unit, Remove Unit,
  Formation (with the 8 diagrams), Placement. This replaces the Legion submenu.
- The bottom panel shows the selected unit: number, name, members, type,
  movement, leader effect and Battle Strategy, plus a card per member with
  class, level and an equipment strip. This is the Unit Information panel from
  the reference.
- Empty slots offer "New Unit". An unassigned-characters tray along the side
  allows drag-style placement.

### B. Character (reference: Character List and Unit Details)

- The left side is a sortable character grid. It replaces Sort for characters,
  and shows unit number, level and EXP to next level.
- The right side has tabs:
  - **Loadout:** equipment slots. Picking a slot opens the item list with stat
    changes. Replaces Equip and Basic Equipment.
  - **Class:** the class line with each class's status: available, the Honors
    cost, or why it is locked. Replaces Change Class and the Class submenu.
  - **Details:** biography, alignment, element and other status fields, with the
    game's own help text. Replaces View Status.
  - **Stats:** stat bars and growth.
- Actions: Rename, Discharge, both with confirmation and the game's character
  lines.

### C. Items (reference: Items)

- Category tabs along the top: all, weapons by type, armour, accessories,
  expendables, treasure.
- The list shows held count, stock, and **who has it**: equipped by a character,
  or carried by a unit.
- Actions: Equip To (opens the character grid with stat changes, like the
  reference's Items/Equip view), Give To Unit (carry), Use (on a unit that is
  not dispatched), Unequip All, Sell.

### D. Armory (reference: Armorer)

- Buys Basic Equipment for the class. The character grid on the left highlights
  who would use it, and the item list with "To Buy" counts and the goth total
  sits on the right. Replaces Buy Basic Equipment.

### E. System

- Sort Units (renumber), Exit. Settings stay where the game has them, and port
  settings stay in the Esc panel.

### Change Class inside Character > Class

As in `design-game-ui.md`:
- no alignment requirement
- Honors lifts stat requirements, and lifts gear requirements once the class's
  full set has been owned
- quest classes stay locked
- every locked class says why

## 4. Style and engine

The native OB64 style does not fit this layout (developer). The screens need
full-resolution text, panels, portraits and many small elements, which the
game's 320x240 menu routines cannot draw.

**Recommended engine: RmlUi through RT64.** Zelda64Recomp and the other
N64Recomp ports draw their menus with RmlUi, an HTML/CSS-like UI library
(`recompui` in RecompFrontend), rendered by RT64 over the game at the window's
resolution. This repo has no RmlUi or RecompFrontend yet; they would be added as
a dependency.

Each screen would be an `.rml` document with a stylesheet. The style is
therefore data that can be restyled without code changes, and a mod can ship its
own stylesheet.

**How it attaches to the game.** When the player opens Organize, the port shows
its own Organize screens instead of the game's:
- It reads the roster, units, legions and stock straight from game memory.
  `honors.cpp`, `war_director.cpp` and `class_rebalance.cpp` already read most of
  these tables.
- It applies changes either through the game's own routines, as
  `director_spawn.cpp` does, or by replaying the same writes those routines
  make. An example is `func_ovlS_801E13B0` for a class change.
- The game's own validation messages, from the decoded text blocks, are shown in
  the new style.

## 4a. Phase 1 status (built; the developer saw the test panel draw on 2026-10-29)

- **Dependencies.** RmlUi 6.0 and FreeType 2.13.2 are fetched by CMake
  (`FetchContent`, pinned tags) in `app/CMakeLists.txt`. FreeType needs
  `CMAKE_POLICY_VERSION_MINIMUM 3.5` under CMake 4. RmlUi's FreeType lookup is
  satisfied by an alias over the fetched target.
- **Shaders.** `app/shaders/RmlUiVS.hlsl` and `RmlUiPS.hlsl` are compiled by
  RT64's own `build_vertex_shader` and `build_pixel_shader`, giving DXIL,
  SPIR-V and Metal. The app sets `DXC` and the DXC option variables itself,
  because RT64 defines them in its own directory scope.
- **Renderer.** `app/features/rml_ui.cpp` registers RT64's render hooks
  (`RT64::SetRenderHooks`). The draw hook runs on every present, after the game
  picture, with the swap-chain framebuffer bound. It draws RmlUi through a plume
  render interface:
  - upload-heap geometry
  - RGBA8 textures uploaded on the present command list
  - premultiplied-alpha blending
  - scissoring

  Released resources are destroyed 4 presents later. RmlUi runs entirely on the
  render thread, and mouse input from SDL is queued and replayed there.
- **Test document.** `app/ui/test.rml` and `style.rcss`, with the Lato fonts
  copied from RmlUi's samples (SIL OFL), go to `ui/` next to the exe. F2 shows
  and hides it, and `OGRE_RMLUI_TEST=1` shows it from the start.
  `OGRE_RMLUI=0` turns RmlUi off.
- **Checked.** The developer pressed F2 in game and the panel drew with text
  (2026-10-29). An earlier run with the font family misnamed (`Lato` for the
  file's `LatoLatin`) crashed for the developer; that run left no `error.log`, so
  the cause was not confirmed. On Android the `ui/` files are not packaged into the APK, so the
  document reports itself missing there. Image loading (`LoadTexture`) and
  transforms are not implemented. Pad navigation is not wired; only the mouse
  and the queued keys are.

## 4b. Phase 2 restyle (built 2026-10-29, not yet seen in game)

The F3 view follows the approved mockup
(``), in `app/ui/organize.rml`
and `organize.rcss`, with the data in `app/features/organize_view.cpp`.

- **Data model placement.** `data-model="organize"` sits on `#screen`, not on
  `<body>`. RmlUi parses a document before it attaches it to the context, so a
  model on `<body>` is not found during the parse and every `data-for` list stays
  as raw template text (seen in game on 2026-10-29).
- **Look.** Parchment windows (`#e9e4bb`) with a two-tone border, labels set into
  the top border, blue name bars, over a dark blue field. The render interface
  has no transforms, gradients or shadows, so the bevel is a two-tone border and
  the boards are not slanted.
- **Formation.** Each unit is a stepped board: three rows, the back row narrowest
  and highest, each row with a riser under it, tan/blue checker. Members stand on
  their squares as coloured figures, one colour per class id
  (`class_colour`). Row 0 of the game's square numbering is drawn at the back;
  which edge faces the enemy is not verified.
- **Top bar.** War Funds (`0x80196A8C`) and the Chaos Frame (byte `0x801936C9`,
  0..100, the value `func_8016FDD0` clamps).
- **Change Class tab** (was Characters). A roster on the left. On the right: the
  stats as bars, the CLASS window (portrait, class, `n / count`), a wardrobe with
  the class's figure, the class list, WAR FUNDS, and a prompt line. Honors was
  removed (developer, 2026-10-29): class changes are vanilla with no alignment
  requirement and are made in the game's own Organize menu, so the list holds
  the current class. Phase 3 fills it from the game's candidate list
  (`func_ovlS_801D2808`) and wires the change.
- **Portraits** (2026-10-29). The roster, the CLASS window and the unit member
  cards show the game's own speaker portraits through `<img src="portrait:N">`,
  which `rml_ui.cpp`'s `LoadTexture` hands to `app/features/portraits.cpp`. The game
  picks a portrait by class in `func_8016FD0C`: `D_8018ED00` (ROM `0x64C00`) is
  6 bytes per class id, and the halfword at `+0` (mode 0) or `+2` (mode 1) holds
  the index (`v >> 3`) and the palette variant (`v & 7`). Asset `0x01DFB36E` is a
  raw table of portrait asset ids (entry 0 unused). Each portrait decompresses to
  a 40x48 CI8 image (0x780 bytes) followed by 80-colour RGBA5551 palettes, the
  variant's at `0x780 + variant * 0xA0`. Found from a capture of a dialogue with
  Magnus on screen: class `0x51` -> index 72 -> asset `0x01E16552`, whose decode
  equals the image at RDRAM `0x803411F0` and the palette after it. Generic
  characters share their class's portrait, as in the game.
- **Not yet.** Class sprites (figures are placeholders), the stat
  hexagon (bars stand in; RmlUi has no polygon drawing here), the stats after a
  change, and the gear part of REQUIRE.

## 4c. Phase 3 start: edits through the working copy (built 2026-10-29, not yet played)

**The game edits a working copy.** RAM snapshots taken by the developer before
and after vanilla Formation, Remove/Add, Battle Strategy and Change Class show
that the unit table `0x80197210` does not change while the Organize screen is
open. The enter `func_ovlS_801D4BB0` copies the units into the Organize state
(word `0x80196B18`), and the commit `func_ovlS_801D5328` writes them back when
the screen closes (snapshot 15 -> 16: the state word goes to 0 and units 0 and 2
change in one step). Character class changes and goth do write their tables
straight away.

| working unit `state + 0x117C + slot*0x36` (30 slots) | committed to |
|---|---|
| `+0x00` unit number | record `+0x00` |
| `+0x01` flags (bit `0x10`) | record `+0x01` (`|1`, `|5` with the bit) |
| `+0x02` legion (`0xFF` none) | the legion table at state `+0x10D4` |
| `+0x03` leader's square (`0xFF` = slot unused) | the leader goes first in the member list |
| `+0x04..+0x0C` character ref per square (`0xFF` empty; 100+ soldiers) | members `+0x02..` and squares `+0x07..` |
| `+0x0D` record index (`0xFF` = allocate) | which record |
| `+0x1D`, `+0x1E` (Battle Strategy), `+0x1F` | record `+0x18`, `+0x17`, `+0x0C` |
| `+0x22` 10 u16 carried items | record `+0x0D..` |

**Design.** `app/features/organize_edit.cpp` queues requests from the view and the
main loop applies them with the game threads parked
(`ultramodern::checkpoint_pause_begin`), writing only the working copy; the
game's own commit writes the unit table and the leader bits. While the game's
Organize screen is open the F3 view reads the working copy, so it shows the
edits made in the game's own menus too; otherwise it reads the unit table and is
read-only.

**Commands built:** Reposition (pick a square, then a square of the same unit:
move or swap; the leader square follows the leader), Remove (not the leader, not
a soldier), Add (a character from Unassigned into a picked empty square, at
most five per unit). With LARGE UNITS on VANILLA, originally large characters
are not moved or added, because their two-place rule is not decoded.

**Make Leader** sets `+0x03` to the picked square when the character passes
the game's own leader test: `func_8016DEE4(class, key)` returns class byte
`+0x06` (of the class, or of the class key at character `+0x12` when the class's
`+0x45` differs), and ten Organize routines call it. The byte is 1 for Knight,
Archer, Gladiator, Paladin, Wizard, Sorceress, Doll Master and 0 for Soldier,
Fighter, Amazon, Hawkman, Ogre, Hellhound, Golem, Faerie and Zombie.

**Battle Strategy** steps `+0x1E` through 0..3. The game's strategy menu stores
its selected index there (`sb v0,30(s0)` at `0x801E9594`), and the names are the
strings at `0x80186F90`/`FA0`/`FB4`/`FC8` (ROM `0x5CE90`), listed in that order by
the pointer table after them: Autonomous, Attack Strongest, Attack Leader,
Attack Weakest.

**Input.** While F3 is open the OS pointer shows and the game's own cursor
ignores the mouse (`panel_owns_mouse` in `sdl_platform.cpp`). Every control takes
focus with spatial navigation; focus starts on the first board square; the pad's
shoulder buttons and Page Up/Down switch tabs; A confirms, B closes.

**Carry Item** (built 2026-10-29, not yet played). Carried items are the working
unit's ten u16 slots at `+0x22` (the commit packs them as bytes into record
`+0x0D..`). The consumable stock is 40 records of `{u16 item, u8 in use, u8
owned}` at `0x80193AE0` (`func_ovlS_801D0470` counts them); snapshots 9 -> 12
show the in-use byte follow the carried items live (item 1: `00` -> `02` -> `01`),
and `func_ovlS_801CA5A8` / `func_ovlS_801CEB44` decrement it when items come
back. A unit's limit is the game's `func_ovlS_801D0398`: the sum over its members
of `func_8016D220(class, key)`, class byte `+0x47` (soldiers as class 1); the
help text says the same ("The sum of this number for all unit members equals
the number of expendable items that a unit can carry"). The view's Carry Item
button opens the free stock; a click carries one (in use + 1), and a click on a
carried item in the Unit panel puts it back (in use - 1, list packed).

**Carry Item names.** The consumable stock ids name items in the consumable
table at `0x8018E6EC` (0x0C per item: name pointer, a word whose top byte is the
category and low bits the price, flags; entry 0 "None"), which follows the
278-entry equipment table. The first build named them from the equipment table
and showed Short Sword etc. (developer). Categories: 0 expendables, 1 stat items,
2 Silver Hourglass / Dowsing Rod / Love and Peace, 3 carried charms, 4 quest
items; Carry Item offers 0..3 (the quest-item exclusion follows the help text
"expendable and portable items", not a read of the game's filter).

**Formation sprites** (built 2026-10-29). The boards, the class list and the
wardrobe draw the game's own formation sprites (`portraits::decode_sprite`,
`<img src="sprite:N">`). The format comes from rempred/LordlyCaliber
`army-sprites.js` (MIT): LZ atlases of two RGBA5551 palettes then CI8 planes; a
class's model is byte `+4` of its route record in the table at ROM `0x64C00`
(`& 0x3F`, lane special when `0x80`, large when the class size is 2). That
project targets the US revision 0 ROM; this ROM is revision 1, so the three
front-view atlases were located here by decoded size (ordinary `0x234F184`,
special `0x23571F8`, large `0x2348678`). Palette 1 is the player's colours.
The same project's squad code shows the game draws a row's squares mirrored
(square `row*3+2` on the left) and that the front row is squares 6..8; the
boards now follow that.

**Remove, disband and New Unit through the game's routines** (built
2026-10-29, not yet played). These change more than the squares, so the view
queues them and `ogre_organize_frame` runs them on the frame pump
(`tools/director_hook.py`, after the swap at `0x8008B120`, a game thread with a
guest stack), only while bank S (ROM `0x87220`) is resident, with `*ctx` saved
and restored around the calls:
- Remove a member: `func_ovlS_801CADB0(ref)` then the square to `0xFF`, which is
  the game's own disband loop for one member. For a soldier ref it counts the
  figures (`func_ovlS_801CBCB8`), zeroes the slot strength (`0x80190EDC + ref*2`)
  and flags (`0x80191030 + ref`) and adds the figures to the reserve byte
  `0x80190FA1`; for anyone it clears the character's Organize flags (state +
  `0x1872 + ref*2`), counts it unplaced (state + `0x5D4`) and refreshes
  (`func_ovlS_801CE094`).
- Remove the leader: disbands the unit, `func_ovlS_801CA5A8(slot)` (releases
  every member, returns carried items to the stock, leaves the legion, lowers
  the unit counters at state + `0x2C0`/`0x2C4`). Developer: removing a leader
  should disband the formation.
- New Unit: working slot 30 is the game's scratch unit; the view fills its
  leader square and square, and `func_ovlS_801CAB88(30)` takes a free slot
  (`func_ovlS_801CBBC0`), a unit number (`func_ovlS_801CBBF4`), clears strategy
  and items, copies the 54 bytes, raises both counters and marks the members
  placed. The leader starts on square 4 and must pass the leader test.
- Add now does the same placed-flag bookkeeping as that member loop.

**Soldier figures.** A soldier slot draws as 1, 2 or 3 figures (atlas models 0,
1, 2) by `func_ovlS_801CBCB8`: one per started third of the maximum strength
(`0x80190FA2`).

**Soldier promotion progress** (built 2026-10-29). A unit's soldiers share one
experience pool: unit record `+0x0C` (working copy `+0x1F`, the commit's
`+0x1F -> +0x0C`). Across one won battle on the field it went 0 -> 5 for unit 3,
which has three soldier slots (developer's snapshots 4 -> 5); no per-soldier
counter changed. A game guide the developer quoted: the pool grows with every
battle won in a mission (not training), more with more soldier slots; at 100 one
slot becomes a Fighter (male leader) or Amazon (female leader, character
`+0x14`), which needs a spare equipment set of that class, and the pool resets.
The Unit panel shows "Soldier EXP n / 100" with a bar for units with soldiers.

**Add through the game's routine, soldiers included.** Add now calls
`func_ovlS_801CB23C(slot, square, member)` on the game thread (member 0 = a
soldier slot from the reserve). For a soldier it takes a free ref
(`func_ovlS_801CBC84`), moves up to three soldiers out of the reserve byte
`0x80190FA1`, sets the strength to the maximum for three or a third of it per
soldier, sets the ref's flag byte to `0xC2` and refreshes the unit
(`func_ovlS_801D04A4`, `func_ovlS_801D0714` -> working `+0x1D`,
`func_ovlS_801C67D0`). Unassigned lists "Soldiers - n in reserve" while the
reserve is not empty. The routine also branches on the Organize menu mode
(state `+0x182`); F3 calls it from whatever mode the game's menu is in.

**Picked member panel.** Picking a square shows the member's portrait, class,
level, HP (strength for soldiers), stats, and the class's attack for each row
(class `+0x37 + 2*row`, attack id and count; names from the attack table
`0x8018AA9C`, "Magic" for spells), with the row the square is in highlighted
(squares 6..8 front, 3..5 middle, 0..2 back).

**Not built yet:** Replenish (no snapshot covers it), Legion commands, Change
Class from the view. The game's own Organize
screen may draw its old layout until its cursor moves; the commit uses the
working copy either way.

## 4d. Parity commands (built 2026-10-29, not yet played)

All addresses are bank S unless noted; Organize state is `*(u32*)0x80196B18`.
F3 runs these on the frame pump (`ogre_organize_frame`) while the game's
Organize screen is open.

| command | the game's code | F3 |
|---|---|---|
| Change Class list | `801D2808` builds `+0x1C0E` (count `+0x1BFC`) from `+0x5E8`; `801D2378(char, class)` greys, reason `+0x5ED`; `801D2F88(char, class)` is the goth for missing basic gear | loads the requirement table at `+0x80` as the sub-screen does (`8009DD38(0x01E398D2)` at `0x801E1418`, freed with `800712C4` at `0x801E2BA4`), restores `+0x18B/+0x5E8/+0x5ED/+0x80` |
| Change Class apply | `801E13B0` `0x801E2414..0x801E2734`: each slot's item and the old basic item lose a use, slots cleared, class/key written (a class with key `+0x45` keeps its class byte; table `0x801EF3D8`), new basic items in use (bought when none spare; record from `801D347C`), unit refresh `801D04A4`/`801D0714` (`+0x1D`), legion `801CDED8`/`801D082C`, then `801CFC44`, `801D2A18` | ported step for step; goth from `801D2F88` |
| Discharge | `801DA7A0` `0x801DAFE0..0x801DB1E8`: out of the unit (leader: `801CA5A8`), `+0x5D4`/`+0x5D8` down, `8016BA6C(0, char)` (main ELF: returns gear, uses up the basic set, drops the char from `0x80197730`, clears `+0x11`), `+0x1872+char*2 = 0`, `801CE3F8`, `801CE094`, `801CDD50(state+0x134)` | same, release through `801CADB0` (which counts the char unplaced first) |
| named characters | classes `>= 0x51`; their refusal lines are message set `0x1C`, index `class-0x51` (`0x801DAD98`) | refuse Discharge and Rename |
| Replenish | `801C9D6C`: clear `+0x1BEE` (`80093380`, `0x1FE`), `801CF818` lists damaged soldiers of undispatched units (need `+0x1DED`), `801CF1B0` fills from `0x80190FA1` | same, list area saved and restored |
| Exchange | (not read) | release both (`801CADB0`) and place each in the other's square (`801CB23C`); leaders and soldiers stay |
| Unequip All | (not read) | every slot holding the item back to basic (Equip's rule); carried consumables put back (Drop) |
| Class List | `801D736C` known bits `0x80196A96` (mask `1 << (class & 7)`) | per known class, `801D2378` for every character |
| Rename | name at character `+0x00`, 16 bytes | 15 characters |

**Legion, read but not built.** Legion table at `+0x10D4`, `0xE` per legion
(up to 10 slots, 6 legions): `+0x0` legion number (1..6, `801EDC5C`), `+0x1`
0 at creation (formation, unconfirmed), `+0x2..+0x6` unit slots (`+0x2` the
core), `+0x7` 1 at creation, `+0x8` unit count, `+0xA`/`+0xB` from
`801D082C`/`801D08D8`. Legion count `+0x5DC`, order list `0x80193AD0`
(`801CE68C`, `801CE394` -> `+0x1AB0`). New Legion commits the scratch
legion at `+0x1160` with `801EDAD4(legion)` (bcopy `80093060`, marks units
`+0x2 = legion` and flag `0x8`, `+0x2C0` down for each, `801D7998`). Disband
`801CA724(legion)`; remove a unit `801CA82C(legion, pos)`. Not read yet: the
Legion Core leader rule (`8016DBF8` reads class `+0x2C`, unconfirmed), Add
Unit, Legion Formation, Division Placement (the submenu `801D93A4` -> `801D91F0`)
and the unlock (candidate: story flag `0xB` via `8016FF14`, bits at
`0x801936A6`, or byte `0x80193700`, both gating a window in `801CC20C`).

## 4d-req. Change Class requirements (2026-10-30)

The Change Class check `func_ovlS_801D2378(character, class)` reads the
requirement table (asset `0x01E398D2`, loaded to state `+0x80`) at
`table + class*8`: bytes 0-5 are the minimum STR, VIT, INT, MEN, AGI and DEX
(character `+0x1C`, `+0x1E`, `+0x20`, `+0x22`, `+0x24`, `+0x26`, u16),
byte 6 the minimum and byte 7 the maximum alignment (character `+0x1B`); `0xFF`
is no requirement (0x801D2470..0x801D25D0). A stat failure leaves the reason
byte `+0x5ED` at 0. The view copies the row with the game's class list and draws
one bar per stat with the minimum marked, red where the character is short (the
game's own screen shows the same chart). The alignment bar is shown only with
CLASS ALIGNMENT = VANILLA. A ready class whose Basic Equipment is missing shows
its cost on the button ("Buy gear (N Goth) and Change Class"); the apply already
buys it (`func_ovlS_801D2F88`, 0x801E20F0). All of this needs the game's
Organize screen open underneath (developer report 2026-10-30: the view opened
alone on the world map, capture 13, was read-only, so the list, Sell and Buy
set did nothing); PORT MENUS = MODERN now opens the view over it.

## 5. Phases

| phase | content |
|---|---|
| 1 | Add RmlUi rendering through RT64: one test document drawn over the game, with pad and mouse input routed to it |
| 2 | Read-only screens: Formation and Character, drawn from game memory, with no edits |
| 3 | Edits, one command at a time, each through the game's own routine: Change Leader, Add/Remove, Reposition, Equip, Change Class (with the Honors rules), Carry Item, Replenish, Legion commands |
| 4 | Items and Armory screens |
| 5 | Replace the game's Organize entry with the new screens. The game's own screen stays reachable through a setting while the new one is checked. **Started 2026-10-30, not played:** with PORT MENUS = MODERN (CONTROLS tab, `Choice::FieldControls`, the default) the view opens by itself over the game's Organize screen (scene `0x06` with the state word `0x80196B18` set), once per visit; CLASSIC leaves the game's screen alone. Closing the view now leaves the game's screen too (2026-10-30, captures 7-8 of 08:50: state 4 step 3, then the world map): the game thread sets state `+0x120` = 999, where the update tears down (commit, free) and returns to the previous screen, from the world map or a mission's R menu alike. The view is fully opaque (the game's screen showed through). Change Class runs through the game's own Change Class screen so its wardrobe animation plays (captures 1-6 of 08:49): the current state is pushed on the stack (`+0xA8 + depth*6`, depth `+0xA6`), `+0x18B` = character, `+0x1BD4` = 20, state 6, step and sub-step 0; at step 7 sub-step 0 (the class list) the cursor `+0x5EA` is set to the target's index in `+0x1C0E` and A is pressed (the pad word, `organize_edit::injected_buttons`); after the change's sub-steps the list returns and B closes the screen. The view hides meanwhile. Not played. The earlier notes on the exit: the game's update `func_ovlS_801C214C` tears down and returns 1 at state `+0x120 = 999` (set by `func_ovlS_801C97C0` when the step `+0x122` reaches `0x63`), 2 at state `0xE` (also sets `0x801939F0 = 1`) and 3 at state `1000` (`func_ovlS_801DA7A0`); which one is the Exit command needs a capture pair around a vanilla Exit. |

## 6. Open questions

- Which routine is each command's apply step, and does it run outside the
  game's Organize screen? Phase 1 of `design-game-ui.md` found the class
  change's (`func_ovlS_801E13B0`), and each other command needs the same
  reading.
- Portraits and unit sprites: the reference shows character portraits. OB64 has
  portraits for named characters only. Generic characters would use their class
  sprite.
- Should the new screens also replace the field's unit menu (Items, Status,
  Exchange at strongholds)? These share commands with Organize.

