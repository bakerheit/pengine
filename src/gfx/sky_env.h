#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "core/rng.h"  // kTwoPi

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
    float snow_cover = 0.0f;        // 0..1 accumulated on exposed surfaces

    // Sky appearance.
    glm::vec3 sky_top{0.30f, 0.55f, 0.90f};
    glm::vec3 sky_bottom{0.70f, 0.80f, 0.92f};
    glm::vec3 sun_color{1.0f, 0.95f, 0.80f};
    glm::vec3 cloud_color{1.0f, 1.0f, 1.0f};
    float star_intensity = 0.0f;   // 0 by day, 1 at night
    float cloud_cover = 0.35f;     // 0..1
    float dusk_style = 0.0f;       // Bellwether's layered blue-hour art direction

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
    float snow = 0.0f;        // 0..1 snow/blizzard intensity
    float snow_cover = 0.0f;  // 0..1 accumulated on the surface
    // DECK OPACITY, independent of precipitation. This is the axis that says
    // how much sky is left; rain and snow below only guarantee a minimum.
    float overcast = 0.0f;    // 0..1 cloud deck thickness
    float fog = 0.0f;         // 0..1 haze density

    // Where the haze band sits when fog > 0. Ignored entirely at fog == 0.
    float fog_start_m = 120.0f;
    float fog_end_m = 900.0f;
};

// GTA-era draw-distance haze is not weather. Even a clear day owns a soft
// atmospheric wall that hides the edge of the rendered world; bad weather
// pulls that wall toward the camera and changes its colour through
// apply_weather(). Keeping this separate preserves WeatherParams' exact-no-op
// contract while giving the app an always-on world-scale tool.
struct DistanceHazeParams {
    float weather_fog = 0.0f;  // 0 = clear visibility, 1 = heavy fog
    float clear_start_m = 550.0f;
    float clear_end_m = 1000.0f;
    float normal_weather_fog = 0.55f;
    float normal_start_m = 100.0f;
    float normal_end_m = 300.0f;
    float foggy_start_m = 95.0f;
    float foggy_end_m = 455.0f;
};

// --- deck tuning ------------------------------------------------------------
//
// How much light a FULL cloud deck lets through, at a high sun and at a low
// one. Two numbers rather than one because the deck is the only thing standing
// between the sun and the ground, and the sun is not equally far away all day.
//
// The old code used a single flat multiply worth ~0.57 of the clear sky at
// every hour, which is why a noon storm read as mid-grey and a dusk storm read
// as the same mid-grey. 0.34 at a high sun is a storm you can still drive in;
// 0.20 at a low one is the dusk squall that sends you looking for the
// headlight switch.
inline constexpr float kHighSunDeckFloor = 0.34f;
inline constexpr float kLowSunDeckFloor = 0.20f;

// The deck a full downpour brings with it when nothing authored one.
//
// A FLOOR, not a sum, and that distinction is the whole point of this pass.
// Adding rain into the deck made the two inseparable: crank the rain and the
// sky closed over whether you wanted it to or not, so a bright sunshower was
// not a thing this engine could represent. A floor keeps the honest half of
// the old rule — rain out of a clear blue sky still looks bolted on — while
// leaving `overcast` free to say how thick the deck actually is.
// The value is bounded from above by the sun: 0.35 base cover plus 0.65 of the
// deck must stay under kSunSwallowedCloudCover below, so that PRECIPITATION
// ALONE CAN NEVER HIDE THE SUN — only the deck axis can. That is the invariant
// the split exists to create, and tests/sky_env_tests.cpp pins it.
inline constexpr float kPrecipCloudFloor = 0.28f;

// Where assets/shaders/sky.frag starts hiding the sun disc behind the deck
// (its `sun_vis` smoothstep runs from here to 0.85). The shader carries the
// same number; this copy exists so a headless test can assert that a preset
// meant to keep its sun actually stays on the clear side of it, which is the
// difference between a sunshower and a grey day with rain in it.
inline constexpr float kSunSwallowedCloudCover = 0.55f;

