# War Director: a Risk of Rain 2 style director for OB64's enemy army

Status: phases 1-3 built (phase 3 untested in play). The goals, design and
phases are superseded by `design-war-director-v2.md` (2026-10-29); this
document keeps the research, the addresses and the record of what is built.

## 1. Goal

OB64's enemy army is a fixed script. Each mission has story units (bosses,
named characters, scripted ambushes) and generic units. Both spawn from fixed
templates at fixed times and walk fixed routes. Once the player knows the
mission, nothing reacts to what the player does.

The War Director replaces the **generic** part of the script with a budget. The
enemy faction earns credits over time, gets more income from the towns and
strongholds it holds, and spends the credits on units built from the classes
that mission already uses. It sends those units where they matter: to retake
towns the player liberated, to reinforce towns under threat, and to intercept
player units.

Non-goals:

- Story units, bosses and scripted events stay exactly as they are.
- The difficulty curve stays close to vanilla. The default budget equals the
  strength of the mission's own generic units, so the director changes how that
  strength is used and when it arrives, not how much exists. Higher settings add
  more on top (section 4.8).
- No new classes, sprites or maps.

## 2. How the Risk of Rain 2 director works

Source: [RoR2 wiki, Directors](https://riskofrain2.wiki.gg/wiki/Directors) and
[RoR2 wiki, Difficulty](https://riskofrain2.wiki.gg/wiki/Difficulty).

### 2.1 Difficulty coefficient

One number, recomputed continuously, drives every director:

```
playerFactor = 1 + 0.3 * (playerCount - 1)
timeFactor   = 0.0506 * difficultyValue * playerCount^0.2
stageFactor  = 1.15^stagesCompleted
coeff        = (playerFactor + timeInMinutes * timeFactor) * stageFactor
enemyLevel   = 1 + (coeff - playerFactor) / 0.33
```

`difficultyValue` is 1 (Drizzle), 2 (Rainstorm) or 3 (Monsoon). Time spent on a
stage and the number of stages cleared both raise it.

### 2.2 Director types

| director | kind | when | credits |
|---|---|---|---|
| Scene | instantaneous | stage setup | map base * coeff (monsters), separate interactable budget |
| Fast combat | continuous | from map entry | `0.75 * (1 + 0.4*coeff) * (players+1)/2` per second |
| Slow combat | continuous | from map entry | same rate, but waits 22.5-30 s between waves |
| Teleporter boss | instantaneous | teleporter starts | `600 * sqrt(coeff)` |
| Teleporter | continuous | while charging | multiplier 2.0 |

The fast director waits 4.5-9 s between waves and the slow one 22.5-30 s. Within
a wave, spawns are 0.1-1 s apart.

### 2.3 Buying

- Every monster is a **spawn card** with a credit cost, a weight and a stage
  condition. Cards sit in **categories** (basic monsters, minibosses,
  champions) that also have weights. The chance of a card is
  `category weight / total category weight * card weight / total card weight in
  that category`.
- A director picks a card, then buys copies of it while it can afford them, up
  to a per-wave limit (5 for a wave; 6 or 15 per director), then rerolls.
- **Too cheap rule:** a continuous director skips a card when its credits exceed
  6 times the card's elite cost, unless that card is the most expensive one
  available. This stops a rich director from flooding the map with the weakest
  monster.
- **Elites:** the director buys the highest elite tier it can afford for the
  chosen card. Tier 1 costs 6x (3-5x HP, 2x damage); Tier 2 costs 36x (18x HP, 6x
  damage) and is available from stage 6.
- **Banking:** unspent credits stay with the director. When a director shuts
  down, 40% of its remaining credits move to another active director and the
  rest are lost.
- **Caps:** a spawn fails at 40 or more monsters alive. The scene director's
  initial spend also stops at 40.

### 2.4 What carries over to OB64

Credits accrue with time and with progress. A rich director buys stronger cards
before it buys more cards. There is a fast trickle and a slow surge. A cap keeps
the map readable. The parts that do not carry over are real-time seconds and
player count; OB64 has an in-game clock, a mission index and a player army.

## 3. What OB64 does today

Verified in this port (see `mods/smart-enemy-formations/README.md` and the
research notes in this session):

| fact | where |
|---|---|
| 60 unit records; 0..29 player, 30..59 enemy | `0x80197210 + u*0x19`; `+0x01 & 0x80` enemy; `+0x02..+0x06` members; `+0x07..+0x0B` squares |
| 100 enemy character records | `0x80195580 + r*0x34`; class `+0x11`, key `+0x12` |
| 120 enemy generic soldiers | `u16` pool at `0x80195414` |
| unit constructor | `func_ovlR_80215360(template, unit index)` in bank R (the mission loader module) |
| character constructor | `func_ovlR_802150DC`, which calls `func_8016F13C(record, level)` to roll stats for a class at a level |
| enemy templates | 35-byte records; `+0x00` leader class, `+0x01` leader level offset, `+0x06` leader square; group 1 at `+0x07` (class, or 1 = generic soldiers), level offset `+0x08`, squares `+0x0D..+0x0F`; group 2 at `+0x10`, level offset `+0x11`, squares `+0x16..+0x18`. `+0x19..+0x22` are not decoded |
| mission base level | levels are template offsets added to the byte at `0x801F1055` |
| class data | `0x80187C34 + c*0x48`: base stats and growth `+0x08..+0x23`, size `+0x04`, attacks per row `+0x38/+0x3A/+0x3C` |
| per-kill alignment change | `func_ovlX_801EFF60` (alignment and level difference, clamped to +-5) |

The mission's templates are read from a file the loader opens with
`func_8007938C(0x021AE3D0, ...)`; its layout and lifetime in RAM are not yet
known.

## 4. Design

### 4.1 Directors

| OB64 director | RoR2 analogue | job |
|---|---|---|
| **Garrison** | Scene | At mission start, spends the mission's generic-unit budget on garrisons for enemy-held towns and strongholds, in place of the generic templates. |
| **Patrol** | Fast combat | Small units (1-3 members) every few in-game hours. Harasses towns the player recently liberated and blocks roads. |
| **Campaign** | Slow combat | Full units (4-5 members, a strong leader) once or twice a day. Aims at the player's most valuable or least defended town. |
| **Rally** | Teleporter boss | One-off spend when the player first gets within reach of the boss stronghold: an escort unit beside the boss. The boss unit itself is never changed. |

Story units bypass all four. The director only spends its own budget and only
fills enemy unit slots that the story does not need (section 4.7).

### 4.2 Threat coefficient

```
missionFactor = 1.10^missionIndex                   (stagesCompleted)
timeFactor    = 0.02 * difficultyValue              (per in-game hour on the mission)
coeff         = (1 + hoursOnMission * timeFactor) * missionFactor
```

`difficultyValue`: Easy 1, Normal 2, Hard 3. The base is 1.10 instead of RoR2's
1.15 because the mission base level (`0x801F1055`) already rises across the
campaign.

### 4.3 Income and context

```
creditsPerHour = K * (1 + 0.4 * coeff) * townFactor
townFactor     = (1 + 0.15 * enemyTowns + 0.5 * enemyStrongholds) / (1 + 0.15 * startTowns)
```

`K` is set so a Normal-difficulty early mission produces one Campaign unit about
every six in-game hours (section 5 gives the numbers). `townFactor` is 1 at
mission start and falls as the player liberates towns, so liberating towns
starves the enemy. Each held stronghold counts for a lot, because strongholds
are where units deploy.

Patrol and Campaign take 40% and 60% of the income. Unspent credits are banked,
up to a cap of three Campaign units' worth.

### 4.4 Card pool built from the mission

At mission load, read every template that is not a story unit (section 6, R7)
and collect the classes it uses: leaders, group classes, and generic soldiers.
Those classes are the mission's **cards**. Weights come from how often each class
appears. The director then builds units that look like that mission's own army:
a Hellhound-and-Griffin mission keeps fielding beasts, and an undead mission
keeps fielding undead.

#### Theme rule

The director never fields a class the mission's own generic units do not use.
This is a hard rule:

- **The pool is per mission and closed.** No class is borrowed from an earlier or
  later mission, from the player's army, or from a related class. If mission 1's
  generic units have no Ogre, mission 1's director has no Ogre card, whatever
  its credits.
- **Story-only classes are excluded.** A class that appears on a mission only in
  story units (a boss, a named character, a scripted ambush that introduces a new
  enemy) is not a card on that mission. The story moment stays the first time the
  player meets that enemy.
- **Upgrades follow the debut table.** Veteran keeps the class. Elite upgrades to
  an advanced class only when that class has already debuted, meaning it
  appeared in a generic or story unit on this mission or an earlier one. A Knight
  becomes a Paladin through Elite only after the story has shown a Paladin.
- **Debut table.** A ROM tool (`tools/director_pools.py`, research item R9)
  decodes every mission's templates once and writes, per mission: its generic
  card pool with weights, the classes it introduces, and the first mission each
  class appears on, split into generic and story. The table ships as data with
  the app. The developer reviews it (ogrebattle64.net's mission lists are the
  reference) before the director uses it.
- **Gender, element and faction follow the templates.** The director keeps the
  alignment and element of the template a card came from (section 4.9), so an
  undead or dark-element mission does not field light-element units.

Categories, each with a weight from the mission's own mix:

| category | rule |
|---|---|
| Line | classes whose best row is the front (attacks F > M,B) |
| Ranged | best row middle |
| Casters | best row back |
| Beasts | size 2 classes |
| Fodder | generic soldiers |

### 4.5 Cost of a card

Raw stat totals barely separate classes (Knight, Berserker and Wizard all total
458), so the cost also counts growth at the unit's level and the class's best
attack count, all read from the class record:

```
power(class, L) = (sum(base stats) + L * sum(growth)) * (0.75 + 0.25 * max row attacks)
cost(class, L)  = power / 20
```

At level 5 (values from the ROM):

| class | base sum | growth sum | best attacks | cost |
|---|---|---|---|---|
| Soldier | 375 | 14 | 1 | 22 |
| Knight | 458 | 21 | 2 | 35 |
| Witch | 487 | 22 | 2 | 37 |
| Griffin | 451 | 21 | 2 | 35 |
| Hellhound | 448 | 22 | 3 | 42 |
| Paladin | 710 | 27 | 3 | 61 |

A **leader** costs 1.25x its card. **Veteran** (RoR2 elite tier 1) costs 2x and
gives +2 levels and the class's best equipment. **Elite** (tier 2) costs 4x and
upgrades the class to its advanced form from the promotion tree (Knight to
Paladin), from mission 6 and only when the debut table says that advanced class
has already appeared (section 4.4, theme rule). These multipliers are below
RoR2's 6x and 36x, so the director upgrades units without making single units
extreme.

### 4.6 Assembling a unit

1. Pick a category, then a leader card from that category's leader-capable
   classes. Apply the too-cheap rule: skip a card when the director's credits
   are more than six times its Veteran cost, unless it is the best leader
   available.
2. Pick a shape from the mission's own templates (for example "leader plus two
   of one class plus soldiers").
3. Fill the members: front-row classes first, then back-row classes, buying
   while credits last, within the five-member limit and with size-2 classes
   counting as two (one with the Small Large Units mod).
4. Spend any remainder on Veteran, then Elite, starting with the leader.
5. Set the squares with the Smart Enemy Formations rules (best row by attack
   count, a large member alone in its row).
6. Levels: `missionBase + difficultyOffset + floor(hoursOnMission / 24)`, with
   the day drift capped at +3. Offsets: Easy -1, Normal 0, Hard +1.

### 4.7 Choosing a target

Every in-game hour, each continuous director scores what it could do:

| target | score |
|---|---|
| retake a town the player liberated | town value * (1 / defenders nearby) * recency |
| reinforce an enemy town a player unit is heading for | town value * threat |
| intercept a player unit far from support | 1 / (unit strength) |
| reinforce the boss stronghold | only when the player is within N hexes |

It spawns at the enemy-held stronghold or town closest to the chosen target and
gives the unit that destination. Unit slots: the director never uses the last
`R` enemy slots, where `R` is the number of story units still to spawn on this
mission. That keeps scripted spawns working.

### 4.8 Difficulty and fairness

| setting | budget scale | level offset | max director units alive |
|---|---|---|---|
| Easy | 0.8x the mission's generic budget | -1 | 4 |
| Normal | 1.0x | 0 | 6 |
| Hard | 1.3x | +1 | 9 |

- The Garrison director's budget equals the cost of the mission's generic
  templates times the scale. On Normal the mission starts as strong as vanilla.
- Income stops while the player holds more than 70% of the mission's towns, so
  a winning player is not punished with endless spawns.
- After the player loses a town, the director pauses for 12 in-game hours.
- A destroyed enemy unit paid Honors in proportion to its cost. Honors was
  removed on 2026-10-29 (see 4.10).

### 4.10 Rewards (built)

RoR2 pays a kill as `coeff x monster cost x reward multiplier`. OB64 gets the
same shape in two places (`app/features/war_director.cpp`, Rewards section):

- **Honors (removed 2026-10-29).** Honors and its pay were taken out of the
  port with `app/src/honors.cpp`; this paragraph records the formula it used.
  A destroyed enemy unit paid
  `round(live cost / 55 x (1 + hours x 0.02 x difficulty))`, at least 1. The live
  cost uses the members' real classes and levels, so the mission's difficulty is
  already in it; the time term is the part of the coefficient that grows while
  the player sits on the mission. Offline: a Knight and four soldiers pay 2 on
  mission 1, 3 on mission 12, 5 on mission 30. Units the director never saw pay
  the old flat 2 (`app/src/honors.cpp`).
- **Drops.** Battle drop table O (`0x801EDB38`, global, read with `rand() % 3`)
  never improves in vanilla. Each equipment entry is upgraded to an item of the
  same category priced up to `original x multiplier`, with
  `multiplier = coeff x (enemy average cost / average when the mission opened,
  1..2)`; one of the three dearest candidates is picked, seeded by mission,
  entry and multiplier step. Unique items (item `+0x1A` bit `0x80`),
  50000-goth spells and quest key items are never given or replaced; consumable
  entries (bit 15 clear) are left alone. Writes happen only while table O's
  record-7 fingerprint holds (`0x0027` at entry 0, `0x0050` at entry 34). A
  re-DMA brings vanilla bytes back, which are taken as the new original.
  (2026-10-31: the upgrade now starts from the content database's rewards and
  is published as its runtime pack `runtime/director-loot`;
  `design-content-database.md` section 2.3.)
  Offline (`tools/test_loot.cpp`): x1.10 on mission 1 (1 of 105 drops changed),
  x3.14 on mission 12 (34), x17 on mission 30 (35; e.g. Halt Hammer ->
  Celestial Hammer). `OGRE_DIRECTOR_LOOT=0` turns it off.
- Item table: RAM `0x8018C42C` (ROM `0x6232C`), 0x20 per item, `+0x04`
  category, `+0x08` u16 price, `+0x1A` bit `0x80` unique. Guard: item 1's name
  pointer is `0x8018B4D0`.
- When Phase 3 sends units, they pay the same formula; RoR2's lower reward for
  pre-placed (scene) monsters is not applied, because every OB64 unit is
  pre-placed today.

### 4.11 Catch-up training (built)

Training is a catch-up mechanic. Its enemies come from every class the player
has fought, and its level brings a unit behind the army up toward it. Code:
`app/features/war_director.cpp` (Training section), hooks in
`tools/director_hook.py`, offline check `tools/test_training.cpp`.

Vanilla, read from the code:

- Bank M (scene `0x05`, the map) `func_ovlM_8019AF0C` sells the fight. It
  subtracts the fee from the goth word `0x80196A8C`, takes a template id from the
  per-location table at `0x801A7B00` (4-byte entries `{u16 id + 1, u8, u8}`, 24
  bytes per row) and stores it at `0x80193690`/`0x80193691` (`0x8019D2C0`).
- Bank AH (battle setup) `func_ovlAH_801D0AAC` builds the enemy. It takes the id
  from `0x80193690` (or from the u16 `0x80197B80`, which scripted encounters
  set). It copies the 35-byte template from `enemydat.bin` (asset `0x021AE3D0`)
  to `sp+24`, sets the level `$s4 = min(99, highest member level of the player's
  unit 0x801976FC)`, and builds the unit as enemy unit 30 through
  `func_ovlAH_801D0860` (class, level, items).

Changes:

- `ogre_training_mark` runs before `0x8019D2C0`. `ogre_training_build` runs at
  `0x801D0C7C`, after `$s4` is final, and its return value replaces `$s4`. It
  acts only on a marked fight with `0x80197B80 == 0`, so scripted encounters
  keep vanilla behaviour.
- Composition: leader and members are drawn from the bestiary
  (`<config>/bestiary.cfg`), within 110% of the template's cost at the new
  level. The director adds every class it sees in an enemy unit on a mission
  field; classes 81+ (story) are never added. Leader items are cleared, so the
  unit carries no items. With an empty bestiary the vanilla composition stays.
- Level: `unit` = the average member level of the training unit, `army` = the
  average of the roster's ten highest levels, and
  `level = unit + clamp(round((army - unit) / 2), 0, 6)`. Offline, a unit
  averaging 8 in an army at 20 fights level 14; a unit at the army's level
  fights its own level.
- Rewards: EXP through the shared EXP split. Honors through the normal per-unit
  payout, priced at the rebuilt unit's cost. No goth is paid out; the fee is
  vanilla's.
- `OGRE_TRAINING=0` keeps vanilla training.

Not verified in a played training fight:
- whether unit 30 empties after the fight, which is what pays the Honors
- whether vanilla training ever pays goth or drops an item

The bestiary is per installation, not per save slot.

### 4.12 Phase 3: deploying bought units (built)

**The game's own spawn.** The field update `func_ovlN_801B15A0` has a
mid-mission spawn at `0x801B222C..0x801B2584`. The game uses it on one mission
only: the byte `0x8018F4A1` is `0x36`, flag `0x14` is set and flag `0x1B` is
clear. Its steps are:

1. **Slot.** It takes the first unit slot 32..48 whose map object
   (`0x801F0CD0 + slot*4`) has `flags & 0x11 == 0`. A story unit that has not
   entered yet keeps its object, so its slot is never taken.
2. **Builder code.** It DMAs ROM `0x195430` (`0x2380` bytes) to `0x80214FA0`
   with `func_8009DA50`.
3. **Unit.** `func_80215360(template, slot)` builds the unit and its members
   from a 35-byte enemydat template. The game's 4 variants are at
   `0x801E85C0 + v*35`, with 8 parameter bytes per variant at
   `0x801E85A0 + v*8`.
4. **Record bytes and map object.**
   - Parameter bytes `+0`/`+1` go to unit record `+0x0C`/`+0x17`.
   - `func_801D4F5C(slot)` creates the map object.
   - Parameter bytes `+2`/`+3` go to object `+0xBA`/`+0xBB` (AI).
5. **Placement.** The ground height comes from `func_801A66E8` (object
   `+0x70 == 1`) or `func_801A641C`, with `a0` = byte `0x800E7AB9`, `a1` = x and
   `a2` = z. Then `func_801CC4A0(object, x, y, z, 0)`. Object `+0x08`/`+0x0C`/`+0x10`
   is the position.
6. **Release.**
   - Clear object flag `0x01` and unit record `+0x01` bit `0x04`.
   - Object `+0x18`/`+0x1C` is the sprite scale.
   - `func_8021592C(slot, 0, 0, 0)` gives the route.
   - Parameter byte `+7` goes to the trigger halfword `0x8019369C + slot*2`.
   - `func_801D4760()` refreshes the field.

**What the director does with it.**
- A purchase goes into a queue of at most 2 units.
- The deployment subscribes to game event 2, field update (`0x801B258C`,
  `config/hooks.toml`, `app/features/game_events.hpp`), and replays the steps above
  for the next queued unit.
- **Template.** It is one of this mission's own generic templates, as the
  garrison rebuilt them, with the bought classes (`take_spawn`). It is written
  into the game's variant-0 slot, and that slot is restored right after the
  build.
- **Slot.** An idle map object in slots 32..48 as the game's spawn uses, or
  else a slot whose unit record (first 0x0C bytes) is empty and has no object:
  `func_801D4F5C` builds the object in its fixed array (`0x801F10A0 +
  slot*0xC0`). Most missions, Tenne Plains included, have no idle objects.
- **Per-unit bytes.** The record bytes, AI bytes, sprite scale and trigger value
  are copied from a generic enemy unit on the field, and remembered for the
  mission, so deployment continues after the player has beaten every generic
  unit.
- **Start point.** The unit starts at the enemy-held location nearest any player
unit on the field, which is the front line (R1). If the enemy holds no
location, it starts at the boss unit's position (slot 30), the stronghold.
- If no slot is free, or no generic enemy has been seen on this mission, the unit
  waits; the log says why once (`[director] spawn waiting: ...`).
- `OGRE_DIRECTOR_SPAWN=0` turns deployment off.

**Clock.** The director's clock counts the game's frame counter `0x800AEFA4`.
It stops while the game is paused and follows the game's frame rate. Before
2026-10-29 it counted the app's main-loop passes, which ran faster than the game
and kept running in the Esc menu. One session's drop multiplier reached 9.6x
that way.

**Played 2026-10-30 (Tenne Plains).** The garrison rebuilt the four generic
units as predicted offline; the director bought a Witch patrol and a Witch +
Knight campaign unit; both deployed (slots 35 and 36) at the stronghold and
fought the player's units (developer). The first build placed nothing (no idle
slot) and the second stalled once every generic unit was beaten; both are fixed
as described above. Not checked yet: deployment at a front-line town instead of
the stronghold.

The unit's destination comes from the AI bytes copied from a mission unit; the
director chooses where it starts, not where it goes.

### 4.9 Alignment

Director units should not push the player into the bad-alignment branch just by
existing. The per-kill alignment change (`func_ovlX_801EFF60`) depends on the
enemy's alignment, so director units take the alignment of the template they
were built from.

## 5. Worked example: Tenne Plains (mission 1, Normal)

Generic units on the mission (from ogrebattle64.net's mission list): Berserker
plus Hellhound, Knight plus Griffin, Witch plus Griffin, Wizard plus Hellhound
plus Knight. The boss unit (Stabilo) is story and is left alone.

- Cards: Berserker, Knight, Witch, Wizard (human), Hellhound, Griffin (beasts),
  Soldier.
- Garrison budget at level 3: the four units cost about 330 credits, so the
  Garrison director has 330.
- Campaign unit example: Knight leader (35 * 1.25 = 44) + Hellhound (42) +
  Witch (37) = 123 credits.
- With `coeff = 1` on mission 1 and `K = 14`: `14 * 1.4 = 19.6` credits per
  in-game hour. 60% goes to Campaign, about 12 per hour, so one Campaign unit
  every 10 hours. Patrol gets 8 per hour and sends a Griffin pair (44 + 35 = 79)
  every 10 hours.
- If the player liberates half the towns, `townFactor` drops to about 0.7 and
  both intervals stretch by 40%.

## 6. Research backlog

Each item names the method. Items marked "developer" need the developer to reach
a game state (AGENTS.md rules 11 and 12).

| id | question | method |
|---|---|---|
| R1 | Where is the town and stronghold table (position, type, owner, liberation flag)? | Static: the map loader `func_ovlR_80215C38` parses the mission buffer; follow its stores. Developer: liberate a town with `OGRE_RAM_KEY` dumps before and after, then `tools/rdram.py diff`. |
| R2 | Where is each unit's map state (position, destination, path, speed, AI mode)? | Developer: dumps while one enemy unit walks, diffed. Static: readers of the unit table in bank N (map module). |
| R3 | Template bytes `+0x19..+0x22`: spawn time, trigger, origin, route, AI? | Static: the map loader's reads of the template record and of the 11-byte spawn entries. |
| R4 | Where do the mission's templates live in RAM, and for how long? | Static: the buffer returned by `func_8007938C` at `0x802164D8`. |
| R5 | In-game clock (hour and day) address | Developer: two dumps an in-game hour apart. |
| R6 | How does the game spawn a unit mid-mission ("attacks after liberating X")? | Static: callers of `func_ovlR_80215360` outside the loader; bank N event code. |
| R7 | What marks a story unit: named leader, dialogue trigger, boss flag? | Static: template fields from R3; compare against the named units on ogrebattle64.net. |
| R8 | How enemy AI picks targets today | Static plus developer, after R2. |
| R9 | Debut table: every mission's generic pool and story-introduced classes | Offline, after R3/R4/R7: `tools/director_pools.py` decodes all missions from the ROM. The developer checks it against ogrebattle64.net. |

## 6a. Phase 0 results (research done offline)

| id | status | finding |
|---|---|---|
| R3 | done | Template layout confirmed against ogrebattle64.net (mission 3 matches unit for unit, levels included). Bytes `+0x19..+0x22` are zero; trigger, origin and route are in the spawn entry instead (below). |
| R4 | done | The game's files are LHA (`-lh5-`) archives stored as assets. `enemydat.bin` (asset `0x021AE3D8`, opened through container `0x021AE3D0`) holds all 556 templates for the whole game. Each mission's data block is `eset<chapter>_<mission>[a/b].bin`: four u16 offsets, the second being the spawn list (count, then 18-byte entries). `tools/mission_data.py` extracts both from the ROM. |
| R7 | done (rule) | Entry 0 of every spawn list (unit slot 30) is the mission boss. Boss and named units use the duplicate story class ids (81+, for example Berserker 118). A unit is story when it is entry 0 or uses a class >= 81. Checked on missions 1 and 3. |
| R9 | done | `docs/mission-enemies.md`: every mission's units, generic pool and story-only classes, and the debut table (first generic and first story appearance of every class). Ogre first appears on mission 8; Skeleton and Ghost on mission 9; advanced human classes from mission 10. |
| R6 | partly | Spawn entries carry the trigger: `+0x0C` is a flag and `+0x0D..+0x0E` a u16 written to `0x8019369C[unit]` (bit 15 set by the flag). Entries with a map position instead of a location are units that enter later. How the trigger value is consumed is not traced yet. |
| R1 | done (static, 2026-10-29) | `func_ovlN_801DA360` (location -> position) and the liberation code `func_ovlN_801C2B90` give the whole table with no played mission. Count: word `0x801F1090`. State: u16 at `0x801951EC + i*0x24`, `0x04` enemy-held, `0x02` player-held, `0x10` the player's headquarters; liberating clears `0x04` and sets `0x02`. Position: x, z floats at `map_data + 0x1C + i*8`, `map_data` = word `0x800E7A90 + 4*byte 0x800E7AC5`; the height comes from the ground (`func_801A641C`). The start states are `scincsv<scenario>.bin`, one `(u16 location id, u16 state)` per location: `0x0004` enemy (366 in the game), `0x2012` headquarters (58, one per mission), `0x2002` a second player base, `0x0000` unowned. Location ids index `ktenmain.bin` (316 records of 28 bytes: a type byte, a 21-byte name, 6 attribute bytes). |
| R2 | partly | Each unit has a 0xC0-byte map object, pointer table at `0x801F0CD0 + unit*4`; `+0x04` unit index, `+0xBA/+0xBB` the spawn entry's AI bytes, position set through `func_801CC4A0(object, x, y, z)`; route bytes go to `func_ovlR_8021592C`. Movement and target fields need a played mission. |
| R5 | open | In-game clock address: needs two dumps an in-game hour apart. |
| R8 | open | After R2. |

Mission block sections (same loader):

| section | records | RAM | content |
|---|---|---|---|
| `+0` | 7 bytes | `0x801F1055..` | base level (`+0`) and two more mission bytes |
| `+2` | count + 18-byte spawn entries | read in place | units (above) |
| `+4` | count + 18-byte trigger records | `0x801F0EDE + i*18` | record `i` is AI mode `4 + i`; the map's AI routine `func_ovlN_801CF0D0` reads bytes `+0x0A..` of the unit's record and looks up the section `+6` record named by byte `+0x0A` |
| `+6` | count + 10-byte records | count `0x801F0E38`, records `0x801F0E39 + i*10` | byte `+0` id, `+1` type (4, 9, 0x0C seen), `+6` a location or index |

Location indexes in spawn entries match the site's names on Tenne Plains:
location 2 is Mulsuk, 4 Buchanan, 7 Theodricus Mine (Stabilo). The Knight
"attacks from Buchanan after liberating Mulsuk" is the spawn entry with AI mode
6, and trigger record 6 names location 2. `scincsv<scenario>.bin` holds one
16-bit state per location (Tenne Plains: 11 locations, `0x0004` for most,
`0x2012` for location 6).

The runtime half (which RAM field changes on liberation, how a trigger releases
a waiting unit, the clock) is captured with `tools/capture-session.bat`, which
writes numbered full dumps on key 9.

Spawn entry (18 bytes, `func_ovlR_80215C38`): `+0x00` group, `+0x02` unit slot,
`+0x03..+0x04` template id, `+0x05/+0x06` map position or location index (when
`+0x06` is 0), `+0x08/+0x09` AI bytes, `+0x0A` written to unit record `+0x17`,
`+0x0C` trigger flag, `+0x0D..+0x0E` trigger value, `+0x0F..+0x11` route.

The open items need a played mission: the developer reaches a mission's map with
dumps armed (`OGRE_RAM_KEY`), and takes one before and one after liberating a
town, and two an in-game hour apart.

## 7. Where the code runs

- The director must change unit records, call the game's own constructors (so
  stats, equipment and sprites are set the game's way) and issue map orders. All
  of that has to happen on the game thread, between game frames.
- A MIPS mod cannot call base-game functions (`jal` from the mod's `0x81000000`
  fails with `R_MIPS_26`, see `history-archive:docs/history/DECISIONS.md` session 124), and bank code
  cannot be hooked.
- The host app can run C++ on the game thread through a native callback from
  generated code, the way `ogre_map_cursor` already does. The director should
  live there (`app/features/war_director.cpp`) and call
  `func_ovlR_80215360` / `func_8016F13C` directly while bank R is resident, or
  replicate their writes once R4 and R6 are understood.

## 8. Phases

| phase | content | done when |
|---|---|---|
| 0 | Research R1-R9 | each answered with instruction-level evidence; the debut table reviewed by the developer |
| 1 | **Observer** (built: `app/features/war_director.cpp`, DEBUG tab "WAR DIRECTOR" rows; `OGRE_DIRECTOR_FORCE=1` runs it off the mission field for console tests): a DIRECTOR section in the overlay's DEBUG tab showing coeff, credits, towns held, the card pool and what it would buy. No spawning. | numbers match a played mission; the developer agrees the card pool looks like the mission |
| 2 | **Garrison** (built: `garrison()` in `app/features/war_director.cpp`, inserted by `tools/director_hook.py` at bank R `0x802164E0`; every generic template on the mission is rebuilt from the mission's own leader and member pools within 110% of its cost, story templates untouched, the cached template file restored before each mission; offline check `tools/test_garrison.cpp`; `OGRE_DIRECTOR_GARRISON=0` turns it off): rewrite the generic templates in RAM before the loader builds them (same budget, director compositions). Uses the game's own constructor. | a mission loads with director-built generic units and plays to the end |
| 3 | **Patrol and Campaign** (built, untested in play: `app/features/director_spawn.cpp`, section 4.12): bought units deploy at the enemy-held town nearest the player's units through the game's own mid-mission spawn steps | units appear, walk to their targets, fight, and pay Honors |
| 4 | **Rally**, difficulty settings in the SETTINGS tab, tuning | a full chapter played on each setting |

## 9. Risks

- Unit map state (R2) may be spread across several structures; spawning without
  all of them could crash the map module. Phase 2 avoids this by changing
  templates before the game builds them.
- The 30 enemy slots are shared with the story. The reserve in 4.7 depends on
  knowing how many story units remain (R7).
- Bank R (the constructor) is only resident while a mission loads; mid-mission
  spawning may need the writes replicated (R6).

