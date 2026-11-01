# Class rebalance and new promotion trees

Status: revision 3. Phase A (data) is implemented as the content pack
`app/content/class-rebalance.json` (2026-10-31, `design-content-database.md`);
the rules that are not data stay in `app/features/class_rebalance.cpp`. Phase B (the Promote tab trees in
`app/src/honors.cpp`) was removed with Honors on 2026-10-29: class changes are
vanilla apart from the alignment requirement, which is dropped
(`docs/anotherworld/design-game-ui.md`). Quest gating and the Faerie/Gremlin behaviour were
never built. Class data comes from `docs/class-catalog.md`, which
`tools/class_catalog.py` generates from the ROM.

**LARGE UNITS ONE SQUARE (2026-10-29).** The class table keeps size 2; the size
getter answers 1 only to the Organize screen's rules (`ogre_class_size`, see
`docs/anotherworld/DECISIONS.md` "one square"). Writing 1 into the table drew large-only classes
as Black Knights in battle and the field menus.

## 1. Goal and rules

Make the weak unit families worth fielding, each with its own role, without
weakening anything.

- **No nerfs.** Only weak classes are raised. Humans stay as they are; their
  strength is gear variety and the class ladder.
- **Later tiers stay better** inside a line (Paladin over Knight over Fighter).
  Variety comes from different lines.
- **Promotion links alone are not enough.** Each weak family gets a mechanic or a
  row profile that makes it useful in its own way.
- **Special units open with their quests.** Lich, Vampire, Angel Knight, Seraph
  and the other quest classes appear on the Promote tab only after their vanilla
  quest is complete (R4 finds the flags). Vanilla's routes stay as well, such as
  Wizard to Lich with the same four items.
- **Large units take one square, always.** This is built into the app (the Small
  Large Units mod is retired). At one square, large units were still weaker than
  humans at equal level, so they keep the growth below with no reduction.
- **Choices**: Hawkman picks Vultan or Raven; Young Dragon picks its element.
- **Only existing classes.** Every class here has sprites and animations.

## 2. What the data shows

"Stat total" is the sum of the seven level-1 base stats (HP, STR, VIT, INT, MEN,
AGI, DEX); "growth" is the sum of their per-level gains. The classes compared:

| class | tier | stat total | growth | per-stat growth (HP STR VIT INT MEN AGI DEX) |
|---|---|---|---|---|
| Fighter | base | 343 | 16 | 4 3 2 2 2 1 2 |
| Knight | class | 458 | 21 | 4 4 3 2 3 3 2 |
| Black Knight | advanced | 719 | 30 | 5 6 5 4 4 3 3 |
| Cleric | class | 443 | 20 | 3 2 3 4 4 2 2 |
| Priest | advanced | 713 | 22 | - |
| Ogre (size 2) | none | 423 | 24 | 6 5 4 2 3 2 2 |
| Goblin | none | 396 | 17 | 4 4 2 1 2 3 1 |
| Saturos | tier 1 | 373 | 21 | 3 3 2 4 3 3 3 |
| Gorgon | tier 1 | 406 | 21 | 3 3 2 2 3 4 4 |
| Faerie | none | 329 | 22 | 2 1 2 3 5 5 4 |
| Gremlin | none | 325 | 24 | 2 1 2 4 5 5 5 |
| Zombie | none | 343 | 16 | 4 3 2 1 2 2 2 |
| Skeleton | none | 346 | 17 | 3 4 1 1 2 3 3 |
| Ghost | none | 361 | 15 | 2 2 1 4 2 2 2 |

What this explains:

- **Ogre** starts ahead of a Fighter (STR 88 against 50) but grows by 24 points a
  level against Black Knight's 30, from a much lower base (423 against 719). By
  the time a Fighter is a Black Knight, the Ogre is far behind, and it occupies
  two squares while doing it.
- **Saturos** is listed as tier 1 but has a base-tier total (373) and mostly 3s
  in growth. It is a weak fighter and a weak caster at once.
- **Goblin** has the lowest growth of any combat class (17) and no promotion.
- **Gorgon**'s front-row Gaze of Terror petrifies (attack `0x19`, target pattern
  4). Its arrows are a plain Shoot 1/2 from DEX 62 with +4 growth, the same shape
  as an Archer, without the Archer's gear or ladder.
