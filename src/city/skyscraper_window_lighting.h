#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include <glm/vec3.hpp>

namespace apricot::city {

// A building's broad use controls the shape of its night: offices empty out
// after dinner, homes fill up through the evening, and mixed towers carry a
// bit of both. Individual suites still get their own timing and preference.
enum class SkyscraperUse : uint8_t { Office, Residential, Mixed };

// Nearby suites can change independently. A wide hysteresis band keeps a
// tower from bouncing between dynamic and static lighting as the camera moves
// around the boundary.
inline constexpr float kSkyscraperWindowDynamicEnterDistanceM = 300.0f;
inline constexpr float kSkyscraperWindowDynamicExitDistanceM = 420.0f;
inline constexpr uint64_t kSkyscraperWindowMinDwellSeconds = 60u;
inline constexpr uint64_t kSkyscraperWindowMaxDwellSeconds = 180u;
// Occupancy decisions are intentionally abrupt, but their emissive result is
// not. A four-second slew keeps thousands of independently scheduled panes
// from reading as city-wide sparkle when several decisions land close
// together.
inline constexpr float kSkyscraperWindowFadeSeconds = 4.0f;
inline constexpr float kSkyscraperWindowMaxEmissivePower = 1.20f;

// Stable authored identity. These are deliberately keys rather than array
// indices: inserting a new tower or pane must not reshuffle every existing
// window's night.
struct SkyscraperWindowAddress {
    uint64_t building_key = 0;
    uint64_t floor_key = 0;
    uint64_t window_key = 0;
    SkyscraperUse use = SkyscraperUse::Office;
};

struct SkyscraperWindowLight {
    bool lit = false;
    float emissive_alpha = 1.0f;
    glm::vec3 tint{0.075f, 0.12f, 0.15f};
};

// Render-only smoothing. The deterministic occupancy/LOD state above remains
// the source of truth; this state merely eases the shader's emissive channel
// toward that target without changing which suite is occupied.
struct SkyscraperWindowPresentationState {
    uint64_t last_step = 0;
    bool initialized = false;
    float emissive_power = 0.0f;
    glm::vec3 lit_tint{0.92f, 0.90f, 0.82f};
};

// Per-pane state for the lighting LOD handoff. Static mode keeps occupied
// exactly as dynamic mode left it. Re-entering dynamic mode starts with that
// same pattern, waits for this pane's next slow beat, then resumes updates.
struct SkyscraperWindowLodState {
    uint64_t session_seed = 0;
    uint64_t next_dynamic_update_step = 0;
    bool initialized = false;
    bool dynamic = false;
    bool occupied = false;
};

namespace skyscraper_window_detail {

inline constexpr uint64_t kStepsPerSecond = 120u;
inline constexpr uint64_t kTimingSalt = 0x452821e638d01377ull;

inline uint64_t mix(uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27u)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31u);
}

inline uint64_t address_key(const SkyscraperWindowAddress& address,
                            uint64_t session_seed, uint64_t salt) {
    uint64_t key = mix(session_seed ^ salt);
    key ^= mix(address.building_key + 0x243f6a8885a308d3ull);
    key ^= mix(address.floor_key + 0x13198a2e03707344ull);
    key ^= mix(address.window_key + 0xa4093822299f31d0ull);
    key ^= mix(static_cast<uint64_t>(address.use) +
               0x082efa98ec4e6c89ull);
    return mix(key);
}

inline float unit_float(uint64_t value) {
    // The top 24 bits map exactly into a float's useful [0, 1) precision.
    return static_cast<float>(value >> 40u) * (1.0f / 16777216.0f);
}

inline float wrapped_day(float time_of_day) {
    if (!std::isfinite(time_of_day)) return 0.0f;
    float wrapped = std::fmod(time_of_day, 1.0f);
    if (wrapped < 0.0f) wrapped += 1.0f;
    return wrapped;
}

struct ClockPoint {
    float time;
    float probability;
};

template <std::size_t N>
inline float sample_clock(const std::array<ClockPoint, N>& points,
                          float time_of_day) {
    const float time = wrapped_day(time_of_day);
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (time > points[i].time) continue;
        const float span = points[i].time - points[i - 1].time;
        const float t = span > 0.0f
                            ? (time - points[i - 1].time) / span
                            : 0.0f;
        return points[i - 1].probability +
               (points[i].probability - points[i - 1].probability) * t;
    }
    return points.back().probability;
}

