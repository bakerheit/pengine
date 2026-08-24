#pragma once

#include <cstdint>

#include "physics/vehicle.h"

namespace apricot {

// Session conditions: where the sun is, how hard it is raining, and how much
// grip that leaves.
//
// A PURE FUNCTION OF (seed, sim step). Not an accumulator, and that is not a
// shortcut — it is the only shape that survives a replay.
//
// If the weather were integrated into mutable session state, a tape replayed
// from a different point in the session would be handed different grip and
// would stop reproducing the run it recorded — and the symptom would read as
// "replays drift", which is the worst week of anyone's life. Keying conditions
// to the ABSOLUTE step index instead means a tape carries the step it started
// at (ReplayTape::start_step, core/replay_tape.h) and gets its own weather
// back, exactly.

// One in-game day. Twenty minutes of sim, so a long session actually sees dusk
// arrive rather than hearing about it.
inline constexpr double kSecondsPerDay = 1200.0;

// How long one weather lattice cell lasts. Fronts move on roughly this scale;
// showers inside a front are faster.
inline constexpr double kSecondsPerWeatherBeat = 90.0;

// Grip loss coefficients, applied against the dry baseline of 1.0.
inline constexpr float kWetGripLoss = 0.22f;
inline constexpr float kRainGripLoss = 0.10f;
inline constexpr float kNightGripLoss = 0.04f;
inline constexpr float kMinGrip = 0.55f;

// How wet the SURFACE is, which is what a driver actually feels. Distinct from
// how hard it happens to be raining this second.
enum class Weather : uint8_t { Dry, Damp, Wet };

enum class Daylight : uint8_t { Night, Dawn, Day, Dusk };

struct Conditions {
    // [0, 1). 0.0 = midnight, 0.25 = dawn, 0.5 = noon, 0.75 = dusk.
    float time_of_day = 0.5f;

    // [-1, 1]. Sine of the sun's elevation; negative is below the horizon. A
    // renderer wants this more often than it wants the raw clock.
    float sun_elevation = 1.0f;

    float rain = 0.0f;      // [0, 1] falling right now
    float wetness = 0.0f;   // [0, 1] standing on the surface
    float grip = 1.0f;      // [kMinGrip, 1] multiplier into the tyre model

    // [0, 1]. How much the car's lights should be doing. Here rather than in
    // the renderer so a replay lights up at the same step it did live.
    float headlight_level = 0.0f;

    Weather weather = Weather::Dry;
    Daylight daylight = Daylight::Day;
};

// The conditions in force at an absolute sim step. Pure: same (seed, step)
// gives the same struct on every machine, every run, in any order.
Conditions conditions_at(uint64_t seed, uint64_t step);

const char* weather_name(Weather w);
const char* daylight_name(Daylight d);

// Fold the conditions into the tuning handed to step_vehicle.
//
// Weather reaches the car through VehicleTuning::grip_scale — the tyre
// friction circle — and through rolling resistance in standing water, and
// nowhere else. It does NOT touch engine or brake torque: doing both would
// apply the weather twice, once to the tyre that is actually sliding and again
// to an engine that does not know what it is driving on. See the note in
// conditions.cpp; it is pinned by tests/conditions_tests.cpp.
VehicleTuning conditioned_tuning(const VehicleTuning& base, const Conditions& c);

}  // namespace apricot
