# Design: mission field controls overhaul

Status: plan, nothing built (developer request, 2026-10-29). Replaces the
multi-select-only plan of the same day.

Every feature here works three ways: mouse, keyboard, and gamepad, with the
same result. Nothing is mouse-only or pad-only.

## 1. Goals

The mission field (scene `0x03`) makes the player give every order through a
menu, hunt for units on a scrolling map, and remember which unit is suited to
which town. The overhaul turns it into a modern real-time strategy layer while
the game's own rules (movement, battles, liberation) stay untouched:

1. **Direct orders** without menus, for one unit or a group.
2. **Selection** of several units: drag box, growing circle, selection bar.
3. **Army panel**: every deployed unit, its status and its alignment.
4. **Idle-unit cycling** and **alerts** that jump to the event.
5. **Camera** control fit for a mouse and a right stick.
6. **Path preview**, **waypoints**, **stop**, and **repeating patrols**.
7. **Group commands**: stop, return, battle strategy, for everything selected.
8. **Pause to plan**, consistent prompts, and accessibility options.

## 2. Controls

| action | mouse | keyboard | gamepad |
|---|---|---|---|
| select a unit | left click it | (cursor keys/WASD, Confirm) | cursor + A |
| select many | left-drag a box over empty field | hold Shift + cursor keys to size a box, Enter | hold X: a green circle grows from the cursor; release selects |
| add / remove from the selection | Shift + click / Ctrl + click | Shift + Enter on a unit | hold X again to add; remove from the bar |
| clear the selection | right click the bar's empty area | Backspace | B |
| Move order: to ground / liberate / attack / return | right click ground / town / enemy / own stronghold | cursor on target, Enter | cursor on target, A |
| enter the town a unit stands in (shop, witch's den, stronghold) | right click that town, or the bar's Enter town button | E | bar command (Y) |
| use the group's items | the bar's Use item button | I; 1..9 picks an item; Enter uses it on the weakest | bar command (Y) |
| queue a waypoint | Shift + right click | Shift + Enter | hold LB + A |
| make the route a patrol | P, or the bar's Patrol button | P | the bar's Patrol button (Y opens the bar's commands) |
| stop | S... see note | Delete | bar command |
| next idle unit | `.` | `.` | RB |
| previous idle unit | `,` | `,` | LB |
| army strip | always docked; click a tile to select, double click to jump | F: focus the strip | View/Select... see note |
| jump to an alert | click the alert | `J` (latest alert) | D-pad Up (latest alert) |
| camera pan | screen edge, middle-drag | WASD / arrows | right stick |
| camera zoom | wheel | R / F (the existing camera keys) | right stick up/down (existing) |
| pause to plan | — | Space | Menu... see note |

**PORT MENUS setting (CONTROLS tab, saved as `field_controls`, 2026-10-30; first named FIELD CONTROLS).** It also opens the port's Organize view over the game's Organize screen (`docs/anotherworld/design-organize-screen.md` phase 5). MODERN (the default)
gives the mouse on the mission field to the overlay, at the developer's
request: the game's command menu must not open, and the game's cursor
following the pointer scrolled the field while the pointer travelled to the
overlay. In MODERN mode mouse buttons and the wheel never reach the game (a
left click on nothing clears the selection), the game's cursor does not follow
the pointer and the OS pointer stays visible, the field scrolls only with the
pointer within 6 points of the window's edge (the D-pad bits the game's cursor
scrolls with). A right click with nothing selected is still the game's B, so
the game's menus can be left with the mouse, and R still opens the game's hold
menu, whose Dispatch is the only way to send units out until the overlay has
its own (developer report the same day: R blocked left no way to dispatch, and
a town or unit list opened from the keyboard trapped the mouse; captures 14-15,
field mode `0x801F0E00` = `0x14` in that list). The overlay's own commands replace the menu: Organize (F3) above the
strip, and Stop (`func_ovlN_801D4630`), Return to HQ (move to the location with
state bit `0x10`) and Clear on the selection bar; a click on a selection card
drops that unit. CLASSIC is the previous behaviour. Pad A and keyboard A still
reach the game in both modes; deploying units from the headquarters and town
entry still need the game's menu until R10 and the deploy routine are found.

**The pad on the MODERN open field (2026-11-01, `field_select.cpp`, "The pad
on the MODERN open field").** Before this the overlay was mouse-only: the pad's
A reached the game, which opened its own unit menu, and nothing was ever
selected (developer report, 2026-11-01). The game's cursor (left stick and
D-pad, the game's own mover) is the pad's pointer, read from `0x800E91D0`
`+0x08`/`+0x0C`. On field mode 0, with no Organize view and no Esc menu:

| pad | does |
|---|---|
| A | own unit under the cursor: select it; units selected: order them to the cursor (ground, town or enemy, as a right click); otherwise the game's A |
| B | close the command menu, else clear the selection, else the game's B |
| X hold | a green circle grows from the cursor, 8 to 96 field pixels over a second; the release adds the units in it |
| Y tap | the command menu: the overlay's buttons (bar, top bar, strip, Dispatch list) in turn; D-pad moves, A presses, B or Y closes |
| Y hold (300 ms) | the game's R, its hold menu |
| RB / LB | next / previous idle unit |

The buttons the overlay takes press nothing in the game:
`field_select::claimed_pad_buttons()` is passed to
`gamecontroller_buttons_from_map` as a skip mask, statically for the open
field (A, X, Y, LB, RB; B with a selection; the D-pad with the menu open) so a
poll between the press and its SDL event never leaks it, and per press until
the release. A pushed left stick or a held D-pad lifts PAUSE ON SELECT, since
the game's cursor runs on the game's frames. The card and the top bar show the
device used last (`input_map.hpp` `last_input_device`), with key and button
images from Kenney's Input Prompts (CC0, `app/ui/prompts`), and a controls line
above the bar while the pad is in use. The left stick's y was passed to the
game with SDL's sign (down positive) and moved every cursor upside down; it is
negated now, and CONTROLS > GAMEPAD has STICK LEFT / RIGHT and STICK UP / DOWN.

Notes. Key choices collide with the existing action map in places (S is a
cursor key, View opens the Esc menu, Menu is Pause today); the final table is
fixed in phase 0 against `app/src/input_map.cpp`, and every binding goes
through the CONTROLS tab so the player can change it.

## 3. Features

### 3.1 Direct orders

A context order replaces Move/confirm: what the cursor is on decides the
order (ground: move; neutral or enemy town: liberate; enemy unit: attack;
own stronghold or town: return or garrison). One unit or a group gets the
same order, each unit through the game's own order routine, so pathing,
speed, battles and liberation are the game's. The UI calls the order
**Move** on every input.

Right click (Enter, A) on the town or stronghold a selected unit already
stands in opens that place's own menus (shop, witch's den, stronghold),
through the game's own town-entry routine (R10). The bar's **Enter town**
button (E) does the same and is greyed out when no selected unit is in a
town. Stop stays as the halt order.

### 3.2 Selection

- Drag box (mouse), growing green circle (pad, from 8 to 96 field pixels over
  one second), Shift-sized box (keyboard).
- Only the player's own deployed units that can take orders.
- **Legion rule:** a unit in a legion is replaced by its Legion Core; each
  legion counts once.
- **Selection bar** at the bottom: a card per selected unit with the leader's
  portrait, unit number, strength bar, alignment marker with "liberates" or
  "captures", and the unit's carried items as icons (a count badge on a stack,
  dashed squares for empty slots); click or press A on a card to remove it;
  B clears. The bar also holds the group commands (3.6).
