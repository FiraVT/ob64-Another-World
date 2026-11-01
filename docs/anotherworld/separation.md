# Feature separation

This project's features live in `app/features/` and reach the recompiled game
through a small interface, so that they can move to another base, such as a
recompilation built on the OB64 decompilation, without rewriting them. This
file records what is separated and what still ties the features to `app/src`. Last measured
2026-10-31, branch `another-world`.

## What is separated

| Part | Where | How it reaches the base |
|---|---|---|
| Feature code (52 files, about 15,700 lines) | `app/features/` | `game_host.hpp`, `options.hpp`, `game_events.hpp`, the hook table, N64ModernRuntime's API |
| Menu options (25) | `app/features/options.cpp` | the menu draws them by tab and section (`ui.cpp` `append_choices`); settings.cfg keys and labels unchanged |
| Hooks (43) | `config/hooks.toml` | each names the ROM address of its instruction; `tools/apply_hooks.py` finds the unit |
| Shared game addresses (23) | `app/features/game_addresses.hpp` | each entry names its source |
| Runtime, recompiler and RT64 changes | `patches/*-anotherworld.patch` | applied after lfarroco's `*-ob64.patch`; `tools/patchcheck.py` checks the pair |
| Docs and decisions | `docs/anotherworld/` | lfarroco's records (handoffs, status log, `DECISIONS.md`) are at the git tag `history-archive` under `docs/history/`, as they were at the fork |

## What still ties the features to the base

1. **Edits inside lfarroco's files: 2,718 added lines in 28 files under
   `app/src`.** The largest:

   | File | Added | What it is |
   |---|---|---|
   | `ui.cpp` | 741 | the SAVES, ROSTER and DEBUG tabs, the game's own Settings rows (`GameOption`), FRAME RATE, RETURN TO TITLE, the GRAPHICS and GAMEPLAY tab layout |
   | `overlay.cpp` | 383 | the RmlUi menu bridge, profile load and title return, the frame gate |
   | `sdl_platform.cpp` | 327 | RmlUi input, MUSIC and EFFECTS, screenshots, KEEP POINTER IN WINDOW, PAUSE WHEN UNFOCUSED |
   | `settings.cpp` | 299 | FRAME RATE, RESOLUTION and the other display modes, MUSIC and EFFECTS |
   | `renderer.cpp` | 221 | frame interpolation on the field, PIXEL FILTER, HD textures |
   | `input_map.cpp` | 171 | the modern action map and BUTTON PROMPTS |

   Next step: move each tab's rows into a feature-owned table the way the
   options moved, starting with SAVES and ROSTER.
2. **Feature files that still include a base header:**
   - `button_legend.cpp`: `input_map.hpp` (the bindings) and `font.hpp` (the
     bitmap font it draws with).
   - `save_backup.cpp`: `game.hpp` (the game id) and `save_import.hpp`.
   - `field_select.cpp`: `settings.hpp` (`settings_buttons`).
   - `field_view.cpp`: `settings.hpp` (`game_speed`).
   - `frame_rate.cpp`: `settings.hpp` (`frame_rate_mode`).
3. **35 of the 43 hooks call a named entry point** (`ogre_alignment_requirement`,
   `ogre_field_limit`, ...) instead of an event. Most return a value the
   game's code then uses, which the event table (`void` handlers) does not
   carry. They are portable as they are: a name plus a ROM address. Only the
   8 `ogre_event` hooks go through `game_events.hpp`.
4. **About 270 addresses used by one feature only** stay as constants in that
   feature's file. Each feature file also keeps its own guest-memory read and
   write helpers (16 files).
5. **The generated-code anchors.** A hook's anchor is N64Recomp's text around
   the instruction. A base built with N64Recomp from the same ROM produces the
   same text; a base built another way needs the hooks rewritten as that
   base's patches, by the same ROM addresses.

## Checks

- `make patch-check` (`tools/patchcheck.py`): both patch layers reproduce the
  vendored trees.
- `tools/apply_hooks.py --revert` then `tools/apply_hooks.py`: the generated
  code comes back byte-identical.
- `make dist` builds the package from this layout.

