#include "game/traffic.h"
#include "game/traffic_internal.h"

#include "game/car_models.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/log.h"
#include "physics/world_collision.h"
#include "render/debug_draw.h"
#include "render/mesh.h"
#include "render/shader.h"
#include "scene/aabb.h"
#include "scene/frustum.h"
#include "scene/scene.h"
#include "scene/scene_node.h"
#include "world/city_layout.h"
#include "world/heightmap.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

// =============================================================================
// Tuning  (anon namespace; touch values here only)
// =============================================================================
namespace {

// Car body models, asset paths, geometry, and tuning all live in
// game/car_models.h / .cpp — that's the single source of truth for adding or
// editing a vehicle. This file just consumes the table.

// ---- Separate wheel mesh ---------------------------------------------------
namespace WheelAsset {
    constexpr const char* MESH_PATH    = ASSETS_DIR "/models/vehicles/wheel.emesh";
    constexpr const char* TEXTURE_PATH = ASSETS_DIR "/vehicles/Vehicles_psx/Wheel/wheel.png";
    constexpr float       VISIBLE_RADIUS = 0.275f;
}


// Police lifecycle limits — read by update() for despawn and target_police
// math. The corresponding POLICE_SPAWN_MIN / SPAWN_MAX / MODEL_ID / PAINT_IDX
// constants live in traffic_spawn.cpp (single-consumer).
constexpr int   POLICE_MAX_CARS     = 8;
constexpr float POLICE_DESPAWN_DIST = 280.f;

// Other anon-namespace tuning that moved during PBD-007:
//   STOP_BACK, LIGHT_PERIOD/HALF/YELLOW_TIME, is_ew, LightPhase, light_phase
//     → traffic_internal.h (multi-consumer: drive + visuals)
//   STUCK_SPEED, STUCK_GAP, SWERVE_RATE, ROUTE_REFRESH_AT,
//   LANE_HALF_WIDTH, CAR_LENGTH → traffic_drive.cpp (single-consumer)
//   ROUTE_LANES → traffic_internal.h (multi-consumer: spawn + drive)
//   OBBxz, make_obb_xz, obb_xz_intersect → traffic_collisions.cpp (single-consumer)
//   ApproachSpec, APPROACHES, light geometry / bulb tints, pack_ij
//     → traffic_visuals.cpp (single-consumer)

} // anonymous namespace

// =============================================================================
// Lifecycle
// =============================================================================

TrafficSystem::TrafficSystem()  = default;
TrafficSystem::~TrafficSystem() = default;

