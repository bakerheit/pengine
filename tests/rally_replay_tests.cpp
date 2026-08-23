// THE HEADLINE SUITE. Determinism of the replay tape and of the ghost.
//
// The claim under test, stated exactly: given the same seed and the same tape,
// the sim reproduces the recorded drive BIT FOR BIT — every component of every
// vehicle state at every step, the gate sequence, and the lap clock. Not
// "closely". Not "to within a tolerance". The same bits.
//
// The recording phase deliberately uses NO teleports. Every position in it
// comes out of the real step_vehicle against the real TerrainCollider, so what
// the tape has to reproduce is a genuine integration over the height field and
// not a scripted path.
//
// HONEST LIMIT, stated once here rather than buried: physics/vehicle.h's step
// is an acknowledged placeholder — it integrates gravity and rests the chassis
// on the terrain, and throttle does nothing. So the recorded run is a coasting
// car, and it crosses the start line and travels on rather than completing
// three laps. What that DOES exercise is the whole determinism chain: the same
// step function, the same conditioned tuning, the same weather keyed to the
// same absolute step, the same gate sweep, the same clock. When the vehicle
// dynamics ticket lands, this suite starts covering a driven lap without a
// line of it changing. The negative controls below exist so that this suite
// cannot pass by accident in the meantime.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/fixed_step.h"
#include "core/rng.h"
#include "game/best_lap.h"
#include "game/rally.h"
#include "game/ghost.h"
#include "game/replay.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr int kGates = 8;
constexpr float kDt = static_cast<float>(kSimDt);
constexpr uint64_t kSeed = 0x5A1AD1CEB0A7ull;
constexpr uint64_t kOtherSeed = 0xC0FFEE1234ull;

// Steps to record. ~17 seconds of sim.
constexpr int kRecordSteps = 2000;

// Where the respawn button is pressed, late enough that most of the tape is
// motion and early enough that the replay has to reproduce the aftermath.
constexpr int kRespawnStep = 1750;

// A deterministic pile of stick movement. Keyed by hash rather than pulled
// from a stream, so frame N is the same whether or not frames 0..N-1 were
// asked for — the rule core/rng.h sets out.
InputFrame scripted_input(uint64_t seed, int step) {
    Rng rng = rng_at(seed, static_cast<int32_t>(step), 0, 77u);
    InputFrame f;
    f.steer = rng.range(-1.0f, 1.0f);
    f.throttle = rng.range(0.0f, 1.0f);
    f.brake = rng.chance(0.15f) ? rng.range(0.0f, 1.0f) : 0.0f;
    f.handbrake = rng.chance(0.08f) ? rng.range(0.0f, 1.0f) : 0.0f;
    f.look_dx = rng.range(-0.05f, 0.05f);
    f.look_dy = rng.range(-0.02f, 0.02f);
    f.held = rng.chance(0.2f) ? kBtnLookBack : 0u;
    f.pressed = (step == kRespawnStep) ? kBtnRespawn : 0u;
    return f;
}

// The input a driver would give, not a dice roll.
//
// scripted_input() alone is uniform random steer. Against the stub vehicle that
// barely moved, the car drifted forward anyway and threaded both gates. Against
// real dynamics (PENG-7) it just spins on the spot: the recorded run crossed
// ZERO gates, so the tape reproduced an integrator and nothing else -- exactly
// the "test would be vacuous" case the assertions below exist to catch.
//
// So the recording aims at the next checkpoint and keeps the random variety on
// top at reduced amplitude. The tape is still a plain InputFrame stream and the
// replay still runs open-loop off it; only the RECORDING is closed-loop, which
// is precisely how a real ghost lap is captured.
InputFrame driving_input(const RallyState& rally, uint64_t seed, int step) {
    InputFrame f = scripted_input(seed, step);

    const Route& route = rally.route;
    const std::size_t n = route.checkpoints.size();
    const Checkpoint& target = route.checkpoints[static_cast<std::size_t>(rally.next_checkpoint) % n];

    const glm::vec3 fwd = vehicle_forward(rally.car);
    glm::vec3 to_gate = target.position - rally.car.position;
    to_gate.y = 0.0f;
    if (glm::length(to_gate) > 1e-3f) {
        to_gate = glm::normalize(to_gate);
        const glm::vec3 flat_fwd =
            glm::normalize(glm::vec3{fwd.x, 0.0f, fwd.z});
        // Signed yaw error: cross product's Y tells us which way to turn.
        const float sin_err = flat_fwd.x * to_gate.z - flat_fwd.z * to_gate.x;
        const float cos_err = glm::dot(flat_fwd, to_gate);
        const float err = std::atan2(sin_err, cos_err);
        f.steer = glm::clamp(-err * 1.6f + 0.12f * f.steer, -1.0f, 1.0f);
        f.throttle = (cos_err > 0.3f) ? 0.85f : 0.45f;
        f.brake = 0.0f;
    }
    return f;
}