- Besides colour, selected units get a ring shape and a number badge, so the
  selection reads without colour.

### 3.3 Army strip

A narrow docked column on the left (the developer chose this over a wide
list panel on 2026-10-29), one tile per deployed unit, the same on every
input:

| tile line | meaning |
|---|---|
| unit chip | unit number on the leader's colour |
| status | MARCH, BATTLE, LOW, IDLE, PATROL, RETREAT, GARRISON |
| alignment | the leader's alignment as a marker and a value: ▲ 71, ◆ 50, ▼ 22 (section 3.4) |

A selected unit's tile has a thick green border. Clicking a tile selects the
unit; double click jumps the camera to it. Destination and strength are on the
selection card and the path line, so the tile stays small.

### 3.4 Alignment for liberation

In OB64 the alignment of the unit that takes a town decides whether it is
liberated or captured and how the Chaos Frame moves. The game's rule (R6,
section 4): the average alignment of all the unit's members against the town's
own alignment byte; a gap of 31 or more (plus up to 7 at random, plus 40 for
some towns) is a capture, and the gap sets the Chaos Frame change from -5 to
+2. The strip, the selection bar and the
order cursor show it:

- the leader's alignment value and a marker shape (▲ / ◆ / ▼), so it reads
  without colour;
- with units selected and the cursor on a town, a **target card**:
  - the verdict for the town: "Will be liberated" or "Will be captured";
  - one line with the rule and the numbers: "unit 64 vs town 55: gap 9,
    liberated, Chaos Frame +1" (the first unit to enter decides);
  - every selected unit in arrival order, with its alignment, its outcome
    ("Liberates" / "Captures") and its travel time;
  - a warning when a later unit would capture the town if an earlier one is
    held up by a battle;
  - two fixes: "Move without Unit N" and "Unit N waits outside";
  - a path line in a warning colour for a unit that would capture.

The mockup's "50 or higher liberates" is a placeholder. The rule and its
inputs are the game's own liberation code (R6, found 2026-10-30).

### 3.5 Idle units and alerts

- **Next / previous idle unit** selects it and moves the camera to it.
- **Alerts** stack at the side for a few seconds and stay in a log: battle
  started, battle won or lost, town liberated, town taken by the enemy, unit
  routed or at low strength, enemy unit near the stronghold, patrol
  interrupted. Clicking an alert (or the pad's alert key) jumps to it.

### 3.6 Routes, patrols and group commands

- **Path preview:** selected units show their path line and destination flag.
- **Waypoints:** Shift-ordered points are queued; the port issues the next
  order when the unit arrives.
- **Patrol:** a route with two or more points repeats until the player gives
  the unit another order. Arriving, a battle, or a town on the way does not
  end it; a rout or a manual order does.
- **Group commands** (selection bar, a 4-wide grid): Move, Stop, Return to
  stronghold, Patrol on/off, Enter town, Battle Strategy for all, Use item.
  On a pad they are the Y menu.

### 3.6b Group items

**Use item** opens a menu that pools the items carried by every selected
unit:

- one row per item: icon, name, total count, effect, and who carries it
  ("Unit 1 ×2 · Unit 4 ×1");
- after an item is picked, a button per selected unit with its strength
  before and after ("74% → 98%"), and "Use on the weakest" (Enter);
- the target can also be picked by clicking a selection card or a unit on
  the field;
- the item is taken from whichever selected unit carries it, through the
  game's own item-use routine (R11), so effects and counts are the game's.

The mockup shows items per unit. If R11 shows that field items are one
army-wide stock, the menu stays the same and the cards drop their item
icons.

### 3.6a Battles without leaving the field

Every encounter on the field switches to battle mode today, and with several
units out that is a lot of interruptions. In OB64 the player does little inside
a battle: the fight runs from each unit's Battle Strategy, and the player's
only inputs are tarot cards and Retreat. So most battles can resolve without
the player watching, while battle mode stays one click away.

**Three ways a battle can play (setting BATTLES, plus per-unit overrides):**

| mode | what the player sees | inputs in the battle |
|---|---|---|
| **Watch** | battle mode as the game draws it today | tarot, Retreat, as today |
| **Quick** | battle mode at high speed (8x, intro and result screens advance by themselves); a tap of Confirm drops back to normal speed | as Watch once slowed down |
| **Auto** | the field stays on screen; a crossed-swords marker with a progress ring sits where the units met, and the result arrives as an alert | none: Battle Strategy decides, no tarot cards are used, Retreat follows the retreat rule below |

The outcome is always the game's own: Quick and Auto run the game's real
battle code, only faster and (for Auto) without drawing it. Nothing about
damage, experience, alignment, loot or deaths is estimated or recomputed by
the port.

**Rules that decide the mode for one battle:**

1. The BATTLES setting gives the default (proposed: Quick).
2. Some battles are always shown, whatever the setting, because they matter:
   a boss or story unit, a unit whose leader is a named character, and the
   first battle of each mission (so the player sees the enemy).
3. A per-unit setting on the unit's selection card: Always watch / Default / Always auto.
4. **Jump in:** when an Auto or Quick battle starts, an alert "Battle: Unit 3
   vs Black Knight" appears with **Watch**. Choosing it (click, `J`, D-pad up)
   switches that battle to Watch from its next round, so the player can still
   use tarot cards or retreat.
5. **Retreat rule for Auto:** a unit retreats on its own when its strength
   falls below a threshold the player sets per unit (off by default, as the
   game never retreats on its own).

**How it plays out, with the field:**

- The game freezes field time during a battle, and that stays: a battle,
  watched or not, happens "between" field moments. With Auto, the freeze is
  only as long as the game's battle code takes at full speed (well under a
  second of real time), so the field feels continuous.
- Two encounters on the same frame resolve one after the other, each with its
  own alert.
- The army strip shows BATTLE and the selection card a progress bar while a battle runs;
  the alert after it reads "Won / Lost / Draw · damage dealt and taken ·
  experience" with **Details**, which opens the battle's result screen as the
  game draws it.
