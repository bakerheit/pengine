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


// STOP_BACK, LIGHT_PERIOD/HALF/YELLOW_TIME, is_ew, LightPhase, light_phase
// moved to traffic_internal.h (multi-consumer: drive + visuals).
// STUCK_SPEED, STUCK_GAP, SWERVE_RATE moved to traffic_drive.cpp
// (single-consumer).
// ---- Bulb tints + signal geometry ------------------------------------------
constexpr glm::vec3 RED_BRIGHT    {0.95f, 0.15f, 0.15f};
constexpr glm::vec3 YELLOW_BRIGHT {0.95f, 0.80f, 0.15f};
constexpr glm::vec3 GREEN_BRIGHT  {0.20f, 0.90f, 0.30f};
constexpr glm::vec3 RED_DIM       {0.30f, 0.10f, 0.10f};
constexpr glm::vec3 YELLOW_DIM    {0.32f, 0.28f, 0.10f};
constexpr glm::vec3 GREEN_DIM     {0.10f, 0.28f, 0.13f};
constexpr glm::vec3 METAL_TINT    {0.16f, 0.16f, 0.18f};

struct ApproachSpec {
    GridDir car_dir;
    float   corner_dx, corner_dz;
    float   arm_dx,    arm_dz;
};
constexpr ApproachSpec APPROACHES[4] = {
    {GridDir::East,  +1.f, -1.f,  0.f, +1.f},
    {GridDir::North, +1.f, +1.f, -1.f,  0.f},
    {GridDir::West,  -1.f, +1.f,  0.f, -1.f},
    {GridDir::South, -1.f, -1.f, +1.f,  0.f},
};
constexpr float POLE_HEIGHT     = 6.0f;
constexpr float POLE_THICK      = 0.20f;
constexpr float ARM_LENGTH      = 4.0f;
constexpr float ARM_THICK       = 0.18f;
constexpr float HOUSING_W       = 0.40f;
constexpr float HOUSING_H       = 1.50f;
constexpr float HOUSING_D       = 0.40f;
constexpr float BULB_SIZE       = 0.32f;
constexpr float BULB_SPACING    = 0.50f;
constexpr float CORNER_INSET    = 1.5f;
constexpr float CORNER_DIST     = STREET_WIDTH * 0.5f + CORNER_INSET;
// LANE_HALF_WIDTH, CAR_LENGTH, ROUTE_REFRESH_AT moved to traffic_drive.cpp.
// ROUTE_LANES moved to traffic_internal.h (multi-consumer: spawn + drive).
// POLICE_MODEL_ID / PAINT_IDX / SPAWN_MIN / SPAWN_MAX moved to
// traffic_spawn.cpp (single-consumer).
constexpr int   POLICE_MAX_CARS  = 8;
constexpr float POLICE_DESPAWN_DIST = 280.f;

inline std::uint64_t pack_ij(int i, int j) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(i)) << 32)
         |  static_cast<std::uint64_t>(static_cast<std::uint32_t>(j));
}

// =============================================================================
// 2D OBB intersection (XZ plane). Cars rotate primarily around Y, so a 2D OBB
// per car is accurate without paying for full 3D SAT. Used by car ↔ car
// collision resolution below.
// =============================================================================

struct OBBxz {
    glm::vec2 center;
    glm::vec2 half_ext;   // along body's local +X and +Z respectively
    glm::vec2 ax_x;       // unit, body +X projected to XZ
    glm::vec2 ax_z;       // unit, body +Z projected to XZ
};