bool same_wheel(const WheelState& a, const WheelState& b) {
    return a.contact_point.x == b.contact_point.x &&
           a.contact_point.y == b.contact_point.y &&
           a.contact_point.z == b.contact_point.z &&
           a.contact_normal.x == b.contact_normal.x &&
           a.contact_normal.y == b.contact_normal.y &&
           a.contact_normal.z == b.contact_normal.z &&
           a.suspension_length == b.suspension_length && a.spin == b.spin &&
           a.grounded == b.grounded;
}

// Component by component, with ==. Not REQUIRE_NEAR: a tolerance here would
// quietly accept the exact class of drift this whole architecture exists to
// prevent.
bool same_vehicle(const VehicleState& a, const VehicleState& b) {
    if (a.position.x != b.position.x || a.position.y != b.position.y ||
        a.position.z != b.position.z) {
        return false;
    }
    if (a.orientation.w != b.orientation.w || a.orientation.x != b.orientation.x ||
        a.orientation.y != b.orientation.y || a.orientation.z != b.orientation.z) {
        return false;
    }
    if (a.velocity.x != b.velocity.x || a.velocity.y != b.velocity.y ||
        a.velocity.z != b.velocity.z) {
        return false;
    }
    if (a.angular_velocity.x != b.angular_velocity.x ||
        a.angular_velocity.y != b.angular_velocity.y ||
        a.angular_velocity.z != b.angular_velocity.z) {
        return false;
    }
    if (a.steer_angle != b.steer_angle) return false;
    if (a.engine_rpm != b.engine_rpm) return false;
    if (a.gear != b.gear) return false;
    for (int i = 0; i < kWheelCount; ++i) {
        const std::size_t w = static_cast<std::size_t>(i);
        if (!same_wheel(a.wheels[w], b.wheels[w])) return false;
    }
    return true;
}

// One recorded session, plus the ground truth to compare a replay against.
struct Recording {
    Route route;
    ReplayTape tape;

    // states[i] is the car AFTER the step that took step_index from
    // start.step + i to start.step + i + 1 — i.e. the state frames[i] produced.
    std::vector<VehicleState> states;

    // Where the recorded run ended up.
    double final_lap_time = 0.0;
    int final_checkpoint = 0;
    std::vector<double> final_splits;
    int gates_crossed = 0;
    float top_speed = 0.0f;
};