- Liberation, town capture and the Chaos Frame change happen exactly as after
  a watched battle, and their alerts follow.

**What has to be found (adds to section 4):**

| # | question |
|---|---|
| R10 | Where the field hands over to battle mode (scene `0x03` → the battle scene, bank X) and back, and what the battle reads from the field (the two units, terrain, time of day). |
| R11 | Whether the battle code runs correctly at a raised speed and with presentation skipped (no frames drawn); the GAME SPEED multiplier (`ultramodern::set_speed_multiplier`) is the starting point, raised for the battle scene only. |
| R12 | The battle's own inputs: where tarot use and Retreat are read, so Auto can leave them untouched and the retreat rule can press Retreat. |
| R13 | The battle result the field reads afterwards, for the alert text. |

**Captures needed:** a key press just before an encounter starts on the field,
one during the battle, and one right after the field returns (R10, R13).

**Phase:** this is phase 7, after the field controls (it needs the alert
system of phase 1). Steps:

34. A: R10..R13 from the captures and the battle bank.
35. A: Quick: raise the speed multiplier for the battle scene only, advance
    its intro and result screens automatically, Confirm drops back to 1x.
36. D: play ten battles on Quick; outcomes look like the game's.
37. A: Auto: run the battle scene without presenting its frames (the field's
    last frame stays on screen with the overlay), field marker and progress
    ring, result alert with Details.
38. A: rules 2..5: always-shown battles, per-unit overrides, Watch from the
    alert, the retreat threshold.
39. D: a whole mission on Auto, checking that experience, deaths, loot and
    liberation match what Watch would give.

### 3.7 Camera, pause, accessibility

- Edge scroll, middle-drag pan, WASD/arrow pan, right stick pan; wheel zoom
  on the game's three zoom levels; double click a card or row to jump.
- Space pauses the field (orders can still be given), on top of GAME SPEED.
- Settings: edge scroll on/off and speed, circle hold or toggle, alert types
  on/off, a first-time hint card listing the field controls.

## 4. What must be found in the game first

Known so far:

| fact | where |
|---|---|
| cursor (f32 screen pixels, 320x240) | state `0x800E91D0` `+0x08`/`+0x0C`; mover `func_ovlN_801BE4E0` (`app/src/mission_cursor.cpp`) |
| the game's unit pick | `func_ovlN_801B8070`, `func_ovlN_801B9694` |
| unit records | `0x80197210 + u*0x19`, 0..29 player |
| map objects | `0x801F0CD0 + slot*4`; object `+0x08/+0x0C/+0x10` world position, `flags & 0x11` |
| field update | `func_ovlN_801B15A0`; the director's hook at its exit (`0x801B258C`) |
| spawned-enemy behaviour table | `func_ovlR_8021592C` builds an AI behaviour list; it is not the player's move order |
| leader alignment | character record `+0x1B` |
| legion table (Organize) | state `+0x10D4`, `0xE` per legion (`design-organize-screen.md` §4d) |

To find:

| # | question |
|---|---|
| R1 | The player's order routine: what "Move" (and liberate, attack, return) writes, and the form of the destination (world point, map cell, town id, enemy slot). |
| R2 | Each unit's screen position: the field's world-to-screen projection or a stored screen point (needed for the box, the circle, rings and path lines). |
| R3 | Legion membership and the core, as the field sees it. |
| R4 | The game's own selected unit and command menu state, so the overlay never acts at the same time. |
| R5 | Unit state: idle, marching, fighting, retreating, garrisoned; strength. |
| R6 | Liberation and alignment: the code that changes a town's reputation from the leader's alignment. |
| R7 | Events for alerts: battle start/end, town owner change, rout. |
| R8 | Arrival: how to tell a unit reached its destination (for waypoints and patrols). |
| R9 | Pause: whether the field can be frozen while orders are still accepted. |
| R10 | Town entry: the routine the game's own command menu calls to open a town's or stronghold's menus (shop, witch's den), and which unit it acts for. |
| R11 | Field items: where a unit's carried items are stored (per unit or army-wide) and the routine that uses one on the field. |

### Captures needed from the developer (AGENTS.md rules 11 and 12)

**How to capture.** Start the game with a capture key (any number-row digit
1..9; the number pad does not count, and the developer's 6 key is broken, so
use 7):

```bash
OGRE_NO_AUDIO=1 OGRE_RAM_KEY=7 OGRE_RAM_KEY_FULL=1 OGRE_RAM_KEY_OUT=<folder> build-dist/ogrebattle64.exe
```

From PowerShell:

```powershell
$env:OGRE_NO_AUDIO="1"; $env:OGRE_RAM_KEY="7"; $env:OGRE_RAM_KEY_FULL="1"; $env:OGRE_RAM_KEY_OUT="captures"; & ".\build-dist\ogrebattle64.exe"
```

Captures only work once the game itself runs (after START GAME on the port's
start screen). Each press writes `<folder>/ram-<n>-full.bin` (the whole 8 MiB
of guest RAM) and logs a `[ramkey]` line; presses are numbered in order, so
taking them in the order below needs no notes. One play session gives the set:

