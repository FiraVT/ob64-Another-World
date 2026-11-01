# Contributing to OB64: Another World

Contributions are welcome: code, research into the game, content, art, testing
and docs. Contributions are licensed GPL-3.0-or-later (`LICENSE`).

This page covers the one-time setup, the build, and where to start reading.

## What you need first

- **Your own ROM.** Ogre Battle 64: Person of Lordly Caliber, USA, Rev A, 40 MB.
  The repository holds no game data. The recompiled C is generated from your ROM
  and is never committed, so a fresh clone cannot build the app until
  `make regenerate` has run once.
- **About 5 GB of disk** for the vendored toolchains, the generated code and the
  build trees.

Content mods (JSON packs and Lua scripts) need no build at all. Start from the
examples in `app/content/examples/` and
[docs/anotherworld/design-mods.md](docs/anotherworld/design-mods.md).

## Set up a build

Steps 2 to 7 are the same on every platform. Run them in bash: Git Bash on
Windows, a terminal on Linux and macOS.

### 1. Install the platform tools

**Windows 10/11**

- Visual Studio 2022 or newer with "Desktop development with C++" and
  "C++ Clang tools for Windows". The app builds with `clang-cl` against the MSVC
  headers.
- Git for Windows (Git Bash), CMake, Ninja, GNU make and Python 3. The release
  workflow installs make and Ninja with `choco install make ninja`.
- MIPS binutils for Windows (`mips-linux-gnu-as`, `-ld`, `-objcopy`,
  `-objdump`) on `PATH`. `tools/build.sh` adds `tools/mips-binutils/bin` to
  `PATH`, and that directory is gitignored.

**Linux (Ubuntu 22.04 or 24.04)**

```bash
sudo apt install -y build-essential cmake clang gcc-mips-linux-gnu libsdl2-dev libvulkan-dev vulkan-tools zlib1g-dev git python3 python3-venv
```

[docs/guides/linux-migration.md](docs/guides/linux-migration.md) has the
details, including the SDL 2.0.22 patch that Ubuntu 22.04 needs.

**macOS (Apple Silicon)**

```bash
brew install mips-linux-gnu-binutils cmake sdl2
```

The RT64 Metal backend needs the Metal toolchain
(`xcodebuild -downloadComponent MetalToolchain`).

### 2. Clone with the renderer submodule

```bash
git clone --recurse-submodules https://github.com/FiraVT/ob64-Another-World.git
```

`tools/RT64` is the only submodule. `tools/N64Recomp` and
`tools/N64ModernRuntime` are gitignored clones that you create in steps 4 and 5.

### 3. splat

```bash
python3 -m venv tools/venv && tools/venv/bin/pip install 'splat64[mips]'
```

On Windows the venv puts its programs in `tools/venv/Scripts/`, and the
Makefile runs `tools/venv/bin/splat`. Create that launcher once:

```bash
mkdir -p tools/venv/bin && printf '#!/bin/sh\nexec "$(cd "$(dirname "$0")/.." && pwd)/Scripts/python.exe" -m splat "$@"\n' > tools/venv/bin/splat && chmod +x tools/venv/bin/splat
```

On Windows, the `pip` in the command above is `tools/venv/Scripts/pip`.

### 4. N64Recomp (the recompiler)

```bash
git clone --recurse-submodules https://github.com/N64Recomp/N64Recomp.git tools/N64Recomp
git -C tools/N64Recomp checkout ffb39cdad1da5de07eaaa48bd1db4a89a7986771
git -C tools/N64Recomp apply ../../patches/n64recomp-ob64.patch
git -C tools/N64Recomp apply ../../patches/n64recomp-anotherworld.patch
cmake -S tools/N64Recomp -B tools/N64Recomp/build -DCMAKE_BUILD_TYPE=Release
cmake --build tools/N64Recomp/build --target N64RecompCLI -j
```

### 5. N64ModernRuntime (the runtime)

The commit is the one the release workflow pins
(`.github/workflows/release.yml`, "Prepare third-party trees").

```bash
git clone https://github.com/N64Recomp/N64ModernRuntime.git tools/N64ModernRuntime
git -C tools/N64ModernRuntime checkout 589bbf018a3e6d3646ddf7de1e7919f1b7e99bb1
git -C tools/N64ModernRuntime submodule update --init --recursive
git -C tools/N64ModernRuntime apply ../../patches/n64modernruntime-ob64.patch
git -C tools/N64ModernRuntime apply ../../patches/n64modernruntime-anotherworld.patch
git -C tools/N64ModernRuntime/N64Recomp apply ../../../patches/n64modernruntime-n64recomp.patch
```

