#include "game/conditions.h"

#include <cmath>

#include "core/fixed_step.h"
#include "core/rng.h"

namespace apricot {
namespace {

// kTwoPi is core's, from core/rng.h included above. A local copy here was a
// redefinition the moment PENG-3 landed one — and the two spellings did not
// even agree past the ninth digit.

// Independent hash channels, so the front and the showers inside it are not
// the same curve at two amplitudes.
constexpr uint32_t kChannelFront = 11u;
constexpr uint32_t kChannelShower = 12u;

// Most of the time the track should be dry. The front noise is uniform in
// [0, 1); everything below this is simply "dry", and the remainder is
// stretched back out so a wet day still reaches properly wet.
constexpr float kDryBias = 0.55f;

float lattice(uint64_t seed, int32_t cell, uint32_t channel) {
    return static_cast<float>(hash_coord3(seed, cell, 0, channel) >> 40) *
           (1.0f / 16777216.0f);
}

// 1-D value noise with a smoothstep fade. Weather has no business being
// C2-continuous, so this does not need heightmap.cpp's quintic.
float weather_noise(uint64_t seed, double t, uint32_t channel) {
    const double floor_t = std::floor(t);
    const int32_t cell = static_cast<int32_t>(floor_t);
    const float u = static_cast<float>(t - floor_t);
    const float s = u * u * (3.0f - 2.0f * u);
    const float a = lattice(seed, cell, channel);
    const float b = lattice(seed, cell + 1, channel);
    return a + (b - a) * s;
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

    c.headlight_level = glm::clamp(0.5f - c.sun_elevation * 1.5f, 0.0f, 1.0f);

    // --- weather -------------------------------------------------------------
    const double beat = seconds / kSecondsPerWeatherBeat;

    // The front is the slow envelope: it is what leaves the surface wet, and it
    // keeps the ground wet after the shower inside it has passed. A true
    // exponential dry-out would need an integrator, and an integrator would
    // need state, and state is what the ghost cannot have — see the header.
    const float front = weather_noise(seed, beat * 0.25, kChannelFront);
    c.wetness = glm::clamp((front - kDryBias) / (1.0f - kDryBias), 0.0f, 1.0f);

    // Showers only exist inside a front, so it never rains on dry tarmac.
    const float shower = weather_noise(seed, beat, kChannelShower);
    c.rain = glm::clamp(c.wetness * shower * 1.4f, 0.0f, 1.0f);

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
                       kNightGripLoss * night;
    c.grip = glm::clamp(1.0f - loss, kMinGrip, 1.0f);

    return c;
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
    out.rolling_resistance = base.rolling_resistance * (1.0f + 0.35f * c.wetness);
    return out;
}

}  // namespace apricot
