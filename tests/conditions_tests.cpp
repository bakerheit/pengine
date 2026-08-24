// Session conditions: time of day, weather, and the grip they leave the tyres.
//
// These assertions used to live in rally_hud_tests.cpp, which went with the
// rally. Nothing in them was ever about the rally: conditions_at() is a pure
// function of (seed, sim step) and conditioned_tuning() folds its result into
// VehicleTuning, and both outlive whichever game is on top. They are carried
// across verbatim rather than rewritten, because a rewritten test is a test
// nobody has watched fail.
//
// The purity claim is the one that matters. A weather system that ACCUMULATED
// would hand a replayed run different grip than the run it recorded, and the
// symptom reads as "replays drift" a long way from the cause.

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/fixed_step.h"
#include "core/rng.h"
#include "game/conditions.h"
#include "physics/vehicle.h"
#include "test_assert.h"

using namespace apricot;

namespace {

uint64_t nth_seed(int i) {
    return splitmix64_mix(0xB0A7C10Dull + static_cast<uint64_t>(i));
}

void test_conditions_are_pure_and_bounded() {
    for (int i = 0; i < 8; ++i) {
        const uint64_t seed = nth_seed(i);

        // Sampled out of order on purpose: a pure function does not care, and
        // an accumulator would fall apart here immediately.
        const uint64_t steps[] = {0u, 500000u, 1u, 120u, 250000u, 7u, 999983u};
        for (const uint64_t s : steps) {
            const Conditions a = conditions_at(seed, s);
            const Conditions b = conditions_at(seed, s);

            REQUIRE_MSG(a.time_of_day == b.time_of_day, "time of day is pure",
                        "step");
            REQUIRE(a.rain == b.rain);
            REQUIRE(a.wetness == b.wetness);
            REQUIRE(a.grip == b.grip);
            REQUIRE(a.sun_elevation == b.sun_elevation);
            REQUIRE(a.weather == b.weather);
            REQUIRE(a.daylight == b.daylight);

            REQUIRE_MSG(a.time_of_day >= 0.0f && a.time_of_day < 1.0f,
                        "time of day stays on the clock face", "step");
            REQUIRE(a.rain >= 0.0f && a.rain <= 1.0f);
            REQUIRE(a.wetness >= 0.0f && a.wetness <= 1.0f);
            REQUIRE(a.grip >= kMinGrip && a.grip <= 1.0f);
            REQUIRE(a.headlight_level >= 0.0f && a.headlight_level <= 1.0f);
            REQUIRE(a.sun_elevation >= -1.0001f && a.sun_elevation <= 1.0001f);

            // Rain only ever falls on ground the front has already wet.
            if (a.wetness == 0.0f) REQUIRE(a.rain == 0.0f);
        }
    }
    apricot_test::pass("conditions are a pure, bounded function of (seed, step)");
}

void test_conditions_advance_over_a_session() {
    const uint64_t seed = nth_seed(2);

    // Quarter of a game day should visibly move the sun.
    const uint64_t quarter =
        static_cast<uint64_t>(kSimHz * kSecondsPerDay * 0.25);
    const Conditions dawn = conditions_at(seed, 0);
    const Conditions later = conditions_at(seed, quarter);

    REQUIRE_MSG(dawn.time_of_day != later.time_of_day,
                "the clock moves over a session", "day");
    REQUIRE_NEAR(static_cast<double>(later.time_of_day - dawn.time_of_day), 0.25,
                 1e-3);

    // And the weather is not a constant either. Walk a long session and check
    // that grip actually varies rather than sitting at 1.0 forever.
    float lo = 2.0f;
    float hi = -1.0f;
    bool saw_wet = false;
    bool saw_night = false;
    for (uint64_t s = 0; s < 2000000u; s += 601u) {
        const Conditions c = conditions_at(seed, s);
        if (c.grip < lo) lo = c.grip;
        if (c.grip > hi) hi = c.grip;
        if (c.weather != Weather::Dry) saw_wet = true;
        if (c.daylight == Daylight::Night) saw_night = true;
    }
    std::printf("      grip ranged %.3f..%.3f over the sampled session\n",
                static_cast<double>(lo), static_cast<double>(hi));
    REQUIRE_MSG(hi > lo, "grip is not a constant", "session");
    REQUIRE_MSG(saw_wet, "the session sees weather", "session");
    REQUIRE_MSG(saw_night, "the session sees night", "session");

    apricot_test::pass("time of day and rain advance over a session");
}

void test_conditions_feed_the_grip_term() {
    VehicleTuning base;

    Conditions dry;
    dry.grip = 1.0f;
    dry.wetness = 0.0f;
    const VehicleTuning on_dry = conditioned_tuning(base, dry);
    REQUIRE(on_dry.grip_scale == base.grip_scale);
    REQUIRE(on_dry.rolling_resistance == base.rolling_resistance);

    Conditions soaked;
    soaked.grip = 0.6f;
    soaked.wetness = 1.0f;
    const VehicleTuning on_wet = conditioned_tuning(base, soaked);
    REQUIRE_MSG(on_wet.grip_scale < base.grip_scale,
                "a wet track gives the tyres less to work with", "grip");
    REQUIRE_MSG(on_wet.rolling_resistance > base.rolling_resistance,
                "standing water drags", "grip");

    // Weather reaches the car through the friction circle and NOWHERE else.
    // This used to scale engine and brake force as a stand-in, back when the
    // step had a single engine_force; with a real tyre model that applies the
    // weather twice. Pinned here so it does not creep back.
    REQUIRE_MSG(on_wet.engine_peak_torque == base.engine_peak_torque,
                "weather does not reach into the engine", "grip");
    REQUIRE_MSG(on_wet.brake_torque == base.brake_torque,
                "weather does not reach into the brakes", "grip");

    // The rest of the setup is passed through untouched — this is a grip
    // term, not a second tuning file.
    REQUIRE(on_wet.mass_kg == base.mass_kg);
    REQUIRE(on_wet.max_steer == base.max_steer);
    REQUIRE(on_wet.suspension_rest == base.suspension_rest);

    apricot_test::pass("conditions feed the grip term handed to the vehicle");
}

void test_the_active_condition_is_named() {
    // A renderer and a HUD both need to say what is going on, not infer it.
    REQUIRE(std::strcmp(weather_name(Weather::Dry), "dry") == 0);
    REQUIRE(std::strcmp(weather_name(Weather::Damp), "damp") == 0);
    REQUIRE(std::strcmp(weather_name(Weather::Wet), "wet") == 0);
    REQUIRE(std::strcmp(daylight_name(Daylight::Night), "night") == 0);
    REQUIRE(std::strcmp(daylight_name(Daylight::Dawn), "dawn") == 0);
    REQUIRE(std::strcmp(daylight_name(Daylight::Day), "day") == 0);
    REQUIRE(std::strcmp(daylight_name(Daylight::Dusk), "dusk") == 0);
    apricot_test::pass("the active condition is exposed by name");
}

}  // namespace

int main() {
    std::printf("conditions_tests\n");
    test_conditions_are_pure_and_bounded();
    test_conditions_advance_over_a_session();
    test_conditions_feed_the_grip_term();
    test_the_active_condition_is_named();
    return apricot_test::done("conditions_tests");
}
