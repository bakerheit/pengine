#include "game/rally.h"

#include <cmath>
#include <cstddef>

namespace apricot {
namespace {

// How far behind the start line the car is parked. Far enough that gate 0 is
// unambiguously in front of it, close enough that nobody is bored getting
// there.
constexpr float kGridSetback = 25.0f;

constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};

float ride_height(const VehicleTuning& tuning) {
    // The same height step_vehicle rests the chassis at. Derived from the
    // tuning rather than repeated as a constant, so a suspension change does
    // not leave every respawn buried to the axles.
    return tuning.suspension_rest + tuning.wheel_radius;
}

// A yaw-only orientation pointing the car's nose along `dir`.
//
// Transform::forward() is rotation * (0, 0, -1), so a yaw of theta about +Y
// aims the car at (-sin theta, 0, -cos theta) — hence the two negations.
glm::quat facing(glm::vec3 dir) {
    const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (len < 1e-5f) return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    const float yaw = std::atan2(-dir.x / len, -dir.z / len);
    return glm::angleAxis(yaw, kWorldUp);
}

// Rewind the recording tape to start HERE. Called once a lap begins, so that
// when a lap turns out to be the fastest, the tape sitting in `state` is
// already exactly that lap and nothing has to be trimmed after the fact.
void restart_tape(RallyState& state, uint64_t seed) {
    state.tape.version = kReplayTapeVersion;
    state.tape.seed = seed;
    // step_index has already been advanced past this step, so this is the step
    // that frames[0] will advance FROM.
    state.tape.start.step = state.step_index;
    state.tape.start.car = state.car;
    state.tape.start.checkpoint = state.next_checkpoint;
    state.tape.start.lap_time = state.timing.lap_time;
    state.tape.frames.clear();
}

void complete_lap(RallyState& state, uint64_t seed, double lap_time) {
    ++state.timing.lap;

    if (!state.best.valid || lap_time < state.best.lap_time) {
        state.best.valid = true;
        state.best.seed = seed;
        state.best.lap_time = lap_time;
        state.best.splits = state.timing.splits;
        // The tape was rewound when this lap started, so it holds this lap and
        // nothing else. No trimming, no offset, no "close enough".
        state.best.tape = state.tape;
        state.events.new_best = true;
    }

    if (!state.route.closed || state.timing.lap >= state.timing.target_laps) {
        state.timing.finished = true;
    }
}

void arm_lap(RallyState& state) {
    state.timing.splits.clear();
    state.timing.splits.push_back(0.0);
    state.events.tape_restarted = true;
}

// Walk the swept segment from -> to against the gates that are owed, in order,
// advancing the lap clock by the exact fraction of the step at which each line
// was cut.
void advance_gates(RallyState& state, uint64_t seed, glm::vec3 from,
                   glm::vec3 to, double dt_seconds) {
    const int count = static_cast<int>(state.route.checkpoints.size());

    double lap_clock = state.timing.lap_time;
    float consumed = 0.0f;  // fraction of this step already accounted for

    for (int guard = 0; guard < count; ++guard) {
        if (state.next_checkpoint < 0 || state.next_checkpoint >= count) break;

        const std::size_t idx = static_cast<std::size_t>(state.next_checkpoint);
        const GateCrossing hit = sweep_gate(state.route.checkpoints[idx], from, to);
        if (!hit.crossed) break;

        // Monotone clock. A gate cut earlier in the segment than one already
        // handled costs no time rather than negative time — which would let a
        // pathological step hand somebody a lap in the past.
        const float t = (hit.t > consumed) ? hit.t : consumed;
        lap_clock += static_cast<double>(t - consumed) * dt_seconds;
        consumed = t;

        const int crossed = state.next_checkpoint;
        state.events.gate_crossed = true;
        state.events.gate_index = crossed;

        if (crossed == 0) {
            if (!state.timing.lap_started) {
                state.timing.lap_started = true;
                state.events.lap_started = true;
            } else {
                complete_lap(state, seed, lap_clock);
                state.events.lap_completed = true;
            }
            lap_clock = 0.0;
            arm_lap(state);
            state.next_checkpoint = (count > 1) ? 1 : 0;
            if (state.timing.finished) break;
        } else {
            state.timing.splits.push_back(lap_clock);
            ++state.next_checkpoint;
            if (state.next_checkpoint >= count) {
                // A closed circuit loops back to the start line; a stage simply
                // owes nothing more, and `count` is deliberately out of range.
                state.next_checkpoint = state.route.closed ? 0 : count;
            }
        }

        // Only reachable on a degenerate single-gate route, where the gate
        // just crossed is also the gate now owed. Without this the same
        // crossing would be counted again on the same segment.
        if (state.next_checkpoint == crossed) break;
    }

    lap_clock += static_cast<double>(1.0f - consumed) * dt_seconds;
    state.timing.lap_time = lap_clock;
}

}  // namespace

CarPose grid_pose(const Route& route, const VehicleTuning& tuning,
                  const TerrainCollider& collider) {
    CarPose pose;
    if (route.checkpoints.empty()) return pose;

    const Checkpoint& start = route.checkpoints.front();
    const glm::vec3 back = start.position - start.forward * kGridSetback;

    // The setback point is not the gate, so its ground has to be asked for
    // rather than inherited — parking the car at the gate's height 25 m up the
    // road drops it through a hillside or hangs it in the air.
    pose.position =
        glm::vec3{back.x, collider.height(back.x, back.z) + ride_height(tuning),
                  back.z};
    pose.orientation = facing(start.forward);
    return pose;
}

