// The weather state machine, headless.
//
// Pins the identity guarantee, the smooth transition, the seeded scheduler, the
// intra-state intensity drift and the directed Rain -> Storm progression.
//
// WHAT MOVED. probablecause's version of this suite spent half its length
// asserting that apply_weather(SkyEnv&, WeatherState) was an exact no-op at
// zero. That function did not come across — gfx/sky_env.h already owns lighting
// modulation and already pins the same property in tests/sky_env_tests.cpp. So
// the identity guarantee is asserted HERE at its source, on the WeatherState
// itself: is_identity() must be exactly true for Clear and for zero intensity,
// and a settled Clear must return to it bit-exactly rather than approximately.
// If the state is exactly zero, whatever applies it cannot perturb anything.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "test_assert.h"

#include "city/city_rng.h"
#include "city/weather.h"

using namespace apricot;

namespace {

// A drift stream keyed the way the system keys its own.
Rng drift_rng(uint64_t seed) { return city_system_rng(seed, kChannelWeather); }

// --- The identity guarantee, at its source ----------------------------------

void test_clear_and_zero_intensity_are_identity() {
    WeatherTuning t;
    // Clear is identity at ANY intensity — the look is chosen by kind first.
    REQUIRE(weather_look_for(t, WeatherKind::Clear, 0.0f).is_identity());
    REQUIRE(weather_look_for(t, WeatherKind::Clear, 1.0f).is_identity());
    // And every kind is identity at zero intensity.
    REQUIRE(weather_look_for(t, WeatherKind::Overcast, 0.0f).is_identity());
    REQUIRE(weather_look_for(t, WeatherKind::Rain, 0.0f).is_identity());
    REQUIRE(weather_look_for(t, WeatherKind::Storm, 0.0f).is_identity());

    // is_identity() is EXACT equality against zero, not a tolerance. A default
    // WeatherState is identity; nudging any one field off zero by the smallest
    // representable amount is not.
    REQUIRE(WeatherState{}.is_identity());
    WeatherState nearly{};
    nearly.fog = 1e-30f;
    REQUIRE(!nearly.is_identity());
    apricot_test::pass("Clear and zero intensity are exactly the identity");
}

void test_intensity_clamps_0_1() {
    WeatherSystem w;
    w.force(WeatherKind::Storm, 5.0f);
    REQUIRE(w.intensity() == 1.0f);
    w.force(WeatherKind::Rain, -3.0f);
    REQUIRE(w.intensity() == 0.0f);

    // weather_look_for clamps too: > 1 never exceeds the base look.
    WeatherTuning t;
    WeatherState over = weather_look_for(t, WeatherKind::Storm, 4.0f);
    REQUIRE(over.cloud_add == t.storm.cloud_add);
    REQUIRE(over.darken == t.storm.darken);
    apricot_test::pass("intensity clamps at both ends");
}

// --- Smooth transition ------------------------------------------------------

// Forcing Storm ramps the blended look smoothly toward the target: monotonically
// increasing, never overshooting, eventually arriving, and clamped to the target.
void test_transition_smooth_and_monotonic() {
    WeatherSystem w;
    WeatherTuning t = w.tuning();
    // Pin the transition in isolation: disable the intra-state drift so the
    // target look is a flat Storm@1.0 and the ramp is purely the exponential
    // smoothing under test. The wander has its own tests below.
    w.tuning().drift_amount = 0.f;
    w.force(WeatherKind::Storm, 1.0f);

    float prev = w.state().darken;
    REQUIRE(prev == 0.0f); // starts from clear
    const float target = t.storm.darken;

    for (int i = 0; i < 60 * 60; ++i) { // 60s at 60 Hz
        w.update(1.0f / 60.0f);
        const float cur = w.state().darken;
        REQUIRE(cur >= prev - 1e-6f);   // monotonic up
        REQUIRE(cur <= target + 1e-6f); // never overshoots
        prev = cur;
    }
    REQUIRE(std::fabs(w.state().darken - target) < 1e-3f); // arrived
    REQUIRE(w.state().cloud_add <= t.storm.cloud_add + 1e-6f);
    apricot_test::pass("the look ramps monotonically and never overshoots");
}

// Storm -> Clear settles back to an EXACT identity. Not "close to zero": the
// snap-to-target in update() exists precisely so a settled clear day is the
// same clear day it was before it ever rained.
void test_settled_clear_returns_to_identity() {
    WeatherSystem w;
    w.force(WeatherKind::Storm, 1.0f);
    for (int i = 0; i < 120 * 60; ++i) w.update(1.0f / 60.0f);
    REQUIRE(!w.state().is_identity()); // storm is active

    w.force(WeatherKind::Clear, 0.0f);
    for (int i = 0; i < 120 * 60; ++i) w.update(1.0f / 60.0f);
    REQUIRE(w.state().is_identity()); // snapped exactly back to zero
    apricot_test::pass("a settled Clear is bit-exactly the identity again");
}

// --- Scheduler determinism --------------------------------------------------

void test_scheduler_is_deterministic() {
    WeatherSystem a(12345u);
    WeatherSystem b(12345u);
    std::vector<int> seq_a, seq_b;
    for (int i = 0; i < 4000; ++i) { // ~4000s, many scheduled changes
        a.update(1.0f);
        b.update(1.0f);
        seq_a.push_back(static_cast<int>(a.kind()));
        seq_b.push_back(static_cast<int>(b.kind()));
    }
    REQUIRE(seq_a == seq_b);

    // A different seed must diverge — guards against a constant scheduler that
    // ignores its seed entirely and would pass every other test here.
    WeatherSystem c(99u);
    std::vector<int> seq_c;
    for (int i = 0; i < 4000; ++i) {
        c.update(1.0f);
        seq_c.push_back(static_cast<int>(c.kind()));
    }
    REQUIRE(seq_c != seq_a);

    // Seed 0 is a city like any other. probablecause remapped it to 1 because
    // its xorshift32 collapses at zero; the seed now goes through hash_coord,
    // whose gamma offset keeps it off splitmix64's fixed point, so this is a
    // real sequence and not a degenerate one.
    WeatherSystem z(0u);
    std::vector<int> seq_z;
    for (int i = 0; i < 4000; ++i) {
        z.update(1.0f);
        seq_z.push_back(static_cast<int>(z.kind()));
    }
    bool z_varies = false;
    for (int k : seq_z) if (k != seq_z.front()) z_varies = true;
    REQUIRE(z_varies);
    apricot_test::pass("the scheduler is seeded, deterministic, and fine at 0");
}

// The scheduler actually advances weather over time (more than one kind seen).
void test_scheduler_advances() {
    WeatherSystem w(2024u);
    bool seen[4] = {false, false, false, false};
    for (int i = 0; i < 20000; ++i) {
        w.update(1.0f);
        seen[static_cast<int>(w.kind())] = true;
    }
    int distinct = 0;
    for (bool s : seen) distinct += s ? 1 : 0;
    REQUIRE(distinct >= 2); // not stuck in a single state forever
    apricot_test::pass("weather actually changes over a long session");
}

// Forcing suspends the scheduler; set_auto resumes it.
void test_force_suspends_auto() {
    WeatherSystem w(7u);
    REQUIRE(w.is_auto());
    w.force(WeatherKind::Rain, 0.8f);
    REQUIRE(!w.is_auto());
    for (int i = 0; i < 5000; ++i) w.update(1.0f); // long enough to schedule
    REQUIRE(w.kind() == WeatherKind::Rain);        // not changed by scheduler
    w.set_auto();
    REQUIRE(w.is_auto());
    apricot_test::pass("force() pins the state until set_auto() releases it");
}

// --- Worsening monotonicity -------------------------------------------------

// Every weight rises as the weather worsens, Clear -> Overcast -> Rain -> Storm.
// An inverted tuning entry would still look like weather; it would just look
// like the WRONG weather, which is exactly the sort of thing nobody notices.
void test_worsening_is_monotone_in_every_weight() {
    WeatherTuning t;
    const WeatherState clear = weather_look_for(t, WeatherKind::Clear, 1.0f);
    const WeatherState over  = weather_look_for(t, WeatherKind::Overcast, 1.0f);
    const WeatherState rain  = weather_look_for(t, WeatherKind::Rain, 1.0f);
    const WeatherState storm = weather_look_for(t, WeatherKind::Storm, 1.0f);

    REQUIRE(clear.is_identity());
    REQUIRE(over.cloud_add  > clear.cloud_add);
    REQUIRE(rain.cloud_add  > over.cloud_add);
    REQUIRE(storm.cloud_add > rain.cloud_add);

    REQUIRE(over.darken  > clear.darken);
    REQUIRE(rain.darken  > over.darken);
    REQUIRE(storm.darken > rain.darken);

    REQUIRE(over.desaturate  > clear.desaturate);
    REQUIRE(rain.desaturate  > over.desaturate);
    REQUIRE(storm.desaturate > rain.desaturate);

    REQUIRE(over.fog  > clear.fog);
    REQUIRE(rain.fog  > over.fog);
    REQUIRE(storm.fog > rain.fog);

    // Weights stay inside the [0,1] range their consumers assume.
    REQUIRE(storm.cloud_add <= 1.0f && storm.darken <= 1.0f);
    REQUIRE(storm.desaturate <= 1.0f && storm.fog <= 1.0f);
    apricot_test::pass("every weight rises as the weather worsens");
}

// Intensity scales the effect: half intensity is a milder look than full.
void test_intensity_scales_effect() {
    WeatherTuning t;
    WeatherState half = weather_look_for(t, WeatherKind::Storm, 0.5f);
    WeatherState full = weather_look_for(t, WeatherKind::Storm, 1.0f);
    REQUIRE(half.cloud_add < full.cloud_add);
    REQUIRE(half.darken < full.darken);
    REQUIRE(half.fog < full.fog);
    REQUIRE(half.cloud_add > 0.0f);
    apricot_test::pass("intensity scales the look");
}

// --- Intra-state intensity drift --------------------------------------------

// The drift stays bounded to [-1,1] and moves by only a small per-step delta
// (smooth, no jumps) — even on frames where a brand-new walk target is drawn.
void test_drift_bounded_and_smooth() {
    WeatherDrift d;
    Rng rng = drift_rng(0x1234u);
    const float dt = 1.0f / 60.0f;
    float prev = d.value;
    for (int i = 0; i < 600 * 60; ++i) { // 10 min at 60 Hz
        weather_drift_step(d, dt, 0.12f, rng);
        REQUIRE(d.value >= -1.0f - 1e-6f);
        REQUIRE(d.value <=  1.0f + 1e-6f);
        REQUIRE(std::fabs(d.value - prev) <= 0.05f); // smooth: no pop
        prev = d.value;
    }
    apricot_test::pass("the drift is bounded and never pops");
}

// Same seed => identical drift sequence; a different seed diverges (so the walk
// genuinely reads the seed and is not a constant).
void test_drift_deterministic() {
    WeatherDrift a, b, c;
    Rng ra = drift_rng(777u), rb = drift_rng(777u), rc = drift_rng(9u);
    const float dt = 1.0f / 60.0f;
    bool diverged = false;
    for (int i = 0; i < 5000; ++i) {
        weather_drift_step(a, dt, 0.2f, ra);
        weather_drift_step(b, dt, 0.2f, rb);
        weather_drift_step(c, dt, 0.2f, rc);
        REQUIRE(a.value == b.value); // bit-identical for the same seed
        if (std::fabs(a.value - c.value) > 1e-4f) diverged = true;
    }
    REQUIRE(diverged);
    apricot_test::pass("the drift walk reads its seed");
}

// Drifted intensity stays inside the state's band [center-band, center+band]
// (clamped to [0,1]) — never reaching a worse state's look.
void test_drifted_intensity_stays_in_band() {
    WeatherDrift d;
    Rng rng = drift_rng(42u);
    const float dt = 1.0f / 60.0f;
    const float center = 0.7f, band = 0.18f;
    bool moved_up = false, moved_down = false;
    for (int i = 0; i < 4000; ++i) {
        weather_drift_step(d, dt, 0.3f, rng);
        const float eff =
            weather_drifted_intensity(WeatherKind::Rain, center, d, band);
        REQUIRE(eff >= center - band - 1e-6f);
        REQUIRE(eff <= center + band + 1e-6f);
        if (eff > center + 1e-3f) moved_up = true;
        if (eff < center - 1e-3f) moved_down = true;
    }
    REQUIRE(moved_up);   // it actually surges above center ...
    REQUIRE(moved_down); // ... and ebbs below it (the look visibly varies)
    apricot_test::pass("drift ebbs and surges without leaving its band");
}

// A rain band at full centre cannot drift into the storm's darker look: even the
// brightest-to-darkest drift of Rain stays below Storm@1.0. Drift is not
// escalation; escalation is the scheduler's job and has its own probability.
void test_drift_does_not_cross_into_storm() {
    WeatherTuning t;
    const float rain_max_darken = weather_look_for(t, WeatherKind::Rain, 1.0f).darken;
    const float storm_darken    = weather_look_for(t, WeatherKind::Storm, 1.0f).darken;
    REQUIRE(rain_max_darken < storm_darken);
    apricot_test::pass("drift cannot reach the next state's look");
}

// Clear NEVER drifts: the effective intensity is exactly the (zero) centre for
// any drift state, so a clear day stays a clear day.
void test_drift_is_zero_when_clear() {
    WeatherDrift d;
    d.value = 0.9f; // even a fully-deflected walk must be ignored at Clear
    REQUIRE(weather_drifted_intensity(WeatherKind::Clear, 0.0f, d, 0.18f) == 0.0f);

    // And through the system: a forced Clear, run long, stays exact identity.
    WeatherSystem w;
    w.force(WeatherKind::Clear, 0.0f);
    for (int i = 0; i < 120 * 60; ++i) w.update(1.0f / 60.0f);
    REQUIRE(w.intensity() == 0.0f);
    REQUIRE(w.state().is_identity());
    apricot_test::pass("Clear never drifts, through the system as well");
}

// A forced Rain breathes: intensity() varies over time (not a flat constant) yet
// stays inside the state band — and rides along into state() (the look).
void test_forced_rain_breathes_within_band() {
    WeatherSystem w(55u);
    w.force(WeatherKind::Rain, 0.7f);
    const float band = w.tuning().drift_amount;
    float lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < 120 * 60; ++i) {
        w.update(1.0f / 60.0f);
        const float e = w.intensity();
        REQUIRE(e >= 0.7f - band - 1e-6f);
        REQUIRE(e <= 0.7f + band + 1e-6f);
        lo = std::min(lo, e);
        hi = std::max(hi, e);
    }
    REQUIRE(hi - lo > 0.02f); // it genuinely ebbs and surges, not flat
    REQUIRE(w.target_intensity() == 0.7f); // the pinned centre is unmoved
    apricot_test::pass("a forced storm still breathes");
}

