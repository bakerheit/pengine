// Gate sequencing, split timing, tunnelling and respawn.
//
// A note on how the car moves in here. physics/vehicle.h says plainly that its
// step is a placeholder: it integrates gravity, rests the chassis on the
// terrain, and throttle does nothing at all. A lap cannot be driven with
// inputs yet. These tests are about the RALLY RULES, so the harness supplies
// the motion — either by giving the car a velocity and letting the real step
// carry it, or by placing it and letting step_rally sweep from where it was.
//
// Nothing here reaches into the crossing logic. The sweep, the ordering, the
// clock and the respawn are exactly the shipping ones; only the thing pushing
// the car is a stand-in, and it is a stand-in for a component that does not
// exist yet rather than one that is being avoided.

#include <cmath>
#include <cstdio>

#include "core/fixed_step.h"
#include "core/rng.h"
#include "core/transform.h"
#include "game/rally.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr int kGates = 8;
constexpr float kDt = static_cast<float>(kSimDt);

// The trigger radius the route used to carry when a gate was a sphere. Kept
// here as the yardstick the tunnelling test measures against: if both ends of
// a step are further from the gate centre than this, a proximity check would
// have seen nothing at all.
constexpr float kOldProximityRadius = 12.0f;

uint64_t nth_seed(int i) {
    return splitmix64_mix(0x6A7E5EEDull + static_cast<uint64_t>(i));
}

struct World {
    uint64_t seed;
    TerrainCollider collider;
    Route route;
    RallyState rally;

    explicit World(int i)
        : seed(nth_seed(i)), collider(seed), route(), rally() {
        route = build_route(seed, collider, kGates);
        REQUIRE(route_ok(route, kGates));
        rally_reset(rally, route, collider);
    }

    const Checkpoint& gate(int g) const {
        return route.checkpoints[static_cast<std::size_t>(g)];
    }
};

// Put the car somewhere and take one step. step_rally sweeps from where the
// car ENDED the previous step, so the jump is part of the swept segment — the
// same path a physics penetration correction would take.
void place_and_step(World& w, glm::vec3 p) {
    w.rally.car.position = p;
    step_rally(w.rally, InputFrame{}, w.collider, kDt);
}

// Approach a gate and pass through it, at `back` metres before and `front`
// metres after. Two steps, and the crossing lands on the second.
void drive_through(World& w, int g, float back = 4.0f, float front = 4.0f) {
    const Checkpoint& cp = w.gate(g);
    place_and_step(w, cp.position - cp.forward * back);
    place_and_step(w, cp.position + cp.forward * front);
}

void test_gates_must_be_taken_in_order() {
    World w(0);

    REQUIRE(w.rally.next_checkpoint == 0);
    REQUIRE(!w.rally.timing.lap_started);

    // Jump the car clean through gate 3's line while gate 0 is what is owed.
    drive_through(w, 3);
    REQUIRE_MSG(w.rally.next_checkpoint == 0,
                "a gate taken out of order does nothing", "gate 3 first");
    REQUIRE(!w.rally.timing.lap_started);
    REQUIRE(w.rally.timing.splits.empty());

    // Now take them properly.
    drive_through(w, 0);
    REQUIRE_MSG(w.rally.timing.lap_started, "gate 0 arms the lap", "gate 0");
    REQUIRE(w.rally.next_checkpoint == 1);

    for (int g = 1; g <= 2; ++g) {
        drive_through(w, g);
        REQUIRE_MSG(w.rally.next_checkpoint == g + 1, "gate advances in order",
                    "gate");
    }

    // Gate 3 is owed again despite having been driven through earlier: the
    // earlier pass bought nothing and was not banked.
    REQUIRE(w.rally.next_checkpoint == 3);
    REQUIRE(w.rally.timing.splits.size() == 3u);  // gates 0, 1, 2

    drive_through(w, 3);
    REQUIRE(w.rally.next_checkpoint == 4);
    REQUIRE(w.rally.timing.splits.size() == 4u);

    apricot_test::pass("gates sequence in order; an out-of-order crossing is ignored");
}