- **Faerie** has Throw a Kiss (attack `0x17`): an ally-side effect (target
  pattern 9, effect byte 2) that the community guides describe as "powers up
  friendly units". Its back-row Magic Missile (attack `0x1E`) uses formula byte
  `0x01`, the same byte physical attacks use, so it scales with Faerie's STR (38,
  growth +1) instead of INT. Real damage spells use formula byte `0x07` (Fireball
  `0x3F`, Lightning `0x37`).
- **Undead** progress in vanilla by being hit: a Zombie burned with fire becomes
  a Skeleton, and a Skeleton hit by wind or fire magic becomes a Ghost
  (ogrebattle64archive.com undead guide). All three stay at base-tier stats, so
  the change is sideways, not upward. Basic undead come back after a battle unless
  a holy attack finishes them.

Attack table fields used above (`ROM 0x6099C + id*0x10`, see
`tools/class_catalog.py`): `+0x04` kind (0 melee, 1 ranged, 3 spell, 4 special,
5 breath), `+0x05` element, `+0x06` target pattern, `+0x07` effect (1 harmful,
2 beneficial, 4 sleep, 8 paralysis, 10 poison), `+0x08` power, `+0x09` formula
(1 physical, 7 magic, 8 status chance, 9 heal), `+0x0A..+0x0D` an extra effect
(`08 <stat> ff <amount>`). The `+0x08`, `+0x09` and extra-effect meanings are
inferred from patterns and need confirming (section 7, R2).

## 3. Trees

| line | base | step 2 | step 3 | how each step is reached |
|---|---|---|---|---|
| Undead | Zombie | Skeleton | Ghost | vanilla triggers (fire, then wind/fire magic) stay; also buyable on the Promote tab |
| Lich | Wizard or Ghost | Lich | - | vanilla Lich quest and its four items only |
| Vampire | (male, chaotic) | Vampire | - | vanilla Vampire quest only |
| Faerie | Faerie | - | - | stays one class at "tier 1.5" (section 4.1) |
| Holy | - | Angel Knight | Seraph | vanilla quests only |
| Gremlin | Gremlin | - | - | stays one class (section 4.2) |
| Raiders | Goblin | Saturos | - | Promote tab |
| Ogre | Ogre | - | - | stays one class with a real growth curve (section 4.3) |
| Gorgon | Gorgon | - | - | stays one class (section 4.4) |
| Hawk | Hawkman | Vultan or Raven (choice) | - | Promote tab; alignment no longer decides |
| Beasts | Griffin, Wyrm, Hellhound, Cockatrice | Opinincus, Wyvern, Cerberus, Sphinx | - | Promote tab |
| Golem | Golem | Stone Golem | Baldr Golem | Promote tab |
| Dragon | Young Dragon | elemental dragon (choice) | elder dragon | Promote tab |

## 4. Unit changes

Numbers are proposals for Phase B tuning. "Total" is the base stat total.

### 4.1 Faerie: tier-1.5 healer and booster

Role: the support a unit without a Cleric can take. It heals worse than a
Cleric and much worse than a Priest, and it boosts allies when nobody needs
healing.

| row | today | proposed |
|---|---|---|
| front | Throw a Kiss x1 | Kiss x1 |
| middle | Throw a Kiss x1 | Kiss x1 |
| back | Magic Missile x2 (STR-based) | Magic Missile x2 (INT-based) |

**Kiss behaviour** (new):

1. If an ally (the Faerie included) is below 70% HP, heal the ally with the
   lowest HP fraction. The Faerie only targets itself when it is that ally.
2. Otherwise, power up the ally with the highest attack stat that is not yet
   boosted.

**Heal strength**: 60% of a Cleric's Healing (attack `0x57`, single target) at
equal MEN. A Cleric heals twice from the back row; a Priest heals a row with
Healing Plus. At level 10 that is about one third of a Cleric's healing per
round and one fifth of a Priest's.

**Power up**: +15% to the target's damage for the rest of the battle, stacking
twice (+30% at most). An OB64 battle lasts three rounds, so one Faerie in the
front row adds about +15% to one ally's last two attacks. A Faerie in the middle
row does the same once per round.

**Magic Missile**: formula byte from `0x01` (physical) to `0x07` (magic), and
INT growth from +3 to +4. It then scales like a Witch's single-target spell,
from a lower INT.