// --- Rain -> Storm escalation and wind-down ---------------------------------

// From Rain, P(next == Storm) tracks rain_to_storm within a statistical bound,
// and is exactly 0 when the chance is 0.
void test_escalation_probability() {
    WeatherTuning t;
    t.rain_to_storm = 0.25f;
    Rng rng = drift_rng(0xABCDEFu);
    const int N = 200000;
    int storms = 0;
    for (int i = 0; i < N; ++i)
        if (weather_next_kind(WeatherKind::Rain, t, rng) == WeatherKind::Storm)
            ++storms;
    const double frac = static_cast<double>(storms) / N;
    REQUIRE(std::fabs(frac - 0.25) < 0.01); // ~configured probability

    // Zero chance => never escalates from Rain.
    t.rain_to_storm = 0.0f;
    int storms0 = 0;
    for (int i = 0; i < N; ++i)
        if (weather_next_kind(WeatherKind::Rain, t, rng) == WeatherKind::Storm)
            ++storms0;
    REQUIRE(storms0 == 0);
    apricot_test::pass("escalation fires at its configured probability");
}

// Storm winds down (Storm -> Rain reachable) and the full Storm->Rain->Clear arc
// is reachable rather than teleporting; calm never jumps straight to Storm.
void test_winddown_and_no_direct_storm_from_calm() {
    WeatherTuning t;
    Rng rng = drift_rng(13579u);
    bool storm_to_rain = false;
    for (int i = 0; i < 5000; ++i)
        if (weather_next_kind(WeatherKind::Storm, t, rng) == WeatherKind::Rain)
            storm_to_rain = true;
    REQUIRE(storm_to_rain); // storms wind down to rain

    bool rain_to_clear_or_over = false;
    for (int i = 0; i < 5000; ++i) {
        WeatherKind k = weather_next_kind(WeatherKind::Rain, t, rng);
        if (k == WeatherKind::Clear || k == WeatherKind::Overcast)
            rain_to_clear_or_over = true;
    }
    REQUIRE(rain_to_clear_or_over); // rain winds down toward calm

    // Calm states never escalate directly to Storm (storms build from rain only).
    for (int i = 0; i < 5000; ++i) {
        REQUIRE(weather_next_kind(WeatherKind::Clear, t, rng) != WeatherKind::Storm);
        REQUIRE(weather_next_kind(WeatherKind::Overcast, t, rng) != WeatherKind::Storm);
    }
    apricot_test::pass("storms build from rain and wind down through it");
}