### 6. RT64 patches

```bash
git -C tools/RT64 apply ../../patches/rt64-ob64.patch
git -C tools/RT64 apply ../../patches/rt64-anotherworld.patch
git -C tools/RT64/src/contrib/plume apply ../../../../../patches/rt64-plume-ob64.patch
```

`git submodule update` resets `tools/RT64` and drops these patches; apply them
again after one. `make patch-check` compares the patches with the trees.

### 7. The ROM and the generated code

Put your dump at `assets/ogre64.z64`. A byte-swapped `.n64` dump converts with
`python3 tools/convert_rom.py`.

```bash
make regenerate
```

This runs splat, the MIPS link, the 34 bank units, the main recompilation and
the RSP microcode, in the one order that works
([docs/guides/build.md](docs/guides/build.md)).

### 8. Build the app

**Linux and macOS**

```bash
cmake -S app -B build-app -DCMAKE_BUILD_TYPE=Release
cmake --build build-app -j
```

The executable is `build-app/ogrebattle64`.

**Windows.** Open Git Bash from an "x64 Native Tools" command prompt, or run
`vcvars64.bat` first, so that `clang-cl` finds the MSVC headers and libraries.
Then run:

```bash
make app DIST_OS=windows DIST_STATIC_SDL=1 CMAKE_ARGS="-G Ninja -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl"
```

The executable is `build-dist/ogrebattle64.exe`. Copy `dxcompiler.dll` and
`dxil.dll` from `tools/RT64/src/contrib/dxc/bin/x64/` next to it.
`tools/build.sh windows` runs the same steps. It currently expects two
local helper scripts in `tools/mips-binutils/` (`vsenv.bat` and `build-app.sh`)
that are not in the repository.

**Android**

```bash
tools/build.sh android
```

This needs the Android SDK, NDK r27 (`27.2.12479018`), JDK 17 and Gradle 8.7.
Set `ANDROID_HOME`, `ANDROID_NDK`, `JAVA_HOME_17` and `GRADLE` if they are not
in the default locations the script names. The APK is written to
`dist/ogrebattle64.apk`.

**A package for players:** `make dist` and `make dist-zip`; see
[docs/guides/releases.md](docs/guides/releases.md).

### Checks that run on every build

The app build also builds and runs `lua_sandbox_test` and `content_pack_test`.
A failing case fails the build (AGENTS.md rule 16). Everything else is checked by
playing the game with your ROM; the repository has no other automated tests.

## Where to start reading

| If you want to | Read |
|---|---|
| see what is built and what is planned | [docs/anotherworld/FEATURES.md](docs/anotherworld/FEATURES.md), [docs/anotherworld/ROADMAP.md](docs/anotherworld/ROADMAP.md) |
| find any document | [docs/README.md](docs/README.md) |
| make a content mod | [docs/anotherworld/design-mods.md](docs/anotherworld/design-mods.md), [docs/anotherworld/security.md](docs/anotherworld/security.md) |
| change a game system | [docs/anotherworld/separation.md](docs/anotherworld/separation.md), then the system's design doc in `docs/anotherworld/` |
| reverse-engineer the game | [AGENTS.md](AGENTS.md), [docs/guides/recomp-facts.md](docs/guides/recomp-facts.md), [docs/guides/diagnostics.md](docs/guides/diagnostics.md) |
| debug a rendering problem | [docs/guides/emulator-first.md](docs/guides/emulator-first.md) |

## Recording your work

- Write in simple, technical English: one claim per sentence, with the address,
  file or measurement that supports it (AGENTS.md, "Writing style").
- A decision goes in [docs/anotherworld/DECISIONS.md](docs/anotherworld/DECISIONS.md),
  newest first.
- A change to what the game does goes in
  [docs/anotherworld/FEATURES.md](docs/anotherworld/FEATURES.md), and a change
  to the plan goes in [docs/anotherworld/ROADMAP.md](docs/anotherworld/ROADMAP.md).
- Never commit a ROM, an extracted asset, generated code, or changes inside the
  vendored trees under `tools/`. A change to a vendored tree is committed as a
  patch in `patches/`.
