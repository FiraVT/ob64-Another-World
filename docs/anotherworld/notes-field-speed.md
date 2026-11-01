# Mission-field speed (the Settings menu's "Game speed")

Code: `app/features/field_speed.cpp`. Hooks: `tools/director_hook.py` (section 4).

## The vanilla option

- Bank V's settings switch (`func_ovlV_80214FA0`) uses the jump table at
  `0x802195C8`. Row 4, "Game speed", stores the cursor value with
  `sb $v0, -0x63ED($at)` at `0x802155EC`, so the option is the byte
  `0x800E9C13`. Its values are 0 Slow, 1 Normal and 2 Fast. The menu and the save
  keep this 0..2 value.
- Bank N's field driver `func_ovlN_801AE880` calls three routines once per
  frame (`0x801AF3D0`, `0x801AF3E0`, `0x801AF3F0`). Each reads the byte as
  `n = value + 1`:

| site | routine | use of `n` |
|---|---|---|
| `0x801B1390` | `func_ovlN_801B134C` | the clock: `2` or `20` ticks x `n` per frame |
| `0x801E4DB4` | `func_ovlN_801E49D0` | a halfword countdown, minus `2 x n` per frame |
| `0x801C70B4` | `func_ovlN_801C6D10` | runs the field update `func_ovlN_801B15A0` (units march) `n` times per frame |

Vanilla Fast is therefore `n = 3`.

## The change

The developer asked for the three settings to make the units march 3x, 6x and
9x as fast as vanilla Fast, which is `n` = 9, 18 and 27 field updates per frame,
and for game time to keep its vanilla rate. `ogre_field_speed` replaces the
loaded byte with `n - 1` only at `0x801C70B4` (the update loop). The clock
(`0x801B1390`) and the countdown (`0x801E4DB4`) keep the vanilla
`n = value + 1`. The first version scaled all three, which also ran the in-game
clock 9x faster; it was corrected on 2026-10-29.

Vanilla checks two stop conditions once, before the update loop
(`0x801C708C`..`0x801C70A8`): bit `0x02` of `0x801F367D`, and the word
`0x801F3630`. At 27 updates per frame, the loop's back edge (`0x801C7100`) repeats
the same check through `ogre_field_hold`. The remaining updates then stop on the
frame a battle or an event starts, as they would before the first update.

`OGRE_FIELD_SPEED=0` keeps vanilla speeds.

## Not verified

The change has not been run on a mission yet. Things to check on a played
mission at each setting:

- marching speed
- that the clock advances at the vanilla rate for the setting while units march faster
- that encounters start where the units meet

