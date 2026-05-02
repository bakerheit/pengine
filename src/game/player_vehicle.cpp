#include "game/player_vehicle.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "physics/world_collision.h"
#include "scene/aabb.h"
#include "scene/scene.h"
#include "scene/scene_node.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

// =============================================================================
// Player vehicle tuning
// =============================================================================
//
// All knobs for the player car's *appearance* and *handling overrides* live
// here. Edit a value and rebuild — there is no other location to touch.
//
// Asset notes:
//   * The car5 body mesh has its baked wheel-tire geometry stripped (see
//     Car5_nowheels.obj); wheels render from a separate Wheel.obj mesh so
//     each wheel can spin / steer / track suspension independently.
//   * Paint variants are loaded as separate textures; AI traffic cars pick
//     one at random per spawn.
//
// Coordinate conventions:
//   * Engine "forward" is local -Z (matches Vehicle::forward()).
//   * The car5 mesh was authored with +Z forward, so it's rotated 180°
//     around Y in chassis-local space (Car5::YAW_OFFSET_DEG).
//   * Mesh native units are metres; the global scale s = chassis_length /
//     model_length is applied uniformly.
namespace {

namespace Car5 {
    // Asset paths.
    constexpr const char* MESH_PATH      = ASSETS_DIR "/models/vehicles/car5.emesh";
    constexpr const char* PAINT_PATHS[3] = {
        ASSETS_DIR "/Vehicles_psx/Car 05/car5.png",       // default
        ASSETS_DIR "/Vehicles_psx/Car 05/car5_green.png",
        ASSETS_DIR "/Vehicles_psx/Car 05/car5_grey.png",
    };

    // Yaw rotation that flips the model's +Z forward to chassis -Z forward.
    constexpr float YAW_OFFSET_DEG = 180.f;

    // Body's wheel-arch midline in mesh-y (native units). At init the body
    // is shifted vertically so this y-line coincides with the physics
    // wheel-centre line at suspension rest. Lowering this value raises the
    // body relative to the wheels.
    constexpr float ARCH_CENTRE_Y_NATIVE = 0.45f;

    // Wheel anchor positions in native units (one quadrant, mirrored).
    constexpr float WHEEL_X_NATIVE  = 1.038f;
    constexpr float WHEEL_ZF_NATIVE = 2.254f;  // model-space +Z = front
    constexpr float WHEEL_ZR_NATIVE = 1.813f;  // model-space -Z = rear
} // Car5

namespace WheelAsset {
    constexpr const char* MESH_PATH    = ASSETS_DIR "/models/vehicles/wheel.emesh";
    constexpr const char* TEXTURE_PATH = ASSETS_DIR "/Vehicles_psx/Wheel/wheel.png";

    // Visible radius after scaling. Smaller than Vehicle::wheel_radius so it
    // fits inside the body's wheel-arch interior even under hard accel/brake
    // compression; update_visuals aligns the visible bottom to contact.
    constexpr float VISIBLE_RADIUS = 0.275f;
} // WheelAsset

namespace SuspensionOverride {
    // Stiffer than the Vehicle defaults: less pitch under accel/brake (so
    // wheel tops don't poke through arch tops) and less roll on tight turns.
    constexpr float SPRING_K = 130000.f;  // ≈ 2.2× the Vehicle default
    constexpr float DAMPER_K =   8000.f;  // ≈ 2.1× the Vehicle default
} // SuspensionOverride

} // anonymous namespace

// =============================================================================
// PlayerVehicle
// =============================================================================

bool PlayerVehicle::init(Scene& scene, const Mesh& cube_mesh,
                          const Texture& checker_tex) {
    scene_       = &scene;
    cube_mesh_   = &cube_mesh;
    checker_tex_ = &checker_tex;
    return load_assets_();
}

void PlayerVehicle::spawn(const glm::vec3& pos, float yaw_deg) {
    // -------- 1. Suspension overrides + rigid-body spawn --------------------
    vehicle_.spring_k = SuspensionOverride::SPRING_K;
    vehicle_.damper_k = SuspensionOverride::DAMPER_K;
    vehicle_.spawn(pos, yaw_deg);

    // -------- 2. Wheel raycast positions ------------------------------------
    //
    // Place each suspension raycast where the model's actual wheel sits.
    // Native anchor positions are mirrored across x; the 180° Y rotation
    // that aligns the model's +Z forward with chassis -Z forward also flips
    // z signs (model front → chassis -z, etc.).
    if (body_loaded_) {
        const float s        = body_visual_scale_.x;
        const float wheel_x  = Car5::WHEEL_X_NATIVE  * s;
        const float wheel_zf = Car5::WHEEL_ZF_NATIVE * s; // chassis-local -Z
        const float wheel_zr = Car5::WHEEL_ZR_NATIVE * s; // chassis-local +Z
        const float mount_y  = -vehicle_.chassis_full_extents.y * 0.5f;
        vehicle_.set_wheel_mount(0, {-wheel_x, mount_y, -wheel_zf}); // FL
        vehicle_.set_wheel_mount(1, {+wheel_x, mount_y, -wheel_zf}); // FR
        vehicle_.set_wheel_mount(2, {-wheel_x, mount_y, +wheel_zr}); // RL
        vehicle_.set_wheel_mount(3, {+wheel_x, mount_y, +wheel_zr}); // RR
    }

    // -------- 3. Scene nodes ------------------------------------------------
    create_scene_nodes_();
}

