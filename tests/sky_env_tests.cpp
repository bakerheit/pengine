// The lighting environment.
//
// The load-bearing claim under test is the one that is easiest to lose and
// hardest to notice: WEATHER IS AN EXACT NO-OP AT ZERO. Not "looks the same",
// not "within an epsilon" — every field bit-for-bit identical. The moment that
// stops being true, the clear-day look drifts a little every time somebody
// tunes a storm, and there is no frame anyone can point at where it broke.
//
// Everything else here pins the sun arc, because the sky pass and the lit
// shaders read the SAME env, and the day they disagree about where the sun is
// the world looks wrong in a way nobody can name.

#include <cmath>
#include <cstdio>

#include "gfx/sky_env.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool exactly_equal(const SkyEnv& a, const SkyEnv& b) {
    return a.time_of_day == b.time_of_day && a.sun_dir == b.sun_dir &&
           a.moon_dir == b.moon_dir && a.light_dir == b.light_dir &&
           a.light_color == b.light_color && a.ambient == b.ambient &&
           a.specular_strength == b.specular_strength &&
           a.snow_cover == b.snow_cover && a.sky_top == b.sky_top &&
           a.sky_bottom == b.sky_bottom && a.sun_color == b.sun_color &&
           a.cloud_color == b.cloud_color &&
           a.star_intensity == b.star_intensity &&
           a.cloud_cover == b.cloud_cover && a.fog_color == b.fog_color &&
           a.fog_start == b.fog_start && a.fog_end == b.fog_end &&
           a.fog_density == b.fog_density;
}

void the_sun_rides_a_real_arc() {
    // 0.25 sunrise, 0.5 noon, 0.75 sunset, 0.0 midnight.
    REQUIRE_NEAR(static_cast<double>(compute_sky_env(0.25f).sun_dir.y), 0.0, 1e-6);
    REQUIRE(compute_sky_env(0.5f).sun_dir.y > 0.9f);
    REQUIRE_NEAR(static_cast<double>(compute_sky_env(0.75f).sun_dir.y), 0.0, 1e-6);
    REQUIRE(compute_sky_env(0.0f).sun_dir.y < -0.9f);

    for (int i = 0; i < 64; ++i) {
        const float t = static_cast<float>(i) / 64.0f;
        const SkyEnv e = compute_sky_env(t);

        REQUIRE_MSG(std::fabs(glm::length(e.sun_dir) - 1.0f) < 1e-5f,
                    "sun_dir must be unit length", "arc");
        REQUIRE_MSG(std::fabs(glm::length(e.light_dir) - 1.0f) < 1e-5f,
                    "light_dir must be unit length", "arc");
        REQUIRE_MSG(e.moon_dir == -e.sun_dir, "the moon is opposite the sun",
                    "arc");

        // Whichever body is up is the one that lights the world, so the light
        // never comes from under the ground. A negative light_dir.y lights
        // every surface from below and the whole scene reads as a horror film.
        REQUIRE_MSG(e.light_dir.y >= -1e-6f,
                    "light must never come from below the horizon", "arc");
    }
    apricot_test::pass("sun arc, unit directions, light never from below");
}

void stars_come_out_at_night_and_not_before() {
    REQUIRE(compute_sky_env(0.5f).star_intensity == 0.0f);   // noon
    REQUIRE(compute_sky_env(0.0f).star_intensity > 0.99f);   // midnight

    // Monotone across the evening: it may plateau, it must never go back up.
    float previous = compute_sky_env(0.5f).star_intensity;
    for (int i = 0; i <= 40; ++i) {
        const float t = 0.5f + 0.5f * static_cast<float>(i) / 40.0f;
        const float now = compute_sky_env(t).star_intensity;
        REQUIRE_MSG(now >= previous - 1e-6f,
                    "star intensity must not fall as night comes on", "dusk");
        previous = now;
    }
    apricot_test::pass("stars fade in through dusk, never back out");
}