| press | moment | answers |
|---|---|---|
| 1 | a unit standing still, the cursor elsewhere | R5 baseline |
| 2 | the command menu open on that unit | R4 |
| 3 | just after "Move" is confirmed to empty ground | R1, R5 |
| 4 | a few seconds later, the unit marching | R5, R8 |
| 5 | just after a unit is ordered to a town | R1 |
| 6 | just after a unit is ordered to an enemy unit | R1 |
| 7 | a unit in a battle | R5, R7 |
| 8, 9 | just before and just after a high-alignment leader liberates a town | R6, R7 |
| 10, 11 | the same with a low-alignment leader, if there is one | R6 |
| 12, 13 | a unit standing in an owned town: before and after choosing to enter it (the town's menu open) | R10 |
| 14, 15 | just before and just after using an item on a unit in the field | R11 |
| 16, 17 | standing still on the field, one in-game hour apart (watch the clock), nothing else changing | the in-game clock (War Director R5, night raids, "days apart") |
| 18, 19 | just before a battle starts on the field and just after you win it and are back on the field | battle start and result, the beaten unit leaving the field (R7, battles R10/R13, War Director R11) |
| 20 | an enemy unit that retreated from a battle, back on the field (if it happens) | retreat versus defeat (War Director spoils) |
| later | a legion deployed on the field (legions unlock later in the story) | R3 |

#### Findings from session 1 (captures 15-21, 2026-10-30, Tenne Plains)

Map object (0xC0 bytes, pointer table `0x801F0CD0 + slot*4`), from units 1, 4
and 0 marching in captures 15-20 and LordlyCaliber's decompiled C:

| field | meaning |
|---|---|
| `+0x00` | flags: `0x01` out on the map (undeployed `0x138`, standing `0x139`, marching `0x13B`; captures 1-4, 32), `0x02` marching, `0x00800000` standing inside a location (a town or the headquarters; captures 22-23), `0x00040000` set with marching, `0x2000` set by hold and pursue |
| `+0x08 / +0x0C / +0x10` | position x, y, z (floats) |
| `+0x14` | current map node (`64 * z_cell + x_cell`) |
| `+0x20` (byte), `+0x24` | an order is active (1) |
| `+0x28 / +0x2C / +0x30` | destination point x, y, z |
| `+0x58` | destination node; equals `+0x14` on arrival (R8) |
| `+0x74` | location index the unit stands in, -1 while marching (unit 1 reads 5 after reaching location 5) |
| `+0x84` | pursued object index, -1 for none |
| `+0x4C / +0x50 / +0x54`, `+0x64` | the pursued unit's position and node |

Order routines (R1), all bank N, revision 1 addresses (LordlyCaliber names in
brackets):

| routine | effect |
|---|---|
| `func_ovlN_801CC5D0(object, x, y, z)` (`func_00120CF0`) | move to a point: clears pursuit, writes the destination and its node (grid from the world bounds at `0x801F0DB8..0x801F0DC4`), sets the order and marching flags, clears "inside the headquarters". Floats travel in `$a1..$a3` (o32, first argument a pointer) |
| `func_ovlN_801CD004(object, x, y, z)` (`func_00121724`) | the same with one more step (to read) |
| `func_ovlN_801D44D4(object, target, mode)` (`func_00128BF4`) | pursue map object `target` |
| `func_ovlN_801D4630(object)` (`func_00128D50`) | hold: destination = current position |

Locations (R7, liberation): state u16 `0x801951EC + i*0x24` goes `0x000D` →
`0x000B` when a town is liberated (`0x04` enemy-held, `0x02` player-held,
`0x10` the headquarters); location 1 between captures 5 and 12, location 5
between 17 and 18 as unit 1 arrived. Beaten enemy units' objects read
`0x81000100` and then return to `0x00000100` (slot empty).

Battles and town events (captures 7-9, 13-14, 19, 24-27; the developer
pressed several during combat): both run in scene `0x0D`, the scene number the
dialogue engine also uses.

| byte | battle | town event (liberation scene) |
|---|---|---|
| `0x801976F8` | `0x01` | `0x02` |
| `0x801976FC` | the player's unit slot | the player's unit slot |
| `0x80197708` | the enemy unit slot (31, the boss 30) | the location index (5, 2) |

`0x8018F1C0` reads the scripted step during a town event (`0x0266`, `0x0263`)
and `0x443A` late in the boss battle. A beaten enemy's map object reads
`0x81000100` on the field afterwards, then `0x00000100` (slot empty).

The game's command menu (R4), captures 30 (menu closed) and 31 (open on
Magnus, mission 2, 146 frames apart): outside the framebuffer table the only
change is `0x801976FC` `0x00` -> `0x20` and `0x80197708` `0x00` -> `0x03`. Both
read 0 in captures 22, 29 and 30, but `0x801976FC` reads 5 in captures 32 and
33 with no menu open, so "both zero" is not a menu test; `0x801976FC` behaves
like the last unit the game acted for. Second pair (captures 34-37:
closed, closed, open, closed): across both pairs, plus the open capture 28, the
only bytes that follow "menu open" everywhere are the words `0x8011CA30` and
`0x8011CA44`: 0 in all three open captures (28, 31, 36), 1 or 2 in every closed
one. The block around them has the shape of libultra message queues (counts
beside `0x8011xxxx` pointers), so these read as the field's update queues
standing empty while its command menu has the field stopped. The overlay uses
"both zero" as "the game's menu holds the field" (R4, provisional until the
queue's owner is read statically).

Cursor and camera (R2), captures 32 (cursor resting on unit 1 out on the map at
-2.82, -15.61) and 33 (cursor on empty ground): with the cursor on a unit, the
block at `0x800E91D0` holds camera values (`70.0`, `1.333`, `1.0`, `160.0`,
`2.0`, then the focused unit's x `-2.82` and `8.88`), not the cursor; on empty
ground it holds the cursor again (`+0x08/+0x0C` = 122, 99). The screen mapping
therefore comes from the camera: field of view 70 degrees, aspect 4:3, and the
camera's position and target, which are the next thing to read.

Town menus (captures 38-42: in a town, then its menu open while switching
entries): `0x801976FC / 0x80197708` follow the highlighted entry (`05/01`,
`23/04`, `20/03`, the same values the command menu shows), so they are the
game's menu cursor, not an open flag. The queue words are not a dependable flag
either (`0x8011CA44` reads 1 and 2 with the town menu open). R4's open flag is to
be found statically.

Liberation rule (R6), from LordlyCaliber's decompiled `func_0012FBB8`
(`func_ovlN_801DB498` here, "town takeover alignment and Chaos Frame
calculation"), revision 1 addresses:

- the town's alignment byte: `0x801951EE + i*0x24` (location record `+0x02`);
  `0xFF` cannot be taken this way (Tenne Plains' boss stronghold); bit `0x80`
  adds 40 to the score;
- the unit's alignment: the average of all its members' alignment (character
  `+0x1B`), not the leader's alone;
- `score = |unit average - town alignment| + rand() % 8 + 40 if bit 0x80`;
- Chaos Frame (`0x801936C9`, clamped 0..100) changes by -5 (score >= 141), -4
  (>= 111), -3 (>= 81), -2 (>= 51), -1 (>= 31), 0 (>= 16), +1 (>= 6), +2;
- the routine returns "bad" for score >= 31.

Captures 5 and 38 read Tenne Plains' towns 50, 52, 38, 45, 71 and the Chaos
Frame 50, then 52 on mission 2. The town target card (section 3.4) shows this
rule with the actual numbers.

Field items (R11), captures 43-50 (leading up to an item use; used between 49
and 50): the item comes from the army-wide consumables list `0x80193AE0` (40
entries `{u16 id, u8 in use, u8 count}`): Power Fruit (id 4) went from 9 to 8
and its in-use byte from 1 to 0. Field consumables are therefore one army
stock, not per unit. The target was unit 0: its five members (characters 1-5)
each changed `+0x32` from `0x27` to `0x13`, and the unit record changed
`+0x0D` 4 -> 2 and `+0x0E` 2 -> 0; the meaning of those fields is still open.
The game's menu count is at `0x801F0BCC` (9 -> 8). Consequence for 3.6b and the
mockup: the group item menu lists the army's stock and asks for a target unit;
selection cards show each unit's equipment, not carried consumables.

A battle the enemy loses (captures 51-55, unit 0 against enemy slot 33,
scene `0x0D`, `0x801976F8` = 1, `0x80197708` = 33; battle state pointer
`*(0x801CE8DC)` = `0x801A1AE0`): character records keep **maximum HP at `+0x16`
and current HP at `+0x18`** (player characters 93/85 and 79/68, the enemy 78/57
as the battle runs; LordlyCaliber's save table names offset 22 hpMax and 24
hpCur). The port read them the other way round in the Organize screen and the
War Director's intensity; both were corrected the same day. The result on the
field after this battle is not in the set (the last capture is still in battle).

The in-game clock (captures 56-57, taken one in-game hour apart, checked against
every field capture): u32 at `0x800E7D34` (copied at `0x800E7D38`), in-game
hours since the campaign began in 16.16 fixed point (hour 240 at capture 1, 651
at 22, 1835 at 57), rising about one hour per 120 game frames. The War Director
runs on it from 2026-10-30 (its earlier "director hour" was a 3600-frame
stand-in, 30 times an in-game hour).

Battle result and retreat (captures 58-76: a battle against enemy slot 35 to its
end, then the field): the loser that survives stays on the map with its unit
record and members (HP 38/86, 65/79, 52/77) and gains `0x81000000` in its object
flags (`0x81041133`, then `0x89040113` while it is pushed back, `0x08000000`
added); its destination is rewritten as it is pushed and then resumes. A unit
destroyed outright reads `0x81000100` (no `0x11`) and then `0x100`, its record
empty (captures 12-15). The War Director's spoils roll only on the second case.

The capture checklist is complete except what comes from the code: the game
menu's open flag (R4) and town entry's routine (R10).

Dispatch (captures 1-5 of 2026-10-30 04:08, one per step of a Dispatch): the
field mode word `0x801F0E00` follows the menu, `0` on the field, `0xC` in the
dispatch list, `9` at the confirm, `4` after it; the menu cursor bytes
`0x801976FC/0x80197708` read `0x24/0x00` in the list. After the confirm the
dispatched unit (slot 1) changes from undeployed (object flags `0x138`, unit
record `+0x01` = `01`) to waiting in the headquarters (`0x00800139`, `+0x01` =
`05`), the state capture 5 of session 1 showed for the opening units; every
map object also gains `0x00100000` while the menu stands. Found the same day with an `OGRE_COVER` census before and after one Dispatch
(live console `cover` on key 8; 77 functions entered for the first time, 31
entered one to three more times): the R menu's Dispatch entry is
`func_ovlN_801B7A64` -> `func_ovlN_801B8B90`, which builds the list with
`func_ovlN_801B87A0` (units with object flags `& 0x19 == 0x18`), allows 10 units
out (counting `& 0x31 == 0x31`), sets mode `0xC` and hands the list to the list
UI `0x80205DE8`. The confirm is a case of the field state handler
`func_ovlN_801B9694` (0x801BA018..0x801BA2E0): the chosen slot from
`0x801F0E20`; `func_ovlN_801DA360(-1, &xyz)` gives the headquarters' position;
the object gets the position, its node, flags `|0x21` without `0x4` and `0x200`,
`func_ovlN_801D5228` (record `+0x01 |= 4`), flags `|0x00800000` and
`func_ovlN_801B551C` (the location it stands in, `+0x74`); a legion repeats the
placement for its divisions; the mode goes to 4. `func_ovlN_801CC4A0(object, x,
y, z, inside)` is the same placement as a routine (the opening units at
mission start; its count did not change during the Dispatch). The overlay's
Dispatch (top bar, a list of the units that can go out) runs
`801DA360`, `801CC4A0` with inside = 1 and `801B551C` on the game thread;
legions still go through the game's R menu.

The game's Settings (developer captures 15-46 of 2026-10-30 09:47, one row
changed per capture): the screen is bank V (`func_ovlV_80214FA0`, field mode
`0x20`), and its switch (0x80215580..0x80215668) writes each row's choice to one
global. Message speed: halfword `0x800E9C0C` from the table `0x80219574`
{256, 512, 1536} (Slow, Normal, Fast). Cursor speed: halfword `0x800E9C0A` =
(4 - choice) x 128 + 256 (Slower .. Faster). Bytes: Help display `0x800E9C10`,
Icon name display `0x800E9C0E`, Game speed `0x800E9C13`, Legion indicator
`0x800E9C14`, Destination display `0x800E9C15`, Unit report type `0x800E9C16`
(0 None, 1 Third Person, 2 First Person), Battle action name `0x800E9C0F`,
Battle animation `0x800E9C12`, Quick exit `0x800E9C17`, Cancel all `0x800E9C18`
(Off 0 / On 1, all On by default), Sound `0x800E9C11` (Stereo / Mono, with
`func_800EA8CC`). The labels and choice words are in the module's data
(`0x802192C0..`), the help lines in the block it decompresses
(`0x802B5280..`). The Esc menu's GAMEPLAY tab has a GAME SETTINGS section with
every row except Sound (the AUDIO tab owns sound), reading and writing these
globals; the selected row's help line replaces the menu's key hint (the port's
own GAMEPLAY rows got tooltips too). In MODERN mode, the game's Settings on the
field is closed with B and the port's menu opens on GAMEPLAY. Not played. Open:
the title menu's sound entry (scene `0x04`, title state 14, the cursor word
`0x801BA554`; captures 53-55) is still the game's.

Camera (R2, found 2026-10-30 with an `OGRE_COVER` census around the R menu's
Mission Objective, whose entry `func_ovlN_801B85EC` loads a module from ROM
`0x171EC0` and calls `0x802173D0(1)`): `func_ovlN_801B8D64` flies the camera to
a point. Arguments (caller 0x801B6CE0): `$f12` x, `$f14` y, `$a2` z, `$a3` the
camera `*(0x8018F5AC)`, and at `sp+0x10` the field mode to return to (kept at
`0x801F0CC4`); it sets `0x801F3630` = 1, which holds the field during the
flight. The overlay calls it on the game thread (field mode 0, no flight under
way, return mode 0) for idle cycling (`.`, `,`, Next idle unit) and a double
click on a strip tile. Not played. Still open: alert jumps.

Field items (captures 1-15 of 2026-10-30 09:40, a Power Fruit and a Quit Gate
used on Dio's unit, slot 3): items are used from what a unit carries. The R
menu's item entry `func_ovlN_801B7A88` (the handler before Organize) loads the
same module as Organize and sets field mode `0x16`, the game's item screen (a
one-unit view of the unit in `0x801F0E20`; its cursor is `0x801976FC/0x80197708`).
Using the Power Fruit (item 4) lowered its stock record from 7 owned / 1 in use
to 6 / 0, cleared it from unit record `+0x0D` and set character `+0x32` of all
five members from `0x0B` to 0. Using the Quit Gate (item 8) took it from
`+0x0F` and the stock (1/1 to 0/0), and the unit's map object was reset to the
headquarters on the next capture. The Group Items panel's "Use an item" button
calls `801B7A88` on the game thread (field mode 0 only), so the item is picked
and used in the game's own screen; choosing the item from the panel itself
needs that screen's cursor layout.

Pursuit flag (captures 1-3 of 2026-10-30 04:33, Lisa and Nathan camping,
Holly moving): units the overlay had ordered to pursue or stop kept object flag
`0x2000` with no target (`+0x84` = -1), marching flags `0x0004213B`, and stood
re-planning in place; a right-click move did not free them. `func_ovlN_801D44D4`
(pursue) and `func_ovlN_801D4630` (hold) set `0x2000`; `func_ovlN_801CC5D0`
(move) does not clear it; `func_ovlN_801D45F8` (end pursuit) does, and the
game's own commands clear it before a new order (0x801BAEAC). The overlay now
calls `801D45F8` before every move and pursue. Not caused by the field speed
setting.

Unit reports (captures 1-3 and 8-12 of 2026-10-30): the field stops in mode
`0x2D` (`0x801F0E00`) whenever a unit reports, with the unit's slot in
`0x801F0E20` and the message id in `0x801F36F0`, and waits for A. `0xD8` is
the camping report (five stops in six seconds, units 3, 4 and 7); `0xBF` came
from Lisa's unit and is not identified. About 20 sites in bank N enter the
mode. The setting UNIT REPORTS (CONTROLS tab, `unit_reports`, LOG by default)
logs each stop in the alert log ("Unit 4 (Dio) is making camp", other ids with
their number) and holds A for two polls once per stop, so the game closes the
report through its own code; A is pressed again only after the mode has left
`0x2D`, so it never reaches the field and a unit's command menu. PAUSE keeps
the game's stops. An extra A pressed by hand just after a report closes reaches
the field and opens the command menu of the unit under the cursor, which is the
likely path of the developer's "somehow opened the unit command menu".

Town capture (same coverage run, censuses 3 and 4 around one town taken; a
War Director spawn and a battle fell in the same window): the only field-bank
function entered for the first time is the liberation rule
`func_ovlN_801DB498`, called from the field state handler (0x801BA450) and two
other sites (0x801C5D40, 0x801C35E8); `func_ovlJ_801DA9F4`, `801DEEE0` and
`801DF094` are bank J (the battle). Taking a town needs no extra routine: a
unit that enters the location runs the game's own takeover, which the
overlay's move order already reaches. Town entry for the shop and witch's den
(R10) is a different action and needs its own census.

Carried items (2026-10-30, from `func_ovlN_801D6238`, the destroyed-unit
cleanup): unit record `+0x0D` onward holds one consumable id per byte (the
count from `func_801FE1B0`); a destroyed unit's items go back to the stock by
lowering each item's in-use byte. The item-use captures 43-50 fit this:
record `+0x0D` 4 -> 2, `+0x0E` 2 -> 0 (Power Fruit used, the next item moved up).

Screen projection (R2), from the code: the unit pick `func_ovlN_801B8070` puts
the cursor on a unit with `func_801A9DD0(camera, &world xyz, &screen x, &screen
y)` (`0x801B81AC`), camera = the word at `0x8018F5AC`, results in framebuffer
pixels (it stores them in the cursor state's `+0x08/+0x0C`). The overlay calls
it for every unit on the map on each field update (game event 2, the field
update's dead locals at `sp+0x10/0x14` as outputs) and draws a marker there
(`app/features/field_view.cpp`, 2026-10-30, not played).

Still open from the checklist: the screen
projection (R2), town entry (R10), item use (R11), the clock, the battle scene
and its result, retreat. Presses for those are still needed.

Before press 1, let the mission's opening dialogue finish and **send units out
of the headquarters** so they stand on the map: a unit still inside the
headquarters cannot take a Move order. Session 1 (2026-10-30, 14 presses) was
taken before any unit left the headquarters: captures 1-4 show the five units
undeployed (map object flags `0x138`, position 0,0), capture 5 shows them
waiting inside the headquarters (object flags `0x00800139`, object flag `0x01`
hidden, unit record `+0x01` bit `0x04`, all at the headquarters' position), and
6-9 and 13-14 are the opening dialogue (scenes `0x02` then `0x0D`). No order,
march, battle or liberation is in that set.

Presses 1 to 20 come from one session on any mission with a few towns left to
liberate; Tenne Plains works. Order matters (the files are numbered by press);
if a moment is missed, press 7 anyway at the next chance and say which number
it was.

## 5. Phases

| phase | content | done when |
|---|---|---|
| 0 | R1..R9 answered from the captures; final key table | every row has an address and evidence |
| 1 | Field hook and overlay; army strip (read-only); idle cycling; alerts **Started 2026-10-30, not played**: the army strip (`app/features/field_view.cpp`, `ui/field.rml`), drawn by RmlUi on scene `0x03` only, one tile per player unit out on the map (object flags `0x11`): unit number, leader class, MARCH / IN TOWN / IDLE, the average alignment of its members with a marker, an HP bar (current `+0x18` over maximum `+0x16`); no input. The alerts layer (`alerts_view.cpp`) is shared with the War Director. A click on a strip tile selects that unit (Shift adds or removes); the tile rectangles are read from RmlUi each frame as fractions of the window. Idle cycling selects without moving the camera: `.` the next player unit standing on the open map (out, not marching, not in a location), `,` the previous one. Not built: the camera jump on cycling, double click and alerts (camera write not found, R2), pad bumpers | the strip matches the field during a played mission |
| 2 | Direct orders for one unit (all three inputs) **Started 2026-10-30, not played** (mouse only, `app/features/field_select.cpp`): right click with units selected orders them: on an enemy marker, pursue (`func_ovlN_801D44D4`); elsewhere, move to the ground point under the pointer (`func_ovlN_801CC5D0`; the point found by inverting the game's projection with Newton's method, height from the first unit). With nothing selected, right click is the game's own B. Right click on a location marker sends the units to the location's own world point (`map_data + 0x1C + i*8`), so they enter it. Not built: keyboard and pad orders | right click, Enter or A moves a unit to ground, a town and an enemy |
| 3 | Selection: box, circle, Shift box, legion rule, selection bar **Started 2026-10-30, not played** (mouse only): left click on an own unit's marker selects it, Shift adds or removes, a left drag draws a box and selects the own units inside, Backspace clears; a left click on nothing is replayed to the game as its own A. Selected markers are filled. The selection bar along the bottom shows one tile per selected unit. Location markers (blue: held, orange: enemy, outline: neutral) are projected each field update from `0x801F1090` and the map data. With units selected, the pointer on an enemy or neutral location shows the target card: per unit, the average alignment against the town's, the gap (plus 40 for bit `0x80`), the verdict (gap 31 or more captures; below 24 always liberates; between, the game's random 0..7 decides) and the Chaos Frame change range (R6). Not played. Not built: the growing circle, the Shift box on the keyboard, the legion rule, location names | the bar shows the right units for any selection |
| 4 | Group orders, group commands, Enter town, group items, path preview | three units liberate a town from one order; a shop opens with right click; a group item is used |
| 5 | Waypoints and patrols | a two-point patrol repeats through a battle |
| 6 | Camera, pause, alignment warnings, settings, first-time hints | the whole flow works with only a pad, and with only a mouse |

## 5a. Full steps

Each step names who does it (D: the developer, A: the agent), what it
produces, and how it is checked. A step starts only when the one before it is
checked.

**Phase 0: find the game's mechanisms**

1. D: one capture session (section 4, presses 1..15).
2. A: diff press 1 against 3 and 4 with `tools/rdram.py diff`; the bytes that
   change in the unit's record and map object are the order and the march
   state. Find the code that writes them (bank N) and name the order routine
   and its arguments (R1, R5).
3. A: from presses 5 and 6, the destination forms for a town and an enemy
   (R1).
4. A: from press 2, the command menu's state and the "acting unit" (R4).
5. A: from press 7, the battle state and its start event (R5, R7).
6. A: from presses 8..11, the reputation change and the alignment rule (R6),
   read in the liberation code so the thresholds are the game's.
6a. A: from presses 12 and 13, the town-entry routine and its arguments (R10).
6b. A: from presses 14 and 15, where carried items live and the field
    item-use routine (R11).
7. A: arrival (R8): the march state returning to idle at the destination in
   presses 3/4 and the code that clears it.
8. A: projection (R2): the sprite draw for map objects, checked against the
   game's own unit pick (`func_ovlN_801B8070`), which already lands on units.
9. A: pause (R9): whether the field update can be held while the overlay
   still runs (the Esc menu's frame gate, `app/src/overlay.cpp`, is the
   starting point).
10. A: fix the key table (section 2) against `app/src/input_map.cpp`; record
    every answer here with addresses and the capture that shows it.
11. D, later: the legion capture (R3) once legions unlock.

**Phase 1: overlay and information**

12. A: a field hook (`tools/director_hook.py`, beside `ogre_director_spawn` at
    `0x801B258C`) that reads every unit's slot, status, position, strength,
    leader and alignment each frame into a shared snapshot.
13. A: `app/features/field_view.cpp` + `app/ui/field.rml`: an RmlUi overlay on scene
    `0x03` (the F3 Organize screen's machinery), drawn in-window, that takes
    input only over its own panels.
14. A: the army strip (section 3.3); tile click selects, double click jumps
    the camera (camera write found in step 8).
15. A: idle cycling (`.`/`,`, RB/LB).
16. A: alerts from the events of step 5/6, clickable, with a log.
17. D: play one mission and compare the panel with the field.

**Phase 2: direct orders for one unit**

18. A: a game-thread order queue (as `ogre_organize_frame` does for Organize)
    that calls the order routine from step 2.
19. A: the context order (ground, town, enemy, own stronghold) on right
    click, Enter and A, with the game's own pick deciding the target.
20. A: input arbitration with the game's command menu (step 4) so one press
    never does both.
21. D: send one unit to ground, a town and an enemy with each input.

**Phase 3: selection**

22. A: drag box, Shift box, growing circle (hold X), using the projection from
    step 8; the legion rule once R3 is known (until then, legions are left
    out of multi-select).
23. A: the selection bar: cards with carried-item icons (R11), deselect,
    B clears, non-colour markers.
24. D: check selections of 1, 2, 5 and 10 units with each input.

**Phase 4: group orders**

25. A: one order to every selected unit through the queue; group commands
    (Move, Stop, Return, Battle Strategy, Patrol toggle).
25a. A: Enter town: right click / E / Y menu on the town a selected unit
     stands in, through the routine from step 6a.
25b. A: the group item menu (section 3.6b) through the routine from step 6b.
26. A: path lines and destination flags.
27. D: three units liberate one town from one order; enter a shop with right
    click and with E; use a potion carried by another selected unit.

**Phase 5: waypoints and patrols**

28. A: per-unit route lists in the port, advanced on arrival (step 7);
    patrols loop until a manual order or a rout.
29. D: a two-point patrol keeps running through a battle.

**Phase 6: camera, pause, polish**

30. A: edge scroll, middle drag, WASD, right stick pan, wheel zoom; Space
    pause (step 9).
31. A: the town target card (section 3.4: verdict, arrival order, warning,
    the two fixes) from the rule in step 6; settings (edge
    scroll, circle hold/toggle, alert types); first-time hint card.
32. A: docs: `docs/guides/controls-and-settings.md`, `docs/anotherworld/DECISIONS.md`, a
    `FEATURES.md` entry.
33. D: one mission with only a gamepad and one with only a mouse.

## 6. Proposals awaiting the developer

- Circle on hold X; an enemy target means attack; marching units keep
  marching until a new order; control groups wait until after phase 6. These
  are proposals, not decisions.

## 6a. Mockup parity (2026-10-30, not played)

The developer compared the build with the mockup and found it far apart. The
overlay (`app/features/field_view.cpp`, `app/ui/field.rml`) now follows the mockup's
layout where the data is known:

| mockup element | built from |
|---|---|
| top bar: day and hour, Chaos Frame bar, Goth, Speed, Organize, Next idle unit | clock `0x800E7D34` (16.16 hours), `0x801936C9`, `0x80196A8C`, `cycle_game_speed()` |
| strip tile: the leader's portrait (`portrait:<class>`, the Organize view's source) with the number on the unit's colour, MARCH / LOW / IN TOWN / IDLE, alignment marker and value | object flags, member HP (LOW below 35%), member alignment average |
| numbered discs on the unit's colour, ring and order badge when selected; dark E discs for enemies | the projection (R2) |
| location squares: blue held, rust enemy, light neutral | location state |
| path dots from a marching unit to its destination, bright for the selection | object `+0x28..+0x30` through the same projection |
| move-to card: verdict badge, the rule, the selected units in arrival order with alignment and verdict, the warning when a later unit captures | R6; arrival order is straight-line distance |
| selection bar: count, cards (the leader's portrait on the unit's colour, Unit N and the leader's name, class and level, HP bar, alignment with the verdict for the town under the pointer), click to drop | character `+0x00` name, `+0x11` class, `+0x13` level |
| command grid: Move, Stop (Delete), Return (H), Strategy, Patrol, Enter town, Use item (I) | Strategy cycles the four strategies for the selection, written to unit record `+0x17` (where the Organize commit puts it); Patrol and Enter town are greyed |
| Dispatch button and list (top bar): units that can leave the headquarters, sent out by a click | the game's placement `func_ovlN_801CC4A0` after `func_ovlN_801DA360(-1)`, the 10-unit limit; not legions |
| location names on the markers and the move-to card | location record `+0x08`, 20 bytes ASCII (captures 14-15: Boolem, Movae, Kiefer, Baldera, Fort Tuatha; empty for the headquarters) |
| Group Items list (Use item, I) | the army stock `0x80193AE0`, names from the consumable table `0x8018E6EC`; listing only until R11 |

Developer feedback the same day, built (not played): the strip stops above
the selection bar and scrolls (mouse wheel over it, Page Up / Page Down; tiles
scrolled out are not clickable); tiles and cards label the alignment ("ALN",
"Alignment"); each card has an activity line (chasing, marching to a named
location from object `+0x28/+0x30`, in a location from `+0x74`, holding, idle;
the strip shows CHASE and HOLD from flag `0x2000` and the target `+0x84`) and
the unit's carried items (unit record `+0x0D..+0x16`, the game's menu icons,
one per kind with a count). Enemies without object flag `0x20` (not shown by the
game yet; garrisons read `0x111` until seen, then `0x131`, captures 12/14) get
no marker and cannot be targeted. Not built: scrolling the strip from a pad;
naming the chased enemy (enemy members are in their own table `0x80195580`).

Marker offset (developer report 2026-10-30): the markers were projected in
the field update (game event 2), which runs before the camera pan and not at
all while the field is held, so they trailed the camera and froze during
reports. Game event 4 (FieldFrame, `config/hooks.toml`, the exit of the camera
pan `func_ovlN_801BECF0` at 0x801BEF60, once per frame) now drives the
projection. Same round: an HP bar on strip tiles and "HP current / max" on the
cards; units still in reserve listed below an IN RESERVE divider in the strip,
greyed, a click sending them out (Dispatch); Group Items lists what the
selected units carry (pooled, with who carries each) instead of the army stock.
The field's Organize button and F3 on the field now open the game's Organize
through the R menu's own entry `func_ovlN_801B7B2C` (the second entry of the
menu's handler table; an `OGRE_COVER` census around opening Organize from the R
menu, run 9 of 2026-10-30, entered it once between the presses, and the next
pair shows bank S starting). It loads the transition module (ROM `0x1A9260` ->
`0x80214FA0`), sets field mode `0x15` and the hold bit `0x01` of `0x801F367D`;
the overlay calls it on the game thread only in field mode 0. The port's view
then opens over the game's screen with editing, including units not
dispatched.

The overlay is hidden and takes no input with PORT MENUS = CLASSIC.

Still missing from the mockup, each waiting on a game mechanism:
- Enter town: R10. Use item and the Group Items panel: R11 (the item-use
  routine; the stock list itself is known, `0x80193AE0`).
- Jump, double click and alert jumps: the camera write (R2).
- Pause: R9. Patrol and waypoints: an arrival event.
- Travel times on the move-to card: the march speed.
- Carried-item icons on the cards: field consumables are army-wide (R11
  finding), so the cards show none.
- Gamepad circle and keyboard box selection.

## 7. Decided from the battlefield mockup (2026-10-29)

Mockup:  (boards: mouse and
keyboard sending to a town, mouse and keyboard using group items, gamepad
circle).

- The compact left strip replaces the wide army panel on every input, with
  alignment on each tile (3.3).
- The town target card states liberate or capture and the reason from the
  leaders' alignment and arrival order (3.4).
- Selection cards show carried items as icons; Use item pools the selected
  units' items (3.6b).
- The order is named Move. Stop stays.
- Town interaction (shop, witch's den, stronghold) is an Enter town command
  (E) and a right click on the town the unit stands in; on a pad it is in
  the Y menu (3.1).

## Chase and pause on select (2026-10-30)

- The game's pursue `0x801D44D4` does not move a player unit: in mode 0 it leaves the unit's destination on itself, and the unit marches in place (developer report). The overlay's chase replaces it. `field_select.cpp` keeps a (chaser, target) list, and every 24 field updates it calls END_PURSUIT `0x801D45F8` and then MOVE_TO `0x801CC5D0` toward the target's position (object `+0x08/+0x10`). The chase ends when the target leaves the map, stops being shown (flag `0x20`) or is beaten, and an alert says so. Any new order for the unit, Stop included, ends its chase. The card shows "Chasing an enemy unit", and the strip shows CHASE.
- PAUSE ON SELECT (CONTROLS tab, ON by default, MODERN only): a new selection on the open field (mode 0) closes the frame gate that the in-window menu pauses with (`overlay.cpp`, `ogre_frame_gate` after the buffer swap at `0x8008B120`). A command opens it again: the order waits in the queue, and the game thread runs it on the first field update after the gate opens. The unit stays selected. An empty selection also opens the gate. The gate has two holders (the menu and the field), so closing the Esc menu does not end a field pause. Not played.

## Pointer: always visible, kept in the window (2026-10-30)

- Developer report: the mouse "disconnects" while the overlay still reacts, and does not feel smooth like StarCraft 2. Cause: `sdl_platform.cpp` hid the operating-system pointer over the game window on every screen except the MODERN field, but a game cursor follows the pointer only on the world map (scene 0x05, `map_cursor`) and the CLASSIC mission field (scene 0x03, `mission_cursor`). In battles, menus, shops and the title the pointer vanished and nothing followed it.
- Now the pointer is hidden only on those two screens; everywhere else it stays visible.
- KEEP POINTER IN WINDOW (CONTROLS > MOUSE, ON by default, `Choice::ConfinePointer`): `SDL_SetWindowGrab` while the game window has input focus and the Esc menu is closed, released when the menu opens or the window loses focus. Pushing against an edge scrolls instead of leaving the window. Not played.
- RTS edge pan (2026-10-30): the D-pad pulses from the pointer edge are gone. `field_select.cpp` `pan_camera` runs on FieldFrame (0x801BEF60, the exit of the game's scroll `func_ovlN_801BECF0`) and moves the camera at `*(0x8019F5AC)` itself: eye `+0x14`/`+0x1C` and look-at `+0x20`/`+0x28` by the same offset (screen right +x, screen up +z), the look-at point clamped to the world rect `0x801F0DD8..0x801F0DE4`. The velocity eases to the pointer's push in about 0.1 s; the speed is 1.2 camera distances a second in real time. Open field only (mode 0). Not played.

