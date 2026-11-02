#pragma once

// The version of OB64: Another World (docs/anotherworld/DECISIONS.md, 2026-10-31).
//
// This is the one place it is set. main.cpp passes it to the runtime as the
// project version (a mod's minimum version is compared against it), the start
// screen footer, the Esc overlay and the crash report print it, and
// tools/release.sh refuses a release tag that is not `v` + VERSION_TEXT.
// packaging/macos-Info.plist (CFBundleShortVersionString, CFBundleVersion) and
// android/app/build.gradle (versionName) repeat it by hand.

namespace ogre {

inline constexpr int VERSION_MAJOR = 0;
inline constexpr int VERSION_MINOR = 3;
inline constexpr int VERSION_PATCH = 0;
inline constexpr const char* VERSION_TEXT = "0.3.0";

}  // namespace ogre