void PlayerVehicle::set_inputs(float throttle, float brake, float steer,
                                bool handbrake) {
    vehicle_.set_inputs(throttle, brake, steer, handbrake);
}

void PlayerVehicle::substep(float dt, const WorldCollision& world) {
    vehicle_.substep(dt, world);
}

void PlayerVehicle::update_visuals(float dt) {
    if (!chassis_node_) return;

    // -------- 1. Rolling-spin accumulator ------------------------------------
    //
    // Forward velocity / wheel_radius gives angular speed; integrate over dt.
    // Signed so the wheels reverse direction when reversing.
    float v_signed = glm::dot(vehicle_.forward(), vehicle_.body().linear_vel);
    wheel_spin_rad_ += static_cast<double>(v_signed) * dt
                     / static_cast<double>(vehicle_.wheel_radius);

    // -------- 2. Chassis world pose -----------------------------------------
    chassis_node_->transform.position = vehicle_.position();
    chassis_node_->transform.rotation = vehicle_.orientation();
    chassis_node_->transform.scale    = {1.f, 1.f, 1.f};
    chassis_node_->mark_dirty();

    // -------- 3. Body visual ------------------------------------------------
    if (body_loaded_) {
        chassis_visual_node_->transform.position = body_visual_offset_;
        chassis_visual_node_->transform.rotation = glm::angleAxis(
            glm::radians(Car5::YAW_OFFSET_DEG), glm::vec3{0.f, 1.f, 0.f});
        chassis_visual_node_->transform.scale    = body_visual_scale_;
    } else {
        chassis_visual_node_->transform.position = {0.f, 0.f, 0.f};
        chassis_visual_node_->transform.rotation = glm::quat{1.f, 0.f, 0.f, 0.f};
        chassis_visual_node_->transform.scale    = vehicle_.chassis_full_extents;
    }
    chassis_visual_node_->mark_dirty();

    // -------- 4. Wheels -----------------------------------------------------
    //
    // Each wheel sits at its physics mount drop (suspension extension).
    // Visible wheel may be smaller than the physics wheel; offset the centre
    // so the visible bottom = contact point regardless. Steering yaw is
    // applied around chassis Y; rolling spin is applied around the wheel's
    // local +X axle in the wheel's own frame (steer * spin order means spin
    // happens first in mesh space, then steering rotates it).
    const auto& wheels       = vehicle_.wheels();
    const float spin         = static_cast<float>(wheel_spin_rad_);
    const glm::vec3 wheel_scale = wheel_loaded_
        ? glm::vec3{wheel_visual_scale_}
        : glm::vec3{0.32f, 2.f * vehicle_.wheel_radius,
                          2.f * vehicle_.wheel_radius};
    const float bottom_align = wheel_loaded_
        ? (vehicle_.wheel_radius - wheel_visible_radius_) : 0.f;
    for (std::size_t i = 0; i < 4; ++i) {
        glm::vec3 pos = wheels[i].mount_local;
        pos.y -= wheels[i].visual_drop + bottom_align;
        wheel_nodes_[i]->transform.position = pos;

        glm::quat steer{1.f, 0.f, 0.f, 0.f};
        if (wheels[i].is_steering)
            steer = glm::angleAxis(-vehicle_.steer_rad(),
                                    glm::vec3{0.f, 1.f, 0.f});
        glm::quat roll = wheel_loaded_
            ? glm::angleAxis(-spin, glm::vec3{1.f, 0.f, 0.f})
            : glm::quat{1.f, 0.f, 0.f, 0.f};
        wheel_nodes_[i]->transform.rotation = steer * roll;
        wheel_nodes_[i]->transform.scale    = wheel_scale;
        wheel_nodes_[i]->mark_dirty();
    }
}

float PlayerVehicle::body_yaw_offset_deg() const {
    return Car5::YAW_OFFSET_DEG;
}

const Texture* PlayerVehicle::paint(int idx) const {
    if (idx < 0 || idx >= 3) return nullptr;
    return &body_paints_[idx];
}

// =============================================================================
// Internal helpers
// =============================================================================

