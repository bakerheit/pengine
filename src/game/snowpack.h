#pragma once

#include <algorithm>
#include <cmath>

namespace apricot {

// Gameplay rates are expressed in metres per simulated second. Snowfall and
// heat inputs are normalized [0, 1] intensities supplied by the caller.
struct SnowpackTuning {
    double max_depth_m = 1.5;
    double accumulation_rate_m_per_s = 0.00025;
    double passive_melt_rate_m_per_s = 0.00005;
    double heat_melt_rate_m_per_s = 0.00050;
    double visual_full_cover_depth_m = 0.10;
    double collision_threshold_depth_m = 0.10;
};

namespace snowpack_detail {

inline double finite_nonnegative(double value, double fallback) {
    if (!std::isfinite(value)) return fallback;
    return std::max(value, 0.0);
}

inline double unit(double value) {
    if (std::isnan(value) || value <= 0.0) return 0.0;
    if (value >= 1.0) return 1.0;
    return value;
}

inline double max_depth(const SnowpackTuning& tuning) {
    return finite_nonnegative(tuning.max_depth_m, 1.5);
}

inline double clamped_depth(double depth_m, const SnowpackTuning& tuning) {
    if (std::isnan(depth_m) || depth_m <= 0.0) return 0.0;
    return std::min(depth_m, max_depth(tuning));
}

inline double rate(double value, double fallback) {
    return finite_nonnegative(value, fallback);
}

}  // namespace snowpack_detail

// Accumulated physical snow depth. Current snowfall is intentionally not part
// of the state: it is a forcing input to advance_snowpack(), so stopping the
// weather does not erase the pack and a replay controls every input explicitly.
class SnowpackState {
public:
    double depth_m() const { return depth_m_; }

    // Safe dev/save-game override. Bad and negative values become zero;
    // positive infinity and oversized values become the configured maximum.
    void set_depth_m(double depth_m, const SnowpackTuning& tuning = {}) {
        depth_m_ = snowpack_detail::clamped_depth(depth_m, tuning);
    }

private:
    double depth_m_ = 0.0;
};

// Pure fixed-step update: equal state, inputs, dt and tuning yield equal output.
// Linear rates make ordinary fixed-step partitioning numerically stable, while
// double precision keeps long sessions from losing small per-step changes.
inline SnowpackState advance_snowpack(
    const SnowpackState& state, double snowfall_intensity,
    double heat_intensity, double dt_seconds,
    const SnowpackTuning& tuning = {}) {
    SnowpackState next = state;
    if (!std::isfinite(dt_seconds) || dt_seconds <= 0.0) {
        next.set_depth_m(state.depth_m(), tuning);
        return next;
    }

    const double snow = snowpack_detail::unit(snowfall_intensity);
    const double heat = snowpack_detail::unit(heat_intensity);
    const double accumulation_rate = snow * snowpack_detail::rate(
        tuning.accumulation_rate_m_per_s, 0.00025);
    // Passive thaw fades out as snowfall intensifies. Keeping this continuous
    // avoids a pack/cover pop when a weather front crosses exactly zero snow.
    // Heat can melt the pack during snowfall too, and at full strength exceeds
    // the default accumulation.
    const double melt_rate =
        (1.0 - snow) * snowpack_detail::rate(
            tuning.passive_melt_rate_m_per_s, 0.00005) +
        heat * snowpack_detail::rate(
            tuning.heat_melt_rate_m_per_s, 0.00050);

    next.set_depth_m(
        state.depth_m() + (accumulation_rate - melt_rate) * dt_seconds,
        tuning);
    return next;
}

// Stateless render mapping. A 5 cm pack is half cover with the defaults;
// 10 cm and deeper is fully covered. Smoothstep avoids a hard visual edge.
inline float visual_snow_cover_from_depth(
    double depth_m, const SnowpackTuning& tuning = {}) {
    const double full_cover_depth = std::max(
        snowpack_detail::finite_nonnegative(
            tuning.visual_full_cover_depth_m, 0.10),
        0.000001);
    const double t = snowpack_detail::unit(
        snowpack_detail::clamped_depth(depth_m, tuning) / full_cover_depth);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

struct SnowpackCollision {
    bool active = false;
    double depth_m = 0.0;
};

// Thin dustings stay visual-only. The threshold acts as a compressed base
// layer; only the depth above it raises the collision surface. That avoids a
// sudden ten-centimetre pop on the frame the pack crosses the threshold.
inline SnowpackCollision snowpack_collision_from_depth(
    double depth_m, const SnowpackTuning& tuning = {}) {
    const double depth = snowpack_detail::clamped_depth(depth_m, tuning);
    const double threshold = std::min(
        snowpack_detail::finite_nonnegative(
            tuning.collision_threshold_depth_m, 0.10),
        snowpack_detail::max_depth(tuning));
    if (depth <= threshold || depth <= 0.0) return {};
    return {true, depth - threshold};
}

}  // namespace apricot
