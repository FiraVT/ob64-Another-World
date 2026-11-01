# US revision 0 and revision 1: what differs

Phase A of `docs/anotherworld/design-ecosystem.md`. Measured offline on 2026-10-29 with
`tools/revmap.py` and `tools/lc_lookup.py`; no game run.

| | revision 0 | revision 1 (the port's build today) |
|---|---|---|
| file | `assets/ogre64-rev0.z64` | `assets/ogre64.z64` |
| header | `NOBE`, byte `0x3F` = 0 | `NOBE`, byte `0x3F` = 1 |
| SHA-256 | `571e8339…` (LordlyCaliber's baseline) | `3bfbaf0a…` |
| size | 40 MiB | 40 MiB |

## 1. Code

All 4,991 functions the port recompiles were looked up in revision 0
(`build/revmap/rev1_rev0.tsv`):

| result | functions |
|---|---|
| same bytes, same offset | 775 |
| same bytes, moved | 297 |
| same code, moved, with shifted address operands | 3,916 |
| changed | 3 |

The ROM is three blocks with one offset each:

| revision 1 ROM | revision 0 ROM = rev1 + |
|---|---|
| `0x001000..0x042F68` | `0` |
| `0x043120..0x1960C8` | `-0x20` |
| `0x1977B0..0x2B8988` | `+0x30` |

The two block boundaries are the changed functions:

1. **`func_8016D068` and `func_8016D110`** (ROM `0x042F68..0x043120`;
   revision 0 `0x042F68..0x043100`, 0x20 bytes shorter). These are the quick
   save and quick load of the 100 enemy character records (`0x80195580`, stride
   0x34; LordlyCaliber hooks the revision 0 entries `0x8016D068`/`0x8016D100`
   as its quick load and quick save). Revision 1 encodes and decodes each
   record's name with 17 bytes instead of 16 (`func_8017AA18` /
   `func_80093060` with `a2 = 17`), writes `0xFF` to the name's byte 16, and
   clears byte 16 again after loading.
2. **`func_ovlR_80215C38`**, the mission loader (revision 1 `0x1960C8`, 0x1650
   bytes; revision 0 `0x1960A8`, 0x50 bytes longer). Revision 1 moves the call
   to `func_8016F13C` (new-unit base stats) earlier and drops a revision 0
   branch that calls `func_801DC054` (revision 0 address) and clears 56 bytes
   at revision 0 `0x80193BC0` (revision 1 `0x80193BE0`, the first player
   character record, stride 0x38) through `func_80093380`.

Everything else is the same code, so a hook or a finding on one revision
carries to the other through the map.

## 2. Data addresses

`build/revmap/data_rev1_rev0.tsv` pairs the address operands of every matched
function (8,607 pairs) and merges them into runs:

| revision 1 RAM | revision 0 = rev1 + | references |
|---|---|---|
| `0x80000000..0x8016CF64` | `0` | 27,140 |
| `0x8016D220..0x80217240` | `-0x20` | 34,546 |
| `0x80217290..0x80217320` | `+0x20`..`+0x30` (overlay window, several banks) | 20 |
| `0x80217370..0x80243DD0` | `-0x20` | 8,225 |

So every game table above `0x8016D220` sits 0x20 lower in revision 0.
Examples the port uses:

| table | revision 1 | revision 0 |
|---|---|---|
| item records (item 0) | `0x8018C42C` | `0x8018C40C` |
| mission index | `0x8018F4A1` | `0x8018F481` |
| enemy character records | `0x80195580` | `0x80195560` |
| player character records | `0x80193BE0` | `0x80193BC0` |
| mission base level | `0x801F1055` | `0x801F1035` |

LordlyCaliber's addresses are revision 0 addresses: its item base
`0x8018C400` and enemy characters `0x80195560` agree with this table. Every
LordlyCaliber RAM address above `0x8016D220` is used on revision 1 as
`address + 0x20`.

Function addresses follow the same rule: a revision 1 function in the
`-0x20` ROM block has a revision 0 address 0x20 lower (`func_8016F13C` here is
`0x8016F11C` there).

## 3. LordlyCaliber's decompiled C

`tools/lc_lookup.py index` joins the map with LordlyCaliber's library manifest
(`native/game/accepted/library-imports.json`, decomp revision `17e07534`):

- 1,109 decompiled functions; the revision 0 bytes of every one match the
  manifest's hash.
- 1,070 map to a function of this port (by start, or as an offset inside one
  of ours where the recompiler split differently).
- 39 do not: libultra OS, controller and video routines (`osSendMesg`,
  `osContQuery`, the VI manager, ...) that `N64ModernRuntime` replaces
  instead of recompiling.

Examples:

| our name | LordlyCaliber | LordlyCaliber's description |
|---|---|---|
| `func_ovlN_801DC740` | `func_00130E60` | encoded inventory item addition |
| `func_ovlN_801AD6BC` | `func_00101DDC` | scripted scenario, site and squad-state transitions |
| `func_8016F13C` | `func_0004501C` | new unit base stats, growth family and equipment |
| `func_ovlN_801B3718` | `func_00107E38` | terrain-aware planned squad routes |
| `func_ovlR_80215C38` | `func_001960A8` | (changed between revisions) |

## 4. Using the tools

```bash
python tools/revmap.py
```

```bash
OGRE_LC_DIR=<LordlyCaliber clone> python tools/lc_lookup.py index
```

```bash
python tools/lc_lookup.py func_ovlN_801DC740 --show
```

The clone stays outside the repository; `--show` reads the C from it. The
outputs under `build/revmap/` are generated and not committed.

## 5. Recompiling revision 0 (phase B, in progress)

`make rev0` (`tools/rev0_gen.py`) produces revision 0's recompiled C without a
MIPS toolchain:

1. `N64Recomp <unit config> --dump-context` writes each unit's sections and
   functions from revision 1's ELF (`build/rev0/ctx/<unit>/dump.toml`).
2. Each section and function is moved to its revision 0 ROM offset (the
   function map) and RAM address (the rule in section 2). The three changed
   functions take their bounds from the gap between matched neighbours, cut at
   `jr ra`: `func_8016D068` 0x98 bytes at ROM `0x042F68`, `func_8016D110`
   0xF4 bytes at `0x043000` (RAM `0x8016D100`), `func_ovlR_80215C38` 0x1690
   bytes at `0x1960A8` (RAM `0x80215C18`).
3. Additions the dump does not carry: the static functions N64Recomp found in
   revision 1 (read from the generated C, with their sizes), and zero-size
   functions for the linker scripts' absolute call targets
   (`undefined_funcs_auto.txt`, `extra_syms.txt`), so those calls become
   runtime lookups as in ELF mode. No `relocs` key is written: in symbol-file
   mode a section with one is relocatable, and calls between sections would
   find no target.
4. `N64Recomp` runs in symbol-file mode on `assets/ogre64-rev0.z64`. A patch
   to N64Recomp (`patches/n64recomp-ob64.patch`) marks the config's
   `function_sizes` entries as size overrides in that mode, as ELF mode does.

Functions keep their revision 1 names, so app code that names a function works
on both revisions.

Result (2026-10-29): all 35 units recompile. Every bank unit has the same
function count and the same number of runtime lookups as revision 1. The main
unit has the same functions apart from statics named by section index
(`static_6_8009E170` is `static_1_8009E170`) and one new jump target at the end
of the changed `func_8016D110`. It has 172 runtime lookups against 189: the
difference is revision 1's cross-bank dispatch, not applied yet.

Not done yet:

| step | what |
|---|---|
| post-processing | `cross_bank.py dispatch` (16 targets) and `gen_bank_funcs.py` (`bank_funcs.inc`) must run on `rev0/`. The app hooks are done: `tools/apply_hooks.py --rev 0` applies all 27 entries of `config/hooks.toml` |
| RSP microcode | `config/rsp-*.toml` ROM offsets for revision 0 |
| app addresses | about 600 revision 1 constants in `app/src` (173 in `bank_overlays.cpp`, the record table the runtime uses on each DMA) need revision 0 values |
| build | a CMake option that compiles `rev0/` and the revision 0 constants into a second executable; the launcher picks it by the ROM's SHA-256 |

## 6. Consequences for the plan

- Revision 0 support (phase B) needs the three changed functions recompiled
  from revision 0, everything else is the same code at known offsets.
- App constants (`0x801F1055`, `0x8018F4A1`, ...) need a per-revision value;
  for addresses above `0x8016D220` the revision 0 value is `- 0x20`.
- Hook anchors (`tools/director_hook.py`) name revision 1 addresses; their
  revision 0 forms follow from the map.

