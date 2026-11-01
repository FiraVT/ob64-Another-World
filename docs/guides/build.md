# Building and running the app

How to regenerate the recompiled code, build the app, run it, and read a crash
report. Contributor setup per platform is in
[../../CONTRIBUTING.md](../../CONTRIBUTING.md).

The app (`app/`) compiles the recompiled game code against `N64ModernRuntime`
and links the renderer (RT64), then boots the game on your ROM.

## Prerequisites

- CMake ≥ 3.20 and a C++20 compiler: clang-cl on Windows, clang or gcc on
  Linux and macOS.
- SDL2.
- splat, MIPS binutils, the N64Recomp and N64ModernRuntime vendored clones,
  and the RT64 submodule.
- Your own big-endian `.z64` dump at `assets/ogre64.z64`.

[../../CONTRIBUTING.md](../../CONTRIBUTING.md) lists the packages and commands
for each platform.

## Regenerating the recompiled code (start here on a fresh clone)

**A fresh clone cannot build the app.** The recompiled C is *generated*, not
committed: `RecompiledFuncs/`, 34 `Bank*Funcs/`, `RspFuncs/` and
`app/src/bank_funcs.inc` are all gitignored, because they are the recompiler's
output from a ROM that cannot be redistributed. You need your own ROM and one
command:

```sh
make regenerate
```

That runs the whole pipeline in the one order that works:

| step | what it produces |
|---|---|
| `make resplit` | `asm/`, `assets/*.bin`, `ogrebattle64.ld`, `config/symbols/undefined_*_auto.txt` (splat) |
| `make` | `build/ogrebattle64.elf` (assemble + link the MIPS ELF) |
| `make bank-recomp` | `Bank*Funcs/`, `app/src/bank_funcs.inc` (the 34 streamed-bank units) |
| `make recomp` | `RecompiledFuncs/` (N64Recomp + the cross-bank dispatch) |
| `make rsp-recomp` | `RspFuncs/` (the njpeg and audio microcode) |

Verified end to end from a clean checkout of the tracked tree: the app builds
and boots, and `RecompiledFuncs/` comes out **byte-identical** to the tree the
project has been testing.

**The order near the end is load-bearing.** `make recomp`'s cross-bank dispatch
(`tools/cross_bank.py dispatch --only …`) rewrites 16 call sites that N64Recomp
otherwise binds to the wrong bank, but it can only see bank records that
`bank_funcs.inc` already lists. Recompile before regenerating the banks and the
dispatch is skipped: the build succeeds and quietly runs wrong-bank code — the
failure class of sessions 41/45/55. `make recomp` now refuses to run in that
state rather than let it through.

Two things worth knowing:

- **`make resplit` leaves tracked files modified.** The committed `asm/*.s` are
  the post-`fix-labels` form, and a re-split writes the raw splat form;
  `tools/fix_cross_overlay_labels.sh` (a prerequisite of the object rules) then
  re-applies the patch on every build, so `git status` shows `asm/1CE040.s` and
  `asm/40E80.s` as dirty with identical content. Do not commit or revert those
  files unless you are deliberately changing the label patch.
- **Regenerated bank units differ from an older tree in one cosmetic way**: the
  `recomp_trace_return(rdram, N)` depth constants in 26 files under
  `Bank{B..L}Funcs/`. They feed the shadow call-chain diagnostic only — a
  diff with those lines filtered out is empty — so a regenerated tree is
  functionally identical. Expect the difference if you diff generated output
  against a tree someone has not regenerated since the recompiler's trace
  numbering changed.

## Configure & build

Once the code above exists:

```sh
# one-time: initialize the RT64 submodule (it is the only submodule; the
# N64Recomp and N64ModernRuntime trees are gitignored vendored clones)
git submodule update --init --recursive

# apply the SDL >= 2.0.22 compatibility patch to RT64's plume submodule
# (needed on systems with older SDL2, e.g. Ubuntu 22.04's 2.0.20)
git -C tools/RT64/src/contrib/plume apply ../../../../rt64-plume-sdl.patch

# apply the runtime patches to their vendored clones
git -C tools/N64ModernRuntime apply ../../patches/n64modernruntime-ob64.patch
git -C tools/N64ModernRuntime apply ../../patches/n64modernruntime-anotherworld.patch
git -C tools/N64ModernRuntime/N64Recomp apply ../../../patches/n64modernruntime-n64recomp.patch

# apply the OGRE diagnostics (present traces, GPU readback capture, presenter
# knobs). patches/rt64-plume-ob64.patch is the texture -> buffer readback support in
# plume's Metal backend.
git -C tools/RT64 apply ../../patches/rt64-ob64.patch
git -C tools/RT64 apply ../../patches/rt64-anotherworld.patch
git -C tools/RT64/src/contrib/plume apply ../../../../../patches/rt64-plume-ob64.patch

# build the app
cmake -S app -B build-app -DCMAKE_BUILD_TYPE=Release
cmake --build build-app -j
```

### Third-party patches

The four vendored trees carry the project's local changes as uncommitted work:
`tools/N64ModernRuntime` and `tools/RT64` are checked out at a pinned upstream
commit and left dirty. The **tracked** copy of those changes is the patch files
in `patches/`, and the two build paths use different sources:

| path | source of the changes |
|---|---|
| a developer's machine | the dirty tree; the patches were applied once at setup |
| a GitHub-hosted release runner (`vars.OGRE_DATA_REPO` set) | the patch files, applied to a pristine checkout of the same commit |

So a patch that no longer matches the tree ships old code, and the build stays
green. That happened: `patches/rt64-ob64.patch` still read
`getenv("OGRE_CAPTURE_PRESENT")` after the tree switched to
`ogre_present_capture_path()`, and `patches/rt64-plume-ob64.patch` had no
`plume_d3d12.cpp` hunk at all. Neither the presented-frame capture fix nor the
D3D12 null-texture guard was in v0.5.0 or v0.5.1, which were built on hosted
runners (session 116).

`tools/patchcheck.py` (or `make patch-check`) compares each patch against its
tree and exits 1 on any difference. It needs no worktree: for every file a patch
touches, the repository's own `git diff HEAD -- <file>` must equal the patch's
block for that file, so equality proves the patch reproduces the tree. A second
pass fails when the tree changes a file that no patch carries. `make patch-fix`
rewrites the stale patches; review `git diff patches/` before committing them.
`tools/release-build.sh` runs the check before packaging, so a developer cannot
package stale patches. An SDL2 older than 2.0.22 also needs
`tools/rt64-plume-sdl.patch`; it has no post-image in the tree and is the one
patch the check does not cover.

The executable is written to `build-app/ogrebattle64`. If it fails with
"the recompiled game code is missing", you skipped the regeneration section
above; `tools/release-build.sh` prints the same list.

`make app` is the Makefile wrapper for the same build, and it does **not** use
`build-app`: `DIST_STATIC_SDL` defaults to `1`, so `make app` builds
`build-dist/ogrebattle64` with a static SDL2. Use `make app DIST_STATIC_SDL=0`,
or `cmake --build build-app --target ogrebattle64 -j`, to refresh the
dynamically linked `build-app` binary. A plain `make app` followed by running
`./build-app/ogrebattle64` tests the previous build; compare the two files'
timestamps before interpreting a run.

### Build variants

| Variant | Configure command | Renderer |
|---|---|---|
| Native + RT64 (default) | `cmake -S app -B build-app -DCMAKE_BUILD_TYPE=Release` | RT64 (Vulkan/Metal/D3D12) |
| Native + null renderer | `cmake -S app -B build-null -DCMAKE_BUILD_TYPE=Release -DOGRE_USE_RT64=OFF` | `null_renderer.cpp` (no GPU, no RT64) |

The null-renderer variant is useful for bring-up/CI on machines without a
working GPU driver. The browser (WebAssembly) build was removed on 2026-11-01: a browser version is not a goal (docs/anotherworld/DECISIONS.md). Its history is in git and in the session handoffs.

## Running

```sh
# from the repo root (the app resolves the ROM path from the config path)
./build-app/ogrebattle64 [path-to-rom.z64]
```

