// Objective and trigger runtime, headless, driven through the producer.
//
// Every case walks a moving tracked point through ObjectiveRuntime::update().
// NOTHING hand-sets a state, and that is the point: a consumer test built from
// hand-written states passes happily while the real transition is broken. The
// HUD that draws any of this is host-side and is a feel check.
//
// Pins:
//   - trigger enter/exit fire exactly on the outside<->inside EDGES of a
//     real walk-through (once each, re-armed on re-entry),
//   - a trigger spawned around the tracked point fires enter on first update,
//   - callbacks may remove/add triggers reentrantly without corrupting the
//     pass,
//   - a reach objective passes via the real walk, then the verdict banner
//     auto-clears after RESULT_HOLD_S,
//   - a timed objective fails on expiry (and unlimited ones never do),
//   - reach beats the deadline when one step crosses both,
//   - the exact smoke-test helper a dev panel calls: marker placement,
//     walk-to-pass, and stand-still-to-fail.

#include <cmath>

#include <glm/glm.hpp>

#include "test_assert.h"

#include "city/objective_runtime.h"

using namespace apricot;

namespace {

constexpr float DT = 1.f / 60.f;   // the game's fixed step

// Step the runtime while moving `pos` toward `target` at `speed_mps`,
// stopping ON ARRIVAL (pos == target) or after `max_steps`. Returns steps.
int walk_to(ObjectiveRuntime& rt, glm::vec3& pos, const glm::vec3& target,
            float speed_mps, int max_steps) {
    int steps = 0;
    while (steps < max_steps) {
        const glm::vec3 to = target - pos;
        const float d = glm::length(to);
        const float step_len = speed_mps * DT;
        pos = (d <= step_len) ? target : pos + to * (step_len / d);
        rt.update(DT, pos);
        ++steps;
        if (d <= step_len) break;
    }
    return steps;
}

// Step the runtime `n` times with the point standing still.
void stand(ObjectiveRuntime& rt, const glm::vec3& pos, int n) {
    for (int i = 0; i < n; ++i) rt.update(DT, pos);
}

void test_trigger_enter_exit_edges() {
    ObjectiveRuntime rt;
    int enters = 0, exits = 0;
    rt.add_trigger({10.f, 0.f, 0.f}, 2.f,
                   [&] { ++enters; }, [&] { ++exits; });

    // Walk from the origin THROUGH the sphere and 10 m out the far side.
    glm::vec3 pos{0.f, 0.f, 0.f};
    walk_to(rt, pos, {20.f, 0.f, 0.f}, 5.f, 10000);
    REQUIRE_MSG(enters == 1, "one enter crossing in", "walk-through enter");
    REQUIRE_MSG(exits == 1, "one exit crossing out", "walk-through exit");

    // Walk back in and stop at the centre: enter re-arms, no extra exit.
    walk_to(rt, pos, {10.f, 0.f, 0.f}, 5.f, 10000);
    REQUIRE_MSG(enters == 2, "re-entry fires enter again", "re-enter");
    REQUIRE_MSG(exits == 1, "no exit while parked inside", "no-exit-inside");

    // Loitering inside fires nothing further (edge, not level, semantics).
    stand(rt, pos, 120);
    REQUIRE_MSG(enters == 2 && exits == 1, "loitering fires no edges", "loiter");
}

void test_trigger_spawned_around_point_fires_on_first_update() {
    ObjectiveRuntime rt;
    const glm::vec3 pos{3.f, 1.f, -4.f};
    int enters = 0;
    rt.add_trigger(pos, 5.f, [&] { ++enters; });
    REQUIRE_MSG(enters == 0, "no fire before any update", "pre-update");
    rt.update(DT, pos);
    REQUIRE_MSG(enters == 1, "already-inside fires enter on first update",
             "first-update");
    REQUIRE_MSG(rt.trigger_inside(0), "containment readable after update",
             "inside-read");
}

void test_trigger_reentrant_remove_and_add() {
    ObjectiveRuntime rt;
    int a_enters = 0, a_exits = 0, b_enters = 0;
    // Trigger A removes ITSELF (and spawns trigger B around the same spot)
    // from inside its own on_enter — the mission-script shape "checkpoint
    // reached, next checkpoint appears".
    ObjectiveRuntime::TriggerId a = ObjectiveRuntime::kInvalidTrigger;
    a = rt.add_trigger({5.f, 0.f, 0.f}, 2.f,
                       [&] {
                           ++a_enters;
                           rt.remove_trigger(a);
                           rt.add_trigger({5.f, 0.f, 0.f}, 4.f,
                                          [&] { ++b_enters; });
                       },
                       [&] { ++a_exits; });

    glm::vec3 pos{0.f, 0.f, 0.f};
    walk_to(rt, pos, {5.f, 0.f, 0.f}, 5.f, 10000);
    REQUIRE_MSG(a_enters == 1, "A fired once", "a-enter");
    REQUIRE_MSG(rt.trigger_count() == 1, "A reaped, B alive", "count");
    // B (added mid-update) is deferred to the NEXT update pass, then fires
    // its enter edge exactly once even though the point sits inside it.
    rt.update(DT, pos);
    REQUIRE_MSG(b_enters == 1, "B fired its enter edge exactly once", "b-enter");
    // Walking out fires nothing for the removed A.
    walk_to(rt, pos, {30.f, 0.f, 0.f}, 5.f, 10000);
    REQUIRE_MSG(a_exits == 0, "removed trigger never fires exit", "a-exit");
}

void test_reach_objective_passes_then_banner_clears() {
    ObjectiveRuntime rt;
    ObjectiveRuntime::Desc d;
    d.text           = "REACH THE MARKER";
    d.waypoint       = {24.f, 0.f, 0.f};
    d.has_waypoint   = true;
    d.reach_radius_m = 2.5f;
    d.time_limit_s   = 20.f;
    rt.start_objective(d);
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Active, "active on start",
             "start");
    REQUIRE_MSG(rt.has_waypoint() && rt.timed(), "waypoint + timer exposed",
             "hud-reads");

