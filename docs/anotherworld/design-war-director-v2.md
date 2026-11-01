# Design: War Director v2 — a replayable enemy that is a rival, not a punishment

Status: plan, 2026-10-29 (revision 2, after developer review). Nothing in this
document is built. The research, the built phases 1-3 and the addresses are in
`design-war-director.md`; this document replaces its goals (section 1), its
design (section 4) and its phases (section 8).

## 1. Goal

**Each mission plays differently on every playthrough. The enemy reacts to the
player like a rival commander: it creates threats and opportunities, gives the
player room to breathe, and makes the player's own characters the source of the
stories. The story's order of events stays intact.**

Developer constraints (2026-10-29):

- The story order matters. No Ogres before their mission, no Lodis forces too
  early.
- Strength must fit the moment. A five-member unit on mission 1 is too much,
  unless the player has sat on the mission for months.
- The director is not nonstop punishment. Difficulty settings do not fix that;
  the design has to.
- Rewards must be fun and varied. The game's item list is limited, and the
  answer is not only "better items".

Testable pillars:

| pillar | the player sees | checked by |
|---|---|---|
| **Variety** | a replay has different garrisons, events and rewards | two new games on one mission differ |
| **Rival, not punisher** | after a hard stretch the enemy eases off; threats come with warning and a way to answer | the developer never feels the enemy is unfair or endless |
| **Choices** | the director offers opportunities as well as threats | each mission offers at least one optional opportunity |
| **Ownership** | rewards land on the player's own characters and towns, not only in the inventory | the mission report names which character earned what |
| **Story order** | no class, race or faction appears before the story introduces it | the debut check (4.2) passes for every mission |
| **Readability** | every director action has a reason the player can read | alerts, markers and the report explain each one |

## 2. Player psychology the design follows

