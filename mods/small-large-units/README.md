# Small Large Units

An Ogre Battle 64: Recomp code mod. Large characters take one square of the
unit grid and count as one of the five members, like every other character.

## What it does

The class table holds one record per class, `0x48` bytes each, starting at
`0x80187C34` for class 0 (class 1 is Soldier). The byte at `+0x04` is the
class's size:

| value | classes |
|---|---|
| `1` | every human/demi-human class |
| `2` | Ogre, all Dragons, Wyrm, Wyvern, Griffin, Opinincus, Cockatrice, Sphinx, Hellhound, Cerberus, the three Golems, Death Bahamut |

The game reads the size only through `func_8016DE3C` (`0x8016DE78`/`0x8016DE84`),
so every place that cares (the Organize Screen, the grid, battle) asks the
same table. The mod rewrites every `2` to `1` once per frame from the hook on
`func_80072944`, because the table lives in a streamed segment.

## Building

```sh
make mod-syms
tools/build-example-mods.sh mods/small-large-units   # -> build/mods/small-large-units.nrm
```

Copy the `.nrm` into the `mods/` folder beside the executable and enable it in
the start screen's **MODS** tab.

## Not yet checked

* How a large character's sprite looks in a single square in battle and on
  the Organize Screen (it may overlap its neighbour).
* Units saved with a large character already in them: whether the second
  square frees up on load or only after the unit is reorganised.

