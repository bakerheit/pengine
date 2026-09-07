#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "game/weather_hazards.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

void tornado_force_is_deterministic_and_falls_off() {
    TornadoParams params;
    params.center_xz = {10.0f, -4.0f};
    params.core_radius_m = 10.0f;
    params.outer_radius_m = 110.0f;
    params.max_horizontal_force_n = 20000.0f;
    params.max_lift_force_n = 8000.0f;

    const TornadoForce core = tornado_force_at({15.0f, -4.0f}, params);
    const TornadoForce middle = tornado_force_at({70.0f, -4.0f}, params);
    const TornadoForce edge = tornado_force_at({120.0f, -4.0f}, params);
    const TornadoForce outside = tornado_force_at({120.01f, -4.0f}, params);
    const TornadoForce repeat = tornado_force_at({70.0f, -4.0f}, params);

    REQUIRE(core.exposure == 1.0f);
    REQUIRE(middle.exposure > 0.0f && middle.exposure < 1.0f);
    REQUIRE(edge.exposure == 0.0f);
    REQUIRE(outside.exposure == 0.0f);
    REQUIRE(middle.exposure == repeat.exposure);
    REQUIRE(middle.force_n == repeat.force_n);
    REQUIRE_NEAR(std::sqrt(core.force_n.x * core.force_n.x +
                           core.force_n.z * core.force_n.z),
                 20000.0, 0.01);
    REQUIRE_NEAR(core.force_n.y, 8000.0, 0.01);
    REQUIRE(std::fabs(core.force_n.z) > std::fabs(core.force_n.x));
    apricot_test::pass("tornado force is pure, rotational, and smoothly bounded");
}

void tornado_degenerate_inputs_stay_finite() {
    TornadoParams point;
    point.core_radius_m = -4.0f;
    point.outer_radius_m = -1.0f;
    point.max_horizontal_force_n = std::numeric_limits<float>::infinity();
    point.max_lift_force_n = std::numeric_limits<float>::quiet_NaN();
    point.inward_fraction = std::numeric_limits<float>::quiet_NaN();
    const TornadoForce center = tornado_force_at({0.0f, 0.0f}, point);
    REQUIRE(finite(center.force_n));
    REQUIRE(center.exposure == 1.0f);
    REQUIRE(center.force_n == glm::vec3(0.0f));

    point.center_xz.x = std::numeric_limits<float>::quiet_NaN();
    const TornadoForce invalid = tornado_force_at({0.0f, 0.0f}, point);
    REQUIRE(invalid.exposure == 0.0f);
    REQUIRE(finite(invalid.force_n));
    apricot_test::pass("degenerate tornado fields return finite bounded values");
}

void flood_uses_elevation_depth_and_event_intensity() {
    FloodParams params;
    params.water_elevation_m = 8.0f;
    params.max_event_rise_m = 2.0f;
    params.max_depth_m = 4.0f;
    params.full_severity_depth_m = 1.0f;

    const FloodSample dry = flood_at(10.5f, 1.0f, params);
    const FloodSample shallow = flood_at(9.75f, 1.0f, params);
    const FloodSample deep = flood_at(8.0f, 1.0f, params);
    const FloodSample weaker = flood_at(8.0f, 0.5f, params);
    REQUIRE(dry.depth_m == 0.0f && dry.severity == 0.0f);
    REQUIRE_NEAR(shallow.depth_m, 0.25, 1e-6);
    REQUIRE(deep.depth_m > shallow.depth_m);
    REQUIRE(deep.severity > shallow.severity);
    REQUIRE(deep.drag > shallow.drag);
    REQUIRE(weaker.surface_elevation_m < deep.surface_elevation_m);
    REQUIRE(weaker.severity < deep.severity);
    apricot_test::pass("flood depth and drag follow terrain and event strength");
}

void flood_degenerate_inputs_are_safe() {
    FloodParams broken;
    broken.water_elevation_m = 5.0f;
    broken.max_event_rise_m = std::numeric_limits<float>::infinity();
    broken.max_depth_m = -2.0f;
    broken.full_severity_depth_m = 0.0f;
    const FloodSample capped = flood_at(-100000.0f, 4.0f, broken);
    REQUIRE(std::isfinite(capped.surface_elevation_m));
    REQUIRE(capped.depth_m == 0.0f);
    REQUIRE(capped.severity >= 0.0f && capped.severity <= 1.0f);
    REQUIRE(capped.drag >= 0.0f && capped.drag <= 1.0f);

    const FloodSample invalid = flood_at(
        std::numeric_limits<float>::quiet_NaN(), 1.0f, broken);
    REQUIRE(invalid.depth_m == 0.0f && invalid.severity == 0.0f);
    apricot_test::pass("flood sampling clamps malformed terrain and event data");
}