OBBxz make_obb_xz(const Vehicle& v) {
    OBBxz o;
    glm::vec3 ax = v.right();        // body +X
    glm::vec3 az = -v.forward();     // body +Z (forward() is body -Z)
    // Footprint half-extents from the visual mesh's body-local AABB so
    // car-vs-car contact matches what the player sees, not the chassis box.
    // The AABB is generally NOT centred on the body origin (the rendered
    // mesh sits a bit forward/back of body+0 depending on how the OBJ
    // was authored), so the OBB centre is the world-space transform of
    // the AABB centre — using body.position would shift the rectangle
    // off the visible body and cause a phantom gap.
    glm::vec3 vmin   = v.visual_aabb_min_local();
    glm::vec3 vmax   = v.visual_aabb_max_local();
    glm::vec3 vctr   = (vmin + vmax) * 0.5f;
    glm::vec3 wctr   = v.position() + v.orientation() * vctr;
    o.center     = {wctr.x, wctr.z};
    o.half_ext   = {(vmax.x - vmin.x) * 0.5f,
                    (vmax.z - vmin.z) * 0.5f};
    glm::vec2 ax2{ax.x, ax.z};
    glm::vec2 az2{az.x, az.z};
    float lx = glm::length(ax2);
    float lz = glm::length(az2);
    o.ax_x = lx > 1e-6f ? ax2 / lx : glm::vec2{1.f, 0.f};
    o.ax_z = lz > 1e-6f ? az2 / lz : glm::vec2{0.f, 1.f};
    return o;
}

// SAT against the four candidate axes. Returns true on overlap, with
// `out_normal` pointing from b toward a and `out_depth` = penetration depth
// along that normal.
bool obb_xz_intersect(const OBBxz& a, const OBBxz& b,
                       glm::vec2& out_normal, float& out_depth) {
    const glm::vec2 axes[4] = {a.ax_x, a.ax_z, b.ax_x, b.ax_z};
    glm::vec2 d = a.center - b.center;

    float min_overlap = std::numeric_limits<float>::max();
    glm::vec2 min_axis{1.f, 0.f};

    for (int i = 0; i < 4; ++i) {
        glm::vec2 n = axes[i];
        float a_proj = std::abs(glm::dot(a.ax_x * a.half_ext.x, n))
                     + std::abs(glm::dot(a.ax_z * a.half_ext.y, n));
        float b_proj = std::abs(glm::dot(b.ax_x * b.half_ext.x, n))
                     + std::abs(glm::dot(b.ax_z * b.half_ext.y, n));
        float dist   = std::abs(glm::dot(d, n));
        float overlap = a_proj + b_proj - dist;
        if (overlap <= 0.f) return false;       // separating axis — done
        if (overlap < min_overlap) {
            min_overlap = overlap;
            min_axis    = n;
        }
    }
    if (glm::dot(d, min_axis) < 0.f) min_axis = -min_axis;
    out_normal = min_axis;
    out_depth  = min_overlap;
    return true;
}

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

void TrafficSystem::draw_wheels(Shader& shader, const Frustum& frustum) const {
    if (!assets_ || !assets_->wheel_ok || cars_.empty()) return;

    // Reuse storage across frames so we don't reallocate every render.
    static thread_local std::vector<glm::mat4> mats;
    mats.clear();
    mats.reserve(cars_.size() * 4);

    for (const auto& car : cars_) {
        if (!car) continue;
        // Cull whole car: if its body is off-screen, skip its 4 wheels.
        if (car->body_visual_node && car->body_visual_node->renderable
            && frustum.cull(car->body_visual_node->world_aabb())) continue;
        for (int wi = 0; wi < 4; ++wi) {
            if (car->wheel_nodes[wi])
                mats.push_back(car->wheel_nodes[wi]->world_matrix());
        }
    }
    if (mats.empty()) return;

    shader.use();
    assets_->wheel_tex.bind(0);
    assets_->wheel_mesh.draw_instanced(mats.data(),
                                        static_cast<int>(mats.size()));
}