    // Walk at 4 m/s. Distance to the pass edge is 24 - 2.5 = 21.5 m,
    // so the pass lands near step ceil(21.5 / (4/60)) — bracket it.
    glm::vec3 pos{0.f, 0.f, 0.f};
    int steps = 0;
    while (rt.state() == ObjectiveRuntime::State::Active && steps < 10000) {
        const glm::vec3 to = d.waypoint - pos;
        pos += to * (4.f * DT / glm::length(to));
        rt.update(DT, pos);
        ++steps;
    }
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Passed,
             "reached => passed", "passed");
    const float dist = glm::length(d.waypoint - pos);
    REQUIRE_MSG(dist <= d.reach_radius_m && dist > d.reach_radius_m - 0.5f,
             "passed on the radius edge, not early/late", "pass-edge");
    REQUIRE_MSG(!rt.has_waypoint(), "marker gone once passed", "marker-off");
    REQUIRE_MSG(rt.result_hold_remaining_s() > 0.f, "banner hold armed", "hold");

    // The verdict banner holds RESULT_HOLD_S, then auto-clears to Inactive.
    stand(rt, pos, static_cast<int>(ObjectiveRuntime::RESULT_HOLD_S / DT) + 2);
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Inactive,
             "banner auto-clears", "auto-clear");
}

void test_timeout_fails_and_untimed_never_does() {
    // Timed: stand still, 2 s limit => Failed at expiry, clock floored at 0.
    ObjectiveRuntime rt;
    ObjectiveRuntime::Desc d;
    d.text           = "REACH THE MARKER";
    d.waypoint       = {100.f, 0.f, 0.f};
    d.has_waypoint   = true;
    d.reach_radius_m = 2.5f;
    d.time_limit_s   = 2.f;
    rt.start_objective(d);
    const glm::vec3 pos{0.f, 0.f, 0.f};
    int steps = 0;
    while (rt.state() == ObjectiveRuntime::State::Active && steps < 10000) {
        rt.update(DT, pos);
        ++steps;
    }
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Failed,
             "expiry => failed", "failed");
    REQUIRE_MSG(rt.time_remaining_s() == 0.f, "clock floored at zero", "floor");
    REQUIRE_MSG(std::abs(static_cast<float>(steps) * DT - 2.f) < 3.f * DT,
             "failed at ~2 s of stepped time", "expiry-step");

    // Untimed: a minute of standing still stays Active.
    ObjectiveRuntime rt2;
    d.time_limit_s = 0.f;
    rt2.start_objective(d);
    stand(rt2, pos, 60 * 60);
    REQUIRE_MSG(rt2.state() == ObjectiveRuntime::State::Active,
             "untimed never expires", "untimed");
    REQUIRE_MSG(!rt2.timed(), "untimed exposes no countdown", "untimed-read");
}

