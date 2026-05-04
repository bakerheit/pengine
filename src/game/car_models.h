#pragma once

namespace pengine {

class Vehicle;

// Definition of one car model — every variable that distinguishes one car
// from another in the game lives here. The full table is in car_models.cpp;
// edit there to tune existing cars or to add a new one (just append a row).
//
// Once a row is added, traffic.cpp picks it up automatically: AI traffic
// samples by `spawn_weight`, and the engine loads the per-model assets at
// init() time.
struct CarModelDef {
    // ---- Identity ---------------------------------------------------------
    const char* internal_name;   // short id used for asset paths and logging
    const char* make;            // brand / manufacturer (display only)
    const char* model;           // model name           (display only)

    // ---- Asset paths (resolved against ASSETS_DIR at build time) ----------
    const char*        mesh_path;     // .emesh body without wheels
    const char* const* paint_paths;   // array of body-paint texture paths
    int                paint_count;   // number of entries in paint_paths

    // ---- Mesh-native geometry --------------------------------------------
    // Most authored OBJ meshes have +Z forward. The engine uses -Z forward;
    // yaw_offset_deg flips into the engine's convention. arch_centre_y_native
    // is the body's wheel-arch midline in mesh-Y, used to pin the body inside
    // the wheels. wheel_x/zf/zr_native are mesh-space wheel mount positions
    // (positive distances; symmetry/sign is applied in code).
    float yaw_offset_deg;
    float arch_centre_y_native;
    float wheel_x_native;
    float wheel_zf_native;
    float wheel_zr_native;

    // ---- Physics tuning ---------------------------------------------------
    float chassis_mass;     // kg
    float max_speed_kmh;    // forward top speed (km/h)
    float max_reverse_kmh;  // reverse top speed (km/h)
    float engine_force;     // total drive force at 0 m/s (N)
    float reverse_force;    // total reverse force at 0 m/s (N)
    float brake_force;      // total brake force (N)
    float linear_drag;      // 1/s air drag
    float lateral_grip;     // per-wheel lateral velocity-kill rate (higher → grips harder → more roll → tips easier)
    float chassis_height;   // chassis_full_extents.y (m) — raises CoM above wheel mounts; taller = more top-heavy = tips easier
    float spring_k;         // suspension spring stiffness (N/m)
    float damper_k;         // suspension damping (N·s/m)

    // ---- AI traffic mix ---------------------------------------------------
    // Relative weight in the AI traffic mix: models with higher weight spawn
    // proportionally more often. ~10 for a normal sedan, lower for trucks /
    // emergency vehicles / rare variants.
    int   spawn_weight;

    // ---- Body-mesh quirks -------------------------------------------------
    // Some authored OBJs have wheels baked into the body mesh. When true,
    // the engine's dynamic wheel scene-nodes are skipped — only the body's
    // built-in wheels render (no spin/steer animation, but no double-wheel
    // visual either). Prefer to author a wheel-less body and leave this
    // false; the strip_wheel_components.awk tool can split wheels off.
    bool  body_has_built_in_wheels;
};

// The full table. Iterate as `for (int i = 0; i < NUM_CAR_MODELS; ++i)`.
extern const CarModelDef CAR_MODELS[];
extern const int         NUM_CAR_MODELS;

// Stamp the model's tuning onto a Vehicle instance (mass, speed, forces,
// drag, lateral grip, chassis height, and per-model suspension).
void apply_model_tuning(Vehicle& v, const CarModelDef& def);

}  // namespace pengine
