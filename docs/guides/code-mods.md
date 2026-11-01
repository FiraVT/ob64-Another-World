# Code mods (`.nrm`)

Code mods are N64Recomp mods compiled for the game's MIPS code. Data and Lua
mods are described in
[../anotherworld/design-mods.md](../anotherworld/design-mods.md).

The runtime (`librecomp/src/mods.cpp`) has a complete mod system: it opens every
mod it finds, recompiles a code mod's MIPS at load time, applies the function
hooks and replacements the mod declares, and persists the player's toggles and
per-mod option values. The port wires it up and gives it a UI.

**Enabling it was two lines and a screen.** `recomp::start()` already called
`initialize_mods()` and `scan_mods()`, and `wait_for_game_started()` already
loaded the enabled mods — but only when `GameEntry::mod_game_id` is non-empty,
and it was never set, so every mod in `mods/` was parsed and thrown away.
`app/src/main.cpp` now sets it to `MOD_GAME_ID` (`app/src/game.hpp`) and calls
`initialize_mods()` + `scan_mods()` before the start screen, so the screen can
list the mods and write a toggle before the game starts. The second scan inside
`recomp::start` re-reads `mods.json` and keeps the toggles.

## Where the files live

| path | what |
|---|---|
| `<config>/mods/*.nrm` | one file per mod; the packaged mod |
| `<config>/mod_config/<mod id>.json` | that mod's option values |
| `<config>/mods.json` | which mods are enabled, and their order |

`<config>` is the executable's own directory unless `OGRE_PREF_DIR` overrides it.

## The start screen

`app/src/launcher.cpp` draws the start screen's list under the wordmark. The list
is the shared tabbed `ui::Panel` (`app/src/ui.cpp`), so the in-game overlay shows
the same rows. There are five tabs:

```
[ MAIN ] [ MODS ] [ CONTROLS ] [ SETTINGS ] [ DEBUG ]
```

```
MAIN         Start Game starts the game ([ ] and not selectable until a ROM is
             loaded), then the ROM row, which reads [x] ROM loaded with the file
             name or [ ] No ROM and opens the file picker, then Exit Game, which
             quits. Exit Game is last on purpose: it is the one row a stray
             SPACE or ENTER must not land on.
MODS         one row per mod, then one row per visible option of that mod; a
             mod's short description sits in a second column
CONTROLS     one row per N64 button with its keyboard key, its gamepad source and
             the field-map action; see "Controller bindings and the in-game
             overlay"
SETTINGS     GAME SPEED, then a DISPLAY section (WIDESCREEN, WINDOW, plus the
             experimental RESOLUTION and MSAA rows when
             OGRE_DISPLAY_EXPERIMENTS=1), an AUDIO section (SOUNDS, VOLUME) and
             an INPUT section (MOUSE ENABLED); see the SETTINGS sections below
DEBUG        the live Chaos Frame readout
```

```
TAB / SHIFT+TAB  switch tab
UP / DOWN        select a row (section headings, and Start Game before a ROM is
                 loaded, are skipped)
SPACE            activate the selected row: Start Game plays, Exit Game quits, the
                 ROM row opens the file picker, a mod row toggles, an option row
                 steps, the GAME SPEED row steps, a binding row arms a rebind
LEFT / RIGHT     step the selected option's or setting's value
ENTER            play, or open the ROM picker when no ROM is loaded or the ROM
                 row is selected; on Exit Game it quits, the same as SPACE
mouse            click a tab to switch, a row to activate it, a GAME SPEED radio
                 marker to set that speed, elsewhere to play
```

Choosing a ROM — by picker or by dropping one on the window — **does not start
the game**. The ROM row shows `[x] ROM loaded` with the file name, Start Game
becomes selectable, and the selection moves to it; ENTER or SPACE then plays.
That is what makes START GAME meaningful on the first run, and the ROM row is
what makes the whole screen usable from the keyboard alone.

A toggle goes to `recomp::mods::enable_mod`, which writes `mods.json`; an option
value goes to `recomp::mods::set_mod_config_value`, which writes
`mod_config/<mod id>.json` on the runtime's config thread. The start screen
appears when no ROM is loaded and whenever a stored ROM is ready with at least
one mod installed, so the player always has a way to turn a shipped mod off. Any
mod can be removed by deleting its `.nrm`.

## Building an example mod

`mods/item-grant-log/` is the reference: one code mod, one entry hook on a
function in a streamed bank unit, and the section macros in `include/`. A
manifest's `enabled_by_default = false` keeps a mod off until the player turns
it on in the MODS section. The runtime has always read that field from
`mod.json`; `RecompModTool` did not write it, so every mod was enabled — the
tool now parses and emits it (`patches/n64recomp-ob64.patch`).

