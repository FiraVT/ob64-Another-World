# Recompilation status and findings

This file holds the status of the recompiled game that OB64: Another World runs
on: what the port runs, its open defects, and the technical findings about the
game. The project's own plan is `docs/anotherworld/ROADMAP.md`, what is built
is `docs/anotherworld/FEATURES.md`, and setup is `CONTRIBUTING.md`.

The port uses **static recompilation** (`N64Recomp`): the ROM's MIPS code is
translated to C and compiled natively against the `N64ModernRuntime` runtime
library, as in Zelda 64: Recompiled. It is not a matching decompilation. The
original C source is not reconstructed and no byte-identical ROM is built.

No copyrighted ROM data is committed to this repository. You must provide a
dump of your own cartridge.

Working rules for reverse-engineering: [`AGENTS.md`](AGENTS.md). The per-session
record up to the fork (sessions 3 to 144) is at the git tag `history-archive`
(`docs/history/README.md`).

---

## Browser port

The browser (WebAssembly) build was removed on 2026-11-01: a browser version is not a goal (docs/anotherworld/DECISIONS.md). Its history is in git and in the session handoffs.

## Status

The game runs end to end. A build boots the publisher stills, the title, the New
Game opening, the map, missions and battles, the tutorial, the shop, the
Organize Screen, the credits, the ending and the attract loop.

- **Recompilation** — 99.05% of the ROM code span `0x001000..0x2B8B70` is
  compiled; the one gap (`0x0DDF80..0x0E4910`) is data. 44 records in 34 bank
  units (`BankA`..`BankZ`, `BankAA`..`BankAH`), 5037 registered function entries
  at 4963 addresses, 986 static dispatch targets with 0 unresolvable.
  `tools/recompcov.py` measures all of it.
- **Streamed code** — no un-recompiled module remains on any reachable path
  (session 85). A hand-played session executes 44.4% of the entries; the rest
  are paths nobody has played.
- **Windows build** — the Release workflow's Windows job compiles the app with
  clang-cl against the MSVC and UCRT headers, and those headers declare no
  two-argument `mkdir`. The trace dump directories now use
  `std::filesystem::create_directories` (`ensure_dir` in `renderer.cpp`), the
  call the rest of the app uses, and the job's four errors are gone (session
  135). A hosted run needs the private data bundle regenerated for the commit it
  builds, because the unpack step refuses a bundle whose recorded commit differs
  from `GITHUB_SHA`; `platforms=windows` is the ~8-minute iteration path.
- **Renderer** — RT64 (Metal, Vulkan, D3D12) with a null renderer for bring-up.
  RT64's **"Fix LR with Scissor"** rect enhancement is off by default here
  (`OGRE_RECT_LR_FIX=1` restores it): it rewrites a rectangle's right edge to the
  scissor's edge whenever the two are within one pixel, which widened the unit
  profile's rightmost DEF/MDEF digit rectangle and made it sample the next font
  cell (session 101). RT64's framebuffer tile copy is refused when the tile's
  texels lie outside the framebuffer's `lastWriteRect`, the rect the copy is
  actually filled from, and when the source framebuffer was not written in the
  workload being processed (`Framebuffer::lastWriteWorkload`, printed as
  `[fbcopy-nowrite]`); a texture load then reads RDRAM. The first condition fixed
  the flat ENV-coloured block on a map unit's head (session 134), the second the
  same block in a second cutscene, where the record was a leftover from an earlier
  scene whose stale rect covered the tiles the game had put its sprite scratch on
  (session 136, open work 0).
