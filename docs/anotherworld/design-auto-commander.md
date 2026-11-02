# Design: the auto commander — an AI that plays the player's side of a mission

Status: phases 1, 2 and 6 built, 2026-10-31 (`app/features/auto_commander.cpp`),
not yet played. Section 10 says what is built and what waits on research.
Section 9 lists the questions still open.

## 1. Goal

An optional AI takes command of the player's units on the mission field
(scene `0x03`). It:

1. walks units over the map's hidden-item spots and picks the items up;
2. takes every town, liberating or capturing each one according to the
   ending the player chose;
3. moves on the boss's stronghold once the towns are done;
4. keeps every unit alive: it uses healing items, rests a hurt unit in a
   held town until it recovers, and retreats a unit before it is routed;
5. sends a unit into a shop to buy healing items when the army's stock runs
   out.

The player can turn it on for the whole army or for chosen units. A move,
pursue or Return the player gives a unit takes that unit from the AI until the
order finishes; a Stop or the unit card's AI button takes it until the player
hands it back (maintainer, 2026-11-01).

Testable checks:

| check | the player sees | checked by |
|---|---|---|
| Collection | every hidden item on the map is picked up | table P's found bits (`0x80196A58`) all set at mission end |
| Ending target | towns change hands the way the chosen ending needs | Chaos Frame (`0x801936C9`) moves the chosen way over a mission |
| Survival | no unit is routed or loses a member to a fight it chose | the mission report; deaths counted from character HP |
| Restock | the army never sits at 0 healing items while goth allows a purchase | army stock `0x80193AE0` and goth `0x80196A8C` over a mission |
| Story order | no story scene fires out of order | the developer's playthrough of the first missions |
| Readability | each AI decision appears as an alert with a reason | alerts log |

## 2. What is built and is reused

The player-side AI has the same shape as `app/features/enemy_commander.cpp`,
which already gives jobs to units on a decision interval and moves them
through the game's own routines. The auto commander reuses its structure and
these pieces:

| piece | where | used for |
|---|---|---|
| move a unit: `func_ovlN_801CC5D0(object, x, y, z)` | `field_select.cpp` `MOVE_TO` | every move order |
| hold: `func_ovlN_801D4630(object)` | `field_select.cpp` `HOLD` | resting, waiting for a group |
| end pursuit: `func_ovlN_801D45F8` | `field_select.cpp` | dropping a chase |
| pursue an enemy (the overlay's chase) | `field_select.cpp` | attacking a unit |
| Dispatch a unit slot, unit count and limit | `field_select.cpp` `can_dispatch`, `units_out`, `field_limit` | putting units on the field |
| map objects per unit slot | `kMapObjects` `0x801F0CD0` | positions, destinations (`+0x28/+0x30`) |
| unit records | `kUnits` `0x80197210` | members, Battle Strategy `+0x17` |
| characters | `kCharacters` `0x80193BE0` | HP, level, alignment (offsets to confirm) |
| location count and state | `kLocationCount` `0x801F1090`, `kLocationState` `0x801951EC` | which towns are held by whom |
| Chaos Frame | `kChaosFrame` `0x801936C9` | the ending target's measure |
| goth | `kGoth` `0x80196A8C` | the shopping budget |
| army consumable stock and names | `0x80193AE0`, `0x8018E6EC` (`design-field-controls.md`) | what can be used, what to buy |
| table P, the map's hidden-item list | `0x801F1002` count, `0x801F1004` ids, `0x801F1024/34/44` byte arrays, found bits at `0x80196A58` (`docs/notes/drop-table.md` §2) | where the hidden items are, which are found |
| enemy units seen on the field | `enemy_commander.cpp`, `war_director` | threat map |
| alerts, game events | `alerts.hpp`, `game_events.hpp` | decision log for the player |
| dialogue autoplay | `dialogue_autoplay.cpp` | town and event text while the AI drives |
| field speed | `field_speed.cpp` | long AI stretches |

## 3. The decision loop

Every decision interval (in-game hours, as `enemy_commander.cpp` does it), on
the field update (game thread), the AI:

1. reads the field: every player unit's position, HP fraction, destination
   and state; every seen enemy unit; every location's owner; the army's
   item stock and goth; table P's unfound entries;
2. gives each AI-controlled unit one job, in the priority order of section
   4. A unit keeps its job until the job finishes or a higher-priority job
   claims the unit;
3. issues orders only for units whose job changed or whose target moved;
4. raises an alert for each new job ("Gryphon squad: resting at Alba, 42% HP").

The AI never acts while a game menu is open (R4 in
`design-field-controls.md`), and never on a unit that is following a player
order (section 6).

Decided by the developer (2026-10-31):

- The AI commands only the units the player sent out. It never dispatches a
  unit.
- It keeps a reserve of **5000 goth**: it buys and revives only with goth
  above that.
- It enters towns that start a story scene. `dialogue_autoplay.cpp` carries
  the scene, and the AI resumes on the field after it.

## 4. Jobs, highest priority first

| job | trigger | what the unit does |
|---|---|---|
| **RETREAT** | HP below the retreat line (default 30%), or a stronger enemy unit within reach | moves to the nearest held town away from the threat; becomes REST on arrival |
| **HEAL** | HP below the heal line (default 50%) and the army has a healing item | uses the item on the unit (R11); the cheapest item that covers the missing HP first |
| **REST** | HP below the rest line and no item, or after RETREAT | holds in the nearest held town (or the headquarters) until HP reaches the resume line (default 90%). Units recover HP everywhere, and faster in a town (developer, 2026-10-31), so a town is worth a detour when one is close; a unit far from any town holds where it is |
| **RESTOCK** | army healing stock below the restock line and goth above the reserve | the nearest healthy unit enters the nearest held town with a shop (R10) and buys items (R12) |
| **REVIVE** | a dead member and a known Witch's Den held by the player, and goth above the reserve | the dead member's unit enters the Den (scene `0x14`) and revives its dead (developer, 2026-10-31: yes) |
| **COLLECT** | an unfound table P entry within the collect radius of a free unit, and no enemy closer to it than the unit | moves the unit onto the spot; the game's own pick-up runs (`func_ovlN_801B9694`, `0x801BB8B8`) |
| **DEFEND** | a seen enemy unit heads for a held town or the headquarters | the nearest strong enough unit intercepts |
| **TAKE TOWN** | an unheld town | the unit whose leader suits the ending target (section 5) goes to it |
| **BOSS** | all towns held (or the developer's rule, section 9), every unit above the resume line | the strongest units converge on the boss's stronghold together; the others guard |
| **GUARD** | nothing else | holds at the headquarters or the most threatened held town |

"Stronger enemy" uses the threat score `enemy_commander.cpp` already keeps
(level times member count, adjusted by HP), compared with the unit's own.

Collection runs between other jobs and is cheap. A unit picks an item up by
walking over the spot (developer, 2026-10-31), so the AI routes a unit's path
through nearby spots on the way to its other jobs, and does not need to stop
on them. Each hidden spot is visited once; table P's found bit tells the AI
the item was taken, so a revisit never happens. If table P's byte arrays do not hold the spot's position (R13), the
AI cannot collect, and the job stays off.

## 4a. Lost fights and enemies stronger than one unit

A unit that loses a battle is knocked back and hurt. The AI treats a loss as
information about the enemy, and answers a strong enemy with several units in
a planned sequence.

**Before a fight: the odds.** For every enemy the AI keeps a power estimate:
the sum over members of HP times level, read from the enemy character records
(`kEnemyCharacters`), plus the extra members `boss_escorts.cpp` adds (the
field does not show them; the AI reads that file's table). It compares this
with the unit's own power and with the time of day for classes the day and
night change (R18). A unit attacks alone only when its power is above the
enemy's by the margin (default 1.2×). Otherwise the enemy gets an ASSAULT
plan.

**After a loss.**

1. The loser goes to RETREAT, then REST.
2. The enemy is marked "too strong for one unit", and its power estimate is
   corrected from the battle's result: the damage each side dealt, read from
   the participant table (`battle_ai.cpp`: `*(0x801CE8DC) + 0x1C4 + n*0xF8`,
   HP `+0x20/+0x22`) at the end of the battle (R16).
3. The AI does not send the same unit again until it is rested and the
   estimate says the odds changed.

**ASSAULT: a relay of units on one enemy.**

1. **Stage.** The AI picks a held town near the target as the forward base.
   The assault units gather there, rest to the resume line, and use healing
   items first.
2. **Waves.** Units attack one after another, a few in-game hours apart, so
   the enemy has no time to recover between battles. Damage carries over
   between battles on the field (to confirm, question 10).
3. **Battle Strategy per wave** (unit record `+0x17`, already written by
   `field_select.cpp`; the index-to-name mapping is R17). The first waves use
   "attack the strongest" or "attack the weakest" to strip members; the last
   wave uses "attack the leader", because a defeated leader routs the unit
   and a defeated boss ends the mission.
4. **Rotation.** A wave unit that drops below the retreat line goes back to
   the forward base to rest, and the next rested unit takes its place. The
   relay continues while the enemy's HP falls between waves.
5. **Time of day.** If the AI's units are stronger at night or by day than
   the target is (R18), the assault waits for that time.

**The AI never gives up** (developer, 2026-10-31). When three waves in a row
take off less than 10% of the enemy's HP, the assault is not cancelled; the
AI changes how it attacks, one step at a time, and goes back to the waves
after each step:

1. **Change the Battle Strategy** of the next waves (a wave that attacked the
   leader switches to the weakest members, and the reverse).
2. **Wait for the better time of day** for the army's classes (R18).
3. **Bring more units.** Units guarding towns that no enemy threatens join
   the relay.
4. **Restock and revive.** The army buys healing items and revives its dead
   at the Witch's Den (above the 5000 goth reserve), so every wave starts
   full.
5. **Train.** The weakest assault units fight enemy units they can beat
   (the power estimate's margin) for experience, then return to the relay.
   Free enemies come from the war director's units, which keep appearing
   while the mission lasts.

Each step raises an alert ("Boss Ogre: three waves did 6% damage; training
units 2 and 4 on weaker enemies"), so the player sees why the attack paused
and can take over with an order at any time.

The boss uses the same plan. The BOSS job is an ASSAULT on the boss slot
(slot 30 in `boss_escorts.cpp`) with every unit that is not guarding a town.

## 5. The ending target

The player picks a target in the GAMEPLAY tab: **LIBERATE** (raise the Chaos
Frame, the good-ending route), **CAPTURE** (lower it), or **KEEP** (hold it
in a band, for example 50..70).

The result depends on alignment (developer, 2026-10-31), and the field
overlay already computes it. `field_view.cpp` `verdict_for(unit, place)`
compares the unit's alignment (the average of its members' character byte
`+0x1B`) with the town's own alignment byte (`kLocationState` entry `+0x02`;
`0xFF` cannot be taken; bit `0x80` adds 40 to the gap):

- gap + 7 < 31: **liberates**;
- gap >= 31: **captures**;
- otherwise: **may capture**.

The AI uses the same function, moved to a shared place so the overlay's
verdict and the AI's choice can never disagree. The plan:

1. For each unheld town, score each free unit by its verdict against the
   target (LIBERATE wants "liberates"; CAPTURE wants "captures"; "may capture"
   is never chosen on purpose), then by distance. The best-scoring unit gets
   TAKE TOWN.
2. If no unit fits, the AI leaves the town until one does, and raises an alert
   ("no unit free that liberates Volmus").
3. After each town changes hands, record the Chaos Frame before and after, so
   a session log shows how much each town moved it.

## 6. Player control and UI

- GAMEPLAY tab: AUTO COMMANDER off / whole army / selected units; ending
  target; the retreat, heal, rest and resume lines; the gold reserve; "attack
  the boss" automatic or on the player's order.
- Field bar: an "AI" toggle on each unit card, and a badge with the unit's
  current job.
- The player takes over at any time, in two ways (developer, 2026-10-31):
  - **Turn it off.** One key and the bar's AI button turn the whole AI off;
    every unit keeps its current destination and nothing more is ordered.
  - **Give an order.** A player order on a unit (`field_select.cpp`'s move,
    pursue, hold, Return) takes that unit from the AI. The AI does not issue
    any order to a unit that is in the player's selection
    (`field_select::selected()`), or to a unit the player has ordered. A
    move, pursue or Return gives the unit back when the unit has stopped
    marching and has no chase (maintainer, 2026-11-01). A Stop or the card's
    toggle keeps it until the toggle hands it back. A new mission and turning
    the AI on give every unit back.
- The AI reads the selection on every decision, so selecting a unit is enough
  to freeze its AI orders while the player decides.
- Every decision is an alert, and the overlay draws each AI unit's target
  line in its own colour.
- The AI stops when the mission ends or a story scene starts, and resumes on
  the field.

## 7. Research needed before each part is built

These are addresses or routines the plan needs and the project does not have.
Each is answered from a developer capture (AGENTS.md rules 11 and 12, the
`OGRE_RAM_KEY` recipe in `design-field-controls.md`), then verified at
instruction level.

| # | question | blocks | how |
|---|---|---|---|
| R5 | unit state on the field: idle, marching, fighting, resting. Answered in part: HP recovers everywhere and faster in a town (developer). The rates are unknown; the AI does not need them, because it rests until a HP line | RETREAT (fighting state) | `field_view.cpp` already reads a status per unit; check it covers "fighting" |
| R6 | the liberation rule. Answered: alignment, as `field_view.cpp` `verdict_for` computes it (developer) | none | none |
| R10 | the town-entry routine and the unit it acts for | RESTOCK, REVIVE | dumps around entering a shop from the field |
| R11 | Answered 2026-10-31 for one-character items. The item screen (bank AE) uses an item in three steps: `amount = func_8016B198(0, 0, item, character)` (`0x80216844`); `func_8016B198(1, amount, item, character)` (`0x802172B0`), which for a player character adds the amount to HP `+0x18`, capped at `+0x16`, and skips a character at 0 HP (`0x8016B30C..0x8016B338`); then inline writes (`0x80216FC8..0x80217058`): `k = func_8016B738(item)` (the record in the 40-slot list `0x80193AE0`: u16 id, u8 in use, u8 count; 511 when absent), the unit's carried slot (unit record `+0x0D + slot`, slot at `0x8021E3C0`, unit at `0x8021E3D4`) is cleared and the record's in-use and count bytes each drop by one; an item from the army's stock (`0x8021E3E3` set) lowers only the count. Heal Leaf (1) is 100 and Heal Seed (2) 300 (`0x8016B1EC..0x8016B204`); Heal Pack (3, `0x80216860`/`0x80217228`) and Revive Stone/Altar (6/7, `0x80216F28`) take other paths, not yet read | Heal Pack, revive items | read those item-screen paths |
| R12 | the shop's buy routine in bank unit AB: stock list, price, goth debit, the army stock write | RESTOCK | dumps before and after one purchase; read unit AB's code |
| R13 | Answered 2026-10-31 from bank N `0x801B5848..0x801B59B0` (the field's per-unit pick-up check): `0x801F1024[i]` is x and `0x801F1034[i]` is z, in 256ths of the rectangle at `0x801F0DB8` (min x, min z, max x, max z floats); `0x801F1044[i]` is the found-flag index into the bits at `0x80196A58`; the pick-up square's half-width is `*(0x801F3A58) / 2` | none | none |
| R14 | the boss: which enemy slot is the boss and where its stronghold is; whether defeating it ends the mission at once | BOSS | `war_director` already separates mission units; confirm with one capture |
| R16 | the end of a battle on the field: which unit won, and each side's HP before and after | section 4a | `game_events.hpp` battle events; dumps around one won and one lost battle |
| R17 | Battle Strategy: which value of unit record `+0x17` is which strategy | section 4a | the developer sets each strategy in turn on the Organize screen and captures |
| R18 | day and night: the clock (`kGameClock` gives hours) and which classes are stronger at night | section 4a | the class table `kClasses`; the developer's knowledge |
| R15 | character alignment. Answered: byte `+0x1B` of the `0x38`-byte record (`field_view.cpp`) | none | none |

The shop is the hardest part. If R10/R12 show that buying needs the shop's own
menu, the AI drives that menu through its button input for the time it is
open, with the screen showing the purchase. A direct write to the stock and
goth is a fallback only if it reproduces what the shop's code writes (rule 7).

## 8. Phases

| phase | builds | done when |
|---|---|---|
| 0 | the captures and readings for R11 and R13 | each has an address and an instruction-level reference in this document |
| 1 | the loop, RETREAT, REST, GUARD, DEFEND, TAKE TOWN with the ending target (section 5), the off switch, the selection rule and alerts | on Tenne Plains the AI takes every town with the chosen verdict, no unit is routed, and a selected unit never receives an AI order |
| 2 | (merged into phase 1: the liberation rule is already in `field_view.cpp`) | a LIBERATE run raises the Chaos Frame and a CAPTURE run lowers it on the same save |
| 3 | COLLECT | every table P item on a mission is picked up |
| 4 | HEAL | units use items in the field |
| 5 | R10, R12, then RESTOCK; REVIVE if the developer wants it | the AI buys items when the stock runs out |
| 6 | the power estimate, the loss response and ASSAULT (section 4a), then BOSS | the AI beats a unit that beat one of its units alone, and finishes a mission from start to boss |

Each phase is checked by the developer playing a mission with the AI on
(memory: no scripted game runs); the AI's alert log is the record.

## 9. Questions for the developer

Answered 2026-10-31: the liberation rule is alignment, as the overlay shows
it; units recover HP everywhere and faster in towns; the player takes over by
turning the AI off or by giving an order, and the AI never overrides a
selected unit; a hidden item is picked up by walking over it; the AI revives
at the Witch's Den; it does not dispatch units; it keeps 5000 goth in
reserve; it enters towns that start a story scene.

1. (answered: AUTO ENDING = LIBERATE / CAPTURE / EITHER)
2. (answered)
3. (answered: yes; collect them all before the boss, picked up on the way
   where possible, one unit per item)
4. (answered)
5. (answered: Heal Leaf and any other healing item a unit carries; the player
   sets each unit's items in Organize)
6. (answered)
7. (answered: the boss assault starts by itself once no town is left)
8. (answered)
9. (answered)
10. (answered: enemies keep lost HP but recover it fast; eliminating a unit
    gives more experience and items; the director buys enemy units without end)
11. (answered: the AI never gives up; it escalates and trains, section 4a)

## 10. Built (2026-10-31)

`app/features/auto_commander.cpp`, native build only (it uses
`field_select.cpp`). Not yet played; the developer's playtest is the check.

| part | state |
|---|---|
| decision loop, every 2 game hours on the field update (open field only, `0x801F0E00 == 0`) | built |
| RETREAT, REST (resume at 90%), DEFEND, TAKE TOWN, GUARD | built |
| ending target: AUTO ENDING = LIBERATE / CAPTURE / EITHER, through `liberation.hpp` (the rule `field_view.cpp` shows, moved there so both use one function) | built |
| power estimate (members' current HP, scaled by `boss_escorts.cpp` extras), ASSAULT relay from a held base, Battle Strategy per wave (`+0x17`: Weakest, Strongest after step 1, Leader below 40%) | built |
| never give up: escalation steps 1 (strategy), 2-3 (relay +1 unit each), 4 (48 game hours of TRAIN), then again from step 1 | built; the time-of-day step waits on R18 |
| BOSS: an ASSAULT on slot 30 once no town is left that the army can take the way the ending wants | built (developer, 2026-10-31: automatic) |
| takeover: AUTO COMMANDER off (GAMEPLAY tab, F4, the top bar's button); a move, stop, pursue or return order releases a unit; the card's AI button hands it back or takes it; a selected unit is skipped | built (MODERN controls only; an order through the game's own CLASSIC menu is not seen) |
| COLLECT: one idle unit at a time while towns are left; one unit per item once none is left; the boss waits for them up to 48 game hours | built (R13 answered, below) |
| waves 1 game hour apart (enemies recover fast); the boss relay starts two waves wide | built |
| HEAL: a member below 60% gets a Heal Leaf or Heal Seed the unit carries (Seed when 200+ HP is missing), through `func_8016B198` and the item screen's own removal writes | built; Heal Pack and the revive items wait on their item-screen paths |
| RESTOCK, REVIVE | not built: R10, R12 |

A battle is counted from HP between decisions: an attacker near the target whose
HP fell, or a fall in the target's HP. R16 (the battle's own result) would
replace this.

Every decision is logged as `[autocmd]` on stderr; retreats, town orders,
assaults, escalations and a defeated target are also alerts.