The app starts on its own **start screen** when it has no ROM to boot: the
`OB64: ANOTHER WORLD` title over a tabbed panel (**MAIN**, **MODS**,
**CONTROLS**, **SETTINGS**, **DEBUG**). The MAIN tab carries the ROM row,
which reads `[ ] No ROM` and `PRESS SPACE TO CHOOSE A ROM, OR DROP IT IN A
WINDOW`. With no ROM loaded that row is the selected one, so `SPACE` (or a click)
opens a native file picker; a ROM can also be dropped onto the window. A ROM
placed in the app's folder (or `<app>/roms/`) is found automatically, so "put the
ROM next to the executable and launch" works with no click at all.

The ROM is validated by XXH3 hash and the runtime stores a copy in the config
directory, so **later launches skip the start screen and boot straight into the
game** — unless a mod is installed, in which case the start screen appears first
so the mods can be turned on or off (see "Mods" below). Holding Shift while the
game starts (Windows) or `OGRE_LAUNCHER=1` forces the start screen even when a
ROM is available; `OGRE_ROM=<path>` names one
without an argument.

**The config directory is the executable's own directory** (`SDL_GetBasePath`),
not the per-user preference dir, so a distributed build is self-contained: the
battery save lands at `saves/<game id>.bin` right beside the executable, and so
do `mods/`. Inside the macOS bundle `SDL_GetBasePath` returns the folder that
holds the `.app` (`SDL_FILESYSTEM_BASE_DIR_TYPE=parent`), which is that same
folder. `OGRE_PREF_DIR=<dir>` still overrides it (the scripted harnesses
use that), and a read-only install location falls back to the platform
preference dir automatically. The boot log prints the resolved path.

## Crash reports — `error.log`

`app/src/crash_log.cpp` is what replaces the console window a distributed build
no longer has. `crash_log::install()` runs at the top of `main()` and:

* replaces fd 1 and fd 2 with the write ends of two pipes, each drained by a
  thread into its own 128 KiB ring while every byte is forwarded to the real
  descriptor. A terminal run, `... > run.log 2>&1` and `2> run.log` all behave
  as before. `stdout` is put back on line buffering with a **static buffer and a
  non-zero size**: the MSVC CRT treats `setvbuf(stream, nullptr, _IOLBF, 0)` as an
  invalid parameter, and its invalid-parameter handler fast-fails the process
  (`0xC0000409`) before any handler can run. glibc and macOS accept a zero size,
  so this only ever showed on Windows — it is what made the 0.2.0 Windows build
  do nothing at all, and why it wrote no `error.log` (session 94). The handlers
  are installed before the capture for the same reason;
* installs the fatal handlers: `SIGSEGV`/`SIGBUS`/`SIGABRT`/`SIGFPE`/`SIGILL`
  through `sigaction` on POSIX, the same through `signal` plus
  `SetUnhandledExceptionFilter` on Windows, and `std::set_terminate` on both.

A fatal event writes `<config dir>/error.log`, which is the executable's own
directory unless that one is read-only (`resolve_pref_dir`). The file holds the
reason, the **faulting instruction and the address it touched** (each with its
host module and offset on Windows and macOS; an access violation also says
whether the access was a read, a write or an execute), the RDRAM base, the N64
thread, the last recompiled function per thread, the last 200 stderr lines and
the last 100 stdout lines (the boot log, `[bank]` load lines, any
`[overlays] streamed function stub called @ ...` line), then the runtime's guest
call chain. The player attaches it to a GitHub issue.

The handler's own output uses only `open`/`write`/`close` and an atomic ring
length; the runtime's dump helpers are `printf`-based and run after the ring has
been written. The handler never calls `fflush`: a fault can arrive while the
faulting thread holds a stdio stream lock, and flushing from the handler
re-enters that lock and hangs the process instead of killing it (session 94).
The `printf`-based dumps run only after a non-blocking `ftrylockfile` probe shows
both stream locks free, and the report says when they were skipped. **Windows has
no `ftrylockfile`, so there the handler uses no stdio at all**: it writes the
fault fields, the N64 thread and the last function per thread with raw writes,
and skips the runtime's printf-based call-chain dump. Only the
first report of a fatal event is written, so a `std::terminate` that then aborts
does not overwrite it. `OGRE_DUMP_RDRAM=<path>` still adds the whole 8 MiB image,
on the crash path as on the exit path. `crash_log::flush()` restores the
descriptors and joins the threads; `atexit` calls it, and the bounded-run and
window-close `_Exit` paths call it explicitly.