**Stats**: total 329 to 420 (HP +20, VIT +15, INT +12, MEN +14, DEX +10, STR
+20); growth unchanged at 22, INT +1.

### 4.2 Gremlin: the debuffer

Mirror of the Faerie: Kiss lowers the enemy with the highest attack stat by 15%
(stacks twice); back-row Abyss keeps its sleep. Abyss (`0x1F`) used the same
physical formula byte as Magic Missile and now uses the magic one. Gremlin's
damage is meant to beat a mage's: base INT 64 against a Wizard's 55, with the
same +5 INT growth. Stats: total 325 to 420; growth 24 to 25.

### 4.3 Ogre: a size-2 bruiser that keeps up

Rule for size-2 classes: they take two squares and two of the five member
slots, so at equal tier they should do about the work of 1.5 humans.

| | today | proposed |
|---|---|---|
| total | 423 | 520 |
| growth | 24 (6 5 4 2 3 2 2) | 32 (8 7 5 2 3 3 4) |
| rows | Crush 3/2/1 | Crush 3/2/1 |

At level 15 that gives about 1000 stat points against a Black Knight's about
1170, so a Black Knight stays the stronger advanced unit, and the Ogre stops
falling away from the human ladder. Large units take one square (section 1), and
the growth stays at 32.

### 4.4 Gorgon: keep the gaze, fix the bow

| row | today | proposed |
|---|---|---|
| front | Gaze of Terror x1 (petrify) | unchanged |
| middle | Shoot x1 | Shoot x2 |
| back | Shoot x2 | Shoot x2 |

DEX growth +4 to +5 and AGI unchanged. Total 406 to 450. The middle row becomes
a real archer slot, and the petrify gaze stays the reason to field one.

### 4.5 Goblin and Saturos: raiders

Goblin (base) becomes a fast skirmisher:

| row | today | proposed |
|---|---|---|
| front | Slash x2 | Slash x2 |
| middle | Slash x1 | Slash x2 |
| back | Slash x1 | Slash x1 |

