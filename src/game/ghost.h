#pragma once

#include "core/transform.h"
#include "game/rally.h"
#include "game/replay.h"

namespace apricot {

// The ghost car.
//
// It is NOT a second integrator fed the same numbers. It is a whole RallyState
// running in replay mode, driven by rally_replay_step — the player's own code
// path with a tape where the sticks would be.
//
// That is deliberate, and it was learned the hard way in this very file. The
// ghost started life as its own little loop calling step_vehicle directly, and
// it reproduced a lap perfectly right up until the tape contained a respawn:
// the player's step applies respawn from the latched button bit, the ghost's
// did not, and from that step on the two cars were driving different runs. The
// numbers still looked plausible. Any rule the rally ever grows — a penalty, a
// pit lane, a reset-on-roll — would have opened the same gap again.
//
// So there is one step function, and the ghost is a caller of it. A ghost
// cannot drift from the player's rules because it does not have any of its own.

struct GhostCar {
    ReplayTape tape;

    // The replayed world: same route, same collider, recording off. Read
    // `sim.car` for the pose, `sim.timing` for where the ghost is on its lap.
    RallyState sim;

    bool active = false;    // false until ghost_reset gives it a tape
    bool finished = false;  // true once the tape has run out
};

// Arm a ghost with a tape.
//
// `route` and the collider passed to step_ghost must be the world the tape was
// recorded on — the tape carries its seed so a caller can check. A tape with no
// frames leaves the ghost INACTIVE rather than active-and-instantly-finished,
// so a renderer never has to decide whether to draw a car that was never
// recorded.
void ghost_reset(GhostCar& ghost, const ReplayTape& tape, const Route& route,
                 const VehicleTuning& tuning);

// Advance the ghost by exactly one sim step. No-op once finished.
void step_ghost(GhostCar& ghost, const TerrainCollider& collider, float dt);

// Read-only pose for a renderer. Scale is always 1 — it is the same car.
Transform ghost_transform(const GhostCar& ghost);

// Convenience read for the renderer and the HUD. `sim.car` is right there and
// is not hidden; these exist so call sites read as what they mean.
inline const VehicleState& ghost_car(const GhostCar& g) { return g.sim.car; }
inline uint64_t ghost_step(const GhostCar& g) { return g.sim.step_index; }

}  // namespace apricot
