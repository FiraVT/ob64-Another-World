# Item Grant Log

An example mod for Ogre Battle 64: Recomp. It is the reference for a hook on
streamed-overlay bank code: one entry hook on a function in a bank unit
(`Bank<U>Funcs/`), which the base game's own section table does not contain.

## What it does

| hook | function | where |
|---|---|---|
| entry | `func_ovlN_801DC740` | bank unit N, record 7 (`bankRec7`, ROM `0x101D00` -> RAM `0x801AD5C0`), the mission scene `0x03`'s module |

`func_ovlN_801DC740` is the item grant routine. It takes one argument, the item
word in `$a0`. Bit 15 selects the list and the low 15 bits are the id
(`history-archive:docs/history/DECISIONS.md`, session 122):

| bit 15 | list |
|---|---|
| set | the 278-slot equipment list at `0x80196B20` |
| clear | the 40-slot consumable list at `0x80193AE0` |

The hook writes one line per grant to the port's output and changes nothing in
the game:

```
[mod] item grant equipment 117
[mod] item grant consumable 1
```

The manifest sets `enabled_by_default = false`. Turn the mod on in the start
screen's MODS tab, play a mission, and receive an item.

## Building

```sh
make bank-recomp     # the bank units, if they are not built yet
make mod-syms        # main + bank reference symbols -> mods/reference/
tools/build-example-mods.sh mods/item-grant-log
```

The result is `build/mods/item-grant-log.nrm`. Copy it into `<config>/mods/`.

`make mod-syms` must have merged the bank units into `mods/reference/dump.toml`
(`tools/merge_mod_syms.py`); with a main-only dump the mod tool stops with
"hooks a function (func_ovlN_801DC740) that doesn't exist in the original ROM".

## Choosing a bank function to hook

The runtime regenerates the hooked function from its ROM words, and every call
in the regenerated body is a function-map lookup. Check two things first, as
`docs/guides/app-build.md` -> "Hooking bank code" describes:

- no `make bank-recomp` script edits the function's generated C;
- every `jal` in the function targets a function entry that is resident when
  the function runs. `func_ovlN_801DC740` has one call, `jal 0x80093540`, a
  `.main` function.

