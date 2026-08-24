#include "city/lightning.h"

#include <algorithm>
#include <cmath>



namespace apricot {

// Interval jitter: the gap to the next strike is the mean gap (1/rate) times a
// random factor in this range, so strikes are irregular (not a metronome) while
// the AVERAGE rate still tracks lightning_rate_per_sec. Kept internal — the
// founder tunes frequency via strikes_per_sec_max, not the jitter shape.
namespace {
constexpr float kIntervalJitterLo = 0.55f;
constexpr float kIntervalJitterHi = 1.70f;
} // namespace

// ---- Pure helpers ----------------------------------------------------------

float lightning_rate_per_sec(const LightningTuning& t, WeatherKind kind,
                             float storm_intensity) {
    // Strikes only while storming — the hard "no strikes unless storming" gate.
    if (kind != WeatherKind::Storm) return 0.f;
    const float i = std::clamp(storm_intensity, 0.f, 1.f);
    const float lo = std::clamp(t.min_storm_intensity, 0.f, 0.999f);
    if (i <= lo) return 0.f;
    // Ramp 0..1 across [min_storm_intensity, 1], so frequency rises with the
    // storm. Squared so the calm end stays genuinely sparse and full storms
    // clearly more frequent (still monotonic non-decreasing in intensity).
    const float ramp = (i - lo) / (1.f - lo);
    const float rate = std::max(t.strikes_per_sec_max, 0.f) * ramp * ramp;
    return rate;
}

float lightning_delay_for(const LightningTuning& t, float s) {
    s = std::clamp(s, 0.f, 1.f);
    // Close (s -> 1) = near-immediate; distant (s -> 0) = up to max_thunder_delay.
    return std::max(t.max_thunder_delay, 0.f) * (1.f - s);
}

float lightning_volume_for(const LightningTuning& t, float s) {
    s = std::clamp(s, 0.f, 1.f);
    const float lo = std::clamp(t.thunder_vol_min, 0.f, 1.f);
    return lo + (1.f - lo) * s; // distant -> floor, close -> 1
}

float lightning_flash_envelope(const LightningTuning& t, float t_since, float s) {
    if (t_since < 0.f) return 0.f;
    s = std::clamp(s, 0.f, 1.f);
    const float tau = std::max(t.flash_decay, 1e-3f);
    // Hard cutoff: past here the exponentials are inaudibly small, and returning
    // EXACTLY 0 gives the no-op-reduction guarantee (an old strike adds nothing).
    const float dur = tau * 6.f + 0.05f;
    if (t_since >= dur) return 0.f;

    // Primary spike + a quick secondary flicker (a "double flash" reads well for
    // lightning). Both exponential; the secondary is delayed ~1.2 tau and weaker.
    const float p1  = std::exp(-t_since / tau);
    const float off = 1.2f * tau;
    const float p2  = (t_since >= off)
                          ? 0.55f * std::exp(-(t_since - off) / (tau * 0.7f))
                          : 0.f;
    float shape = p1 + p2;
    if (shape > 1.f) shape = 1.f; // keep the peak at the configured flash_peak
    return std::max(t.flash_peak, 0.f) * s * shape;
}

// NOT LIFTED (PENG-29): apply_lightning_flash(SkyEnv&, float). It added a white
// punch to the light, ambient and sky palette. gfx/sky_env.h owns that side of
// the boundary; flash() hands out the scalar and the host layer decides what a
// flash looks like.

// ---- LightningSystem -------------------------------------------------------

LightningSystem::LightningSystem(uint64_t s)
    : rng_(city_system_rng(s, kChannelLightning)) {}

float LightningSystem::frand(float lo, float hi) { return rng_.range(lo, hi); }

float LightningSystem::next_interval(float rate) {
    const float mean = (rate > 1e-6f) ? (1.f / rate) : 1e6f;
    return mean * frand(kIntervalJitterLo, kIntervalJitterHi);
}

void LightningSystem::spawn_strike(float storm_intensity) {
    const float lo = std::clamp(tuning_.intensity_min, 0.f, 1.f);
    // Base roll across the full range, then pull the mean up with storm strength
    // so heavier storms read brighter/closer on average while still varying.
    const float base = frand(lo, 1.f);
    const float bias = 0.30f * std::clamp(storm_intensity, 0.f, 1.f) * frand(0.f, 1.f);
    emit_strike(std::clamp(base + bias, lo, 1.f));
}

void LightningSystem::emit_strike(float s) {
    s = std::clamp(s, std::clamp(tuning_.intensity_min, 0.f, 1.f), 1.f);

    // Retrigger the flash to this strike (flashes are brief; a new one wins).
    flash_active_    = true;
    flash_time_      = 0.f;
    flash_intensity_ = s;
    flash_           = lightning_flash_envelope(tuning_, 0.f, s);

    // Queue the delayed thunder (distance-delayed, loudness from intensity).
    pending_.push_back(Pending{lightning_delay_for(tuning_, s),
                               lightning_volume_for(tuning_, s)});
    ++strikes_;
}

void LightningSystem::trigger_strike(float intensity) {
    // Dev / explicit path: the strike IS this intensity (no storm-biased roll),
    // so "Strike now" auditions exactly close (1.0) vs far (low) on demand.
    emit_strike(intensity);
}

void LightningSystem::update(float dt, WeatherKind kind, float storm_intensity) {
    fired_.clear();
    if (dt < 0.f) dt = 0.f;

    // Advance the flash decay. Even if the storm just ended, an in-progress flash
    // finishes naturally; once past the cutoff it snaps to an exact 0 (no-op).
    if (flash_active_) {
        flash_time_ += dt;
        flash_ = lightning_flash_envelope(tuning_, flash_time_, flash_intensity_);
        if (flash_ <= 0.f) {
            flash_        = 0.f;
            flash_active_ = false;
        }
    }

    // Advance pending thunders. These keep counting down even if the storm has
    // ended — the thunder from the last strike is already on its way (realistic),
    // so the strike's audio still arrives. Fired ones are removed.
    if (!pending_.empty()) {
        std::size_t w = 0;
        for (std::size_t r = 0; r < pending_.size(); ++r) {
            pending_[r].delay -= dt;
            if (pending_[r].delay <= 0.f) {
                fired_.push_back(pending_[r].volume);
            } else {
                pending_[w++] = pending_[r];
            }
        }
        pending_.resize(w);
    }

    // Roll NEW strikes only while the rate is positive (i.e. storming above the
    // floor). On the rising edge (rate just became positive) seed a fresh first
    // interval so the storm doesn't crack the instant it arrives.
    const float rate = lightning_rate_per_sec(tuning_, kind, storm_intensity);
    if (rate > 0.f) {
        if (!striking_) {
            striking_     = true;
            time_to_next_ = next_interval(rate);
        }
        time_to_next_ -= dt;
        // Loop in case of a large dt (a lag spike), but cap so a pathological dt
        // can't spawn an unbounded burst.
        int guard = 0;
        while (time_to_next_ <= 0.f && guard < 8) {
            spawn_strike(storm_intensity);
            time_to_next_ += next_interval(rate);
            ++guard;
        }
        if (time_to_next_ < 0.f) time_to_next_ = next_interval(rate);
    } else {
        striking_ = false; // re-seed the interval next time it storms
    }
}

} // namespace apricot