void test_reversing_over_a_line_is_worth_nothing() {
    World w(1);

    const Checkpoint& cp = w.gate(0);

    // Get to the far side WITHOUT using the gate: swing wide of the posts,
    // then come back to the centre line. (Going straight there would be a
    // perfectly good forward crossing, which is the point of the gate.)
    place_and_step(w, cp.position + cp.forward * 6.0f + cp.right * 60.0f);
    REQUIRE_MSG(!w.rally.timing.lap_started, "going round the posts is not a gate",
                "detour");
    place_and_step(w, cp.position + cp.forward * 6.0f);
    REQUIRE(!w.rally.timing.lap_started);

    // Now back through it, the wrong way.
    place_and_step(w, cp.position - cp.forward * 6.0f);

    REQUIRE_MSG(w.rally.next_checkpoint == 0, "backwards through the line does nothing",
                "reverse");
    REQUIRE(!w.rally.timing.lap_started);

    // And forwards through it still works afterwards.
    place_and_step(w, cp.position + cp.forward * 6.0f);
    REQUIRE(w.rally.timing.lap_started);
    REQUIRE(w.rally.next_checkpoint == 1);

    apricot_test::pass("a gate taken backwards does not count");
}

void test_wide_of_the_posts_does_not_count() {
    World w(2);
    const Checkpoint& cp = w.gate(0);

    // Cross the gate's PLANE, but well outside its width. An infinite plane
    // would take this; a gate must not.
    const float wide = cp.half_width + 25.0f;
    place_and_step(w, cp.position - cp.forward * 6.0f + cp.right * wide);
    place_and_step(w, cp.position + cp.forward * 6.0f + cp.right * wide);

    REQUIRE_MSG(!w.rally.timing.lap_started,
                "crossing the plane outside the posts is not a gate", "wide");
    REQUIRE(w.rally.next_checkpoint == 0);

    apricot_test::pass("crossing the gate plane wide of the posts is ignored");
}

void test_no_tunnelling_at_speed() {
    World w(3);
    const Checkpoint& cp = w.gate(0);

    // One step that starts well before the line and ends well past it. The
    // distance is what matters: both ends are outside any trigger sphere a
    // proximity check would plausibly have used.
    const float reach = 40.0f;
    place_and_step(w, cp.position - cp.forward * reach);

    const glm::vec3 before = w.rally.car.position;
    place_and_step(w, cp.position + cp.forward * reach);
    const glm::vec3 after = w.rally.car.position;

    const float d_before = glm::length(before - cp.position);
    const float d_after = glm::length(after - cp.position);
    std::printf("      step spanned %.1f m; endpoints %.1f m / %.1f m from the gate\n",
                static_cast<double>(glm::length(after - before)),
                static_cast<double>(d_before), static_cast<double>(d_after));

    REQUIRE_MSG(d_before > kOldProximityRadius && d_after > kOldProximityRadius,
                "a proximity trigger would have seen neither end of this step",
                "tunnelling");
    REQUIRE_MSG(w.rally.timing.lap_started,
                "the swept crossing counts it anyway", "tunnelling");
    REQUIRE(w.rally.next_checkpoint == 1);
    REQUIRE(w.rally.events.gate_crossed);

    apricot_test::pass("a car moved across a gate between steps is still counted");
}