`OGRE_CRASH_TEST=<segv|bus|abrt|fpe|ill>` raises that signal immediately after
the logger is armed, so a packaged build's `error.log` can be checked without
provoking a real crash.

`SDL_ShowSimpleMessageBox` errors from the runtime (its `message_box` channel)
also write `error.log`, for the same reason: a player who sees the box has no
console to copy.

`report_boot_failure()` (`app/src/sdl_platform.cpp`) is the sibling for a failure
that is not a crash: it writes `error.log` and shows the reason in a message box.
The SDL init, the game window, and the start screen's window and renderer all
call it, because a silent `EXIT_FAILURE` in a console-less build is
indistinguishable from "the program does nothing" (session 94). `init_sdl`
requires only video and events: `SDL_Init` fails the whole call when any
requested subsystem fails, and `SDL_INIT_AUDIO` can fail on a host with no usable
output device, which used to stop the app before its first window. Audio and game
controllers are initialised separately and may be absent; the game runs silently
without them.

## Closing the window

The window's close button makes SDL2 post `SDL_QUIT` (the last window's
`SDL_WINDOWEVENT_CLOSE` posts it too; `src/events/SDL_windowevents.c`).
`update_gfx` writes any requested dumps and calls `ultramodern::quit()`.

`quit()` stops the runtime's own threads, but the game's threads are host threads
running recompiled code and nothing joins them. Two things follow, and both are
needed for the window to close instead of hanging:

* `librecomp/src/recomp.cpp` no longer frees RDRAM when `exited` is set (the
  change is in `patches/n64modernruntime-ob64.patch`). It used to `munmap` the 8 MiB
  image at the end of `recomp::start`, and a game thread that touched it
  afterwards faulted — session 94's window-close freeze, whose fault address was
  inside the image. The process is about to exit, so the OS reclaims the mapping.
* `main.cpp` runs the SDL teardown and then `_Exit(EXIT_SUCCESS)` instead of
  returning. `return` runs the C++ static destructors while those threads are
  live, which is the same race from the other side. `crash_log::flush()` runs
  first, because `_Exit` skips the atexit handler.

Teardown takes about half a second after the close, exits 0, and writes no
`error.log` (a normal close is not a crash). The battery save is safe: the
runtime joins its saving thread inside `recomp::start`, before either change.

## Config directory

The executable's own directory by default (`SDL_GetBasePath`), so saves and
mods live beside the app. `OGRE_PREF_DIR` overrides it; a read-only install
location falls back to the per-user preference dir:

- macOS: `~/Library/Application Support/ogrebattle64/`
- Linux: `~/.config/ogrebattle64/`
- Windows: `%APPDATA%\ogrebattle64\`

## Troubleshooting

### RT64 submodules won't check out ("Unable to find current revision")

RT64 pins contrib submodules (imgui, xxHash) to commits that aren't always
fetchable by plain `git submodule update`. Fix by fetching the pinned commit
explicitly:

```sh
git -C tools/RT64/src/contrib/imgui fetch origin <pinned-sha>
git -C tools/RT64/src/contrib/imgui checkout <pinned-sha>
```

### DXC shader compiler aborts ("Library not loaded: libz.dylib")

The bundled `dxc-macos` needs `libz.dylib` next to it (its `DYLD_LIBRARY_PATH`
includes `tools/RT64/src/contrib/dxc/lib/x64`). Install zlib and symlink it in:

```sh
brew install zlib
ln -sf "$(brew --prefix zlib)/lib/libz.dylib" tools/RT64/src/contrib/dxc/lib/x64/libz.dylib
```

### `metal` tool missing ("missing Metal Toolchain")

RT64's macOS shader build calls `xcrun -sdk macosx metal`. Newer Xcode releases
require the separate Metal toolchain component:

```sh
xcodebuild -downloadComponent MetalToolchain
```

### RT64 build still fails after changes

The RT64 CMake cache is sticky; after touching `tools/RT64/CMakeLists.txt` or
fixing toolchain issues, delete the `build-app` directory and reconfigure.
