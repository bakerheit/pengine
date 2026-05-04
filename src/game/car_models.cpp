// car_models.cpp — single source of truth for every car in the game.
//
// To add a new car: drop the body OBJ + paint PNGs under
// assets/Vehicles_psx/Car NN/, run meshconv to produce car<NN>.emesh, then
// append a new row to CAR_MODELS[] below. No other code changes needed.

#include "game/car_models.h"

#include "game/vehicle.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

// =============================================================================
// Paint palettes — one array per model, listed in the order shown in-game.
// =============================================================================

namespace {

constexpr const char* CAR5_PAINTS[] = {
    ASSETS_DIR "/Vehicles_psx/Car 05/car5.png",
    ASSETS_DIR "/Vehicles_psx/Car 05/car5_green.png",
    ASSETS_DIR "/Vehicles_psx/Car 05/car5_grey.png",
};

constexpr const char* CAR8_PAINTS[] = {
    ASSETS_DIR "/Vehicles_psx/Car 08/Car8.png",
    ASSETS_DIR "/Vehicles_psx/Car 08/Car8_grey.png",
    ASSETS_DIR "/Vehicles_psx/Car 08/Car8_purple.png",
    ASSETS_DIR "/Vehicles_psx/Car 08/Car8_mail.png",
};

}  // namespace

// =============================================================================
// CAR_MODELS — every car the game knows about.
// Add new entries below, copying an existing block as a template.
// =============================================================================

const CarModelDef CAR_MODELS[] = {

    // ===== Car 5 =============================================================
    // A normal sedan. Most common in AI traffic.
    {
        /*internal_name*/ "Car5",
        /*make*/          "Generic",
        /*model*/         "Sedan",

        /*mesh_path*/     ASSETS_DIR "/models/vehicles/car5.emesh",
        /*paint_paths*/   CAR5_PAINTS,
        /*paint_count*/   3,

        /*yaw_offset_deg*/        180.f,
        /*arch_centre_y_native*/  0.45f,
        /*wheel_x_native*/        1.038f,
        /*wheel_zf_native*/       2.254f,
        /*wheel_zr_native*/       1.813f,

        /*chassis_mass*/    1500.f,
        /*max_speed_kmh*/     100.f,
        /*max_reverse_kmh*/   50.f,
        /*engine_force*/   16000.f,
        /*reverse_force*/  13000.f,
        /*brake_force*/    36000.f,
        /*linear_drag*/        0.50f,
        /*lateral_grip*/      10.f,   // slides before it rolls — hard to tip
        /*chassis_height*/     0.8f,
        /*com_height_above_mount*/ 0.05f, // CoM ~0.80 m above ground → a_tip ≈ 12.7 m/s² (well above μg)
        /*spring_k*/       130000.f,
        /*damper_k*/        13000.f,

        /*spawn_weight*/   10,
        /*body_has_built_in_wheels*/ false,
    },

    // ===== Car 8 =============================================================
    // Heavier delivery truck. Slower top speed.
    // car8.emesh built from Car8_nowheels.obj (Car8.obj with the 4 wheel
    // components stripped). Wheel mounts shifted ~0.15m rearward from the
    // raw wheel-component centroids so the dynamic wheels sit where the eye
    // expects on the body.
    {
        /*internal_name*/ "Car8",
        /*make*/          "Generic",
        /*model*/         "Box Truck",

        /*mesh_path*/     ASSETS_DIR "/models/vehicles/car8.emesh",
        /*paint_paths*/   CAR8_PAINTS,
        /*paint_count*/   4,

        /*yaw_offset_deg*/        180.f,
        /*arch_centre_y_native*/  0.525f,
        /*wheel_x_native*/        1.1f,
        /*wheel_zf_native*/       1.65f,
        /*wheel_zr_native*/       2.25f,

        /*chassis_mass*/    1800.f,
        /*max_speed_kmh*/     60.f,
        /*max_reverse_kmh*/   16.f,
        /*engine_force*/   18000.f,
        /*reverse_force*/  14000.f,
        /*brake_force*/    30000.f,
        /*linear_drag*/        0.55f,
        /*lateral_grip*/      20.f,   // high grip + tall CoM — tips easily
        /*chassis_height*/     2.0f,  // tall box truck — visual / collision box
        /*com_height_above_mount*/ 0.6f, // CoM ~1.30 m above ground → a_tip ≈ 8.3 m/s² (just under μg, tips in hard corners and on bumps)
        /*spring_k*/        45000.f,  // soft suspension → more body roll
        /*damper_k*/         5000.f,

        /*spawn_weight*/    4,
        /*body_has_built_in_wheels*/ false,
    },

    // ===== (template — copy this block for a new car, fill in the values) ===
    // {
    //     /*internal_name*/ "CarN",
    //     /*make*/          "...",
    //     /*model*/         "...",
    //
    //     /*mesh_path*/     ASSETS_DIR "/models/vehicles/carN.emesh",
    //     /*paint_paths*/   CARN_PAINTS,
    //     /*paint_count*/   <n>,
    //
    //     /*yaw_offset_deg*/        180.f,
    //     /*arch_centre_y_native*/  ...,
    //     /*wheel_x_native*/        ...,
    //     /*wheel_zf_native*/       ...,
    //     /*wheel_zr_native*/       ...,
    //
    //     /*chassis_mass*/    ...,
    //     /*max_speed_kmh*/   ...,
    //     /*max_reverse_kmh*/ ...,
    //     /*engine_force*/    ...,
    //     /*reverse_force*/   ...,
    //     /*brake_force*/     ...,
    //     /*linear_drag*/     ...,
    //     /*lateral_grip*/    ...,
    //     /*chassis_height*/  ...,
    //     /*spring_k*/        ...,
    //     /*damper_k*/        ...,
    //
    //     /*spawn_weight*/    ...,
    //     /*body_has_built_in_wheels*/ false,
    // },
};

const int NUM_CAR_MODELS = sizeof(CAR_MODELS) / sizeof(CAR_MODELS[0]);

void apply_model_tuning(Vehicle& v, const CarModelDef& def) {
    v.chassis_mass  = def.chassis_mass;
    v.max_speed     = def.max_speed_kmh   / 3.6f;
    v.max_reverse   = def.max_reverse_kmh / 3.6f;
    v.engine_force  = def.engine_force;
    v.reverse_force = def.reverse_force;
    v.brake_force   = def.brake_force;
    v.linear_drag            = def.linear_drag;
    v.lateral_grip           = def.lateral_grip;
    v.chassis_full_extents.y    = def.chassis_height;
    v.com_height_above_mount    = def.com_height_above_mount;
    v.spring_k                  = def.spring_k;
    v.damper_k                  = def.damper_k;
}

}  // namespace pengine
