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

#include <algorithm>
#include <array>
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
            REQUIRE(a.snow == b.snow);
            REQUIRE(a.snow_depth_m == b.snow_depth_m);
            REQUIRE(a.snow_cover == b.snow_cover);
            REQUIRE(a.flood == b.flood);
            REQUIRE(a.hail == b.hail);
            REQUIRE(a.heatwave == b.heatwave);
            REQUIRE(a.lightning == b.lightning);
            REQUIRE(a.tornado_intensity == b.tornado_intensity);
            REQUIRE(a.tornado_center_m == b.tornado_center_m);
            REQUIRE(a.wetness == b.wetness);
            REQUIRE(a.grip == b.grip);
            REQUIRE(a.overcast == b.overcast);
            REQUIRE(a.fog == b.fog);
            REQUIRE(a.wind_mps == b.wind_mps);
            REQUIRE(a.atmosphere == b.atmosphere);
            REQUIRE(a.episode_duration_seconds == b.episode_duration_seconds);
            REQUIRE(a.transition_progress == b.transition_progress);
            REQUIRE(a.sun_elevation == b.sun_elevation);
            REQUIRE(a.weather == b.weather);
            REQUIRE(a.daylight == b.daylight);

            REQUIRE_MSG(a.time_of_day >= 0.0f && a.time_of_day < 1.0f,
                        "time of day stays on the clock face", "step");
            REQUIRE(a.rain >= 0.0f && a.rain <= 1.0f);
            REQUIRE(a.snow >= 0.0f && a.snow <= 1.0f);
            REQUIRE(a.snow_depth_m >= 0.0f && a.snow_depth_m <= 1.5f);
            REQUIRE(a.snow_cover >= 0.0f && a.snow_cover <= 1.0f);
            REQUIRE(a.flood >= 0.0f && a.flood <= 1.0f);
            REQUIRE(a.hail >= 0.0f && a.hail <= 1.0f);
            REQUIRE(a.heatwave >= 0.0f && a.heatwave <= 1.0f);
            REQUIRE(a.lightning >= 0.0f && a.lightning <= 1.0f);
            REQUIRE(a.tornado_intensity >= 0.0f && a.tornado_intensity <= 1.0f);
            REQUIRE(std::fabs(a.tornado_center_m.x) <= kTornadoTrackRadiusMeters + 0.001f);
            REQUIRE(std::fabs(a.tornado_center_m.y) <= kTornadoTrackRadiusMeters + 0.001f);
            REQUIRE(a.wetness >= 0.0f && a.wetness <= 1.0f);
            REQUIRE(a.overcast >= 0.0f && a.overcast <= 1.0f);
            REQUIRE(a.fog >= 0.0f && a.fog <= 1.0f);
            REQUIRE(glm::length(a.wind_mps) <= 22.001f);
            REQUIRE(a.episode_duration_seconds >= 100.0f);
            REQUIRE(a.episode_duration_seconds <= 380.0f);
            REQUIRE(a.transition_progress >= 0.0f && a.transition_progress <= 1.0f);
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

    REQUIRE_NEAR(kSecondsPerDay / (24.0 * 60.0), 2.0, 1e-9);

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

void test_severe_weather_requires_headlights_in_daylight() {
    REQUIRE(automatic_headlight_level(
                1.0f, AtmosphericWeather::Clear) == 0.0f);
    REQUIRE(automatic_headlight_level(
                1.0f, AtmosphericWeather::Rain) == 0.0f);
    REQUIRE(automatic_headlight_level(
                1.0f, AtmosphericWeather::Snow) == 0.0f);
    REQUIRE(automatic_headlight_level(
                1.0f, AtmosphericWeather::Storm) == 1.0f);
    REQUIRE(automatic_headlight_level(
                1.0f, AtmosphericWeather::Thunderstorm) == 1.0f);
    REQUIRE(automatic_headlight_level(
                1.0f, AtmosphericWeather::Blizzard) == 1.0f);
    REQUIRE(automatic_headlight_level(
                -1.0f, AtmosphericWeather::Clear) == 1.0f);
    apricot_test::pass(
        "storms, thunderstorms and blizzards require headlights in daylight");
}

// Exercise the actual timeline at one-second intervals, including episode
// boundaries and the dry-out after a shower. This catches a renderer-facing
// discontinuity that a broad range/determinism test would miss.
void test_weather_fronts_are_gradual_and_coherent() {
    bool saw_rain = false;
    bool saw_drying = false;
    bool saw_cloud_building = false;
    for (int i = 0; i < 4; ++i) {
        const uint64_t seed = nth_seed(i);
        Conditions previous = conditions_at(seed, 0);
        for (uint64_t second = 1; second <= 3600; ++second) {
            const Conditions c = conditions_at(seed, second * 120u);
            REQUIRE(std::fabs(c.rain - previous.rain) < 0.06f);
            REQUIRE(std::fabs(c.snow - previous.snow) < 0.05f);
            REQUIRE(std::fabs(c.snow_depth_m - previous.snow_depth_m) < 0.01f);
            REQUIRE(std::fabs(c.snow_cover - previous.snow_cover) < 0.05f);
            REQUIRE(std::fabs(c.flood - previous.flood) < 0.05f);
            REQUIRE(std::fabs(c.hail - previous.hail) < 0.05f);
            REQUIRE(std::fabs(c.heatwave - previous.heatwave) < 0.05f);
            REQUIRE(std::fabs(c.lightning - previous.lightning) < 0.05f);
            REQUIRE(std::fabs(c.tornado_intensity - previous.tornado_intensity) < 0.05f);
            REQUIRE(glm::length(c.tornado_center_m - previous.tornado_center_m) < 41.0f);
            REQUIRE(std::fabs(c.overcast - previous.overcast) < 0.06f);
            REQUIRE(std::fabs(c.fog - previous.fog) < 0.04f);
            REQUIRE(std::fabs(c.wetness - previous.wetness) < 0.06f);
            REQUIRE(glm::length(c.wind_mps - previous.wind_mps) < 1.7f);
            if (c.rain > 0.05f) {
                REQUIRE(c.overcast > 0.45f);
                REQUIRE(c.wetness >= c.rain);
                saw_rain = true;
            }
            if (c.rain == 0.0f && c.wetness > 0.05f) saw_drying = true;
            if (c.rain == 0.0f && c.overcast > previous.overcast + 0.001f)
                saw_cloud_building = true;
            previous = c;
        }
    }
    REQUIRE(saw_rain);
    REQUIRE(saw_drying);
    REQUIRE(saw_cloud_building);
    apricot_test::pass("fronts build gradually and roads stay wet after rain stops");
}

