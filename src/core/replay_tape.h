#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/input_frame.h"

namespace apricot {

// The replay tape, and the version that pairs with it.
//
// A recorded run is a seed, the sim step it began from, and one InputFrame per
// step. That is the entire format. It stores no positions along the way: the
// trajectory is RECOMPUTED by pushing the frames back through the same pure
// step functions the player drove, so there is exactly one trajectory in the
// build and no second copy of it to drift.
//
// THIS LIVES IN core/ ON PURPOSE. It used to live in game/, next to the sample
// game that happened to record tapes first, and that made the version constant
// unreachable from tests/sim_determinism_tests.cpp — the suite that pins the
// format — because that suite deliberately includes nothing from game/. A
// format owned by whichever game is currently the sample is a format that
// leaves with it. The tape belongs to the engine; a game is just its first
// caller.

// Bump this whenever InputFrame's layout, the fields of ReplayTape, or the step
// semantics change. An old tape played back against new physics produces a
// plausible-looking wrong result, and that is far worse than a refusal to load.
//
// v1: seed plus frames. Replayable only from a standing start at the origin,
//     which no real run is.
// v2: added a ReplayStart block carrying the whole VehicleState the run began
//     from, plus the sample game's next-checkpoint index and lap clock.
// v3: the start block is gone from the tape. Two of its three fields were the
//     sample game's rules, and the third — the start VehicleState — would drag
//     physics/ into core/ and invert the layering. `start_step` survives on its
//     own because it is not a game concept (see below). A caller that needs to
//     restore a start POSE pairs the tape with its own start record; the tape
//     stays a flat block of intent.
inline constexpr uint32_t kReplayTapeVersion = 3;

struct ReplayTape {
    uint32_t version = kReplayTapeVersion;
    uint64_t seed = 0;

    // The ABSOLUTE sim step that frames[0] advances FROM.
    //
    // Not bookkeeping. Anything keyed on absolute step — game/conditions.h's
    // weather is the live example — hands a tape recorded an hour into a
    // session the conditions of step zero unless the tape carries where it
    // started. The symptom is "replays drift", which is nobody's good week.
    uint64_t start_step = 0;

    // One per sim step. Index i advances the world from step
    // (start_step + i) to (start_step + i + 1).
    std::vector<InputFrame> frames;
};

// Fetch the input at tape-relative index `frame` — NOT an absolute sim step;
// add tape.start_step for that. Returns false past the end of the tape, which
// is how playback knows the run is over.
//
// NEVER EXTRAPOLATES. A silently repeated last frame would let a replay drive
// on past the end of the run it recorded, and it would look entirely plausible
// while doing it.
//
// Inline here rather than in a .cpp for the same reason is_held() and
// was_pressed() are: it is a bounds check and a copy, and core/input_frame.h is
// the shape this file follows.
inline bool replay_input(const ReplayTape& tape, uint64_t frame, InputFrame& out) {
    if (frame >= tape.frames.size()) return false;
    out = tape.frames[static_cast<std::size_t>(frame)];
    return true;
}

}  // namespace apricot
