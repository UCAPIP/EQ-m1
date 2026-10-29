#!/usr/bin/env bash
# Build and run the framework-free DSP core tests. Usage: scripts/test.sh
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build-test -G Ninja -DCMAKE_BUILD_TYPE=Release -DEQM1_BUILD_PLUGIN=OFF
cmake --build build-test -j"$(nproc)"
./build-test/EQM1Tests