bool TrafficSystem::init(Scene& scene, const LightVisuals& lights,
                          RoadGraph& graph, int target_ai_count) {
    scene_     = &scene;
    light_vis_ = lights;
    graph_     = &graph;
    target_ai_count_ = target_ai_count;

    assets_ = std::make_unique<Assets>();

    // ---- Wheel mesh + texture (shared across all car models) --------------
    if (load_static_emesh(WheelAsset::MESH_PATH, assets_->wheel_mesh)
        && assets_->wheel_tex.load_file(WheelAsset::TEXTURE_PATH)) {
        glm::vec3 wmn = assets_->wheel_mesh.bounds_min();
        glm::vec3 wmx = assets_->wheel_mesh.bounds_max();
        float native_r = std::max(wmx.y - wmn.y, wmx.z - wmn.z) * 0.5f;
        assets_->wheel_visual_scale   = (native_r > 1e-4f)
            ? WheelAsset::VISIBLE_RADIUS / native_r : 1.f;
        assets_->wheel_visible_radius = WheelAsset::VISIBLE_RADIUS;
        assets_->wheel_ok = true;
    } else {
        PE_WARN("Wheel load failed; falling back to cube wheels");
    }

    // ---- Per-model body mesh + paints + derived geometry ------------------
    assets_->models.resize(static_cast<std::size_t>(NUM_CAR_MODELS));
    for (int m = 0; m < NUM_CAR_MODELS; ++m) {
        const CarModelDef& def = CAR_MODELS[m];
        auto&              ma  = assets_->models[static_cast<std::size_t>(m)];

        ma.body_ok = load_static_emesh(def.mesh_path, ma.body_mesh);
        ma.paints.resize(static_cast<std::size_t>(def.paint_count));
        for (int p = 0; ma.body_ok && p < def.paint_count; ++p)
            ma.body_ok = ma.paints[static_cast<std::size_t>(p)]
                            .load_file(def.paint_paths[p]);
        if (!ma.body_ok) {
            PE_WARN("Car model '%s' load failed; cars will not spawn",
                    def.internal_name);
            return false;
        }

        // Reference Vehicle pre-loaded with this model's tuning, so derived
        // values (ride height, static suspension compression) reflect the
        // mass/wheel/suspension this model actually uses.
        Vehicle ref;
        apply_model_tuning(ref, def);

        glm::vec3 mn = ma.body_mesh.bounds_min();
        glm::vec3 mx = ma.body_mesh.bounds_max();
        float native_length = std::max(0.001f, mx.z - mn.z);
        float target_length = ref.chassis_full_extents.z;
        float s             = target_length / native_length;
        float wheel_chassis_y = -ref.com_height_above_mount
                                 - ref.suspension_rest;
        ma.body_visual_scale  = glm::vec3{s};
        ma.body_visual_offset = {
            -(mn.x + mx.x) * 0.5f * s,
            wheel_chassis_y - def.arch_centre_y_native * s,
            -(mn.z + mx.z) * 0.5f * s,
        };
        ma.ride_height_at_rest = ref.com_height_above_mount
                               + ref.suspension_rest
                               + ref.wheel_radius;
        const float static_compress =
            (ref.chassis_mass * std::abs(ref.gravity))
                / (4.f * ref.spring_k);
        ma.static_visual_drop =
            std::max(0.f, ref.suspension_rest - static_compress);

        // Body-local AABB of the actual rendered mesh, computed by walking
        // the eight native-bounds corners through the visual node's full
        // transform (T * R * S). The rotation matters: when the native
        // mesh isn't symmetric around its own origin, applying yaw_offset
        // shifts where the rendered mesh actually sits relative to the
        // body. The previous (mn*s + offset, mx*s + offset) shortcut
        // assumed centering and gave a phantom collision box at the front
        // of trucks whose mesh wasn't bilaterally Z-centred.
        glm::mat4 visual_xform =
            glm::translate(glm::mat4{1.f}, ma.body_visual_offset) *
            glm::mat4_cast(glm::angleAxis(
                glm::radians(def.yaw_offset_deg), glm::vec3{0.f, 1.f, 0.f})) *
            glm::scale(glm::mat4{1.f}, ma.body_visual_scale);
        ma.visual_aabb_min = glm::vec3{ FLT_MAX,  FLT_MAX,  FLT_MAX};
        ma.visual_aabb_max = glm::vec3{-FLT_MAX, -FLT_MAX, -FLT_MAX};
        for (int corner = 0; corner < 8; ++corner) {
            glm::vec3 c{(corner & 1) ? mx.x : mn.x,
                        (corner & 2) ? mx.y : mn.y,
                        (corner & 4) ? mx.z : mn.z};
            glm::vec3 cw = glm::vec3(visual_xform * glm::vec4{c, 1.f});
            ma.visual_aabb_min = glm::min(ma.visual_aabb_min, cw);
            ma.visual_aabb_max = glm::max(ma.visual_aabb_max, cw);
        }

        // Wheel mount positions (chassis-local) from this model's mesh-native
        // wheel positions, scaled by the body-fit factor `s`. Mount Y matches
        // the body origin's offset above the wheel mounts (= the CoM height).
        const float mount_y  = -ref.com_height_above_mount;
        const float wheel_x  = def.wheel_x_native  * s;
        const float wheel_zf = def.wheel_zf_native * s;
        const float wheel_zr = def.wheel_zr_native * s;
        ma.wheel_mount[0] = {-wheel_x, mount_y, -wheel_zf}; // FL
        ma.wheel_mount[1] = {+wheel_x, mount_y, -wheel_zf}; // FR
        ma.wheel_mount[2] = {-wheel_x, mount_y, +wheel_zr}; // RL
        ma.wheel_mount[3] = {+wheel_x, mount_y, +wheel_zr}; // RR

        PE_INFO("Car model '%s' (%s %s): native=%.2fx%.2fx%.2f scale=%.3f "
                "ride=%.2f max=%.0f km/h mass=%.0f kg",
                def.internal_name, def.make, def.model,
                mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, s,
                ma.ride_height_at_rest, def.max_speed_kmh, def.chassis_mass);
    }

    return true;
}

void TrafficSystem::shutdown() {
    while (!cars_.empty()) destroy_car(cars_.size() - 1);
    auto remove = [&](SceneNode* n) {
        if (n && scene_) scene_->remove_node(n);
    };
    while (!lights_.empty()) {
        Light& L = lights_.back();
        remove(L.pole);   remove(L.arm);    remove(L.housing);
        remove(L.bulb_r); remove(L.bulb_y); remove(L.bulb_g);
        lights_.pop_back();
    }
    light_intersections_.clear();
    player_car_ = nullptr;
    scene_ = nullptr;
    graph_ = nullptr;
    assets_.reset();
}

// =============================================================================
// Car creation
// =============================================================================

// create_car_at_pose, spawn_player_car, destroy_car: extracted to traffic_spawn.cpp.

void TrafficSystem::set_police_response(int wanted_level,
                                         const glm::vec3& target_pos) {
    wanted_level_ = std::clamp(wanted_level, 0, 5);
    police_target_pos_ = target_pos;
}

// =============================================================================
// Player driver transfer (no teleport)
// =============================================================================