void the_time_of_day_wraps_rather_than_clamping() {
    // A sim clock only ever counts up, so the env has to take 3.28 and mean
    // 0.28. Clamping instead would freeze the sky at midnight on day one.
    //
    // NOT bit-exact, and it cannot be: 3.28f - 3.0f loses low bits that 0.28f
    // still has, so the wrapped time lands a few ulps away. That is inherent to
    // subtracting a large float from a nearby one, not a defect to chase. The
    // no-op claim that IS bit-exact is the weather one below, where nothing is
    // subtracted at all.
    const SkyEnv base = compute_sky_env(0.28f);
    const SkyEnv next_day = compute_sky_env(3.28f);
    const SkyEnv previous_day = compute_sky_env(-0.72f);

    for (const SkyEnv& other : {next_day, previous_day}) {
        REQUIRE_NEAR(static_cast<double>(other.time_of_day),
                     static_cast<double>(base.time_of_day), 1e-6);
        REQUIRE_NEAR(static_cast<double>(glm::length(other.sun_dir - base.sun_dir)),
                     0.0, 1e-5);
        REQUIRE_NEAR(
            static_cast<double>(glm::length(other.light_color - base.light_color)),
            0.0, 1e-5);
        REQUIRE_NEAR(static_cast<double>(other.star_intensity),
                     static_cast<double>(base.star_intensity), 1e-5);
        // The one thing that must be exact whichever day it is: the sky is
        // never clamped to an endpoint. A wrap that clamped would pin
        // time_of_day at 1.0 or 0.0 and the sun would stop moving.
        REQUIRE(other.time_of_day >= 0.0f && other.time_of_day < 1.0f);
    }
    apricot_test::pass("time of day wraps across day boundaries");
}

void the_same_time_gives_the_same_environment() {
    for (int i = 0; i < 32; ++i) {
        const float t = static_cast<float>(i) * 0.031f;
        REQUIRE_MSG(exactly_equal(compute_sky_env(t), compute_sky_env(t)),
                    "compute_sky_env must be pure", "determinism");
    }
    apricot_test::pass("compute_sky_env is pure");
}

void zero_weather_changes_absolutely_nothing() {
    // THE test. Bit-for-bit, at every hour, including the fog fields.
    for (int i = 0; i < 48; ++i) {
        const float t = static_cast<float>(i) / 48.0f;
        const SkyEnv baseline = compute_sky_env(t);

        SkyEnv layered = baseline;
        apply_weather(layered, WeatherParams{});
        REQUIRE_MSG(exactly_equal(baseline, layered),
                    "apply_weather at zero must not change one bit", "all-zero");

        // Negative inputs are clamped to zero, so they must be no-ops too — a
        // slider that momentarily reads -0.0001 must not nudge the look.
        WeatherParams negative;
        negative.rain = -0.5f;
        negative.snow = -0.5f;
        negative.snow_cover = -0.5f;
        negative.overcast = -1.0f;
        negative.fog = -2.0f;
        SkyEnv from_negative = baseline;
        apply_weather(from_negative, negative);
        REQUIRE_MSG(exactly_equal(baseline, from_negative),
                    "negative weather must clamp to a no-op", "negative");

        // And the two-argument convenience must agree with doing it by hand.
        REQUIRE_MSG(exactly_equal(baseline, compute_sky_env(t, WeatherParams{})),
                    "compute_sky_env(t, {}) must equal compute_sky_env(t)",
                    "convenience");
    }
    apricot_test::pass("zero weather is an EXACT no-op at every hour");
}

float luminance(const glm::vec3& color) {
    return glm::dot(color, glm::vec3{0.2126f, 0.7152f, 0.0722f});
}

float chroma(const glm::vec3& color) {
    return std::max(color.r, std::max(color.g, color.b)) -
           std::min(color.r, std::min(color.g, color.b));
}