The first reference, lfarroco's `skip-boot-logos`, was removed on 2026-11-01
(SKIP INTRO does the same in the port, `ogre_skip_intro` in
`app/src/bank_overlays.cpp`). It wrote the pending-scene word from scene
`0x09`'s update so the boot goes straight to the title. What it found still
holds:

**Skip to the title from scene `0x09`, not from scene `0x0A`.** Writing the word
from scene `0x0A`'s update cuts the stills to one frame, and the title then
produces no display lists at all and the last presented frame stays on screen.
Scene `0x0A`'s enter and leave set up and tear down globals in overlay C that
only its *completed* state machine leaves consistent. Entering the title from
scene `0x09` never runs `0x0A` and renders correctly.

```sh
make mod-syms        # -> mods/reference/{dump,data_dump}.toml
make example-mods    # -> build/mods/<name>.nrm for each mod in mods/
```

`make mod-syms` runs `tools/merge_mod_syms.py`. The script runs N64Recomp with
`--dump-context` on `config/config.toml` (the linked `build/ogrebattle64.elf`)
and on each `config/banks/config-bank<U>.toml` (the bank ELFs that
`make bank-recomp` linked), and appends the 44 bank sections and their 3512
functions to the main dump. A mod names the functions it hooks, and the mod tool
resolves those names against that dump. Both files are generated and
gitignored, for the same reason `RecompiledFuncs/` is: they need the ROM.

`tools/build-example-mods.sh` compiles each mod's `src/*.c` to big-endian MIPS
and runs `RecompModTool` on the result. Apple's clang has no MIPS target, so the
script uses a `mips-linux-gnu-gcc` when one is on `PATH` and otherwise runs
`gcc-mips-linux-gnu` in a `debian:bookworm-slim` container.

`make dist` packages no code mods (docs/anotherworld/DECISIONS.md
2026-11-01). The package's `mods/` folder is empty, for code mods a player
adds; the example mods are JSON and Lua folders in `content/examples/`, and
`tools/smoke-dist.sh` checks that they are there.

## What a hook can reach

A mod declares a hook as a *section ROM address + function vram* pair. The
runtime resolves the pair in `build_regen_list` (`librecomp/src/mods.cpp`)
through two tables:

- `get_vrom_to_section_map()`: the base ELF's own code sections (`.entry`,
  `.main`, `.streamedA/B/C`), registered by `init_overlays`.
- The bank hook registry (`recomp::overlays::register_bank_hook_sections`): the
  44 streamed-overlay bank records from `app/src/bank_funcs.inc`, which
  `register_bank_overlays()` (`app/src/bank_overlays.cpp`) registers from the
  `on_init_callback`, after `init_overlays` and before `load_mods`.

## Hooking bank code

`RECOMP_HOOK("func_ovl<U>_<vram>")` and `RECOMP_HOOK_RETURN(...)` work on a
function in a bank unit (`Bank<U>Funcs/`). `mods/item-grant-log/` is the
reference: one entry hook on the item grant routine `func_ovlN_801DC740` (bank
unit N, record 7, ROM `0x101D00` -> RAM `0x801AD5C0`), which writes
`[mod] item grant equipment <id>` or `[mod] item grant consumable <id>` for each
item the game grants in a mission.

The parts:

1. **Reference symbols.** `make mod-syms` merges every bank unit's
   `[[section]]` blocks into `mods/reference/dump.toml`
   (`tools/merge_mod_syms.py`). `RecompModTool` reads one function reference
   file, and `from_symbol_file` numbers sections by position, so appended
   sections leave the main sections' indices unchanged. The main ELF's two raw
   asset blobs (`._66E30`, `._1F0A00`) have data sections whose `rom` equals a
   bank record's ROM start with a different `vram`, which the mod tool rejects;
   the script drops their `rom` key, which keeps them absolute as before. The
   five existing example mods package byte-identically with the merged dump.
2. **The registry.** The bank records stay out of `sections_info`, because that
   table also drives the DMA-driven section loader (`notify_rom_read`) and the
   `section_addresses` relocation table, and a bank record must enter neither.
   `build_regen_list` consults the registry only when `section_vrom_map` misses.
   It takes the record's RAM base, uses no relocs (bank code is linked at its RAM
   base with absolute immediates, `.num_relocs = 0`), and finds the function in
   the record's own table.