CarPose respawn_pose(const Route& route, int next_checkpoint, bool lap_started,
                     const VehicleTuning& tuning) {
    CarPose pose;
    const int count = static_cast<int>(route.checkpoints.size());
    if (count == 0) return pose;

    // Nothing has been passed yet, so there is no gate to go back to.
    if (!lap_started && next_checkpoint == 0) {
        const Checkpoint& start = route.checkpoints.front();
        pose.position = start.position;
        pose.position.y += ride_height(tuning);
        pose.orientation = facing(start.forward);
        return pose;
    }

    const int owed = (next_checkpoint < 0 || next_checkpoint >= count)
                         ? 0
                         : next_checkpoint;
    const int last = (owed - 1 + count) % count;

    const Checkpoint& gate = route.checkpoints[static_cast<std::size_t>(last)];
    // Checkpoint::position.y is the surface by construction (game/route.h), so
    // this needs no collider and stays a pure function of the route.
    pose.position = gate.position;
    pose.position.y += ride_height(tuning);

    // Aimed at the gate that is actually owed, not along the gate's own
    // forward: after a spin on a hairpin those differ by half the corner, and
    // the driver wants to be pointed at the next line.
    const glm::vec3 target =
        route.checkpoints[static_cast<std::size_t>(owed)].position;
    const glm::vec3 to_next = target - gate.position;
    pose.orientation = (glm::dot(to_next, to_next) > 1e-6f) ? facing(to_next)
                                                            : facing(gate.forward);
    return pose;
}

void rally_reset(RallyState& state, const Route& route,
                 const TerrainCollider& collider) {
    state.route = route;
    state.next_checkpoint = 0;
    state.timing = LapTiming{};
    state.events = RallyEvents{};
    state.step_index = 0;
    state.recording = true;

    const CarPose pose = grid_pose(route, state.tuning, collider);
    state.car = VehicleState{};
    state.car.position = pose.position;
    state.car.orientation = pose.orientation;

    state.last_car_position = pose.position;
    state.has_last_position = true;

    state.conditions = conditions_at(collider.seed(), 0);
    restart_tape(state, collider.seed());
    // `best` is deliberately untouched: a fresh session on the same world
    // still races the ghost it loaded from disk.
}

void step_rally(RallyState& state, const InputFrame& input,
                const TerrainCollider& collider, float dt) {
    state.events = RallyEvents{};

    const uint64_t seed = collider.seed();

    // Record BEFORE stepping. The tape must hold the input that produced the
    // transition out of the current state; recording afterwards offsets the
    // whole tape by one step and the replay drifts from the moment it starts.
    if (state.recording) state.tape.frames.push_back(input);

    if (!state.has_last_position) {
        state.last_car_position = state.car.position;
        state.has_last_position = true;
    }

    // Respawn is applied as an INPUT EFFECT, before the integration, so it
    // lands on the tape and a replay reproduces it. A respawn poked in from
    // outside step_rally would be invisible to the tape, and the ghost would
    // carry serenely on into the tree the player just teleported away from.
    if (was_pressed(input, kBtnRespawn) && !state.route.checkpoints.empty()) {
        const CarPose pose = respawn_pose(state.route, state.next_checkpoint,
                                          state.timing.lap_started, state.tuning);
        state.car.position = pose.position;
        state.car.orientation = pose.orientation;
        state.car.velocity = glm::vec3{0.0f};
        state.car.angular_velocity = glm::vec3{0.0f};
        state.car.steer_angle = 0.0f;

        // The gate sweep must NOT span the teleport, or a respawn would read
        // as crossing every line between where the car was and where it went.
        state.last_car_position = pose.position;
        state.events.respawned = true;
    }

    state.conditions = conditions_at(seed, state.step_index);
    const VehicleTuning tuned = conditioned_tuning(state.tuning, state.conditions);

    state.car = step_vehicle(state.car, tuned, input, collider, dt);

    const glm::vec3 from = state.last_car_position;
    const glm::vec3 to = state.car.position;
    state.last_car_position = to;
    ++state.step_index;

    if (state.timing.finished) return;

    const double d = static_cast<double>(dt);
    state.timing.total_time += d;

    if (state.route.checkpoints.empty()) {
        state.timing.lap_time += d;
        return;
    }

    advance_gates(state, seed, from, to, d);

    // Rewound only after the step is completely settled, so the tape's start
    // state is the car as it will be at the top of the next step.
    if (state.events.tape_restarted && state.recording) {
        restart_tape(state, seed);
    }
}

void rally_begin_replay(RallyState& state, const ReplayTape& tape) {
    state.car = tape.start.car;
    state.next_checkpoint = tape.start.checkpoint;
    state.step_index = tape.start.step;

    state.timing = LapTiming{};
    state.timing.lap_time = tape.start.lap_time;
    // A tape recorded from a lap start owes gate 1, which means the line has
    // already been crossed. A tape that starts owing gate 0 has not.
    state.timing.lap_started = tape.start.checkpoint != 0;
    if (state.timing.lap_started) state.timing.splits.push_back(0.0);
    // The session clock is not a property of the lap, so a replay starts its
    // own at zero rather than inventing the drive that came before.
    state.timing.total_time = 0.0;

    state.last_car_position = tape.start.car.position;
    state.has_last_position = true;

    state.recording = false;
    state.tape = ReplayTape{};
    state.events = RallyEvents{};
    state.conditions = conditions_at(tape.seed, tape.start.step);
}

bool rally_replay_step(RallyState& state, const ReplayTape& tape,
                       const TerrainCollider& collider, float dt) {
    if (state.step_index < tape.start.step) return false;

    InputFrame in;
    if (!replay_input(tape, state.step_index - tape.start.step, in)) return false;

    step_rally(state, in, collider, dt);
    return true;
}

}  // namespace apricot