// Over a long random walk through the chain, calm (Clear/Overcast) dominates and
// storms stay occasional — the calm bias survives the directed progression.
void test_calm_dominates_long_horizon() {
    WeatherTuning t;
    Rng rng = drift_rng(246810u);
    int count[4] = {0, 0, 0, 0};
    WeatherKind k = WeatherKind::Clear;
    const int N = 200000;
    for (int i = 0; i < N; ++i) {
        k = weather_next_kind(k, t, rng);
        ++count[static_cast<int>(k)];
    }
    const int clear = count[static_cast<int>(WeatherKind::Clear)];
    const int over  = count[static_cast<int>(WeatherKind::Overcast)];
    const int storm = count[static_cast<int>(WeatherKind::Storm)];
    REQUIRE(clear + over > N / 2);     // calm is the majority of decisions
    REQUIRE(clear > count[static_cast<int>(WeatherKind::Rain)]); // clear plurality
    REQUIRE(static_cast<double>(storm) / N < 0.15); // storms occasional
    REQUIRE(storm > 0);                             // ... but they do happen
    apricot_test::pass("calm dominates over a long horizon");
}

// A scheduled (auto) rain can build into a storm over time — the directed
// progression actually fires through the WeatherSystem, not just the helper.
void test_auto_rain_can_escalate_to_storm() {
    WeatherSystem w(31337u);
    bool saw_rain = false, saw_storm = false;
    for (int i = 0; i < 200000; ++i) {
        w.update(1.0f);
        if (w.kind() == WeatherKind::Rain)  saw_rain = true;
        if (w.kind() == WeatherKind::Storm) saw_storm = true;
    }
    REQUIRE(saw_rain);
    REQUIRE(saw_storm); // rain built into a storm somewhere along the way
    apricot_test::pass("a storm arrives on its own, through the real system");
}

} // namespace

int main() {
    test_clear_and_zero_intensity_are_identity();
    test_intensity_clamps_0_1();
    test_transition_smooth_and_monotonic();
    test_settled_clear_returns_to_identity();
    test_scheduler_is_deterministic();
    test_scheduler_advances();
    test_force_suspends_auto();
    test_worsening_is_monotone_in_every_weight();
    test_intensity_scales_effect();
    test_drift_bounded_and_smooth();
    test_drift_deterministic();
    test_drifted_intensity_stays_in_band();
    test_drift_does_not_cross_into_storm();
    test_drift_is_zero_when_clear();
    test_forced_rain_breathes_within_band();
    test_escalation_probability();
    test_winddown_and_no_direct_storm_from_calm();
    test_calm_dominates_long_horizon();
    test_auto_rain_can_escalate_to_storm();
    return apricot_test::done("weather_tests");
}