void TrafficSystem::debug_draw(DebugDraw& debug) const {
    if (!graph_) return;
    TrafficLaneGraph lane_graph(*graph_);
    for (const auto& car : cars_) {
        if (!car || car->driver != Driver::AI) continue;
        const Car& c = *car;

        glm::vec3 cur = lane_graph.lane_pose(c.ai_lane, c.ai_distance_along,
                                             lane_offset_,
                                             c.ai_lateral_offset);
        cur.y = Heightmap::sample(cur.x, cur.z) + 0.18f;
        debug.cross(cur, c.ai_honking ? 0.55f : 0.28f);

        float look = c.ai_distance_along;
        LaneId lane = c.ai_lane;
        for (int k = 0; k < 3; ++k) {
            float next_d = (k == 0) ? std::min(ROAD_PITCH, look + 16.f)
                                    : std::min(ROAD_PITCH, 16.f);
            glm::vec3 a = lane_graph.lane_pose(lane,
                                               (k == 0) ? look : 0.f,
                                               lane_offset_, 0.f);
            glm::vec3 b = lane_graph.lane_pose(lane, next_d,
                                               lane_offset_, 0.f);
            a.y = Heightmap::sample(a.x, a.z) + 0.12f;
            b.y = Heightmap::sample(b.x, b.z) + 0.12f;
            debug.line(a, b);
            std::size_t idx = c.ai_route_index + static_cast<std::size_t>(k) + 1u;
            if (idx >= c.ai_route.lanes.size()) break;
            lane = c.ai_route.lanes[idx];
            look = 0.f;
        }

        glm::vec3 stop = lane_graph.lane_pose(c.ai_lane, ROAD_PITCH - STOP_BACK,
                                              lane_offset_, 0.f);
        stop.y = Heightmap::sample(stop.x, stop.z) + 0.14f;
        debug.cross(stop, 0.18f);
    }
}

// =============================================================================
// Vehicle ↔ vehicle collisions
// =============================================================================
//
// Run once per frame after every car has had its pose updated. Pairwise OBB
// (XZ-plane) intersection; for each colliding pair, separate the two cars and
// — when at least one of them is dynamic — apply an impulse along the contact
// normal so the player gets a believable bump. AI cars are kinematic and will
// re-stamp from their lane state next frame, so we only push them visually.
// A few iterations handle short chains (A pushed into B pushed into C).

