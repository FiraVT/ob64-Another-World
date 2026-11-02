# Design: changing form in battle

Status: research, 2026-11-01, read from bank units C, X, W and AH
(`build/bank*.elf`, revision 1). Built: figures on demand (section 7.1), the
`battle_forms` pack block (section 6) and the form switch (sections 7.2..7.5).
Passed the testing session's hands-off battle run (section 7.5); not played by
hand.

## 1. Goal

The port gets the hooks and data a mod needs to change a fighter's class for
one strike in battle. The test case is one Fighter (class 2) that, in one
battle round:

1. Slashes as a Fighter.
2. Turns into a Werewolf (class 42), attacks with Claw, runs back to its
   square and turns back into a Fighter.
3. On its next strike turns into a Tiamat (class 68) and uses Rotten Breath,
   whatever row it stands in.

The work is the mechanism. A polished mod is not part of it.

Ids, from `docs/class-catalog.md` and the attack table (ROM `0x6099C`):

| class | id | size | front / middle / back |
|---|---|---|---|
| Fighter | 2 | 1 | Slash `0x04` x2 / x1 / x1 |
| Werewolf | 42 | 1 | Claw `0x07` x3 / x2 / x1 |
| Tiamat | 68 | 2 | Rotten Breath `0x25` x2 / x2, Evil Dead `0x2C` x2 |

Rotten Breath is kind 5 (breath), element bane, target pattern 3.

## 2. The battle participant

Participant `n` is `*(0x801CE8DC) + 0x1C4 + n*0xF8`, `n < 20`
(`app/features/battle_ai.cpp`). The fields this design uses:

| offset | size | meaning | evidence |
|---|---|---|---|
| `+0x00..+0x08` | 3 words | pointers to the participant's three draw objects | `func_ovlC_801AFE50` walks them (`0x801AFEC4`) |
| `+0x33` | byte | fallback attack | `func_ovlC_801C898C` returns it at `0x801C8A54` |
| `+0x36..+0x3C` | 4 halves | equipment | passed to `func_8016FA54`, `func_8016E490` |
| `+0x40` | word | flags: bit 0 out of the fight, bit 8 enemy side, bit 10 an alternate figure | `0x801CB8D8..0x801CB8F0` |
| `+0x48` | word | class (low byte `+0x4B`) | draw `0x801B07E4`; attack `0x801C89D0` |
| `+0x4C` | word | class key (low byte `+0x4F`) | draw `0x801B07EC`; attack `0x801C89D4` |
| `+0x58` | word | square; `func_ovlC_801C9FC4` turns it into the row | `0x801C89C4` |
| `+0x6C` | word | strikes used this round | `0x801CC508`; zeroed by the round reset at `0x801AFF8C` |
| `+0x70` | word | strikes this round | `0x801AFFA4` |

The class word `+0x48` and the key word `+0x4C` are the only inputs that tie a
participant to its class during the fight. Section 4 shows that the draw code
and the attack lookup both read them live, every frame and every strike.

## 3. The strike: how the attack is chosen

`func_ovlC_801C898C(participant)` (bank C, record 10) returns the attack id
for one strike. It is reached through `func_ovlC_8020ABBC(index)`, which turns
a participant index into a pointer (`func_ovlC_801C9008`) and calls it.
Callers: `0x80207BEC`, `0x80209284`, `0x80225040`.

1. If `*(*(0x801D0830) + 0x14)` is not zero, the routine calls it and returns
   its result (`0x801C8998..0x801C89B4`). This is a function pointer the game
   installs for scripted fights. Not traced further.
2. A class word of `0x101` returns `+0x33` directly (`0x801C89BC`).
3. Otherwise: row = `func_ovlC_801C9FC4(+0x58)`, attack =
   `func_8016E250(+0x4B, +0x4F, row)` (`0x801C89DC`). The port already hooks
   the return here: `ogre_battle_pick_attack` (`config/hooks.toml`) gets the
   participant in `$s0` and the attack in `$v0` and returns the attack to use.