A row only gets an attack its class already has an animation for, so Goblin
keeps Slash (no bow animation) and Hawkman keeps Strike (Wind Shot belongs to
Vultan's sprite). Their rows gain attacks instead.

Total 396 to 440; growth 17 to 22 (4 4 3 2 2 4 3).

Saturos (Goblin's promotion) becomes a battle mage, the only class with two
melee strikes in the middle row and spells in the back:

| row | today | proposed |
|---|---|---|
| front | Strike x2 | Strike x2 |
| middle | Strike x2 | Strike x2 |
| back | magic x2 (by gear) | magic x2 (by gear) |

Total 373 to 700 (advanced band); growth 21 to 27 (4 4 4 5 4 3 3). Its spells
follow its equipment's element, so a Saturos can fill whatever element a unit
lacks.

### 4.6 Undead: a real ladder on vanilla's own triggers

| class | step | total | growth | rows |
|---|---|---|---|---|
| Zombie | base | 343 to 440 | 16 to 22 (HP 6) | Bite 3/1/1 |
| Skeleton | class | 346 to 560 | 17 to 25 | Strike 2/2/1 |
| Ghost | advanced | 361 to 700 | 15 to 26 | Nightmare 1/1/2, back row hits a row |

The auto-revive (unless finished by holy damage) stays as the family's
identity. Lich and Vampire stay quest classes and are not changed.

### 4.7 Other raises

| class | change |
|---|---|
| Hawkman | total 366 to 440; back row Strike x2 (from x1) |
| Pumpkinhead | total 400 to 460; growth 18 to 22 |
| Young Dragon | growth 17 to 22; middle Bite x2 |
| Golem | total 378 to 450 |
| Cockatrice, Sphinx, Griffin, Hellhound | growth to 23 |
| Centurion (34 male, 35 female, 94 Europea) | developer, 2026-10-31: base stats halfway between Knight and Paladin (male 118 92 83 68 79 70 74, total 584; female and 94 keep the ROM's difference from the male: 114 86 81 74 82 74 72); growth unchanged (4 4 3 3 4 3 3, total 24, each stat at the Knight's or the Paladin's value); Slash x3 / x2 / x1 (from x2 / x1 / x1). Class feature: each Centurion that can still fight adds one attack to every Soldier (class 1) on its side, stacking (`ogre_battle_attack_count` in `app/features/battle_ai.cpp`, seven hooks on the participant's attacks-per-fight store `+0x70`). A Soldier has Thrust x1 in every row, so four Centurions give it 5 attacks, and 6 under a Princess leader. `OGRE_CENTURION_COMMAND=0` turns the feature off. Built, not played. |

## 5. What needs new code

| change | data only | needs code |
|---|---|---|
| stats, growth, rows, promotion links | yes | - |
| Magic Missile formula byte | yes (attack table) | - |
| Promote tab trees and choices | - | `app/src/honors.cpp` (small) |
| quest gating for special classes | - | detect the quest flags or the items (R4) |
| Faerie "heal if hurt, else boost" and Gremlin's targeting | - | the attack selector for a row (`func_8016E250(class, key, row)` returns the attack id) can return Healing or Kiss based on the unit's HP. It is a `.streamedB` function, so a mod `RECOMP_PATCH` may work there (as hooks on `func_8016DE3C` do); the selector does not receive the acting character, so the patch needs the battle's current-actor field (R3). |
| Kiss strength (+15%, stacks twice) | if the extra-effect bytes (`08 05 ff 02`) are stat and amount, partly | otherwise in the battle's status code (R2) |

## 6. Phases

| phase | content |
|---|---|
| A | Data rebalance: a table of class and attack changes written into the class table (`0x80187C34`) and attack table (`0x8018AA9C`, ROM `0x6099C`) every frame, as the Small Large Units mod does. `tools/class_catalog.py --apply` prints before and after. |
| B | Promote tab: the trees in section 3, the Hawk and Dragon choices, special classes excluded. |
| C | Faerie and Gremlin behaviour (after R2 and R3). |
| D | Tuning on played missions. |

The class table is shared with the enemy, so enemy Ogres, Gorgons and undead get
stronger too. The War Director's costs read the same table and follow.

## 7. Research

| id | question | method |
|---|---|---|
| R1 | Class record bytes R0 and R2 (`+0x23`, `+0x25`) and the order of R3..R6 | compare with the in-game status screen |
| R2 | (Partly done: bank X copies attack bytes `+0x06..+0x0D` into a local block in `func_ovlX_801EC2D0`, picks targets with `func_ovlX_801F55CC` and applies the effect with `func_ovlX_801F4B7C`.) Attack table `+0x08`, `+0x09` and the extra effect `08 <stat> ff <amount>`: what does Faerie's Kiss raise today, and by how much? | find the battle code that reads `+0x09` and `+0x0A..+0x0D`; one test battle with a Faerie, with participant dumps before and after a Kiss (`OGRE_RAM_KEY`) |
| R3 | Done. The battle's per-action routine `func_ovlC_801C898C` calls `func_8016E250` at `0x801C89DC` with the actor in `$s0`. `tools/battle_ai.py` inserts `ogre_battle_pick_attack` (`app/features/battle_ai.cpp`) after the return. Participants: `*(0x801CE8DC) + 0x1C4 + n*0xF8`, live while `+0x48 != 0`; flags `+0x40` (bit 0 out, bit 8 enemy side); HP `+0x20`/`+0x22`. Faerie's Kiss becomes Healing (`0x57`) when an ally on its side is below 70% HP; the game's heal targeting then picks the most hurt ally. `OGRE_FAERIE_HEAL=0` turns it off. Not yet seen in a battle. | - |
| R4 | Done. Quest classes open when the quest items are owned (equipment inventory `0x80196B20`, records `{u16 id, u8 in use, u8 count}`). Item ids: Kerykeion 124, Bloodstained Robe 202, Book of Bane 248, Ring of the Dead 271 (Lich); Count's Garment 217, Bloody Emblem 270 (Vampire); Battle Fan 133, Pure-White Dress 209, Dream Tiara 268 (Princess); Feather of Archangel 262 (Seraph, a project choice: no vanilla route is documented). Angel Knight stays recruit-only (Love and Peace). Implemented as `QUEST_TREE` in `app/src/honors.cpp`. | - |
| R5 | Vanilla's class-change rule for monsters (which levels turn Griffin into Opinincus today) | the class-change code (not found yet; Organize Screen module) |

Sources: ogrebattle64archive.com [undead](https://www.ogrebattle64archive.com/undead-class-guide.html)
and [demi-human](https://www.ogrebattle64archive.com/demi-human-class-guide.html)
class guides; class and attack data from the ROM.

