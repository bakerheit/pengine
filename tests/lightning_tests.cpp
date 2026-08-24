// Lightning and thunder, headless.
//
// Pins what the feature actually promises:
//   * strike FREQUENCY scales with storm intensity, and is ZERO when it is not
//     storming;
//   * per-strike intensity stays in range and maps monotonically to thunder
//     delay (down) and loudness (up);
//   * a seed produces an identical strike / flash / thunder sequence;
//   * the flash decays to an EXACT zero, so a consumer's no-op branch is
//     reachable rather than aspirational.
//
// NOT HERE: the apply_lightning_flash(SkyEnv&) tests. That function stayed in
// probablecause (PENG-29) — gfx/sky_env.h owns lighting modulation. What
// survives is the guarantee that made it safe: flash() reaching exactly 0.

#include <cmath>
#include <cstdint>
#include <vector>

#include "test_assert.h"

#include "city/lightning.h"

using namespace apricot;

namespace {

// --- Rate: zero off-storm, scales with storm intensity ----------------------

void test_rate_zero_when_not_storming() {
    LightningTuning t;
    REQUIRE(lightning_rate_per_sec(t, WeatherKind::Clear,    1.0f) == 0.f);
    REQUIRE(lightning_rate_per_sec(t, WeatherKind::Overcast, 1.0f) == 0.f);
    REQUIRE(lightning_rate_per_sec(t, WeatherKind::Rain,     1.0f) == 0.f);
    // Storm below the floor: still no strikes.
    REQUIRE(lightning_rate_per_sec(t, WeatherKind::Storm, 0.0f) == 0.f);
    REQUIRE(lightning_rate_per_sec(t, WeatherKind::Storm,
                                   t.min_storm_intensity * 0.5f) == 0.f);
    apricot_test::pass("nothing strikes unless it is genuinely storming");
}

void test_rate_monotonic_in_intensity() {
    LightningTuning t;
    float prev = -1.f;
    for (int i = 0; i <= 10; ++i) {
        const float in = static_cast<float>(i) / 10.f;
        const float r  = lightning_rate_per_sec(t, WeatherKind::Storm, in);
        REQUIRE(r >= prev - 1e-7f); // non-decreasing
        prev = r;
    }
    // Strictly more frequent at full storm than at a light storm above the floor.
    REQUIRE(lightning_rate_per_sec(t, WeatherKind::Storm, 1.0f) >
            lightning_rate_per_sec(t, WeatherKind::Storm, 0.5f));
    REQUIRE(lightning_rate_per_sec(t, WeatherKind::Storm, 0.5f) > 0.f);
    apricot_test::pass("strike rate rises with the storm");
}

// --- Per-strike intensity -> delay (down) + loudness (up) -------------------

void test_delay_and_volume_monotonic() {
    LightningTuning t;
    float prev_delay = 1e9f, prev_vol = -1.f;
    for (int i = 0; i <= 10; ++i) {
        const float s = static_cast<float>(i) / 10.f;
        const float d = lightning_delay_for(t, s);
        const float v = lightning_volume_for(t, s);
        REQUIRE(d <= prev_delay + 1e-6f); // delay DECREASES with intensity
        REQUIRE(v >= prev_vol - 1e-6f);   // loudness INCREASES with intensity
        REQUIRE(v >= 0.f && v <= 1.f);
        prev_delay = d;
        prev_vol   = v;
    }
    // Close strike: near-immediate + full loudness. Distant: long delay + floor.
    REQUIRE(lightning_delay_for(t, 1.0f) < lightning_delay_for(t, 0.0f));
    REQUIRE(std::fabs(lightning_delay_for(t, 1.0f)) < 1e-6f);
    REQUIRE(std::fabs(lightning_volume_for(t, 1.0f) - 1.0f) < 1e-6f);
    REQUIRE(std::fabs(lightning_volume_for(t, 0.0f) - t.thunder_vol_min) < 1e-6f);
    apricot_test::pass("one intensity drives distance, delay and loudness");
}

// --- Flash envelope: scales with s, exact 0 past cutoff ---------------------

void test_flash_envelope() {
    LightningTuning t;
    // Peak at t=0 scales linearly with strike intensity.
    const float f_half = lightning_flash_envelope(t, 0.f, 0.5f);
    const float f_full = lightning_flash_envelope(t, 0.f, 1.0f);
    REQUIRE(f_full > f_half);
    REQUIRE(f_half > 0.f);
    REQUIRE(std::fabs(f_full - t.flash_peak) < 1e-5f); // peak == flash_peak at s=1

    // Decays to EXACTLY 0 past the cutoff. This is the guarantee the consumer's
    // no-op branch rests on, so it is exact equality and not a tolerance.
    REQUIRE(lightning_flash_envelope(t, 100.f, 1.0f) == 0.f);
    // ... and a negative time is 0 too.
    REQUIRE(lightning_flash_envelope(t, -1.f, 1.0f) == 0.f);

    // The flash fades over time (sampled well after the secondary flicker).
    const float early = lightning_flash_envelope(t, 0.0f, 1.0f);
    const float late  = lightning_flash_envelope(t, t.flash_decay * 3.f, 1.0f);
    REQUIRE(late < early);
    apricot_test::pass("the flash peaks, flickers and reaches exactly zero");
}

// --- Frequency scales with storm intensity (count over a fixed window) ------

int count_strikes(uint64_t seed, WeatherKind kind, float intensity,
                  int steps, float dt) {
    LightningSystem ls(seed);
    for (int i = 0; i < steps; ++i) ls.update(dt, kind, intensity);
    return ls.strikes();
}

void test_frequency_scales_with_intensity() {
    const int   steps = 30000; // 3000 s at dt = 0.1
    const float dt    = 0.1f;
    const int   full  = count_strikes(123u, WeatherKind::Storm, 1.0f,  steps, dt);
    const int   light = count_strikes(123u, WeatherKind::Storm, 0.45f, steps, dt);
    const int   rain  = count_strikes(123u, WeatherKind::Rain,  1.0f,  steps, dt);
    const int   clear = count_strikes(123u, WeatherKind::Clear, 1.0f,  steps, dt);

    REQUIRE(rain  == 0); // not storming -> zero
    REQUIRE(clear == 0);
    REQUIRE(light > 0);     // a light storm still strikes occasionally
    REQUIRE(full  > light); // ... but a full storm is clearly more frequent
    apricot_test::pass("a heavy storm strikes measurably more than a light one");
}

void test_no_strikes_off_storm_is_total_noop() {
    LightningSystem ls(7u);
    for (int i = 0; i < 20000; ++i) {
        ls.update(0.1f, WeatherKind::Overcast, 1.0f);
        REQUIRE(ls.thunders_this_frame().empty());
    }
    REQUIRE(ls.strikes() == 0);
    REQUIRE(ls.flash() == 0.f); // no active flash ever
    apricot_test::pass("an overcast day is a total no-op, for 2000 seconds");
}

// --- Determinism: a seed -> identical strike/flash/thunder sequence ---------

void run_trace(uint64_t seed, std::vector<float>& thunders,
               std::vector<float>& flashes, int& strikes) {
    LightningSystem ls(seed);
    for (int i = 0; i < 8000; ++i) { // 800 s at dt = 0.1, many strikes
        ls.update(0.1f, WeatherKind::Storm, 1.0f);
        for (float v : ls.thunders_this_frame()) thunders.push_back(v);
        flashes.push_back(ls.flash());
    }
    strikes = ls.strikes();
}

void test_determinism() {
    std::vector<float> th_a, th_b, fl_a, fl_b;
    int s_a = 0, s_b = 0;
    run_trace(4242u, th_a, fl_a, s_a);
    run_trace(4242u, th_b, fl_b, s_b);
    REQUIRE(s_a == s_b);
    REQUIRE(th_a == th_b); // identical thunder loudness sequence
    REQUIRE(fl_a == fl_b); // identical per-frame flash trace
    REQUIRE(!th_a.empty());

    // Every fired thunder is a valid loudness in [thunder_vol_min, 1].
    LightningTuning t;
    for (float v : th_a) {
        REQUIRE(v >= t.thunder_vol_min - 1e-6f);
        REQUIRE(v <= 1.0f + 1e-6f);
    }

    // A different seed diverges (guards against a seed-ignoring scheduler).
    std::vector<float> th_c, fl_c;
    int s_c = 0;
    run_trace(99u, th_c, fl_c, s_c);
    REQUIRE(th_c != th_a);
    apricot_test::pass("a seed replays the same storm, frame for frame");
}

// --- Strike behaviour: flash now, thunder after the distance delay ----------

void test_thunder_is_delayed_by_distance() {
    // A DISTANT strike (low intensity, above intensity_min so it isn't clamped):
    // flash immediately, thunder only later.
    LightningSystem ls(1u);
    LightningTuning t = ls.tuning();
    const float distant = 0.30f;
    ls.trigger_strike(distant);
    REQUIRE(ls.flash() > 0.f);                 // flash is instant
    REQUIRE(ls.thunders_this_frame().empty()); // ... thunder is not (yet)

    const float expect_delay = lightning_delay_for(t, distant);
    REQUIRE(expect_delay > 0.5f); // a distant strike really is delayed

    // Advance up to just before the delay: still no clap. Note the storm has
    // ENDED here — a clap already in flight still has to arrive, which is both
    // physically right and the thing a naive "clear the queue on stop" would
    // break.
    float elapsed = 0.f;
    bool  fired   = false;
    for (int i = 0; i < 1000 && !fired; ++i) {
        ls.update(0.05f, WeatherKind::Clear, 0.f);
        elapsed += 0.05f;
        if (!ls.thunders_this_frame().empty()) fired = true;
    }
    REQUIRE(fired);                          // the clap eventually arrives...
    REQUIRE(elapsed >= expect_delay - 0.1f); // ... at roughly the distance delay

    // A CLOSE strike cracks essentially immediately on the next update.
    LightningSystem near_strike(2u);
    near_strike.trigger_strike(1.0f);
    near_strike.update(0.05f, WeatherKind::Clear, 0.f);
    REQUIRE(!near_strike.thunders_this_frame().empty());
    apricot_test::pass("thunder lags the flash by the distance it came");
}

void test_flash_returns_to_exact_zero() {
    LightningSystem ls(3u);
    ls.trigger_strike(1.0f);
    ls.update(0.001f, WeatherKind::Storm, 1.0f);
    REQUIRE(ls.flash() > 0.f);
    // Advance well past the flash duration with no new strikes.
    for (int i = 0; i < 200; ++i) ls.update(0.05f, WeatherKind::Clear, 0.f);
    REQUIRE(ls.flash() == 0.f); // snapped to an exact 0, not merely a small one
    apricot_test::pass("a spent flash is exactly zero, not nearly zero");
}

} // namespace

int main() {
    test_rate_zero_when_not_storming();
    test_rate_monotonic_in_intensity();
    test_delay_and_volume_monotonic();
    test_flash_envelope();
    test_frequency_scales_with_intensity();
    test_no_strikes_off_storm_is_total_noop();
    test_determinism();
    test_thunder_is_delayed_by_distance();
    test_flash_returns_to_exact_zero();
    return apricot_test::done("lightning_tests");
}