4. `func_8016E478(attack)` gives the attack's kind. For kinds 3, 4 and 5
   (spell, special, breath) attacks 49 and 50 return `+0x33`, attack 145 takes
   a separate path, and the rest pass through `func_8016E52C` with the class,
   key and equipment. Every path except the `+0x33` returns ends in
   `func_8016E490(attack, equipment)`.

The attack count for the round is `+0x70`, written by the round reset
`func_ovlC_801AFE50` (`0x801AFFA4`) and the six other stores that
`ogre_battle_attack_count` already wraps. `func_ovlC_801CC46C` ends the
round when every live participant has `+0x6C >= +0x70`.

Correction (same day, from reading bank X): `func_ovlC_801C898C` is reached
from the battle's action script, and the strike itself chooses its attack
again in bank X. `func_ovlX_801E9E9C` runs one participant's strike as a
per-frame state machine (a jump table at `0x801E9F70`). It advances `+0x6C`
first (`lw`/`addiu`/`sw` at `0x801EA140..0x801EA14C` and
`0x801EA544..0x801EA558`) and then looks up the attack from the class with
`func_8016E250(+0x4B, +0x4F, row)` (`0x801EA40C`, `0x801EA5C8`, `0x801EA658`,
`0x801EA6CC`), followed by the class animation selector
`func_801BE328(+0x48, +0x4C, attack)` (`0x801EA420`). `func_ovlX_801EBCA0` is
bank X's copy of the bank C routine (lookups at `0x801EBCD8`, `0x801EBD30`),
and `func_ovlX_801EC2D0` has one more (`0x801ECAE0`). So:

- At a strike's attack lookup the strike number (0-based) is `+0x6C - 1`.
- The class at that moment gives the strike's attack, its animation and its
  figure. The earlier claim that the existing `ogre_battle_pick_attack` alone
  gives "Rotten Breath from any row" was wrong: that hook covers only the bank
  C path. Section 7.2 hooks every lookup.
- Fourteen other sites in bank X advance `+0x6C` (the other attack-kind
  handlers); `func_ovlX_801E6FD0` and five others set `+0x6C = +0x70`, which
  ends a participant's strikes. The end of one strike's animation is still not
  identified.

## 4. How a participant is drawn

### 4.1 The figure cache

Battle figures (sprite sheets and animation data) live in a cache of 20
entries of `0xB0` bytes at `*(0x801D0748)`, allocated by
`func_ovlC_801C2330` (`0xDC0` bytes) and freed by `func_ovlC_801C236C`.

