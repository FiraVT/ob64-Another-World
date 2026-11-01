#!/usr/bin/env bash
# Build the port for one target, with the example mods bundled.
#
#   tools/build.sh windows     # build-dist/ogrebattle64.exe (+ mods in build-dist/mods)
#   tools/build.sh android     # dist/ogrebattle64.apk
#
# Both targets share the generated game code (`make regenerate` once) and keep
# separate build directories, so switching between them never rebuilds the other.
#
# Windows needs Visual Studio 2022 with the C++ Clang tools. Android needs the
# Android SDK, NDK r27 and a JDK 17; the defaults below are where they were
# installed on this machine and can be overridden with the environment.
#
# Large units taking one square is built into the app (app/features/class_rebalance.cpp),
# so the Small Large Units mod is no longer bundled.
#
# The mods listed in MODS are built (tools/build-example-mods.sh), copied into
# the target, and enabled the first time the game starts.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

target=${1:-}
# No mod by default. smart-enemy-formations used to ship here, enabled on
# Android, and it re-seats every enemy member after the built-in ENEMY
# FORMATIONS, so the built-in formations never showed (docs/anotherworld/DECISIONS.md
# 2026-10-31). Name mods here to bundle them for a test build.
MODS=${MODS:-""}
ENABLED_IDS=${ENABLED_IDS:-""}

ANDROID_HOME=${ANDROID_HOME:-$HOME/Android/Sdk}
ANDROID_NDK=${ANDROID_NDK:-$ANDROID_HOME/ndk/27.2.12479018}
JAVA_HOME=${JAVA_HOME_17:-$HOME/Android/jdk17}
GRADLE=${GRADLE:-$HOME/Android/gradle-8.7/bin/gradle}
VSENV="$root/tools/mips-binutils/vsenv.bat"
GIT_BASH="C:\\Program Files\\Git\\usr\\bin\\bash.exe"

export PATH="$root/tools/mips-binutils/bin:$PATH"
export PYTHONUTF8=1

build_mods() {
    if [ ! -f mods/reference/dump.toml ]; then
        make mod-syms
    fi
    for mod in $MODS; do
        tools/build-example-mods.sh "mods/$mod"
    done
}

case "$target" in
windows)
    build_mods
    cmd //c "$(cygpath -w "$VSENV")" "$GIT_BASH" "$root/tools/mips-binutils/build-app.sh"
    cp tools/RT64/src/contrib/dxc/bin/x64/dxcompiler.dll tools/RT64/src/contrib/dxc/bin/x64/dxil.dll build-dist/
    mkdir -p build-dist/mods
    for mod in $MODS; do
        cp "build/mods/$mod.nrm" build-dist/mods/
    done
    echo "==> build-dist/ogrebattle64.exe"
    ;;

android)
    build_mods
    if [ ! -f build-android/build.ninja ]; then
        cmake -S app -B build-android -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared \
            -DRT64_HOST_FILE_TO_C="$root/build-dist/file_to_c.exe"
    fi
    cmake --build build-android --target ogrebattle64 -j

    llvm="$ANDROID_NDK/toolchains/llvm/prebuilt/windows-x86_64"
    jni=android/app/src/main/jniLibs/arm64-v8a
    mkdir -p "$jni"
    "$llvm/bin/llvm-strip.exe" --strip-debug -o "$jni/libmain.so" build-android/libmain.so
    "$llvm/bin/llvm-strip.exe" --strip-debug -o "$jni/libSDL2.so" build-android/SDL2/libSDL2.so
    cp "$llvm/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so" "$jni/"

    assets=android/app/src/main/assets/mods
    rm -rf "$assets"
    mkdir -p "$assets"
    for mod in $MODS; do
        cp "build/mods/$mod.nrm" "$assets/"
    done
    : > "$assets/enabled.txt"
    for id in $ENABLED_IDS; do
        printf '%s\n' "$id" >> "$assets/enabled.txt"
    done

    # OGRE_BUNDLE_ROM=1: a personal build that carries the developer's own ROM
    # (assets/ogre64.z64) so the app installs it on first run instead of asking
    # (RomSetupActivity). That APK contains the game and is for the developer's
    # own devices; the default build carries no ROM. android/app/src/main/assets
    # is gitignored.
    rom_assets=android/app/src/main/assets/rom
    rm -rf "$rom_assets"
    apk_name=ogrebattle64.apk
    if [ "${OGRE_BUNDLE_ROM:-0}" = "1" ]; then
        if [ ! -f assets/ogre64.z64 ]; then
            echo "OGRE_BUNDLE_ROM=1 needs assets/ogre64.z64" >&2
            exit 1
        fi
        mkdir -p "$rom_assets"
        cp assets/ogre64.z64 "$rom_assets/ogre64.z64"
        apk_name=ogrebattle64-with-rom.apk
    fi

    echo "sdk.dir=$(cygpath -m "$ANDROID_HOME")" > android/local.properties
    (cd android && JAVA_HOME="$(cygpath -w "$JAVA_HOME")" "$GRADLE" --no-daemon assembleRelease)
    rm -rf "$rom_assets"
    mkdir -p dist
    cp android/app/build/outputs/apk/release/app-release.apk "dist/$apk_name"
    echo "==> dist/$apk_name"
    ;;

*)
    echo "usage: tools/build.sh windows|android" >&2
    exit 2
    ;;
esac

