#include "game/conditions.h"

#include <cmath>

#include "core/fixed_step.h"
#include "core/rng.h"
#include "game/snowpack.h"
#include "game/weather_hazards.h"

namespace apricot {
namespace {

// kTwoPi is core's, from core/rng.h included above. A local copy here was a
// redefinition the moment PENG-3 landed one — and the two spellings did not
// even agree past the ninth digit.

// Episode lookup is constant-time even when a replay seeks hours ahead. Each
// nominal four-minute boundary has an independent +/-70 second offset, so
// intervals vary without accumulating a scheduler or repeating a fixed cycle.
constexpr double kBoundaryJitterSeconds = 70.0;

float episode_random(uint64_t seed, int64_t episode, uint64_t channel) {
    const uint64_t h = splitmix64_mix(seed ^
        splitmix64_mix(static_cast<uint64_t>(episode) + 0x9E3779B97F4A7C15ull) ^
        (channel * 0xD6E8FEB86659FD93ull));
    return static_cast<float>(h >> 40) * (1.0f / 16777216.0f);
}

double episode_start(uint64_t seed, int64_t episode) {
    return static_cast<double>(episode) * kSecondsPerWeatherBeat +
        (static_cast<double>(episode_random(seed, episode, 1u)) * 2.0 - 1.0) *
        kBoundaryJitterSeconds;
}

float smooth_range(float lo, float hi, float value) {
    const float t = glm::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Atmosphere {
    float rain = 0.0f;
    float snow = 0.0f;
    float flood = 0.0f;
    float hail = 0.0f;
    float heatwave = 0.0f;
    float lightning = 0.0f;
    float tornado = 0.0f;
    float overcast = 0.0f;
    float fog = 0.0f;
    glm::vec2 wind_mps{0.0f};
    AtmosphericWeather kind = AtmosphericWeather::Clear;
    float duration = 0.0f;
    float transition = 1.0f;
};

Atmosphere episode_target(uint64_t seed, int64_t episode) {
    Atmosphere a;
    const float choice = episode_random(seed, episode, 2u);
    const float strength = episode_random(seed, episode, 3u);
    float speed = 0.0f;
    // Keep the original clear/overcast/rain/storm distribution as the base.
    // A separate roll promotes some episodes into severe weather, so ordinary
    // fronts retain their established tuning and severe events stay uncommon.
    if (choice < 0.35f) {
        a.kind = AtmosphericWeather::Clear;
        a.overcast = strength * 0.12f;
        a.fog = 0.02f + strength * 0.06f;
        speed = 0.8f + strength * 1.6f;
    } else if (choice < 0.65f) {
        a.kind = AtmosphericWeather::Overcast;
        a.overcast = 0.45f + strength * 0.35f;
        a.fog = 0.12f + strength * 0.18f;
        speed = 2.0f + strength * 2.0f;
    } else if (choice < 0.92f) {
        a.kind = AtmosphericWeather::Rain;
        a.overcast = 0.85f + strength * 0.15f;
        a.rain = 0.22f + strength * 0.43f;
        a.fog = 0.25f + strength * 0.25f;
        speed = 3.0f + strength * 3.0f;
    } else {
        a.kind = AtmosphericWeather::Storm;
        a.overcast = 1.0f;
        a.rain = 0.72f + strength * 0.28f;
        a.fog = 0.50f + strength * 0.20f;
        speed = 7.0f + strength * 4.0f;
    }
    const float severe = episode_random(seed, episode, 9u);
    if (severe < 0.28f) {
        const int event = static_cast<int>(severe / 0.04f);
        switch (event) {
            case 0:  // Snow
                a.kind = AtmosphericWeather::Snow;
                a.rain = 0.0f;
                a.snow = 0.35f + strength * 0.45f;
                a.overcast = 0.72f + strength * 0.20f;
                a.fog = 0.22f + strength * 0.25f;
                speed = 2.5f + strength * 3.5f;
                break;
            case 1:  // Blizzard
                a.kind = AtmosphericWeather::Blizzard;
                a.rain = 0.0f;
                a.snow = 0.78f + strength * 0.22f;
                a.overcast = 0.94f + strength * 0.06f;
                a.fog = 0.70f + strength * 0.30f;
                speed = 13.0f + strength * 9.0f;
                break;
            case 2:  // Thunderstorm
                a.kind = AtmosphericWeather::Thunderstorm;
                a.rain = 0.72f + strength * 0.28f;
                a.lightning = 0.55f + strength * 0.45f;
                a.overcast = 0.96f + strength * 0.04f;
                a.fog = 0.45f + strength * 0.25f;
                speed = 8.0f + strength * 6.0f;
                break;
            case 3:  // Tornado
                a.kind = AtmosphericWeather::Tornado;
                a.rain = 0.48f + strength * 0.32f;
                a.lightning = 0.25f + strength * 0.35f;
                a.tornado = 0.50f + strength * 0.50f;
                a.overcast = 1.0f;
                a.fog = 0.48f + strength * 0.24f;
                speed = 15.0f + strength * 7.0f;
                break;
            case 4:  // Flood
                a.kind = AtmosphericWeather::Flood;
                a.rain = 0.70f + strength * 0.30f;
                a.flood = 0.50f + strength * 0.50f;
                a.overcast = 0.92f + strength * 0.08f;
                a.fog = 0.52f + strength * 0.28f;
                speed = 5.0f + strength * 5.0f;
                break;
            case 5:  // Hail
                a.kind = AtmosphericWeather::Hail;
                a.rain = 0.35f + strength * 0.30f;
                a.hail = 0.55f + strength * 0.45f;
                a.lightning = 0.15f + strength * 0.30f;
                a.overcast = 0.90f + strength * 0.10f;
                a.fog = 0.32f + strength * 0.20f;
                speed = 7.0f + strength * 7.0f;
                break;
            default:  // Heatwave
                a.kind = AtmosphericWeather::Heatwave;
                a.rain = 0.0f;
                a.heatwave = 0.60f + strength * 0.40f;
                a.overcast = strength * 0.16f;
                a.fog = 0.06f + strength * 0.12f;
                speed = 0.5f + strength * 2.0f;
                break;
        }
    }
    const float prevailing = episode_random(seed, 0, 8u) * kTwoPi;
    const float direction = prevailing +
        (episode_random(seed, episode, 4u) - 0.5f) * 2.0f;
    a.wind_mps = glm::vec2{std::cos(direction), std::sin(direction)} * speed;
    return a;
}

Atmosphere atmosphere_at_seconds(uint64_t seed, double seconds) {
    int64_t episode = static_cast<int64_t>(std::floor(seconds / kSecondsPerWeatherBeat));
    if (seconds < episode_start(seed, episode)) --episode;
    else if (seconds >= episode_start(seed, episode + 1)) ++episode;
    const double start = episode_start(seed, episode);
    const Atmosphere from = episode_target(seed, episode - 1);
    const Atmosphere to = episode_target(seed, episode);
    const float transition_seconds = 40.0f + episode_random(seed, episode, 5u) * 40.0f;
    const float u = glm::clamp(static_cast<float>((seconds - start) /
        static_cast<double>(transition_seconds)), 0.0f, 1.0f);
    const float blend = smooth_range(0.0f, 1.0f, u);
    // Clouds arrive before precipitation. On clearing, rain stops before the
    // cloud deck breaks. Even a clear-to-storm draw passes through a gradual
    // cloudy, light-rain stage instead of suddenly switching on a downpour.
    const bool worsening = to.rain > from.rain;
    const float cloud_blend = worsening ? smooth_range(0.0f, 0.65f, u)
                                       : smooth_range(0.30f, 1.0f, u);
    const float rain_blend = worsening ? smooth_range(0.30f, 1.0f, u)
                                      : smooth_range(0.0f, 0.65f, u);
    Atmosphere a;
    a.rain = glm::mix(from.rain, to.rain, rain_blend);
    a.snow = glm::mix(from.snow, to.snow, blend);
    a.flood = glm::mix(from.flood, to.flood, blend);
    a.hail = glm::mix(from.hail, to.hail, blend);
    a.heatwave = glm::mix(from.heatwave, to.heatwave, blend);
    a.lightning = glm::mix(from.lightning, to.lightning, blend);
    a.tornado = glm::mix(from.tornado, to.tornado, blend);
    a.overcast = glm::mix(from.overcast, to.overcast, cloud_blend);
    a.fog = glm::mix(from.fog, to.fog, blend);
    a.wind_mps = glm::mix(from.wind_mps, to.wind_mps, blend);
    a.kind = to.kind;
    a.duration = static_cast<float>(episode_start(seed, episode + 1) - start);
    a.transition = u;
    return a;
}

glm::vec2 tornado_center_at(uint64_t seed, double seconds) {
    // Two incommensurate, seed-shifted orbits make a continuous compact track
    // across the playable weather domain without mutable position state.
    const double phase_x = static_cast<double>(episode_random(seed, 0, 10u)) * kTwoPi;
    const double phase_z = static_cast<double>(episode_random(seed, 0, 11u)) * kTwoPi;
    const double x_period = 900.0 + episode_random(seed, 0, 12u) * 600.0;
    const double z_period = 1050.0 + episode_random(seed, 0, 13u) * 600.0;
    return {kTornadoTrackRadiusMeters * std::sin(phase_x + kTwoPi * seconds / x_period),
            kTornadoTrackRadiusMeters * std::sin(phase_z + kTwoPi * seconds / z_period)};
}

SnowpackState snowpack_at_seconds(uint64_t seed, double seconds) {
    // Rebuild a short physical history from the same pure atmosphere timeline
    // instead of keeping a hidden accumulator. This preserves arbitrary replay
    // seeks while still making the pack build and melt over simulated time.
    // Sixteen 75-second slices cover twenty real minutes (ten in-game hours).
    // A continuous blizzard reaches roughly 30 cm; deeper collision QA is
    // available through the explicit developer snow-depth control.
    constexpr int kHistorySlices = 16;
    constexpr double kSliceSeconds = 75.0;
    SnowpackState pack;
    for (int slice = kHistorySlices; slice > 0; --slice) {
        const double sample_seconds =
            seconds - (static_cast<double>(slice) - 0.5) * kSliceSeconds;
        const Atmosphere sample = atmosphere_at_seconds(seed, sample_seconds);
        pack = advance_snowpack(pack, sample.snow, sample.heatwave,
                                kSliceSeconds);
    }
    return pack;
}

}  // namespace

Conditions conditions_at(uint64_t seed, uint64_t step) {
    const double seconds = static_cast<double>(step) / kSimHz;

    Conditions c;

    // --- time of day ---------------------------------------------------------
    // Each seed starts its session somewhere in the afternoon, so a long run
    // drives into dusk instead of always starting at the same hour.
    Rng clock_rng{splitmix64_mix(seed ^ 0x54494D4530ull)};  // "TIME0"
    const double start_tod = static_cast<double>(clock_rng.range(0.42f, 0.68f));

    double tod = start_tod + seconds / kSecondsPerDay;
    tod -= std::floor(tod);
    c.time_of_day = static_cast<float>(tod);

    // Peaks at noon (0.5), zero at dawn (0.25) and dusk (0.75).
    c.sun_elevation = std::sin(kTwoPi * (c.time_of_day - 0.25f));

    if (c.sun_elevation > 0.15f) {
        c.daylight = Daylight::Day;
    } else if (c.sun_elevation < -0.15f) {
        c.daylight = Daylight::Night;
    } else {
        // Between the two thresholds, which side of the day it is decides.
        c.daylight = (c.time_of_day < 0.5f) ? Daylight::Dawn : Daylight::Dusk;
    }

    // --- weather -------------------------------------------------------------
    const Atmosphere atmosphere = atmosphere_at_seconds(seed, seconds);
    c.rain = atmosphere.rain;
    c.snow = atmosphere.snow;
    c.flood = atmosphere.flood;
    c.hail = atmosphere.hail;
    c.heatwave = atmosphere.heatwave;
    c.lightning = atmosphere.lightning;
    c.tornado_intensity = atmosphere.tornado;
    c.tornado_center_m = tornado_center_at(seed, seconds);
    c.overcast = atmosphere.overcast;
    c.fog = atmosphere.fog;
    c.wind_mps = atmosphere.wind_mps;
    c.atmosphere = atmosphere.kind;
    c.headlight_level = automatic_headlight_level(
        c.sun_elevation, c.atmosphere);
    c.episode_duration_seconds = atmosphere.duration;
    c.transition_progress = atmosphere.transition;

    // A short, weighted history leaves roads wet for several minutes after
    // rain ends. Sampling the same pure timeline (including before session
    // start) preserves replay seeking without a mutable dry-out accumulator.
    float recent_rain = c.rain;
    float recent_flood = c.flood;
    float weight_sum = 1.0f;
    for (int i = 1; i <= 12; ++i) {
        const float weight = 1.0f - static_cast<float>(i) / 13.0f;
        const Atmosphere recent = atmosphere_at_seconds(
            seed, seconds - static_cast<double>(i) * 15.0);
        recent_rain += recent.rain * weight;
        recent_flood += recent.flood * weight;
        weight_sum += weight;
    }
    const SnowpackState snowpack = snowpack_at_seconds(seed, seconds);
    c.snow_depth_m = static_cast<float>(snowpack.depth_m());
    c.snow_cover = visual_snow_cover_from_depth(c.snow_depth_m);
    c.flood = glm::clamp(glm::max(c.flood,
        recent_flood / weight_sum * 1.15f), 0.0f, 1.0f);
    c.wetness = glm::clamp(glm::max(c.rain,
        glm::max(c.flood, recent_rain / weight_sum * 1.35f)), 0.0f, 1.0f);

    if (c.wetness < 0.05f) {
        c.weather = Weather::Dry;
    } else if (c.wetness < 0.40f) {
        c.weather = Weather::Damp;
    } else {
        c.weather = Weather::Wet;
    }

    // --- grip ----------------------------------------------------------------
    const float night = glm::clamp(-c.sun_elevation * 2.0f, 0.0f, 1.0f);
    const float loss = kWetGripLoss * c.wetness + kRainGripLoss * c.rain +
                       kSnowGripLoss * c.snow_cover +
                       kFloodGripLoss * c.flood + kHailGripLoss * c.hail +
                       kNightGripLoss * night;
    c.grip = glm::clamp(1.0f - loss, kMinGrip, 1.0f);

    return c;
}

const char* atmospheric_weather_name(AtmosphericWeather w) {
    switch (w) {
        case AtmosphericWeather::Clear: return "clear";
        case AtmosphericWeather::Overcast: return "overcast";
        case AtmosphericWeather::Rain: return "rain";
        case AtmosphericWeather::Storm: return "storm";
        case AtmosphericWeather::Snow: return "snow";
        case AtmosphericWeather::Blizzard: return "blizzard";
        case AtmosphericWeather::Thunderstorm: return "thunderstorm";
        case AtmosphericWeather::Tornado: return "tornado";
        case AtmosphericWeather::Flood: return "flood";
        case AtmosphericWeather::Hail: return "hail";
        case AtmosphericWeather::Heatwave: return "heatwave";
    }
    return "?";
}

bool weather_requires_headlights(AtmosphericWeather weather) {
    return weather == AtmosphericWeather::Storm ||
           weather == AtmosphericWeather::Thunderstorm ||
           weather == AtmosphericWeather::Blizzard;
}

float automatic_headlight_level(float sun_elevation,
                                AtmosphericWeather weather) {
    const float darkness = glm::clamp(
        0.5f - sun_elevation * 1.5f, 0.0f, 1.0f);
    return weather_requires_headlights(weather) ? 1.0f : darkness;
}

const char* weather_name(Weather w) {
    switch (w) {
        case Weather::Dry: return "dry";
        case Weather::Damp: return "damp";
        case Weather::Wet: return "wet";
    }
    return "?";
}

const char* daylight_name(Daylight d) {
    switch (d) {
        case Daylight::Night: return "night";
        case Daylight::Dawn: return "dawn";
        case Daylight::Day: return "day";
        case Daylight::Dusk: return "dusk";
    }
    return "?";
}

VehicleTuning conditioned_tuning(const VehicleTuning& base,
                                 const Conditions& c) {
    VehicleTuning out = base;
    HazardExposure hazards;
    hazards.snow_ice = c.snow_cover;
    hazards.flood = c.flood;
    hazards.hail = c.hail;
    hazards.heatwave = c.heatwave;
    const HazardVehicleAdjustments effects =
        hazard_vehicle_adjustments(hazards);

    // Weather reaches the car through the TYRE and nowhere else. This function
    // used to scale engine and brake force by c.grip as a stand-in, back when
    // the vehicle step had a single engine_force; doing that now would apply
    // the weather twice — once to the tyre that is actually sliding, and again
    // to an engine that does not know what it is driving on. So it sets
    // grip_scale, which vehicle.cpp multiplies into GroundHit::grip to size the
    // friction circle, and it leaves engine and brake torque alone.
    //
    // CAUTION, and the comment that used to sit here got this backwards.
    // TerrainCollider::set_wetness() is a SECOND route to the same friction
    // circle, for how wet the SURFACE is. Nothing calls it today. If a caller
    // ever drives it from these same Conditions, the session weather lands on
    // the tyres twice and the car gets mysteriously undrivable in light rain.
    // Pick one owner for weather-into-grip before wiring the other up.
    //
    // Standing water still drags on the wheels where the tyres DO bite, and
    // that is a separate effect from losing grip, so it stays.
    out.grip_scale = base.grip_scale * c.grip;
    out.lateral_grip_scale =
        base.lateral_grip_scale * effects.lateral_grip_multiplier;
    out.rolling_resistance = base.rolling_resistance *
        effects.rolling_resistance_multiplier * (1.0f + 0.20f * c.wetness);
    out.drag = base.drag * effects.drag_multiplier;
    out.engine_peak_torque =
        base.engine_peak_torque * effects.engine_torque_multiplier;
    return out;
}

}  // namespace apricot
