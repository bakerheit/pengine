#pragma once

#include <cstdint>

#include "city/city_rng.h"

// The weather state machine.
//
// A `WeatherKind` (Clear / Overcast / Rain / Storm) names a discrete mood; a
// `WeatherState` is the continuous, lerp-able set of modulation weights that a
// renderer consumes. All-zero is the identity, and the system blends toward its
// target look so a change of weather is a transition rather than a cut.
//
// Pure logic: no host-layer types and no glm in the header, so the state
// machine, the scheduler, the drift and the progression all pin headless.
//
// TWO THINGS THIS DELIBERATELY DOES NOT DO, both left for the wiring ticket.
//
// It does not touch a lighting environment. probablecause ended this file with
// an apply_weather(SkyEnv&, const WeatherState&) that greyed and dimmed the sky
// palette in place. apricot already owns that step, in gfx/sky_env.h, against
// its own WeatherParams and with the same exact-no-op-at-zero guarantee. Two
// functions modulating one struct is how a look starts depending on which one
// ran; whoever wires weather up maps WeatherState onto WeatherParams and there
// stays exactly one modulator.
//
// And it does not claim to be apricot's only weather. game/conditions.h already
// derives rain, wetness and grip as a pure function of (seed, sim step),
// specifically so a ghost replaying at a different point in a session gets the
// weather it recorded under. This system is stateful and does not have that
// property. They overlap and they must be reconciled; PENG-29 lands the library
// and does not wire it, so the reconciliation is somebody's ticket, not a
// silent choice made here.

namespace apricot {

enum class WeatherKind { Clear, Overcast, Rain, Storm };

// Human label for UI / console echo.
const char* weather_kind_name(WeatherKind k);

// The continuous atmosphere-modulation weights produced by blending toward a
// weather kind at some intensity. All-zero is the identity (no modulation) — the
// renderer can skip the work entirely, and whatever applies these is then a
// guaranteed exact no-op on a clear day.
struct WeatherState {
    float cloud_add  = 0.f; // added to the sky's cloud cover (0..1 after clamp)
    float darken     = 0.f; // 0..1: dims the directional light + ambient
    float desaturate = 0.f; // 0..1: greys the palette/light toward overcast grey
    float fog        = 0.f; // 0..1: pulls the distance fog in (tint follows sky)

    bool is_identity() const {
        return cloud_add == 0.f && darken == 0.f &&
               desaturate == 0.f && fog == 0.f;
    }
};

// Per-kind tuning: the look each state targets at full intensity (Clear is fixed
// identity and not listed), plus the scheduler cadence + transition speed. These
// are the live look-knobs.
struct WeatherTuning {
    // Base looks at intensity = 1.0 (scaled down by the running intensity).
    WeatherState overcast{0.35f, 0.45f, 0.55f, 0.15f};
    WeatherState rain{0.45f, 0.60f, 0.70f, 0.45f};
    WeatherState storm{0.55f, 0.80f, 0.85f, 0.70f};

    float transition_secs = 8.0f;  // exp time-constant of the look transition
    float dwell_min_secs  = 45.f;  // scheduler: min time held in a state
    float dwell_max_secs  = 120.f; // scheduler: max time held in a state

    // PCG-041 part A — intra-state look drift. Within Rain/Storm (and any forced
    // non-Clear state) the *effective* intensity wanders within +/- drift_amount
    // of the state's center on a smoothed, seeded random walk, so the sky ebbs and
    // surges instead of holding one flat look. Clear NEVER drifts (drift_amount
    // is ignored there — the no-drift identity guarantee). drift_amount == 0
    // disables the wander entirely.
    float drift_amount = 0.18f; // band half-width around center (0..~0.5)
    float drift_speed  = 0.12f; // walk rate (1/s-ish): higher = faster ebb/surge