void test_weather_episode_lengths_and_sequence_vary() {
    constexpr std::size_t kAtmosphereCount =
        static_cast<std::size_t>(AtmosphericWeather::Heatwave) + 1u;
    std::array<bool, kAtmosphereCount> seen{};
    float shortest = 1000.0f;
    float longest = 0.0f;
    bool different_seeds = false;
    bool clouds_change_within_minutes = false;
    const uint64_t seed = nth_seed(3);
    const Conditions opening = conditions_at(seed, 0);
    for (int seed_index = 0; seed_index < 12; ++seed_index) {
        for (uint64_t second = 0; second < 28800; second += 53) {
            const Conditions c = conditions_at(nth_seed(seed_index), second * 120u);
            const Conditions other = conditions_at(nth_seed(seed_index + 1), second * 120u);
            if (c.transition_progress >= 1.0f) {
                seen[static_cast<std::size_t>(c.atmosphere)] = true;
                switch (c.atmosphere) {
                    case AtmosphericWeather::Clear:
                        REQUIRE(c.rain == 0.0f && c.overcast < 0.13f);
                        break;
                    case AtmosphericWeather::Overcast:
                        REQUIRE(c.rain == 0.0f && c.overcast >= 0.45f);
                        break;
                    case AtmosphericWeather::Rain:
                        REQUIRE(c.rain >= 0.22f && c.overcast >= 0.85f);
                        break;
                    case AtmosphericWeather::Storm:
                        REQUIRE(c.rain >= 0.72f && glm::length(c.wind_mps) >= 7.0f);
                        break;
                    case AtmosphericWeather::Snow:
                        REQUIRE(c.snow >= 0.35f && c.rain == 0.0f);
                        break;
                    case AtmosphericWeather::Blizzard:
                        REQUIRE(c.snow >= 0.78f && glm::length(c.wind_mps) >= 13.0f);
                        break;
                    case AtmosphericWeather::Thunderstorm:
                        REQUIRE(c.rain >= 0.72f && c.lightning >= 0.55f);
                        break;
                    case AtmosphericWeather::Tornado:
                        REQUIRE(c.tornado_intensity >= 0.50f &&
                                glm::length(c.wind_mps) >= 15.0f);
                        break;
                    case AtmosphericWeather::Flood:
                        REQUIRE(c.flood >= 0.50f && c.wetness >= c.flood);
                        break;
                    case AtmosphericWeather::Hail:
                        REQUIRE(c.hail >= 0.55f && c.rain >= 0.35f);
                        break;
                    case AtmosphericWeather::Heatwave:
                        REQUIRE(c.heatwave >= 0.60f && c.rain == 0.0f);
                        break;
                }
            }
            shortest = std::min(shortest, c.episode_duration_seconds);
            longest = std::max(longest, c.episode_duration_seconds);
            if (c.atmosphere != other.atmosphere &&
                c.episode_duration_seconds != other.episode_duration_seconds)
                different_seeds = true;
            if (seed_index == 3 && second <= 600 &&
                std::fabs(c.overcast - opening.overcast) > 0.2f)
                clouds_change_within_minutes = true;
        }
    }
    for (const bool present : seen) REQUIRE(present);
    REQUIRE(longest - shortest > 100.0f);
    REQUIRE(different_seeds);
    REQUIRE(clouds_change_within_minutes);
    std::printf("      episode durations %.1f..%.1f seconds\n",
                static_cast<double>(shortest), static_cast<double>(longest));
    apricot_test::pass("all settled event kinds are covered and internally coherent");
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
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Clear), "clear") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Overcast), "overcast") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Rain), "rain") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Storm), "storm") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Snow), "snow") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Blizzard), "blizzard") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Thunderstorm), "thunderstorm") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Tornado), "tornado") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Flood), "flood") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Hail), "hail") == 0);
    REQUIRE(std::strcmp(atmospheric_weather_name(AtmosphericWeather::Heatwave), "heatwave") == 0);
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
    test_severe_weather_requires_headlights_in_daylight();
    test_weather_fronts_are_gradual_and_coherent();
    test_weather_episode_lengths_and_sequence_vary();
    test_conditions_feed_the_grip_term();
    test_the_active_condition_is_named();
    return apricot_test::done("conditions_tests");
}