TrafficSystem::Car* TrafficSystem::find_nearest(const glm::vec3& point,
                                                 float radius) const {
    Car* best = nullptr;
    float best_d2 = radius * radius;
    for (const auto& cp : cars_) {
        Car& c = *cp;
        glm::vec3 p = c.vehicle.position();
        float dx = p.x - point.x;
        float dz = p.z - point.z;
        float d2 = dx*dx + dz*dz;
        if (d2 < best_d2) {
            best_d2 = d2;
            best    = &c;
        }
    }
    return best;
}

bool TrafficSystem::set_player_driver(Car* target) {
    if (target == player_car_) return false;
    // Demote the previous player car (if any) to Parked. It keeps its
    // current pose / velocity — no teleport.
    if (player_car_) {
        player_car_->driver = Driver::Parked;
        player_car_->vehicle.set_inputs(0.f, 0.f, 0.f, /*handbrake=*/ true);
    }
    if (!target) {
        player_car_ = nullptr;
        return true;
    }
    if (target->driver == Driver::AI)
        target->ai_state = TrafficAgentState::PhysicsFallback;
    target->driver = Driver::Player;
    target->police_unit = false;
    target->vehicle.set_inputs(0.f, 0.f, 0.f, /*handbrake=*/ false);
    // The player taking over cancels any pending auto-recovery on this car.
    target->ai_recovery_pending = false;
    target->ai_recovery_timer   = 0.f;
    player_car_ = target;
    return true;
}

void TrafficSystem::set_player_inputs(float throttle, float brake, float steer,
                                       bool handbrake) {
    if (!player_car_) return;
    player_car_->vehicle.set_inputs(throttle, brake, steer, handbrake);
}


// =============================================================================
// Per-frame entry point
// =============================================================================

void TrafficSystem::update(float dt, double time_seconds,
                            const glm::vec3& camera_pos,
                            const WorldCollision& world) {
    if (!graph_) return;

    // Despawn AI cars whose intersection is no longer loaded, or that are
    // far from the camera. Police cars are removed once the wanted level
    // clears or the chase has moved far away. Player and Parked cars are
    // NEVER despawned — they stay where the player left them.
    for (std::size_t k = cars_.size(); k-- > 0;) {
        Car& c = *cars_[k];
        glm::vec3 p = c.vehicle.position();
        float dx = p.x - camera_pos.x, dz = p.z - camera_pos.z;
        if (c.driver == Driver::AI) {
            bool unloaded = !ai_route_valid(c);
            bool too_far = (dx*dx + dz*dz) > (despawn_dist_ * despawn_dist_);
            if (unloaded || too_far) destroy_car(k);
        } else if (c.driver == Driver::Police ||
                   (c.driver == Driver::Parked && c.police_unit && wanted_level_ <= 0)) {
            bool chase_over = wanted_level_ <= 0;
            bool too_far = (dx*dx + dz*dz)
                         > (POLICE_DESPAWN_DIST * POLICE_DESPAWN_DIST);
            if (chase_over || too_far) destroy_car(k);
        }
    }

    // Spawn AI cars to fill toward the target count.
    int ai_count = 0;
    int police_count = 0;
    for (const auto& cp : cars_) {
        if (cp->driver == Driver::AI) ++ai_count;
        else if (cp->driver == Driver::Police ||
                 (cp->driver == Driver::Parked && cp->police_unit)) ++police_count;
    }
    int budget = 2;
    while (ai_count < target_ai_count_ && budget-- > 0) {
        if (!try_spawn_ai(camera_pos)) break;
        ++ai_count;
    }
    int target_police = wanted_level_ <= 0
                      ? 0
                      : std::min(POLICE_MAX_CARS, wanted_level_ + 1);
    if (police_count < target_police) {
        police_spawn_timer_ -= dt;
        if (police_spawn_timer_ <= 0.f) {
            if (try_spawn_police()) ++police_count;
            police_spawn_timer_ = wanted_level_ >= 2 ? 0.8f : 1.4f;
        }
    } else {
        police_spawn_timer_ = 0.f;
    }

    // Per-driver update. sync_visuals is deferred until after collision
    // resolution so that the rendered pose reflects the post-resolve state
    // (otherwise overlapping cars would render mid-collision for one frame).
    for (auto& cp : cars_) {
        Car& c = *cp;
        // Parked cars flagged after a hit get a chance to re-attach to the
        // AI script before this frame's physics runs.
        try_ai_recover(c, dt);
        switch (c.driver) {
            case Driver::AI:
                update_ai_kinematic(c, dt, time_seconds);
                break;
            case Driver::Police:
                update_police_dynamic(c, dt);
                integrate_player_or_parked(c, dt, world);
                break;
            case Driver::Player:
            case Driver::Parked:
                integrate_player_or_parked(c, dt, world);
                break;
        }
    }

    resolve_vehicle_collisions();

    for (auto& cp : cars_) sync_visuals(*cp);

    sync_lights_to_loaded();
    update_light_visuals(time_seconds);
}


} // namespace pengine
