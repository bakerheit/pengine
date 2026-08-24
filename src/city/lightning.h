#pragma once

#include <cstdint>
#include <vector>

#include "city/weather.h"  // WeatherKind, and the Rng the scheduler shares

// Lightning and thunder during storms.
//
// A strike system that sits ALONGSIDE WeatherSystem: while it is storming it
// fires occasional strikes, each one a brief additive sky/scene FLASH plus a
// distance-delayed THUNDER one-shot (a loudness scalar the caller forwards to
// whatever plays it). BOTH the per-strike intensity AND the strike frequency
// scale with storm intensity — heavier storm means brighter, louder and more
// frequent; a calm storm is sparse and distant; nothing at all when it is not
// storming.
//
// Design, mirroring WeatherSystem:
//   * One apricot Rng, seeded through hash_coord off the run seed, on its own
//     channel so lightning and weather do not reshuffle each other.
//   * The scheduling and the intensity -> flash/thunder/delay maths are free
//     functions, so the whole thing pins headless.
//   * A single per-strike "intensity" s in [intensity_min, 1] IS the distance:
//     close is a bright flash, loud, near-immediate; distant is dim, quiet and
//     long-delayed. All three derived quantities are monotonic in s.
//   * The flash reduces to an EXACT no-op when no strike is active, so it can
//     stack on the weather look without perturbing the clear-weather baseline.
//
// Flash only: no bolt mesh, no positional audio, no gameplay effect.
//
// NOT LIFTED (PENG-29): apply_lightning_flash(SkyEnv&, float), for the same
// reason weather's modulator stayed behind — gfx/sky_env.h owns what happens to
// a lighting environment, and flash() hands out the scalar it needs.

namespace apricot {

// Live tuning for the lightning/thunder feel. Defaults reproduce the
// shipped feel; the dev panel's "Reset lightning defaults" restores them.
struct LightningTuning {
    // Strike FREQUENCY. Mean strikes-per-second at FULL storm intensity; the
    // realised rate scales down with storm intensity and is zero outside Storm.
    float strikes_per_sec_max = 0.18f;   // ~1 strike / 5.5 s avg at full storm
    // Storms below this intensity don't strike (just the heavy look). Auto
    // storms schedule at intensity >= 0.55, so all of them strike; a forced
    // low-intensity storm can sit under this floor and stay silent.
    float min_storm_intensity = 0.20f;

    // Per-strike INTENSITY range. Each strike rolls s in [intensity_min, 1],
    // with the mean pulled up by storm intensity (heavier storm = brighter).
    float intensity_min = 0.25f;

    // FLASH: peak additive light/ambient punch at s = 1, and the exponential
    // decay time-constant (s). Short so it reads as a flash; a quick secondary
    // flicker rides on top (see lightning_flash_envelope).
    float flash_peak  = 1.40f;
    float flash_decay = 0.10f;

    // THUNDER: the flash->thunder delay (s) for the most distant (s -> 0)
    // strike; a close (s -> 1) strike is near-immediate. And the loudness floor
    // a distant strike maps to (close strikes map to 1.0).
    float max_thunder_delay = 7.0f;
    float thunder_vol_min   = 0.35f;
};

// ---- Pure helpers (unit-tested headlessly) ---------------------------------

// Mean strike rate (strikes/sec) for a weather kind + storm intensity. ZERO
// unless kind == Storm; for Storm it ramps from 0 at `min_storm_intensity` up to
// `strikes_per_sec_max` at intensity 1 (monotonic non-decreasing in intensity).
float lightning_rate_per_sec(const LightningTuning& t, WeatherKind kind,
                             float storm_intensity);

// Flash->thunder delay (s) for a strike of intensity s in [0,1]. Monotonic
// DECREASING: a close/bright strike cracks almost immediately, a distant/dim one
// rumbles in after up to `max_thunder_delay`.
float lightning_delay_for(const LightningTuning& t, float s);

// Thunder loudness [0,1] for a strike of intensity s. Monotonic INCREASING from
// `thunder_vol_min` (distant) to 1 (close).
float lightning_volume_for(const LightningTuning& t, float s);

// Additive flash brightness `t_since` seconds after a strike of intensity s. A
// bright initial spike plus a quick secondary flicker (both exponential), and
// EXACTLY 0 once decayed past a hard cutoff — so an old / no strike contributes
// nothing (the no-op-reduction guarantee). Scales linearly with s and flash_peak.
float lightning_flash_envelope(const LightningTuning& t, float t_since, float s);

// ---- The strike system -----------------------------------------------------

class LightningSystem {
public:
    explicit LightningSystem(uint64_t seed = 0xb01d1337u);

    // Advance the scheduler, the flash decay, and the pending-thunder timers.
    // `kind`/`storm_intensity` come from WeatherSystem. Frame-rate independent,
    // and it plays nothing — it QUEUES thunder loudnesses for the caller to
    // forward on this frame (see thunders_this_frame()).
    void update(float dt, WeatherKind kind, float storm_intensity);

    // Current additive flash brightness in [0, flash_peak]. 0 means no active
    // flash, and whatever consumes this must treat 0 as an exact no-op.
    float flash() const { return flash_; }

    // Thunder one-shots whose delay elapsed during the most recent update():
    // each entry is a loudness in [0,1]. Populated by update() (cleared at its
    // start), so the caller drains it AFTER update() and BEFORE the next one.
    const std::vector<float>& thunders_this_frame() const { return fired_; }

    // Dev: fire a strike right now at an explicit intensity, independent of the
    // scheduler. Clamped to [intensity_min, 1].
    void trigger_strike(float intensity);

    // Total strikes rolled since construction (telemetry / tests).
    int strikes() const { return strikes_; }

    LightningTuning&       tuning()       { return tuning_; }
    const LightningTuning& tuning() const { return tuning_; }

    // Deterministic re-seed. Through hash_coord, like WeatherSystem::seed, so
    // seed 0 needs no special case.
    void seed(uint64_t s) { rng_ = city_system_rng(s, kChannelLightning); }

private:
    float frand(float lo, float hi);
    void  spawn_strike(float storm_intensity);  // roll s, then emit_strike
    void  emit_strike(float s);                 // flash + queue thunder for s
    float next_interval(float rate);            // jittered gap to next strike

    LightningTuning tuning_;

    // Flash state: a single retriggerable envelope (a new strike resets it).
    bool  flash_active_    = false;
    float flash_           = 0.f;  // current additive brightness
    float flash_time_      = 0.f;  // seconds since the active strike
    float flash_intensity_ = 0.f;  // s of the active strike

    // Pending thunder rumbles still in flight (delay counting down).
    struct Pending { float delay; float volume; };
    std::vector<Pending> pending_;
    std::vector<float>   fired_;   // loudnesses that fired during update()

    bool          striking_      = false; // was the rate > 0 last update?
    float         time_to_next_  = 0.f;   // seconds until the next strike roll
    int           strikes_       = 0;
    Rng           rng_;
};

} // namespace apricot
