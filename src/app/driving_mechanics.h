#pragma once

#include <cstddef>
#include <cstdint>

#include "physics/vehicle.h"

namespace apricot {

// Player-only handling presets exposed through the F1 development menu. These
// change tuning, never VehicleState, so switching live preserves the car's
// position, velocity, gear, health and damage.
enum class DrivingMechanicsStyle : uint8_t {
    Balanced = 0,
    Arcade,
    Rally,
    Heavy,
    Sport,
    Muscle,
    Offroad,
    Drift,
    ClassicGta,
    Count,
};

inline constexpr std::size_t kDrivingMechanicsStyleCount =
    static_cast<std::size_t>(DrivingMechanicsStyle::Count);

inline const char* driving_mechanics_name(DrivingMechanicsStyle style) {
    switch (style) {
        case DrivingMechanicsStyle::Balanced: return "BALANCED";
        case DrivingMechanicsStyle::Arcade:   return "ARCADE";
        case DrivingMechanicsStyle::Rally:    return "RALLY";
        case DrivingMechanicsStyle::Heavy:    return "HEAVY";
        case DrivingMechanicsStyle::Sport:    return "SPORT";
        case DrivingMechanicsStyle::Muscle:   return "MUSCLE";
        case DrivingMechanicsStyle::Offroad:  return "OFFROAD";
        case DrivingMechanicsStyle::Drift:    return "DRIFT";
        case DrivingMechanicsStyle::ClassicGta: return "CLASSIC GTA";
        case DrivingMechanicsStyle::Count:    break;
    }
    return "BALANCED";
}

inline const char* driving_mechanics_menu_label(DrivingMechanicsStyle style) {
    switch (style) {
        case DrivingMechanicsStyle::Balanced:
            return "BALANCED - NEUTRAL MIX";
        case DrivingMechanicsStyle::Arcade:
            return "ARCADE - SHARP AND PLANTED";
        case DrivingMechanicsStyle::Rally:
            return "RALLY - LOOSE AND ROTATABLE";
        case DrivingMechanicsStyle::Heavy:
            return "HEAVY - WEIGHTY AND DELIBERATE";
        case DrivingMechanicsStyle::Sport:
            return "SPORT - PRECISE AND FAST";
        case DrivingMechanicsStyle::Muscle:
            return "MUSCLE - TORQUE AND OVERSTEER";
        case DrivingMechanicsStyle::Offroad:
            return "OFFROAD - SOFT AND FORGIVING";
        case DrivingMechanicsStyle::Drift:
            return "DRIFT - BIG LOCK AND LONG SLIDES";
        case DrivingMechanicsStyle::ClassicGta:
            return "CLASSIC GTA - DEFAULT VC / SA FEEL";
        case DrivingMechanicsStyle::Count:
            break;
    }
    return "";
}

inline bool valid_driving_mechanics_style(int index) {
    return index >= 0 &&
           index < static_cast<int>(kDrivingMechanicsStyleCount);
}

// Build a complete player tuning instead of patching the current profile.
// That makes switching A -> B -> A exact and prevents values from one style
// leaking into the next. Geometry and collision dimensions stay common across
// every profile so a live switch cannot resize the visible or solid car.
inline VehicleTuning player_vehicle_tuning(DrivingMechanicsStyle style) {
    VehicleTuning tuning;
    tuning.arcade_reverse = true;

    switch (style) {
        case DrivingMechanicsStyle::Balanced:
            // VehicleTuning's defaults are the current agreed baseline.
            break;

        case DrivingMechanicsStyle::Arcade:
            tuning.mass_kg = 1125.0f;
            tuning.max_steer = 0.73f;
            tuning.steer_rate = 7.0f;
            tuning.steer_return_rate = 8.0f;
            tuning.steer_input_exponent = 1.15f;
            tuning.steer_speed_falloff = 0.027f;
            tuning.engine_peak_torque = 455.0f;
            tuning.front_drive_bias = 0.55f;
            tuning.differential_coupling = 55.0f;
            tuning.brake_torque = 20000.0f;
            tuning.service_brake_grip_boost = 2.6f;
            tuning.service_brake_lateral_damping = 180.0f;
            tuning.handbrake_grip_scale = 0.76f;
            tuning.tyre_peak_slip_ratio = 0.15f;
            tuning.tyre_tail_grip = 0.82f;
            tuning.tyre_falloff = 0.40f;
            tuning.lateral_grip_scale = 1.55f;
            tuning.spring_k = 58000.0f;
            tuning.damper_c = 7400.0f;
            tuning.anti_roll_front = 15000.0f;
            tuning.anti_roll_rear = 7000.0f;
            tuning.roll_inertia_scale = 2.4f;
            tuning.com_height_above_mount = 0.03f;
            tuning.tyre_force_height = 0.42f;
            break;

        case DrivingMechanicsStyle::Rally:
            tuning.mass_kg = 1200.0f;
            tuning.max_steer = 0.67f;
            tuning.steer_rate = 5.8f;
            tuning.steer_return_rate = 7.2f;
            tuning.steer_input_exponent = 1.20f;
            tuning.steer_speed_falloff = 0.047f;
            tuning.engine_peak_torque = 445.0f;
            tuning.front_drive_bias = 0.40f;
            tuning.differential_coupling = 62.0f;
            tuning.brake_torque = 16500.0f;
            tuning.service_brake_grip_boost = 2.4f;
            tuning.service_brake_lateral_damping = 65.0f;
            tuning.handbrake_torque = 2800.0f;
            tuning.handbrake_grip_scale = 0.50f;
            tuning.tyre_peak_slip_ratio = 0.10f;
            tuning.tyre_tail_grip = 0.54f;
            tuning.tyre_falloff = 0.90f;
            tuning.suspension_rest = 0.27f;
            tuning.suspension_travel = 0.20f;
            tuning.spring_k = 45000.0f;
            tuning.damper_c = 5600.0f;
            tuning.anti_roll_front = 9000.0f;
            tuning.anti_roll_rear = 7500.0f;
            tuning.roll_inertia_scale = 1.6f;
            tuning.com_height_above_mount = 0.09f;
            tuning.tyre_force_height = 0.62f;
            break;

        case DrivingMechanicsStyle::Heavy:
            tuning.mass_kg = 1650.0f;
            tuning.max_steer = 0.58f;
            tuning.steer_rate = 3.4f;
            tuning.steer_return_rate = 4.8f;
            tuning.steer_input_exponent = 1.45f;
            tuning.steer_speed_falloff = 0.060f;
            tuning.engine_peak_torque = 470.0f;
            tuning.engine_inertia = 0.30f;
            tuning.front_drive_bias = 0.50f;
            tuning.differential_coupling = 32.0f;
            tuning.brake_torque = 19500.0f;
            tuning.service_brake_grip_boost = 2.2f;
            tuning.service_brake_lateral_damping = 75.0f;
            tuning.handbrake_grip_scale = 0.68f;
            tuning.tyre_peak_slip_ratio = 0.12f;
            tuning.tyre_tail_grip = 0.72f;
            tuning.tyre_falloff = 0.58f;
            tuning.suspension_rest = 0.26f;
            tuning.suspension_travel = 0.18f;
            tuning.spring_k = 58000.0f;
            tuning.damper_c = 7600.0f;
            tuning.anti_roll_front = 8500.0f;
            tuning.anti_roll_rear = 2800.0f;
            tuning.roll_inertia_scale = 1.45f;
            tuning.com_height_above_mount = 0.12f;
            tuning.tyre_force_height = 0.70f;
            tuning.drag = 0.48f;
            tuning.rolling_resistance = 0.022f;
            break;

        case DrivingMechanicsStyle::Sport:
            tuning.mass_kg = 1050.0f;
            tuning.max_steer = 0.70f;
            tuning.steer_rate = 6.8f;
            tuning.steer_return_rate = 8.2f;
            tuning.steer_input_exponent = 1.22f;
            tuning.steer_speed_falloff = 0.029f;
            tuning.engine_peak_torque = 485.0f;
            tuning.engine_inertia = 0.17f;
            tuning.front_drive_bias = 0.25f;
            tuning.differential_coupling = 58.0f;
            tuning.brake_torque = 21000.0f;
            tuning.service_brake_grip_boost = 3.1f;
            tuning.service_brake_lateral_damping = 140.0f;
            tuning.handbrake_grip_scale = 0.66f;
            tuning.tyre_peak_slip_ratio = 0.13f;
            tuning.tyre_tail_grip = 0.76f;
            tuning.tyre_falloff = 0.50f;
            tuning.lateral_grip_scale = 1.62f;
            tuning.suspension_rest = 0.22f;
            tuning.suspension_travel = 0.13f;
            tuning.spring_k = 65000.0f;
            tuning.damper_c = 8200.0f;
            tuning.anti_roll_front = 16500.0f;
            tuning.anti_roll_rear = 9000.0f;
            tuning.roll_inertia_scale = 2.2f;
            tuning.yaw_inertia_scale = 0.88f;
            tuning.com_height_above_mount = 0.025f;
            tuning.tyre_force_height = 0.40f;
            tuning.drag = 0.38f;
            break;

        case DrivingMechanicsStyle::Muscle:
            tuning.mass_kg = 1500.0f;
            tuning.max_steer = 0.60f;
            tuning.steer_rate = 4.1f;
            tuning.steer_return_rate = 5.4f;
            tuning.steer_input_exponent = 1.30f;
            tuning.steer_speed_falloff = 0.052f;
            tuning.engine_peak_torque = 560.0f;
            tuning.engine_peak_rpm = 3600.0f;
            tuning.engine_redline_rpm = 6400.0f;
            tuning.engine_inertia = 0.34f;
            // Strongly rear-biased rather than mathematically pure RWD. In
            // this four-contact model, 620 N.m through only the rear pair sat
            // beyond the tyre peak and produced a permanent burnout instead
            // of a muscle-car launch. A little front pull makes it usable
            // while the rear still owns rotation under power.
            tuning.front_drive_bias = 0.12f;
            tuning.differential_coupling = 55.0f;
            tuning.brake_torque = 19000.0f;
            tuning.service_brake_grip_boost = 2.5f;
            tuning.service_brake_lateral_damping = 85.0f;
            tuning.handbrake_torque = 2500.0f;
            tuning.handbrake_grip_scale = 0.58f;
            tuning.tyre_peak_slip_ratio = 0.11f;
            tuning.tyre_tail_grip = 0.66f;
            tuning.tyre_falloff = 0.68f;
            tuning.suspension_rest = 0.27f;
            tuning.suspension_travel = 0.18f;
            tuning.spring_k = 50000.0f;
            tuning.damper_c = 6500.0f;
            tuning.anti_roll_front = 9500.0f;
            tuning.anti_roll_rear = 6000.0f;
            tuning.roll_inertia_scale = 1.55f;
            tuning.yaw_inertia_scale = 0.92f;
            tuning.com_height_above_mount = 0.10f;
            tuning.tyre_force_height = 0.64f;
            break;

        case DrivingMechanicsStyle::Offroad:
            tuning.mass_kg = 1400.0f;
            tuning.max_steer = 0.65f;
            tuning.steer_rate = 4.8f;
            tuning.steer_return_rate = 6.0f;
            tuning.steer_input_exponent = 1.25f;
            tuning.steer_speed_falloff = 0.050f;
            tuning.engine_peak_torque = 500.0f;
            tuning.front_drive_bias = 0.50f;
            tuning.differential_coupling = 72.0f;
            tuning.brake_torque = 18500.0f;
            tuning.service_brake_grip_boost = 3.4f;
            tuning.service_brake_lateral_damping = 105.0f;
            tuning.handbrake_grip_scale = 0.70f;
            tuning.tyre_peak_slip = 1.30f;
            tuning.tyre_peak_slip_ratio = 0.16f;
            tuning.tyre_tail_grip = 0.78f;
            tuning.tyre_falloff = 0.46f;
            tuning.suspension_rest = 0.31f;
            tuning.suspension_travel = 0.25f;
            tuning.spring_k = 39000.0f;
            tuning.damper_c = 5300.0f;
            tuning.bumpstop_k = 240000.0f;
            tuning.anti_roll_front = 6500.0f;
            tuning.anti_roll_rear = 3200.0f;
            tuning.roll_inertia_scale = 1.75f;
            tuning.com_height_above_mount = 0.14f;
            tuning.tyre_force_height = 0.68f;
            tuning.rolling_resistance = 0.024f;
            tuning.grounded_roll_damping = 6.0f;
            break;

        case DrivingMechanicsStyle::Drift:
            tuning.mass_kg = 1250.0f;
            tuning.max_steer = 0.82f;
            tuning.steer_rate = 7.0f;
            tuning.steer_return_rate = 7.5f;
            tuning.steer_input_exponent = 1.08f;
            tuning.steer_speed_falloff = 0.034f;
            tuning.engine_peak_torque = 480.0f;
            tuning.engine_inertia = 0.19f;
            // Ten percent front pull keeps a slide translating. Pure RWD in
            // this tyre model can hold both rear contacts past the grip peak
            // indefinitely, which looks like a stationary burnout rather than
            // a drift once the steering and handbrake are involved.
            tuning.front_drive_bias = 0.10f;
            tuning.differential_coupling = 90.0f;
            tuning.brake_torque = 16000.0f;
            tuning.service_brake_grip_boost = 1.8f;
            tuning.service_brake_lateral_damping = 25.0f;
            tuning.handbrake_torque = 3400.0f;
            tuning.handbrake_grip_scale = 0.36f;
            tuning.tyre_peak_slip_ratio = 0.11f;
            tuning.tyre_tail_grip = 0.52f;
            tuning.tyre_falloff = 0.90f;
            tuning.spring_k = 50000.0f;
            tuning.damper_c = 6200.0f;
            tuning.anti_roll_front = 8000.0f;
            tuning.anti_roll_rear = 10500.0f;
            tuning.roll_inertia_scale = 1.65f;
            tuning.yaw_inertia_scale = 0.78f;
            tuning.com_height_above_mount = 0.08f;
            tuning.tyre_force_height = 0.60f;
            break;

        case DrivingMechanicsStyle::ClassicGta:
            // Early-2000s crime-game feel: steering bites immediately, the
            // body visibly takes a set, power can rotate the rear, and a
            // handbrake tap makes a clean city-corner pivot. It stays much
            // easier to catch than the dedicated Drift profile.
            tuning.mass_kg = 1210.0f;
            tuning.max_steer = 0.76f;
            tuning.steer_rate = 7.4f;
            tuning.steer_return_rate = 8.4f;
            tuning.steer_input_exponent = 1.08f;
            tuning.steer_speed_falloff = 0.025f;
            tuning.engine_peak_torque = 465.0f;
            tuning.engine_inertia = 0.18f;
            tuning.front_drive_bias = 0.20f;
            tuning.differential_coupling = 68.0f;
            tuning.brake_torque = 20500.0f;
            tuning.service_brake_grip_boost = 2.6f;
            tuning.service_brake_steer_scale = 0.40f;
            tuning.service_brake_steer_start_speed = 18.0f;
            tuning.service_brake_steer_full_speed = 34.0f;
            tuning.service_brake_lateral_damping = 105.0f;
            tuning.handbrake_torque = 3000.0f;
            tuning.handbrake_grip_scale = 0.45f;
            tuning.tyre_peak_slip_ratio = 0.12f;
            tuning.tyre_tail_grip = 0.75f;
            tuning.tyre_falloff = 0.50f;
            tuning.lateral_grip_scale = 1.75f;
            tuning.suspension_rest = 0.26f;
            tuning.suspension_travel = 0.18f;
            tuning.spring_k = 47500.0f;
            tuning.damper_c = 6000.0f;
            tuning.anti_roll_front = 8500.0f;
            tuning.anti_roll_rear = 6800.0f;
            tuning.pitch_inertia_scale = 2.0f;
            tuning.roll_inertia_scale = 1.50f;
            tuning.yaw_inertia_scale = 0.80f;
            tuning.com_height_above_mount = 0.03f;
            tuning.tyre_force_height = 0.40f;
            tuning.grounded_roll_damping = 12.0f;
            tuning.grounded_roll_damping_threshold = 0.35f;
            tuning.grounded_roll_rate_limit = 1.75f;
            tuning.drag = 0.40f;
            break;

        case DrivingMechanicsStyle::Count:
            break;
    }

    return tuning;
}

}  // namespace apricot
