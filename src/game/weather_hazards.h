#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "physics/vehicle.h"

namespace apricot {

// Stateless gameplay-side weather effects. Callers provide the complete input
// for a sample; there are no clocks, random sources, globals, or accumulators
// here, so replay code can evaluate these helpers in any order.
enum class GameplayHazard : uint8_t {
    None,
    Tornado,
    SnowIce,
    Flood,
    Hail,
    Heatwave,
};

namespace weather_hazard_detail {

inline float finite_or(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

inline float unit(float value) {
    return std::clamp(finite_or(value, 0.0f), 0.0f, 1.0f);
}

inline float nonnegative(float value, float fallback, float maximum) {
    return std::clamp(finite_or(value, fallback), 0.0f, maximum);
}

inline float smooth01(float value) {
    const float t = unit(value);
    return t * t * (3.0f - 2.0f * t);
}

inline bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

inline float scaled(float value, float multiplier, float fallback,
                    float maximum) {
    const double base = static_cast<double>(
        nonnegative(value, fallback, maximum));
    const double scale = static_cast<double>(
        nonnegative(multiplier, 1.0f, 100.0f));
    return static_cast<float>(std::min(base * scale,
                                       static_cast<double>(maximum)));
}

}  // namespace weather_hazard_detail

struct HazardExposure {
    float tornado = 0.0f;
    float snow_ice = 0.0f;
    float flood = 0.0f;
    float hail = 0.0f;
    float heatwave = 0.0f;
};

struct TornadoParams {
    glm::vec2 center_xz{0.0f};
    float core_radius_m = 12.0f;
    float outer_radius_m = 120.0f;
    float max_horizontal_force_n = 28000.0f;
    float max_lift_force_n = 9000.0f;
    // 0 is purely tangential and 1 points entirely toward the funnel.
    float inward_fraction = 0.30f;
    bool clockwise = false;
};

struct TornadoForce {
    glm::vec3 force_n{0.0f};
    float exposure = 0.0f;
};

// Smooth full strength through the core, then cubic falloff to exactly zero at
// the outer radius. Equal or inverted radii intentionally become a hard edge.
inline TornadoForce tornado_force_at(glm::vec2 position_xz,
                                     const TornadoParams& params) {
    using namespace weather_hazard_detail;
    TornadoForce result;
    if (!finite(position_xz) || !finite(params.center_xz)) return result;

    constexpr float kMaxRadiusM = 100000.0f;
    constexpr float kMaxForceN = 1000000.0f;
    const float core = nonnegative(params.core_radius_m, 0.0f, kMaxRadiusM);
    const float outer = std::max(
        core, nonnegative(params.outer_radius_m, core, kMaxRadiusM));
    const glm::vec2 offset = position_xz - params.center_xz;
    const float distance = std::sqrt(offset.x * offset.x + offset.y * offset.y);
    if (!std::isfinite(distance) || distance > outer) return result;

    if (distance <= core) {
        result.exposure = 1.0f;
    } else if (outer > core) {
        result.exposure = 1.0f - smooth01((distance - core) / (outer - core));
    } else {
        // A zero-width field can still affect its exact center without a
        // division by zero. Everywhere else was rejected above.
        result.exposure = 1.0f;
    }

    const glm::vec2 outward = distance > 0.0001f
        ? offset / distance
        : glm::vec2{1.0f, 0.0f};
    const glm::vec2 tangent = params.clockwise
        ? glm::vec2{outward.y, -outward.x}
        : glm::vec2{-outward.y, outward.x};
    const float inward = unit(params.inward_fraction);
    glm::vec2 direction = tangent * (1.0f - inward) - outward * inward;
    const float direction_length = std::sqrt(
        direction.x * direction.x + direction.y * direction.y);
    if (direction_length > 0.0001f) direction /= direction_length;

    const float horizontal = nonnegative(
        params.max_horizontal_force_n, 0.0f, kMaxForceN) * result.exposure;
    const float lift = nonnegative(
        params.max_lift_force_n, 0.0f, kMaxForceN) * result.exposure;
    result.force_n = {direction.x * horizontal, lift,
                      direction.y * horizontal};
    return result;
}

struct FloodParams {
    // Standing-water elevation before the event-driven rise.
    float water_elevation_m = 0.0f;
    float max_event_rise_m = 2.0f;
    // Both caps keep malformed terrain or event data from creating absurd
    // physics values. full_severity_depth_m controls how quickly danger rises.
    float max_depth_m = 6.0f;
    float full_severity_depth_m = 1.25f;
};

struct FloodSample {
    float surface_elevation_m = 0.0f;
    float depth_m = 0.0f;
    float severity = 0.0f;
    float drag = 0.0f;
};

inline FloodSample flood_at(float terrain_elevation_m, float event_intensity,
                            const FloodParams& params = {}) {
    using namespace weather_hazard_detail;
    FloodSample result;
    if (!std::isfinite(terrain_elevation_m) ||
        !std::isfinite(params.water_elevation_m)) {
        return result;
    }

    constexpr float kMaxFloodDepthM = 100.0f;
    const float intensity = unit(event_intensity);
    const float rise = nonnegative(
        params.max_event_rise_m, 0.0f, kMaxFloodDepthM);
    const float depth_cap = nonnegative(
        params.max_depth_m, 0.0f, kMaxFloodDepthM);
    const float full_depth = std::max(
        nonnegative(params.full_severity_depth_m, 1.25f, kMaxFloodDepthM),
        0.01f);

    result.surface_elevation_m = params.water_elevation_m + intensity * rise;
    if (!std::isfinite(result.surface_elevation_m)) {
        result.surface_elevation_m = params.water_elevation_m;
    }
    result.depth_m = std::clamp(
        result.surface_elevation_m - terrain_elevation_m, 0.0f, depth_cap);
    result.severity = intensity * smooth01(result.depth_m / full_depth);
    // Drag is a normalized gameplay input, kept separate from physical depth.
    result.drag = unit(result.severity *
                       (0.35f + 0.65f * unit(result.depth_m / full_depth)));
    return result;
}

struct HazardVehicleAdjustments {
    float grip_multiplier = 1.0f;
    float lateral_grip_multiplier = 1.0f;
    float rolling_resistance_multiplier = 1.0f;
    float drag_multiplier = 1.0f;
    float engine_torque_multiplier = 1.0f;
};

inline HazardVehicleAdjustments hazard_vehicle_adjustments(
    const HazardExposure& exposure) {
    using weather_hazard_detail::unit;
    const float snow = unit(exposure.snow_ice);
    const float flood = unit(exposure.flood);
    const float hail = unit(exposure.hail);
    const float heat = unit(exposure.heatwave);

    HazardVehicleAdjustments out;
    out.grip_multiplier = std::clamp(
        (1.0f - 0.68f * snow) * (1.0f - 0.25f * flood) *
            (1.0f - 0.08f * hail) * (1.0f - 0.04f * heat),
        0.18f, 1.0f);
    out.lateral_grip_multiplier = std::clamp(
        (1.0f - 0.55f * snow) * (1.0f - 0.20f * flood), 0.25f, 1.0f);
    out.rolling_resistance_multiplier = 1.0f + 0.25f * snow + 2.75f * flood;
    out.drag_multiplier = 1.0f + 1.50f * flood + 0.08f * hail;
    out.engine_torque_multiplier = std::clamp(
        (1.0f - 0.18f * heat) * (1.0f - 0.12f * flood), 0.65f, 1.0f);
    return out;
}

// Applies only the tuning fields owned by these hazards. Tornado force remains
// an external body force, and service/handbrake torque stays untouched.
inline VehicleTuning apply_weather_hazards(const VehicleTuning& base,
                                           const HazardExposure& exposure) {
    using weather_hazard_detail::scaled;
    const HazardVehicleAdjustments effects =
        hazard_vehicle_adjustments(exposure);
    VehicleTuning out = base;
    out.grip_scale = scaled(base.grip_scale, effects.grip_multiplier,
                            1.0f, 4.0f);
    out.lateral_grip_scale = scaled(
        base.lateral_grip_scale, effects.lateral_grip_multiplier, 1.0f, 4.0f);
    out.rolling_resistance = scaled(
        base.rolling_resistance, effects.rolling_resistance_multiplier,
        0.018f, 1.0f);
    out.drag = scaled(base.drag, effects.drag_multiplier, 0.42f, 100.0f);
    out.engine_peak_torque = scaled(
        base.engine_peak_torque, effects.engine_torque_multiplier,
        420.0f, 5000.0f);
    return out;
}

inline const char* hazard_label(GameplayHazard hazard) {
    switch (hazard) {
        case GameplayHazard::Tornado: return "Tornado";
        case GameplayHazard::SnowIce: return "Snow / ice";
        case GameplayHazard::Flood: return "Flash flood";
        case GameplayHazard::Hail: return "Hail";
        case GameplayHazard::Heatwave: return "Heatwave";
        case GameplayHazard::None: return "Clear";
    }
    return "Clear";
}

inline GameplayHazard dominant_hazard(const HazardExposure& exposure) {
    using weather_hazard_detail::unit;
    GameplayHazard best = GameplayHazard::None;
    float severity = 0.0f;
    const auto consider = [&](GameplayHazard hazard, float candidate) {
        candidate = unit(candidate);
        if (candidate > severity) {
            severity = candidate;
            best = hazard;
        }
    };
    // Tie order is intentional: immediate threats beat environmental stress.
    consider(GameplayHazard::Tornado, exposure.tornado);
    consider(GameplayHazard::Flood, exposure.flood);
    consider(GameplayHazard::SnowIce, exposure.snow_ice);
    consider(GameplayHazard::Hail, exposure.hail);
    consider(GameplayHazard::Heatwave, exposure.heatwave);
    return best;
}

inline const char* hazard_warning(GameplayHazard hazard, float severity) {
    const float level = weather_hazard_detail::unit(severity);
    if (hazard == GameplayHazard::None || level < 0.15f) return "No active hazard";
    if (level < 0.5f) return "Weather advisory";
    switch (hazard) {
        case GameplayHazard::Tornado: return "TORNADO: seek shelter now";
        case GameplayHazard::SnowIce: return "ICE: severe traction loss";
        case GameplayHazard::Flood: return "FLOOD: turn around";
        case GameplayHazard::Hail: return "HAIL: find cover";
        case GameplayHazard::Heatwave: return "HEAT: engine power reduced";
        case GameplayHazard::None: break;
    }
    return "Severe weather";
}

}  // namespace apricot
