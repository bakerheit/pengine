#pragma once

#include <algorithm>

#include "app/driving_mechanics.h"
#include "app/player_car_catalog.h"

namespace apricot {

// Driving style controls the broad game feel. This layer is the make/model
// identity, so a Pip stays light and eager while a Cityliner stays heavy and
// deliberate in every style. Values are style multipliers except fixed mass.
struct PlayerCarPerformanceProfile {
    float mass_kg = 1210.f;
    float engine_torque_scale = 1.f;
    float top_speed_scale = 1.f;
    float brake_scale = 1.f;
    float steer_lock_scale = 1.f;
    float steer_rate_scale = 1.f;
    float steer_speed_falloff_scale = 1.f;
    float lateral_grip_scale = 1.f;
    float suspension_scale = 1.f;
    float anti_roll_scale = 1.f;
    float roll_inertia_scale = 1.f;
    float com_height_offset = 0.f;
    float drag_scale = 1.f;
    float rolling_resistance_scale = 1.f;
    float front_drive_bias_offset = 0.f;
    float wheel_inertia_scale = 1.f;
    // ATTENUATION ONLY, DESPITE THE NAME. apply_vehicle_impact clamps this to
    // [0, 1] before it scales the dent, so 1.f is already the maximum and any
    // value above it is silently discarded. FangVenom shipped 1.18 here and it
    // did nothing at all — identical dents to 1.f, verified — while reading in
    // the table as if the bike bruised 18% more easily. Keep every row in
    // (0, 1]; vehicle_model_tuning_tests enforces it.
    float body_damage_gain = 1.f;
};

// Every real PlayerCarId has an intentional row. The neutral fallback only
// exists for the kCount sentinel and must never service a catalog car.
inline constexpr PlayerCarPerformanceProfile player_car_performance_profile(
    PlayerCarId car) {
    car = canonical_player_car_id(car);
    switch (car) {
        case PlayerCarId::AlderPip: // light city hatch
            return {1050.f,.68f,.72f,.94f,1.12f,1.10f,1.12f,.94f,.90f,
                    .88f,.92f,.035f,1.04f,.92f,.28f,.82f,1.f};
        case PlayerCarId::AlderRidge: // short-wheelbase SUV
            return {1650.f,.98f,.78f,.96f,1.07f,.92f,1.12f,.90f,1.02f,
                    .82f,.88f,.065f,1.16f,1.22f,.18f,1.18f,1.f};
        case PlayerCarId::AlderWayfarer: // long family wagon
            return {1560.f,.91f,.84f,1.f,.96f,.94f,1.06f,1.f,.96f,
                    .96f,1.08f,.025f,1.10f,1.08f,.12f,1.10f,1.f};
        case PlayerCarId::GlmLunge: // flagship mid-engine wedge
            return {1420.f,1.38f,1.15f,1.12f,1.04f,1.10f,.88f,1.16f,1.16f,
                    1.22f,1.18f,-.018f,.90f,.96f,-.14f,.96f,1.f};
        case PlayerCarId::GlmZip: // compact targa sports car
            return {1180.f,1.15f,1.08f,1.08f,1.08f,1.13f,.91f,1.10f,1.10f,
                    1.14f,1.08f,-.012f,.94f,.94f,-.10f,.88f,1.f};
        case PlayerCarId::HalcyonSix: // pre-war family sedan
            return {1720.f,.76f,.58f,.78f,1.02f,.78f,1.16f,.78f,.78f,
                    .68f,.80f,.080f,1.30f,1.28f,-.04f,1.28f,1.f};
        case PlayerCarId::HalcyonSovereign: // long limousine
            return {2750.f,1.60f,.70f,.96f,.79f,.72f,1.18f,.88f,.94f,
                    .80f,1.28f,.050f,1.24f,1.22f,-.05f,1.40f,1.f};
        case PlayerCarId::HarrowCityliner: // full-size coach
            return {9000.f,4.f,.38f,.90f,.68f,.56f,1.25f,.82f,.92f,
                    .82f,1.48f,.110f,1.52f,1.48f,.16f,4.f,.25f};
        case PlayerCarId::HarrowHauler: // tractor cab
            return {6500.f,4.40f,.42f,.94f,.72f,.62f,1.22f,.86f,.98f,
                    .88f,1.38f,.090f,1.44f,1.46f,.08f,3.f,1.f};
        case PlayerCarId::HarrowParcel: // commercial panel van
            return {2200.f,1.34f,.72f,.98f,.91f,.82f,1.15f,.88f,.94f,
                    .84f,.92f,.075f,1.28f,1.30f,.10f,1.42f,1.f};
        case PlayerCarId::RodeoGrazer: // compact four-wheel-drive pickup
            return {1580.f,1.18f,.82f,1.01f,.94f,.91f,1.06f,.95f,.95f,
                    .94f,1.10f,.025f,1.15f,1.18f,.0f,1.23f,1.f};
        case PlayerCarId::HarrowWorkman: // torquey utility pickup
            return {1980.f,1.28f,.78f,.96f,.96f,.88f,1.08f,.91f,.96f,
                    .90f,.94f,.060f,1.20f,1.24f,-.12f,1.32f,1.f};
        case PlayerCarId::LegacyCar5: // established all-rounder
            return {1210.f,1.f,1.f,1.f,1.f,1.f,1.f,1.f,1.f,
                    1.f,1.f,0.f,1.f,1.f,0.f,1.f,1.f};
        case PlayerCarId::LegacyCar5Next: // refreshed all-rounder, same shell
            return {1185.f,1.04f,1.02f,1.02f,1.01f,1.03f,.98f,1.02f,1.02f,
                    1.03f,1.01f,-.004f,.98f,.99f,-.02f,.98f,1.f};
        case PlayerCarId::LegacyCar5NextPolice: // patrol tune of the same shell
            return {1310.f,1.00f,1.06f,1.08f,1.00f,1.05f,.95f,1.06f,1.06f,
                    1.08f,1.06f,-.006f,.96f,.97f,-.03f,1.04f,1.f};
        case PlayerCarId::LegacyCar8: // larger old sedan
            return {1480.f,1.10f,.84f,.95f,.96f,.90f,1.06f,.95f,.92f,
                    .88f,1.02f,.035f,1.12f,1.12f,-.07f,1.16f,1.f};
        // Car 8's shell carrying a stretcher, cabinets and crew: heavier off
        // the same running gear, so more torque to move it and less of
        // everything that mass takes away. Stiffer springs for the load, but
        // the load sits high, so the centre of mass rises and it rolls more.
        case PlayerCarId::LegacyCar8Ambulance: // Car 8 shell, loaded ambulance
            return {1900.f,1.22f,.80f,.93f,.96f,.88f,1.08f,.92f,.96f,
                    .90f,1.10f,.055f,1.12f,1.20f,-.07f,1.28f,1.f};
        case PlayerCarId::MontroseRegentEight: // 1930s formal luxury car
            return {2150.f,.84f,.52f,.72f,.94f,.70f,1.20f,.72f,.72f,
                    .62f,.76f,.100f,1.38f,1.34f,-.06f,1.48f,1.f};
        case PlayerCarId::MunicipalAmbulance: // emergency van
            return {3200.f,1.90f,.76f,1.08f,.88f,.82f,1.14f,.90f,.98f,
                    .92f,1.05f,.080f,1.30f,1.34f,.12f,1.65f,1.f};
        case PlayerCarId::MunicipalFiretruck: // heavy fire apparatus
            return {7500.f,3.80f,.40f,1.02f,.70f,.58f,1.24f,.84f,.94f,
                    .86f,1.42f,.105f,1.50f,1.52f,.14f,3.40f,1.f};
        // THE 1991 CRUISERS ARE BIG SEDANS, NOT SPORTS CARS. Before
        // 2026-09-13 every one of them out-ran all but a handful of the
        // roster (91-E topped out at 94 m/s, second only to the Ember GT), and
        // the free-driving pursuit steps 91-C. They sit now where a V8 police
        // sedan of the period sat: a top speed that beats most traffic but
        // stays at or under the fastest 30% of civilian cars, and a launch no
        // harder than the Car 5 all-rounder. Top speed is the redline in top
        // gear, so it is set by top_speed_scale and the wheel radius; torque
        // then puts the launch back where each variant's character wants it.
        // police_performance_tests measures both against the whole roster in
        // every driving style that can launch flat out (MUSCLE cannot yet).
        case PlayerCarId::MunicipalCruiser91A: // square, heavy fleet sedan
            return {1825.f,1.15f,.73f,1.13f,.99f,1.04f,.93f,1.08f,1.08f,
                    1.12f,1.18f,.002f,1.02f,1.04f,-.05f,1.10f,1.f};
        case PlayerCarId::MunicipalCruiser91B: // cleaner transitional aero shell
            return {1745.f,1.29f,.97f,1.15f,1.02f,1.09f,.90f,1.12f,1.10f,
                    1.16f,1.16f,-.006f,.95f,.99f,-.04f,1.05f,1.f};
        case PlayerCarId::MunicipalCruiser91C: // state pursuit tune
            return {1885.f,1.40f,.96f,1.18f,.98f,1.06f,.88f,1.14f,1.12f,
                    1.18f,1.22f,-.008f,.94f,.98f,-.06f,1.08f,1.f};
        case PlayerCarId::MunicipalCruiser91D: // shorter urban response tune
            return {1665.f,1.20f,.80f,1.17f,1.05f,1.13f,.92f,1.13f,1.08f,
                    1.15f,1.12f,-.004f,1.f,1.f,-.02f,1.02f,1.f};
        case PlayerCarId::MunicipalCruiser91E: // planted highway interceptor
            return {1855.f,1.31f,.83f,1.20f,.97f,1.05f,.86f,1.16f,1.13f,
                    1.20f,1.24f,-.010f,.92f,.97f,-.07f,1.07f,1.f};
        case PlayerCarId::OrisonCinderGt: // compact 1990s front-engine GT
            return {1340.f,1.25f,1.10f,1.10f,1.02f,1.10f,.93f,1.10f,1.12f,
                    1.15f,1.12f,-.018f,.92f,.96f,-.11f,.94f,1.f};
        case PlayerCarId::SpagattiShu: // low grand-touring exotic
            return {1460.f,1.52f,1.18f,1.16f,1.03f,1.12f,.86f,1.18f,1.16f,
                    1.20f,1.18f,-.020f,.88f,.94f,-.14f,.94f,1.f};
        case PlayerCarId::EmberGt: // wide, low mid-engine sports coupe
            return {1490.f,1.56f,1.21f,1.20f,.95f,1.13f,.84f,1.22f,1.21f,
                    1.27f,1.20f,-.024f,.86f,.94f,-.15f,.95f,1.f};
        case PlayerCarId::VesperMistral: // front-engine sports coupe
            return {1380.f,1.22f,1.08f,1.09f,1.04f,1.08f,.91f,1.10f,1.10f,
                    1.14f,1.12f,-.012f,.94f,.96f,-.10f,.98f,1.f};
        case PlayerCarId::VesperScythe: // light flagship exotic
            return {1320.f,1.45f,1.20f,1.18f,1.06f,1.16f,.84f,1.20f,1.20f,
                    1.28f,1.22f,-.022f,.86f,.92f,-.15f,.90f,1.f};
        case PlayerCarId::VesperVx91: // large grand tourer
            return {1560.f,1.34f,1.14f,1.12f,.99f,1.02f,.90f,1.12f,1.12f,
                    1.16f,1.16f,-.014f,.90f,.98f,-.11f,1.08f,1.f};
        case PlayerCarId::FangVenom: // light, quick-steering 1991 sportbike
            // Trailing 1.f is body_damage_gain: the bike used to declare 1.18,
            // which the [0,1] clamp in apply_vehicle_impact threw away. The
            // dents were always the 1.f ones; this is the honest value, not a
            // change of feel.
            return {310.f,.58f,1.16f,.90f,1.18f,1.18f,.84f,1.08f,.76f,
                    .62f,.42f,.105f,.72f,.52f,-.62f,.35f,1.f};
        case PlayerCarId::LegacyCruiser91CSlot:
        case PlayerCarId::kCount:
            break;
    }
    return {};
}

inline VehicleTuning player_model_tuning(DrivingMechanicsStyle style,
                                         PlayerCarId car) {
    VehicleTuning tuning = player_vehicle_tuning(style);
    const auto profile = player_car_performance_profile(car);
    const float mass_ratio = profile.mass_kg / tuning.mass_kg;

    tuning.mass_kg = profile.mass_kg;
    tuning.spring_k *= mass_ratio * profile.suspension_scale;
    tuning.damper_c *= mass_ratio * profile.suspension_scale;
    tuning.bumpstop_k *= mass_ratio * profile.suspension_scale;
    tuning.anti_roll_front *= mass_ratio * profile.anti_roll_scale;
    tuning.anti_roll_rear *= mass_ratio * profile.anti_roll_scale;
    tuning.brake_torque *= mass_ratio * profile.brake_scale;
    tuning.handbrake_torque *= mass_ratio * profile.brake_scale;
    tuning.service_brake_grip_boost *= profile.brake_scale;
    tuning.engine_peak_torque *= profile.engine_torque_scale;
    tuning.final_drive /= profile.top_speed_scale;
    tuning.max_steer *= profile.steer_lock_scale;
    tuning.steer_rate *= profile.steer_rate_scale;
    tuning.steer_return_rate *= profile.steer_rate_scale;
    tuning.steer_speed_falloff *= profile.steer_speed_falloff_scale;
    tuning.lateral_grip_scale *= profile.lateral_grip_scale;
    tuning.roll_inertia_scale *= profile.roll_inertia_scale;
    tuning.com_height_above_mount = std::max(
        .015f, tuning.com_height_above_mount + profile.com_height_offset);
    tuning.drag *= profile.drag_scale;
    tuning.rolling_resistance *= profile.rolling_resistance_scale;
    tuning.front_drive_bias = std::clamp(
        tuning.front_drive_bias + profile.front_drive_bias_offset, .05f, .85f);
    tuning.wheel_inertia *= profile.wheel_inertia_scale;
    tuning.body_damage_gain = profile.body_damage_gain;

    const auto& definition = player_car_definition(car);
    if (definition.physical_half_wheelbase > 0.f) {
        tuning.half_wheelbase = definition.physical_half_wheelbase;
        tuning.half_track = definition.physical_half_track;
        tuning.wheel_radius = definition.physical_wheel_radius;
    }

    if (is_motorbike(car)) {
        // The simulation retains a narrow four-contact arcade chassis for
        // stability; the renderer collapses it to the bike's two centre wheels.
        tuning.max_steer=std::min(tuning.max_steer,.70f);
        tuning.chassis_half_width=.25f;
        tuning.chassis_half_length=.83f;
        tuning.car_collision_half_width=.42f;
        tuning.car_collision_half_length=1.13f;
        tuning.chassis_floor=-.11f-static_suspension_length(tuning)-
                             tuning.com_height_above_mount;
        tuning.chassis_roof=1.16f-definition.arch_centre_y-
                            static_suspension_length(tuning)-
                            tuning.com_height_above_mount;
    } else if (is_municipal_cruiser_91(car)) {
        tuning.max_steer=std::min(tuning.max_steer,.64f);
        tuning.chassis_half_width=.96f;
        tuning.chassis_half_length=2.38f;
        tuning.car_collision_half_width=1.08f;
        tuning.car_collision_half_length=2.75f;
        tuning.chassis_floor=.18f-definition.arch_centre_y-
                             static_suspension_length(tuning)-
                             tuning.com_height_above_mount;
        // The lightbar can break away visually; it does not make the main
        // collision box as tall as the roof equipment.
        tuning.chassis_roof=1.58f-definition.arch_centre_y-
                            static_suspension_length(tuning)-
                            tuning.com_height_above_mount;
    } else if (car == PlayerCarId::AlderPip) {
        tuning.chassis_half_width=.82f;
        tuning.chassis_half_length=1.75f;
        tuning.car_collision_half_width=.86f;
        tuning.car_collision_half_length=1.9f;
        tuning.chassis_floor=-.15f-static_suspension_length(tuning)-
                             tuning.com_height_above_mount;
        tuning.chassis_roof=1.52f-definition.arch_centre_y-
                            static_suspension_length(tuning)-
                            tuning.com_height_above_mount;
    } else if (car == PlayerCarId::OrisonCinderGt) {
        // Ackermann makes the inside wheel turn further than this central
        // angle. Keep that wheel within the asset's tested .82-radian sweep.
        tuning.max_steer=std::min(tuning.max_steer,.67f);
        tuning.chassis_half_width=.90f;
        tuning.chassis_half_length=2.16f;
        tuning.car_collision_half_width=.98f;
        // Source axles are centred at +.03 m. After the body fit, the rear
        // exhaust tips reach +2.325 m from the physical chassis centre.
        tuning.car_collision_half_length=2.325f;
        tuning.chassis_floor=.15f-definition.arch_centre_y-
                             static_suspension_length(tuning)-
                             tuning.com_height_above_mount;
        tuning.chassis_roof=1.245f-definition.arch_centre_y-
                             static_suspension_length(tuning)-
                             tuning.com_height_above_mount;
    } else if (car == PlayerCarId::RodeoGrazer) {
        tuning.front_drive_bias=.50f;
        tuning.suspension_travel=std::max(tuning.suspension_travel,.20f);
        tuning.max_steer=std::min(tuning.max_steer,.52f);
        tuning.chassis_half_width=.90f;tuning.chassis_half_length=2.40f;
        tuning.car_collision_half_width=1.16f;tuning.car_collision_half_length=2.62f;
        tuning.chassis_floor=.31f-definition.arch_centre_y-static_suspension_length(tuning)-tuning.com_height_above_mount;
        tuning.chassis_roof=1.78f-definition.arch_centre_y-static_suspension_length(tuning)-tuning.com_height_above_mount;
    } else if (car == PlayerCarId::EmberGt) {
        tuning.max_steer=std::min(tuning.max_steer,.48f);
        tuning.chassis_half_width=.92f;
        tuning.chassis_half_length=2.15f;
        tuning.car_collision_half_width=1.14f;
        tuning.car_collision_half_length=2.49f;
        tuning.chassis_floor=.16f-definition.arch_centre_y-
                             static_suspension_length(tuning)-tuning.com_height_above_mount;
        tuning.chassis_roof=1.247f-definition.arch_centre_y-
                            static_suspension_length(tuning)-tuning.com_height_above_mount;
    } else if (car == PlayerCarId::SpagattiShu) {
        tuning.max_steer=std::min(tuning.max_steer,.60f);
        tuning.chassis_half_width=.96f;
        tuning.chassis_half_length=2.30f;
        tuning.car_collision_half_width=1.09f;
        tuning.car_collision_half_length=2.43f;
        tuning.chassis_floor=-.18f-static_suspension_length(tuning)-
                             tuning.com_height_above_mount;
        tuning.chassis_roof=1.225f-definition.arch_centre_y-
                            static_suspension_length(tuning)-
                            tuning.com_height_above_mount;
    } else if (car == PlayerCarId::VesperScythe ||
               car == PlayerCarId::HalcyonSovereign) {
        const bool exotic=car==PlayerCarId::VesperScythe;
        if (!exotic) tuning.max_steer=std::min(tuning.max_steer,.60f);
        tuning.chassis_half_width=exotic?.98f:.99f;
        tuning.chassis_half_length=exotic?2.15f:3.80f;
        tuning.car_collision_half_width=exotic?1.02f:1.03f;
        tuning.car_collision_half_length=exotic?2.325f:4.10f;
        tuning.chassis_floor=-.18f-static_suspension_length(tuning)-
                             tuning.com_height_above_mount;
        tuning.chassis_roof=(exotic?1.19f:1.68f)-definition.arch_centre_y-
                            static_suspension_length(tuning)-
                            tuning.com_height_above_mount;
    } else if (car == PlayerCarId::HarrowHauler) {
        tuning.max_steer=std::min(tuning.max_steer,.55f);
        tuning.chassis_half_width=1.15f;
        tuning.chassis_half_length=3.1f;
        tuning.car_collision_half_width=1.25f;
        tuning.car_collision_half_length=3.3f;
        tuning.chassis_floor=-.25f;
        tuning.chassis_roof=3.25f-.50f-static_suspension_length(tuning)-
                            tuning.com_height_above_mount;
    } else if (car == PlayerCarId::HarrowCityliner) {
        tuning.max_steer=std::min(tuning.max_steer,.52f);
        tuning.chassis_half_width=1.22f;
        tuning.chassis_half_length=4.9f;
        tuning.car_collision_half_width=1.25f;
        tuning.car_collision_half_length=5.5f;
        tuning.chassis_floor=-.39f;
        tuning.chassis_roof=3.10f-definition.arch_centre_y-
                            static_suspension_length(tuning)-
                            tuning.com_height_above_mount;
    }
    return tuning;
}

}  // namespace apricot