| entry offset | meaning |
|---|---|
| `+0x00` | handle (0 = free) |
| `+0x04` | asset id of the figure |
| `+0x08` | the loaded figure |
| `+0x0C` | palette attribute (`func_ovlC_801BE1AC`, the handle's top nibble) |
| `+0x10` | equipment variant (`func_ovlC_801BE0FC`) |
| `+0x14` | second variant (`func_8016F600`) |
| `+0x44`, `+0x48`, `+0x6C` | unpacked sections (`func_8009DD38`) |

`func_ovlC_801BE158(class, key, alt, side)` gives the handle: the row is the
class when `class_table[class].+0x45 == key`, otherwise the key, and the
handle is the 12 low bits of `animation[row*4 + alt*2 + side]` (slot 0 of the
class animation resource, asset `0x00315736`,
`design-new-classes.md` section 7).

`func_ovlC_801C2818` loads one figure: it reads the directory asset
`0x003B6CD0`, takes the figure's asset id at `handle - 1`, loads and unpacks
it, and fills the entry.

### 4.2 Figures are loaded once, at the start of the battle

`func_ovlC_801CA6A0` (the battle's setup) walks the 20 participants
(`0x801CB894..0x801CBA54`). For each live one it reads `+0x48`, `+0x4C`, flag
bits 8 and 10 and the equipment variant `func_8016FA54(+0x36..+0x3C)`, and
adds the combination to six parallel stack arrays, sorted by handle and
without duplicates:

| array | contents |
|---|---|
| `sp+0x18` | class |
| `sp+0x60` | key |
| `sp+0xA8` | handle |
| `sp+0xF0` | equipment variant |
| `sp+0x138` | flag bit 10 |
| `sp+0x180` | flag bit 8 (side) |

Each array holds 18 words (`0x48` bytes), so a battle lists at most 18
distinct figures; the cache has 20 entries. At `0x801CBA58` the setup groups
equal handles and calls `func_ovlC_801C41E8` per group (`0x801CBAD8`), which
loads every figure the cache does not yet hold into a free entry.

No other path in banks C, X or W loads a figure for a participant during the
fight. `func_ovlC_801C2518` frees figures listed at `*(0x801D07F0)` (entries
of 100 bytes, count `*(0x801D07F4)`), called from `func_ovlC_801B7EBC` and
`func_ovlC_801BBC98`; that list is not the participants'.

### 4.3 The draw reads the class every frame

`func_ovlC_801B0790(draw_object, ...)` draws one participant. It reads the
participant from `draw_object+0x40` and loads the class and key from
`participant+0x48` and `+0x4C` at entry (`0x801B07CC..0x801B07F8`). It then
finds the figure with `func_ovlC_801C2ED0(class, key, ...)`, which computes
the handle and scans the cache, matching the handle and then the palette
attribute (`+0x0C`) or the equipment variant (`+0x10`).

When the scan finds nothing, `func_ovlC_801C2ED0` returns 0
(`0x801C33E4`), and the caller spins forever:

```
801b12b4:  lw   t2,404(sp)      ; the frame func_ovlC_801C2ED0 returned
801b12b8:  bnez t2,801b12c8
801b12c0:  j    801b12c0        ; no figure: jump to itself
```

So writing a class into `+0x48` changes the participant's picture on the next
frame, and a class whose figure is not in the cache hangs the game. Every form
a mod uses must be loaded at the battle's start (section 7, hook H1), and the
port must check the cache before it switches (H2).

### 4.4 Size

`func_ovlC_801C8E14(participant)` returns 1 when
`func_8016DE3C(+0x4B, +0x4F)` (the class record's `+0x04`) is 2. It reads the
current class, so a participant that becomes a Tiamat is treated as large at
once. Callers: the draw `func_ovlC_801B0790` (`0x801B16B4`), three other draw
routines (`0x801B26F4`, `0x801B347C`, `0x801B4CD4`), `0x801C643C`,
`0x801C7B04`, and seven sites in bank X's `func_ovlX_801E7230`.

`func_ovlC_801CC594` places a participant on the battle grid and uses a
different slot range for a large class (`addiu s2,s2,12` at `0x801CC5E4`).
It runs at placement. A Fighter that becomes a Tiamat keeps its one-square
placement while the draw and bank X treat it as large.

The port already fights large classes from one square. LARGE UNITS ONE SQUARE
(`class_rebalance.cpp`, `DECISIONS.md` 2026-10-29) keeps size 2 in the class
table and answers 1 only to the Organize screen's rules, so a large class sits
in one square and battle draws its large figure there. A Tiamat form is the
same arrangement: a large figure on a one-square placement. Whether targeting
treats it the same way as a one-square Tiamat recruited through Organize is
not checked.

## 5. Existing form changes in the game

Corrected 2026-11-01 after the first play test. The game changes a
participant's class during a fight with `func_ovlX_801F0F7C(participant,
class)`; `func_ovlX_801F106C` does the same with a visual effect (effects 57
and 58). The routine:

- writes the class into `+0x48` when the key word `+0x4C` equals it, and the
  key into `+0x4C` (`0x801F0F94..0x801F0FB8`);
- rebuilds the equipment pictures `+0x36..+0x3C` from the class
  (`func_8016DFA8`, `func_8016DFFC`, `func_8016E050`, `func_8016E0A4`);
- sets the strike count `+0x70` from the class's row (`func_8016E358`,
  `0x801F1014`);
- computes the figure handle (`func_ovlC_801BE158`) and stores it in `+0x50`
  and in the draw object `*(+0x00)` at `+0x44` and `+0x18`;
- loads the figure: `func_ovlJ_801E1600` calls the game's loader
  `func_ovlC_801C41E8` with the participant's side, alternate flag and variant.

`func_ovlX_801F4848` picks the class from a table of five 5-byte rows at
`0x801F6A98` (from, to, two thresholds, situation): Golem to Stone Golem, Stone
Golem to Baldr Golem, Zombie (two rows) to Skeleton, Skeleton to Ghost.
Callers: `0x801E8AA4`, `0x801E92F0`, `0x801E9C3C` (with the effect), and
`0x801EAD88`, `0x801EB30C`, `0x801ED16C`, `0x801EF844`.

The research pass missed this: it searched these stores but read only bank C's
context. The draw object holds the figure handle, so the first build, which
wrote `+0x48`/`+0x4C` directly, changed the attack lookups and not the picture.
The Lycanthrope/Werewolf day and night form is not in the table; where the game
chooses it is still open.

## 6. What the mod declares

Built 2026-11-01. A JSON content pack carries a top-level `battle_forms`
list (`content_json.cpp`, `read_battle_forms`):

```json
"battle_forms": [
  {
    "class": 2,
    "attacks": 3,
    "strikes": [
      {},
      {"form": 42, "attack": 7},
      {"form": 68, "attack": 37}
    ]
  }
]
```

- `class`: the class the rule is for, a class id 1..164 or a new class key.
- `strikes`: 1..9 objects. Strike `i` of a battle uses `strikes[i]`; a strike
  past the list keeps the class. `form` is the class to fight as (an id or a
  new class key), or a list of 1..32 classes: each strike draws one at random,
  never the fighter's previous form when the list has another. Without it the
  strike keeps the class. `attack` is the attack id (0..158;
  `content-dump/attacks.json` lists them), or `"breath"`: the form's breath
  attack (the first attack of kind 5 in its rows, front first), from any row.
  Without it the strike uses the form's attack for the fighter's row.
- `attacks`: optional, 1..9, the fighter's strikes in a battle, replacing its
  class's count.
- `repeat`: optional, `true` makes strike `i` use `strikes[i % n]`, so a rule
  with one strike applies to every strike.
- Keys starting with `_` are notes. Anything else refuses the file.

A later pack's rule for a class replaces an earlier one's
(`content::battle_form_rules`). A rule whose class or form key has no id is
left out. The MODS tab counts a pack's rules as `BATTLE FORMS`. Ten hostile
cases are in `content_pack_test` (`security.md` section 2). The example mod
is `app/content/examples/battle-forms/`.

## 7. Hook points

### 7.1 Built: figures on demand (H0)

Built 2026-11-01, not played. `app/features/battle_figures.cpp`, one hook in
`config/hooks.toml` at `0x801C3000` (ROM `0x206440`) in `func_ovlC_801C2ED0`,
after `func_ovlC_801BE158` stored the handle at `sp+0x34` and before the cache
scan. When no cache entry holds the handle with a loaded figure, the port calls
the game's loader `func_ovlC_801C41E8` with one-element arrays (class `$fp`,
key `sp+0x1C`, side `sp+0x24`, alternate flag `sp+0x78`, equipment variant
`sp+0x7C`) placed below the frame, restores every register, and the game's
scan then finds the new entry. It guards the game's two spins: no load when
the 20 entries are taken (`func_ovlC_801C41E8` spins at `0x801C4544`), and an
entry left half-filled by a failed allocation is cleared. Each request is
tried once per battle and logged as `[battle-figure]`.

With H0 a mod no longer has to preload its forms, and the 18-figure limit of
the setup list does not apply. H1 below stays useful to load forms before the
fight starts instead of at their first draw.

Not covered by H0:

- `func_ovlC_801C3770`, the lookup the draw routines `func_ovlC_801B21E4` and
  `func_ovlC_801B2C28` use, has the same miss. Its loop overwrites `$s3` (its
  third argument) after a partial match (`0x801C3980`), so its caller's use of
  a 0 result (`0x801B25E8` reads `0x3(v0)`) needs reading before it gets a
  hook.
- An entry with the right handle and another palette or variant still misses
  in `func_ovlC_801C2ED0`'s scan, and H0 does not load a second copy.

### 7.2 Built: the form switch

Built 2026-11-01. Revised after the first play test (section 7.3). `app/features/battle_form.cpp`, 15 hooks in
`config/hooks.toml`:

| hook | where | registers | what the port does |
|---|---|---|---|
| strike | before each attack lookup: bank X `0x801EA404`, `0x801EA5B0`, `0x801EA650`, `0x801EA6C4` (`$s6`), `0x801EBCD0` (`$s1`), `0x801ECAD8` (`$v1`); bank C `0x801C89BC` (`$s0`) | participant | `ogre_battle_form_strike`: strike = `+0x6C - 1`. A strike number that is new for this participant ends every other participant's form. If the participant's class has a rule with a form for this strike, load the form's figure (`battle_figures::ensure`, with side, alternate flag and `func_8016FA54(equipment)` as the setup passes them), save the class and key words once, and write the form's class and its class key (class record `+0x45`) into `+0x48`/`+0x4C`, keeping the words' upper bytes. A form whose figure does not load is skipped. Otherwise put the class back. |
| attack | after each bank X lookup: `0x801EA414`, `0x801EA5D0`, `0x801EA660`, `0x801EA6D4`, `0x801EBCE0`, `0x801ECAE8`; bank C through `ogre_battle_pick_attack` | participant, `$v0` | `ogre_battle_form_attack`: the rule's `attack` for this strike, else the game's. |
| count | the seven count stores `ogre_battle_attack_count` already wraps | participant, `$v0` | The rule's `attacks`, before the Centurion bonus. |
| round reset | `0x801AFF84` in `func_ovlC_801AFE50`, before it reads the class for the count | `$s3` | `ogre_battle_form_reset`: put the class back. |
| battle end | `0x801B00D0`, the entry of the teardown `func_ovlC_801B00D0` | - | `ogre_battle_form_end`: put every class back while the participant table is valid. |

A fighter keeps its form from its strike's first lookup until another strike
starts, so the Werewolf runs back to its square as a Werewolf and turns back
when the next strike of the battle begins. After the battle's last strike the
form stays until the teardown. Participants whose class word has more than
the low byte set (`0x101`, the game's special figures) are left alone.
`OGRE_BATTLE_FORMS=0` turns the rules off; each switch logs a `[battle-form]`
line.

Not settled, and visible only in play:

- Whether a Werewolf or Tiamat figure loads during the fight for every
  fighter (heap, a full cache). Each failure is logged and the strike then
  keeps the class.
- Whether `func_ovlX_801EBCA0` and `func_ovlC_801C898C` run for a fighter
  outside its own strike (an AI preview). They use the same strike number, so
  they keep the form of the fighter's last strike and do not start a new one.
- The Princess passes store count + 1; a rule's `attacks` replaces that value,
  so a fighter with a rule loses the Princess bonus.

H1 (preloading at the setup, `0x801CBA58`) is not built; forms load at their
first strike instead.

Memory: each figure is allocated from the game's heap (`func_80070F30`). On a
failed allocation `func_ovlC_801C2818` returns 0 (`0x801C2934`) after it wrote
the handle into the entry (`0x801C28E0`), leaving `+0x08` zero;
`battle_figures::ensure` clears such an entry and reports the form as not
loaded.

### 7.3 First play test and the revision

The testing session ran the example (`devtest/devtest scenario battle-forms`,
2026-11-01): the log showed `participant 7 strike 2: class 2 -> 42` and the
Fighter stayed a Fighter on screen; no `strike 3` line came in four battles,
and participants 6 and 7 always switched at the same moment, the second one
ending the first one's form.

- The picture: the draw object caches the figure handle (section 5). The
  switch and the way back now go through `func_ovlX_801F0F7C`, after the
  figure is loaded through `battle_figures::ensure` (the game's own loader
  spins on a full cache). The routine also rewrites `+0x70` and the equipment
  pictures; the port keeps `+0x70` and puts the equipment pictures back with
  the class.
- The strike number: the attack pickers (`func_ovlC_801C898C` at
  `0x801C89BC`, `func_ovlX_801EBCA0` at `0x801EBCD0`/`0x801EBCE0`) run before
  `+0x6C` advances, so their strike is `+0x6C`; the first build counted it as
  `+0x6C - 1`, so a picker could switch a form one strike early and end
  another fighter's form. Pickers now only get the rule's attack, and only the
  executor sites switch forms.
- The missing third strike is not explained yet. `OGRE_BATTLE_FORM_TRACE=1`
  logs every hook call with its site, `+0x6C`, `+0x70`, class and key, and every
  strike count the rule sets, for the next run.

### 7.4 Second play test: the fight is computed, then played back

The testing session's run with the 7.3 build (`devtest/out/scenario-battle-forms-20261101-171207.log`):
every strike hook and the form switch and its revert ran in one burst at the
start of the fight (one display-list time stamp), with `+0x6C` counting up to 3;
0.3 s later RAM showed both Fighters as class 2, handle `0x003`, `+0x6C` 0. Strike
2 (melee) never reached the hooked lookups; strike 3 (breath) did.

The game computes the fight first and plays it back:

- `func_ovlX_801EFACC` computes the whole fight with nothing drawn. Each
  attack-kind handler starts a strike with `func_ovlJ_801D9F50` (an event at the
  fighter's time), sets the fighter's time cursor `+0x94` and advances `+0x6C`
  (16 sites, `0x801E7FFC`..`0x801EDBDC`). Every step goes into a time-sorted
  event timeline at `*(0x801CE8E0) + 0x10`, 0x800 bytes, written by
  `func_ovlJ_801D9914(opcode, time, participant, ...)`; event lengths by opcode
  at `0x801E5C90`.
- The fighters are then reset and `func_ovlJ_801DB90C` plays the timeline back,
  dispatching on the opcode through the table at `0x801E6D18` (opcodes 1..63).
- The game's visible class change, `func_ovlX_801F106C(participant, time,
  class)`, records event 57 at the time plus the length of the fighter's
  animations 17 and 18 (`func_ovlJ_801DF2CC`) and event 58 35 ticks later, then
  changes the class as `func_ovlX_801F0F7C` does and moves `+0x94` past it.
  Event 57 (`0x801DDA00`) copies the fighter into the first participant slot
  with `+0x48 == 0` with the new class (`func_ovlJ_801E1680`) and hides the
  original (flag `0x800`); event 58 (`0x801DDB10`) copies it back over the
  fighter, writes the class and key into the character record (`+0x11`,
  `+0x12`) and clears its equipment (`+0x2A..+0x30`, `0x801DDC6C..0x801DDC88`),
  and rebuilds the fighter's equipment pictures.

The build after this test (not played):

- The switch moved to the strike starts: before each handler reads `+0x6C` and
  `+0x94`, the strike is `+0x6C`. It calls `func_ovlX_801F106C` at the
  fighter's cursor, after checking for a free participant slot and loading the
  figure through `battle_figures::ensure`; the strike count is kept. A new
  strike of anyone sends every other fighter in a form back through the same
  routine at that fighter's own cursor, the end of its strike. `0x801EFDE0`, where
  the computation ends and the fight's length is taken from the cursors, sends
  every remaining form back.
- Event 58: a hook before the writes (`0x801DDC6C`, record in `$v1`) saves the
  character record's class, key and equipment once per fighter the port
  transformed; a hook after them (`0x801DDCD0`) writes them back, and puts the
  fighter's equipment pictures back when its class is its own again.
- The attack lookups keep the rule's attack; a picker (strike `+0x6C`) gives the
  form's attack for the row when the rule has none. The earlier switch sites at
  the lookups are removed.

### 7.5 Third and fourth play tests

The third run (`devtest/out/scenario-battle-forms-20261101-174505.log`) showed
p7 as a Werewolf striking for strike 2, then both Fighters vanished and the game
crashed (0xC0000005). Every change uses the same spare slot, the first with
`+0x48 == 0`, which the computation never fills; p6's and p7's 57..58 windows
overlapped, so one event 58 copied an emptied slot back over p7 (`event58 p7
class=00000000`).

The port now keeps the windows recorded in the battle and starts a change after
any window it would overlap: the lead to event 57 is the length of the
fighter's animations 17 and 18 (`func_ovlJ_801DF2CC`, as
`func_ovlX_801F106C` computes it at `0x801F1088..0x801F10C4`), and the routine
gets `start - lead` as its time.

The fourth run passed (`devtest/out/scenario-battle-forms-20261101-175006.log`,
hands-off, the testing session's harness):

- RAM captures once a second: p7 class 42 for strike 2, then p6; p7 class 68
  for strike 3, then p6; class 2 in every other capture. Each form's draw
  object handle equals `+0x50`, its figure is in the cache, and no Fighter
  shares it. Screenshots show the Werewolf striking and the Tiamat breathing.
- The windows: p7 to 42 at 910/945, back at 1041/1076; p6 to 42 at 1077/1112
  (its cursor was 995, moved past p7's window), back at 1208/1243; the Tiamat
  changes at 1887..2549.
- Every character's class, key and equipment in the record are the same after
  the battle as at its load.
- No freeze or crash; the field follows the fight.

Each change shows the game's message "<name> has changed class." in the
playback, which comes with the game's transformation events (section 8,
question 5).

## 8. Questions for the maintainer

1. A Tiamat is a large (size 2) unit. This design draws the Tiamat form the way
   LARGE UNITS ONE SQUARE draws a recruited Tiamat: the large figure in the
   Fighter's square. Is that the wanted look?
2. The Werewolf turns back when the next strike of the battle starts (section
   7.2). Is that close enough, or must it turn back the moment it is back in
   its square?
3. Should the transformed fighter use only the form's look and attack, or
   also the form's stats (HP, STR and the rest stay the Fighter's in this
   design)?
4. The Tiamat turns back like the Werewolf: at the next strike, or at the
   battle's end when its breath is the last strike. Is that right?
5. Each change showed the game's "<name> has changed class." message. The
   maintainer chose to hide it for the port's forms (2026-11-01): a hook at
   `0x801AFBF0` in `func_ovlC_801AFB2C`, right after `func_ovlC_801AF9D8`
   opened the window, closes it the way the game does when the timer at
   battle state `+0x57D0` runs out (`func_8017C31C`, `+0x57D4` cleared;
   `0x801D7FA8..0x801D7FC4`). `OGRE_BATTLE_FORM_MESSAGE=1` keeps it. Passed the testing session's run (`devtest/out/scenario-battle-forms-20261101-180826.log`): the forms show and turn back as before, and no message window appears.

## 9. Next

1. The Dragon Knight example (`app/content/examples/new-class/dragon-knight.json`):
   a `repeat` rule that turns it into one of the six elder dragons (63
   Quetzalcoatl, 64 Flarebrass, 65 Ahzi Dahaka, 66 Hydra, 67 Bahamut, 68
   Tiamat; the maintainer's choice) at random on each strike, with that
   dragon's breath. Passed the testing session's run
   (`devtest/out/scenario-dragon-knight-20261101-183046.log`): Quetzalcoatl,
   Bahamut and Ahzi Dahaka drawn breathing on strikes 1..3, back to the Dragon
   Knight between them, character records unchanged, no crash. The first run
   kept class 172 with only the key changed and drew no dragon: the character
   got its class from a script and has key 0, and `func_ovlX_801F106C` changes
   the class word only when it equals the key. The round reset now gives a
   fighter with a rule and key 0 its class as key, as standard classes have
   it; the record is not touched.
2. A hand play test of the example mods.
3. Hook `func_ovlC_801C3770`'s miss (section 7.1).