void test_splits_land_on_the_line_not_the_step() {
    World w(4);

    // Arm the lap, then approach gate 1 so the crossing falls at a known
    // fraction of the step. The gate plane's normal is horizontal, so the
    // terrain snapping the car's Y cannot move the fraction at all — the only
    // slack is float cancellation in the plane distance, which at several
    // hundred metres from the origin is a few parts in 10^6 of one step. One
    // microsecond of tolerance on a split is a tight claim, not a loose one.
    // Was 1e-6, which held while step_vehicle was a stub that barely moved the
    // car: place_and_step() put it exactly at -1 and +3, so the crossing
    // fraction was exactly 1/4. With real dynamics (PENG-7) the car also
    // integrates its own velocity and gravity during that step, so the sweep
    // endpoints are a few millimetres from where they were placed and the true
    // fraction is no longer exactly 1/4. Measured discrepancy: 2.9e-6 s.
    //
    // 1e-5 s is still a tight claim — a split pinned to about one eight-hundredth
    // of a sim step, and four orders of magnitude finer than a lap time anyone
    // can perceive.
    constexpr double kSplitEps = 1e-5;

    drive_through(w, 0);
    REQUIRE(w.rally.next_checkpoint == 1);

    const Checkpoint& cp = w.gate(1);
    place_and_step(w, cp.position - cp.forward * 1.0f);

    const double clock_before = w.rally.timing.lap_time;
    place_and_step(w, cp.position + cp.forward * 3.0f);

    // s0 = -1, s1 = +3  =>  t = 1 / 4.
    const double expected_split =
        clock_before + 0.25 * static_cast<double>(kDt);
    const double expected_lap_time =
        clock_before + 1.00 * static_cast<double>(kDt);

    REQUIRE(w.rally.timing.splits.size() == 2u);
    REQUIRE_NEAR(w.rally.timing.splits[1], expected_split, kSplitEps);
    REQUIRE_NEAR(w.rally.timing.lap_time, expected_lap_time, kSplitEps);

    // The split is a quarter of a step EARLIER than the step boundary — which
    // is the whole difference between timing the line and timing the tick.
    REQUIRE(w.rally.timing.splits[1] < w.rally.timing.lap_time);

    // splits[0] is the start line and is always zero.
    REQUIRE(w.rally.timing.splits[0] == 0.0);

    apricot_test::pass("a split is the moment the line was cut, not the end of the step");
}

void test_a_full_lap_completes_and_rearms() {
    World w(5);

    drive_through(w, 0);
    for (int g = 1; g < kGates; ++g) drive_through(w, g);
    REQUIRE_MSG(w.rally.next_checkpoint == 0, "the loop comes back to the line",
                "lap");
    REQUIRE(w.rally.timing.lap == 0);
    REQUIRE(w.rally.timing.splits.size() == static_cast<std::size_t>(kGates));

    const double lap_clock = w.rally.timing.lap_time;
    drive_through(w, 0);

    REQUIRE_MSG(w.rally.timing.lap == 1, "crossing the line completes the lap",
                "lap");
    REQUIRE(w.rally.events.lap_completed);
    REQUIRE(w.rally.events.new_best);
    REQUIRE(w.rally.best.valid);
    REQUIRE(w.rally.best.seed == w.seed);
    // drive_through() is two steps, and the line is cut somewhere inside the
    // second, so the banked lap is the clock at the last gate plus between one
    // and two steps. Bounded on BOTH sides: a lap time that merely "looks
    // plausible" is how an off-by-one lap boundary survives a test suite.
    REQUIRE(w.rally.best.lap_time > lap_clock);
    REQUIRE(w.rally.best.lap_time < lap_clock + 2.0 * static_cast<double>(kDt));
    REQUIRE_MSG(w.rally.best.splits.size() == static_cast<std::size_t>(kGates),
                "the best lap keeps one split per gate", "lap");

    // Splits climb, start at the line, and all land before the flag.
    REQUIRE(w.rally.best.splits[0] == 0.0);
    for (std::size_t i = 1; i < w.rally.best.splits.size(); ++i) {
        REQUIRE_MSG(w.rally.best.splits[i] > w.rally.best.splits[i - 1],
                    "splits increase down the lap", "split");
        REQUIRE_MSG(w.rally.best.splits[i] < w.rally.best.lap_time,
                    "every split lands before the lap time", "split");
    }

    // The next lap is armed immediately — flying laps, the clock never stops.
    REQUIRE(w.rally.timing.lap_started);
    REQUIRE(w.rally.next_checkpoint == 1);
    REQUIRE_MSG(w.rally.timing.lap_time < static_cast<double>(kDt),
                "the new lap's clock starts from the crossing, not the step",
                "lap");
    REQUIRE(w.rally.timing.splits.size() == 1u);

    apricot_test::pass("a full lap completes, banks a best, and arms the next");
}

