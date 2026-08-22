#include "core/fixed_step.h"

#include <cmath>

namespace apricot {

FixedStep::Tick FixedStep::advance(double frame_dt) {
    Tick tick;

    // Written as `!(x > 0)` rather than `x <= 0` on purpose: this form is also
    // true for NaN, so a garbage delta from a misbehaving timer produces a
    // zero-step frame instead of a NaN accumulator that poisons the clock for
    // the rest of the run.
    if (!(frame_dt > 0.0)) return tick;

    // The accumulator is held in STEP units, not seconds. That choice is what
    // makes the arithmetic below exact: `x - floor(x)` is exactly representable
    // in IEEE-754 at these magnitudes, so the remainder is always genuinely in
    // [0, 1) and alpha() needs no clamping. Holding seconds instead means
    // subtracting kSimDt (which is 1/120 and therefore NOT representable), and
    // the rounding slop lands exactly on the step boundary — where it either
    // drops a step or double-counts one.
    pending_ += frame_dt * kSimHz;

    // One floor. NOT a `while (acc >= dt) { acc -= dt; ++n; }` loop: that form
    // derives an integer by repeated float subtraction, costing an iteration
    // per owed step after a hitch and accumulating error into the one quantity
    // the engine's determinism actually rests on.
    const double owed = std::floor(pending_);

    if (owed >= static_cast<double>(kMaxStepsPerFrame)) {
        // Over budget. DISCARD the surplus rather than carry it: carrying means
        // the following frames each run a full 12 steps repaying a debt they
        // can never clear, so one 20-second breakpoint becomes a permanently
        // stuttering session. Sim time may fall behind wall time. It may never
        // spiral.
        tick.steps = kMaxStepsPerFrame;
        tick.clamped = true;
        pending_ = 0.0;
        return tick;
    }

    tick.steps = static_cast<int>(owed);
    pending_ -= owed;  // exact
    return tick;
}

double FixedStep::alpha() const { return pending_; }

double FixedStep::accumulator() const { return pending_ * kSimDt; }

}  // namespace apricot
