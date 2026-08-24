#include "city/weather.h"

#include <algorithm>
#include <cmath>



namespace apricot {

const char* weather_kind_name(WeatherKind k) {
    switch (k) {
        case WeatherKind::Clear:    return "clear";
        case WeatherKind::Overcast: return "overcast";
        case WeatherKind::Rain:     return "rain";
        case WeatherKind::Storm:    return "storm";
    }
    return "clear";
}

WeatherState weather_look_for(const WeatherTuning& t, WeatherKind kind,
                              float intensity) {
    const float i = std::clamp(intensity, 0.f, 1.f);
    WeatherState base;
    switch (kind) {
        case WeatherKind::Clear:    return WeatherState{}; // identity, always
        case WeatherKind::Overcast: base = t.overcast; break;
        case WeatherKind::Rain:     base = t.rain;     break;
        case WeatherKind::Storm:    base = t.storm;    break;
    }
    return WeatherState{base.cloud_add * i, base.darken * i,
                        base.desaturate * i, base.fog * i};
}

// ---- PCG-041 part A: intra-state intensity drift ---------------------------

void weather_drift_step(WeatherDrift& d, float dt, float speed, Rng& rng) {
    if (dt < 0.f) dt = 0.f;

    // Ease `value` toward `target` with an exp time-constant derived from speed
    // (faster speed => shorter tau => quicker ebb/surge). `value` only ever moves
    // by (target-value)*a per step, so it is always smooth — never a jump, even
    // on the frame a brand-new target is drawn.
    const float tau = 1.0f / std::max(speed, 1e-3f);
    const float a   = 1.f - std::exp(-dt / tau);
    d.value += (d.target - d.value) * a;
    d.value  = std::clamp(d.value, -1.f, 1.f);

    // Re-pick the walk target on a randomized cadence (a few tau) so the wander
    // reads as an organic ebb/surge rather than a metronome. Only this branch
    // consumes the rng, keeping the stream tight + deterministic.
    d.timer -= dt;
    if (d.timer <= 0.f) {
        d.target = rng.range(-1.f, 1.f);
        d.timer  = rng.range(1.5f, 3.5f) * tau;
    }
}

float weather_drifted_intensity(WeatherKind kind, float center,
                                const WeatherDrift& d, float band) {
    // No drift at Clear (or a zero center): return the center EXACTLY, so a Clear
    // state stays byte-exact identity — the PCG-035 no-drift guarantee.
    if (kind == WeatherKind::Clear || center <= 0.f) return center;

    // Wander within +/- band of the center, clamped into [0,1]. d.value is bounded
    // to [-1,1], so the result stays inside the state's band and never reaches a
    // worse state's look (crossing into storm territory is escalation, not drift).
    const float lo = std::clamp(center - band, 0.f, 1.f);
    const float hi = std::clamp(center + band, 0.f, 1.f);
    return std::clamp(center + d.value * band, lo, hi);
}

// ---- PCG-041 part B: directed scheduler progression ------------------------

namespace {
// Calm-biased weighted pick over {Clear, Overcast, Rain} (Storm is never picked
// directly — storms build from rain). Weights are out of their sum.
WeatherKind weighted_calm(Rng& rng, int w_clear, int w_over, int w_rain) {
    const int total = w_clear + w_over + w_rain;
    const int r = rng.next_int(0, total - 1);
    if (r < w_clear)          return WeatherKind::Clear;
    if (r < w_clear + w_over) return WeatherKind::Overcast;
    return WeatherKind::Rain;
}
} // namespace

WeatherKind weather_next_kind(WeatherKind current, const WeatherTuning& t,
                              Rng& rng) {
    switch (current) {
        case WeatherKind::Clear:
            // Mostly hold calm; can build toward overcast/rain. Clear dominates.
            return weighted_calm(rng, 6, 3, 1);
        case WeatherKind::Overcast:
            // Either settle back to clear or build toward rain.
            return weighted_calm(rng, 4, 3, 3);
        case WeatherKind::Rain: {
            // A front may build up: escalate to Storm at the tuned probability.
            // (Roll first + exclusively, so P(Storm | Rain) == rain_to_storm.)
            if (rng.next_float() < std::clamp(t.rain_to_storm, 0.f, 1.f))
                return WeatherKind::Storm;
            // Otherwise wind down toward calm or keep raining (never -> Storm).
            return weighted_calm(rng, 3, 4, 3);
        }
        case WeatherKind::Storm:
            // Storms don't teleport to clear — they wind down to rain first,
            // else linger another dwell.
            if (rng.next_float() < std::clamp(t.storm_to_rain, 0.f, 1.f))
                return WeatherKind::Rain;
            return WeatherKind::Storm;
    }
    return WeatherKind::Clear;
}

// ---- WeatherSystem ---------------------------------------------------------

WeatherSystem::WeatherSystem(uint64_t s)
    : rng_(city_system_rng(s, kChannelWeather)) {
    // Start Clear (exact identity) and hold for an initial dwell, so the game
    // opens on today's look with no weather drift before the first auto change.
    dwell_left_ = frand(tuning_.dwell_min_secs, tuning_.dwell_max_secs);
}

float WeatherSystem::frand(float lo, float hi) { return rng_.range(lo, hi); }

void WeatherSystem::schedule_next() {
    // PCG-041 part B: directed progression off the CURRENT kind (rain can build
    // into a storm; storms wind down) instead of an independent calm-biased roll.
    const WeatherKind k = weather_next_kind(target_kind_, tuning_, rng_);

    target_kind_         = k;
    target_intensity_    = (k == WeatherKind::Clear) ? 0.f : frand(0.55f, 1.0f);
    effective_intensity_ = target_intensity_; // start AT center; drift eases out
    drift_               = WeatherDrift{};     // fresh wander for the new state
    dwell_left_          = frand(tuning_.dwell_min_secs, tuning_.dwell_max_secs);
}

void WeatherSystem::update(float dt) {
    if (dt < 0.f) dt = 0.f;

    if (auto_) {
        dwell_left_ -= dt;
        if (dwell_left_ <= 0.f) schedule_next();
    }

    // PCG-041 part A: drift the EFFECTIVE intensity within the state's band so the
    // look (and every consumer of intensity() — precip density, lightning rate,
    // audio bed) ebbs and surges over time. Clear never drifts: the drift is
    // frozen and the effective intensity is pinned at 0 => exact identity.
    if (target_kind_ != WeatherKind::Clear && target_intensity_ > 0.f) {
        weather_drift_step(drift_, dt, tuning_.drift_speed, rng_);
        effective_intensity_ = weather_drifted_intensity(
            target_kind_, target_intensity_, drift_, tuning_.drift_amount);
    } else {
        effective_intensity_ = target_intensity_; // Clear / zero center: no drift
    }

    const WeatherState tgt =
        weather_look_for(tuning_, target_kind_, effective_intensity_);

    // Exponential smoothing toward the target look — smooth, never overshoots,
    // frame-rate independent. Snap to the target once within epsilon so a settled
    // state is exact (and a settled Clear is exact identity: no float drift).
    const float tau = std::max(tuning_.transition_secs, 1e-3f);
    const float a   = 1.f - std::exp(-dt / tau);
    auto approach   = [&](float& c, float t) {
        c += (t - c) * a;
        // Snap to the exact target once visually settled (< 0.1% of the [0,1]
        // range). This makes a settled state exact — and a settled Clear an exact
        // identity (zero), so there is no float drift in the day/night look.
        if (std::fabs(t - c) < 1e-3f) c = t;
    };
    approach(current_.cloud_add,  tgt.cloud_add);
    approach(current_.darken,     tgt.darken);
    approach(current_.desaturate, tgt.desaturate);
    approach(current_.fog,        tgt.fog);
}

void WeatherSystem::force(WeatherKind kind, float intensity) {
    auto_             = false; // pin until set_auto()
    target_kind_      = kind;
    target_intensity_ = (kind == WeatherKind::Clear)
                            ? 0.f
                            : std::clamp(intensity, 0.f, 1.f);
    // Start the live value AT the pinned center (so intensity() reads it back
    // exactly right after a force), then let update() drift it. A forced storm
    // still breathes; a forced Clear stays flat (drift is gated off above).
    effective_intensity_ = target_intensity_;
    drift_               = WeatherDrift{};
}

void WeatherSystem::set_auto() {
    auto_       = true;
    // Hold the current target for a fresh dwell, then let the scheduler take over.
    dwell_left_ = frand(tuning_.dwell_min_secs, tuning_.dwell_max_secs);
}

} // namespace apricot
