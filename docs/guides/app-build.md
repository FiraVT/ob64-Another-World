# Building and running the app: section index

This guide was split into topic guides on 2026-11-01. Code comments and older
documents cite its sections by name (`docs/guides/app-build.md` → "Diagnostics
toolkit"). The table maps each section name to the guide that holds it now.

| Guide | Contents |
|---|---|
| [build.md](build.md) | prerequisites, regenerating the recompiled code, the build, running, crash reports, the config directory, troubleshooting |
| [releases.md](releases.md) | `make dist`, the smoke test, GitHub releases and the hosted runners |
| [diagnostics.md](diagnostics.md) | scripted runs, frame capture, the diagnostics toolkit, the live console, checkpoints, saves |
| [controls-and-settings.md](controls-and-settings.md) | bindings, game speed, widescreen, display, sound, mouse, the in-game overlay |
| [code-mods.md](code-mods.md) | code mods (`.nrm`): where they live, building one, hooks |

## Sections

| Section | Guide |
|---|---|
| Prerequisites | [build.md](build.md) |
| Regenerating the recompiled code (start here on a fresh clone) | [build.md](build.md) |
| Configure & build | [build.md](build.md) |
| Third-party patches | [build.md](build.md) |
| Build variants | [build.md](build.md) |
| Running | [build.md](build.md) |
| Distribution — `make dist` | [releases.md](releases.md) |
| Smoke test — `make smoke` | [releases.md](releases.md) |
| Crash reports — `error.log` | [build.md](build.md) |
| Closing the window | [build.md](build.md) |
| Releases (GitHub Actions) | [releases.md](releases.md) |
| Building releases on GitHub-hosted runners | [releases.md](releases.md) |
| Scripted runs and diagnostics | [diagnostics.md](diagnostics.md) |
| The world map's mouse cursor (scene `0x05`) | [diagnostics.md](diagnostics.md) |
| The mission field's mouse cursor (scene `0x03`) | [diagnostics.md](diagnostics.md) |
| Jumping straight into a screen | [diagnostics.md](diagnostics.md) |
| Capturing the native output | [diagnostics.md](diagnostics.md) |
| The game's own frames | [diagnostics.md](diagnostics.md) |
| Reading a dump | [diagnostics.md](diagnostics.md) |
| Diagnostics toolkit | [diagnostics.md](diagnostics.md) |
| A slow screen — a host stack sample, not the profiler (sessions 112, 115) | [diagnostics.md](diagnostics.md) |
| A repeated stutter — `tools/dlgaps.py` (session 116) | [diagnostics.md](diagnostics.md) |
| `make elf-rom-check` — is the linked code the ROM's code? (session 65) | [diagnostics.md](diagnostics.md) |
| The live console — query a running game (session 56) | [diagnostics.md](diagnostics.md) |
| Checkpoints — `save` / `load` (session 58) | [diagnostics.md](diagnostics.md) |
| Saves — the cartridge battery (SRAM), and emulator interchange (session 66) | [diagnostics.md](diagnostics.md) |
| Running with one of these saves — `OGRE_SAVE`, or `tools/run-save.sh` | [diagnostics.md](diagnostics.md) |
| `tools/runlog.py <run.log>` — one screen per run | [diagnostics.md](diagnostics.md) |
| `tools/rdram.py ... image` — is the buffer blank or an image? | [diagnostics.md](diagnostics.md) |
| `OGRE_NJREAD_LOG=1` — which framebuffer the njpeg readback copies | [diagnostics.md](diagnostics.md) |
| `tools/rdram.py diff A.bin B.bin` — what did a run change, by module? | [diagnostics.md](diagnostics.md) |
| `tools/guestmap.py <addr>` — which module is this? | [diagnostics.md](diagnostics.md) |
| `tools/scenemap.py` — the scene registry, the transitions, the script (session 64) | [diagnostics.md](diagnostics.md) |
| `tools/arenamap.py` — every streamed arena, and whether we have it (session 73) | [diagnostics.md](diagnostics.md) |
| `tools/stubmap.py` — which dispatches can find nothing, and who points at them | [diagnostics.md](diagnostics.md) |
| `tools/recompcov.py` — how complete is the recompilation? (session 83) | [diagnostics.md](diagnostics.md) |
| Adding a streamed bank unit (the session-67 recipe) | [diagnostics.md](diagnostics.md) |
| `tools/watch.sh <guest-addr>` — who writes this address? | [diagnostics.md](diagnostics.md) |
| Controller bindings and the in-game overlay | [controls-and-settings.md](controls-and-settings.md) |
| The map | [controls-and-settings.md](controls-and-settings.md) |
| Which N64 controller a pad drives | [controls-and-settings.md](controls-and-settings.md) |
| The CONTROLS tab | [controls-and-settings.md](controls-and-settings.md) |
| Game speed (the SETTINGS tab) | [controls-and-settings.md](controls-and-settings.md) |
| Widescreen (the SETTINGS tab) | [controls-and-settings.md](controls-and-settings.md) |
| Display: window, and the experimental resolution and MSAA rows (the SETTINGS tab) | [controls-and-settings.md](controls-and-settings.md) |
| Sounds and volume (the SETTINGS tab) | [controls-and-settings.md](controls-and-settings.md) |
| Mouse enabled (the SETTINGS tab) | [controls-and-settings.md](controls-and-settings.md) |
| The in-game overlay | [controls-and-settings.md](controls-and-settings.md) |
| Verifying it without a human | [controls-and-settings.md](controls-and-settings.md) |
| Mods | [code-mods.md](code-mods.md) |
| Where the files live | [code-mods.md](code-mods.md) |
| The start screen | [code-mods.md](code-mods.md) |
| Building an example mod | [code-mods.md](code-mods.md) |
| What a hook can reach | [code-mods.md](code-mods.md) |
| Hooking bank code | [code-mods.md](code-mods.md) |
| Four runtime fixes this needed | [code-mods.md](code-mods.md) |
| Config directory | [build.md](build.md) |
| Troubleshooting | [build.md](build.md) |
| RT64 submodules won't check out ("Unable to find current revision") | [build.md](build.md) |
| DXC shader compiler aborts ("Library not loaded: libz.dylib") | [build.md](build.md) |
| `metal` tool missing ("missing Metal Toolchain") | [build.md](build.md) |
| RT64 build still fails after changes | [build.md](build.md) |