inline float office_probability(float time_of_day) {
    // Lights used rather than literal bodies at desks: the dinner-hour peak
    // includes cleaners and meeting rooms, then drops hard after 22:00.
    constexpr std::array<ClockPoint, 10> profile{{
        {0.0f, 0.10f},
        {4.0f / 24.0f, 0.025f},
        {6.0f / 24.0f, 0.03f},
        {8.0f / 24.0f, 0.12f},
        {12.0f / 24.0f, 0.06f},
        {17.0f / 24.0f, 0.26f},
        {18.0f / 24.0f, 0.68f},
        {20.0f / 24.0f, 0.58f},
        {22.0f / 24.0f, 0.30f},
        {1.0f, 0.10f},
    }};
    return sample_clock(profile, time_of_day);
}

inline float residential_probability(float time_of_day) {
    constexpr std::array<ClockPoint, 10> profile{{
        {0.0f, 0.45f},
        {4.0f / 24.0f, 0.10f},
        {6.0f / 24.0f, 0.09f},
        {8.0f / 24.0f, 0.07f},
        {12.0f / 24.0f, 0.08f},
        {17.0f / 24.0f, 0.32f},
        {18.0f / 24.0f, 0.58f},
        {20.0f / 24.0f, 0.76f},
        {22.0f / 24.0f, 0.68f},
        {1.0f, 0.45f},
    }};
    return sample_clock(profile, time_of_day);
}

}  // namespace skyscraper_window_detail

// Probability that an otherwise typical suite wants its light on at this
// visible time of day. Darkness is intentionally not folded in here: the sky
// can be pinned by a QA preset while the deterministic occupancy clock keeps
// advancing from the absolute simulation step.
inline float skyscraper_occupancy_probability(SkyscraperUse use,
                                               float visible_time_of_day) {
    using namespace skyscraper_window_detail;
    const float office = office_probability(visible_time_of_day);
    const float residential = residential_probability(visible_time_of_day);
    switch (use) {
        case SkyscraperUse::Office: return office;
        case SkyscraperUse::Residential: return residential;
        case SkyscraperUse::Mixed:
            return office * 0.56f + residential * 0.44f;
    }
    return office;
}

// A tower owns one permanent lamp temperature. building_key comes from its
// authored site, so neither a new session seed nor an individual suite timer
// can change a building from one approved yellow-white shade to another.
inline glm::vec3 skyscraper_building_window_tint(uint64_t building_key) {
    using namespace skyscraper_window_detail;
    constexpr std::array<glm::vec3, 5> kLampPalette{{
        {0.92f, 0.90f, 0.82f},  // warm white
        {0.95f, 0.90f, 0.70f},  // ivory
        {0.90f, 0.90f, 0.86f},  // neutral white
        {0.96f, 0.87f, 0.58f},  // soft yellow
        {0.88f, 0.88f, 0.85f},  // soft white
    }};
    const uint64_t appearance =
        mix(building_key ^ 0xb8e1afed6a267e96ull);
    return kLampPalette[appearance % kLampPalette.size()];
}

inline uint64_t skyscraper_window_interval_steps(
    const SkyscraperWindowAddress& address, uint64_t session_seed) {
    using namespace skyscraper_window_detail;
    const uint64_t timing_key = address_key(address, session_seed, kTimingSalt);
    const uint64_t interval_seconds =
        kSkyscraperWindowMinDwellSeconds +
        timing_key % (kSkyscraperWindowMaxDwellSeconds -
                      kSkyscraperWindowMinDwellSeconds + 1u);
    return interval_seconds * kStepsPerSecond;
}

// Turn a cached occupancy decision into the actual visible/emissive pane.
// Darkness stays live in both LODs, so dawn and dusk still work without
// changing which rooms the static facade remembers.
inline SkyscraperWindowLight skyscraper_window_light_from_occupancy(
    const SkyscraperWindowAddress& address, uint64_t session_seed,
    bool occupied, float darkness) {
    using namespace skyscraper_window_detail;
    const uint64_t timing_key = address_key(address, session_seed, kTimingSalt);
    const float safe_darkness =
        std::isfinite(darkness) ? std::clamp(darkness, 0.0f, 1.0f) : 0.0f;
    const float activation =
        0.08f + 0.20f *
                    unit_float(mix(timing_key ^ 0x2ffd72dbd01adfb7ull));
    const bool lit = occupied && safe_darkness >= activation;

    SkyscraperWindowLight result;
    result.lit = lit;
    if (!lit) {
        // Alpha above one emits unconditionally in lit.frag, including noon.
        // Keep the off/day value exact so callers cannot leak emissive light.
        result.emissive_alpha = 1.0f;
        result.tint = {0.075f, 0.12f, 0.15f};
        return result;
    }

    const uint64_t suite_appearance =
        address_key(address, session_seed, 0xb8e1afed6a267e96ull);
    const float strength =
        0.72f + 0.48f *
                    unit_float(mix(suite_appearance ^
                                   0x9c30d5392af26013ull));
    result.emissive_alpha = 1.0f + safe_darkness * strength;
    result.tint = skyscraper_building_window_tint(address.building_key);
    return result;
}