3. **`rom_size`.** `BankFunctionEntry` carries the function's ROM size, which
   `tools/gen_bank_funcs.py` copies from `recomp_overlays.inl`. The hook path
   regenerates the function from that many ROM words.
4. **When it applies.** `patch_func` rewrites the entry of the recompiled bank
   function's native body at mod load. The body is the same pointer
   `on_streamed_dma` puts in the function map whenever the game DMA's the record,
   so the hook is live every time the bank is resident, with no work at bank
   load.

Limits:

- **The hooked function is regenerated from ROM.** Text edits that the build
  makes to generated C (the app hooks in `config/hooks.toml`, applied by
  `tools/apply_hooks.py`, and the `cross_bank.py` dispatch and tail-call
  repairs) are absent from the hooked version of that function. Do not hook a
  function that one of those edits changes.
- **Every call in the regenerated body is a `get_function` lookup**
  (`use_lookup_for_all_function_calls`). A `jal` into a function interior, or
  into another bank's RAM that the static build bound with a dispatch, resolves
  to whatever the function map holds at that address. Session 123's hook on
  `func_8016B774` (`.streamedB`) crashed the save load this way: its regenerated
  body failed `get_function`. Before hooking, list the function's `jal` targets
  (`mips-linux-gnu-objdump -d build/bank<U>.elf --start-address=...`) and check
  that each is a function entry that is resident when the function runs.
  `func_ovlN_801DC740` has one, `jal 0x80093540`, a `.main` entry that the
  static code already calls through `LOOKUP_FUNC`.
- **Hooks only.** `RECOMP_PATCH` on a bank function and a mod relocation against
  a bank section are not wired (`parse_mod_symbols` still resolves sections
  through `section_vrom_map` only). A mod calls bank code by reading or writing
  RAM, or from a hook.
- **The dump and the app must come from the same bank ELFs.** Run
  `make bank-recomp` before `make mod-syms`; a function that moved between them
  fails at mod load with `InvalidHook`.

## Four runtime fixes this needed

All four are in `patches/n64modernruntime-ob64.patch`; without them a hook either fails
to load, corrupts memory, or the periodic snapshot kills the process.

1. **Section indices.** A generated `RelocEntry.target_section` is the
   *recompiler's* number for an ELF section (this port's code sections are
   3/6/9/12/16 of 23). `populate_reference_symbols` looks the function up by its
   *position* in the code-section table, so it found nothing and the mod failed
   with `Failed to load mod code (Code mod loading internal error)`.
   `context_from_regenerated_list` now translates the index
   (`get_code_section_index_from_section_index`).
2. **Data relocations.** A HI16/LO16 relocation against a section that holds no
   code is a data reference. This project recompiles with absolute symbols, so
   the immediate already holds the final address and the runtime has no load
   address for that section (`section_addresses[data_section]` is zero).
   Regenerating it as a reference-symbol relocation emitted a load from address
   0 and the following store ran off the end of RDRAM. Those relocations are now
   left as the immediates they are.
3. **macOS code pages.** macOS maps the executable's `__TEXT` with an `r-x`
   maxprot, so `mprotect` fails with `EACCES` and `vm_protect` returns
   `KERN_PROTECTION_FAILURE`; patching a recompiled function's entry to point at
   its hooked version is a write there, and it died with `SIGBUS` inside
   `apply_regenlist` -> `patch_func`. `unprotect` now replaces the page with a
   private anonymous copy whose maxprot is `rwx` (`vm_remap` with
   `VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE`) before making it writable.
4. **The periodic `[snap]` snapshot dereferenced 4 GiB past RDRAM.** The runtime
   dumps a message-queue snapshot every ~90 VI retraces
   (`ultramodern::debug_dump_queue_snapshot`), enabled with `OGRE_SNAP` (session
   116 made it opt-in; it ran unconditionally before that). Its guest addresses are 32-bit,
   and it converted them with `TO_PTR`/`addr - 0xFFFFFFFF80000000`, which only
   works on a value that is already sign-extended: a bare `uint32_t` zero-extends,
   so the computed host address was `rdram + addr + 0x80000000`. Whether that
   faults depends on the process address map, which is why it was intermittent —
   the snapshot had been printing garbage (`count=-1996517056`) and the
   developer's run died in `debug_dump_queue_snapshot + 0xF7` with the fault
   4 GiB past RDRAM. It now sign-extends before the map, as `function_trace.cpp`
   already did. The snapshot is a diagnostic, so this is a fix to the diagnostic;
   a `[snap]` line with an impossible count is now a bug report about the game,
   not about the snapshot.
