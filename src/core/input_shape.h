#pragma once

#include <glm/glm.hpp>

#include <cstdint>

#include "core/input_frame.h"

// Shaping raw device readings into the normalised values an InputFrame holds.
//
// PURE MATHS ONLY. Nothing in here knows what a keyboard, a pad or a window
// is; it takes plain numbers and returns plain numbers. That is not tidiness,
// it is what lets tests/input_latch_tests.cpp drive the REAL latch, deadzone
// and ramp code with no window, no device and no host library — testing a
// hand-written copy of this logic would only prove the copy works.
//
// The host layer (platform/input.h) owns the per-axis state these functions
// thread through, reads the devices, and calls in here.

namespace apricot::input {

// --- tuning ------------------------------------------------------------------
// Deliberately constants rather than settings. Every number below ends up
// baked into the analogue values a replay tape stores, and a tape recorded
// against one deadzone and replayed against another is a tape that desyncs.

// Radial deadzone on a stick, as a fraction of full deflection. A worn pad
// rests around 0.10-0.15; 0.18 covers that without eating usable travel.
inline constexpr float kStickDeadzone = 0.18f;

// Deflection at which the stick reads full. Short of 1.0 on purpose: a stick
// that only reaches 0.97 in the corner can otherwise never ask for full lock,
// and the car understeers for reasons the player cannot see.
inline constexpr float kStickSaturation = 0.95f;

// Triggers rest at zero and only need enough to cover creep.
inline constexpr float kTriggerDeadzone = 0.06f;
inline constexpr float kTriggerSaturation = 0.97f;

// Seconds of held key to travel the full 0..1 of an axis, and to fall back.
// Falls are quicker than rises for the same reason a real pedal returns faster
// than a foot presses: releasing must feel immediate or the car feels late.
//
// These exist so a keyboard and a pad are comparable. A digital key slammed
// straight to 1.0 gives the keyboard player instant full lock, which is not
// "responsive", it is a different and worse car — it cannot be balanced
// against a stick without making one of the two inputs pointless.
inline constexpr float kSteerRiseSeconds = 0.22f;
inline constexpr float kSteerFallSeconds = 0.12f;
inline constexpr float kPedalRiseSeconds = 0.14f;
inline constexpr float kPedalFallSeconds = 0.08f;
inline constexpr float kHandbrakeRiseSeconds = 0.06f;
inline constexpr float kHandbrakeFallSeconds = 0.06f;

// --- small helpers -----------------------------------------------------------
// std::fabs and std::clamp are not constexpr in C++17, and these want to be
// usable in a static_assert.

constexpr float abs_f(float v) { return v < 0.0f ? -v : v; }

constexpr float clamp_f(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// --- device normalisation ----------------------------------------------------

// A signed 16-bit stick axis to [-1, 1].
//
// The device range is ASYMMETRIC: -32768 to +32767. Dividing by 32767 makes
// full deflection one way read -1.00003, and that tiny overshoot survives all
// the way to whichever module asserts its inputs are in range, three systems
// downstream, where it looks like a physics bug. Clamped here, once.
constexpr float normalise_axis16(int32_t raw) {
    return clamp_f(static_cast<float>(raw) * (1.0f / 32767.0f), -1.0f, 1.0f);
}

// A 16-bit trigger reading to [0, 1]. Triggers report 0..32767 on every device
// worth supporting, but a negative reading is clamped rather than trusted:
// throttle must not be able to go backwards because a driver reported nonsense.
constexpr float normalise_trigger16(int32_t raw) {
    return clamp_f(static_cast<float>(raw) * (1.0f / 32767.0f), 0.0f, 1.0f);
}

// --- deadzones ---------------------------------------------------------------

// Scalar deadzone with RESCALING. Below `dz` the axis reads zero; above it the
// remaining travel is stretched back out to a full 0..1.
//
// The rescale is the whole job. Without it the axis sits at 0 and then JUMPS
// to `dz` the instant the stick clears the threshold, so the car snaps to 18%
// steering lock from nothing. Players describe that as the steering being
// "notchy" and it is very hard to hear as "your deadzone does not rescale".
constexpr float apply_deadzone(float v, float dz, float saturation) {
    const float m = abs_f(v);
    if (m <= dz) return 0.0f;
    // A saturation point at or below the deadzone leaves no travel to map. Go
    // fully digital rather than dividing by zero and returning infinity.
    if (saturation <= dz) return v < 0.0f ? -1.0f : 1.0f;
    const float t = clamp_f((m - dz) / (saturation - dz), 0.0f, 1.0f);
    return v < 0.0f ? -t : t;
}

// RADIAL deadzone for a two-axis stick.
//
// Applying the scalar deadzone per axis is the common and wrong version: it
// carves a SQUARE hole out of a ROUND stick. Push straight up and you clear
// the threshold on Y with X still inside it, so X reads exactly zero; ease off
// a few degrees and X snaps in. The steering twitches on small corrections,
// which are the only corrections that matter at speed. One length, one
// threshold, one rescale — round hole, round stick.
inline glm::vec2 apply_stick_deadzone(const glm::vec2& v, float dz,
                                      float saturation) {
    const float m = glm::length(v);
    if (m <= dz || m <= 0.0f) return glm::vec2{0.0f};
    if (saturation <= dz) return v / m;
    const float t = clamp_f((m - dz) / (saturation - dz), 0.0f, 1.0f);
    return v * (t / m);  // rescaled magnitude, direction preserved exactly
}

// --- analogue ramps ----------------------------------------------------------

// Move `current` one frame's worth toward `target`.
//
// LINEAR, not exponential. An exponential ease (`current += (target - current)
// * k`) never actually arrives, so a held key asymptotes at 0.98 and full
// throttle is unreachable by a keyboard player forever. It is also only
// frame-rate independent if you write the pow() form, which nobody does. A
// linear rate arrives exactly, arrives on schedule, and gives the same answer
// whether the frame was subdivided or not — which tests/input_latch_tests.cpp
// pins.
//
// `rise_seconds` applies when the magnitude is growing, `fall_seconds` when it
// is shrinking, so returning to centre can be quicker than leaving it.
constexpr float ramp_toward(float current, float target, float dt,
                            float rise_seconds, float fall_seconds) {
    // Written as !(dt > 0) so a NaN dt is inert rather than poisoning the axis
    // for the rest of the run.
    if (!(dt > 0.0f)) return current;

    const float seconds =
        (abs_f(target) > abs_f(current)) ? rise_seconds : fall_seconds;
    if (!(seconds > 0.0f)) return target;  // zero ramp time means instant

    const float step = dt / seconds;
    const float delta = target - current;
    if (abs_f(delta) <= step) return target;  // arrive exactly, never overshoot
    return current + (delta > 0.0f ? step : -step);
}

// --- button latching ---------------------------------------------------------

// Fold a button transition into a frame.
//
// The edge is latched only on a genuine DOWN transition. Devices repeat their
// down events while a button is held, and without the transition check a hold
// re-latches `pressed` on every repeat, making a tap and a hold indistinguish-
// able to the sim.
//
// `pressed` is NEVER cleared here. It is cleared by clear_edges(), once, after
// a frame that actually ran a sim step — see core/input_frame.h. This function
// only ever ORs bits in.
constexpr void latch_button(InputFrame& f, uint32_t bit, bool down) {
    if (down) {
        if ((f.held & bit) == 0u) f.pressed |= bit;
        f.held |= bit;
    } else {
        f.held &= ~bit;
    }
}

}  // namespace apricot::input
