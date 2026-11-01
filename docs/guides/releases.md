# Packages and releases

`make dist`, the smoke test, and the GitHub release workflow.

## Distribution — `make dist`

```sh
make dist        # -> dist/ogre-battle-64-recomp/  (the app + README + SDL2 license)
make dist-zip    # -> dist/ogre-battle-64-recomp-<platform>.zip
```

**The package shows no console window.** macOS wraps the executable in
`Ogre Battle 64.app` (`packaging/macos-Info.plist`), so Finder launches it
without opening Terminal, and Windows links `ogrebattle64.exe` as a GUI-subsystem
binary (`WIN32_EXECUTABLE` in `app/CMakeLists.txt`; `app/src/main.cpp` supplies
the `WinMain` forwarder that subsystem's CRT entry point needs, and attaches to
the parent's console with `AttachConsole` when one exists, so running the `.exe`
from a terminal still shows the log without ever creating a window). The app's
`[boot]`/`[bank]` log therefore has no console to reach for a double-clicked
launch, so a crash writes `error.log` next to the save file — see "Crash
reports" below.

The macOS bundle carries `SDL_FILESYSTEM_BASE_DIR_TYPE=parent`. `SDL_GetBasePath`
then returns the folder that holds the `.app`, which is the same folder the
README describes, so the ROM, `saves/`, `mods/`, `controls.cfg`, `settings.cfg`
and `error.log` all stay beside the bundle rather than inside it. The bundle is
ad-hoc signed at the end of the recipe.

**The package is a single file: SDL2 is linked statically.** Everything else is
a system framework or already static, so the executable has no non-system
dynamic dependency at all:

```sh
otool -L "dist/ogre-battle-64-recomp/Ogre Battle 64.app/Contents/MacOS/ogrebattle64" \
  | grep -v "System/Library\|/usr/lib"   # prints nothing
```

Static SDL2 needs the *real* SDL2 sources: Homebrew's `sdl2` formula on macOS is
**`sdl2-compat`**, a shim that `dlopen`s `libSDL3.0.dylib` and ships no static
library, so `make dist` first runs `make sdl2-static`, which fetches a pinned
SDL2 (`2.32.10`; API/ABI-compatible with the 2.32.70 shim, so either header set
works) into the gitignored `tools/SDL2-static/` and builds it with
`-DSDL_SHARED=OFF -DSDL_STATIC=ON`. That happens once and is reused. `dist`
builds the app in its own `build-dist/` directory against that prefix, and
copies SDL2's zlib license into the package as `SDL2-LICENSE.txt`.

