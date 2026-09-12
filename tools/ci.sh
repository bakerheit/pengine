#!/usr/bin/env bash
#
# Local CI. Run it before you push; it is the same set of gates the build is
# expected to survive.
#
#   1. sim purity  - the architecture guard, first because it is instant
#   2. lightbars   - the other text guard; cross-file, so no build can catch it
#   3. configure
#   4. build       - -Werror is set on our targets in CMakeLists.txt
#   5. ctest       - every suite is headless: no window, no GL, no audio device
#
# Steps 1 and 2 are plain text checks that need no build and no cooked assets,
# which is the only reason they can run before configure. Everything a compiler
# or a headless suite can prove belongs in ctest instead.
#
#     tools/ci.sh
set -euo pipefail

cd "$(dirname "$0")/.."

BUILD_DIR="${BUILD_DIR:-build}"

echo ">> [1/5] sim purity guard"
tools/guard_sim_purity.sh

echo ">> [2/5] lightbar profile guard"
python3 tools/guard_lightbar_profiles.py

echo ">> [3/5] configure"
cmake -S . -B "$BUILD_DIR"

echo ">> [4/5] build (-Werror)"
cmake --build "$BUILD_DIR" -j

echo ">> [5/5] headless tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo ""
echo ">> local CI passed"