void hazards_adjust_only_owned_vehicle_tuning() {
    VehicleTuning base;
    HazardExposure severe;
    severe.snow_ice = 1.0f;
    severe.flood = 1.0f;
    severe.hail = 1.0f;
    severe.heatwave = 1.0f;
    const HazardVehicleAdjustments effects = hazard_vehicle_adjustments(severe);
    const VehicleTuning tuned = apply_weather_hazards(base, severe);

    REQUIRE(effects.grip_multiplier >= 0.18f && effects.grip_multiplier <= 1.0f);
    REQUIRE(tuned.grip_scale < base.grip_scale);
    REQUIRE(tuned.lateral_grip_scale < base.lateral_grip_scale);
    REQUIRE(tuned.rolling_resistance > base.rolling_resistance);
    REQUIRE(tuned.drag > base.drag);
    REQUIRE(tuned.engine_peak_torque < base.engine_peak_torque);
    REQUIRE(tuned.brake_torque == base.brake_torque);
    REQUIRE(tuned.handbrake_torque == base.handbrake_torque);
    REQUIRE(tuned.mass_kg == base.mass_kg);

    const VehicleTuning clear = apply_weather_hazards(base, {});
    REQUIRE(clear.grip_scale == base.grip_scale);
    REQUIRE(clear.rolling_resistance == base.rolling_resistance);
    REQUIRE(clear.engine_peak_torque == base.engine_peak_torque);
    apricot_test::pass("hazards compose into bounded vehicle tuning multipliers");
}

void malformed_exposure_and_tuning_stay_bounded() {
    HazardExposure bad;
    bad.snow_ice = std::numeric_limits<float>::quiet_NaN();
    bad.flood = std::numeric_limits<float>::infinity();
    bad.hail = -100.0f;
    bad.heatwave = 100.0f;
    VehicleTuning base;
    base.grip_scale = std::numeric_limits<float>::infinity();
    base.lateral_grip_scale = std::numeric_limits<float>::quiet_NaN();
    base.rolling_resistance = -1.0f;
    base.drag = std::numeric_limits<float>::max();
    base.engine_peak_torque = std::numeric_limits<float>::quiet_NaN();
    const VehicleTuning tuned = apply_weather_hazards(base, bad);
    REQUIRE(std::isfinite(tuned.grip_scale) && tuned.grip_scale <= 4.0f);
    REQUIRE(std::isfinite(tuned.lateral_grip_scale) &&
            tuned.lateral_grip_scale <= 4.0f);
    REQUIRE(std::isfinite(tuned.rolling_resistance) &&
            tuned.rolling_resistance <= 1.0f);
    REQUIRE(std::isfinite(tuned.drag) && tuned.drag <= 100.0f);
    REQUIRE(std::isfinite(tuned.engine_peak_torque) &&
            tuned.engine_peak_torque <= 5000.0f);
    apricot_test::pass("malformed tuning inputs cannot escape gameplay bounds");
}

void labels_and_warnings_are_stable() {
    HazardExposure exposure;
    exposure.tornado = 0.8f;
    exposure.flood = 0.8f;
    exposure.heatwave = 1.0f;
    // Higher severity wins; exact ties keep the explicit threat priority.
    REQUIRE(dominant_hazard(exposure) == GameplayHazard::Heatwave);
    exposure.heatwave = 0.8f;
    REQUIRE(dominant_hazard(exposure) == GameplayHazard::Tornado);
    REQUIRE(std::strcmp(hazard_label(GameplayHazard::Flood), "Flash flood") == 0);
    REQUIRE(std::strcmp(hazard_warning(GameplayHazard::Flood, 1.0f),
                        "FLOOD: turn around") == 0);
    REQUIRE(std::strcmp(hazard_warning(GameplayHazard::Hail, 0.3f),
                        "Weather advisory") == 0);
    REQUIRE(std::strcmp(hazard_warning(GameplayHazard::Tornado,
                        std::numeric_limits<float>::quiet_NaN()),
                        "No active hazard") == 0);
    apricot_test::pass("dominant hazards expose deterministic HUD copy");
}

}  // namespace

int main() {
    std::printf("weather_hazards_tests\n");
    tornado_force_is_deterministic_and_falls_off();
    tornado_degenerate_inputs_stay_finite();
    flood_uses_elevation_depth_and_event_intensity();
    flood_degenerate_inputs_are_safe();
    hazards_adjust_only_owned_vehicle_tuning();
    malformed_exposure_and_tuning_stay_bounded();
    labels_and_warnings_are_stable();
    return apricot_test::done("weather_hazards_tests");
}