bool PlayerVehicle::load_assets_() {
    // -------- 1. Body mesh + paint variants ---------------------------------
    bool body_ok = load_static_emesh(Car5::MESH_PATH, body_mesh_);
    for (int i = 0; body_ok && i < 3; ++i)
        body_ok = body_paints_[i].load_file(Car5::PAINT_PATHS[i]);
    if (!body_ok) {
        PE_WARN("Car5 load failed; cars will fall back to cube visuals");
        return false;
    }
    body_loaded_ = true;

    // -------- 2. Body uniform scale + visual offset -------------------------
    //
    // Scale: model_length is reported by the mesh; uniform scale so the
    //        model length matches Vehicle::chassis_full_extents.z.
    //
    // Offset y: the body is shifted vertically so its wheel-arch midline
    //           (Car5::ARCH_CENTRE_Y_NATIVE in mesh units) coincides with
    //           the physics wheel-centre line at suspension rest. Lowering
    //           that constant lifts the body further above the wheels.
    glm::vec3 mn = body_mesh_.bounds_min();
    glm::vec3 mx = body_mesh_.bounds_max();
    float native_length   = std::max(0.001f, mx.z - mn.z);
    float target_length   = vehicle_.chassis_full_extents.z;
    float s               = target_length / native_length;
    float wheel_chassis_y = -vehicle_.chassis_full_extents.y * 0.5f
                          -  vehicle_.suspension_rest;
    body_visual_scale_  = glm::vec3{s};
    body_visual_offset_ = {
        -(mn.x + mx.x) * 0.5f * s,                               // centre x
        wheel_chassis_y - Car5::ARCH_CENTRE_Y_NATIVE * s,        // arch ↔ wheel
        -(mn.z + mx.z) * 0.5f * s,                               // centre z
    };
    PE_INFO("Car5 model: native=%.2fx%.2fx%.2f scale=%.3f",
            mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, s);

    // -------- 3. Wheel mesh + texture ---------------------------------------
    //
    // Native wheel radius is taken from the larger of the y/z extents (axle
    // is along x). Scale to WheelAsset::VISIBLE_RADIUS (smaller than the
    // physics wheel_radius so the wheel fits the arch interior).
    if (load_static_emesh(WheelAsset::MESH_PATH, wheel_mesh_)
        && wheel_tex_.load_file(WheelAsset::TEXTURE_PATH)) {
        glm::vec3 wmn = wheel_mesh_.bounds_min();
        glm::vec3 wmx = wheel_mesh_.bounds_max();
        float native_r = std::max(wmx.y - wmn.y, wmx.z - wmn.z) * 0.5f;
        wheel_visual_scale_   = (native_r > 1e-4f)
            ? WheelAsset::VISIBLE_RADIUS / native_r : 1.f;
        wheel_visible_radius_ = WheelAsset::VISIBLE_RADIUS;
        wheel_loaded_         = true;
    } else {
        PE_WARN("Wheel load failed; falling back to cube wheels");
    }

    return true;
}

void PlayerVehicle::create_scene_nodes_() {
    // chassis_node_         carries the rigid-body world pose (pos + rot).
    // chassis_visual_node_  child holding the model's local offset, the
    //                       180° yaw fix-up, and the global scale.
    // wheel_nodes_[0..3]    siblings of the visual under chassis_node_;
    //                       update_visuals() pushes per-wheel suspension /
    //                       steering / rolling state into them each frame.
    chassis_node_        = scene_->create_node();
    chassis_visual_node_ = scene_->create_node(chassis_node_);

    if (body_loaded_) {
        AABB local;
        local.min = body_mesh_.bounds_min();
        local.max = body_mesh_.bounds_max();
        chassis_visual_node_->renderable = Renderable{
            &body_mesh_, local, glm::vec3{1.f, 1.f, 1.f},
            glm::vec2{1.f, 1.f}, &body_paints_[0]};
    } else {
        AABB cube_aabb;
        cube_aabb.min = cube_mesh_->bounds_min();
        cube_aabb.max = cube_mesh_->bounds_max();
        chassis_visual_node_->renderable = Renderable{
            cube_mesh_, cube_aabb, glm::vec3{0.85f, 0.20f, 0.18f},
            glm::vec2{1.f, 1.f}, checker_tex_};
    }

    AABB wheel_aabb;
    wheel_aabb.min = wheel_loaded_ ? wheel_mesh_.bounds_min() : -glm::vec3{0.5f};
    wheel_aabb.max = wheel_loaded_ ? wheel_mesh_.bounds_max() :  glm::vec3{0.5f};
    const Mesh*    wm    = wheel_loaded_ ? &wheel_mesh_ : cube_mesh_;
    const Texture* wt    = wheel_loaded_ ? &wheel_tex_  : checker_tex_;
    const glm::vec3 wtint = wheel_loaded_ ? glm::vec3{1.f}
                                           : glm::vec3{0.10f, 0.10f, 0.10f};
    for (int i = 0; i < 4; ++i) {
        wheel_nodes_[i] = scene_->create_node(chassis_node_);
        wheel_nodes_[i]->renderable = Renderable{
            wm, wheel_aabb, wtint, glm::vec2{1.f, 1.f}, wt};
    }
}

} // namespace pengine