void test_reach_beats_deadline_on_the_same_step() {
    ObjectiveRuntime rt;
    ObjectiveRuntime::Desc d;
    d.waypoint       = {1.f, 0.f, 0.f};
    d.has_waypoint   = true;
    d.reach_radius_m = 2.5f;
    d.time_limit_s   = 0.5f * DT;   // expires inside the first step
    rt.start_objective(d);
    // The first update finds the point already inside the reach sphere AND
    // past the deadline — reach is checked first, so the player wins the tie.
    rt.update(DT, {0.f, 0.f, 0.f});
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Passed,
             "reach wins the tie", "tie");
}

void test_external_verdicts() {
    ObjectiveRuntime rt;
    // Verdicts are no-ops unless Active.
    rt.pass_objective();
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Inactive,
             "pass on inactive is a no-op", "inactive-pass");

    ObjectiveRuntime::Desc d;
    d.text = "DROP THE TAIL";
    rt.start_objective(d);
    rt.fail_objective();
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Failed,
             "external fail lands", "ext-fail");
    rt.pass_objective();   // verdict already in — must not flip
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Failed,
             "verdict is final until cleared", "final");
    rt.clear_objective();
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Inactive
                 && rt.result_hold_remaining_s() == 0.f,
             "clear resets state + banner", "clear");
}

// ground_y stand-in for Heightmap::sample (captureless => function pointer).
float fake_ground_y(float, float) { return 7.25f; }

void test_smoke_helper_walk_to_pass() {
    // The EXACT call the F1 Mission button makes (camera forward, ground fn),
    // then the walk the founder will do — the producer path end to end.
    ObjectiveRuntime rt;
    const glm::vec3 start{5.f, 3.f, 7.f};
    start_smoke_test_objective(rt, start, {0.6f, -0.2f, -0.8f}, &fake_ground_y);

    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Active, "spawned active",
             "active");
    REQUIRE_MSG(rt.has_waypoint() && rt.timed(), "marker + timer up", "hud");
    const glm::vec3 wp = rt.waypoint();
    const glm::vec2 flat{wp.x - start.x, wp.z - start.z};
    REQUIRE_MSG(std::abs(glm::length(flat) - 40.f) < 0.01f,
             "marker 40 m out in XZ (Y component of facing ignored)", "dist");
    REQUIRE_MSG(wp.y == 7.25f, "marker snapped to the ground fn", "ground");

    // Jog to it at 6 m/s: passes well inside the 30 s limit.
    glm::vec3 pos = start;
    walk_to(rt, pos, wp, 6.f, 10000);
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Passed,
             "smoke objective passes on reach", "pass");
}

void test_smoke_helper_stand_still_to_fail() {
    ObjectiveRuntime rt;
    const glm::vec3 start{0.f, 0.f, 0.f};
    // Degenerate facing (straight down): helper falls back to -Z.
    start_smoke_test_objective(rt, start, {0.f, -1.f, 0.f}, nullptr);
    REQUIRE_MSG(rt.waypoint().z == -40.f && rt.waypoint().y == 0.f,
             "degenerate facing falls back to -Z, null ground keeps Y",
             "fallback");

    // Stand still past the 30 s limit => Failed.
    int steps = 0;
    while (rt.state() == ObjectiveRuntime::State::Active && steps < 60 * 40) {
        rt.update(DT, start);
        ++steps;
    }
    REQUIRE_MSG(rt.state() == ObjectiveRuntime::State::Failed,
             "smoke objective fails on timeout", "timeout");
}

} // namespace

int main() {
    test_trigger_enter_exit_edges();
    test_trigger_spawned_around_point_fires_on_first_update();
    test_trigger_reentrant_remove_and_add();
    test_reach_objective_passes_then_banner_clears();
    test_timeout_fails_and_untimed_never_does();
    test_reach_beats_deadline_on_the_same_step();
    test_external_verdicts();
    test_smoke_helper_walk_to_pass();
    test_smoke_helper_stand_still_to_fail();
    return apricot_test::done("objective_runtime_tests");
}
