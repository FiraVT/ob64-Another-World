# Balance master plan: classes, enemy party composition and the war director

Status: plan, 2026-10-30. Developer decisions the same day: no nerfs (D1); the
first work is enemy formations and enemy weapons (section 8, phases F1 and W1,
built, not played). This document ties three
existing pieces into one plan:

- `docs/anotherworld/design-class-rebalance.md` (class data, Phase A built in
  `app/features/class_rebalance.cpp`);
- `mods/smart-enemy-formations` (every enemy member moved to its best row);
- `docs/anotherworld/design-war-director-v2.md` (the director: garrisons, waves, events).

Sources used: `gameinfo/mission detail` (the 43 scenes' enemy squads, 583
squads), `gameinfo/classinfo` (class pages), `docs/class-catalog.md` (ROM class
data), and the LordlyCaliber editor (github.com/rempred/LordlyCaliber,
`squads.js`: the 35-byte enemy squad record and its validation rules).

## 1. Goal

**Enemy units look like armies that people built, with a purpose and a weakness
the player can read. They are not the best possible squads. Class balance makes
more of the roster worth fielding, for the player and for the enemy. The war
director decides how many units, how strong and when; the composer (section 5)
decides what each unit is.**

Developer constraints (2026-10-30):

| # | constraint |
|---|---|
| C1 | A real balance change, not only member rows. |
| C2 | A real change to enemy party composition. |
| C3 | No perfect teams. The enemy must not beat the player down with optimal squads. |
| C4 | Enemy units can field large members next to each other, as the player can. |
| C5 | The war director is part of the plan (`design-war-director-v2.md`: story order, strength fits the moment, rival not punisher). |

## 2. What the data shows

Numbers from the 583 vanilla squads in `gameinfo/mission detail`, sizes and best
rows from `docs/class-catalog.md`
(`tools/squad_stats.py` prints them).

| measure | value |
|---|---|
| members per squad (1/2/3/4/5) | 1 / 20 / 148 / 154 / 260 |
| distinct member classes (1/2) | 149 / 433 (74% use both member groups) |
| large members per squad (0/1/2) | 334 / 128 / 121 |
| large members with any neighbour | 0 of 370 |
| squads with two large members placed next to each other | 0 of 121 |
| members standing in their best row | 1150 of 2393 (48%) |
| average members, scenes 1-9 / 10-19 / 20-29 / 30-43 | 3.26 / 4.31 / 4.27 / 4.22 |
| most common leaders | Saturos 47, Knight Templar 44, Black Knight 37, Raven 31, Paladin 27, Archmage 26 |

What this means for the plan:

- **Vanilla squads are not optimal.** Half the members stand outside their best
  row. The smart-formations mod moves all of them, which makes every enemy unit
  close to perfect. That is the "beaten down" effect C3 describes. The composer
  keeps a measured share of imperfection instead (section 5.4).
- **The two-group template is the game's own shape.** A squad has one leader and
  at most two member classes, three members each (LordlyCaliber `squads.js`:
  record bytes 0-6 leader, 7-15 group B, 16-24 group C). That shape gives each
  unit a readable identity ("a Witch with two Hellhounds"). The plan keeps it.
- **Large units never touch.** LordlyCaliber's validator states the game's rule:
  a large unit forbids any unit in its 8 neighbouring cells. The data agrees
  (0 of 370). The port answers size 1 to the Organize screen only
  (`ogre_class_size`, `docs/anotherworld/DECISIONS.md` "one square"); the enemy unit builder
  `func_80215360` still places large members as size 2. C4 needs research R1-R3.
- **Strength by stage already flattens after scene 10.** Squads reach about 4.3
  members at scene 10 and stay there. Later difficulty comes from levels, tiers
  and classes, not from size. The director's `stage_cap` (2 / 3 / 4 members by
  scene 9 / 18 / later) follows this.

## 3. The three layers

