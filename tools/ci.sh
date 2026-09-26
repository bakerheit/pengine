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
# Bare -j launches every compiler at once. A version bump recompiles the whole
# tree and can exhaust a developer's Mac while other agents are building too.
BUILD_JOBS="${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "CMAKE_BUILD_PARALLEL_LEVEL must be a positive integer" >&2
    exit 2
fi

# Not a gate step: makes sure this clone runs the version hooks in .githooks/
# (docs/versioning.md). Idempotent, and silent once it is set.
tools/version.sh install

# The pre-push hook runs this gate before any push to main, and skips it when
# this file already names the tree being pushed. Written only for a checkout
# with no tracked changes that is still the same tree when the gate goes green,
# so the stamp never vouches for edits that were not part of the commit.
STAMP="$BUILD_DIR/gate-passed-tree"
clean_tree() {
    [[ -z "$(git status --porcelain --untracked-files=no)" ]] && git rev-parse 'HEAD^{tree}'
}
START_TREE="$(clean_tree || true)"
rm -f "$STAMP"

echo ">> [1/5] sim purity guard"
tools/guard_sim_purity.sh

echo ">> [2/5] lightbar profile guard"
python3 tools/guard_lightbar_profiles.py

echo ">> [3/5] configure"
cmake -S . -B "$BUILD_DIR"

echo ">> [4/5] build (-Werror, $BUILD_JOBS jobs)"
cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

echo ">> [5/5] headless tests"
ctest --test-dir "$BUILD_DIR" --output-on-failure

if [[ -n "$START_TREE" && "$(clean_tree || true)" == "$START_TREE" ]]; then
    echo "$START_TREE" >"$STAMP"
fi

echo ""
echo ">> local CI passed"