On Linux the static SDL2 also needs the audio development headers:
`libasound2-dev`, `libpulse-dev` and `libpipewire-0.3-dev` (the first two are
dependencies of `libsdl2-dev`). SDL2's CMake **drops an audio backend whose
headers are missing without failing** — the configure summary prints
`SDL_ALSA (Wanted: ON): OFF` while the cache variable still reads ON — and the
OSS driver's header is the one that is always present on Linux. The result is an
SDL2 whose only output driver is `/dev/dsp`, which no PipeWire or PulseAudio
machine has: `SDL_InitSubSystem(SDL_INIT_AUDIO)` fails with
`dsp: No such audio device`, the app prints `[SDL] audio unavailable, running
silently`, and the game has no sound. The 0.3.0 Linux release was built on such
a host and shipped that binary (issue #8). `make sdl2-static` therefore verifies
the archive with `tools/check-sdl2-audio.sh` and rebuilds from scratch when a
cached copy fails, and `tools/smoke-dist.sh` checks the packaged binary for an
ALSA, PulseAudio or PipeWire backend. The system SDL2 a Linux distribution ships
always has them, which is why `DIST_STATIC_SDL=0` (system `libsdl2-dev`) is
unaffected.

The static link pulls in the frameworks SDL2 uses (Cocoa, IOKit, CoreAudio,
AudioToolbox, AVFoundation, Carbon, CoreVideo, Foundation, plus weakly
GameController/Metal/QuartzCore/CoreHaptics), which is why the package still
runs anywhere those system frameworks exist — macOS only.

`DIST_STATIC_SDL=0 make dist` skips all of that and bundles the shared SDL2
instead (two files, seconds instead of a minute) — useful for a quick local
package.

`make dist` needs **no ROM and no recompiler run** — but it does need the
recompiled code to already be present on the machine. `RecompiledFuncs/`,
`Bank*Funcs/`, `RspFuncs/` and `app/src/bank_funcs.inc` are **gitignored and
generated**, so a fresh clone has none of them and `make dist` fails with the
"recompiled game code is missing" message from `tools/release-build.sh` until
`make && make recomp && make bank-recomp` has been run once against a ROM on
that machine. Release builds therefore run on a machine with the ROM, unless the
generated code is supplied from the private data repository — see "Releases"
below.

On Windows the executable links `SDL2-static` and the static CRT
(`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, set for every target in
`app/CMakeLists.txt`), and the package carries the MSVC runtime DLLs beside the
`.exe`. That is not optional: RT64 statically imports `dxcompiler.dll`/`dxil.dll`,
and those two Microsoft binaries import `MSVCP140.dll`/`VCRUNTIME140.dll`, so a
machine without the Visual C++ redistributable cannot start the process at all.
The 0.1.0 and 0.2.0 packages shipped neither, which `tools/pe_imports.py` now
catches on any machine — including a runner that has the redistributable
installed and where a launch would therefore succeed. `make sdl2-static` builds
SDL2 with the same runtime-library setting, and the `dist` recipe fails rather
than shipping a Windows package without the DLLs.

A first launch of the package in an empty folder is the user-facing acceptance
test: the black start screen appears, clicking/dropping a ROM boots the game,
and `saves/ogrebattle64-us-rev1.bin` is written next to the executable.

## Smoke test — `make smoke`

```sh
make dist && make smoke        # or: tools/smoke-dist.sh [dist-dir]
```

`tools/smoke-dist.sh` runs on the machine that built the package and checks
three things. The release workflow runs the same script on every runner image
between "Build and package" and "Collect the archive", so a package that cannot
start fails the release instead of reaching a player.

1. The app binary and `README.txt` exist, and on Windows the companion DLLs
   (`dxcompiler.dll`, `dxil.dll`, `msvcp140.dll`, `vcruntime140.dll`,
   `vcruntime140_1.dll`) do too.
2. The package is self-contained. On Windows, `tools/pe_imports.py` parses every
   shipped `.exe`/`.dll` and requires each imported DLL to be either shipped
   beside them or a Windows system DLL. This is the check a launch cannot make on
   a runner: the image has the Visual C++ redistributable installed, so a package
   that is missing `msvcp140.dll` still starts there (session 94 shipped exactly
   that package). macOS checks `otool -L` for non-system dependencies, Linux
   checks `ldd` for unresolved libraries and greps the binary for an ALSA,
   PulseAudio or PipeWire SDL backend. SDL2 is linked statically, so its audio
   backends are compiled in; the OSS-only build that a host without the audio
   headers produces cannot open a device on any PipeWire or PulseAudio machine,
   and the 0.3.0 Linux package shipped exactly that (session 100, issue #8).
3. The binary loads and reaches `main`: it runs with `OGRE_SMOKE=1`, which exits
   0 as soon as `main` is reached, under a deadline (`SMOKE_TIMEOUT` seconds,
   default 90) so a loader error dialog or a hang cannot stall a release run.
   The launch check is skipped when the package is for another platform (a
   Windows zip checked from macOS still gets checks 1 and 2).
4. A crash leaves a usable report: the same binary runs with
   `OGRE_CRASH_TEST=segv`, and `error.log` must exist and contain the `[boot]`
   line printed immediately before the raise. That line only reaches the report
   through the stdout/stderr capture, so an empty report (the capture never ran,
   or the handler died on a stdio lock) fails the package instead of reaching a
   player. `error.log` is removed afterwards.

## Releases (GitHub Actions)

`.github/workflows/release.yml` builds macOS, Linux and Windows packages,
creates a GitHub release and attaches one archive per platform — `.tar.gz` on
macOS and Linux, `.zip` on Windows — so the downloads appear on the repository's
Releases page.

Prepare a release with the one command that does all of it:

```sh
make release VERSION=v0.6.0
```

`tools/release.sh` is that command. It:

1. regenerates the game code (`make bank-recomp`, `make recomp`, `make
   rsp-recomp`) and the example mods,
2. packs `files.tar.gz` with `tools/data-bundle.sh`, then unpacks it and
   compares every file against the tree, so a bundle holding code generated
   before the current inputs fails here rather than in CI,
3. commits and pushes the bundle to the private data repository, and
4. dispatches the Release workflow with `draft` and waits for all three
   platform builds.

The draft release is at `https://github.com/<repo>/releases`. Publish it after
reviewing the assets:

```sh
gh release edit v0.6.0 --draft=false
```

The tag is created by the workflow's `gh release create`, on the commit the run
built. `make release` refuses a version that already has a tag or a release, a
tracked tree with uncommitted changes, and a branch whose tip is not pushed,
because the bundle records `git rev-parse HEAD` and the hosted unpack step fails
when that differs from the commit being built.

`make release-check VERSION=v0.6.0` is the same script with `NO_PUSH=1`: it
regenerates, packs and reports the bundle, the commit it would push and the run
it would start, and changes nothing outside this tree.

The generated code is a function of the ROM and `config/`, so a commit that
changes only tooling or documentation leaves the bundle byte-identical. The
script compares the packed code with the tree (the workflow cannot: it has no
ROM) and, when they agree, repacks the archive with the commit being released,
because the workflow refuses a bundle whose recorded commit differs from the
build's.

```sh
VERSION=v0.6.0          # required only to override; default: newest tag + 1
DATA_REPO=OWNER/REPO    # default: the OGRE_DATA_REPO Actions variable
REGEN=0                 # use the generated code already in the tree
SKIP_BUNDLE=1           # reuse dist/ogre-data/files.tar.gz; for a retry after
                        # the workflow failed. The code comparison still runs
                        # and the bundle is re-recorded if it is identical
DRAFT=false             # publish instead of drafting
NO_WAIT=1               # do not wait for the run
make release
```

**`platforms` selects the matrix for a dispatched run** (`all`, `windows`,
`linux` or `macos`; a tag push always builds all three). A Windows-only run is
about 8 minutes instead of 30, which is the iteration path for a
Windows-specific bug; it still runs the smoke test and creates the release, so
that release carries only the one platform's asset. `make release` always asks
for `all`.

```sh
gh workflow run release.yml -f tag=v0.2.1-rc3 -f platforms=windows
```

A tag push runs the workflow too, and on that path the `tag` and `draft` inputs
are empty: the tag that was pushed is used and the release is published. Use
`make release` or a dispatch when the release should be a draft; the `draft`
input defaults to `true` so an unattended dispatch does not publish.

`tools/release-build.sh` is the single build entry point — the workflow just
exports `DIST_OS` and runs it, so a release artifact is byte-for-byte what
`make dist` produces locally:

```sh
tools/release-build.sh                  # this machine's platform
DIST_OS=linux tools/release-build.sh    # what CI does per runner
```

**Why the workflow uses self-hosted runners by default.** A GitHub-hosted runner
clones the repository, and the recompiled code that `make dist` links is *not in
the repository* (`RecompiledFuncs/`, `Bank*Funcs/`, `RspFuncs/`,
`app/src/bank_funcs.inc` are generated from the ROM and gitignored). Those
runners have no ROM, so they cannot produce it, and `tools/release-build.sh`
fails fast with the list of what is missing rather than a wall of compile
errors. The default `runs-on:` values therefore select self-hosted runners,
where the ROM and the generated code already are.

## Building releases on GitHub-hosted runners

A private data repository supplies the generated code, which lets the release
matrix run on hosted runners with no ROM present.

1. On a machine with the ROM and a built tree, produce the archive. `make
   release` does this and the two steps after it; the commands it runs are:

   ```sh
   make bank-recomp && make recomp && make rsp-recomp
   make example-mods
   tools/data-bundle.sh                 # -> dist/ogre-data/files.tar.gz
   ```

   The order is `make regenerate`'s: `make recomp` dispatches the cross-bank
   calls against the records `make bank-recomp` writes into
   `app/src/bank_funcs.inc`, and refuses to run without them.

   The archive holds `RecompiledFuncs/`, `Bank*Funcs/`, `RspFuncs/` and
   `app/src/bank_funcs.inc` at its root, the packaged example mods
   (`build/mods/*.nrm`), plus `ogre-data.txt` recording the public commit it was
   generated from. It refuses to run when any of them is missing.

   The mods are in the archive because a release runner cannot build them: it has
   no ROM and no `mips-linux-gnu-gcc`, and the macOS image has no Linux container
   runtime for `tools/build-example-mods.sh`'s Docker fallback. `make dist`
   requires at least one `.nrm` and fails without one,
   `tools/release-build.sh` checks for them before the app build, the workflow's
   unpack step fails a bundle that carries none, and `tools/smoke-dist.sh` fails
   a package whose `mods/` directory is empty. v0.4.0 shipped an empty `mods/`
   because the bundle omitted them and `make dist` only warned (session 108).

   The unpack step also fails when the bundle's recorded commit differs from the
   commit being built. The runner cannot recompile, so it cannot test whether an
   older bundle still matches; it prints how many commits newer the build is and
   the regeneration command. Regenerating is part of preparing a release, not an
   optional refresh.

2. Commit `files.tar.gz` to the private repository. `make release` copies it
   there, commits it as `generated code at <commit>, with the example mods` and
   pushes, then dispatches the workflow.

3. In this repository's settings, set the **Actions variable** `OGRE_DATA_REPO`
   to the private repository slug (for example `OWNER/PRIVATE-REPO`), and add the
   **secret** `OGRE_DATA_TOKEN`: a fine-grained token with `Contents: Read` on
   that private repository. Setting the variable switches every matrix job to its
   hosted runner and unpacks the archive from there; leaving it unset keeps the
   self-hosted labels. The repository's name is not recorded here; the variable
   is its only reference.

The archive is a snapshot of the recompiler's output, so regenerate it whenever
a change alters the generated code or anything under `mods/`. Regenerate it as
part of preparing a release: the workflow fails the job when the archive's
recorded commit differs from the commit being built, because nothing on the
runner can tell whether that bundle still describes this source tree. The stale
bundle left by session 108 failed three hosted jobs at the unpack step with
`build/mods/*.nrm is missing from files.tar.gz`. Hosting the generated C
in a private repository keeps it out of this repository and out of the release
archives; it does not change what the published binary contains.

On the hosted path the workflow reconstructs everything else a fresh checkout
lacks: it clones `N64ModernRuntime` at the pinned commit and applies
`patches/n64modernruntime-ob64.patch` and `patches/n64modernruntime-n64recomp.patch`, applies
`patches/rt64-ob64.patch` and `patches/rt64-plume-ob64.patch`/`tools/rt64-plume-sdl.patch` to the
RT64 submodules, and installs what the runner image is missing (`libgtk-3-dev`,
`libvulkan-dev`, `libx11-dev` on Linux; the Metal toolchain on macOS; `make` on
Windows). The self-hosted path keeps its own toolchain and dirtied submodules.

The patch step is line-ending tolerant. A Windows checkout gives both the patch
files and the RT64 tree CRLF, and `git apply` fails on a hunk whose last line has
no newline (`\ No newline at end of file`; `patches/rt64-ob64.patch` has two) in that
state. `.gitattributes` pins `*.patch` to `eol=lf` and the workflow passes
`--ignore-whitespace` to `git apply`. Keep both when editing a patch or the
`apply_patch` helper: without them the Windows job fails at
`rt64_tmem_hasher.h:203` and `rt64_native_target.cpp:369`.

Three platform properties of the hosted path are load-bearing.

- **The data bundle must not carry AppleDouble files.** macOS `tar` writes
  `._<name>` members for files with extended attributes. They are metadata on
  macOS but ordinary files on Linux, and `app/CMakeLists.txt` globs `*.c`, so
  the Linux build compiles them and fails with
  `._funcs_0.c:1:2: error: stray '\5' in program`. `tools/data-bundle.sh` sets
  `COPYFILE_DISABLE=1` and excludes `._*`; the workflow's unpack step deletes
  any that an older bundle still carries.
- **The arm64 build needs the `sse2neon` include.** `librecomp/rsp_vu.hpp`
  includes `<sse2neon.h>` under `__aarch64__` and `<nmmintrin.h>` on x86-64.
  `ultramodern` propagates `thirdparty/sse2neon` PUBLIC, which reaches the app
  target, but `ogrebattle64_rsp` does not link `ultramodern`, so
  `app/CMakeLists.txt` lists the directory for it. An x86-64 machine never
  reads the header, so a missing entry only shows on Apple Silicon.
- **A Windows runner builds with clang-cl under Ninja.** `cl.exe` rejects the
  GCC/Clang spellings the app, the runtime and RT64 use for their flags — `cl :
  command line error D8021: invalid numeric argument '/Wno-unused-variable'` —
  so the Windows job uses the configuration Zelda64Recomp uses:
  `ilammy/msvc-dev-cmd@v1` exports the MSVC headers, libraries and linker,
  `choco install ninja` provides the generator, and `CMAKE_ARGS=-G Ninja
  -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl` selects the
  compiler. `app/CMakeLists.txt` then adds `-Xclang -fexceptions
  -fcxx-exceptions` for clang-cl and keys its flag groups on
  `CMAKE_CXX_COMPILER_ID`, not on `MSVC` — CMake reports `MSVC` for clang-cl
  too, because it targets the MSVC ABI. Windows-only consequences: the static
  SDL2 library is `SDL2-static.lib` and its package config is in
  `<prefix>/cmake` (SDL2 uses `lib/cmake/SDL2` only off MSVC), so `make` passes
  `-DOGRE_STATIC_SDL2=ON` and the app finds the config in either directory
  before RT64 is added; `--config Release` is passed so a multi-config generator
  would not build Debug; the app defines `SDL_MAIN_HANDLED` and calls
  `SDL_SetMainReady()` so it keeps its own `main`; `create_window` returns
  RT64's `{ HWND, thread_id }` handle; `readlink` and `__attribute__((weak))`
  have Windows branches (the weak attribute is dropped for `_MSC_VER`, and the
  crash handler uses POSIX `sigaction`, so it is compiled out). The package
  carries `dxcompiler.dll` and `dxil.dll`, which RT64 copies next to its library
  and loads at runtime.

A self-hosted runner needs, per platform:

| platform | toolchain | SDL2 |
|---|---|---|
| macOS | Xcode command line tools (incl. the Metal toolchain: `xcodebuild -downloadComponent MetalToolchain`) | built by `make sdl2-static` |
| Linux | `cmake`, `ninja`, `g++`, `pkg-config`, `libgtk-3-dev`, `libvulkan-dev`, `libx11-dev` | `libsdl2-dev` (or `make sdl2-static`, which also needs `libasound2-dev libpulse-dev libpipewire-0.3-dev` so the static SDL2 has audio) |
| Windows | Visual Studio 2022 with the C++ Clang tools (`clang-cl`) and `ninja`, `make`, run from a Developer Command Prompt; MinGW is not tested | built by `make sdl2-static` |