void test_respawn_returns_to_the_last_gate_with_the_clock_running() {
    World w(6);

    drive_through(w, 0);
    for (int g = 1; g <= 2; ++g) drive_through(w, g);
    REQUIRE(w.rally.next_checkpoint == 3);

    // Wander off and get lost.
    place_and_step(w, w.gate(3).position + w.gate(3).right * 300.0f);

    const double clock_before = w.rally.timing.lap_time;
    const double total_before = w.rally.timing.total_time;
    const int lap_before = w.rally.timing.lap;
    const std::size_t splits_before = w.rally.timing.splits.size();

    InputFrame respawn{};
    respawn.pressed = kBtnRespawn;
    step_rally(w.rally, respawn, w.collider, kDt);

    REQUIRE(w.rally.events.respawned);

    const CarPose expect = respawn_pose(w.route, 3, true, w.rally.tuning, w.collider);

    // The car is put back on gate 2 — the last one it passed.
    //
    // Not `==` any more. step_rally applies the respawn as an input effect and
    // then INTEGRATES that same step, so the car has one step of real motion on
    // it by the time we look. That was invisible while step_vehicle was a stub;
    // with PENG-7's dynamics it is a few millimetres of settling. The claim
    // being made is "put back on the gate", and a millimetre band says that
    // just as strictly as an exact compare did, without asserting that physics
    // does not happen.
    const float placed_dx = std::fabs(w.rally.car.position.x - expect.position.x);
    const float placed_dz = std::fabs(w.rally.car.position.z - expect.position.z);
    std::printf("      (respawn landed %.5f m / %.5f m from the pose, after one "
                "integrated step)\n",
                static_cast<double>(placed_dx), static_cast<double>(placed_dz));
    REQUIRE_NEAR(static_cast<double>(w.rally.car.position.x),
                 static_cast<double>(expect.position.x), 1e-3);
    REQUIRE_NEAR(static_cast<double>(w.rally.car.position.z),
                 static_cast<double>(expect.position.z), 1e-3);
    REQUIRE_NEAR(static_cast<double>(w.rally.car.position.y),
                 static_cast<double>(expect.position.y), 1e-4);
    REQUIRE_NEAR(static_cast<double>(w.gate(2).position.x),
                 static_cast<double>(expect.position.x), 1e-6);

    // ...facing the gate it still owes.
    Transform tf;
    tf.rotation = w.rally.car.orientation;
    glm::vec3 want = w.gate(3).position - w.gate(2).position;
    want.y = 0.0f;
    want = glm::normalize(want);
    // Compare the HEADING, not the full forward vector. respawn_pose now tilts
    // the car onto the slope it lands on (see rally.h), so on a sloping gate its
    // forward legitimately has a vertical component and can never dot to 1.0
    // against a horizontal target. What is being claimed is "pointed at the next
    // gate", which is a question about heading.
    glm::vec3 got = tf.forward();
    got.y = 0.0f;
    got = glm::normalize(got);
    REQUIRE_NEAR(static_cast<double>(glm::dot(got, want)), 1.0, 1e-5);

    // The clock kept running, by exactly one step and no more.
    REQUIRE_NEAR(w.rally.timing.lap_time,
                 clock_before + static_cast<double>(kDt), 1e-12);
    REQUIRE_NEAR(w.rally.timing.total_time,
                 total_before + static_cast<double>(kDt), 1e-12);
    REQUIRE_MSG(w.rally.timing.lap == lap_before, "respawn is not a lap", "respawn");
    REQUIRE_MSG(w.rally.next_checkpoint == 3, "respawn does not change what is owed",
                "respawn");
    REQUIRE_MSG(w.rally.timing.splits.size() == splits_before,
                "respawn does not bank a split", "respawn");

    // And the teleport back to gate 2 did not read as re-crossing anything.
    REQUIRE(!w.rally.events.gate_crossed);

    apricot_test::pass("respawn returns to the last gate, facing the next, clock running");
}