namespace detail {

inline float sky_fract(float x) { return x - std::floor(x); }

inline float sky_smoothstep(float e0, float e1, float x) {
    if (e0 == e1) return x < e0 ? 0.0f : 1.0f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Collapse a colour to its own luminance, scaled, with a faint cool cast.
//
// Deriving the target from the INPUT's luminance rather than from an absolute
// grey is what keeps a night storm darker than a noon storm. An absolute
// constant here reads as one flat tint at every hour, and the tell is a foggy
// midnight coming out brighter than the sky above it.
//
// Named for the blizzard path that needed it first; the cloud deck and the
// haze band now use the same curve, because they are the same phenomenon at
// different strengths.
inline glm::vec3 deck_grey(const glm::vec3& color, float brightness) {
    const float luminance = glm::dot(color, glm::vec3{0.2126f, 0.7152f, 0.0722f});
    return glm::vec3{luminance * brightness} *
           glm::vec3{0.94f, 0.98f, 1.05f};
}

// How much of the light reaching the top of the deck comes out of the bottom.
//
// WEIGHTED BY BOTH the deck and the sun's height, and the second term is the
// one that was missing: a low sun's light takes a longer slant path through
// the same cloud, so the identical storm has to bite harder at dusk than at
// noon. With a single flat multiplier the sky went grey at every hour and a
// dusk storm looked like a noon storm with the lights turned down.
//
// Returns exactly 1 at zero cloud, which is what keeps the no-op contract.
inline float deck_transmission(float cloud, float sun_up) {
    const float day = sky_smoothstep(-0.10f, 0.25f, sun_up);
    const float floor_at_full = glm::mix(kLowSunDeckFloor, kHighSunDeckFloor, day);
    return glm::mix(1.0f, floor_at_full, cloud);
}

}  // namespace detail

// Pure material-side coverage. Vertical and downward faces stay bare; shallow
// ledges get only a dusting; upward surfaces can reach the full condition value.
// The shader carries the same thresholds, while this copy makes the contract
// testable without a GL context.
inline float snow_accumulation(float snow_cover, float upward_normal) {
    const float cover = std::clamp(snow_cover, 0.0f, 1.0f);
    if (cover <= 0.0f) return 0.0f;
    const float upward = std::clamp(upward_normal, 0.0f, 1.0f);
    return cover * detail::sky_smoothstep(0.35f, 0.85f, upward);
}

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
    // kTwoPi comes from core. Declaring a local one here shadows it, which is
    // an error under -Wshadow — and it is the right error: three files each had
    // their own before the modules met.
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
    e.snow_cover = 0.0f;

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
    const float snow = std::clamp(w.snow, 0.0f, 1.0f);
    const float snow_cover = std::clamp(w.snow_cover, 0.0f, 1.0f);
    const float fog = std::clamp(w.fog, 0.0f, 1.0f);

    // TWO AXES, NOT ONE. `overcast` is how thick the deck is; rain and snow say
    // how much water is falling through it. They used to be summed, which meant
    // the deck was a function of the downpour and a bright sunshower could not
    // exist. Precipitation now only guarantees a MINIMUM deck.
    const float precipitation = std::max(rain, snow);
    const float precipitation_deck = precipitation * kPrecipCloudFloor;
    const float cloud = std::clamp(std::max(overcast, precipitation_deck),
                                   0.0f, 1.0f);

    if (cloud > 0.0f) {
        env.cloud_cover = glm::mix(env.cloud_cover, 1.0f, cloud);

        // A thick deck scatters the sun into a flat grey dome: the directional
        // light loses its warmth and most of its punch, and ambient picks up
        // what it lost so the scene dims rather than going black.
        //
        // WEIGHTED BY THE SUN, through deck_transmission(). Every target below
        // is derived from the field's own current value, so the same deck lands
        // differently at noon, at dusk and at midnight instead of pulling all
        // three toward one hardcoded grey.
        const float transmission =
            detail::deck_transmission(cloud, env.sun_dir.y);
        const float day = detail::sky_smoothstep(-0.10f, 0.25f, env.sun_dir.y);

        env.light_color = glm::mix(
            env.light_color, detail::deck_grey(env.light_color, transmission),
            cloud);
        env.sky_top = glm::mix(
            env.sky_top, detail::deck_grey(env.sky_top, transmission), cloud);
        env.sky_bottom = glm::mix(
            env.sky_bottom, detail::deck_grey(env.sky_bottom, transmission),
            cloud);

        // The deck reads as the UNDERSIDE of something solid, so in daylight it
        // has to come out darker than the sky showing between the gaps. It used
        // to land at 0.75 luminance over a sky greyed to 0.46, which is why
        // thickening a storm made the screen brighter instead of darker.
        //
        // The extra factor is what buys that margin: the deck's own base colour
        // is white by day where the sky is 0.81, so equal treatment would leave
        // the cloud fractionally ahead.
        //
        // By night it is deliberately allowed back over the sky — an overcast
        // midnight really does glow above a dark horizon, and clamping it there
        // reads as a hole in the sky rather than cloud.
        env.cloud_color = glm::mix(
            env.cloud_color, detail::deck_grey(env.cloud_color,
                                               transmission * 0.72f), cloud);

        // Overcast lifts the fill light, because a deck turns a hard sun into a
        // sky-wide softbox and the shadows open up. That is a DAYTIME effect:
        // scaling it by daylight stops a midnight storm from brightening the
        // ground it is supposed to be darkening.
        env.ambient = glm::mix(
            env.ambient,
            env.ambient * (1.0f + 0.35f * day) + glm::vec3{0.02f * day}, cloud);

        // Wet surfaces are shinier. It is a cheap trick and it works. Driven by
        // rain rather than by the deck, so a sunshower still glosses the roads.
        env.specular_strength =
            glm::mix(env.specular_strength, env.specular_strength + 0.25f, rain);

        // Stars go out behind cloud before they go out at dawn.
        env.star_intensity *= (1.0f - cloud);
    }

