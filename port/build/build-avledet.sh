#!/usr/bin/env bash
# Build Avledet inside the avledet-build image.
# Usage: build/build-avledet.sh [Release|Debug]
# Needs Steamworks SDK unpacked at third_party/steamsdk/sdk
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TYPE="${1:-Release}"
SDK="$ROOT/third_party/steamsdk"

if [[ ! -f "$SDK/sdk/redistributable_bin/linux64/libsteam_api.so" ]]; then
    echo "Steamworks SDK not found at $SDK/sdk" >&2
    exit 1
fi

docker run --rm -u "$(id -u):$(id -g)" \
    -v "$ROOT/avledet:/src" -v "$SDK:/steamsdk:ro" \
    avledet-build:1 bash -c "
        cmake -S /src -B /src/build-$TYPE -G Ninja \
            -DCMAKE_BUILD_TYPE=$TYPE \
            -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
            -DVCPKG_MANIFEST_MODE=OFF \
            -DLUA_INCLUDE_DIR=/opt/vcpkg/installed/x64-linux-dynamic/include \
            -DLUA_LIBRARY=/opt/vcpkg/installed/x64-linux-dynamic/lib/liblua.so \
            -DSTEAMWORKS_SDK=/steamsdk \
        && cmake --build /src/build-$TYPE"