void test_respawn_before_the_first_gate_goes_to_the_line() {
    World w(7);

    place_and_step(w, w.gate(4).position + w.gate(4).right * 200.0f);

    InputFrame respawn{};
    respawn.pressed = kBtnRespawn;
    step_rally(w.rally, respawn, w.collider, kDt);

    const CarPose expect = respawn_pose(w.route, 0, false, w.rally.tuning, w.collider);
    // Banded, not exact, for the same reason as the case above: step_rally
    // integrates the step it respawned on, and the car has real dynamics now.
    REQUIRE_NEAR(static_cast<double>(w.rally.car.position.x),
                 static_cast<double>(expect.position.x), 1e-3);
    REQUIRE_NEAR(static_cast<double>(w.rally.car.position.z),
                 static_cast<double>(expect.position.z), 1e-3);
    REQUIRE_NEAR(static_cast<double>(expect.position.x),
                 static_cast<double>(w.gate(0).position.x), 1e-6);
    REQUIRE(!w.rally.timing.lap_started);
    REQUIRE(w.rally.next_checkpoint == 0);

    apricot_test::pass("respawn before the first gate puts the car on the line");
}

void test_sweep_geometry_directly() {
    // The crossing maths on its own, with no terrain and no physics in the way.
    Checkpoint gate;
    gate.position = glm::vec3{0.0f, 10.0f, 0.0f};
    gate.forward = glm::vec3{0.0f, 0.0f, -1.0f};
    gate.right = glm::vec3{1.0f, 0.0f, 0.0f};
    gate.half_width = 9.0f;
    gate.half_height = 12.0f;

    // A single step spanning 800 metres straight through the middle.
    const GateCrossing far =
        sweep_gate(gate, glm::vec3{0.0f, 10.0f, 400.0f},
                   glm::vec3{0.0f, 10.0f, -400.0f});
    REQUIRE_MSG(far.crossed, "800 m in one step does not tunnel", "sweep");
    REQUIRE_NEAR(static_cast<double>(far.t), 0.5, 1e-6);

    // The same segment run backwards.
    const GateCrossing back =
        sweep_gate(gate, glm::vec3{0.0f, 10.0f, -400.0f},
                   glm::vec3{0.0f, 10.0f, 400.0f});
    REQUIRE(!back.crossed);

    // Past the posts.
    const GateCrossing wide =
        sweep_gate(gate, glm::vec3{20.0f, 10.0f, 5.0f},
                   glm::vec3{20.0f, 10.0f, -5.0f});
    REQUIRE(!wide.crossed);

    // Sailing over the top.
    const GateCrossing over =
        sweep_gate(gate, glm::vec3{0.0f, 40.0f, 5.0f},
                   glm::vec3{0.0f, 40.0f, -5.0f});
    REQUIRE(!over.crossed);

    // A step that ends exactly on the plane still counts — otherwise a car
    // that stops dead on the line is stuck there forever.
    const GateCrossing exact =
        sweep_gate(gate, glm::vec3{0.0f, 10.0f, 5.0f},
                   glm::vec3{0.0f, 10.0f, 0.0f});
    REQUIRE(exact.crossed);
    REQUIRE_NEAR(static_cast<double>(exact.t), 1.0, 1e-6);

    // A step entirely on the approach side does not.
    const GateCrossing shy =
        sweep_gate(gate, glm::vec3{0.0f, 10.0f, 9.0f},
                   glm::vec3{0.0f, 10.0f, 1.0f});
    REQUIRE(!shy.crossed);

    apricot_test::pass("gate sweep geometry: no tunnelling, no backwards, no overflight");
}

}  // namespace

int main() {
    test_sweep_geometry_directly();
    test_gates_must_be_taken_in_order();
    test_reversing_over_a_line_is_worth_nothing();
    test_wide_of_the_posts_does_not_count();
    test_no_tunnelling_at_speed();
    test_splits_land_on_the_line_not_the_step();
    test_a_full_lap_completes_and_rearms();
    test_respawn_returns_to_the_last_gate_with_the_clock_running();
    test_respawn_before_the_first_gate_goes_to_the_line();
    return apricot_test::done("rally_gate_tests");
}