| principle | what it means here |
|---|---|
| **Tension and release** (Left 4 Dead's AI director) | The director tracks the player's recent stress: fights, losses, units hurt. When stress is high it stops building threats and switches to a relax phase. Pressure comes in waves with real gaps. |
| **Loss aversion** (a loss weighs about twice a gain) | A loss is always recoverable and always telegraphed. A town is never taken without warning. Defending something pays, so an attack is also a chance. |
| **Agency** | Threats are announced before they act, with a time and a target. Opportunities are optional. The player chooses which to answer. |
| **Competence and growth** | Rewards that make the player's characters better at what they did: the knight who held the bridge gets a feat. |
| **Relatedness** | Named enemy lieutenants become rivals; the player's characters earn titles. Stories are about people, not numbers. |
| **Variable rewards with a floor** | Drops are random, but a "pity" counter guarantees a good one after a dry run. |
| **Peak-end rule** | The mission report highlights the best moment (a lieutenant beaten, a town held against the odds). |
| **No learned helplessness** | Escalation is slow, bounded and announced ("the enemy grows bolder"). It never compounds without limit. |
| **Fair opponent** | The enemy pays for its units and loses income when it loses towns. The player can see that. |

## 3. What is wrong with what is built

From the Tenne Plains replay (offline, against the ROM, 2026-10-29):

| # | problem | effect |
|---|---|---|
| P1 | The garrison seed is `mission * 7919 + template * 104729`. | Every playthrough of a mission gets the same units. |
| P2 | Bought units always take the most common leader and members in a fixed order. | Tenne Plains buys "Witch + Hellhound" every time. |
| P3 | The pool is the mission's own generic classes only. | Few distinct units. |
| P4 | Drops are keyed by the defeated unit's class; only monster classes have rows. | Units led by humans drop nothing. |
| P5 | The loot upgrade scales with `1.10^mission`. | Useless early, extreme late (Halt Hammer to Celestial Hammer on mission 30). |
| P6 | A bought unit starts at the front line and copies another unit's AI bytes. | It has no readable purpose. |
| P7 | Income depends on time only. | The player's progress is not part of the loop. |
| P8 | Credits accrue at the same rate whatever the player is going through. | The enemy keeps pushing during the player's worst moment. |
| P9 | Nothing is shown to the player. | The director is invisible. |
| P10 | Phase 3 deployment has never run in play. | The core mechanism is unproven. |

## 4. The enemy

### 4.1 The war seed

- Rolled at a new game, stored per save slot (`<config>/director/<slot>.cfg`).
  Every director roll mixes it with the mission index and a counter.
- A new game gives a different war. A reload of the same save replays the same
  rolls (proposal; decision 2 in section 11).

### 4.2 Story order: the debut gate

A class, race or faction is available to the director only once the story has
introduced it. This is a hard rule:

- **Classes**: a class is a card only on or after its first appearance in the
  campaign (`docs/mission-enemies.md`, debut table). Story-only first
  appearances count: the director may not show a class before the story does.
  Ogre: mission 8. Skeleton and Ghost: mission 9.
- **Factions**: classes tied to a story faction are cards only on missions
  where that faction is the enemy. Lodis forces (Knight Templar and their
  units) are not cards before the story brings Lodis in. The faction list per
  mission is data the developer reviews (research R14).
- **Advanced classes** (Elite tier, 4.4) follow the same gate.
- **Pool**: the mission's own generic classes (weight 3), plus classes that
  passed the gate on an earlier mission and share the mission's theme tags
  (`human`, `beast`, `dragon`, `undead`, `golem`, `flyer`; weight 1).
  Mission 1 therefore uses only its own classes.

### 4.3 Strength fits the moment

Unit size and level come from the **campaign stage** and the **time on the
mission**, not from credits alone.

| stage | leader + members (normal) | lieutenants |
|---|---|---|
| chapter 0-1 (missions 1-9) | leader + 1-2 | none on mission 1-2 |
| chapter 2 | leader + 2-3 | yes |
| chapter 3+ | leader + 3-4 | yes |

- **Par time**: each mission has a par time in in-game days (from a normal
  play; the developer sets the table). Before par, the director's size cap is
  the stage cap.
- **Stalling**: after par, the director grows bolder: +1 member allowed per
  par length past it (at most the stage cap + 2, so five members is reached
  only after a long stall), orders form more often, and the alert says so
  ("The enemy grows bolder"). It stops growing at that bound.
- Levels follow the mission base level (`0x801F1055`) plus the tier bonus,
  never the time.

### 4.4 Shapes, tiers and lieutenants

- **Shapes** from the game's own templates, by role: Raider (fast or flying),
  Line (front-heavy), Battery (casters or archers), Warband (leader and
  soldiers). Leader and members are drawn by weight, not "the most common".
- **Tiers**: Common; Veteran (+2 levels, best gear for the class); Elite (the
  advanced class, gated by 4.2); Lieutenant.
- **Lieutenants**: a generic leader with a name and a title ("Gerold the Grey,
  Knight"). If his unit is routed, he escapes and returns later with +1 level.
  He remembers who beat him and seeks that unit. Beating him for good is a
  highlight with its own reward (6.2). At most one at a time. Names exist only
  in the port's overlay, alerts and report.

## 5. The rhythm

### 5.1 Intensity

The director keeps an **intensity** value, as Left 4 Dead does:

- It rises when the player fights, loses a unit, loses a town or has units
  below half strength.
- It decays over in-game hours.

Phases:

| phase | when | director |
|---|---|---|
| Build-up | intensity low | forms orders, offers opportunities |
| Peak | an order arrives | fights happen |
| Relax | intensity above a threshold, or right after a peak | no new threats for 8-16 in-game hours; opportunities may still appear |

The relax phase is what stops nonstop punishment (P8). It cannot be skipped.

### 5.2 Orders (threats)

| order | trigger | target |
|---|---|---|
| Garrison | mission load | enemy towns (built, phase 2) |
| Raid | build-up | a player town with no unit near it, or a lone unit |
| Counterattack | a grudge reaches its threshold | the town with the highest grudge |
| Reinforce | a player unit heads for an enemy town | that town |
| Rally | the player first nears the boss | the boss stronghold; raids stop after it |

- Every order **forms** before it marches, at a location, with a time, and the
  player gets an alert and a map marker ("A raid is forming at Buchanan, 3 h").
- **Grudge**: liberating a town adds grudge to it; it decays; a player unit
  standing in the town halves the gain. The counterattack goes where the grudge
  is highest, so the player can predict it.
- **Income** comes from towns and strongholds the enemy holds, and stops while
  the player holds 70% of the mission's towns. Liberating starves the enemy.

### 5.3 Opportunities

The director also creates chances that the player may take or ignore. Each one
appears in build-up or relax, with an alert and a marker, and expires.

| opportunity | what happens | reward |
|---|---|---|
| Supply train | a weak enemy unit carries supplies between two enemy towns | intercept it: consumables and goth; the enemy loses income |
| Deserters | a small enemy unit wants to defect at a town | reach it in time: it joins the army (research R15) |
| Town plea | a liberated town under raid asks for help | defend it: the town's favour (6.3) |
| Wounded lieutenant | a routed lieutenant rests at a town | catch him before he heals |
| Rumour | a villager points to a hidden item | the map's own hidden-item list (table P), revealed early |
| Mercenaries | a neutral unit offers to fight for one mission | pay goth: an extra unit for this mission |

### 5.4a Events: goals, catalog and roadmap (developer review 2026-10-30)

The first played version had one event, the supply train, every few director
hours on every mission. The developer's review: events must be days apart and
must not overlap, and one event that recurs on every mission adds little
replayability. Variety comes from many event types, drawn differently each
mission and each war.

#### Goals

| goal | rule |
|---|---|
| Variety | a catalog of event types; each mission draws a few from it with the war seed, weighted by the mission's theme and the war condition, so two wars on one mission get different events |
| Rhythm | an event scheduler, not one timer per event: at most one event active at a time, the next one 1..3 in-game days after the last one ends, and only in build-up or relax phases (never in the middle of an attack wave) |
| Choice | every event names its place, has a deadline, and offers a trade: chase it (divert a unit, take a risk) or ignore it (the opportunity is lost, or the threat lands) |
| Readability | an alert when it starts, a reminder before the deadline, an alert when it ends either way; later a map marker (field overlay) |
| Consequence | the outcome shows: "the enemy's next attack is delayed", "the town is grateful", "the deserters joined you" |
| Story safe | events use only classes that passed the debut gate (4.2) and never touch story units |

#### Catalog

| event | what happens | needs | status |
|---|---|---|---|
| Supply caravan | a weak unit stops at an enemy town; take it within the deadline for consumables and Goth, and the next attack is delayed | location names; real travel between towns needs R2 | built without names or deadline; days apart, one at a time (2026-10-30) |
| Bounty | a veteran unit (+2 levels, best gear) holds an enemy town; beat it before it leaves for equipment from the band above | location names | not built |
| Fortified town | the enemy reinforces one town ahead of the player; liberating it pays extra spoils | location names | not built |
| Town plea | an attack wave aims at a town the player liberated; the town asks for help; held to the end, it becomes loyal and sends a gift | location names | loyalty gift built; the plea itself not built |
| Night raid | a wave forms only at night and strikes a lone unit | in-game clock R5 | not built |
| Rumour | a villager names a hidden item on the map; it is revealed early | the map's hidden-item list (table P) positions | not built |
| Deserters | a small enemy unit offers to defect at a town; reach it in time and it joins | adding a recruit R15 | not built |
| Mercenaries | a neutral unit offers to fight for this mission for Goth | adding a player unit R15 | not built |
| Wounded lieutenant | a routed lieutenant rests at a town; catch him before he heals | lieutenants (4.4) | not built |
| Escort | a liberated town's supplies travel to the player's headquarters; protect them | unit destinations R2 | not built |

#### Per mission

The scheduler draws a mission's event list when the mission loads: two to four
events from the catalog (fewer on the first missions), no type twice in a
mission, weighted by the war condition (Rich Caravans: more caravans; Beast
Season: bounties on beasts). The list and the order come from the war seed, so
a replay of the same war is the same, and a new war differs.

#### Roadmap

| step | content | unlocks |
|---|---|---|
| E1 | Location names: read each location's name from the game data (`ktenmain.bin` records, location ids from the mission's `scincsv` file) | named alerts for every event |
| E2 | The event scheduler: per-mission list, one active event, days apart, start / reminder / end alerts, deadlines | rhythm and choice for all events |
| E3 | Supply caravan, bounty and fortified town on the scheduler (all hold-at-town units, buildable today) | three event types without new research |
| E4 | Town plea on the scheduler, tied to attack waves | the counterattack becomes a story |
| E5 | The field capture session (R2, R5, R10, R11) | travelling caravans, escorts, night raids, true battle results |
| E6 | Recruit research (R15) | deserters, mercenaries |
| E7 | Lieutenants (4.4) | wounded lieutenant, lieutenant bounties |

## 6. Rewards

The vanilla item list has 278 records and no spare entries, and an owned item
is only an id and a count (`0x80196B20`), so per-copy rolled items are not
possible. New items (ids up to 510) are added through content packs
(`design-content-expansion.md`): lieutenant signatures, faction gear, town
gifts and relics. Rewards still come in several kinds, and items are one of
them.

### 6.1 Spoils (items)

- A beaten director unit rolls by tier: Common 30%, Veteran 60%, Elite and
  Lieutenant 100%.
- **Bands** per chapter set the price range of each item category from what
  the shops of that chapter sell. Elite and Lieutenant drop from the next band.
  Consumables have bands too (Heal Leaf, Heal Seed, Heal Pack; Power Fruit,
  Angel Fruit; Revive Stone later).
- **Choice**: a Lieutenant or an opportunity offers three items and the player
  picks one.
- **Pity**: after N beaten units with no drop, the next drop is certain.
- Unique items, 50000-goth spells and quest items are never given (v1 rule).

### 6.2 Feats (the player's characters)

- A character who does something notable earns a **feat**: a title shown in
  Organize and a small permanent stat gain written to the character record
  (`0x80193BE0 + i*0x38`).
- Examples: *Bridge Holder* (held a town against a counterattack: +2 VIT),
  *Rivalbane* (beat a lieutenant: +2 STR), *Swift Hand* (intercepted a supply
  train: +2 AGI), *Lifeline* (healed in three fights of one mission: +2 MEN).
- Caps: one feat per character per mission; each feat once per character.
- Feats are stored per save slot by the port and re-applied on load
  (research R13).

### 6.3 Town favour

- A town the player liberated and then defended becomes **loyal**: its shop
  gives a discount, it can warn of the next raid early, and once per mission
  it gives a gift (a consumable or a recruit).
- A town the player let fall without a fight does not become loyal.

### 6.4 Recognition

- **Mission report**: the best moment, the feats earned, the lieutenants
  beaten, the towns held, the spoils.
- **Bestiary and war journal**: classes met, lieutenants beaten, feats earned,
  across the save.
- **Renown** per war: a score from the report. It unlocks war conditions and
  cosmetic titles for the war journal; it does not raise power.

## 7. War conditions

Each mission may roll one condition from the war seed, shown before deployment.
Each condition has an upside and a downside, so none is pure punishment.

| condition | downside | upside |
|---|---|---|
| Beast Season | beasts and flyers weight x3 | beast drops roll one band higher |
| Iron Garrisons | garrisons +1 member | liberating a garrisoned town gives spoils |
| Night Raiders | raids only at night | days are quiet |
| Grudging Lord | counterattacks form faster | each defended town counts double for favour |
| Rich Caravans | supply trains are escorted | supply trains carry more |
| Thin Supply | enemy units +1 level | the enemy fields fewer units |

Optional, the developer decides: a **Wager** at deployment that adds a second
condition for a guaranteed spoils choice from the boss.

## 8. Readability and UI

These go into the field overlay of `design-field-controls.md`:

- **Intensity and threat** in the top bar: a calm/tense/peak marker and the
  next order's form time.
- **Alerts and markers** for orders forming and marching, opportunities
  appearing and expiring, lieutenants seen and fled, feats earned.
- **Briefing** before deployment: the war condition.
- **Report** at the end (6.4).

## 9. Research needed

| id | question | method |
|---|---|---|
| R2 | unit destination and movement fields | field-controls capture presses 3-6 |
| R5 | in-game clock (hour, day, night) | two dumps an in-game hour apart |
| R8 | how the enemy AI chooses and keeps a target | read `func_ovlN_801CF0D0` after R2 |
| R10 | town state at runtime, liberation event | dumps before and after liberating a town (static half done: `0x801951EC + i*0x24`) |
| R11 | unit destroyed and unit retreated events | dumps around a won battle and a battle where the enemy retreats |
| R12 | the drop key `0x80197708` and the pending-id slot `0x8019369C`: which can carry a tier result | static read of the writers; one played battle |
| R13 | the save slot index at load, for per-slot data (seed, feats) | static: the save loader `func_80185128` |
| R14 | which missions have which enemy faction (Lodis, Zenobia, undead) | the developer reviews a per-mission list generated from the templates |
| R15 | how the game adds a recruit to the army (for Deserters) | static: the routine that adds a recruited character; Organize's character table |
| R16 | par time per mission | the developer gives typical in-game days per mission |

R2, R5, R10 and R11 share one capture session with the field controls work.

## 10. Phases

Each phase ends with a developer playtest that answers the pillar checks.

| phase | content | done when |
|---|---|---|
| 0 | Play existing deployment on Tenne Plains (P10). One capture session for R2, R5, R10, R11. Generate the faction list (R14). | a bought unit appears, walks and fights; each R item has an address |
| 1 | War seed, debut gate with factions, stage caps, shapes and weighted picks (4.1-4.4 without lieutenants). **Built 2026-10-30, not played** (`app/features/war_director.cpp`): the seed is rolled when a new game reaches the name-entry form (scene `0x07`) and kept in `<config>/war_seed.cfg` (one per installation, not per save slot yet, R13); the garrison, every purchase and every deployment mix it in; purchases draw the leader by weight and a shape (Line, Battery, Mixed) that prefers front- or back-row members; members per unit are capped at 2 up to mission index 9, 3 up to 18, 4 after (bands by the loader's mission index, provisional); the DEBUG tab shows the seed and the next units. Not built: widening the pool to earlier missions' classes and the faction gate, which need the mission index to story order map (R14) | two new games on mission 3 differ; no class or faction appears early; mission 1 units stay small |
| 2 | Intensity and the relax phase (5.1), orders with targets, grudge, town income (5.2). **Built 2026-10-30, not played** (`app/features/war_director.cpp`, `director_spawn.cpp`): towns are read from the location state table each frame; income is scaled by enemy-held towns against the start and stops while the player holds 70%; liberating a town adds grudge (30, a stronghold 60), losing one adds intensity; HP lost by the player's units on the field adds intensity as a percentage of their total HP; intensity decays 10 per director hour, and at 60 the director relaxes for 8..16 hours (no purchases, no deployments, credits banked up to three campaign units). Bought units start at the enemy-held location nearest the highest-grudge town, else nearest the player's units. Not built: a march target for the deployed unit (needs R2/R8), announced orders | after a hard fight the enemy eases off; a liberated town draws a counterattack the player can predict |
| 3 | Telegraphs, intensity marker, briefing, report (8) on the field overlay **Started 2026-10-30, not played**: an alerts layer (`app/features/alerts.hpp`, `alerts_view.cpp`, `ui/alerts.rml`, top right, fades after 7 s) shows: a unit forming, with its classes and the time to deployment (a bought unit now waits 2 director hours, a patrol 1, before it deploys); a unit deployed and where (near a liberated town, near your army, at the stronghold); the relax phase; a town liberated or retaken. Not built: map markers, the threat meter, briefing and report (need the field overlay) | the developer can say why each enemy unit exists |
| 3a | Wave filling (playtest 2026-10-30, mission 3, `build-dist/run-cover.log`): 8 of 11 waves attacked with one unit, because a patrol could only take the first place of a wave and the second waited for a campaign unit the bank seldom reached within the 48-hour limit. A patrol now takes any open place once the wave has waited 24 hours for a campaign unit; two units bought on the same pass are numbered 1 and 2 of the wave (both read "1 of 2" before); the supply caravan alerts say it arrives at a town (it appears there, it does not travel); buys and releases log the mission hour. Not played | most waves reach their size |
| 4 | Spoils with bands, choice and pity (6.1) **Started 2026-10-30, not played**: units the director deployed roll spoils when they leave the field (large 60%, patrol 30%, certain after three dry rolls); large units give equipment half the time; equipment comes from a price band (`150 + 50 x mission index`, provisional), consumables by stage (Heal Leaf, Heal Seed, Power Fruit; Heal Pack and Angel Fruit from index 10; Revive Stone from 19); the game's own `func_ovlN_801DC740` grants them on the field update, with a "Spoils" alert. The drop-table upgrade is capped by the same band. Not built: tiers beyond large/patrol, the choice of three | mission 1 drops something worth equipping; mission 20 drops are one band ahead |
| 5 | Opportunities (5.3), town favour (6.3) **Started 2026-10-30, not played**: supply trains (a leader alone, every 8..16 director hours from hour 6 while the enemy holds a town and is not resting; announced when forming and when it sets out; beating it always gives two consumables and `100 + 20 x mission index` Goth, and the enemy loses 30% of its banked credits); town favour (a counterattack sent after a liberated town, beaten while the player still holds the town, makes the town loyal and it sends a consumable gift once per mission). Not built: deserters (R15), town plea, wounded lieutenant, rumour, mercenaries, loyal-town shop discount | each mission offers at least one; taking one feels worth the detour |
| 6 | Lieutenants (4.4), feats (6.2), stalling (4.3) | a lieutenant becomes a rival within a mission; a feat lands on the character who earned it |
| 7 | War conditions (7), renown and journal (6.4), difficulty settings | three replays of one mission feel different |

## 11. Decisions for the developer

1. Factions: which classes belong to Lodis and other story factions, and from
   which mission each faction may appear (R14).
2. Reload behaviour: replay the director's rolls (proposal) or reroll.
3. Par times per mission, and how long a stall must be before units reach five
   members.
4. Feats: wanted? Stat gains or titles only?
5. Lieutenant names: generated, or taken from the game's own generic names.
6. Opportunities: which of the six to build first.
7. War conditions: always on, or a setting; the Wager wanted or not.

## Finding 2026-10-30: bought units never attacked (run-mouse1)

Developer report: many in-game days on Tenne Plains with no attack. The log shows every bought unit spawned at location 2 with "AI from slot 31" and then, from 1141 h, `spawn waiting: no free enemy slot in 32..48`. `director_spawn.cpp` copied the AI bytes (map object `+0xBA`/`+0xBB`) from the first live generic enemy, which on Tenne Plains is the slot 31 Wizard with the pair `01 04`, the hold pair the boss and the caravans use. Every unit held its spawn town, none was ever fought, and the slots filled. Marching units carry other pairs (Tenne Plains' slot 33 Witch `05 02`; mission 42's marching Knight Templars `0E 03`). Fix: the reference skips a unit with `01 04` and uses it only when no other generic unit has been seen on the mission (a remembered mover's bytes are kept after its unit is beaten). Not played.

## The enemy commander (built 2026-10-30, not played)

`app/features/enemy_commander.cpp`, on the field update (game thread). Developer review the same day: the director bought units but nothing decided what they did; "the AI is meant to be fun, not brutal", with real defence, strategy and a difficulty that scales; story units with scenes must never be moved.

**Story safety.** It orders only the units the director deployed (`war_director::director_units`, supply trains excluded). The boss, named characters, garrisons and units with spawn triggers are untouched. Research item: decode which spawn trigger values (`+0x0C` flag, the halfword copied to `0x8019369C[slot]`) mean a scene, so plain garrisons can join later.

**Jobs**, chosen every decision interval: RECOVER (below 35% HP, to the nearest enemy town), COUNTERATTACK (a town lost since the last decision), DEFEND (an enemy town with a seen player unit near and nobody home), RALLY (a seen player unit near the boss), RAID (the player's headquarters with no player unit near it), STRIKE (the rest converge on the seen player unit nearest them, re-aimed every 24 field updates; see "Alerts, boss rush and targeting" below). Orders use the game's move `func_ovlN_801CC5D0` after `func_ovlN_801D45F8`. A unit whose destination (`+0x28/+0x30`) no longer matches its order at the next decision is logged as drifted, so a play session shows whether the game's own AI overrides orders.

**What it knows.** EASY and NORMAL: a player unit within a fifth of the map's width of any enemy unit or enemy town. HARD and VETERAN: every unit.

| DIFFICULTY (GAMEPLAY > WAR DIRECTOR) | decides every | knows | strike size | defend, rally, recover | raid HQ | warnings |
|---|---|---|---|---|---|---|
| EASY | 12 h | sight | 1 | no | no | yes |
| NORMAL (default) | 6 h | sight | 2 | yes | no | yes |
| HARD | 3 h | all | 3 | yes | yes | yes |
| VETERAN | 1 h | all | 3 | yes, rally 4 | yes | no |

**Across the war.** The interval shortens by up to a quarter over the first thirty missions, and a strike's size is capped at 1 up to mission 11 and 2 up to mission 23. The other systems that scale with the mission: unit size caps (2 / 3 / 4 members by missions 9 / 18), promotions (0% to mission 6, then +2.5% a mission, 75% from 36), weapon upgrades (members 35% + 2% a mission up to 75%, price band 150 + 50 x mission), formation imperfection (35% / 25% / 15% off-row), boss extras (1, then 2 from mission 15), and the director's income (`coeff`: hours on the mission and `MISSION_BASE^mission`).

## Pacing and momentum (built 2026-10-30, not played)

Developer question, 2026-10-30: is the director a spawn table, a reactive budget with peaks and valleys, or a flat climb? Before this change it was a Risk of Rain credit budget that climbed with time and mission, released in waves, with one Left 4 Dead piece (a relax switch at intensity 60). A player who cleared missions quickly met less of it: income comes from hours on the mission, and freed towns cut it to zero at 70% held.

**Pacing** (`app/features/war_director.cpp`, `Phase`). Four phases replace the relax switch.

| phase | director | enemy commander | ends |
|---|---|---|---|
| BUILD-UP | buys and masses a wave | as before | the wave is released (full, or waited 48 h); intensity 60 goes straight to FADE |
| PEAK | buys nothing | as before | intensity 60, no wave unit left, or 72 h |
| FADE | income x0.5, buys nothing | no STRIKE or RAID; RECOVER below 60% HP; idle units DEFEND the nearest enemy town | 12 h |
| RELAX | income accrues, buys nothing, nothing deploys | as FADE, and no COUNTERATTACK | 24-48 h (war seed) |

Intensity now also rises 5 for each player character brought to 0 HP (on top of the HP share).

**Push.** On HARD and VETERAN, a PEAK whose intensity stayed below 20 makes the next BUILD-UP push: forming times and the partial-wave wait are halved and the wave has one more unit.

**Momentum.** When the mission index changes after a played mission (12 in-game hours or more, the player holding half its towns), the mission is scored: speed = 1 - hours / 240 (0.5 at the five-day par), clean = 1 - (summed HP lost %) / 300, score = speed x (0.5 + 0.5 x clean). Momentum = 0.6 x old + 0.4 x score, kept in `war_momentum.cfg` beside `war_seed.cfg` and reset by a new game. It is one value for the app, not per save profile.

- War chest: when the mission's prices are known, the campaign bank gets momentum x 0 / 1 / 2 / 3 campaign units (EASY / NORMAL / HARD / VETERAN).
- Wave size: on HARD, +1 for a push or momentum 0.5 and up; on VETERAN, +1 for each (at most +2).

**Last stand** (NORMAL and up, once per mission). When the player first holds 70% of the towns while the enemy still holds one, the campaign bank is raised to at least 1 / 2 / 2 campaign budgets (NORMAL / HARD / VETERAN), the phase returns to BUILD-UP with halved forming, the wave is released when the bank cannot pay for the next unit, and the enemy commander sends every free unit to counterattack `grudge_town()`. Income stops after it as before.

The DEBUG tab's DIRECTOR rows show the phase (hours left, pushing, LAST STAND) and the momentum (war chest, wave bonus).

Not done: "a player town under attack" as a stress signal (it needs enemy positions against town positions on the director's side).

## Alerts, boss rush and targeting (2026-10-30, not played)

Developer review the same day: too many alerts read as background noise, and they need not say what is attacking; a boss rush skipped the last stand; focus fire on the weakest unit is not fun ("Risk of Rain 2 and Left 4 Dead 2 did not pound the weakest into dust").

**Alerts.** One per event the player can act on, with no unit classes or counts.

| event | alert |
|---|---|
| a wave's first unit is bought | "Enemy forces are gathering for an attack" |
| the wave is released | "The enemy attacks" |
| RELAX begins | "The enemy pulls back to regroup" |
| a supply caravan arrives | the arrival only (the six-hour notice is gone) |
| the enemy retakes a town | kept |
| COUNTERATTACK, RAID | kept (they name the town) |
| a last stand | one, from the commander, naming the target |
| spoils | one line: "Spoils: item, item, N Goth" |

Removed: the per-unit massing and deployment alerts, "loses its strength", the FADE "pulls back", "Town liberated", the war chest, the strike's "converging", and the separate reinforced-unit and Goth lines.

**Boss rush.** The commander checks every decision whether a player unit is within twice its "near" radius of the boss, at every difficulty. That sets off the mission's last stand if it has not had one: the wave deploys at the stronghold, forms in a quarter of the normal time, and every free unit RALLIES to the boss ("The enemy rallies to defend its commander.").

**Targeting.** STRIKE picks the seen player unit nearest the free units; HARD and VETERAN add a preference for one standing near an enemy town. A unit below 35% HP is not chosen as a new target while another is in sight. A strike already under way keeps chasing its target however hurt it gets (developer: some risk is fine; players have healing and Quit Gates).
## Group size and variety (2026-10-30, not played)

Finding (run-pacing1, mission 1, about 13,660 in-game hours): the director bought 53 units. Every leader was a Wizard or a Witch, every member a Knight, Griffin or Hellhound, no campaign unit had more than two members, and the 10 patrols were a lone Wizard or Witch. The developer: too little variety, and "15 days is a long time to still be fighting 3 as a max".

Changes in `app/features/war_director.cpp`:

- **Size grows with time on the mission.** `member_cap` is the stage cap (one fewer on EASY, one more on HARD and VETERAN), +1 after 5 in-game days and +1 more after 15, at most four members. On mission 1 NORMAL: leader + 2, then + 3 from day 5, then a full unit of five from day 15. This replaces the single +1 at ten days.
- **Budgets follow the size.** A unit of n members gets `0.9 + 0.35 n` average generic units (1.6 for two members, the old campaign budget; 2.3 for four).
- **Patrols have members.** A patrol may have two fewer members than a campaign unit, at least one, with the same budget rule. The old 0.6 budget paid for a lone leader on mission 1.
- **Base humans join every pool.** Soldier (1), Fighter (2) and Amazon (4) join the member pool, and Fighter and Amazon the leader pool, each with weight 1. No higher tier and no family the mission has not shown is added (developer: "Fighter amazon soldiers", no undead). The garrison rebuild keeps the mission's own pool.
- **Plans spawn as bought.** A plan holds at most two member classes of three each (a template has two groups), and `take_spawn` writes it as is (`rebuild(..., exact)`). Before, `take_spawn` re-rolled the classes from the plan's members and could drop one more member a quarter of the time.

