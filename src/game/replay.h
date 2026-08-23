#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/input_frame.h"
#include "core/transform.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"

namespace apricot {

// Replay tapes and the ghost that plays them back.
//
// The architectural bet of this engine, stated plainly: a recorded lap is a
// seed plus a start state plus the exact inputs. The ghost is not a track of
// recorded positions being interpolated — it is those inputs pushed back
// through step_vehicle, the same function the player is driving, one sim step
// at a time. There is therefore exactly ONE trajectory in the build, and a
// ghost cannot disagree with the physics it is drawn next to, because it IS
// the physics.

// Bump this whenever InputFrame's layout, ReplayStart's contents, or the step
// semantics change. An old tape played back against new physics produces a
// plausible-looking wrong result, which is far worse than a refusal to load.
//
// v2: added ReplayStart. v1 tapes carried no start state and were therefore
// only replayable from a standing start at the origin, which no real lap is.
inline constexpr uint32_t kReplayTapeVersion = 2;

// The exact state the recorded lap began from.
//
// Yes, this stores a position — ONE, the boundary condition of an integration.
// Replaying inputs without it is not an under-determined problem, it is a
// DIFFERENT problem: the same steering from a different pose is a different
// drive. What is deliberately absent is a position per frame. The trajectory
// is recomputed, never stored, so there is no second copy of it to drift.
struct ReplayStart {
    uint64_t step = 0;       // absolute sim step that frames[0] advances FROM
    VehicleState car;        // complete — wheels included, see replay.cpp
    int checkpoint = 0;      // next gate owed at that moment
    double lap_time = 0.0;   // lap clock at that moment
};

struct ReplayTape {
    uint32_t version = kReplayTapeVersion;
    uint64_t seed = 0;
    ReplayStart start;

    // One per sim step. Index i advances the world from step
    // (start.step + i) to (start.step + i + 1).
    std::vector<InputFrame> frames;
};

// Fetch the input at tape-relative index `frame` — NOT an absolute sim step;
// add tape.start.step for that. Returns false past the end of the tape, which
// is how playback knows the run is over. Never extrapolates: a silently
// repeated last frame would let a replay drive on past the finish.
bool replay_input(const ReplayTape& tape, uint64_t frame, InputFrame& out);

// The ghost itself lives in game/ghost.h, because it is not a separate
// integrator — it is a whole RallyState in replay mode, and it therefore needs
// the rules. See the note at the top of that file.

}  // namespace apricot
