# Ogre Battle 64: Person of Lordly Caliber - PC port app.
#
# Builds the recompiled game code (RecompiledFuncs/) against N64ModernRuntime
# (ultramodern + librecomp) and links the RT64 renderer. See
# docs/guides/app-build.md for instructions.
#
#   cmake -S app -B build-app -DCMAKE_BUILD_TYPE=Release          # RT64 (default)
#   cmake -S app -B build-null -DOGRE_USE_RT64=OFF                # null renderer
#   cmake --build <dir> -j

cmake_minimum_required(VERSION 3.20)

project(ogrebattle64 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (WIN32)
    # Opt out of constexpr mutex constructor on windows to prevent vcredist issues
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR")
    # SDL_syswm.h pulls in windows.h for the HWND, and windows.h defines min/max
    # macros that break std::min/std::max. WIN32_LEAN_AND_MEAN is not set here:
    # the runtime's renderer_context.hpp defines it, and defining it again on the
    # command line makes clang-cl warn about the redefinition.
    add_compile_definitions(NOMINMAX)
endif()

# --- Flags for the app's own targets -----------------------------------------
# These are GCC/Clang spellings. The Windows build uses clang-cl (see
# docs/guides/app-build.md -> Releases), which is Clang and accepts all of them;
# cl.exe rejects them with a hard error rather than a warning:
#
#   cl : command line error D8021: invalid numeric argument '/Wno-unused-variable'
#
# So they live in one variable that is empty for cl.exe. Keyed on the compiler
# *ID*, not on `MSVC`: CMake reports `MSVC` as true for clang-cl too, because
# clang-cl targets the MSVC ABI. cl.exe also needs no -msse4.1 (it exposes the
# SSSE3/SSE4.1 intrinsics that librecomp/rsp_vu.hpp uses without an /arch
# switch), while GCC and Clang gate them behind the flag on an x86-64 baseline.
# -fno-omit-frame-pointer keeps a host backtrace unwindable at a crash: the
# runtime's shadow guest chain is empty when the fault is in runtime code
# reached through a bridge (see tools/watch.sh and docs/guides/app-build.md).
set(OGRE_GCC_FLAGS
    -fno-strict-aliasing
    -fno-omit-frame-pointer
    -Wno-unused-variable
    -Wno-unused-parameter
    -Wno-unused-function
    -Wno-unused-but-set-variable
    -Wno-implicit-function-declaration
)
set(OGRE_X86_SIMD_FLAGS "")
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(OGRE_GCC_FLAGS "")
elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(OGRE_X86_SIMD_FLAGS -msse4.1)
endif()

# clang-cl does not enable C++ exceptions by default, and RT64's D3D12 path
# needs them. Mirrors Zelda64Recomp's Windows build (-DCMAKE_CXX_FLAGS="-Xclang
# -fexceptions -Xclang -fcxx-exceptions"); CMAKE_CXX_FLAGS keeps it off the C
# compilations of the recompiled game code.
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Xclang -fexceptions -Xclang -fcxx-exceptions")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Source paths compiled into the binary (__FILE__ in asserts and logs) are
# written relative to the repository root, so a release does not carry the
# build machine's folder names. Set before the subdirectories, so the runtime,
# RT64 and the recompiled code get it too. cl.exe has no equivalent flag.
get_filename_component(OGRE_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC")
    add_compile_options("/clang:-ffile-prefix-map=${OGRE_REPO_ROOT}/=")
elseif (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options("-ffile-prefix-map=${OGRE_REPO_ROOT}/=")
endif()

# Root of the third-party tree (sibling of app/).
set(OGRE_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/../tools")

# --- Static CRT on Windows ---------------------------------------------------
# A distributed Windows build must not depend on the Visual C++ redistributable.
# Without vcruntime140.dll/msvcp140.dll the loader fails before any code runs,
# and a GUI-subsystem image shows no window and writes no log (session 94: the
# packaged .exe "did nothing"). Every target this project creates inherits the
# setting from here, including the N64ModernRuntime and RT64 subdirectories
# below; the prebuilt static SDL2 is built the same way by `make sdl2-static`.
# CMP0091 (CMake >= 3.15) makes the policy variable the switch for this, and
# `project()` above set a new-enough minimum for it.
if (WIN32 AND (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded" CACHE STRING
        "Static CRT for the distributed Windows build" FORCE)
endif()

# --- Renderer selection -----------------------------------------------------
# RT64 is the primary native renderer (Vulkan on Linux, Metal on macOS,
# D3D12/Vulkan on Windows). It can be disabled for a GPU-free null-renderer
# build (useful for bring-up and CI).
option(OGRE_USE_RT64 "Build with the RT64 renderer (native builds only)" ON)

# --- Static SDL2 from `make sdl2-static` -------------------------------------
# `make sdl2-static` builds SDL2 into tools/SDL2-static/prefix and passes
# -DOGRE_STATIC_SDL2=ON. Point find_package at that copy here, before RT64 is
# added below, so RT64 and the app link one SDL2 instead of RT64's bundled
# win32-deps copy. The package config directory differs by generator: SDL2
# installs it to <prefix>/cmake under MSVC (SDL2's CMakeLists:
# `if (WINDOWS AND NOT MINGW) set(SDL_INSTALL_CMAKEDIR_DEFAULT "cmake")`) and to
# <prefix>/lib/cmake/SDL2 everywhere else, so check both instead of passing a
# path from the Makefile, which cannot know the generator.
option(OGRE_STATIC_SDL2 "Link the static SDL2 under tools/SDL2-static/prefix" OFF)
if (OGRE_STATIC_SDL2)
    set(OGRE_SDL2_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/../tools/SDL2-static/prefix")
    foreach (OGRE_SDL2_CANDIDATE "${OGRE_SDL2_PREFIX}/cmake" "${OGRE_SDL2_PREFIX}/lib/cmake/SDL2")
        if (EXISTS "${OGRE_SDL2_CANDIDATE}/SDL2Config.cmake")
            set(SDL2_DIR "${OGRE_SDL2_CANDIDATE}" CACHE PATH "SDL2 package config directory" FORCE)
            message(STATUS "static SDL2: ${SDL2_DIR}")
            break()
        endif()
    endforeach()
endif()

# --- Android: SDL2 built from source ----------------------------------------
# The Android build is a shared library (`libmain.so`) that SDL's Java activity
# loads next to `libSDL2.so`, so SDL2 is built here from the source tree that
# `make sdl2-static` fetched (OGRE_ANDROID_SDL2_SOURCE). See android/README.md.
if (ANDROID)
    set(OGRE_ANDROID_SDL2_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../tools/SDL2-static/SDL2-2.32.10"
        CACHE PATH "SDL2 source tree for the Android build")
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${OGRE_ANDROID_SDL2_SOURCE} ${CMAKE_BINARY_DIR}/SDL2)
    # RT64 reads SDL2_INCLUDE_DIRS, which find_package would otherwise set.
    set(SDL2_INCLUDE_DIRS "${OGRE_ANDROID_SDL2_SOURCE}/include" "${CMAKE_BINARY_DIR}/SDL2/include"
        "${CMAKE_BINARY_DIR}/SDL2/include-config-release")
endif()

# --- Runtime: ultramodern + librecomp (+ its bundled N64Recomp) --------------
add_subdirectory(${OGRE_TOOLS}/N64ModernRuntime ${CMAKE_BINARY_DIR}/N64ModernRuntime)

# Frame pointers in the runtime too, not just in the app: a crash inside
# runtime code reached through a bridge (the message-queue math, for example)
# has an empty shadow *guest* chain, and without frame pointers lldb prints only
# the faulting frame and nobody can see who called it. RT64 keeps its own flags
# (hot path). See docs/guides/app-build.md -> "Diagnostics toolkit". cl.exe
# keeps frame pointers on x64 by default and rejects the flag, so it is skipped
# there; clang-cl is Clang and takes it.
if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    foreach(OGRE_FP_TARGET librecomp ultramodern)
        if (TARGET ${OGRE_FP_TARGET})
            target_compile_options(${OGRE_FP_TARGET} PRIVATE -fno-omit-frame-pointer)
        endif()
    endforeach()
endif()

# librecomp's RSP vector unit (librecomp/rsp_vu.hpp, which the recompiled RSP
# microcode in RspFuncs/ includes) enables its SIMD path with
# `#if defined(__x86_64__) || defined(_M_X64)` and then uses SSSE3/SSE4.1
# intrinsics (`_mm_shuffle_epi8`, `_mm_blendv_epi8`) with no runtime guard, but
# no target asks the compiler for those instructions. The x86-64 baseline is
# SSE2 only, so a GCC build fails to compile outright:
#
#   error: inlining failed in call to 'always_inline' '_mm_shuffle_epi8':
#          target specific option mismatch
#
# Apple clang defaults to a newer -march that happens to include them, which is
# why this only bites on Linux. The header gates the path on
# ARCHITECTURE_SUPPORTS_SSE4_1, so SSE4.1 (a superset of SSSE3) is the right
# level; every x86-64 CPU this port targets has had it since ~2008. aarch64
# needs nothing (rsp_vu.hpp reaches the same code through sse2neon), and MSVC
# needs nothing (it allows the intrinsics without an /arch switch).
# OGRE_X86_SIMD_FLAGS is set next to OGRE_GCC_FLAGS above; the RSP microcode
# target gets it below.
if (TARGET librecomp AND OGRE_X86_SIMD_FLAGS)
    target_compile_options(librecomp PRIVATE ${OGRE_X86_SIMD_FLAGS})
endif()

# --- Renderer ----------------------------------------------------------------
if (OGRE_USE_RT64)
    # Static build, expecting an SDL window outside of Windows (same options as
    # Zelda64Recomp). On Linux RT64_SDL_WINDOW_VULKAN creates a Vulkan surface from
    # the SDL window (the primary, most-tested backend path).
    set(RT64_STATIC TRUE)
    set(RT64_SDL_WINDOW_VULKAN TRUE)
    add_compile_definitions(HLSL_CPU)
    add_subdirectory(${OGRE_TOOLS}/RT64 ${CMAKE_BINARY_DIR}/RT64)
    target_include_directories(rt64 PRIVATE ${CMAKE_BINARY_DIR}/RT64/src)
    set(OGRE_RENDERER_SOURCES src/renderer.cpp)
else()
    set(OGRE_RENDERER_SOURCES src/null_renderer.cpp)
endif()

# --- SDL2 (window, input, audio) ---------------------------------------------
if (NOT TARGET SDL2::SDL2)
    find_package(SDL2 REQUIRED)
endif()

# --- Recompiled game code ----------------------------------------------------
# The main-segment recompilation output (807 functions). Compiled as a static
# lib so the app can link it alongside the runtime. CONFIGURE_DEPENDS re-globs
# when `make recomp` adds/removes files.
file(GLOB RECOMP_C_SOURCES   CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../RecompiledFuncs/*.c)
file(GLOB RECOMP_CPP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../RecompiledFuncs/*.cpp)

add_library(ogrebattle64_recomp STATIC ${RECOMP_C_SOURCES} ${RECOMP_CPP_SOURCES})

target_include_directories(ogrebattle64_recomp PRIVATE
    ${OGRE_TOOLS}/N64ModernRuntime/N64Recomp/include
    ${OGRE_TOOLS}/N64ModernRuntime/ultramodern/include
    ${OGRE_TOOLS}/N64ModernRuntime/librecomp/include
)

target_compile_options(ogrebattle64_recomp PRIVATE ${OGRE_GCC_FLAGS})

# --- Recompiled RSP microcode -----------------------------------------------
# Non-graphics RSP tasks are run through recompiled microcode (src/rsp.cpp).
# Today that is the game's Nintendo-JPEG decoder (M_NJPEGTASK, type 4) used for
# the New Game opening's full-screen backgrounds; regenerate with
# `make rsp-recomp` (config/rsp-njpeg.toml -> RspFuncs/njpeg_ucode.cpp). The generated
# code is C++ (it uses librecomp's RSP VU implementation).
file(GLOB RSP_RECOMP_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../RspFuncs/*.cpp)

if(RSP_RECOMP_SOURCES)
    add_library(ogrebattle64_rsp STATIC ${RSP_RECOMP_SOURCES})

    target_include_directories(ogrebattle64_rsp PRIVATE
        ${OGRE_TOOLS}/N64ModernRuntime/N64Recomp/include
        ${OGRE_TOOLS}/N64ModernRuntime/ultramodern/include
        ${OGRE_TOOLS}/N64ModernRuntime/librecomp/include
        # On arm64, librecomp/rsp_vu.hpp includes <sse2neon.h> and not the x86
        # SSE4.1 intrinsics header. ultramodern propagates this directory
        # PUBLIC, which reaches the app target, but this static library does not
        # link ultramodern, so it needs the directory itself. Without it an
        # Apple Silicon build fails at `rsp_vu.hpp:29: 'sse2neon.h' file not
        # found`; an x86-64 build never reads the header.
        ${OGRE_TOOLS}/N64ModernRuntime/thirdparty/sse2neon
    )

    target_compile_options(ogrebattle64_rsp PRIVATE
        ${OGRE_GCC_FLAGS}
        ${OGRE_X86_SIMD_FLAGS}
    )
endif()

# --- Streamed-overlay bank units (Phase 4) ----------------------------------
# Independent recompilations (config/banks/config-bank<U>.yaml / .toml, built by
# `make bank-recomp`) for the streamed overlay records the game loads into RAM
# that overlay C also uses. They cannot share overlay C's VMAs in one ELF, so
# each unit namespaces its symbols (`ovl<U>_`) and they are registered at runtime
# by src/bank_overlays.cpp. One static library per unit: each unit's sources must
# include their own funcs.h.
file(GLOB BANK_UNIT_DIRS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../Bank*Funcs)

set(OGRE_BANK_LIBS)
foreach(BANK_UNIT_DIR ${BANK_UNIT_DIRS})
    get_filename_component(BANK_UNIT_LIB ${BANK_UNIT_DIR} NAME)
    file(GLOB BANK_UNIT_SOURCES CONFIGURE_DEPENDS ${BANK_UNIT_DIR}/*.c)

    add_library(${BANK_UNIT_LIB} STATIC ${BANK_UNIT_SOURCES})
    target_include_directories(${BANK_UNIT_LIB} PRIVATE
        ${BANK_UNIT_DIR}
        ${OGRE_TOOLS}/N64ModernRuntime/N64Recomp/include
        ${OGRE_TOOLS}/N64ModernRuntime/ultramodern/include
        ${OGRE_TOOLS}/N64ModernRuntime/librecomp/include
    )
    target_compile_options(${BANK_UNIT_LIB} PRIVATE ${OGRE_GCC_FLAGS})
    list(APPEND OGRE_BANK_LIBS ${BANK_UNIT_LIB})
endforeach()

# --- The app --------------------------------------------------------------
# SDL-based entry and platform callbacks. gbi.cpp is the F3DEX2 display list
# parser and workload analyzer.
set(OGRE_APP_SOURCES
    src/main.cpp
    src/sdl_platform.cpp
    src/crash_log.cpp
    src/synth_frame.cpp
    src/rsp.cpp
    src/overlays.cpp
    src/bank_overlays.cpp
    src/gbi.cpp
    src/save_import.cpp
    src/font.cpp
    src/input_map.cpp
    src/settings.cpp
    src/game_host.cpp
    features/options.cpp
    features/army_roster.cpp
    features/exp_share.cpp
    features/class_rebalance.cpp
    features/content_db.cpp
    features/content_json.cpp
    features/content_files.cpp
    features/class_animation.cpp
    features/class_change.cpp
    features/dialogue_autoplay.cpp
    features/war_director.cpp
    features/boss_escorts.cpp
    features/recruit.cpp
    features/enemy_commander.cpp
    features/field_speed.cpp
    features/director_spawn.cpp
    features/game_events.cpp
    features/alerts.cpp
    features/rom_patch.cpp
    src/widescreen.cpp
    features/frame_rate.cpp
    src/ui.cpp
    src/launcher.cpp
    src/overlay.cpp
    src/speed_badge.cpp
    src/hang_watch.cpp
    src/quick_save.cpp
    features/button_legend.cpp
    features/save_backup.cpp
    features/save_profiles.cpp
    features/screenshot.cpp
    src/hd_backgrounds.cpp
    src/map_cursor.cpp
    features/battle_ai.cpp
    features/battle_figures.cpp
    features/battle_form.cpp
    src/mission_cursor.cpp
    src/ram_key.cpp
    features/music_replace.cpp
)

if (ANDROID)
    # SDLActivity loads `libmain.so` and calls its SDL_main.
    add_library(ogrebattle64 SHARED ${OGRE_APP_SOURCES} ${OGRE_RENDERER_SOURCES})
    set_target_properties(ogrebattle64 PROPERTIES OUTPUT_NAME main)
    target_link_libraries(ogrebattle64 PRIVATE android log)
else()
    add_executable(ogrebattle64 ${OGRE_APP_SOURCES} ${OGRE_RENDERER_SOURCES})
endif()

# The distributed build has no console window. A default Windows link is a
# console-subsystem binary, so launching the .exe allocates a console that shows
# the [boot] log; link a GUI-subsystem binary instead. That subsystem selects the
# CRT entry point `WinMainCRTStartup`, which calls `WinMain`; the app defines
# `main`, so app/src/main.cpp adds a `WinMain` forwarder (MinGW's own CRT already
# supplies one, hence the guard there). app/src/crash_log.cpp is what replaces
# the console for a crash report.
if (WIN32)
    set_target_properties(ogrebattle64 PROPERTIES WIN32_EXECUTABLE TRUE)
    # Identical code folding merges functions that compile to the same bytes, and
    # the runtime patches a function's own code to install a mod hook, so a fold
    # can make a hook patch the wrong function (Zelda64Recomp disables it on
    # Windows for that reason). It is a cl.exe-era default; lld-link, which the
    # CI's clang-cl build uses, does not fold unless asked, so this is only for a
    # link driven by cl.exe.
    if (MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_link_options(ogrebattle64 PRIVATE /OPT:NOICF)
    endif()
endif()

# Copy the app-local MSVC runtime next to the executable.
#
# RT64 statically imports `dxcompiler.dll` and `dxil.dll`, which the package
# bundles, and those two are Microsoft binaries that import `MSVCP140.dll`,
# `VCRUNTIME140.dll` and `VCRUNTIME140_1.dll`. A machine without the Visual C++
# 2015-2022 redistributable therefore cannot start the process at all, and with a
# GUI-subsystem image that is a dialog at best and "nothing happens" at worst
# (session 94). Shipping the CRT DLLs beside the .exe is Microsoft's supported
# "local deployment" of the redistributable, and it is what a player needs. The
# app itself links the static CRT (see CMAKE_MSVC_RUNTIME_LIBRARY above), so
# these DLLs exist for the shader compiler only.
if (WIN32 AND (MSVC OR CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC"))
    set(_ogre_crt_patterns "")
    if (DEFINED ENV{VCToolsRedistDir} AND NOT "$ENV{VCToolsRedistDir}" STREQUAL "")
        list(APPEND _ogre_crt_patterns
            "$ENV{VCToolsRedistDir}x64/Microsoft.VC1*.CRT"
            "$ENV{VCToolsRedistDir}/x64/Microsoft.VC1*.CRT")
    endif()
    if (DEFINED ENV{VCINSTALLDIR} AND NOT "$ENV{VCINSTALLDIR}" STREQUAL "")
        list(APPEND _ogre_crt_patterns
            "$ENV{VCINSTALLDIR}/Redist/MSVC/*/x64/Microsoft.VC1*.CRT")
    endif()
    set(_ogre_crt_dir "")
    if (_ogre_crt_patterns)
        file(GLOB _ogre_crt_dirs ${_ogre_crt_patterns})
        if (_ogre_crt_dirs)
            list(GET _ogre_crt_dirs 0 _ogre_crt_dir)
        endif()
    endif()
    if (_ogre_crt_dir)
        foreach(_ogre_crt_dll msvcp140.dll msvcp140_atomic_wait.dll vcruntime140.dll vcruntime140_1.dll)
            if (EXISTS "${_ogre_crt_dir}/${_ogre_crt_dll}")
                add_custom_command(TARGET ogrebattle64 POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_ogre_crt_dir}/${_ogre_crt_dll}"
                            "$<TARGET_FILE_DIR:ogrebattle64>/${_ogre_crt_dll}"
                    COMMENT "app-local MSVC runtime: ${_ogre_crt_dll}")
            endif()
        endforeach()
    else()
        message(WARNING
            "no Microsoft.VC1*.CRT directory under VCToolsRedistDir/VCINSTALLDIR; "
            "the app-local MSVC runtime (msvcp140.dll, vcruntime140.dll) will not "
            "be copied and dxcompiler.dll will not load on a machine without the "
            "Visual C++ redistributable")
    endif()
endif()

# RT64's CMake copies dxcompiler.dll and dxil.dll next to its own library
# (<build>/RT64), and `make dist` takes them from there for the package. The
# exe in the build directory needs them beside it too, or a run from a fresh
# build directory stops with a "DLL not found" error before the first frame.
if (WIN32 AND NOT ANDROID AND TARGET rt64)
    foreach(_ogre_dxc_dll dxcompiler.dll dxil.dll)
        add_custom_command(TARGET ogrebattle64 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_BINARY_DIR}/RT64/${_ogre_dxc_dll}"
                    "$<TARGET_FILE_DIR:ogrebattle64>/${_ogre_dxc_dll}"
            COMMENT "RT64 shader compiler: ${_ogre_dxc_dll}")
    endforeach()
endif()

# The project's content packs and scripts (app/content/*.json, *.lua) beside the
# executable, in content/, where the content database and the Lua runtime load
# them (features/content_db.cpp, features/lua_runtime.cpp).
add_custom_command(TARGET ogrebattle64 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/content" "$<TARGET_FILE_DIR:ogrebattle64>/content"
    COMMENT "content packs: content/")

# Keep frame pointers in the app + recompiled code so lldb can unwind a host
# backtrace at a crash. The runtime keeps a shadow *guest* call chain, but it is
# empty when the fault is inside runtime code reached through a bridge (a
# SIGFPE in the message-queue math, for example), and without frame pointers
# `bt` prints only the faulting frame. RT64 is left alone (hot path). cl.exe
# keeps frame pointers on x64 by default and rejects the flag.
if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(ogrebattle64 PRIVATE -fno-omit-frame-pointer)
endif()

target_include_directories(ogrebattle64 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # OB64: Another World's features; app/src is the recompilation's app shell.
    ${CMAKE_CURRENT_SOURCE_DIR}/features
    ${CMAKE_CURRENT_SOURCE_DIR}/../RecompiledFuncs
    ${OGRE_TOOLS}/N64Recomp/include
    ${OGRE_TOOLS}/N64ModernRuntime/N64Recomp/include
    ${OGRE_TOOLS}/N64ModernRuntime/ultramodern/include
    ${OGRE_TOOLS}/N64ModernRuntime/librecomp/include
    ${OGRE_TOOLS}/N64ModernRuntime/thirdparty
    # stb_image, for the HD-background pack's PNG decode
    # (src/hd_backgrounds.cpp). Present for every build flavor, RT64 or not.
    ${OGRE_TOOLS}/RT64/src/contrib
)

if (OGRE_USE_RT64)
    # RT64 headers. RT64's own include_directories() only apply inside the RT64
    # subdirectory, so repeat them here for the app's renderer.cpp.
    target_include_directories(ogrebattle64 PRIVATE
        ${OGRE_TOOLS}/RT64/src
        ${OGRE_TOOLS}/RT64/src/contrib
        ${OGRE_TOOLS}/RT64/src/contrib/plume
        ${OGRE_TOOLS}/RT64/src/contrib/imgui
        ${OGRE_TOOLS}/RT64/src/contrib/hlslpp/include
        ${OGRE_TOOLS}/RT64/src/contrib/mupen64plus-core/src/api
        ${OGRE_TOOLS}/RT64/src/contrib/nativefiledialog-extended/src/include
    )
endif()

target_link_libraries(ogrebattle64 PRIVATE
    ogrebattle64_recomp
    ${OGRE_BANK_LIBS}
    librecomp
    ultramodern
)

if(RSP_RECOMP_SOURCES)
    target_link_libraries(ogrebattle64 PRIVATE ogrebattle64_rsp)
endif()

# Lua 5.4 (MIT) for content scripts (features/lua_runtime.cpp), from lua.org's
# release tarball, pinned by the SHA-256 lua.org publishes. Lua ships no CMake
# build, so its library sources are compiled here, as C++: a script error then
# unwinds as a C++ exception through the port's API functions instead of a
# longjmp past their destructors.
include(FetchContent)
FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.8.tar.gz
    URL_HASH SHA256=4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae
    SOURCE_SUBDIR no-cmake-build)
FetchContent_MakeAvailable(lua)
file(GLOB OGRE_LUA_SOURCES "${lua_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM OGRE_LUA_SOURCES "${lua_SOURCE_DIR}/src/lua.c" "${lua_SOURCE_DIR}/src/luac.c")
set_source_files_properties(${OGRE_LUA_SOURCES} PROPERTIES LANGUAGE CXX)
add_library(ogre_lua STATIC ${OGRE_LUA_SOURCES})
target_include_directories(ogre_lua PUBLIC "${lua_SOURCE_DIR}/src")
if (MSVC)
    target_compile_options(ogre_lua PRIVATE /EHsc /W0)
endif()
target_sources(ogrebattle64 PRIVATE features/lua_runtime.cpp features/lua_sandbox.cpp)
target_link_libraries(ogrebattle64 PRIVATE ogre_lua)

# The sandbox escape test (docs/anotherworld/security.md): built with the
# game and run after every build, so a change that opens a way out of the
# sandbox fails the build. Not run when cross-compiling (Android).
add_executable(lua_sandbox_test features/lua_sandbox_test.cpp features/lua_sandbox.cpp)
target_link_libraries(lua_sandbox_test PRIVATE ogre_lua)
if (NOT CMAKE_CROSSCOMPILING)
    add_custom_command(TARGET lua_sandbox_test POST_BUILD
        COMMAND lua_sandbox_test
        COMMENT "Lua sandbox escape test")
endif()
add_dependencies(ogrebattle64 lua_sandbox_test)

# The content pack hostile-file test, run the same way.
add_executable(content_pack_test features/content_pack_test.cpp features/content_db.cpp
    features/content_json.cpp features/content_files.cpp features/class_animation.cpp
    features/class_change.cpp)
target_include_directories(content_pack_test PRIVATE features
    "${OGRE_TOOLS}/N64ModernRuntime/thirdparty"
    "${OGRE_TOOLS}/N64ModernRuntime/librecomp/include"
    "${OGRE_TOOLS}/N64ModernRuntime/ultramodern/include"
    "${OGRE_TOOLS}/N64ModernRuntime/thirdparty/concurrentqueue"
    "${OGRE_TOOLS}/N64ModernRuntime/N64Recomp/include")
if (NOT CMAKE_CROSSCOMPILING)
    add_custom_command(TARGET content_pack_test POST_BUILD
        COMMAND content_pack_test "${PROJECT_SOURCE_DIR}/content/examples"
        COMMENT "Content pack hostile-file test")
endif()
add_dependencies(ogrebattle64 content_pack_test)

if (OGRE_USE_RT64)
    # --- RmlUi: the port's own game screens, drawn by RT64 over the game ------
    # (app/features/rml_ui.cpp, docs/anotherworld/design-organize-screen.md section 4). FreeType
    # and RmlUi are fetched at pinned release tags. RmlUi's find_package(Freetype)
    # is satisfied by an alias over the fetched FreeType: FindFreetype does not
    # create Freetype::Freetype when the target already exists.
    include(FetchContent)
    # FreeType 2.13.2 declares a minimum CMake below 3.5, which CMake 4 rejects
    # unless a policy floor is given.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG VER-2-13-2
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(freetype)
    if (NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype)
    endif()
    set(FREETYPE_LIBRARY freetype CACHE STRING "" FORCE)
    set(FREETYPE_INCLUDE_DIRS "${freetype_SOURCE_DIR}/include" CACHE STRING "" FORCE)
    set(FREETYPE_INCLUDE_DIR_ft2build "${freetype_SOURCE_DIR}/include" CACHE STRING "" FORCE)
    set(FREETYPE_INCLUDE_DIR_freetype2 "${freetype_SOURCE_DIR}/include" CACHE STRING "" FORCE)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(rmlui
        GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
        GIT_TAG 6.0
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(rmlui)

    # The two RmlUi shaders, compiled with RT64's own shader functions (DXIL on
    # Windows, Metal on macOS, SPIR-V everywhere). Those functions read these
    # variables at call time, and RT64 sets them in its own directory scope.
    set(OGRE_RT64_DIR "${OGRE_TOOLS}/RT64")
    if (WIN32 OR (ANDROID AND CMAKE_HOST_WIN32))
        set(DXC "${OGRE_RT64_DIR}/src/contrib/dxc/bin/x64/dxc.exe")
    elseif (APPLE)
        if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
            set(DXC "DYLD_LIBRARY_PATH=${OGRE_RT64_DIR}/src/contrib/dxc/lib/x64" "${OGRE_RT64_DIR}/src/contrib/dxc/bin/x64/dxc-macos")
        else()
            set(DXC "DYLD_LIBRARY_PATH=${OGRE_RT64_DIR}/src/contrib/dxc/lib/arm64" "${OGRE_RT64_DIR}/src/contrib/dxc/bin/arm64/dxc-macos")
        endif()
    else()
        if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
            set(DXC "LD_LIBRARY_PATH=${OGRE_RT64_DIR}/src/contrib/dxc/lib/x64" "${OGRE_RT64_DIR}/src/contrib/dxc/bin/x64/dxc-linux")
        else()
            set(DXC "LD_LIBRARY_PATH=${OGRE_RT64_DIR}/src/contrib/dxc/lib/arm64" "${OGRE_RT64_DIR}/src/contrib/dxc/bin/arm64/dxc-linux")
        endif()
    endif()
    set(DXC_COMMON_OPTS "-I${PROJECT_SOURCE_DIR}/shaders")
    set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
    set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
    set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_3")
    set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_3" "-fvk-invert-y")
    build_vertex_shader(ogrebattle64 "shaders/RmlUiVS.hlsl")
    build_pixel_shader(ogrebattle64 "shaders/RmlUiPS.hlsl")
    target_sources(ogrebattle64 PRIVATE features/rml_ui.cpp features/organize_view.cpp features/organize_edit.cpp features/options_view.cpp features/alerts_view.cpp features/field_view.cpp features/field_select.cpp features/portraits.cpp features/auto_commander.cpp
        features/class_art.cpp)
    target_include_directories(ogrebattle64 PRIVATE ${CMAKE_BINARY_DIR})
    target_link_libraries(ogrebattle64 PRIVATE RmlUi::Core)
    target_compile_definitions(ogrebattle64 PRIVATE OGRE_HAVE_RMLUI=1)

    # Documents and fonts next to the executable, in ui/ (the Lato fonts ship
    # with RmlUi's samples under the SIL Open Font License).
    add_custom_command(TARGET ogrebattle64 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/ui" "$<TARGET_FILE_DIR:ogrebattle64>/ui"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ogrebattle64>/ui/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf" "$<TARGET_FILE_DIR:ogrebattle64>/ui/fonts/"
        COMMAND ${CMAKE_COMMAND} -E copy "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Bold.ttf" "$<TARGET_FILE_DIR:ogrebattle64>/ui/fonts/"
        COMMAND ${CMAKE_COMMAND} -E copy "${rmlui_SOURCE_DIR}/Samples/assets/LICENSE.txt" "$<TARGET_FILE_DIR:ogrebattle64>/ui/fonts/OFL-LICENSE.txt")

    target_link_libraries(ogrebattle64 PRIVATE rt64)
    # The start screen's "click to load your ROM" uses nativefiledialog-extended,
    # which RT64 already vendors and compiles on every desktop platform (Cocoa
    # on macOS, Win32 on Windows, GTK/portal on Linux). The null-renderer
    # variant deliberately does not link it: it keeps the ROM drag-and-drop path
    # and OGRE_ROM, and reports the missing browser on screen.
    if (NOT ANDROID)
        target_compile_definitions(ogrebattle64 PRIVATE OGRE_HAVE_NFD=1)
        target_link_libraries(ogrebattle64 PRIVATE nfd)
    endif()
endif()

# Tells the platform code whether a real renderer will attach to the window, so
# it only asks SDL for the backend's window flag (Metal/Vulkan) when something
# will use it. The null build must still work on a machine with no GPU driver —
# see the window-creation comment in src/sdl_platform.cpp.
if (OGRE_USE_RT64)
    target_compile_definitions(ogrebattle64 PRIVATE OGRE_USE_RT64=1)
endif()

target_link_libraries(ogrebattle64 PRIVATE SDL2::SDL2)
if (WIN32)
    # SDL.h renames `main` to `SDL_main` on Windows and expects SDL2main to
    # provide the real entry point. This app defines its own main and does
    # not link SDL2main, so opt out of the rename. Without it the MSVC link
    # fails on an unresolved `main`.
    target_compile_definitions(ogrebattle64 PRIVATE SDL_MAIN_HANDLED)
    # The start screen's file picker is an IFileDialog owned by the SDL
    # window (launcher.cpp). ole32/uuid are the COM libraries it uses.
    target_link_libraries(ogrebattle64 PRIVATE ole32 uuid)
endif()
if (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(ogrebattle64 PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endif()

