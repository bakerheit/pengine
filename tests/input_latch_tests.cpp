// Input latching, deadzones and analogue ramps — headless.
//
// No window, no device, no host library. These drive the SAME functions
// platform/input.cpp calls (core/input_shape.h), which is the point: a test
// against a hand-written copy of the latch rule proves the copy works and
// nothing else, and copies drift.
//
// The latched-edge cases are the expensive ones. They only misbehave when the
// display rate is above the sim rate, so they do not reproduce on the machine
// where the code gets written, and a player experiences the failure as "the
// handbrake doesn't always work".

#include <cstdio>

#include "core/fixed_step.h"
#include "core/input_frame.h"
#include "core/input_shape.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot::input;

namespace {

// --- latching ----------------------------------------------------------------

void a_hold_latches_one_edge_not_many() {
    InputFrame f;

    latch_button(f, kBtnRespawn, true);
    REQUIRE(was_pressed(f, kBtnRespawn));
    REQUIRE(is_held(f, kBtnRespawn));

    // Devices repeat their down events while a button is held. Consume the
    // edge, keep holding, and no NEW edge may appear — otherwise a tap and a
    // hold are indistinguishable and holding respawn respawns you forever.
    clear_edges(f);
    for (int repeat = 0; repeat < 32; ++repeat) {
        latch_button(f, kBtnRespawn, true);
    }
    REQUIRE_MSG(!was_pressed(f, kBtnRespawn),
                "a key repeat re-latched the edge", "hold is not a tap");
    REQUIRE_MSG(is_held(f, kBtnRespawn), "the hold was lost", "hold is a hold");

    // Release then press again IS a new edge.
    latch_button(f, kBtnRespawn, false);
    REQUIRE(!is_held(f, kBtnRespawn));
    latch_button(f, kBtnRespawn, true);
    REQUIRE(was_pressed(f, kBtnRespawn));

    apricot_test::pass("a hold latches exactly one edge, a re-press latches another");
}

void releasing_never_clears_a_latched_edge() {
    InputFrame f;

    // Press and release inside one frame, before any step has run. The edge
    // must survive the release: the press HAPPENED, and a sim running at half
    // the display rate has not had a chance to see it yet. Clearing on release
    // is how a quick tap becomes no input at all.
    latch_button(f, kBtnShiftUp, true);
    latch_button(f, kBtnShiftUp, false);

    REQUIRE_MSG(was_pressed(f, kBtnShiftUp),
                "releasing the key threw away the press", "tap survives");
    REQUIRE_MSG(!is_held(f, kBtnShiftUp), "the release did not clear held",
                "tap survives");

    apricot_test::pass("a press-and-release in one frame keeps its edge");
}

// THE ONE THAT MATTERS, driven through the real clock.
void an_edge_survives_zero_step_frames_and_is_consumed_once() {
    FixedStep clock;
    InputFrame f;

    // A display running four times the sim rate: three frames in four owe no
    // step at all.
    const double frame_dt = kSimDt * 0.25;

    // The press arrives on a frame that will owe nothing.
    FixedStep::Tick tick = clock.advance(frame_dt);
    REQUIRE_MSG(tick.steps == 0, "expected a zero-step frame", "setup");
    latch_button(f, kBtnCamCycle, true);

    int observed = 0;
    int zero_step_frames = 0;

    // Run frames until a step lands, counting how many times a step sees it.
    for (int frame = 0; frame < 16; ++frame) {
        if (tick.steps == 0) ++zero_step_frames;

        REQUIRE_MSG(was_pressed(f, kBtnCamCycle) || observed > 0,
                    "the edge vanished before any step ran",
                    "survives zero-step frames");

        for (int i = 0; i < tick.steps; ++i) {
            if (was_pressed(f, kBtnCamCycle)) ++observed;
        }
        // Cleared on STEP COUNT, never per frame.
        if (tick.steps > 0) clear_edges(f);

        tick = clock.advance(frame_dt);
    }

    REQUIRE_MSG(zero_step_frames > 0, "no zero-step frames occurred",
                "test would be vacuous");
    REQUIRE_MSG(observed == 1, "the edge was not delivered exactly once",
                "consumed exactly once");
    REQUIRE_MSG(!was_pressed(f, kBtnCamCycle),
                "the edge outlived its consumption", "consumed exactly once");

    std::printf("      (%d frames owed no step; edge delivered %d time)\n",
                zero_step_frames, observed);
    apricot_test::pass("an edge crosses zero-step frames and is consumed once");
}

// Two DIFFERENT buttons pressed within one frame. Both must reach the sim:
// the mask is a set of bits, and a second press must not overwrite the first.
void two_edges_in_one_frame_both_survive() {
    FixedStep clock;
    InputFrame f;

    // A zero-step frame, so both presses have to survive the frame boundary.
    FixedStep::Tick tick = clock.advance(kSimDt * 0.5);
    REQUIRE_MSG(tick.steps == 0, "expected a zero-step frame", "setup");

    latch_button(f, kBtnShiftUp, true);
    latch_button(f, kBtnRespawn, true);

    // And one of them released again before the step runs — still an edge.
    latch_button(f, kBtnShiftUp, false);

    if (tick.steps > 0) clear_edges(f);

    REQUIRE_MSG(was_pressed(f, kBtnShiftUp), "the first edge was lost",
                "two edges");
    REQUIRE_MSG(was_pressed(f, kBtnRespawn), "the second edge was lost",
                "two edges");
    REQUIRE_MSG(was_pressed(f, kBtnShiftUp | kBtnRespawn),
                "the combined mask test failed", "two edges");

    tick = clock.advance(kSimDt * 0.5);
    REQUIRE_MSG(tick.steps == 1, "expected the step to land here", "setup");

    int saw_shift = 0;
    int saw_respawn = 0;
    for (int i = 0; i < tick.steps; ++i) {
        if (was_pressed(f, kBtnShiftUp)) ++saw_shift;
        if (was_pressed(f, kBtnRespawn)) ++saw_respawn;
    }
    if (tick.steps > 0) clear_edges(f);

    REQUIRE(saw_shift == 1);
    REQUIRE(saw_respawn == 1);
    REQUIRE_MSG(f.pressed == 0u, "clearing left a bit behind", "two edges");

    // Level state is independent of edge state: shift was released, respawn
    // was not.
    REQUIRE(!is_held(f, kBtnShiftUp));
    REQUIRE(is_held(f, kBtnRespawn));

    apricot_test::pass("two edges in one frame both reach the sim");
}

// A frame that owes SEVERAL steps shows the same edge to each of them, because
// there is one InputFrame per render frame and it is cleared once, after the
// last step. That is the contract, and it is pinned here so nobody "fixes" it
// into a per-step consume — which would make the number of times a press fires
// depend on the frame rate.
void a_multi_step_frame_shows_one_edge_to_every_step() {
    FixedStep clock;
    InputFrame f;

    const FixedStep::Tick tick = clock.advance(kSimDt * 3.0);
    REQUIRE_MSG(tick.steps == 3, "expected a three-step frame", "setup");

    latch_button(f, kBtnPause, true);

    int seen = 0;
    for (int i = 0; i < tick.steps; ++i) {
        if (was_pressed(f, kBtnPause)) ++seen;
    }
    if (tick.steps > 0) clear_edges(f);

    REQUIRE_MSG(seen == tick.steps,
                "the edge was not visible to every step of its frame",
                "one frame, one input");
    REQUIRE(!was_pressed(f, kBtnPause));

    apricot_test::pass("a multi-step frame sees one edge, then it is gone");
}

// --- device normalisation ----------------------------------------------------

void device_axes_normalise_without_overshooting() {
    // The 16-bit range is ASYMMETRIC. Full deflection one way is -32768, and
    // dividing by 32767 gives -1.00003 — an overshoot that survives all the
    // way downstream and trips whichever module asserts its inputs are in
    // range, where it reads as a physics bug.
    REQUIRE_NEAR(normalise_axis16(-32768), -1.0, 1e-7);
    REQUIRE_NEAR(normalise_axis16(32767), 1.0, 1e-7);
    REQUIRE_NEAR(normalise_axis16(0), 0.0, 1e-7);
    REQUIRE(normalise_axis16(-32768) >= -1.0f);

    // Nonsense from a driver must not let throttle run backwards.
    REQUIRE_NEAR(normalise_trigger16(0), 0.0, 1e-7);
    REQUIRE_NEAR(normalise_trigger16(32767), 1.0, 1e-7);
    REQUIRE_NEAR(normalise_trigger16(-5000), 0.0, 1e-7);

    apricot_test::pass("device axes normalise and clamp");
}

// --- deadzones ---------------------------------------------------------------

void scalar_deadzone_rescales_instead_of_jumping() {
    constexpr float dz = 0.18f;
    constexpr float sat = 0.95f;

    REQUIRE_NEAR(apply_deadzone(0.0f, dz, sat), 0.0, 1e-7);
    REQUIRE_NEAR(apply_deadzone(0.17f, dz, sat), 0.0, 1e-7);
    REQUIRE_NEAR(apply_deadzone(dz, dz, sat), 0.0, 1e-7);

    // THE RESCALE. Just past the threshold the axis must leave zero SMOOTHLY.
    // Without the rescale it jumps straight to 0.18, and the car snaps to
    // eighteen percent of steering lock out of nothing — which players report
    // as notchy steering, not as a deadzone bug.
    const float just_past = apply_deadzone(dz + 0.001f, dz, sat);
    REQUIRE_MSG(just_past > 0.0f, "the axis did not leave zero", "rescale");
    REQUIRE_MSG(just_past < 0.02f, "the axis JUMPED past the deadzone value",
                "rescale");

    // Full travel at the saturation point, and clamped beyond it — a stick
    // that only physically reaches 0.97 must still be able to ask for full
    // lock.
    REQUIRE_NEAR(apply_deadzone(sat, dz, sat), 1.0, 1e-6);
    REQUIRE_NEAR(apply_deadzone(1.0f, dz, sat), 1.0, 1e-6);

    // Symmetric. An asymmetric deadzone makes the car pull to one side.
    REQUIRE_NEAR(apply_deadzone(-0.5f, dz, sat),
                 -static_cast<double>(apply_deadzone(0.5f, dz, sat)), 1e-7);

    // Degenerate tuning must not divide by zero. A saturation point at or
    // below the deadzone leaves no travel to rescale into, so the axis goes
    // fully digital rather than returning infinity — and the deadzone still
    // wins for anything below it.
    REQUIRE_NEAR(apply_deadzone(0.95f, 0.9f, 0.5f), 1.0, 1e-6);
    REQUIRE_NEAR(apply_deadzone(0.5f, 0.9f, 0.5f), 0.0, 1e-6);
    REQUIRE_NEAR(apply_deadzone(0.95f, 0.0f, 0.0f), 1.0, 1e-6);
    REQUIRE_NEAR(apply_deadzone(-0.95f, 0.9f, 0.5f), -1.0, 1e-6);

    // Monotonic across the whole travel: more push is never less output.
    float previous = -1.0f;
    for (int i = 0; i <= 200; ++i) {
        const float v = static_cast<float>(i) / 200.0f;
        const float out = apply_deadzone(v, dz, sat);
        REQUIRE_MSG(out >= previous, "the deadzone curve went backwards",
                    "monotonic");
        REQUIRE_MSG(out >= 0.0f && out <= 1.0f, "the deadzone left [0,1]",
                    "monotonic");
        previous = out;
    }

    apricot_test::pass("scalar deadzone rescales, saturates and stays monotonic");
}

void stick_deadzone_is_round_not_square() {
    constexpr float dz = kStickDeadzone;
    constexpr float sat = kStickSaturation;

    // Dead centre.
    glm::vec2 out = apply_stick_deadzone(glm::vec2{0.0f, 0.0f}, dz, sat);
    REQUIRE(out.x == 0.0f && out.y == 0.0f);

    // THE SQUARE-HOLE BUG. Each component is well inside the deadzone on its
    // own, but the stick is pushed 0.212 from centre — outside it. A per-axis
    // deadzone zeroes both components here and the stick appears dead in the
    // diagonals; a radial one lets it through.
    const glm::vec2 diagonal{0.15f, 0.15f};
    REQUIRE_MSG(glm::length(diagonal) > dz, "the diagonal case is not set up",
                "setup");
    out = apply_stick_deadzone(diagonal, dz, sat);
    REQUIRE_MSG(out.x > 0.0f && out.y > 0.0f,
                "a per-axis deadzone squared off the stick", "round hole");

    // A shorter diagonal, genuinely inside the radius, must still be dead.
    out = apply_stick_deadzone(glm::vec2{0.12f, 0.12f}, dz, sat);
    REQUIRE_MSG(out.x == 0.0f && out.y == 0.0f, "the deadzone let creep through",
                "round hole");

    // DIRECTION IS PRESERVED EXACTLY. Only the magnitude is rescaled — a
    // deadzone that bends the direction makes the car steer somewhere the
    // player did not point.
    const glm::vec2 pushed{0.6f, -0.3f};
    out = apply_stick_deadzone(pushed, dz, sat);
    REQUIRE_NEAR(static_cast<double>(out.x / out.y),
                 static_cast<double>(pushed.x / pushed.y), 1e-4);

    // Magnitude never exceeds 1, even from a square-gated stick reporting
    // 1.0/1.0 in the corner (length 1.414).
    out = apply_stick_deadzone(glm::vec2{1.0f, 1.0f}, dz, sat);
    REQUIRE_MSG(glm::length(out) <= 1.0f + 1e-5f,
                "the corner of the gate produced more than full deflection",
                "clamped");

    // Full deflection on one axis reaches exactly 1.
    out = apply_stick_deadzone(glm::vec2{1.0f, 0.0f}, dz, sat);
    REQUIRE_NEAR(out.x, 1.0, 1e-5);

    apricot_test::pass("stick deadzone is radial, direction-preserving and clamped");
}

// --- ramps -------------------------------------------------------------------

void ramp_arrives_exactly_and_on_schedule() {
    // LINEAR, so it ARRIVES. An exponential ease asymptotes: a keyboard player
    // holding throttle would sit at 0.98 forever and never reach full power,
    // and no amount of holding the key would fix it.
    float v = 0.0f;
    const float dt = 1.0f / 240.0f;
    int frames = 0;
    while (v < 1.0f && frames < 10000) {
        v = ramp_toward(v, 1.0f, dt, kPedalRiseSeconds, kPedalFallSeconds);
        ++frames;
    }
    REQUIRE_MSG(v == 1.0f, "the ramp never actually reached full travel",
                "arrives exactly");

    // And it arrived on schedule, not eventually.
    const double taken = static_cast<double>(frames) * static_cast<double>(dt);
    REQUIRE_NEAR(taken, static_cast<double>(kPedalRiseSeconds), 2.0 * dt);

    // Once there, it stays: no overshoot, no oscillation.
    for (int i = 0; i < 100; ++i) {
        v = ramp_toward(v, 1.0f, dt, kPedalRiseSeconds, kPedalFallSeconds);
        REQUIRE_MSG(v == 1.0f, "the ramp overshot its target", "no overshoot");
    }

    apricot_test::pass("the ramp arrives exactly, on schedule, and stays");
}

void ramp_is_frame_rate_independent() {
    // The same elapsed time must give the same axis value whether it arrived as
    // one long frame or many short ones. Otherwise the car steers differently
    // at 60 and 144 Hz, which is the kind of thing that gets blamed on the
    // physics for months.
    const float total = 0.1f;

    float coarse = 0.0f;
    coarse = ramp_toward(coarse, 1.0f, total, kSteerRiseSeconds,
                         kSteerFallSeconds);

    float fine = 0.0f;
    const int subdivisions = 500;
    for (int i = 0; i < subdivisions; ++i) {
        fine = ramp_toward(fine, 1.0f, total / static_cast<float>(subdivisions),
                           kSteerRiseSeconds, kSteerFallSeconds);
    }

    REQUIRE_NEAR(coarse, fine, 1e-4);
    REQUIRE_MSG(coarse > 0.0f && coarse < 1.0f,
                "the ramp finished within the window; the test proves nothing",
                "setup");

    apricot_test::pass("subdividing a frame does not change the ramp");
}

void ramp_falls_faster_than_it_rises_and_handles_junk() {
    const float dt = 1.0f / 120.0f;

    // Falls are quicker than rises on purpose — a pedal returns faster than a
    // foot presses, and a release that feels late feels broken.
    REQUIRE(kPedalFallSeconds < kPedalRiseSeconds);

    const float rose = ramp_toward(0.0f, 1.0f, dt, kPedalRiseSeconds,
                                   kPedalFallSeconds);
    const float fell = 1.0f - ramp_toward(1.0f, 0.0f, dt, kPedalRiseSeconds,
                                          kPedalFallSeconds);
    REQUIRE_MSG(fell > rose, "the fall was not quicker than the rise",
                "asymmetric ramp");

    // A full-lock reversal crosses zero without sticking.
    float v = 1.0f;
    for (int i = 0; i < 1000 && v > -1.0f; ++i) {
        v = ramp_toward(v, -1.0f, dt, kSteerRiseSeconds, kSteerFallSeconds);
    }
    REQUIRE_MSG(v == -1.0f, "the axis never made it to full opposite lock",
                "sign flip");

    // Junk deltas are inert rather than destructive. A NaN dt reaching an axis
    // poisons it for the rest of the run, and every value derived from it.
    REQUIRE(ramp_toward(0.4f, 1.0f, 0.0f, 0.2f, 0.1f) == 0.4f);
    REQUIRE(ramp_toward(0.4f, 1.0f, -1.0f, 0.2f, 0.1f) == 0.4f);
    const float nan_dt = std::nanf("");
    REQUIRE_MSG(ramp_toward(0.4f, 1.0f, nan_dt, 0.2f, 0.1f) == 0.4f,
                "a NaN delta moved the axis", "junk dt");

    // A zero ramp time means instant, not a division by zero.
    REQUIRE(ramp_toward(0.0f, 1.0f, dt, 0.0f, 0.0f) == 1.0f);

    apricot_test::pass("falls beat rises; junk deltas and zero ramp times are safe");
}

// The reason the ramps exist at all: a key and a stick have to be comparable
// inputs. A digital key wired straight to an axis gives instant full lock,
// which is not "responsive", it is a different and better car.
void a_key_ramp_is_not_instant() {
    const float dt = 1.0f / 120.0f;
    const float after_one_frame =
        ramp_toward(0.0f, 1.0f, dt, kSteerRiseSeconds, kSteerFallSeconds);

    REQUIRE_MSG(after_one_frame < 0.2f,
                "one frame of a held key produced most of full lock",
                "comparable to a stick");
    REQUIRE_MSG(after_one_frame > 0.0f, "the key produced nothing at all",
                "comparable to a stick");

    apricot_test::pass("one frame of a held key is not full lock");
}

}  // namespace

int main() {
    std::printf("input_latch_tests\n");
    a_hold_latches_one_edge_not_many();
    releasing_never_clears_a_latched_edge();
    an_edge_survives_zero_step_frames_and_is_consumed_once();
    two_edges_in_one_frame_both_survive();
    a_multi_step_frame_shows_one_edge_to_every_step();
    device_axes_normalise_without_overshooting();
    scalar_deadzone_rescales_instead_of_jumping();
    stick_deadzone_is_round_not_square();
    ramp_arrives_exactly_and_on_schedule();
    ramp_is_frame_rate_independent();
    ramp_falls_faster_than_it_rises_and_handles_junk();
    a_key_ramp_is_not_instant();
    return apricot_test::done("input_latch_tests");
}
