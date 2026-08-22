#include "game/rally.h"

#include <cmath>

#include "core/rng.h"

namespace apricot {

void step_rally(RallyState& state, const InputFrame& input,
                const TerrainCollider& collider, float dt) {
    // Record BEFORE stepping. The tape must hold the input that produced the
    // transition out of the current state; recording afterwards offsets the
    // whole tape by one step and the replay drifts from the moment it starts.
    if (state.recording) state.tape.frames.push_back(input);

    state.car = step_vehicle(state.car, state.tuning, input, collider, dt);
    ++state.step_index;

    if (state.timing.finished) return;

    const double d = static_cast<double>(dt);
    state.timing.lap_time += d;
    state.timing.total_time += d;

    if (state.route.checkpoints.empty()) return;

    const std::size_t idx = static_cast<std::size_t>(state.next_checkpoint);
    if (idx >= state.route.checkpoints.size()) return;

    const Checkpoint& cp = state.route.checkpoints[idx];
    const glm::vec3 delta = state.car.position - cp.position;

    if (glm::dot(delta, delta) <= cp.radius * cp.radius) {
        // Reject a gate taken backwards. Without this, reversing over the last
        // checkpoint repeatedly is the fastest lap in the game.
        const bool right_way = glm::dot(state.car.velocity, cp.forward) >= 0.0f;
        if (right_way) {
            ++state.next_checkpoint;

            if (static_cast<std::size_t>(state.next_checkpoint) >=
                state.route.checkpoints.size()) {
                ++state.timing.lap;
                if (state.timing.best_lap == 0.0 ||
                    state.timing.lap_time < state.timing.best_lap) {
                    state.timing.best_lap = state.timing.lap_time;
                }
                state.timing.lap_time = 0.0;
                state.next_checkpoint = 0;

                if (!state.route.closed ||
                    state.timing.lap >= state.timing.target_laps) {
                    state.timing.finished = true;
                }
            }
        }
    }
}

bool replay_input(const ReplayTape& tape, uint64_t step, InputFrame& out) {
    if (step >= tape.frames.size()) return false;
    out = tape.frames[static_cast<std::size_t>(step)];
    return true;
}

Route build_route(uint64_t seed, const TerrainCollider& collider, int count) {
    Route route;
    route.seed = seed;
    route.closed = true;
    if (count <= 0) return route;

    // PLACEHOLDER LAYOUT — see the header. A plain circle, terrain-dropped.
    // TODO(route generation ticket): follow drivable gradient instead.
    constexpr float kRingRadius = 220.0f;
    const float step = 6.2831853f / static_cast<float>(count);

    route.checkpoints.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float a = step * static_cast<float>(i);
        const float x = std::cos(a) * kRingRadius;
        const float z = std::sin(a) * kRingRadius;

        Checkpoint cp;
        cp.position = glm::vec3{x, collider.height(x, z), z};
        // Tangent to the circle: the direction a car following the ring is
        // travelling as it passes through.
        cp.forward = glm::normalize(glm::vec3{-std::sin(a), 0.0f, std::cos(a)});
        route.checkpoints.push_back(cp);
    }
    return route;
}

}  // namespace apricot