// Smooth a scheduled light for presentation. The rate is based on the saved
// 120 Hz simulation clock, so a 30, 60 or 144 Hz render produces the same
// result. Initial load snaps to its authored state; only later changes fade.
inline SkyscraperWindowLight skyscraper_window_smoothed_light(
    SkyscraperWindowPresentationState& state,
    const SkyscraperWindowLight& target, uint64_t absolute_step) {
    using namespace skyscraper_window_detail;

    const float target_power = target.lit &&
                                       std::isfinite(target.emissive_alpha)
                                   ? std::clamp(target.emissive_alpha - 1.0f,
                                                0.0f,
                                                kSkyscraperWindowMaxEmissivePower)
                                   : 0.0f;
    if (target.lit) state.lit_tint = target.tint;

    if (!state.initialized || absolute_step < state.last_step) {
        state.initialized = true;
        state.last_step = absolute_step;
        state.emissive_power = target_power;
    } else if (absolute_step > state.last_step) {
        const uint64_t elapsed_steps = absolute_step - state.last_step;
        const float max_change =
            static_cast<float>(elapsed_steps) /
            (static_cast<float>(kStepsPerSecond) *
             kSkyscraperWindowFadeSeconds) *
            kSkyscraperWindowMaxEmissivePower;
        if (state.emissive_power < target_power)
            state.emissive_power =
                std::min(target_power, state.emissive_power + max_change);
        else
            state.emissive_power =
                std::max(target_power, state.emissive_power - max_change);
        state.last_step = absolute_step;
    }

    // Collapse the tiny tail to exact-off so overlay panes can disappear and
    // existing glass can return to its normal non-emissive material.
    if (target_power == 0.0f && state.emissive_power < 0.001f)
        state.emissive_power = 0.0f;

    SkyscraperWindowLight result;
    result.lit = state.emissive_power > 0.0f;
    result.emissive_alpha = 1.0f + state.emissive_power;
    result.tint = state.lit_tint;
    return result;
}

// Pure, order-independent suite lighting. absolute_step is the saved 120 Hz
// simulation step, never a rendered frame or wall clock. Each suite chooses a
// 60..180 second check interval and a separate phase. That makes changes feel
// irregular across a facade while retaining exact replay behavior.
inline SkyscraperWindowLight skyscraper_window_light(
    const SkyscraperWindowAddress& address, uint64_t session_seed,
    uint64_t absolute_step, float visible_time_of_day, float darkness,
    float time_of_day_per_step = 0.0f) {
    using namespace skyscraper_window_detail;

    const uint64_t timing_key = address_key(address, session_seed, kTimingSalt);
    const uint64_t interval_steps =
        skyscraper_window_interval_steps(address, session_seed);
    const uint64_t phase = mix(timing_key ^ 0xbe5466cf34e90c6cull) %
                           interval_steps;
    const uint64_t epoch = (absolute_step + phase) / interval_steps;

    // Sample the occupancy curve at this suite's epoch boundary. Without this
    // reconstruction, a fast-moving sky can slide the probability past a
    // fixed decision between beats and defeat the promised minimum dwell.
    // The darkness gate below intentionally stays live so dusk still rolls
    // across the facade instead of snapping on at one instant.
    const double epoch_start_step =
        static_cast<double>(epoch) * static_cast<double>(interval_steps) -
        static_cast<double>(phase);
    const double safe_time_rate = std::isfinite(time_of_day_per_step)
                                      ? time_of_day_per_step
                                      : 0.0;
    const float epoch_time_of_day = wrapped_day(static_cast<float>(
        static_cast<double>(visible_time_of_day) +
        (epoch_start_step - static_cast<double>(absolute_step)) *
            safe_time_rate));

    // Building and floor biases give neighboring suites a little shared
    // character without sharing their decision clock. The per-window bias is
    // stronger, so floors never collapse into one solid strip of light.
    const float building_bias =
        (unit_float(mix(session_seed ^ address.building_key ^
                        0xc0ac29b7c97c50ddull)) -
         0.5f) *
        0.10f;
    const float floor_bias =
        (unit_float(mix(session_seed ^ address.building_key ^
                        mix(address.floor_key) ^ 0x3f84d5b5b5470917ull)) -
         0.5f) *
        0.14f;
    const float suite_bias =
        (unit_float(mix(timing_key ^ 0x9216d5d98979fb1bull)) - 0.5f) *
        0.24f;
    const float probability = std::clamp(
        skyscraper_occupancy_probability(address.use, epoch_time_of_day) +
            building_bias + floor_bias + suite_bias,
        0.01f, 0.94f);
    const float decision = unit_float(
        mix(timing_key ^ mix(epoch + 0xd1310ba698dfb5acull)));
    // A few suites stay vacant for the whole session. A much smaller set are
    // security desks, stair landings or overnight operations that remain on
    // whenever it is dark. Both traits are stable, so they add texture without
    // turning the facade into visual noise.
    const uint64_t occupancy_traits = address_key(
        address, session_seed, 0x7f4a7c159e3779b9ull);
    const bool vacant =
        unit_float(mix(occupancy_traits ^ 0x85a308d3243f6a88ull)) < 0.065f;
    const bool security =
        unit_float(mix(occupancy_traits ^ 0x0370734413198a2eull)) < 0.018f;
    const bool occupied = security || (!vacant && decision < probability);

    return skyscraper_window_light_from_occupancy(
        address, session_seed, occupied, darkness);
}