void a_blizzard_is_dark_and_grey_instead_of_bright_blue() {
    const SkyEnv clear = compute_sky_env(0.5f);
    WeatherParams weather;
    weather.snow = 1.0f;
    weather.snow_cover = 1.0f;
    weather.overcast = 1.0f;
    weather.fog = 1.0f;
    const SkyEnv blizzard = compute_sky_env(0.5f, weather);

    REQUIRE(luminance(blizzard.light_color + blizzard.ambient) <
            luminance(clear.light_color + clear.ambient) * 0.40f);
    REQUIRE(luminance(blizzard.sky_top + blizzard.sky_bottom) <
            luminance(clear.sky_top + clear.sky_bottom) * 0.35f);
    REQUIRE(chroma(blizzard.light_color) < chroma(clear.light_color) * 0.25f);
    REQUIRE(chroma(blizzard.sky_bottom) < chroma(clear.sky_bottom) * 0.25f);
    REQUIRE(blizzard.snow_cover == 1.0f);
    apricot_test::pass("blizzard lighting is substantially darker and greyer");
}

void snow_cover_only_collects_on_upward_surfaces() {
    REQUIRE(snow_accumulation(0.0f, 1.0f) == 0.0f);
    REQUIRE(snow_accumulation(-1.0f, 1.0f) == 0.0f);
    REQUIRE(snow_accumulation(1.0f, -1.0f) == 0.0f);
    REQUIRE(snow_accumulation(1.0f, 0.0f) == 0.0f);
    REQUIRE(snow_accumulation(1.0f, 0.40f) < 0.05f);
    REQUIRE(snow_accumulation(1.0f, 0.60f) > 0.40f);
    REQUIRE(snow_accumulation(1.0f, 0.60f) < 0.60f);
    REQUIRE(snow_accumulation(1.0f, 1.0f) == 1.0f);
    REQUIRE(snow_accumulation(2.0f, 1.0f) == 1.0f);

    WeatherParams cover_only;
    cover_only.snow_cover = 0.65f;
    const SkyEnv clear = compute_sky_env(0.5f);
    const SkyEnv covered = compute_sky_env(0.5f, cover_only);
    REQUIRE_NEAR(static_cast<double>(covered.snow_cover), 0.65, 1e-6);
    SkyEnv without_cover = covered;
    without_cover.snow_cover = clear.snow_cover;
    REQUIRE(exactly_equal(clear, without_cover));
    apricot_test::pass("snow cover is slope-aware and independent of snowfall");
}

void fog_is_off_until_it_is_asked_for() {
    const SkyEnv clear = compute_sky_env(0.5f);
    // The shader's disable test is fog_end <= fog_start. A base env must fail
    // it, or every shader fogs by default.
    REQUIRE(!(clear.fog_end > clear.fog_start));
    REQUIRE(clear.fog_density == 0.0f);

    WeatherParams w;
    w.fog = 0.6f;
    w.fog_start_m = 100.0f;
    w.fog_end_m = 800.0f;
    const SkyEnv hazy = compute_sky_env(0.5f, w);
    REQUIRE(hazy.fog_end > hazy.fog_start);
    REQUIRE(hazy.fog_density > 0.0f);
    REQUIRE_NEAR(static_cast<double>(hazy.fog_start), 100.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(hazy.fog_end), 800.0, 1e-4);
    apricot_test::pass("fog is disabled by default and enabled only on request");
}

