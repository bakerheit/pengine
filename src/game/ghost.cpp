#include "game/ghost.h"

namespace apricot {

void ghost_reset(GhostCar& ghost, const ReplayTape& tape, const Route& route,
                 const VehicleTuning& tuning) {
    ghost.tape = tape;
    ghost.sim = RallyState{};
    ghost.sim.route = route;
    ghost.sim.tuning = tuning;
    rally_begin_replay(ghost.sim, ghost.tape);
    ghost.active = !ghost.tape.frames.empty();
    ghost.finished = false;
}

void step_ghost(GhostCar& ghost, const TerrainCollider& collider, float dt) {
    if (!ghost.active || ghost.finished) return;
    if (!rally_replay_step(ghost.sim, ghost.tape, collider, dt)) {
        ghost.finished = true;
        return;
    }

    // Flagged the moment the last frame is consumed, not one no-op call later.
    // A renderer asking "is the ghost still running" wants the answer on the
    // step it stopped, or it draws one frame of a car that has finished.
    const uint64_t end = ghost.tape.start.step + ghost.tape.frames.size();
    if (ghost.sim.step_index >= end) ghost.finished = true;
}

Transform ghost_transform(const GhostCar& ghost) {
    Transform t;
    t.position = ghost.sim.car.position;
    t.rotation = ghost.sim.car.orientation;
    return t;
}

}  // namespace apricot