Recording record_a_run(uint64_t seed, const TerrainCollider& collider) {
    Recording rec;
    rec.route = build_route(seed, collider, kGates);
    REQUIRE(route_ok(rec.route, kGates));

    RallyState rally;
    rally_reset(rally, rec.route, collider);

    // Line the car up behind the start and give it a shove. This is the ONLY
    // thing the harness does to the car, and it happens BEFORE the tape's
    // start state is captured — everything the tape has to reproduce is a
    // consequence of step_vehicle from that point on.
    //
    // Aimed down the chord from gate 0 to gate 1 rather than along gate 0's
    // own facing, so the coast threads both lines: the replay then has to
    // reproduce an arming crossing AND a split, not just the one.
    const Checkpoint& start = rec.route.checkpoints[0];
    const Checkpoint& second = rec.route.checkpoints[1];
    glm::vec3 aim = second.position - start.position;
    aim.y = 0.0f;
    aim = glm::normalize(aim);

    // Put it on the GROUND at the new spot, not at gate 0's altitude.
    //
    // This used to keep start.position.y while moving 120 m away, which on real
    // terrain (PENG-6) leaves the car buried in a hill or dropped from height:
    // no wheel ever touches, so throttle does nothing and the run coasted at
    // its initial shove and crossed no gates.
    const glm::vec3 launch = start.position - aim * 120.0f;
    rally.car = spawn_vehicle(rally.tuning, collider, launch.x, launch.z,
                              std::atan2(aim.x, aim.z));
    rally.car.velocity = aim * 45.0f;  // 162 km/h
    rally.last_car_position = rally.car.position;

    for (int i = 0; i < kRecordSteps; ++i) {
        const InputFrame in = driving_input(rally, seed, i);
        step_rally(rally, in, collider, kDt);

        if (rally.events.gate_crossed) ++rec.gates_crossed;

        const float speed = glm::length(rally.car.velocity);
        if (speed > rec.top_speed) rec.top_speed = speed;

        // Collect ground truth only once the tape being kept has started.
        if (!rally.tape.frames.empty()) rec.states.push_back(rally.car);
        // A tape restart throws away everything recorded before it, so the
        // ground truth has to be thrown away with it or the two fall out of
        // step by exactly the amount that hides a bug.
        if (rally.events.tape_restarted) rec.states.clear();
    }

    rec.tape = rally.tape;
    rec.final_lap_time = rally.timing.lap_time;
    rec.final_checkpoint = rally.next_checkpoint;
    rec.final_splits = rally.timing.splits;
    return rec;
}

// Replay a tape through a fresh RallyState and hand back where it ended up.
struct Playback {
    int steps = 0;
    VehicleState car;
    double lap_time = 0.0;
    int checkpoint = 0;
    std::vector<double> splits;
    std::vector<VehicleState> states;
};

Playback replay(const Route& route, const ReplayTape& tape,
                const TerrainCollider& collider) {
    RallyState state;
    state.route = route;
    rally_begin_replay(state, tape);

    Playback out;
    while (rally_replay_step(state, tape, collider, kDt)) {
        out.states.push_back(state.car);
        ++out.steps;
    }
    out.car = state.car;
    out.lap_time = state.timing.lap_time;
    out.checkpoint = state.next_checkpoint;
    out.splits = state.timing.splits;
    return out;
}

// ---------------------------------------------------------------------------

void test_replay_reproduces_the_run_bit_for_bit() {
    const TerrainCollider collider(kSeed);
    const Recording rec = record_a_run(kSeed, collider);

    std::printf(
        "      recorded %zu frames from step %llu; %d gate crossings, "
        "top speed %.1f m/s\n",
        rec.tape.frames.size(),
        static_cast<unsigned long long>(rec.tape.start.step), rec.gates_crossed,
        static_cast<double>(rec.top_speed));

    REQUIRE_MSG(rec.tape.frames.size() > 100u, "the tape is long enough to mean anything",
                "record");
    REQUIRE_MSG(rec.gates_crossed >= 2,
                "the recorded run crossed the line AND a gate beyond it, so "
                "the sequencing and the split clock are both inside what is "
                "being reproduced rather than just the integrator",
                "record");
    REQUIRE_MSG(rec.final_splits.size() >= 2u,
                "a split was banked inside the kept tape", "record");
    REQUIRE(rec.final_checkpoint >= 2);
    REQUIRE(rec.states.size() == rec.tape.frames.size());
    REQUIRE(rec.tape.seed == kSeed);
    REQUIRE(rec.tape.version == kReplayTapeVersion);

    const Playback play = replay(rec.route, rec.tape, collider);

    REQUIRE_MSG(play.steps == static_cast<int>(rec.tape.frames.size()),
                "the replay ran exactly as many steps as were recorded",
                "replay");

    // Every step, not just the last one. An end-state comparison passes even
    // when the trajectory wandered off and happened to come back.
    for (std::size_t i = 0; i < rec.states.size(); ++i) {
        REQUIRE_MSG(same_vehicle(rec.states[i], play.states[i]),
                    "replayed vehicle state is bit-identical at every step",
                    "step");
    }

    // The finishing state and the lap clock, exactly.
    REQUIRE(same_vehicle(rec.states.back(), play.car));
    REQUIRE_MSG(play.lap_time == rec.final_lap_time,
                "lap time matches bit-for-bit", "replay");
    REQUIRE_MSG(play.checkpoint == rec.final_checkpoint,
                "the same gate is owed at the end", "replay");
    REQUIRE(play.splits.size() == rec.final_splits.size());
    for (std::size_t i = 0; i < play.splits.size(); ++i) {
        REQUIRE_MSG(play.splits[i] == rec.final_splits[i],
                    "every split matches bit-for-bit", "split");
    }

    std::printf("      %d steps replayed, lap clock %.17g on both sides\n",
                play.steps, play.lap_time);
    apricot_test::pass("a recorded tape replays to the same finishing state and lap time");
}

