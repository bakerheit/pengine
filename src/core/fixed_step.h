#pragma once

namespace apricot {

// The simulation ticks at exactly 120 Hz. Everything downstream — vehicle
// integration, replay tapes, lap times — is defined in terms of this number,
// so it is a constant, not a setting. Changing it invalidates every recorded
// tape.
inline constexpr double kSimHz = 120.0;
inline constexpr double kSimDt = 1.0 / kSimHz;

// Hard ceiling on steps per render frame. Without it, a breakpoint held for
// twenty seconds returns a 2400-step frame and the app appears to hang while
// it "catches up" — the classic spiral of death. 12 steps = 100 ms of sim,
// which comfortably covers a genuine hitch and refuses to cover a debugger.
inline constexpr int kMaxStepsPerFrame = 12;

// Fixed-timestep accumulator.
//
// Deliberately has NO clock of its own: you feed it a measured frame delta and
// it tells you how many sim steps that frame owes. Keeping wall-clock reads out
// of here is what lets a replay tape be fed through the identical code path as
// a live frame and produce identical results.
//
// Usage, and the ordering matters:
//
//     const FixedStep::Tick tick = clock.advance(frame_dt);
//     for (int i = 0; i < tick.steps; ++i)
//         step_world(world, input, kSimDt);
//     if (tick.steps > 0) clear_edges(input);   // ONLY when a step ran
//     render(world, clock.alpha());
//
// A frame can legitimately owe ZERO steps (a 240 Hz display against a 120 Hz
// sim owes a step every other frame). That is exactly why latched input edges
// are cleared on step count, never on frame boundary — see core/input_frame.h.
class FixedStep {
public:
    struct Tick {
        // How many times to run the sim this frame. Always >= 0.
        int steps = 0;

        // True when the owed step count hit kMaxStepsPerFrame and the surplus
        // sim time was DISCARDED rather than carried. Surface it: a build that
        // reports this outside of a debugger session is genuinely too slow,
        // and silently swallowing it hides that.
        bool clamped = false;
    };

    // Feed one measured render-frame delta in seconds. Non-positive and NaN
    // deltas are treated as zero (a frame that owes nothing) rather than
    // trusted — a backwards clock must not be able to rewind the sim.
    Tick advance(double frame_dt);

    // Fraction of the way from the last completed sim step to the next.
    // Guaranteed to be in [0, 1) — see the note on pending_ below. Blend your
    // previous and current sim states by this to render between ticks; without
    // it a 120 Hz sim visibly judders on a 144 Hz panel.
    double alpha() const;

    // Leftover un-stepped sim time, in seconds. Always in [0, kSimDt).
    double accumulator() const;

    // Drop all pending sim time. Call after anything that makes the elapsed
    // wall time a lie: a level load, a resume from pause, a replay seek.
    void reset() { pending_ = 0.0; }

private:
    // Un-stepped time in STEP units (1.0 == one sim step), not seconds. Held
    // this way so that `pending_ -= floor(pending_)` is exact and alpha() is
    // provably in [0, 1); holding seconds would mean subtracting the
    // unrepresentable 1/120 and eating rounding slop right on the step
    // boundary. See fixed_step.cpp.
    double pending_ = 0.0;
};

}  // namespace apricot