void TrafficSystem::resolve_vehicle_collisions() {
    if (cars_.size() < 2) return;

    constexpr int   MAX_ITER    = 4;
    constexpr float SLOP        = 0.005f;
    constexpr float RESTITUTION = 0.2f;     // mostly inelastic — cars don't bounce off each other much

    for (int iter = 0; iter < MAX_ITER; ++iter) {
        bool any = false;

        for (std::size_t i = 0; i < cars_.size(); ++i) {
            for (std::size_t j = i + 1; j < cars_.size(); ++j) {
                Car& a = *cars_[i];
                Car& b = *cars_[j];

                OBBxz oa = make_obb_xz(a.vehicle);
                OBBxz ob = make_obb_xz(b.vehicle);

                glm::vec2 n2; float depth;
                if (!obb_xz_intersect(oa, ob, n2, depth)) continue;
                any = true;

                // Inflate normal to 3D (Y = 0 — XZ-plane resolution only).
                glm::vec3 n{n2.x, 0.f, n2.y};
                float push = depth + SLOP;

                // If a dynamic car hits an AI, promote that AI to Parked so
                // it picks up real physics for the impact. We flag it for
                // recovery (try_ai_recover) so once the chassis has settled
                // — upright, slowed, still near its assigned lane — it
                // snaps back onto the lane and resumes AI driving. Cars
                // that end up flipped or knocked far off the road simply
                // never satisfy the recovery conditions and stay parked.
                // We deliberately do NOT promote on AI ↔ AI contact — that
                // would turn every traffic jam into a pile of inert wrecks.
                bool a_was_ai = (a.driver == Driver::AI);
                bool b_was_ai = (b.driver == Driver::AI);
                if (a_was_ai && !b_was_ai) {
                    a.driver = Driver::Parked;
                    a.ai_state = TrafficAgentState::PhysicsFallback;
                    a.vehicle.set_inputs(0.f, 0.f, 0.f, false);
                    a.ai_recovery_pending = true;
                    a.ai_recovery_timer   = 0.f;
                }
                if (b_was_ai && !a_was_ai) {
                    b.driver = Driver::Parked;
                    b.ai_state = TrafficAgentState::PhysicsFallback;
                    b.vehicle.set_inputs(0.f, 0.f, 0.f, false);
                    b.ai_recovery_pending = true;
                    b.ai_recovery_timer   = 0.f;
                }

                bool a_dyn = (a.driver != Driver::AI);
                bool b_dyn = (b.driver != Driver::AI);

                if (a_dyn && b_dyn) {
                    // Both dynamic — separate proportional to inverse mass
                    // and exchange momentum along the contact normal.
                    // Position correction: translate apart by inverse-mass weights.
                    float ma = a.vehicle.body().mass;
                    float mb = b.vehicle.body().mass;
                    float mt = ma + mb;
                    a.vehicle.translate( n * (push * (mb / mt)));
                    b.vehicle.translate(-n * (push * (ma / mt)));

                    // Contact point: midpoint between the two visual centres
                    // in XZ. The Y is interpolated between bumper height and
                    // CoM height based on how head-on the hit is. Below-CoM
                    // contacts are load-bearing for SIDE hits — the r×n arm
                    // produces a roll torque so a fast T-bone can flip a car
                    // instead of just shoving it. But the same arm on a
                    // longitudinal hit (rear-end / head-on) becomes pure
                    // PITCH torque, which lifts the rear of the impacting car
                    // off the ground in a way that takes too long to settle.
                    // Solution: scale the bumper drop by min(longitudinality_a,
                    // longitudinality_b), where longitudinality is |n·body_z|.
                    // A pure rear-end (both cars longitudinal) → contact at
                    // CoM-Y, no pitch torque. A T-bone (one car broadside) →
                    // min is low, contact stays at bumper-Y so the side-hit
                    // car still rolls.
                    glm::vec3 ap = a.vehicle.position();
                    glm::vec3 bp = b.vehicle.position();
                    glm::vec3 a_min = a.vehicle.visual_aabb_min_local();
                    glm::vec3 b_min = b.vehicle.visual_aabb_min_local();
                    glm::vec3 n_local_a = glm::inverse(a.vehicle.orientation()) * n;
                    glm::vec3 n_local_b = glm::inverse(b.vehicle.orientation()) * n;
                    float long_a = std::abs(n_local_a.z);
                    float long_b = std::abs(n_local_b.z);
                    float drop_scale = 1.f - std::min(long_a, long_b);
                    glm::vec3 contact{
                        (ap.x + bp.x) * 0.5f,
                        ((ap.y + a_min.y * drop_scale)
                         + (bp.y + b_min.y * drop_scale)) * 0.5f,
                        (ap.z + bp.z) * 0.5f};

                    // Use point velocities (CoM linear + angular×r), not pure
                    // linear, so a spinning car contributes its tangential
                    // velocity at the contact correctly.
                    glm::vec3 v_a_pt = a.vehicle.body().point_velocity(contact);
                    glm::vec3 v_b_pt = b.vehicle.body().point_velocity(contact);
                    glm::vec3 v_rel  = v_a_pt - v_b_pt;
                    float v_n = glm::dot(v_rel, n);
                    if (v_n < 0.f) {
                        // Effective inverse mass at the contact accounts for
                        // both linear and rotational compliance. Without the
                        // (r×n)·I⁻¹(r×n) terms an off-centre impulse over-
                        // bounces because we'd use the linear-only formula
                        // while routing energy into rotation.
                        glm::vec3 ra = contact - ap;
                        glm::vec3 rb = contact - bp;
                        glm::vec3 ra_x_n = glm::cross(ra, n);
                        glm::vec3 rb_x_n = glm::cross(rb, n);
                        glm::mat3 inv_I_a = a.vehicle.body().inv_inertia_world();
                        glm::mat3 inv_I_b = b.vehicle.body().inv_inertia_world();
                        float k =  1.f / ma + 1.f / mb
                                + glm::dot(ra_x_n, inv_I_a * ra_x_n)
                                + glm::dot(rb_x_n, inv_I_b * rb_x_n);
                        float jmag = -(1.f + RESTITUTION) * v_n / k;
                        glm::vec3 imp = n * jmag;
                        a.vehicle.apply_impulse_at( imp, contact);
                        b.vehicle.apply_impulse_at(-imp, contact);
                    }
                } else {
                    // Both AI — split the push 50/50. Both will re-stamp from
                    // their lane scripts next frame; this just avoids a frame
                    // of overlap and prevents wedging.
                    a.vehicle.translate( n * (push * 0.5f));
                    b.vehicle.translate(-n * (push * 0.5f));
                }
            }
        }

        if (!any) break;
    }
}

