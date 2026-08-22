// Fixed-step clock accounting.
//
// Two things are under test and they are equally load-bearing:
//
//   1. The step count is right, and stays right over a long run. Sim time may
//      lag wall time; it must never run ahead of it, and it must never spiral.
//   2. A frame that owes ZERO steps does not lose a latched input edge. This
//      is the bug the whole latched-edge design exists to prevent, and it only
//      reproduces when the display rate is above the sim rate — i.e. never on
//      the machine where it gets written.

#include <cmath>
#include <cstdio>

#include "core/fixed_step.h"
#include "core/input_frame.h"
#include "test_assert.h"

using namespace apricot;

namespace {

void constants_are_what_everything_assumes() {
    REQUIRE(kSimHz == 120.0);
    REQUIRE_NEAR(kSimDt, 1.0 / 120.0, 1e-18);
    REQUIRE(kMaxStepsPerFrame > 0);
    apricot_test::pass("sim runs at 120 Hz");
}

void exact_frames_owe_exact_steps() {
    FixedStep clock;

    // Exactly one step of time owes exactly one step and leaves nothing over.
    FixedStep::Tick t = clock.advance(kSimDt);
    REQUIRE(t.steps == 1);
    REQUIRE(!t.clamped);
    REQUIRE_NEAR(clock.alpha(), 0.0, 1e-12);

    // Exactly five steps' worth in one frame.
    t = clock.advance(kSimDt * 5.0);
    REQUIRE(t.steps == 5);
    REQUIRE(!t.clamped);
    REQUIRE_NEAR(clock.alpha(), 0.0, 1e-12);

    apricot_test::pass("whole multiples of the step owe exactly that many steps");
}

void partial_frames_accumulate_instead_of_rounding() {
    FixedStep clock;

    // Half a step owes nothing yet...
    FixedStep::Tick t = clock.advance(kSimDt * 0.5);
    REQUIRE(t.steps == 0);
    REQUIRE_NEAR(clock.alpha(), 0.5, 1e-9);

    // ...and the other half completes it. The remainder must carry, not round.
    t = clock.advance(kSimDt * 0.5);
    REQUIRE(t.steps == 1);
    REQUIRE_NEAR(clock.alpha(), 0.0, 1e-9);

    // A step and a half owes one, and keeps the half.
    t = clock.advance(kSimDt * 1.5);
    REQUIRE(t.steps == 1);
    REQUIRE_NEAR(clock.alpha(), 0.5, 1e-9);

    apricot_test::pass("partial frames carry their remainder");
}

void alpha_never_leaves_its_range() {
    FixedStep clock;

    // Deliberately awkward deltas, none of them a clean multiple of the step.
    const double deltas[] = {0.0031, 0.0170, 0.0083, 0.0009, 0.1400,
                             0.0071, 0.0002, 0.0333, 0.0500, 0.0117};
    for (int round = 0; round < 500; ++round) {
        for (const double d : deltas) {
            (void)clock.advance(d);
            const double a = clock.alpha();
            REQUIRE_MSG(a >= 0.0 && a < 1.0, "alpha left [0,1)", "alpha range");
        }
    }
    apricot_test::pass("alpha stays in [0,1) under ragged frame times");
}

// The anti-drift property. Over a long run the total number of steps must
// track elapsed time exactly: never ahead (that would be simulating time that
// has not happened), and at most one step behind (the in-progress step).
void long_run_does_not_drift() {
    FixedStep clock;

    constexpr int kFrames = 100000;
    constexpr double kFrameDt = 1.0 / 60.0;  // 60 Hz display, 120 Hz sim

    long long total = 0;
    for (int i = 0; i < kFrames; ++i) {
        const FixedStep::Tick t = clock.advance(kFrameDt);
        REQUIRE_MSG(!t.clamped, "a steady 60 Hz frame should never clamp",
                    "no spurious clamp");
        total += t.steps;
    }

    // Two sim steps per display frame, and no accumulated error either way.
    REQUIRE(total == 2LL * kFrames);

    // The same run at a rate that is NOT a clean divisor of the sim rate.
    FixedStep ragged;
    constexpr double kOddDt = 1.0 / 144.0;
    long long odd_total = 0;
    for (int i = 0; i < kFrames; ++i) odd_total += ragged.advance(kOddDt).steps;

    const double elapsed = kOddDt * static_cast<double>(kFrames);
    const long long expected = static_cast<long long>(elapsed * kSimHz);
    REQUIRE_MSG(odd_total <= expected, "sim ran AHEAD of wall time", "no overrun");
    REQUIRE_MSG(odd_total >= expected - 1, "sim fell behind wall time",
                "no underrun");

    apricot_test::pass("no drift over 100k frames at 60 Hz and 144 Hz");
}

void a_stall_clamps_and_does_not_repay_the_debt() {
    FixedStep clock;

    // Twenty seconds gone: a breakpoint, not a hitch.
    const FixedStep::Tick t = clock.advance(20.0);
    REQUIRE(t.steps == kMaxStepsPerFrame);
    REQUIRE(t.clamped);

    // The surplus must be DISCARDED. If it were carried, the next frames would
    // each run a full budget trying to repay a debt they can never clear, and
    // one pause would leave the session permanently stuttering.
    const FixedStep::Tick after = clock.advance(kSimDt);
    REQUIRE_MSG(after.steps == 1, "clamped time was carried instead of dropped",
                "debt discarded");
    REQUIRE(!after.clamped);

    apricot_test::pass("a long stall clamps and drops the surplus");
}

void garbage_deltas_cannot_move_the_clock() {
    FixedStep clock;

    REQUIRE(clock.advance(0.0).steps == 0);
    REQUIRE(clock.advance(-1.0).steps == 0);           // a clock that went backwards
    REQUIRE(clock.advance(-0.0).steps == 0);
    const double nan = std::nan("");
    REQUIRE(clock.advance(nan).steps == 0);            // must not poison the accumulator

    // After all that rubbish the clock must still be healthy.
    REQUIRE_NEAR(clock.alpha(), 0.0, 1e-12);
    REQUIRE(clock.advance(kSimDt).steps == 1);

    apricot_test::pass("negative, zero and NaN deltas are inert");
}

// THE ONE THAT MATTERS.
//
// A 240 Hz display against a 120 Hz sim owes a step only every other frame. If
// the input edge mask were cleared per FRAME rather than per STEP, every press
// that landed on a zero-step frame would be silently discarded — an
// intermittent, frame-rate-dependent dropped input that a player experiences
// as "the handbrake doesn't always work".
void zero_step_frame_keeps_a_latched_edge() {
    FixedStep clock;
    InputFrame input;

    // Frame 1: half a step of time. The player presses the handbrake.
    FixedStep::Tick tick = clock.advance(kSimDt * 0.5);
    REQUIRE_MSG(tick.steps == 0, "expected a zero-step frame", "setup");

    input.pressed |= kBtnRespawn;  // the edge, latched at event time

    // The frame loop clears edges ONLY when a step ran. No step ran.
    if (tick.steps > 0) clear_edges(input);

    REQUIRE_MSG(was_pressed(input, kBtnRespawn),
                "a zero-step frame swallowed the latched edge",
                "edge survives a zero-step frame");

    // Frame 2: the rest of the step arrives. Now the sim runs and sees it.
    tick = clock.advance(kSimDt * 0.5);
    REQUIRE_MSG(tick.steps == 1, "expected the step to land here", "setup");

    bool sim_saw_the_press = false;
    for (int i = 0; i < tick.steps; ++i) {
        if (was_pressed(input, kBtnRespawn)) sim_saw_the_press = true;
    }
    REQUIRE_MSG(sim_saw_the_press, "the sim never observed the press",
                "edge delivered to the step");

    // Consumed exactly once, now that a step has actually run.
    if (tick.steps > 0) clear_edges(input);
    REQUIRE_MSG(!was_pressed(input, kBtnRespawn), "edge was not consumed",
                "edge cleared after the step");

    apricot_test::pass("a zero-step frame does not lose a latched edge");
}

// The same property over a long, realistic run: a 240 Hz display, a press on
// every frame, and not one of them may be lost.
void no_press_is_lost_at_double_refresh() {
    FixedStep clock;
    InputFrame input;

    constexpr int kFrames = 4000;
    const double frame_dt = kSimDt * 0.5;  // 240 Hz display, 120 Hz sim

    int presses = 0;
    int observed = 0;
    int zero_step_frames = 0;

    for (int f = 0; f < kFrames; ++f) {
        const FixedStep::Tick tick = clock.advance(frame_dt);
        if (tick.steps == 0) ++zero_step_frames;

        // A press arrives every frame, including the zero-step ones.
        if ((input.pressed & kBtnRespawn) == 0u) {
            input.pressed |= kBtnRespawn;
            ++presses;
        }

        for (int i = 0; i < tick.steps; ++i) {
            if (was_pressed(input, kBtnRespawn)) ++observed;
        }
        if (tick.steps > 0) clear_edges(input);
    }

    // Half the frames owe nothing, which is exactly the condition that used to
    // eat inputs. If this is zero the test is not testing anything.
    REQUIRE_MSG(zero_step_frames > 0, "no zero-step frames occurred",
                "test would be vacuous");

    // Every press latched was eventually seen by a step.
    REQUIRE_MSG(observed == presses, "a latched press was never observed",
                "no dropped inputs at 240 Hz");

    std::printf("      (%d frames, %d owed no step, %d presses, all delivered)\n",
                kFrames, zero_step_frames, presses);
    apricot_test::pass("no input lost over 4000 frames at double refresh");
}

}  // namespace

int main() {
    std::printf("fixed_step_tests\n");
    constants_are_what_everything_assumes();
    exact_frames_owe_exact_steps();
    partial_frames_accumulate_instead_of_rounding();
    alpha_never_leaves_its_range();
    long_run_does_not_drift();
    a_stall_clamps_and_does_not_repay_the_debt();
    garbage_deltas_cannot_move_the_clock();
    zero_step_frame_keeps_a_latched_edge();
    no_press_is_lost_at_double_refresh();
    return apricot_test::done("fixed_step_tests");
}