- **Audio** — the game's own audio microcode is recompiled and runs by default.
  The Linux package needs a static SDL2 with an ALSA, PulseAudio or PipeWire
  backend. The 0.3.0 build host had no audio development headers, SDL2 dropped
  all three without failing, and the shipped binary ran silently on every
  PipeWire and PulseAudio machine (`dsp: No such audio device`). `make
  sdl2-static` and `tools/smoke-dist.sh` now fail instead of shipping that
  (session 100, issue #8).
- **Saves** — the cartridge battery is SRAM and works; the Controller Pak menus
  render.
- **Player features** — `docs/anotherworld/FEATURES.md` lists them, and
  `docs/guides/controls-and-settings.md` documents the panel. The start screen
  and the `ESC` overlay draw one `ui::Panel` with the tabs MAIN (the ROM row and
  EXIT GAME), MODS, CONTROLS, GRAPHICS, AUDIO, GAMEPLAY, ROSTER (overlay only)
  and DEBUG (`app/src/ui.cpp`, `Panel::tab_label`). The package ships no code
  mods; content mods are JSON packs and Lua scripts
  (`docs/anotherworld/design-mods.md`). `make release VERSION=vX.Y.Z` prepares
  the data bundle the hosted runners unpack and dispatches the Release workflow
  as a draft (`docs/guides/releases.md`).
- **Mouse (world map)** — the map screen's cursor follows the pointer, a left
  click acts as the game's `A` on the location under it, and pushing the pointer
  past the map's edge scrolls the view. The operating system pointer is hidden
  while it is over the game window and shown over the ESC overlay window. The
  cursor is the map module's screen-space sprite at `state+0x52`/`+0x54`
  (`state = *(u32*)0x80197B18`, clamped by `func_ovlM_8019AF0C` to x
  `0x28..0x110`, y `0x28..0xC0`), written at the scene hook's cursor draw
  (`func_ovlM_801A103C`, `0x801A1C48`). The draw site is bank code, so
  `config/hooks.toml` inserts the call and `make bank-recomp` re-applies it
  (session 127). The click and the edge direction ride `get_input`, the path the
  keyboard uses, and are gated on the map's cursor draw so they are inert on
  other screens. **Both this screen and the mission field are behind
  SETTINGS > INPUT > MOUSE ENABLED, off by default** (session 129): the row
  publishes to `ogre_map_cursor_set_enabled` / `ogre_mission_cursor_set_enabled`,
  and `OGRE_MAP_CURSOR=1`/`=0` still forces this screen for a scripted run.
  `OGRE_CURSOR=0` keeps the operating system pointer visible
  (`docs/guides/diagnostics.md` → "The world map's mouse cursor"). The merged
  contribution that claimed this feature had no caller and is deleted.
- **Mouse (mission field)** — the mission screen's cursor follows the pointer and
  a left click acts as the game's `A`. The cursor is two f32 framebuffer pixels
  at `state+0x08`/`+0x0C` (`state = *(u32*)0x801AB870 = 0x800E91D0`, a fixed
  address), kept by the mover `func_ovlN_801BE4E0` and clamped to x 40..280,
  y 30..210. The hook is inserted at the generated label `L_801BEC04`, after both
  clamps, by `config/hooks.toml` and re-applied by `make bank-recomp`
  (session 128). **The mission's cursor is digital (D-pad) where the map's is
  analog (the stick), and it is a servo the game moves on nearly every frame**
  (1-9 px with nothing held, measured), so the pointer hands it to the game only
  on an explicit signal — a direction held, the C-LEFT centring branch, the
  mover's own delta, or the window after a click — and adopts the game's position
  then. The click window is what leaves the cursor on the place a command acted
  on. **The view scrolls when the pointer is close to a play-area edge**
  (session 129): the field's camera pans with the cursor, not with a held
  direction. The mover turns the held D-pad into the delta bytes
  `state+0x06`/`+0x07`, and `func_ovlN_801BECF0` (called right after the mover at
  `0x801AF130`) adds `delta/128 * 8.5 * table[e]` to the eye and the look-at x of
  the camera at `*(u32*)0x8018F5AC` (`0x801BEE04`-`0x801BEE1C`; the same for z at
  `0x801BEE9C`-`0x801BEF5C`), clamped to the world rect at `0x801F0DD8`, and only
  while `*(0x801F3630)` is set, which the mover sets only when the cursor pair
  changed. The pointer's pixel is clamped by the mover, so the edge intent
  arrives as input: within `OGRE_MISSION_CURSOR_EDGE` (24) px of a bound the hook
  holds the matching N64 D-pad bit, `get_input` ORs it into controller 0, and the
  `pad=` signal already makes the pointer adopt the game's cursor, so the two do
  not fight. Developer-confirmed. Both this screen and the world map are behind
  SETTINGS > INPUT > MOUSE ENABLED, off by default (session 129), and
  `OGRE_MISSION_CURSOR=1`/`=0` forces this screen for one run
  (`docs/guides/diagnostics.md` → "The mission field's mouse cursor").
- **HD backgrounds (experiment)** — `mods/backgrounds/` is a pack of PNGs that
  replaces the dialogue/cutscene backdrops. The app writes the pack's image over
  the pixels the njpeg readback copies, split to match the game's four-chunk
  496x384 backdrop canvas, so one PNG fills the whole backdrop. The first
  mapping is scene `0x0D` step 2 (the New Game cathedral), with a plain
  `01.png` and a widescreen `01-wide.png`. With WIDESCREEN on, scene `0x0D`
  expands whether or not the pack is installed (its own 496x384 backdrop has
  content the 4:3 view crops), and a scene the pack covers is added to the RT64
  Expand set, so the backdrop fills the 16:9 window and reveals more of the
  canvas at the sides while the dialogue box and text keep their size.
  `OGRE_BG=0` runs without the pack. `tools/backgrounds.py` (and `make
  bg-extract`) extracts the game's own backdrop as the reference an artist
  draws over; the output lives in the gitignored `mods/backgrounds/reference/`.
  It is a reskin at the game's resolution, and not an HD texture replacement.
  See `docs/guides/hd-backgrounds.md` and
  `history-archive:docs/history/handoffs/HANDOFF-2026-09-29-session114.md`.
- **Performance** — five misclassified work loops in bank unit N carried a
  recompiler-injected blocking `yield_self`; their backward branches are listed
  in `config/banks/config-bankN.toml`'s `yield_work_loop_branches`
  (`0x801B3008`, `0x801DA2C0`, `0x801D0C08`, `0x8020A910`, `0x80204C48`;
  sessions 80, 90, 98, 112, 115). A slow screen is measured with `OGRE_PROFILE=1
  OGRE_DL_TRACE=1 OGRE_SCENE_LOG=1` for its display-list series and its `t4`
  `[prof]` line, but the profiler reports the most recently entered function and
  can name an entry-heavy function rather than the one holding the thread. Take a
  host stack sample (`sample <pid> 4 -file …`) during the slow state as well; that
  named `func_ovlN_80208E84` in session 112 and `func_ovlN_80204C08` in session
  115 after the profiler had named other addresses. `debug/menu-probe.sh` runs
  the app, the sampler, the console `snap` output and the coverage census
  together, so no capture time has to be agreed in advance.
- **Periodic hitch** — the runtime's message-queue snapshot
  (`ultramodern::debug_dump_queue_snapshot`) ran unconditionally every 90 VI
  frames on the Critical-priority VI thread, which measured as a 33 -> 59 ms
  frame every 1.5 s on macOS and is larger on Windows, where its
  `sleep_for(200us)` sampler resolves to SDL's 1 ms timer. It is now behind
  `OGRE_SNAP` and off by default (session 116). `OGRE_SNAP=1` restores the old
  ~1.5 s period for hang diagnosis; a number sets the period in VI frames.
  `tools/dlgaps.py <run.log>` measures the next such report: it prints the gap
  distribution and the phase histogram of the spikes, and `--check` fails when
  they are periodic. See `history-archive:docs/history/handoffs/HANDOFF-2026-10-01-session116.md`.
- **Debug console** — the live console's `snap [prefix] [ms]` writes the whole
  8 MiB RDRAM image with the game threads parked and, for `ms`, RT64's
  presented-frame capture for the same screen, so a player-reported defect can be
  read offline without a bounded run (`docs/guides/diagnostics.md`, session 101).
- **Windows** — v0.4.0's access violation on the first present is fixed, released
  and confirmed by a Windows run. The presented-frame capture was left enabled by
  `init_capture_env`'s `putenv` name-mangling (the UCRT copies the string), and
  RT64's readback handed the D3D12 backend a null texture. The capture path is
  now app-owned and the D3D12 copy skips sample positions for a buffer
  destination (session 107). The released Windows package of 2026-09-28 carries
  the fix: session 113 measured it opening its window on the developer's GeForce
  940MX / Intel HD 620 laptop with `d3d12.dll` and `D3D12Core.dll` loaded and no
  `vulkan-1.dll`, exit 0 and no new `error.log`. Players on v0.4.0 need the newer
  package. **Those two changes were not in any release**: the hosted runner
  builds the third-party trees from `patches/`, and both RT64 patches had drifted
  from them (session 117). The patches are regenerated and `make patch-check` now
  fails when a patch stops describing its tree. The captured `stdout`/`stderr`
  sections are empty in an Explorer launch because the process starts with NULL
  standard handles, and in that state the capture records nothing at all
  (session 119). A player's report therefore carries no log, and
  `ogrebattle64.exe > run.log 2>&1` from a shell discards the redirect as well.
- **Release parity** — a GitHub-hosted release builds three different sources: the
  public repository, the private generated-code bundle (`ogre-data/files.tar.gz`,
  which has a commit-drift check), and the `patches/` applied to pristine
  third-party checkouts, which had none. `tools/patchcheck.py` and
  `make patch-check` add it, and `tools/release-build.sh` runs it before
  packaging. Measured: v0.5.1 was built from `c7bd999` (session 112), so session
  115's slowdown fix and session 116's stutter fix are in no release, and the
  bundle holds 34 `yield_self` sites against the tree's 33 (session 117).
- **Debug behaviour in a release** — an audit with no environment variables set
  found the live console channel (a fixed path whose commands write guest RAM and
  load checkpoints), 116 `[rsp]` lines/s, per-display-list `[renderer]` lines, and
  trace hooks on every recompiled function entry and return. The console is now
  off unless `OGRE_LIVE_CONSOLE=1` (or the trigger's own variable) is set, so a
  stock run opens no path, reads no keyboard and prints nothing (session 117). The
  rest is open work below.

What each screen should show is `docs/scenes.md`. Build instructions are
`docs/guides/build.md`, and every `OGRE_*` knob is in `docs/guides/diagnostics.md`. A picture or a display list that a
run produces is evidence about the port until the five checks in
`docs/guides/emulator-first.md` pass.

## Open work

Ordered by how much each item blocks a player. Each item names the handoff that
holds the evidence.

0. **A map unit's head sprite drew as a flat ENV-coloured block. Fixed in two
   steps (sessions 134 and 136).** The head is the mission field's 14-part
   map-unit sprite and its 16x15 colour tile was served by RT64's framebuffer
   tile-copy path instead of being read from RDRAM. A framebuffer at masked
   `0x2E6840` whose `lastWriteFmt` is `G_IM_FMT_DEPTH` is still on record over the
   heap the game builds the head-down bitmap in (`func_8019ABC4`, `0x802FB0E8`),
   and `FramebufferManager::createTileCopyRecord` fills a tile copy from that
   framebuffer's `lastWriteRect` only. The head's tile (`468,65..484,67`) lies
   outside it, so the copy it sampled was never written: `TEXEL1` read zero and the
   combiner `(1 - TEXEL1) * ENV + TEXEL1` (`FC62FE03 2F0D73FD`, `G_CYC_2CYCLE`)
   painted the flat `ENV >> 3` = `0x2947` over the mask's correct silhouette
   (session 134). The same fault in a second cutscene, reached from `pre_ogre.bin`,
   showed the write-rect test is one instance of a larger condition: there the
   source record (`0x32AF40`, 320x240 RGBA16) is not a colour image of the frame at
   all (`SETCIMG` is `0x400`), its stale `lastWriteRect` `0,0..319,239` covers the
   tiles, and the addresses hold the game's CPU-built sprite scratch, so the
   guard served 23 copies and the head was the frame's own `G_SETENVCOLOR 0x000001`
   (session 136). A tile copy is now served only when the source framebuffer was
   written **in the workload being processed** (`Framebuffer::lastWriteWorkload`
   against the manager's per-workload `usedTimestamp`, `[fbcopy-nowrite]`), which
   keeps the copy for a framebuffer this frame rendered and refuses a leftover one.
   `FramebufferManager::checkRegionsTMEM` holds both conditions; the load then
   reads RDRAM the way hardware does. Verified in the first scene (the head box
   holds 1 pixel of `0x2947` against 76 before) and in the second (14
   `[fbcopy-nowrite]` refusals, 0 `[fbcopy]`, developer-confirmed), with the
   scene-14 regression frame matching the session-134 verified dump and its
   `y=120` scenery row unchanged. A blanket refusal of depth framebuffers as colour
   sources also fixed the first scene but changed one scenery scanline, because a
   one-row strip legitimately samples the same framebuffer's rendered rows; the
   write-rect test keeps that copy. Instruments that found it and stay:
   `OGRE_SPRITE_TRACE=1` (one line per small tile with the draw-call state,
   descriptor and resolved hash, printed after the upload decision; stops at 40000
   lines), `OGRE_TILE_DECODE=1` (the RGBA the renderer decodes out of the TMEM
   snapshot it hashes, plus the raw-TMEM path and the `[fbcopy]` lines), and
   `OGRE_TILE_TRACE_ALL=1` with `OGRE_TILE_TRACE_MAXH=64` (cap 5000). Residual: a
   freshness test against `fb->RAMHash` is unreliable on its own because
   `hashTracking` re-hashes every framebuffer at each frame end without touching
   the GPU copies, and the writers of the two leftover framebuffer records
   (`0x802E6840` and `0x32AF40`) are not identified; neither address is in its
   fault frame's image. See `history-archive:docs/history/handoffs/HANDOFF-2026-10-11-session134.md`,
   `history-archive:docs/history/handoffs/HANDOFF-2026-10-12-session136.md`, `docs/notes/black-sprite.md` §10-§11,
   and the session-133 handoff for the evidence that led here.

1. **Windows access violation when the game starts (`0xC0000005`). Closed, and
   verified on Windows (session 113).** Session 107 root-caused v0.4.0's fault at
   `ogrebattle64.exe+0x10001AE`: `plume::d3d12::D3D12CommandList::setSamplePositions`
   with a null `texture`, called from `copyTextureRegion` with a `PlacedFootprint`
   (buffer) destination, which is the port's own presented-frame readback. The
   readback ran because `init_capture_env` hid `OGRE_CAPTURE_PRESENT` by rewriting
   the first byte of a `putenv`'d name, which the UCRT's copying `_putenv` ignores.
   `snap` now toggles an app-owned path through `ogre_present_capture_path()`,
   the D3D12 path skips sample positions for a non-texture destination, and
   `tools/smoke-dist.sh` fails a package whose capture is on. The released Windows
   package of 2026-09-28 contains the fix and not the defect (the pre-fix
   `OGRE_CAPTURE_PRESENT=` string is absent, session 107's `capture path:` strings
   are present), and a bounded run on the laptop that produced the report
   initializes D3D12 (`d3d12.dll`, `D3D12Core.dll`, no `vulkan-1.dll`), opens its
   window, exits 0 and writes no `error.log`. Part (b), the failure on releases
   older than the capture code, no longer reproduces: RT64's
   `Falling back to Vulkan due to device workaround.` did not run, because the
   adapters report Intel `31.0.101.2140` and NVIDIA `32.0.15.8266` (582.66),
   both above session 94e's thresholds. The captured `stdout`/`stderr` sections
   are empty in an Explorer launch, and that is now root-caused: the process
   starts with NULL standard handles, and in that state the capture records
   nothing — a six-second boot puts 101 `stdout` and 53 `stderr` lines into the
   report with valid handles and 0 lines with NULL handles, so the device, the
   driver version and the fallback are still not visible in a player's report.
   The proposed fix is to give fds 1/2 a real destination before
   `crash_log::install`, and to stop the `AttachConsole` branch from freopening
   `CONOUT$` over an inherited redirection. See
   `history-archive:docs/history/handoffs/HANDOFF-2026-09-30-session119.md`,
   `history-archive:docs/history/handoffs/HANDOFF-2026-09-30-session113.md`,
   `history-archive:docs/history/handoffs/HANDOFF-2026-09-27-session107.md` and
   `history-archive:docs/history/handoffs/HANDOFF-2026-09-20-session94.md` part 5.
2. **SIGBUS after a lost battle, `func_800862C0+0x59A` at guest `0x80086328`.**
   The faulting instruction is identified and the writer is not. The instruction
   is `lw $s6, 4($a1)`, where `$a1` is `+0x7C` of an effect object; the value
   looks like a misread record field, not a pointer, and the crash lands on a
   scene/mode transition. The effect subsystem itself has no cross-bank call
   site, so the write that corrupts the record list comes from another resident
   function. See `history-archive:docs/history/handoffs/HANDOFF-2026-09-21-session96.md` §1.
3. **Title sprite green border, a regression.** A transparent sprite composites
   opaquely for 1-3 presents on a type's first appearance, and the sprite's colour
   texture border shows as exactly `(0,132,0)`. The mask and the combiner are
   byte-exact; the failing draw reaches the `M == PM_FRAMEBUFFER_COLOR` arm with
   `forceBlend` set and returns `finalAlpha = 1` instead of the combiner alpha
   (`rt64_blender.h`). The defect is present in every source state buildable on
   this machine, including RT64 at the 2026-09-17 commit `52f7160`, so no RT64
   change since then causes it and none fixes it. The next instrument is in the
   shader at the tagged draws: log `otherMode.forceBlend()`, `cycleType()`,
   `combinerCycles` and `blenderInputs()` and compare them with the CPU's values
   for the same `omL`. See `history-archive:docs/history/handoffs/HANDOFF-2026-09-21-session96.md` §2-§3.
4. **Credits screen (scene `0x11`) artifacts, and three latent leak-shaped
   `jal`s.** The screen plays; its backgrounds carry artifacts, and three `jal`s
   are emitted in the call-plus-early-`return` shape that leaks the frame-pump
   stack if their scene is reached. See
   `history-archive:docs/history/handoffs/HANDOFF-2026-09-19-session86.md` §5 and `-session85.md` §8.
5. **Teardown crash `func_8007F8E4+0x2F8` after the DMA census dump.** The
   window-close path dumps the DMA census and then faults. See
   `history-archive:docs/history/handoffs/HANDOFF-2026-09-19-session85.md` §4.
6. **Latent static backlog.** `tools/stubmap.py report` classifies the 986 static
   dispatch targets as 130 missing-function candidates, 152 bank-selection
   candidates, 369 inert, 332 resolvable and 3 IMEM. The main unit also makes 951
   direct calls into swappable RAM across 236 targets (`make cross-bank-check`),
   a known backlog that `cross_bank.py dispatch --only` fixes one target at a
   time. The 288 indirect `jalr` sites are outside any static measurement; a
   run's stub log is the only check for them.
7. **Controller Pak copy/backup (cartridge battery ↔ Controller Pak
   `Save`/`Load`/`Erase`).** The screen renders; the copy flow is deferred. The
   developer's decision is that it is low priority for a PC port. The device half
   (`osPfs*` and a flat 32 KiB `.mpk`) is implemented. See `history-archive:docs/history/DECISIONS.md`
   (88b).
8. **Web build.** Removed on 2026-11-01 (a browser version is not a goal).
9. **Empty `mods/` in the upstream v0.4.0 archives.** Closed on 2026-11-01: the package ships no code mods, and the release tools no longer require an `.nrm` (`docs/anotherworld/DECISIONS.md`). The upstream release history is in `history-archive:docs/history/handoffs/HANDOFF-2026-09-27-session108.md` and `-session111.md`.
10. **HD backgrounds: true HD needs the renderer.** `mods/backgrounds/` reskins
    the backdrop at the game's own 320x240 (box-filtered to the 496x384 canvas),
    so its detail is capped by the original. Sampling a larger texture needs
    RT64 texture replacement, keyed by the drawn texture's hash. RT64 already has
    the machinery (`TextureCache::loadReplacementDirectory`, keyed by a TMEM
    hash) and `upscale2D = ScaledOnly`, but the port does not load a replacement
    directory and the hash for one backdrop has to be recorded from a run. The
    four-chunk layout the current feature uses is hardcoded to the cathedral
    asset's chunk sizes; a pack image for a backdrop with a different layout is
    left alone (`no canvas rect`). See `docs/guides/hd-backgrounds.md`.
11. **Confirm the periodic-hitch fix on Windows, and keep the hang dump
    available.** Session 116 measured the snapshot's 1.5 s hitch on macOS and
    gated it; the Windows size is inferred from SDL's `timeBeginPeriod(1)` and
    the 200 us sampler, so one run there decides it (`OGRE_DL_TRACE=1` with
    `OGRE_SNAP` unset against `OGRE_SNAP=1`). With the gate off by default a hang
    produces no `[snap]` output unless the run asked for it; a
    frame-counter-triggered dump would keep the diagnosis at zero periodic cost.
    See `history-archive:docs/history/handoffs/HANDOFF-2026-10-01-session116.md`.
12. **Gate the remaining debug behaviour that ships (session 117).** The live
    console is done: it is off unless `OGRE_LIVE_CONSOLE=1` or one of its own
    trigger variables is set. What is left, ranked by the difficulty of removing
    it rather than by the cost:
    (a) `[rsp]` (116 lines/s) and the per-display-list `[renderer]` lines — gate
    them and set the variable in `make smoke`, `tools/smoke-dist.sh` and the
    CI `runlog.py --check`, or those checks silently stop checking anything;
    (b) the trace hooks on every recompiled function entry and return (28 + 4
    instructions at 5316 sites with the gates off) — making the disabled path a
    load-and-branch needs a change to `patches/n64recomp-ob64.patch`, so it
    regenerates the game code and re-runs the stutter measurement;
    (c) the audio auto-response on `0x800C49E8` is a behaviour shim, not a log:
    firing the real `OS_EVENT_AI` when a buffer drains lets it be deleted.
    See `history-archive:docs/history/handoffs/HANDOFF-2026-10-01-session117.md` for the full list.
13. **The browser build's gamepad slots.** Closed on 2026-11-01: the browser build was removed.
14. **RESOLUTION and MSAA artifacts.** Both rows work and reach RT64, and a
    player run with them showed visual artifacts that were not characterised
    (developer, session 121). They ship behind `OGRE_DISPLAY_EXPERIMENTS=1`
    (hidden rows, values not applied) until someone reproduces the artifacts
    with a capture and fixes them in RT64 or in the port's configuration. The
    first step is a captured A/B of a named screen at native vs `2x`, and at
    MSAA off vs `4x`, since both settings are one `GraphicsConfig` field.
    See `history-archive:docs/history/handoffs/HANDOFF-2026-10-01-session121.md`.
15. **A mod's own config file is rejected unless it carries `mod_id`, so every
    mod option read its default. Closed (session 123).**
    `parse_mod_config_storage`
    (`tools/N64ModernRuntime/librecomp/src/mod_manifest.cpp:815`) returned false
    when the JSON had no `mod_id` key, and `Config::load_config` then leaves every
    option on its default with no error and no log. The loader now accepts a
    missing key, because the file's own name is the mod id, and still rejects a
    key that names a different mod. A/B on one file: unpatched
    `[mod] start verb mode shops 0 0 0 0`, patched `2 1 1 0` /
    `[mod] start chance seed 55 12345`. Session 122's `recomp_log` export was also
    in no patch, so no release carried it and every mod that imports `recomp_log`
    was refused by a shipped build; both changes are now in
    `patches/n64modernruntime-ob64.patch` and `tools/patchcheck.py` passes. See
    `history-archive:docs/history/handoffs/HANDOFF-2026-10-03-session123.md` §1.
16. **The Item Randomizer.** Closed on 2026-11-01: the mod was removed with the other inherited code mods (`docs/anotherworld/DECISIONS.md`). The research on the grant routine `func_ovlN_801DC740` and the pickup table is in `history-archive:docs/history/handoffs/HANDOFF-2026-10-03-session123.md` and `docs/notes/item-equipped.md`.
17. **The Tutorial (scene `0x17`) crashes on entry with a SIGBUS; the Item
    Randomizer is in no frame of it.** Four macOS crash reports on 2026-10-03 (16:59:21,
    17:38:38, 17:46:34, 17:46:57) are one fault: `EXC_BAD_ACCESS` /
    `KERN_PROTECTION_FAILURE` on N64 Thread 3, frame 1 at `func_80075BC0`
    (the scene dispatcher) and frame 2 at `func_80071EB0` (the streamed-overlay
    loader). Frame 0 has no symbol and the fault address differs per run
    (`0x199ab25f0`, `0x1a4e015f0`, `0x1a4c195f0`, `0x19dd635f0`), so the crash is
    a data address the guest computed, not a fixed one. Every address is above
    `0xE0000000`, which no guest mapping in this port reaches, so the guest is
    dereferencing a value that is neither a guest pointer nor a guest code
    address. The run log ends right after the tutorial's two bank records load
    (`rom=0x069920` -> `0x80197B90`, `rom=0x1BA020` -> `0x80220F60`) and a save
    read. The mod is in no frame, it only writes guest RAM and never installs a
    pointer the game follows, and an unknown guest code address resolves to a stub
    rather than a jump. The earliest report (16:59) also predates session 124's
    change to the mod. That is a mechanism argument and not a measurement: the run
    that would have measured it did not have the mod off. The A/B that was meant to show this did
    not: `run-with-save.sh` copies `build-app/mods.json` over any `mods.json`
    written into the preference directory, so the mod was loaded in both runs;
    the veto has to move `<pref>/mods/item-randomizer.nrm` aside. Open: which
    instruction in `func_80075BC0` computes the bad address, and whether the
    tutorial is reachable at all from a suspend save rather than through its own
    flow (`AGENTS.md` §9). **It is not reproducible now**: the developer reports
    the tutorial runs fine, and neither binary changed, so the rebuild cannot be
    the reason (`build-app/ogrebattle64` is still `Oct 3 12:06:40`, 27277408
    bytes, the binary the reports came from; the mod's `.nrm` is unchanged at
    8591 bytes). The leading candidate is the save sandbox: a crash mid-write
    leaves the sandbox copy inconsistent with the imported file, and every report
    follows another run. If it returns, capture the save at the crash and read
    `D_800AF028`, the resolved descriptor and its hook words from the frame
    before. See `docs/notes/tutorial-crash.md` §5.
18. **The speed badge is not drawn in fullscreen, and it hides itself when the
    pointer is over it (session 137).** The badge is a small borderless
    always-on-top SDL window over the corner of the game window, because RT64
    owns the game's swap chain and has no UI pass. Fullscreen is a display space
    of its own, so the window has nowhere to draw; the speed still changes and
    only the readout is missing. The window also takes the mouse focus while the
    pointer is inside its 56x47 rect, because SDL2's `SDL_HitTestResult` has no
    click-through value (the bundled headers have `NORMAL`, `DRAGGABLE` and the
    resize edges, and cocoa implements only `DRAGGABLE`), so the badge hides on
    mouse focus rather than swallowing a click or holding the map and mission
    cursors' motion events. A badge that survives both needs a draw inside RT64's
    frame, or a texture and display list the port submits itself.

Parked, not defects:

- Audio above 1× speed is untested; `OGRE_SPEED` runs always had audio off
  (`history-archive:docs/history/handoffs/HANDOFF-2026-09-20-session93.md` §8).
- The `ESC` overlay does not pause the game (the developer's choice), and gamepad
  rebinding is code-verified only because this machine has no gamepad
  (`history-archive:docs/history/handoffs/HANDOFF-2026-09-20-session92.md` §9).
- `OGRE_NO_AUDIO=1` is the workaround on hosts where `SDL_OpenAudioDevice`
  blocks.
- `0x801936A9`, the community's Chaos Frame address, is in the same saved flags
  block but has no static read or write in this ROM
  (`history-archive:docs/history/handoffs/HANDOFF-2026-09-21-session95.md` §5).

## Roadmap

1. **Phase 0 — toolchain & ROM** ✅
2. **Phase 1 — disassembly & ELF** ✅
3. **Phase 2 — recompile the main segment** ✅
4. **Phase 3 — first boot** ✅ (sessions 3-6)
5. **Phase 4 — overlays & streamed code** ✅ — 44 records in 34 units (A..AH);
   no un-recompiled module on a reachable path (session 85)
6. **Phase 5 — assets** ✅ — LZ, njpeg backgrounds, textures; 99.05% of the code
   span recompiled; `tools/recompcov.py` measures coverage
7. **Phase 6 — saves, audio, QoL** — the SRAM battery and the Controller Pak
   device work, the audio microcode runs, and settings, controls and the overlay
   are done. Open: the Controller Pak copy flow, audio above 1×
8. **Phase 7 — modding framework & packaging** ✅ — mod system with a shipped
   example, `make dist` packages, GitHub releases
9. **Phase 8 — modernization** (in progress, from 2026-10-29) — the game plays
   like a modern PC/console release: modern menus and controls, HD graphics,
   and quality-of-life layers on top of the game's own rules. Status per track:

   | track | built and played | built, not yet played | next |
   |---|---|---|---|
   | **Organize screen (F3, RmlUi)** | formation board, moves, leader, carry, add/remove soldiers, new unit, strategy picker, portraits, sprites, soldier EXP | Change Class (the game's checks and apply), Discharge, Rename, Replenish, Exchange, Equipment/Items/Armory (equip, sell, buy sets), Unequip All, Class List, attack details | Legion commands (routines mapped in `docs/anotherworld/design-organize-screen.md` §4d; needs a legion capture), Item Use, unit Sort, damage numbers, replacing the game's Organize entry |
   | **Esc menu (RmlUi)** | tabs, clicks, pause at the frame gate (presents kept alive while paused) | — | — |
   | **Controls** | modern action map (keyboard/mouse/pad), BUTTON PROMPTS (Xbox/PlayStation/Nintendo) | — | rebinding UI polish |
   | **Graphics** | resolution native..8K labelled by aspect, ASPECT RATIO 4:3/16:9/16:10/21:9, anti-aliasing, HD texture pipeline (`tools/hd_textures.py` → `texture-packs/hd.rtz`, mipmapped DDS), HD text with switchable fonts (TEXT FONT) | — | widescreen beyond the mission field, 60 fps beyond the mission field, the high-resolution glitches (need screenshots), a free font to ship, building the HD pack on the player's machine (the pack is made from game art and cannot ship) |
   | **Gameplay QoL** | SKIP INTRO, Honors removed (no alignment requirement for class changes), LARGE UNITS ONE SQUARE | — | — |
   | **Field controls overhaul** | — | — | the 33-step plan in `docs/anotherworld/design-field-controls.md`: direct orders, multi-select (drag box / growing circle), army panel with alignment, idle cycling, alerts, waypoints, patrols, camera; starts with one capture session |
   | **Android** | APK builds; personal build with the ROM inside (`OGRE_BUNDLE_ROM=1`) | start-screen fix (no minimise on focus loss, taps only on rows) | play test on the phone |
   | **War Director v2** | — | — | the plan in `docs/anotherworld/design-war-director-v2.md`: war seed, story-order gate, intensity and relax phases, orders and opportunities, tiered rewards, feats; starts by playing the built deployment |
10. **Phase 9 — ecosystem: both revisions, LordlyCaliber content, modding API**
    (planned 2026-10-29) — the port runs US revision 0 and revision 1, loads
    content authored in [LordlyCaliber](https://github.com/rempred/LordlyCaliber)
    at runtime, and exposes events, game-state views and data packs to mods.
    The port does not build its own data editor. Plan and phases A-H:
    `docs/anotherworld/design-ecosystem.md`; API: `docs/anotherworld/design-modding-platform.md`; new
    content: `docs/anotherworld/design-content-expansion.md`.

## Toolchain

| Tool | Purpose | Location |
|---|---|---|
| splat 0.50 (`splat64[mips]`) | ROM splitting / disassembly | `tools/venv` |
| spimdisasm | MIPS disassembler (used by splat) | via pip |
| MIPS binutils (`mips-linux-gnu-*`) | assemble `.s` → `.o`, link ELF | system package, or `tools/mips-binutils/bin` on Windows |
| N64Recomp (patched) | MIPS → C recompilation | `tools/N64Recomp` (vendored clone) |
| N64ModernRuntime (patched) | recompiled game runtime (libultra shim, renderer) | `tools/N64ModernRuntime` (vendored clone) |
| RT64 (patched) | renderer | `tools/RT64` (git submodule) |

The project's changes to these tools are the patches in `patches/`.
`CONTRIBUTING.md` has the pinned commits, the apply order and the build
commands for Windows, Linux, macOS and Android. `make regenerate` is the one
supported order for the generated code; `make recomp` refuses to run when
`app/src/bank_funcs.inc` is missing, because its cross-bank dispatch would then
be skipped and calls into swappable RAM would bind to the wrong bank
(`docs/guides/build.md` → "Regenerating the recompiled code").

## Key technical findings

- **Byte order.** The conventional dump is a 16-bit byte-swapped `.n64`.
  `tools/convert_rom.py` converts it to big-endian `.z64`. The app accepts
  `.z64`, `.n64` and `.v64`.
- **Header layout.** Official Nintendo layout: entry point at offset `0x08`
  (`0x80070C00`), internal name at `0x20` (`OgreBattle64`), country at `0x3E`.
- **Memory map.** ROM `0x1000` ↔ vram `0x80070C00` (entry segment, `0x60` bytes);
  main segment ROM `0x1060` ↔ `0x80070C60`, ending at BSS start `0x800AEDB0`;
  BSS `0x800AEDB0..0x800E9C20`; stack `0x800C6D60`; `main` at `0x8007F880`.
- **libultra** is bridged by name: `config/symbols/symbol_addrs.txt` names the
  libultra functions so the runtime's native `osXxx_recomp` services replace
  OB64's verbatim copy. `history-archive:docs/history/LIBULTRA-BRIDGING.md` records the identification
  method and the address→name table.
- **Streamed code is mapped and compiled.** Each streamed overlay is plain
  linked MIPS code and data DMA'd to a fixed RAM address. Records that share a
  RAM range are separate bank units, so a call into the range compiles as
  `LOOKUP_FUNC` and the runtime's DMA-driven bank map picks the resident module.
  Compiling a swappable range into the unit that calls into it binds the call to
  one bank's bodies at build time and runs them with the wrong frame contract
  (sessions 41/45/55). AGENTS.md rule 4 holds the details.
- **Scene graph.** `func_80075BC0` looks up `D_800AF028[scene_id]()`; there are
  exactly 25 scene types, 39 transitions and a 1693-entry scripted step table.
  `tools/scenemap.py` extracts all of it from the ROM in about 3 seconds.
  `docs/guides/recomp-facts.md` holds the addresses.
- **Display lists are not run through recompiled microcode.** The runtime routes
  gfx tasks to RT64, which parses them with its own GBI interpreters. RSPRecomp
  compiles the game's njpeg and audio microcodes only.
- **The world map's cursor is a screen-space sprite, and the mouse drives it.**
  The map module keeps `state+0x52`/`+0x54` as screen pixels and clamps them to
  x `0x28..0x110`, y `0x28..0xC0` (`func_ovlM_8019AF0C`), and the scene hook's
  cursor draw (`func_ovlM_801A103C`) reads the pair at
  `0x801A1C48`/`0x801A1C4C`. `app/src/map_cursor.cpp` writes the pointer's pixel
  into that pair at the draw site (`config/hooks.toml` inserts the call and
  `make bank-recomp` re-applies it, because bank code is outside the mod hook
  map). A left click ORs the N64 `A` bit into controller 0 in `get_input`, and a
  pointer within 24 pixels of a play-area bound holds the matching D-pad bit,
  which scrolls the view. All three are developer-confirmed. See
  `docs/guides/diagnostics.md` → "The world map's mouse cursor" and
  `history-archive:docs/history/DECISIONS.md` (2026-10-10).
- **The mission field's cursor is also a screen-space pair, and it is digital.**
  `state = *(u32*)0x801AB870 = 0x800E91D0`, a fixed BSS address that the mission's
  modules each hold as a pointer constant (session 115 read it through
  `*(0x80213690)` and called it a heap pointer). `state+0x08`/`+0x0C` are f32
  framebuffer pixels; the mover `func_ovlN_801BE4E0` reads the **held** button
  word `*(0x800E8108)` and the profile's D-pad masks at `*(0x801F0CC0)`,
  integrates a per-frame delta and clamps to x 40..280, y 30..210. There is no
  analog path in any mission bank: the map reads the stick at
  `*(0x800C47F0)+2/+3`, the mission does not. `app/src/mission_cursor.cpp` writes
  the pointer's pixel at the generated label `L_801BEC04`, after both clamps and
  before the tail that reverts the cursor when no unit is focused, and stands
  down while a direction is held, adopting the game's position so the release
  does not jump. **The mission field's camera pans with the cursor, and not with
  a held direction, so the pointer scrolls the view by holding the direction the
  game's own mover reads.** The mover `func_ovlN_801BE4E0` converts the held
  D-pad into the delta bytes `state+0x06`/`+0x07`, and `func_ovlN_801BECF0`
  (called right after it at `0x801AF130`) adds `delta/128 * 8.5 * table[e]` to
  the eye x and the look-at x of the camera at `*(u32*)0x8018F5AC`
  (`0x801BEE04`-`0x801BEE1C`), and the same for z (`0x801BEE9C`-`0x801BEF5C`),
  clamped to the map's world rect at `0x801F0DD8`; it runs only while
  `*(0x801F3630)` is set, which the mover sets only when the cursor pair changed
  on that call (`0x801BEC3C`-`0x801BEC70`). The camera is `+0x14` the eye (its
  height at `+0x18`), `+0x20` the look-at and `+0x2C` the up vector, which
  `func_ovlA_801AAE30` builds the screen-to-world basis from; `lw $v0,-2644($v0)`
  with `lui $v0,0x8019` is `0x8018F5AC`, and not `0x8019F5AC`.
  `app/src/mission_cursor.cpp` holds the matching N64 D-pad bit while the pointer
  is within `OGRE_MISSION_CURSOR_EDGE` (24) px of a play-area bound, and
  `get_input` ORs it into controller 0; the four masks the mover compares are this mode's
  profile's own (`*(0x801F0CC0)`), which are the N64 bits. See
  `docs/guides/diagnostics.md` → "The mission field's mouse cursor" and
  `history-archive:docs/history/DECISIONS.md` (2026-10-10, sessions 128 and 129).

## Working documents

The documentation index is [docs/README.md](docs/README.md).
