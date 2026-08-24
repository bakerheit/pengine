#!/usr/bin/env bash
#
# THE ARCHITECTURE, ENFORCED.
#
# apricot_sim links glm and Threads. Nothing else. No windowing library, no GL
# loader, no audio device, no UI toolkit. That constraint is what makes every
# sim module headless-testable, deterministic and replayable, and it is the
# kind of rule that erodes one innocent #include at a time.
#
# So this runs FIRST in CI, before anything is compiled: a compile error you
# hit in thirty seconds is cheap, and an architecture you notice you lost six
# months later is not.
#
# It is a plain text search, deliberately. That means it also trips on the
# banned words in COMMENTS, which is not a bug — sim code should not need to
# talk about the windowing library either. If you need to reference the host
# layer from a sim comment, say "the host layer" or "the platform layer".
#
# If this fails, the fix is essentially never to loosen the guard. It is to
# move the code to src/platform/ or src/gfx/ and have the sim side talk to it
# through plain data (see core/input_frame.h for the shape).
set -euo pipefail

cd "$(dirname "$0")/.."

# Modules owned entirely by apricot_sim.
SIM_DIRS=(
    src/core
    src/scene
    src/terrain
    src/road
    src/physics
    src/game
)

# src/audio is the one SPLIT module: these files build into apricot_sim, while
# device.{h,cpp} and miniaudio_impl.c build into apricot_host and are exempt.
SIM_FILES=(
    src/audio/mixer.h
    src/audio/synth.h
    src/audio/synth.cpp
)

BANNED='glad|SDL|<GL/|miniaudio|imgui'

fail=0

# --- 1. no hardware-layer identifiers in sim sources -------------------------
files=()
for d in "${SIM_DIRS[@]}"; do
    if [[ ! -d "$d" ]]; then
        echo "guard: sim module '$d' is missing (was it renamed?)" >&2
        fail=1
        continue
    fi
    while IFS= read -r f; do
        files+=("$f")
    done < <(find "$d" -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.c' \))
done

for f in "${SIM_FILES[@]}"; do
    if [[ ! -f "$f" ]]; then
        # A renamed or deleted split-module file must not silently stop being
        # checked. That is how a guard rots into decoration.
        echo "guard: expected sim-side file '$f' is missing (was it renamed?)" >&2
        fail=1
        continue
    fi
    files+=("$f")
done

if [[ ${#files[@]} -eq 0 ]]; then
    echo "guard: found no sim sources to check - that cannot be right" >&2
    exit 1
fi

if hits=$(grep -nE "$BANNED" "${files[@]}" 2>/dev/null); then
    echo "" >&2
    echo "SIM PURITY VIOLATION" >&2
    echo "apricot_sim must not reference the host layer. Found:" >&2
    echo "" >&2
    echo "$hits" >&2
    echo "" >&2
    echo "Move the code to src/platform/ or src/gfx/ and pass plain data across." >&2
    fail=1
fi

# --- 2. no sim module may attach sources to apricot_host ---------------------
# The grep above cannot see a module quietly adding itself to the host target,
# which is the other way the split gets lost.
for d in "${SIM_DIRS[@]}"; do
    cm="$d/CMakeLists.txt"
    [[ -f "$cm" ]] || continue
    if grep -q 'apricot_host' "$cm"; then
        echo "guard: $cm attaches sources to apricot_host; $d is a sim module" >&2
        fail=1
    fi
done

if [[ $fail -ne 0 ]]; then
    exit 1
fi

echo "sim purity OK (${#files[@]} files checked, 0 violations)"
