#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace apricot {

// THE LIGHTING ENVIRONMENT. One struct, one producer, every lit shader.
//
// compute_sky_env(time_of_day) is the single source of truth for what the world
// is lit by. The sky pass reads it, every lit shader reads it through
// apply_lighting(), the rain reads it for fog. There is deliberately no second
// place to set a light direction, because the day the sky and the geometry
// disagree about where the sun is, the game looks wrong in a way nobody can
// point at.
//
// This header is GL-free and pure so the headless suites can exercise the real
// producer instead of a hand-built copy of it (see tests/sky_env_tests.cpp).
// Keep it that way: the moment it needs a GL type, the thing that needed it
// belongs in gfx/sky.h instead.

struct SkyEnv {
    float time_of_day = 0.5f;  // [0,1): 0 = midnight, 0.25 sunrise, 0.5 noon

    glm::vec3 sun_dir{0.0f, 1.0f, 0.0f};   // normalised, TOWARD the sun
    glm::vec3 moon_dir{0.0f, -1.0f, 0.0f};

    // What lit geometry consumes.
    glm::vec3 light_dir{0.6f, 1.0f, 0.4f}; // normalised, toward whichever body is up
    glm::vec3 light_color{1.0f, 0.95f, 0.85f};
    glm::vec3 ambient{0.18f, 0.22f, 0.28f};
    float specular_strength = 0.25f;

    // Sky appearance.
    glm::vec3 sky_top{0.30f, 0.55f, 0.90f};
    glm::vec3 sky_bottom{0.70f, 0.80f, 0.92f};
    glm::vec3 sun_color{1.0f, 0.95f, 0.80f};
    glm::vec3 cloud_color{1.0f, 1.0f, 1.0f};
    float star_intensity = 0.0f;   // 0 by day, 1 at night
    float cloud_cover = 0.35f;     // 0..1

    // Distance fog. Disabled when fog_end <= fog_start OR fog_density <= 0, and
    // "disabled" means the shader returns its input untouched — see
    // assets/shaders/lighting.glsl.
    glm::vec3 fog_color{0.62f, 0.70f, 0.80f};
    float fog_start = 0.0f;
    float fog_end = 0.0f;
    float fog_density = 0.0f;
};

// Weather layered ONTO a SkyEnv.
//
// THE RULE, and it is the whole reason weather is a separate struct rather than
// more fields on SkyEnv: every one of these must be an EXACT no-op at zero.
// Not "close enough to zero", not "visually identical" — the clear-day look has
// to be bit-for-bit what it was before weather existed, or the baseline drifts
// every time somebody tunes a storm and nobody can tell when it happened.
struct WeatherParams {
    float rain = 0.0f;        // 0..1 precipitation intensity
    float overcast = 0.0f;    // 0..1 extra cloud, dimmer sun, greyer light
    float fog = 0.0f;         // 0..1 haze density

    // Where the haze band sits when fog > 0. Ignored entirely at fog == 0.
    float fog_start_m = 120.0f;
    float fog_end_m = 900.0f;
};