// =============================================================================
// AI lane follow (kinematic)
// =============================================================================

// try_spawn_ai, try_spawn_police: extracted to traffic_spawn.cpp.
// ai_route_valid, ai_extend_route, ai_safe_to_shift, ai_update_speed,
// ai_advance, update_ai_kinematic, try_ai_recover, update_police_dynamic:
// extracted to traffic_drive.cpp.


void TrafficSystem::integrate_player_or_parked(Car& c, float dt,
                                                const WorldCollision& world) {
    // Reset the per-frame VFX scratch (chassis-on-ground contact points).
    // Substep appends to it; the renderer drains it each frame to spawn
    // sparks.
    c.vehicle.clear_scrape_contacts();
    // Two substeps at half-dt to match the player's old cadence.
    c.vehicle.substep(dt * 0.5f, world);
    c.vehicle.substep(dt * 0.5f, world);

    if (assets_->wheel_visible_radius > 1e-4f) {
        float v = glm::dot(c.vehicle.forward(), c.vehicle.body().linear_vel);
        c.wheel_spin_rad += v * dt / assets_->wheel_visible_radius;
    }
}

// =============================================================================
// Visual sync (identical for every car)
// =============================================================================

void TrafficSystem::sync_visuals(Car& c) {
    if (!c.chassis_node) return;

    // chassis_node carries the rigid-body pose.
    c.chassis_node->transform.position = c.vehicle.position();
    c.chassis_node->transform.rotation = c.vehicle.orientation();
    c.chassis_node->transform.scale    = {1.f, 1.f, 1.f};
    c.chassis_node->mark_dirty();

    // Body visual child: model offset + scale, plus the model-axis fix-up.
    const auto& ma = assets_->models[static_cast<std::size_t>(c.model_id)];
    const CarModelDef& def = CAR_MODELS[c.model_id];
    if (ma.body_ok && c.body_visual_node) {
        c.body_visual_node->transform.position = ma.body_visual_offset;
        c.body_visual_node->transform.rotation = glm::angleAxis(
            glm::radians(def.yaw_offset_deg), glm::vec3{0.f, 1.f, 0.f});
        c.body_visual_node->transform.scale    = ma.body_visual_scale;
        c.body_visual_node->mark_dirty();
    } else if (c.body_visual_node) {
        c.body_visual_node->transform.position = {0.f, 0.f, 0.f};
        c.body_visual_node->transform.rotation = glm::quat{1.f, 0.f, 0.f, 0.f};
        c.body_visual_node->transform.scale    = c.vehicle.chassis_full_extents;
        c.body_visual_node->mark_dirty();
    }

    // Wheels — position by mount + suspension drop, rotation = steer * roll.
    if (c.wheel_nodes[0]) {
        const float spin       = c.wheel_spin_rad;
        const float bottom_align = assets_->wheel_ok
            ? (c.vehicle.wheel_radius - assets_->wheel_visible_radius) : 0.f;
        const glm::vec3 wheel_scale = assets_->wheel_ok
            ? glm::vec3{assets_->wheel_visual_scale}
            : glm::vec3{0.32f, 2.f * c.vehicle.wheel_radius,
                              2.f * c.vehicle.wheel_radius};
        const auto& wheels = c.vehicle.wheels();
        const bool kinematic = (c.driver == Driver::AI);
        for (std::size_t i = 0; i < 4; ++i) {
            glm::vec3 pos = wheels[i].mount_local;
            // AI cars don't run wheel raycasts (kinematic), so use the
            // precomputed static-rest drop instead of the live one.
            float drop = kinematic ? ma.static_visual_drop
                                    : wheels[i].visual_drop;
            pos.y -= drop + bottom_align;
            c.wheel_nodes[i]->transform.position = pos;

            glm::quat steer{1.f, 0.f, 0.f, 0.f};
            if (wheels[i].is_steering) {
                steer = glm::angleAxis(-c.vehicle.steer_rad(),
                                        glm::vec3{0.f, 1.f, 0.f});
            }
            glm::quat roll = assets_->wheel_ok
                ? glm::angleAxis(-spin, glm::vec3{1.f, 0.f, 0.f})
                : glm::quat{1.f, 0.f, 0.f, 0.f};
            c.wheel_nodes[i]->transform.rotation = steer * roll;
            c.wheel_nodes[i]->transform.scale    = wheel_scale;
            c.wheel_nodes[i]->mark_dirty();
        }
    }
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

// =============================================================================
// Traffic lights (unchanged)
// =============================================================================

void TrafficSystem::sync_lights_to_loaded() {
    if (!graph_ || !scene_ || !light_vis_.cube_mesh) return;

    auto loaded = graph_->loaded_intersections();
    std::unordered_set<std::uint64_t> wanted;
    wanted.reserve(loaded.size());
    for (const auto& p : loaded) wanted.insert(pack_ij(p.first, p.second));

    for (auto it = light_intersections_.begin(); it != light_intersections_.end(); ) {
        if (wanted.count(*it) == 0) {
            int i = static_cast<int>(static_cast<std::int32_t>(*it >> 32));
            int j = static_cast<int>(static_cast<std::int32_t>(*it & 0xFFFFFFFFu));
            destroy_lights_at(i, j);
            it = light_intersections_.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& p : loaded) {
        std::uint64_t k = pack_ij(p.first, p.second);
        if (light_intersections_.insert(k).second) spawn_lights_at(p.first, p.second);
    }
}

void TrafficSystem::spawn_lights_at(int i, int j) {
    glm::vec3 ix = RoadGraph::intersection_pos(i, j, 0.f);

    // Traffic light nodes are static (they never move). Register them in the
    // cell bucket so cull() can skip this entire intersection at once when
    // it's outside the camera frustum.
    CellCoord light_cell = RoadGraph::intersection_cell(i, j);

    auto unit_cube = []() {
        AABB a; a.min = -glm::vec3{0.5f}; a.max = glm::vec3{0.5f}; return a;
    };
    auto make_node = [&](const glm::vec3& pos, const glm::vec3& scale,
                         const glm::vec3& tint) {
        SceneNode* n = scene_->create_node_static(nullptr, light_cell);
        n->renderable = Renderable{light_vis_.cube_mesh, unit_cube(), tint,
                                    glm::vec2{1.f, 1.f}, light_vis_.checker_tex};
        n->transform.position = pos;
        n->transform.scale    = scale;
        n->mark_dirty();
        return n;
    };

    for (int a = 0; a < 4; ++a) {
        const ApproachSpec& s = APPROACHES[a];
        float pole_x = ix.x + s.corner_dx * CORNER_DIST;
        float pole_z = ix.z + s.corner_dz * CORNER_DIST;
        float ground = city_ground_sample(pole_x, pole_z);
        float pole_top_y = ground + POLE_HEIGHT;

        float arm_cx = pole_x + s.arm_dx * (ARM_LENGTH * 0.5f);
        float arm_cz = pole_z + s.arm_dz * (ARM_LENGTH * 0.5f);
        float arm_cy = pole_top_y - ARM_THICK * 0.5f;
        glm::vec3 arm_scale = std::abs(s.arm_dx) > 0.5f
            ? glm::vec3{ARM_LENGTH, ARM_THICK, ARM_THICK}
            : glm::vec3{ARM_THICK, ARM_THICK, ARM_LENGTH};

        float arm_end_x = pole_x + s.arm_dx * ARM_LENGTH;
        float arm_end_z = pole_z + s.arm_dz * ARM_LENGTH;
        float housing_cy = pole_top_y - ARM_THICK - HOUSING_H * 0.5f;
        glm::vec3 housing_pos{arm_end_x, housing_cy, arm_end_z};

        glm::vec3 face{-s.arm_dz, 0.f, s.arm_dx};
        glm::vec3 bulb_offset = face * (HOUSING_D * 0.5f + BULB_SIZE * 0.5f);
        glm::vec3 r_pos = housing_pos + glm::vec3{0.f, +BULB_SPACING, 0.f} + bulb_offset;
        glm::vec3 y_pos = housing_pos                                       + bulb_offset;
        glm::vec3 g_pos = housing_pos + glm::vec3{0.f, -BULB_SPACING, 0.f} + bulb_offset;

        Light L;
        L.i = i; L.j = j; L.approach = a;
        L.pole    = make_node({pole_x, ground + POLE_HEIGHT * 0.5f, pole_z},
                               {POLE_THICK, POLE_HEIGHT, POLE_THICK}, METAL_TINT);
        L.arm     = make_node({arm_cx, arm_cy, arm_cz}, arm_scale, METAL_TINT);
        L.housing = make_node(housing_pos,
                               {HOUSING_W, HOUSING_H, HOUSING_D}, METAL_TINT);
        L.bulb_r  = make_node(r_pos, glm::vec3{BULB_SIZE}, RED_DIM);
        L.bulb_y  = make_node(y_pos, glm::vec3{BULB_SIZE}, YELLOW_DIM);
        L.bulb_g  = make_node(g_pos, glm::vec3{BULB_SIZE}, GREEN_DIM);
        lights_.push_back(L);
    }
}

void TrafficSystem::destroy_lights_at(int i, int j) {
    // Wipe the whole static bucket for this intersection first (O(1)).
    if (scene_) scene_->remove_static_cell(RoadGraph::intersection_cell(i, j));
    auto remove = [&](SceneNode* n) {
        if (n && scene_) scene_->remove_node(n);
    };
    for (std::size_t k = lights_.size(); k-- > 0;) {
        Light& L = lights_[k];
        if (L.i != i || L.j != j) continue;
        remove(L.pole);   remove(L.arm);    remove(L.housing);
        remove(L.bulb_r); remove(L.bulb_y); remove(L.bulb_g);
        lights_[k] = std::move(lights_.back());
        lights_.pop_back();
    }
}

void TrafficSystem::update_light_visuals(double t) {
    auto set_tint = [](SceneNode* n, const glm::vec3& c) {
        if (n && n->renderable) n->renderable->tint = c;
    };
    for (Light& L : lights_) {
        const ApproachSpec& s = APPROACHES[L.approach];
        LightPhase phase = light_phase(L.i, L.j, s.car_dir, t);
        set_tint(L.bulb_r, phase == LightPhase::Red    ? RED_BRIGHT    : RED_DIM);
        set_tint(L.bulb_y, phase == LightPhase::Yellow ? YELLOW_BRIGHT : YELLOW_DIM);
        set_tint(L.bulb_g, phase == LightPhase::Green  ? GREEN_BRIGHT  : GREEN_DIM);
    }
}

} // namespace pengine