void test_ghost_and_player_agree() {
    const TerrainCollider collider(kSeed);
    const Recording rec = record_a_run(kSeed, collider);

    GhostCar ghost;
    VehicleTuning tuning;  // the same defaults the recording used
    ghost_reset(ghost, rec.tape, rec.route, tuning);

    REQUIRE(ghost.active);
    REQUIRE(ghost_step(ghost) == rec.tape.start.step);

    std::size_t i = 0;
    while (!ghost.finished) {
        step_ghost(ghost, collider, kDt);
        REQUIRE_MSG(i < rec.states.size(), "the ghost does not outrun the tape",
                    "ghost");
        REQUIRE_MSG(same_vehicle(rec.states[i], ghost_car(ghost)),
                    "ghost trajectory is bit-identical to the player's",
                    "ghost step");

        const Transform tf = ghost_transform(ghost);
        REQUIRE(tf.position.x == ghost_car(ghost).position.x);
        REQUIRE(tf.position.y == ghost_car(ghost).position.y);
        REQUIRE(tf.position.z == ghost_car(ghost).position.z);
        REQUIRE(tf.rotation.w == ghost_car(ghost).orientation.w);
        ++i;
    }

    REQUIRE_MSG(i == rec.states.size(), "the ghost ran the whole tape and stopped",
                "ghost");

    // The ghost's own lap clock agrees with the player's too — it is running
    // the same rules, not just the same integrator.
    REQUIRE_MSG(ghost.sim.timing.lap_time == rec.final_lap_time,
                "the ghost's lap clock matches the recorded one", "ghost");
    REQUIRE(ghost.sim.next_checkpoint == rec.final_checkpoint);

    // It stays stopped, and stops moving when it does.
    const VehicleState parked = ghost_car(ghost);
    step_ghost(ghost, collider, kDt);
    REQUIRE(same_vehicle(parked, ghost_car(ghost)));

    std::printf("      ghost matched the player for all %zu steps\n", i);
    apricot_test::pass("ghost and player fed the same tape produce identical trajectories");
}

void test_a_different_world_gives_a_different_drive() {
    // The negative control. Without it, every assertion above would still pass
    // if the vehicle step ignored the terrain entirely.
    const TerrainCollider collider(kSeed);
    const TerrainCollider elsewhere(kOtherSeed);
    const Recording rec = record_a_run(kSeed, collider);

    const Playback here = replay(rec.route, rec.tape, collider);
    const Playback there = replay(rec.route, rec.tape, elsewhere);

    REQUIRE(here.steps == there.steps);
    bool diverged = false;
    for (std::size_t i = 0; i < here.states.size(); ++i) {
        if (!same_vehicle(here.states[i], there.states[i])) {
            diverged = true;
            break;
        }
    }
    REQUIRE_MSG(diverged,
                "the same tape on different terrain must NOT reproduce the run",
                "control");
    apricot_test::pass("negative control: a different seed diverges");
}

