# Smart Enemy Formations

An Ogre Battle 64: Recomp code mod. Enemy units keep their members, classes and
levels, but every member stands in the row its class fights best from.

## How a row is chosen

Each class record (`0x80187C34 + class * 0x48`) carries the number of attacks
the class gets from each row: `+0x38` front, `+0x3A` middle, `+0x3C` back. A
member goes to the row with the most attacks; a tie goes to the front, then the
middle. Members are seated in order of how much they care (the gap between their
best and second-best row), large characters first, the leader winning ties; a
member whose row is full takes its next choice. Within a row the centre square
fills first, then left, then right.

| class | attacks F/M/B | row |
|---|---|---|
| Fighter, Knight, Berserker | 2/1/1 | front |
| Archer | 1/2/2 | middle |
| Wizard, Witch, Cleric | 1/1/2 | back |
| Hellhound, Golem | 3/2/1 | front |

A large character (class size 2) takes a row to itself, because the game picks
its second square. With the Small Large Units mod on, every class is size 1.

## The data it edits

| what | where |
|---|---|
| unit records | `0x80197210 + u * 0x19`, 60 records, 30..59 the enemy's; `+0x01 & 0x80` enemy, `+0x02..+0x06` members (leader first), `+0x07..+0x0B` squares |
| enemy characters | `0x80195580 + ref * 0x34` for a member reference 1..99; class `+0x11`, key `+0x12`; 100+ is a generic soldier |
| squares | `row * 3 + column`, row 0 the back: `func_ovlAA` at `0x802164CC` maps square < 3 to the back row's attacks, < 6 to the middle's, else the front's |

## When it runs

From the per-frame hook on `func_80072944`. An enemy unit is laid out once,
when members appear in it that the mod has not seen there (the map loader
creating it, a slot reused, a save loaded). A member dying only removes a
reference, so nothing moves during a battle.

## Building

```sh
make mod-syms
tools/build-example-mods.sh mods/smart-enemy-formations
```

## Not yet checked in game

* The row mapping is read from code, not yet seen on a battle screen.
* Generic soldiers (references 100+) are treated as Soldiers (front row); their
  class is not read.
* Neutral encounters (bank AH's table at `0x801F0CD0`) are not touched.

