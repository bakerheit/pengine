#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/input_frame.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"

namespace apricot {

// Rally rules: a checkpoint route, lap timing, and the replay tape.
//
// Every clock in here counts SIM STEPS, never wall time. Lap times are
// accumulated dt, and progress is indexed by step. That is what makes a lap
// time a property of the drive rather than of the machine it ran on — and it
// is what lets a replay reproduce a run exactly instead of approximately.

struct Checkpoint {
    glm::vec3 position{0.0f};

    // Direction the car should be travelling through the gate. Used to reject
    // a car that clips the trigger sphere going backwards, which is otherwise
    // a free lap for anyone who reverses over the line.
    glm::vec3 forward{0.0f, 0.0f, -1.0f};

    float radius = 12.0f;
};

struct Route {
    uint64_t seed = 0;
    std::vector<Checkpoint> checkpoints;

    // True when the last checkpoint feeds back into the first (a circuit);
    // false for a point-to-point stage.
    bool closed = true;
};

struct LapTiming {
    double lap_time = 0.0;     // seconds, current lap
    double total_time = 0.0;   // seconds, whole session
    double best_lap = 0.0;     // 0 = no lap completed yet
    int lap = 0;               // laps completed
    int target_laps = 3;
    bool finished = false;
};

// Bump this whenever InputFrame's layout or the step semantics change. An old
// tape played back against new physics produces a plausible-looking wrong
// result, which is far worse than a refusal to load.
inline constexpr uint32_t kReplayTapeVersion = 1;

// A whole recorded run: a seed and the exact inputs, nothing else. The world
// is a pure function of the seed and the car is a pure function of its inputs,
// so this reproduces the drive completely — no positions are stored, and
// storing them would only create a second source of truth to disagree with.
struct ReplayTape {
    uint32_t version = kReplayTapeVersion;
    uint64_t seed = 0;
    std::vector<InputFrame> frames;  // one per sim step, index == step number
};

struct RallyState {
    VehicleState car;
    VehicleTuning tuning;
    Route route;

    // Index into route.checkpoints of the next gate to pass.
    int next_checkpoint = 0;

    LapTiming timing;
    ReplayTape tape;

    // Sim steps elapsed. The authoritative clock for everything here.
    uint64_t step_index = 0;

    // While true, step_rally appends each input to `tape`. Turn it off when
    // playing a tape back, or the playback re-records itself.
    bool recording = true;
};

// Advance the rally by exactly one sim step. Pure in the same sense as
// step_vehicle: no wall clock, no globals. `dt` is kSimDt.
//
// Consumes the LATCHED edge bits in `input` but does not clear them — the
// frame loop clears them once, after the last step of the frame. See
// core/fixed_step.h.
void step_rally(RallyState& state, const InputFrame& input,
                const TerrainCollider& collider, float dt);

// Fetch the input recorded for a given sim step. Returns false past the end of
// the tape, which is how playback knows the run is over. Never extrapolates:
// a silently repeated last frame would let a replay drive on past the finish.
bool replay_input(const ReplayTape& tape, uint64_t step, InputFrame& out);

// Lay out a checkpoint route over the terrain.
//
// PLACEHOLDER. Currently emits `count` gates on a fixed-radius circle around
// the origin, dropped onto the terrain surface. It is a valid route and it is
// deterministic, but it ignores gradient, so gates can land on cliff faces.
// TODO(route generation ticket): grow the route along drivable gradient, with
// a minimum turn radius and a check that consecutive gates are reachable.
Route build_route(uint64_t seed, const TerrainCollider& collider, int count);

}  // namespace apricot