    if (fog > 0.0f) {
        // The haze is lit by the sky it hangs under, so its colour comes from
        // that sky's luminance. This used to be an absolute light grey with no
        // daylight term at all, and it showed: a foggy midnight painted a
        // brighter band around the horizon than the sky it sat against.
        env.fog_color = glm::mix(
            env.sky_bottom, detail::deck_grey(env.sky_bottom, 1.05f), fog * 0.5f);
        env.fog_start = w.fog_start_m;
        env.fog_end = w.fog_end_m;
        env.fog_density = fog;
    }

    if (snow > 0.0f) {
        // Heavy snowfall used to blend toward a bright blue-white palette,
        // undoing most of the overcast dimming. Square the intensity so ordinary
        // snow stays readable, while a forced blizzard becomes a genuinely dark,
        // low-saturation wall of weather. Deriving each target from its current
        // luminance keeps night darker than day instead of forcing one flat tint.
        const float blizzard = snow * snow;
        env.light_color = glm::mix(
            env.light_color, detail::deck_grey(env.light_color, 0.42f), blizzard);
        env.ambient = glm::mix(
            env.ambient, detail::deck_grey(env.ambient, 0.43f), blizzard);
        env.sky_top = glm::mix(
            env.sky_top, detail::deck_grey(env.sky_top, 0.42f), blizzard);
        env.sky_bottom = glm::mix(
            env.sky_bottom, detail::deck_grey(env.sky_bottom, 0.45f), blizzard);
        env.sun_color = glm::mix(
            env.sun_color, detail::deck_grey(env.sun_color, 0.30f), blizzard);
        env.cloud_color = glm::mix(
            env.cloud_color, detail::deck_grey(env.cloud_color, 0.42f), blizzard);
        env.fog_color = glm::mix(
            env.fog_color, detail::deck_grey(env.fog_color, 0.50f), blizzard);
    }

    if (snow_cover > 0.0f) env.snow_cover = snow_cover;
}

// Layer the world's visibility limit after weather. The far edge is fully
// opaque on purpose: geometry can be culled just behind it without a skyline
// pop, which is the practical trick that made older open worlds feel larger
// than their draw distance.
inline void apply_distance_haze(SkyEnv& env, const DistanceHazeParams& p) {
    const float fog = std::clamp(p.weather_fog, 0.0f, 1.0f);
    const float normal = std::clamp(p.normal_weather_fog, 0.0f, 1.0f);
    float start = p.normal_start_m;
    float end = p.normal_end_m;
    if (fog < normal && normal > 0.0f) {
        const float t = fog / normal;
        start = glm::mix(p.clear_start_m, p.normal_start_m, t);
        end = glm::mix(p.clear_end_m, p.normal_end_m, t);
    } else if (fog > normal && normal < 1.0f) {
        const float t = (fog - normal) / (1.0f - normal);
        start = glm::mix(p.normal_start_m, p.foggy_start_m, t);
        end = glm::mix(p.normal_end_m, p.foggy_end_m, t);
    }
    start = std::max(0.0f, start);
    end = std::max(start + 1.0f, end);

    // On clear days this stays close to the time-cycle horizon. Weather has
    // already pushed env.fog_color toward grey, so increasing fog naturally
    // inherits the storm palette rather than introducing a second colour model.
    env.fog_color =
        glm::mix(env.sky_bottom, env.fog_color, 0.35f + fog * 0.65f);
    env.fog_start = start;
    env.fog_end = end;
    env.fog_density = 1.0f;
}

// Convenience: the whole environment for a moment, weather included. This is
// what the app calls; nothing else should be recomputing a light direction.
inline SkyEnv compute_sky_env(float time_of_day, const WeatherParams& w) {
    SkyEnv e = compute_sky_env(time_of_day);
    apply_weather(e, w);
    return e;
}

}  // namespace apricot