    // PCG-041 part B — directed scheduler progression (seeded). While Rain, a
    // per-decision chance to escalate into Storm (a front building up); Storm
    // winds down toward Rain rather than teleporting to Clear. Calm states still
    // pick calm-biased, and Storm is reachable ONLY by escalating from Rain.
    float rain_to_storm = 0.25f; // P(Rain  -> Storm) per scheduler decision
    float storm_to_rain = 0.65f; // P(Storm -> Rain)  wind-down per decision
};

// Pure: the target look for a kind at intensity [0,1]. Clear (or intensity 0)
// returns the identity WeatherState — the no-drift guarantee at the source.
WeatherState weather_look_for(const WeatherTuning& t, WeatherKind kind,
                              float intensity);

// NOT LIFTED (PENG-29): apply_weather(SkyEnv&, const WeatherState&). See the
// file banner — gfx/sky_env.h already owns lighting modulation, and one struct
// wants one modulator.

// ---- PCG-041 part A: intra-state intensity drift ---------------------------
//
// A bounded, smoothed random walk in [-1, 1]. `value` is the live normalized
// offset (fed through `weather_drifted_intensity`); `target` is the current walk
// target; `timer` counts down to the next target re-pick. All state lives here so
// the step is a pure function of (state, dt, speed, rng).
struct WeatherDrift {
    float value  = 0.f; // current smoothed offset, [-1,1]
    float target = 0.f; // walk target, [-1,1]
    float timer  = 0.f; // seconds until the next target re-pick (<=0 => re-pick)
};

// Pure: advance the drift one timestep. `speed` sets how fast `value` eases toward
// `target` (exp smoothing) and the cadence of fresh targets. `rng` is advanced
// deterministically only when a new target is drawn. `value` stays in [-1,1] and
// only ever moves by a bounded per-step delta (no jumps), so the look never pops.
void weather_drift_step(WeatherDrift& d, float dt, float speed, Rng& rng);

// Pure: the effective (drifted) intensity for a state. Clear (or a non-positive
// center) returns the center EXACTLY — no drift at Clear (the identity guarantee).
// Otherwise returns center + value*band, clamped into the state's [0,1] band, so
// the wander stays inside the state and never crosses into a worse state's look.
float weather_drifted_intensity(WeatherKind kind, float center,
                                const WeatherDrift& d, float band);

// ---- PCG-041 part B: directed scheduler progression ------------------------
//
// Pure: choose the next weather kind given the current one, using a seeded roll.
// Rain may escalate to Storm with probability `t.rain_to_storm` (a front building
// up); Storm winds down to Rain with `t.storm_to_rain` (else lingers); calm states
// pick calm-biased and Storm is reachable ONLY via Rain escalation. `rng` advanced
// deterministically. Calm stays dominant over a long horizon.
WeatherKind weather_next_kind(WeatherKind current, const WeatherTuning& t,
                              Rng& rng);

// The weather state machine: target kind + intensity, a smooth transition of the
// blended look, and a seeded scheduler that advances weather over time. Dev code
// can force a state (suspending the scheduler) and resume auto.
class WeatherSystem {
public:
    explicit WeatherSystem(uint64_t seed = 0x5eed1234u);

    // Advance the scheduler + the look transition. Frame-rate independent.
    void update(float dt);

    // What the renderer consumes each frame (blended / transitioning weights).
    const WeatherState& state() const { return current_; }

    // Dev controls. force() pins a state and suspends the scheduler until
    // set_auto() resumes it. Intensity is clamped to [0,1].
    void force(WeatherKind kind, float intensity);
    void set_auto();
    bool is_auto() const { return auto_; }

    // Target descriptors (for UI labels / sliders).
    WeatherKind kind() const { return target_kind_; }

    // The LIVE, drifted intensity the renderer + downstream consumers read each
    // frame (precip density, lightning rate, audio bed all ride this) — so a
    // single intensity drift makes them all ebb and surge together (PCG-041 A).
    float intensity() const { return effective_intensity_; }

    // The state's CENTER intensity (what force()/the scheduler pinned). Stable —
    // for UI sliders / readouts that shouldn't jitter with the live drift.
    float target_intensity() const { return target_intensity_; }

    WeatherTuning&       tuning()       { return tuning_; }
    const WeatherTuning& tuning() const { return tuning_; }

    // Deterministic re-seed. Goes through hash_coord() rather than being
    // assigned raw, so seed 0 is a perfectly good city like any other — the
    // gamma offset in hash_coord is what keeps it off splitmix64's fixed point,
    // and that is why this no longer needs probablecause's "remap 0 to 1" hack.
    void seed(uint64_t s) { rng_ = city_system_rng(s, kChannelWeather); }

private:
    float frand(float lo, float hi);
    void  schedule_next();              // pick the next kind + intensity + dwell

    WeatherTuning tuning_;
    WeatherState  current_;             // blended look the renderer reads
    WeatherKind   target_kind_      = WeatherKind::Clear;
    float         target_intensity_ = 0.f; // state center (scheduler/force pin)
    float         effective_intensity_ = 0.f; // live drifted value (intensity())
    WeatherDrift  drift_;               // PCG-041: intra-state intensity wander
    bool          auto_             = true;
    float         dwell_left_       = 0.f; // seconds until the next auto change
    Rng           rng_;
};

} // namespace apricot