void distance_haze_hides_the_world_edge_and_weather_pulls_it_in() {
    const SkyEnv untouched = compute_sky_env(0.5f);

    SkyEnv clear = untouched;
    apply_distance_haze(clear, DistanceHazeParams{});
    REQUIRE_NEAR(static_cast<double>(clear.fog_start), 550.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(clear.fog_end), 1000.0, 1e-4);
    REQUIRE(clear.fog_density == 1.0f);
    REQUIRE(clear.fog_color != untouched.fog_color);

    DistanceHazeParams normal;
    normal.weather_fog = normal.normal_weather_fog;
    SkyEnv default_view = untouched;
    apply_distance_haze(default_view, normal);
    REQUIRE_NEAR(static_cast<double>(default_view.fog_start), 100.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(default_view.fog_end), 300.0, 1e-4);

    DistanceHazeParams heavy;
    heavy.weather_fog = 1.0f;
    SkyEnv foggy = compute_sky_env(0.5f);
    WeatherParams weather;
    weather.fog = 1.0f;
    apply_weather(foggy, weather);
    apply_distance_haze(foggy, heavy);
    REQUIRE_NEAR(static_cast<double>(foggy.fog_start), 95.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(foggy.fog_end), 455.0, 1e-4);
    REQUIRE(foggy.fog_start < clear.fog_start);
    REQUIRE(foggy.fog_end < clear.fog_end);
    REQUIRE(foggy.fog_density == 1.0f);

    // It is a separate layer: adding world haze must not weaken the exact
    // zero-weather guarantee above or mutate a fresh environment by surprise.
    REQUIRE(exactly_equal(untouched, compute_sky_env(0.5f)));
    apricot_test::pass(
        "distance haze fully hides the world edge and closes in with weather");
}

void weather_moves_the_look_in_the_direction_it_claims() {
    const SkyEnv clear = compute_sky_env(0.5f);

    WeatherParams storm;
    storm.overcast = 1.0f;
    storm.rain = 1.0f;
    const SkyEnv wet = compute_sky_env(0.5f, storm);

    REQUIRE_MSG(wet.cloud_cover > clear.cloud_cover, "overcast adds cloud",
                "storm");
    REQUIRE_MSG(glm::length(wet.light_color) < glm::length(clear.light_color),
                "a thick deck dims the directional light", "storm");
    REQUIRE_MSG(glm::length(wet.ambient) > glm::length(clear.ambient),
                "and lifts ambient, so the scene dims instead of going black",
                "storm");
    REQUIRE_MSG(wet.specular_strength > clear.specular_strength,
                "wet surfaces are shinier", "storm");

    // Rain alone must bring cloud with it. Rain out of a clear blue sky is the
    // single most obvious way to make weather look bolted on.
    WeatherParams rain_only;
    rain_only.rain = 1.0f;
    REQUIRE(compute_sky_env(0.5f, rain_only).cloud_cover > clear.cloud_cover);

    WeatherParams snow_only;
    snow_only.snow = 1.0f;
    const SkyEnv snowy = compute_sky_env(0.5f, snow_only);
    REQUIRE(snowy.cloud_cover > clear.cloud_cover);
    REQUIRE(snowy.light_color != clear.light_color);
    REQUIRE(snowy.fog_color != clear.fog_color);

    // Cloud must put the stars out, at night, where there were stars to put out.
    const SkyEnv night = compute_sky_env(0.0f);
    const SkyEnv night_storm = compute_sky_env(0.0f, storm);
    REQUIRE(night.star_intensity > 0.9f);
    REQUIRE(night_storm.star_intensity < night.star_intensity);
    apricot_test::pass("weather moves cloud, light, ambient, sheen and stars");
}

}  // namespace

int main() {
    the_sun_rides_a_real_arc();
    stars_come_out_at_night_and_not_before();
    the_time_of_day_wraps_rather_than_clamping();
    the_same_time_gives_the_same_environment();
    zero_weather_changes_absolutely_nothing();
    fog_is_off_until_it_is_asked_for();
    distance_haze_hides_the_world_edge_and_weather_pulls_it_in();
    weather_moves_the_look_in_the_direction_it_claims();
    a_blizzard_is_dark_and_grey_instead_of_bright_blue();
    snow_cover_only_collects_on_upward_surfaces();
    return apricot_test::done("sky_env_tests");
}
