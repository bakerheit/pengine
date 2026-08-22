#!/usr/bin/env bash
#
# Local CI. Run it before you push; it is the same set of gates the build is
# expected to survive.
#
#   1. sim purity  - the architecture guard, first because it is instant
#   2. configure
#   3. build       - -Werror is set on our targets in CMakeLists.txt
#   4. ctest       - every suite is headless: no window, no GL, no audio device
#
#     tools/ci.sh
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build}"

echo ">> [1/4] sim purity guard"
tools/guard_sim_purity.sh

echo ">> [2/4] configure"
cmake -S . -B "$BUILD_DIR"

echo ">> [3/4] build (-Werror)"
cmake --build "$BUILD_DIR" -j

echo ">> [4/4] headless tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo ""
echo ">> local CI passed"
