#!/usr/bin/env bash
# Build the Linux package on the device itself: a SteamOS handheld (x86-64 or
# ARM64), inside a distrobox Ubuntu 24.04 container, from a source bundle made
# on the development PC (the generated game code is in the bundle, so no ROM
# recompilation runs here). RT64 carries a shader compiler for both Linux
# architectures (tools/RT64/src/contrib/dxc/bin/{x64,arm64}/dxc-linux).
#
# On the device, in the extracted bundle folder:
#
#   bash tools/build-on-device.sh
#
# Output: dist/OB64-AnotherWorld/, run with ./ob64-anotherworld from that
# folder. When assets/ogre64.z64 is in the bundle (a personal bundle), it is
# copied into the package's roms/ so the game starts without asking.
#
# The first run creates the container and installs the build packages (about
# 1 GB) and downloads SDL2's source; later runs reuse both. The build runs with
# Ninja, which limits parallel jobs to the CPU count, so a handheld does not run
# out of memory the way an unlimited `make -j` would.
set -euo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"
box=${BOX:-ob64-build}

# A bundle packed on Windows carries CRLF line ends in the Makefile and the
# shell scripts, which make and bash reject, and no executable bit on RT64's
# shader tools.
sed -i 's/\r$//' Makefile tools/*.sh
chmod +x tools/*.sh tools/RT64/src/contrib/dxc/bin/*/dxc-* \
    tools/RT64/src/contrib/spirv-cross/bin/*/spirv-cross 2>/dev/null || true

if ! command -v distrobox >/dev/null 2>&1; then
    echo "build-on-device.sh: distrobox is missing (SteamOS 3.5 and later ship it)" >&2
    exit 1
fi
if ! distrobox list 2>/dev/null | grep -q "| $box "; then
    distrobox create --name "$box" --image docker.io/library/ubuntu:24.04 --yes
fi

distrobox enter "$box" -- bash -c "
set -euo pipefail
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake ninja-build make python3 curl pkg-config \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev \
    libxss-dev libxfixes-dev libwayland-dev libxkbcommon-dev wayland-protocols \
    libdecor-0-dev libegl-dev libgl-dev libdrm-dev libgbm-dev \
    libasound2-dev libpulse-dev libpipewire-0.3-dev \
    libgtk-3-dev libvulkan-dev zlib1g-dev
cd '$root'
make dist DIST_OS=linux CMAKE_ARGS='-G Ninja'
"

if [ -f assets/ogre64.z64 ]; then
    cp assets/ogre64.z64 dist/OB64-AnotherWorld/roms/
fi
echo "==> dist/OB64-AnotherWorld/ob64-anotherworld"

