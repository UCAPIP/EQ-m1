#!/usr/bin/env bash
# Configure and build the plugin (VST3 + Standalone). Usage: scripts/build.sh [buildDir]
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR="${1:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo "Artefacts:"
if [ -d "$BUILD_DIR/EQ_M1_artefacts" ]; then
    find "$BUILD_DIR/EQ_M1_artefacts" -maxdepth 5 \( -name '*.vst3' -o -name '*.so' \) -print
fi