| layer | owns | data it changes | built today |
|---|---|---|---|
| L1 class balance | what each class is worth | class records `0x80187C34 + class*0x48`, attack table `ROM 0x6099C` | Phase A (`class_rebalance.cpp`) |
| L2 composer | what one enemy unit is: leader, groups, levels, items, squares | the 35-byte template (`0x801E85C0 + variant*35`) | the director's `rebuild()` and `plan_unit()`: random picks by weight and a budget |
| L3 war director | how many units, how strong, where, when | spawn lists, waves, events | garrisons, waves, caravans (v2 phases) |

The rule between them: **L3 asks L2 for a unit by role and budget; L2 answers
with a template that L1's numbers price.** L3 never picks classes, and L2 never
decides timing.

## 4. L1: class balance

### 4.1 Principles

| # | principle |
|---|---|
| B1 | Each class has a job: a row, a damage type, or a support effect. A class without a job gets one (`design-class-rebalance.md` section 4 does this for Faerie, Gremlin, Ogre, Gorgon, Goblin, Saturos). |
| B2 | Later tiers stay better within a line. Variety comes from different lines, not from flattening a line. |
| B3 | Large classes are worth about 1.5 human members at equal tier (they take two of the five slots). |
| B4 | One price for both sides. The composer prices units with the same class values the player sees, so a buff to a weak class also makes the enemies that field it stronger, and the budget accounts for it. |
| B5 | Change data before code. A stat, growth or row-attack change is a table edit; a new behaviour (Faerie's Kiss heal) needs code and a research item. |

### 4.2 Decided: no nerfs

`design-class-rebalance.md` rule 1 is "no nerfs". C1 asks for a real balance
change. The dominant player picks (vanilla community consensus: Black Knight,
Lich, Sword Master, Dragoon lines, elemental dragons) stay dominant under "no
nerfs". Decision D1 (developer, 2026-10-30): no nerfs. Dominance is answered on the enemy side instead,
because the composer fields counters (section 5.3) and the player meets them.

### 4.3 Work

| step | content |
|---|---|
| L1-a | Confirm the attack-table meanings (R2 of the class doc: power, formula, extra effect) in game. |
| L1-b | A class value table: `card_cost` (`war_director.cpp`: stat total at level, scaled by the best row's attack count) extended with the attack's kind, element and target count. The table is the shared price for B4. |
| L1-c | Phase B of the class doc for the remaining weak families, one family per step, each played before the next. |
| L1-d | Element coverage per class (which element each row attacks with), for the composer's coverage rule (5.3). |

## 5. L2: the composer

### 5.1 Input and output

Input: a role (garrison, raid, wave, caravan, bounty, lieutenant, boss escort),
a budget in L1 value, the mission (for the story pool and theme), the war seed,
and the director's current intensity.

Output: a 35-byte template: leader class and level offset, up to two member
groups (class, level offset, up to three squares each), item choices, plus a
short label for alerts and the field overlay ("Beast pack", "Battery").

### 5.2 Doctrines

A doctrine is the idea behind a unit. It gives the leader a reason to have those
members and gives the player a counter to read. Each doctrine names its
strength and its built-in weakness.

| doctrine | leader | groups | strength | weakness the player can use |
|---|---|---|---|---|
| Shield wall | Knight, Phalanx, Paladin | front fighters; one support group or none | holds a town, high VIT | slow, few spells; casters break it |
| Battery | Wizard, Witch, Archmage, Sorceress | back-row casters; one front group to screen | burst magic damage | thin front; a fast melee unit reaches the casters |
| Beast pack | Beast Tamer, Beast Master | beasts and hounds, one class | many attacks, fast | low INT/MEN; magic hurts, no healer |
| Dragon riders | Dragon Tamer, Dragon Master | dragons (large) | big breath attacks | one element; resist it and the unit stalls |
| Holy order | Cleric, Priest, Paladin | front fighters plus healers | outlasts the player | low damage; ignore it or burst it |
| Raiders | Goblin, Saturos, Hawkman | fast skirmishers | reaches lone units, raids towns | fragile; any steady unit beats it |
| Dark host | Lich, Death Templar | undead, ghosts | returns after battles | holy damage ends it |
| Mixed company | any | two different groups, picked by weight | flexible | no plan: the "average" unit |

Doctrines are data (a table per doctrine: leader list, group rules, row plan,
weight by mission theme). The story-order gate (`design-war-director-v2.md`
4.2) filters every class before a doctrine may use it. A doctrine with no legal
leader on a mission is skipped.

### 5.3 Picking: purpose over optimum

The composer builds a unit in this order:

1. **Doctrine** by weight: the mission's theme (from the vanilla squads of that
   scene, which show what the mission is about), the role, and the war
   condition (`design-war-director-v2.md` section 7).
2. **Leader** from the doctrine's legal leaders.
3. **Groups** from the doctrine's group rules, within the budget.
4. **Counter-pick, bounded.** At most one group in a wave (not per unit) may be
   chosen as a counter to what the player fields most (the class mix of the
   player's dispatched units). The rest of the wave stays on doctrine. The
   enemy answers the player sometimes, not always.
5. **Squares** by the doctrine's row plan with the imperfection rule (5.4).
6. **Items**: the class defaults; better items only for lieutenants and bosses,
   from the loot bands.

### 5.4 Competence: the "no perfect teams" rule

Every built unit gets a competence score from 0 to 1, from four checks:

| check | full marks when |
|---|---|
| rows | every member stands in its best row |
| coverage | the unit has two or more damage elements or kinds |
| sustain | the unit has a healer or a regenerating class |
| synergy | the leader's doctrine matches its groups |

The director sets a target band per unit, and the composer adds or removes
flaws to land in it:

| unit | competence band |
|---|---|
| scenes 1-9 | 0.35 - 0.55 |
| scenes 10-29 | 0.45 - 0.70 |
| scenes 30+ | 0.55 - 0.80 |
| lieutenant | band + 0.10 |
| boss | the boss stays vanilla (story units are not touched) |
| director in relax phase | band - 0.10 |

Flaws the composer uses, in this order, so a flaw is something a real commander
might do:

| flaw | example |
|---|---|
| a member one row off | an Archer in the front row |
| no healer | a Shield wall without a Cleric |
| one element | a Dragon riders unit of one breath type |
| a weak second group | three Soldiers behind a strong leader |
| a leader one level lower | a lieutenant without the bonus |

The vanilla baseline (48% of members in their best row) sits inside the
middle band, so the composer's enemies feel like the original game's on average
and vary around it.

### 5.5 Large members next to each other (C4)

Rules once R1-R3 are answered:

- A unit may hold up to two large members, as vanilla does (121 squads).
- Two large members may stand side by side in one row or one behind the other.
- A large member takes two of the five member slots, as now (B3 prices it).

If R2 shows the battle cannot draw two enemy large members in adjacent squares,
the fallback keeps the vanilla 8-neighbour rule for enemies and gives the player
the advantage of one-square large units. That fallback is decision D3.

### 5.6 Relation to the smart-formations mod

The mod's row chooser (most attacks wins, centre first, large first) becomes the
composer's "rows" step for members with no flaw. The mod itself stays as an
optional "always perfect rows" mode for players who want a harder game, off by
default. Without the director, the mod's current behaviour applies to the
vanilla templates as now.

## 6. L3: the war director

What changes in `design-war-director-v2.md`:

| v2 section | change |
|---|---|
| 4.3 strength fits the moment | the member caps stay; the composer's competence band becomes the second dial |
| 4.4 shapes, tiers and lieutenants | shapes (Line, Battery, Mixed in `plan_unit`) become doctrines (5.2) |
| 5.1 intensity | intensity also lowers the competence band in relax phases (5.4) |
| 5.2 orders | each order names a role for the composer: Garrison → Shield wall or Mixed; Raid → Raiders or Beast pack; Counterattack → the doctrine the player struggled against least (so the counter is fair) |
| 5.3/5.4a events | the caravan, bounty and fortified-town events ask the composer for their unit by role |
| 7 war conditions | conditions weight doctrines: Beast Season → Beast pack x3 |
| 8 readability | alerts and the overlay show the doctrine label ("A Battery marches on Alba") |

The director's `rebuild()` (garrisons) and `plan_unit()` (bought units) both
call the composer. Neither keeps its own class picking.

## 7. Measuring balance

No playtest can cover 43 scenes. An offline evaluator does:

| tool | what it reports |
|---|---|
| `tools/squad_eval.py` | per scene: vanilla squads and composer squads side by side, with L1 value, competence, members, large count, elements |
| a seed sweep | 100 war seeds per scene; the value and competence spread must stay inside the bands, and no seed may produce a unit above the band ceiling |
| the director's DEBUG tab rows | the live unit's doctrine, value and competence in game |

Pass criteria for each phase: the sweep stays in band, and the developer plays
the scenes named in that phase.

## 8. Phases

| phase | content | check |
|---|---|---|
| F1 | ENEMY FORMATIONS (GAMEPLAY, on by default): every generic template is re-seated at mission load by row attacks, with a stage-based share one row off (35% / 25% / 15% for scenes 1-9 / 10-29 / 30+), the leader leaning back in units of three or more, and the sturdiest member moved up when nobody chose the front. Large members are seated first, one square each, and may stand side by side: the builder `func_ovlR_80215360` copies template squares without reading a class size, so the vanilla no-neighbour rule is only data (R1 answered; R2, how the battle draws two adjacent large enemies, is the in-game check). `war_director.cpp` `form_template`. Built 2026-10-30, not played. The smart-enemy-formations mod re-seated members after this and hid it; since 2026-10-31 no package ships it (`DIST_EXCLUDE_MODS`, `tools/build.sh`). | `[enemy] tN:` log lines; battles show casters behind a front row |
| W1 | ENEMY WEAPONS (GAMEPLAY, on by default): each group's first item choice gets a weapon of its class's basic weapon category, priced above the basic one and within the mission's band (`band_max` = 150 + 50 x mission); leaders 75%, members 35% + 2% a mission up to 75%; uniform pick, leaders from the stronger half. `arm_template`. Built 2026-10-30, not played; R6 (the choice equips in battle) is the first check. | a battle's enemy status screen shows the weapon |
| E1 | ENEMY RANKS (GAMEPLAY, VETERANS by default), developer report 2026-10-30: the finale fields level-36 Valkyries instead of Freyas and a unit of five Sword Masters, and boss leaders stand exposed. (a) Promotion: a generic group past its class's promotion level (the game's class-change levels, `docs/class-catalog.md`; beast, golem, hawk and raider lines at 16) takes the next class of its line, chance 30% + 6% per level past it, at most 90%, up to two steps. (b) Mixed groups: a unit of one class (one group of three or more) moves half its members into a second group of another class from the mission's member pool, weighted x3 toward classes whose best row differs, within five places. (c) Formations guard the leader in units of three or more and in boss units: the leader is seated last, in the back row (then the middle) in the column with the most members in front. Boss units keep the boss's class and gear; their generic escorts promote. `promote_template`, `mix_template`, `guarded_square`. Built 2026-10-30, not played. | the finale's units in `[enemy]` lines show Freyas and mixed groups; boss leaders behind a member |
| E2 | Enemy-army classes (developer, 2026-10-30: "computer only like templar"). Class ids 81+ were all treated as story, so every Knight Templar unit was skipped by the enemy features. The ROM's non-boss spawn entries show three ids used across missions: Grappler 114 (78 members, 5 missions), Knight Templar 115 (53 leaders, 73 members, 10 missions), Daemon 116 (13 leaders, 3 missions). `enemy_army_class`/`story_class` in `war_director.cpp` count them as generic: their units get formations, weapons, mixing and the director's pools; every other 81+ id (Temple Commanders, Vanity, Knight Templar 147/155, Danika) stays story. The developer confirmed the guarded leader works (standing behind a member protects the leader from melee). Built, not played. | `[enemy]` lines for Lodis units |
| E3 | Developer review 2026-10-30. (a) The promotion levels are the class record's `+0x44` byte, which `tools/class_catalog.py` calls a level gate; no game code reading it is confirmed, and class changes in the game run on stats and alignment. Promotion now follows the campaign: the share is 0 up to mission 6, then 2.5% a mission up to 75% (mission 36), with the `+0x44` level as a floor. (b) Ghost becomes Lich as a themed late form (Lich: magic x2/x2/x3 against the Ghost's x1/x1/x2; no generic enemy unit used it). (c) The guarded leader takes the covered square where its class has the most attacks (back row on a tie). (d) Generic class use in the ROM's non-boss units: Goblin 281, Ogre 167 and Gremlin 165 are the three most used classes and appear to the last mission; never used: Lycanthrope, Werewolf, Lich, Vampire (44), Princess, Giant (all stats 255, a placeholder); barely used: Dragoon 1, Seraph 3, Vampire (43) 3, Gorgon 4, Thunder Dragon 3, Quetzalcoatl 5. Built, not played. | mission 42: Ghosts as Liches in the Daemon unit, Goblins mostly Saturos |
| E4 | The refit (`refit_template`), developer review 2026-10-30 ("a late game flying team ... still using hawkmen and 0 strategy behind the picks"). A unit whose leader and members all fly (class `+0x2C` = 1) stays all-flying, and promotions that would ground it are skipped; other units take classes sharing a family with the leader (`CLASS_ROLES`: family, role F/R/S, debut mission from `docs/mission-enemies.md`). Roles: a unit with no back-row damage (R) gets one first; a unit of four or more with no support (S) turns a second fighter group into support, never its only damage; otherwise a base-tier group takes a stronger class of its role. A replacement must be worth at least what it replaces (`card_cost` at the group's level x members; 80% when filling a missing role); two small members may become one large member within five places. Applied per weak group with the campaign share. Offline estimate for the Raven unit at level 33: Hawkmen x2 (116) -> Vultan x2 (178) or Raven x2 (175); Gremlin x2 (140) has no flying caster worth as much (Ghost x2 107, Sphinx 82, Cockatrice 79), so the Gremlins stay. `card_cost` counts stats and the best row's attack count, not the attack's kind or power, so a Gremlin's Kiss and Abyss count like sword blows: L1-b (a class value that prices attacks) is the next step. Built, not played. | `[enemy] refit:` lines |
| E5 | Boss units larger than five (developer, 2026-10-30: "put them inside the unit; we are increasing the maximum ... at least for mission bosses"). Built first: `reinforce_boss` fills a boss unit to five places with one or two themed members (escort group, or a new group from the mission pool, role-first, by `class_value`); 12 of the 63 boss units have room, 51 are full. Research for 6-7 members: (1) the unit record (`0x80197210 + unit*0x19`, 60 records back to back) has five member slots `+0x02..+0x06` and five squares `+0x07..+0x0B`; members 6-7 need storage outside it. (2) The battle has room: its participant table `*(0x801CE8DC) + 0x1C4 + n*0xF8` has 20 slots (`slti 0x14` at `0x801D0F68`, `0x801D1C78`, `0x801D1E38`). (3) The table is filled by `func_ovlJ_801D1A80`, one loop per side bounded by `s7 < 5` (`0x801D1C88`, `0x801D1E48`); per member it reads the reference from the unit record `+0x02+i` (`0x801D1ADC`), the square from a five-byte battle-state array `*(0x801CE8E0) + 0x836 + i` (`0x801D1B08`; the other side's array presumably follows, so seven squares need a new place), finds a free participant (`+0x48 == 0`) and builds it with `0x801CA400`. Open: the enemy character records for the extra members (`0x80195580`, refs 1..99), every other five-member loop in battle (targeting, victory check, results, post-battle write-back) and on the field (unit menus, the overlay), found with a key-8 census around a boss battle. Census (run-boss1, presses 3 -> 4: three battles, the boss last): 1327 functions ran; 24 of them have a loop bounded at 5 that touches unit records, the battle unit indexes `0x801976FC`/`0x80197708`, the battle squares `+0x836` or the enemy characters. Battle setup: `func_ovlJ_801D1A80` (participants, both sides), `func_ovlC_801CA6A0` (both unit indexes and the enemy characters), `func_ovlJ_801D08A0` (member 0's class at `0x80195591`). In battle and after: `func_ovlJ_801D2A40` (849 instructions), `func_ovlJ_801D48F8`, `func_ovlJ_801D85E4` (enemy unit and enemy characters: the end-of-battle write-back candidate), `func_ovlJ_801DB90C` (both sides, 954 instructions), `func_ovlC_801C6304`/`801C6B04` (member class loops). Field side (bank N, 11 functions including the map object builder `func_801D4F5C`) and banks AC/AE: they read the unit record only, so members kept outside it are invisible there. Plan: battle-only escort members for the boss unit, stored outside the unit record with their own enemy character records, injected at battle setup, written back after the battle, and left out of the field's five-member loops (the boss unit's strength shown on the field stays the record's five). | S1 built 2026-10-30, not played: `app/features/boss_escorts.cpp` and six `config/hooks.toml` hooks in bank J. On entry to `func_ovlJ_801D1A80`, when the battle's enemy unit (`0x80197708`) is the boss (slot 30) and ENEMY RANKS is on, the mission's extra members are created once: one (two from mission 15) copies of a generic escort's enemy character record (`0x80195580 + ref*0x34`, 0x34 bytes) in free records (class key `+0x12` == 0, which is how the builder `func_ovlR_80215360` allocates), standing in free squares in front of the boss's column (squares 0-based, `0x802154CC`). The enemy loop's three member reads (`0x801D1CC0`, `0x801D1D7C`, `0x801D1DD0`), its square read (`0x801D1D10`, the battle copy `+0x84F`, mapped by `0x801CA5C0` through the camera's table) and its bound (`0x801D1E48`) pass through `ogre_battle_member`, `ogre_battle_square` and `ogre_battle_more`, so members 6 and 7 come from the table. A loaded save is checked: an extra whose record is free again or listed by a unit is replaced. Played 2026-10-30 (run-boss2, Tenne Plains): the boss unit fought with a sixth member, a copy of its Wyrm escort (ref 13, square 6), in two boss battles that ended normally. S2: no new code is needed for HP. `func_ovlJ_801D9100` writes every live participant back to its character record by its reference (+0xF6) after a battle, over all 20 slots (`0x801D92D4`): HP `+0x20` -> `+0x18`, `+0x34` -> `+0x1B`, a status bit in `+0x33`; the extras are participants with their own references. `func_ovlJ_801D85E4`'s five-member loop (`0x801D8D00..0x801D8D78`) is not a write-back: when `0x801C8AFC` holds it erases the enemy unit's character records and unit record (a temporary enemy). The `[boss]` line now prints each extra's saved HP at every boss battle, to confirm in play. S3 (2026-10-30): two battle setups run per battle: `func_ovlC_801CA6A0` from the battle main (`0x801AFDD8`, both sides, gated by `0x801976F9`) and `func_ovlJ_801D1A80` (`0x801D783C`), which adds any member without a participant (it skips a reference that has one). Victory, results and EXP run on participants. Five-member loops that read the unit record in battle: `func_ovlC_801C6304` (each side's occupancy grid, `*(0x801CFA90) + side*0x24`, from members, squares and class sizes; called at `0x801AFE14`/`0x801AFE30`), `func_ovlC_801C6B04` (two loops, members and squares; one caller `0x801BD9AC`), `func_ovlC_801CA6A0` (two loops). The grid loop now passes through `ogre_unit_member`/`ogre_unit_square`/`ogre_unit_more` with the side's unit index (side struct `+0x10`), so the extras stand in it. `801C6B04` and `801CA6A0` are not hooked yet. S4: the extras are made on the field update (game thread) as soon as the mission runs; the overlay draws the boss as a gold-rimmed B disc with a "BOSS +N escorts" tag (`boss_escorts::extra_members`, read-only). Not played. Random themed extras (2026-10-30, built, not played): on the mission's first field update `boss_escorts.cpp` asks `war_director::themed_extra_class` for each extra's class (flyers stay flyers, otherwise the escort's family; introduced by the mission; a role the boss unit lacks weighted x3 for damage, x2 for support; weighted by `class_value` at the escort's level; seeded by the war seed), loads the builder's bank R (ROM `0x195430`, `0x2380` at `0x80214FA0`) with `func_8009DA50` as the director's spawns do, and builds each extra with `func_ovlR_802150DC(ref, template, group 1, enemy 1, [sp+0x10] unit 30)` from a scratch group in the game's template buffer `0x801E85C0` (class, level offset from `0x801F1055`, class gear), on its own stack space 0x40 below the caller. Bank P (ROM `0x171EC0`, `0x6030`), the field's bank of that RAM, is loaded back when it was resident. When no class fits, the extra copies the escort's record. Battle hooks only read the table (`state_valid`: same mission, records claimed, no unit lists them). A fresh run that loads a save made after the extras were built builds new ones and leaves the old records claimed (at most two a reload). Open: the developer's checks (victory with extras alive, EXP from an extra, results). `OGRE_BOSS_ESCORTS=0/1` forces it. | `[boss]` log lines; the boss battle shows seven on the enemy side |
| E6 | Unit size (developer, 2026-10-30: full units should become the norm as the war goes on; a large member takes one place, so a leader with four Hellhounds or dragons should be possible; up to nine squares in theory, a VETERAN thing). Built, not played: `place_cost` counts a large member as one place for the enemy when LARGE UNITS is ONE SQUARE (two under VANILLA), in the refit, boss reinforcement and mixed groups; `member_cap(mission, hours)` = the stage cap (2 / 3 / 4 by missions 9 / 18), -1 on EASY, +1 on HARD and VETERAN, +1 after ten in-game days on a mission (NORMAL and up), at most 4 (five per unit record), used by the garrison rebuild and the director's bought units. Members six to nine need the battle-only extras of E5 generalised from the boss to any enemy unit (a table per unit slot, the same setup and grid hooks keyed by the side's unit index); planned for VETERAN in late missions. | `units up to N members` in the director's debug rows |
| E7 | Prices and elites (developer, 2026-10-30). The director's price `card_cost` was the stat total scaled by attack count, which compresses strength (Goblin to Tiamat about 2x in price, about 7x in `class_value`; a Tiamat cost the same as a Paladin). Now `card_cost` = the old average price at the level x `class_value` / the average value at the level, over the generic classes (`level_averages`, cleared at each mission load), so total spending per level is unchanged and each class pays for its strength (level 30: Goblin about 0.5 of the average, Paladin 1.7, Tiamat 3.3). Elite = a value at least 2x the average: the six elder dragons (3.0-3.5x) and Baldr Golem (2.1-2.3x); everything else is 1.9x or less. Elite members per unit: EASY 0 (1 from mission 30), NORMAL 1, HARD 2, VETERAN 4, applied in bought units, garrison rebuilds, promotions, the refit and mixed groups. One unit in four is built a member short. Built, not played. Open (developer question): with E6's members six to nine, a VETERAN war left running for months could reach a leader with eight elites if the cap grows with time. | the director's purchases and `[enemy]` lines |
| E8 | Members past five for every enemy unit (developer, 2026-10-30: a VETERAN war left running for months may reach a leader with eight elites, but a month is too fast; NORMAL and HARD may go past five in a much weaker way). `boss_escorts.cpp` now keeps extras per enemy slot (30..59), made on the field update for full generic units (five record members; the boss always): NORMAL 1 weak extra (base tier, 3 levels lower) from mission 25 on about half the full units; HARD 1 standard (never elite) from 15, 2 from 30; VETERAN 2 from 10, 3 from 25, 4 from 35, +1 after 30 days on the mission, elites within `war_director::elite_cap_now` (4, 6 after 90 days, 8 after 180); EASY none. Story units never. Extras are built in one bank R load per field update, freed (record zeroed) when the unit leaves the map or its slot changes leader, and made only while more than 25 enemy character records stay free. The same setup and grid hooks serve every unit (keyed by the battle's enemy unit and the grid side's unit). The map shows a "+N" tag under such a unit. Built, not played. | `[extras]` lines; "+N" on the map |
| E9 | Rewards follow the difficulty (developer, 2026-10-30: fighting a harder unit must not pay nothing). `war_director.cpp` `reward_scale`: the loot band's price ceiling `band_max` x0.85 / x1 / x1.25 / x1.5 (EASY..VETERAN), which raises battle drops, spoils and gifts (and enemy weapons, which use the same band); the director units' drop chance x0.8 / x1 / x1.2 / x1.4, at most 95%. A unit with members past five that leaves the field pays `reward_extras`: one roll per extra (equipment for each elite; half the time from mission 15 otherwise) and Goth 50 per extra + 200 per elite, times the scale, granted through the director's spoils on the field update. Built, not played. | `[extras] ... beaten` lines and the Good alert |
| B0 | Extend `tools/squad_stats.py` to write the vanilla squads per scene into a data file the composer reads (themes, leaders, sizes). | the table in section 2 regenerates |
| B1 | Research R1-R5 (large adjacency, builder, battle draw, AI targeting). | a capture of an enemy unit with two adjacent large members, drawn correctly, or decision D3 |
| B2 | L1-a and L1-b: confirmed attack meanings and the class value table. | `squad_eval.py` prices vanilla squads; values rise with scene |
| B3 | The composer with doctrines, competence and the story gate, behind the director's GARRISON path only. | sweep in band; the developer plays scenes 1, 7, 13 |
| B4 | The composer behind bought units, waves and events; doctrine labels in alerts and the overlay. | the developer plays one mission per chapter |
| B5 | Large members adjacent (if R1-R3 allow). | scenes with dragon and golem squads |
| B6 | L1-c: class Phase B, one weak family per step. | each family played before the next |
| B7 | Tuning: competence bands, counter-pick share, relax-phase drop. | the developer's "rival, not punisher" pillar holds over a full chapter |

## 9. Decisions for the developer

| # | question | proposal |
|---|---|---|
| D1 | Nerfs to strong player classes allowed? | decided: no |
| D2 | Do story and boss units stay vanilla? | changed 2026-10-30: the boss keeps its class and gear; its escorts promote and the boss unit is re-seated with the boss guarded |
| D3 | If enemies cannot draw adjacent large members, keep the vanilla rule for enemies only? | yes |
| D4 | Competence bands as in 5.4? | yes, tuned in B7 |
| D5 | Smart formations as an optional "perfect rows" mode, off by default? | yes |
| D6 | Counter-picks: at most one group per wave? | yes |
| D7 | Does the doctrine list in 5.2 match the game's feel; anything to add or drop? | developer's call |

## 10. Research

| # | question | how |
|---|---|---|
| R1 | Where does the enemy builder `func_80215360` place a large member's second square, and what blocks two large members side by side? | answered 2026-10-30: nothing. `func_ovlR_80215360` copies the template squares into the unit record and builds each character with `func_802150DC` (class, level from `0x801F1055` + offset, items through `func_80214FA0`); no class size is read. The no-neighbour rule is the game's data and the Organize rules only. |
| R2 | Can the battle scene draw two enemy large members in adjacent squares? | a template with two adjacent dragons, forced on a garrison; capture the battle |
| R3 | Do targeting and row attacks treat a large member as one square once it is adjacent to another large member? | the same capture; the battle log |
| R4 | The meaning of template bytes 25-34 (LordlyCaliber leaves them unreferenced). | read the builder's accesses to the template |
| R5 | Generic soldiers (member references 100+): which class do they fight as? | the enemy character table `0x80195580` with a capture |
| R6 | Item choices in the template (`+0x02..+0x05`, u16 big-endian, `0..0x115`): confirm they equip in battle. | a template with a known weapon; capture |
| R7 | The attack-table fields `+0x08` power, `+0x09` formula, extra effect (class doc R2). | class doc |