void test_the_tape_is_actually_consumed() {
    // The other negative control, and the one that matters most while the
    // vehicle step is a placeholder.
    //
    // Today that step reads input.steer (and nothing else) — steer_angle is
    // rate-limited toward it, which is real code, while throttle and brake do
    // not move the car at all. So a perturbed tape CANNOT be expected to move
    // the car yet, and asserting that it did would be asserting a fiction.
    // What can be asserted, and is, is that changing one byte of the tape
    // changes the state that comes out: the tape is genuinely being read.
    const TerrainCollider collider(kSeed);
    const Recording rec = record_a_run(kSeed, collider);

    ReplayTape tampered = rec.tape;
    REQUIRE(tampered.frames.size() > 10u);
    tampered.frames[5].steer = -tampered.frames[5].steer - 0.5f;

    const Playback clean = replay(rec.route, rec.tape, collider);
    const Playback dirty = replay(rec.route, tampered, collider);

    bool differs = false;
    for (std::size_t i = 0; i < clean.states.size(); ++i) {
        if (clean.states[i].steer_angle != dirty.states[i].steer_angle) {
            differs = true;
            break;
        }
    }
    REQUIRE_MSG(differs, "one altered input frame changes the replayed state",
                "control");
    apricot_test::pass("negative control: altering the tape alters the replay");
}

void test_respawn_is_on_the_tape() {
    // Respawn is applied inside step_rally from the latched button bit, so it
    // rides on the tape like any other input. If it were poked in from outside,
    // the replay would sail straight past the point the player teleported.
    const TerrainCollider collider(kSeed);
    const Recording rec = record_a_run(kSeed, collider);

    const uint64_t respawn_abs = static_cast<uint64_t>(kRespawnStep);
    REQUIRE_MSG(respawn_abs >= rec.tape.start.step,
                "the respawn press is inside the kept tape", "respawn");

    const std::size_t f =
        static_cast<std::size_t>(respawn_abs - rec.tape.start.step);
    REQUIRE(f < rec.tape.frames.size());
    REQUIRE_MSG(was_pressed(rec.tape.frames[f], kBtnRespawn),
                "the respawn press was recorded", "respawn");

    // The recorded run's own state at that step is a hard teleport...
    const glm::vec3 moved =
        rec.states[f].position - rec.states[f > 0 ? f - 1 : 0].position;
    REQUIRE_MSG(glm::length(moved) > 5.0f,
                "the respawn really did move the car a long way in one step",
                "respawn");

    // ...and the replay reproduces it, which the bit-for-bit test above
    // already covers; assert it here too so a failure names the cause.
    const Playback play = replay(rec.route, rec.tape, collider);
    REQUIRE(same_vehicle(rec.states[f], play.states[f]));

    std::printf("      respawn at tape frame %zu moved the car %.1f m\n", f,
                static_cast<double>(glm::length(moved)));
    apricot_test::pass("a respawn rides on the tape and replays exactly");
}