inline bool skyscraper_window_dynamic_lod(float viewer_distance_m,
                                          bool currently_dynamic) {
    if (!std::isfinite(viewer_distance_m) || viewer_distance_m < 0.0f)
        return false;
    const float threshold = currently_dynamic
                                ? kSkyscraperWindowDynamicExitDistanceM
                                : kSkyscraperWindowDynamicEnterDistanceM;
    return viewer_distance_m <= threshold;
}

inline bool skyscraper_window_initial_lod_occupancy(
    const SkyscraperWindowAddress& address, uint64_t session_seed) {
    const float static_evening_time =
        address.use == SkyscraperUse::Residential ? 21.0f / 24.0f
        : address.use == SkyscraperUse::Office ? 20.0f / 24.0f
                                                : 20.5f / 24.0f;
    return skyscraper_window_light(address, session_seed, 0u,
                                    static_evening_time, 1.0f, 0.0f)
        .lit;
}

// Stateful LOD handoff. Far mode does no occupancy scheduling at all: it
// renders the last cached pattern. Crossing into near mode keeps that exact
// pattern for one full per-suite interval before dynamic decisions resume.
// Crossing back out simply freezes the latest dynamic result.
inline SkyscraperWindowLight skyscraper_window_lod_light(
    SkyscraperWindowLodState& state,
    const SkyscraperWindowAddress& address, uint64_t session_seed,
    uint64_t absolute_step, float visible_time_of_day, float darkness,
    float viewer_distance_m, float time_of_day_per_step = 0.0f) {
    const bool reset = !state.initialized || state.session_seed != session_seed;
    if (reset) {
        state = {};
        state.initialized = true;
        state.session_seed = session_seed;
        state.dynamic = skyscraper_window_dynamic_lod(viewer_distance_m, false);
        if (state.dynamic) {
            state.occupied = skyscraper_window_light(
                address, session_seed, absolute_step, visible_time_of_day,
                1.0f, time_of_day_per_step).lit;
            state.next_dynamic_update_step =
                absolute_step +
                skyscraper_window_interval_steps(address, session_seed);
        } else {
            state.occupied = skyscraper_window_initial_lod_occupancy(
                address, session_seed);
        }
    } else {
        const bool wants_dynamic = skyscraper_window_dynamic_lod(
            viewer_distance_m, state.dynamic);
        if (wants_dynamic != state.dynamic) {
            state.dynamic = wants_dynamic;
            if (state.dynamic) {
                // Do not replace the static LOD on the load frame. Its exact
                // pattern is now the starting state of the dynamic windows.
                state.next_dynamic_update_step =
                    absolute_step +
                    skyscraper_window_interval_steps(address, session_seed);
            } else {
                // occupied already holds the last near decision. Leaving it
                // untouched is the requested updated static LOD snapshot.
                state.next_dynamic_update_step = 0;
            }
        }

        if (state.dynamic &&
            absolute_step >= state.next_dynamic_update_step) {
            state.occupied = skyscraper_window_light(
                address, session_seed, absolute_step, visible_time_of_day,
                1.0f, time_of_day_per_step).lit;
            const uint64_t interval =
                skyscraper_window_interval_steps(address, session_seed);
            do {
                state.next_dynamic_update_step += interval;
            } while (state.next_dynamic_update_step <= absolute_step);
        }
    }

    return skyscraper_window_light_from_occupancy(
        address, session_seed, state.occupied, darkness);
}

}  // namespace apricot::city
