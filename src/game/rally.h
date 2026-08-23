#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/input_frame.h"
#include "game/best_lap.h"
#include "game/conditions.h"
#include "game/replay.h"
#include "game/route.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"

namespace apricot {

// Rally rules: a checkpoint route, lap timing, the replay tape, and respawn.
//
// Every clock in here counts SIM STEPS, never wall time. Lap times are
// accumulated dt and progress is indexed by step. That is what makes a lap
// time a property of the drive rather than of the machine it ran on — and it
// is what lets a replay reproduce a run exactly instead of approximately.
//
// THE LAP MODEL. Gate 0 is the start/finish line. The car begins set back
// behind it and has to cross it like any other gate; that crossing ARMS the
// lap and zeroes the clock. Gates 1..N-1 record splits. Crossing gate 0 again
// completes the lap and immediately arms the next one, so laps are flying
// laps and the clock never stops between them.

struct LapTiming {
    double lap_time = 0.0;    // seconds, the lap in progress
    double total_time = 0.0;  // seconds, the whole session
    int lap = 0;              // laps completed
    int target_laps = 3;
    bool finished = false;

    // False until gate 0 has been crossed for the first time. Until then
    // lap_time is not counting a lap, it is counting the roll-up to the line.
    bool lap_started = false;

    // splits[i] is the lap clock at the moment gate i was cut. Empty until the
    // lap is armed; from then on splits[0] is always 0.0 — the start line —
    // and the vector grows to the gate count over a clean lap. Indexed BY GATE
    // on purpose: a split vector that skipped gate 0 would need an off-by-one
    // at every read, and one of those reads would eventually be wrong.
    std::vector<double> splits;
};

// What happened during the last step_rally. Reset at the top of every step, so
// the app can react (a sound, a flash, re-arming the ghost) without diffing
// the whole state against a copy it kept.
struct RallyEvents {
    bool gate_crossed = false;
    int gate_index = -1;     // the gate that was cut, when gate_crossed

    bool lap_started = false;    // gate 0 cut for the first time
    bool lap_completed = false;  // a full lap finished this step
    bool new_best = false;       // ...and it was the fastest so far
    bool respawned = false;
    bool tape_restarted = false;  // the recording tape was rewound this step
};

// A place to put the car and a way to point it.
struct CarPose {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
};

struct RallyState {
    VehicleState car;

    // The car's BASE tuning. What actually reaches step_vehicle is this run
    // through conditioned_tuning() with the current weather — read `tuning`
    // for the setup, `conditions` for what the driver is fighting.
    VehicleTuning tuning;

    Route route;

    // Index into route.checkpoints of the next gate owed.
    int next_checkpoint = 0;

    LapTiming timing;
    Conditions conditions;
    RallyEvents events;

    // The tape for the lap IN PROGRESS. Rewound every time a lap starts, so
    // when a lap turns out to be the fastest there is already a clean tape of
    // exactly that lap sitting here to hand to `best`.
    ReplayTape tape;

    // Fastest lap on this world plus the ghost that drove it. Loaded from disk
    // at launch (game/best_lap.h) and replaced in place when it is beaten.
    BestLap best;

    // Where the car was at the end of the previous step. The gate test sweeps
    // FROM here, not from the car's pre-step position, so any displacement is
    // covered — a physics step, a big penetration correction, anything. That
    // is what makes a gate untunnellable rather than merely hard to tunnel.
    glm::vec3 last_car_position{0.0f};
    bool has_last_position = false;

    // Sim steps elapsed. The authoritative clock for everything here, and the
    // key the weather is drawn from.
    uint64_t step_index = 0;

    // While true, step_rally appends each input to `tape`. Turn it off when
    // playing a tape back, or the playback re-records itself.
    bool recording = true;
};

// Advance the rally by exactly one sim step. Pure in the same sense as
// step_vehicle: no wall clock, no globals, no random source.  `dt` is kSimDt.
//
// The world seed comes from `collider` — it is the one seed guaranteed to be
// the terrain's, and the weather has to agree with the ground it falls on.
//
// Consumes the LATCHED edge bits in `input` but does not clear them — the
// frame loop clears them once, after the last step of the frame. See
// core/fixed_step.h.
void step_rally(RallyState& state, const InputFrame& input,
                const TerrainCollider& collider, float dt);

// Put a state on the grid for `route`: clock zeroed, tape rewound, car set
// back behind gate 0 and aimed at it. Keeps `best` — a fresh session on the
// same world still races the ghost it loaded.
void rally_reset(RallyState& state, const Route& route,
                 const TerrainCollider& collider);

// Where the car sits before the lap is armed: behind gate 0, aimed through it,
// so the start line has to be crossed like any other gate. Needs the collider
// because the setback point is not the gate and does not share its ground.
CarPose grid_pose(const Route& route, const VehicleTuning& tuning,
                  const TerrainCollider& collider);

// Where a respawn puts the car: on the last gate it passed, aimed at the one
// it owes next. Before the first gate is passed there is no "last gate", so
// this is gate 0 itself.
//
// A pure function of the route — no collider — because Checkpoint::position.y
// is already the terrain surface (game/route.h). That is worth keeping: it
// makes a respawn's landing spot something a test can state exactly.
CarPose respawn_pose(const Route& route, int next_checkpoint, bool lap_started,
                     const VehicleTuning& tuning);

// Rewind a state to the start of a tape, ready to be fed by rally_replay_step.
// Turns recording OFF — a playback that re-records itself is a tape that grows
// every time you watch it.
//
// `state.route` and the collider passed to the steps must be the same world the
// tape was recorded on, or the replay is reproducing a different drive. The
// tape carries its seed for exactly that check.
void rally_begin_replay(RallyState& state, const ReplayTape& tape);

// Feed one tape frame through step_rally. Returns false when the tape has run
// out, which is how a caller knows the replay is over.
bool rally_replay_step(RallyState& state, const ReplayTape& tape,
                       const TerrainCollider& collider, float dt);

}  // namespace apricot