void test_best_lap_survives_a_round_trip_through_disk() {
    const TerrainCollider collider(kSeed);
    const Recording rec = record_a_run(kSeed, collider);

    BestLap saved;
    saved.valid = true;
    saved.seed = kSeed;
    saved.lap_time = 91.2345678901234;
    saved.splits = {0.0, 11.5, 24.25, 37.125};
    saved.tape = rec.tape;

    const std::string path = "rally_best_lap_roundtrip.tmp";
    std::remove(path.c_str());

    REQUIRE(save_best_lap(path, saved));

    BestLap loaded;
    REQUIRE_MSG(load_best_lap(path, kSeed, loaded), "the record loads back",
                "disk");
    REQUIRE(loaded.valid);
    REQUIRE(loaded.seed == kSeed);
    REQUIRE_MSG(loaded.lap_time == saved.lap_time,
                "lap time survives the round trip exactly", "disk");
    REQUIRE(loaded.splits.size() == saved.splits.size());
    for (std::size_t i = 0; i < saved.splits.size(); ++i) {
        REQUIRE(loaded.splits[i] == saved.splits[i]);
    }

    REQUIRE(loaded.tape.version == saved.tape.version);
    REQUIRE(loaded.tape.seed == saved.tape.seed);
    REQUIRE(loaded.tape.start.step == saved.tape.start.step);
    REQUIRE(loaded.tape.start.checkpoint == saved.tape.start.checkpoint);
    REQUIRE(loaded.tape.start.lap_time == saved.tape.start.lap_time);
    REQUIRE(same_vehicle(loaded.tape.start.car, saved.tape.start.car));
    REQUIRE(loaded.tape.frames.size() == saved.tape.frames.size());

    // The point of the round trip: the tape off disk drives the same lap.
    const Playback from_memory = replay(rec.route, saved.tape, collider);
    const Playback from_disk = replay(rec.route, loaded.tape, collider);
    REQUIRE(from_memory.steps == from_disk.steps);
    for (std::size_t i = 0; i < from_memory.states.size(); ++i) {
        REQUIRE_MSG(same_vehicle(from_memory.states[i], from_disk.states[i]),
                    "a tape off disk replays identically to one in memory",
                    "disk step");
    }
    REQUIRE(from_disk.lap_time == from_memory.lap_time);

    // A best lap from another world is refused, not loaded and believed.
    BestLap wrong_world;
    REQUIRE_MSG(!load_best_lap(path, kOtherSeed, wrong_world),
                "a record from a different seed is refused", "disk");
    REQUIRE(!wrong_world.valid);

    std::remove(path.c_str());

    // A missing file is a clean false, not a crash and not a half-record.
    BestLap missing;
    REQUIRE(!load_best_lap(path, kSeed, missing));
    REQUIRE(!missing.valid);

    apricot_test::pass("a best lap round-trips through disk and replays identically");
}

void test_truncated_and_corrupt_files_are_refused() {
    const TerrainCollider collider(kSeed);
    const Recording rec = record_a_run(kSeed, collider);

    BestLap saved;
    saved.valid = true;
    saved.seed = kSeed;
    saved.lap_time = 42.0;
    saved.splits = {0.0, 10.0};
    saved.tape = rec.tape;

    const std::string path = "rally_best_lap_corrupt.tmp";
    std::remove(path.c_str());
    REQUIRE(save_best_lap(path, saved));

    // How big is a good file?
    std::FILE* f = std::fopen(path.c_str(), "rb");
    REQUIRE(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    const long full = std::ftell(f);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(full));
    std::fseek(f, 0, SEEK_SET);
    REQUIRE(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
    std::fclose(f);
    REQUIRE(full > 64);

    // Chop it at several points. Every one of them must refuse.
    const long cuts[] = {0, 4, 8, 20, 64, full / 2, full - 1};
    for (const long cut : cuts) {
        std::FILE* out = std::fopen(path.c_str(), "wb");
        REQUIRE(out != nullptr);
        if (cut > 0) {
            std::fwrite(bytes.data(), 1, static_cast<std::size_t>(cut), out);
        }
        std::fclose(out);

        BestLap loaded;
        REQUIRE_MSG(!load_best_lap(path, kSeed, loaded),
                    "a truncated record is refused", "cut");
        REQUIRE_MSG(!loaded.valid, "and nothing partial is handed back", "cut");
    }

    // A good file with the magic vandalised.
    std::FILE* out = std::fopen(path.c_str(), "wb");
    REQUIRE(out != nullptr);
    bytes[2] = 'X';
    std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    BestLap bad_magic;
    REQUIRE(!load_best_lap(path, kSeed, bad_magic));

    std::remove(path.c_str());
    apricot_test::pass("truncated and corrupt records are refused, never half-loaded");
}

}  // namespace

int main() {
    test_replay_reproduces_the_run_bit_for_bit();
    test_ghost_and_player_agree();
    test_a_different_world_gives_a_different_drive();
    test_the_tape_is_actually_consumed();
    test_respawn_is_on_the_tape();
    test_best_lap_survives_a_round_trip_through_disk();
    test_truncated_and_corrupt_files_are_refused();
    return apricot_test::done("rally_replay_tests");
}