namespace detail {

inline float sky_fract(float x) { return x - std::floor(x); }

inline float sky_smoothstep(float e0, float e1, float x) {
    if (e0 == e1) return x < e0 ? 0.0f : 1.0f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace detail

// Pure: a normalised time of day to a full lighting environment. No clock, no
// GL, no globals. Feeding it a value outside [0,1) wraps rather than clamping,
// so a monotonically increasing sim time can be handed straight in.
inline SkyEnv compute_sky_env(float time_of_day) {
    using detail::sky_fract;
    using detail::sky_smoothstep;

    SkyEnv e;
    e.time_of_day = sky_fract(time_of_day);

    // The sun rides an arc: on the horizon at 0.25, overhead at 0.5, back to
    // the horizon at 0.75, under the world at 0.0. The constant +Z tilt stops
    // the arc from passing exactly through the zenith, where the sun would sit
    // straight overhead at noon and every vertical surface would go flat.
    constexpr float kTwoPi = 6.28318530718f;
    const float a = (e.time_of_day - 0.25f) * kTwoPi;
    e.sun_dir = glm::normalize(glm::vec3{std::cos(a), std::sin(a), 0.35f});
    e.moon_dir = -e.sun_dir;

    // How much daylight there is, and how strongly the low-sun warm tint bites.
    const float daylight = sky_smoothstep(-0.10f, 0.18f, e.sun_dir.y);
    const float sunset = (1.0f - sky_smoothstep(0.0f, 0.30f, std::fabs(e.sun_dir.y))) *
                         sky_smoothstep(-0.22f, 0.06f, e.sun_dir.y);

    const glm::vec3 day_top{0.24f, 0.50f, 0.92f};
    const glm::vec3 day_bottom{0.72f, 0.82f, 0.92f};
    const glm::vec3 night_top{0.02f, 0.03f, 0.08f};
    const glm::vec3 night_bottom{0.04f, 0.05f, 0.11f};
    const glm::vec3 warm{0.95f, 0.45f, 0.22f};

    e.sky_top = glm::mix(night_top, day_top, daylight);
    e.sky_bottom = glm::mix(night_bottom, day_bottom, daylight);
    e.sky_bottom = glm::mix(e.sky_bottom, warm, sunset * 0.7f);

    const glm::vec3 day_light{1.00f, 0.95f, 0.85f};
    const glm::vec3 moon_light{0.28f, 0.33f, 0.48f};
    e.light_color = glm::mix(moon_light, day_light, daylight);
    e.light_color = glm::mix(e.light_color, warm, sunset * 0.5f);

    const glm::vec3 day_ambient{0.22f, 0.26f, 0.32f};
    const glm::vec3 night_ambient{0.05f, 0.06f, 0.11f};
    e.ambient = glm::mix(night_ambient, day_ambient, daylight);

    // Light comes from whichever body is above the horizon. moon_dir is the
    // negated sun, so as the sun dips the moon is already up to take over and
    // the world never goes unlit — it just goes cold.
    e.light_dir = glm::normalize(e.sun_dir.y > 0.0f ? e.sun_dir : e.moon_dir);

    e.sun_color = glm::mix(glm::vec3{1.0f, 0.96f, 0.82f}, warm, sunset);

    const glm::vec3 night_cloud{0.09f, 0.10f, 0.16f};
    const glm::vec3 day_cloud = glm::mix(glm::vec3{1.0f}, warm, sunset * 0.6f);
    e.cloud_color = glm::mix(night_cloud, day_cloud, daylight);
    e.cloud_cover = 0.35f;

    e.star_intensity = sky_smoothstep(0.10f, -0.12f, e.sun_dir.y);

    e.specular_strength = 0.25f;

    // Fog is off in the base environment. Distance haze is a weather decision,
    // not a time-of-day one.
    e.fog_color = glm::mix(e.sky_bottom, e.sky_top, 0.35f);
    e.fog_start = 0.0f;
    e.fog_end = 0.0f;
    e.fog_density = 0.0f;

    return e;
}

// Layer weather onto an environment, in place.
//
// EXACT no-op at zero, and it is written to be obviously so: each effect early-
// returns on its own zero rather than relying on mix(x, y, 0) rounding back to
// x. Pinned by tests/sky_env_tests.cpp, which compares every field bit-for-bit
// against an untouched env.
inline void apply_weather(SkyEnv& env, const WeatherParams& w) {
    const float overcast = std::clamp(w.overcast, 0.0f, 1.0f);
    const float rain = std::clamp(w.rain, 0.0f, 1.0f);
    const float fog = std::clamp(w.fog, 0.0f, 1.0f);

    // Rain implies cloud. A downpour under a clear blue sky is the single most
    // obvious way to make weather look bolted on.
    const float cloud = std::clamp(overcast + rain * 0.6f, 0.0f, 1.0f);

    if (cloud > 0.0f) {
        env.cloud_cover = glm::mix(env.cloud_cover, 1.0f, cloud);

        // A thick deck scatters the sun into a flat grey dome: the directional
        // light loses its warmth and most of its punch, and ambient picks up
        // what it lost so the scene dims rather than going black.
        const glm::vec3 grey{0.55f, 0.57f, 0.62f};
        env.light_color = glm::mix(env.light_color, env.light_color * grey, cloud);
        env.ambient = glm::mix(env.ambient, env.ambient * 1.35f + glm::vec3{0.02f},
                               cloud);
        env.sky_top = glm::mix(env.sky_top, env.sky_top * grey, cloud);
        env.sky_bottom = glm::mix(env.sky_bottom, env.sky_bottom * grey, cloud);
        env.cloud_color = glm::mix(env.cloud_color, env.cloud_color * 0.75f, cloud);

        // Wet surfaces are shinier. It is a cheap trick and it works.
        env.specular_strength =
            glm::mix(env.specular_strength, env.specular_strength + 0.25f, rain);

        // Stars go out behind cloud before they go out at dawn.
        env.star_intensity *= (1.0f - cloud);
    }

    if (fog > 0.0f) {
        env.fog_color = glm::mix(env.sky_bottom, glm::vec3{0.72f, 0.74f, 0.78f},
                                 fog * 0.5f);
        env.fog_start = w.fog_start_m;
        env.fog_end = w.fog_end_m;
        env.fog_density = fog;
    }
}

// Convenience: the whole environment for a moment, weather included. This is
// what the app calls; nothing else should be recomputing a light direction.
inline SkyEnv compute_sky_env(float time_of_day, const WeatherParams& w) {
    SkyEnv e = compute_sky_env(time_of_day);
    apply_weather(e, w);
    return e;
}

}  // namespace apricot
