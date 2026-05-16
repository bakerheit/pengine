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


// ---- AI lane-follow tuning -------------------------------------------------
constexpr float STOP_BACK     = 6.f;

// ---- Blocked-car avoidance tuning ------------------------------------------
// Cars stuck behind a dynamic blocker grow a blocked timer. Driver profiles
// decide when they honk or cautiously shift toward the opposing lane.
constexpr float STUCK_SPEED    = 1.5f;
constexpr float STUCK_GAP      = 8.0f;
constexpr float SWERVE_RATE    = 1.5f;

// ---- Traffic-light cycle ---------------------------------------------------
constexpr float LIGHT_PERIOD  = 20.f;
constexpr float LIGHT_HALF    = LIGHT_PERIOD * 0.5f;
constexpr float YELLOW_TIME   = 2.0f;

inline bool is_ew(GridDir d) {
    return d == GridDir::East || d == GridDir::West;
}

// ---- Light state -----------------------------------------------------------
enum class LightPhase { Red, Yellow, Green };
LightPhase light_phase(int i, int j, GridDir car_dir, double t) {
    std::uint32_t hash = static_cast<std::uint32_t>(i) * 0x9E3779B1u
                       ^ static_cast<std::uint32_t>(j) * 0x85EBCA77u;
    float offset = static_cast<float>(hash & 0xFFFFu) / 65535.f * LIGHT_PERIOD;
    float local  = static_cast<float>(std::fmod(t + offset, LIGHT_PERIOD));
    bool   ew_active   = (local < LIGHT_HALF);
    float  into_window = ew_active ? local : (local - LIGHT_HALF);
    bool   yellow      = (into_window > LIGHT_HALF - YELLOW_TIME);
    bool   showing_ew  = is_ew(car_dir);
    if (showing_ew == ew_active) return yellow ? LightPhase::Yellow : LightPhase::Green;
    return LightPhase::Red;
}
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
constexpr float LANE_HALF_WIDTH = 2.0f;
constexpr float CAR_LENGTH      = 4.0f;
constexpr float ROUTE_REFRESH_AT = ROAD_PITCH - 12.f;
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

bool TrafficSystem::ai_route_valid(const Car& c) const {
    if (!graph_) return false;
    TrafficLaneGraph lane_graph(*graph_);
    return lane_graph.lane_loaded(c.ai_lane);
}

void TrafficSystem::ai_extend_route(Car& c) {
    if (!graph_) return;
    TrafficLaneGraph lane_graph(*graph_);
    if (c.ai_route.lanes.empty()) {
        c.ai_route = lane_graph.make_route(c.ai_lane, ROUTE_LANES, rng_);
        c.ai_route_index = 0;
    }
    while (c.ai_route.lanes.size()
           < c.ai_route_index + static_cast<std::size_t>(ROUTE_LANES)) {
        LaneId from = c.ai_route.lanes.empty() ? c.ai_lane : c.ai_route.lanes.back();
        LaneId next = lane_graph.choose_next_lane(from, rng_);
        if (!lane_graph.lane_loaded(next)) break;
        c.ai_route.lanes.push_back(next);
    }
}

bool TrafficSystem::ai_safe_to_shift(const Car& c, float clear_dist) const {
    TrafficDirInfo info = traffic_dir_info(c.ai_lane.dir);
    glm::vec3 from = RoadGraph::intersection_pos(c.ai_lane.i, c.ai_lane.j, 0.f);
    glm::vec3 shift_origin = from - info.right * lane_offset_;

    for (const auto& op : cars_) {
        const Car& o = *op;
        if (&o == &c) continue;
        glm::vec3 op_pos = o.vehicle.position();

        auto in_window = [&](const glm::vec3& origin,
                             float min_along, float max_along) {
            glm::vec3 d = op_pos - origin;
            float along = glm::dot(d, info.unit);
            float lat   = glm::dot(d, info.right);
            return std::abs(lat) <= LANE_HALF_WIDTH
                && along >= min_along && along <= max_along;
        };

        if (in_window(shift_origin, c.ai_distance_along - 8.f,
                      std::min(c.ai_distance_along + clear_dist, ROAD_PITCH))) {
            return false;
        }
        float remaining = (c.ai_distance_along + clear_dist) - ROAD_PITCH;
        if (remaining > 0.f
            && in_window(shift_origin + info.unit * ROAD_PITCH, 0.f, remaining)) {
            return false;
        }
    }
    return true;
}

void TrafficSystem::ai_update_speed(Car& c, float dt, double time_seconds) {
    float target = c.ai_target_speed;
    float best_gap = std::numeric_limits<float>::infinity();
    bool leader_is_dynamic = false;
    bool leader_is_stopped_ai = false;
    {
        TrafficDirInfo info = traffic_dir_info(c.ai_lane.dir);
        glm::vec3 from = RoadGraph::intersection_pos(c.ai_lane.i, c.ai_lane.j, 0.f);
        glm::vec3 lane_origin = from + info.right
            * (lane_offset_ + c.ai_lateral_offset);

        for (const auto& op : cars_) {
            const Car& o = *op;
            if (&o == &c) continue;
            glm::vec3 op_pos = o.vehicle.position();

            glm::vec3 d   = op_pos - lane_origin;
            float    along = glm::dot(d, info.unit);
            float    lat   = glm::dot(d, info.right);
            if (std::abs(lat) <= LANE_HALF_WIDTH
                && along > c.ai_distance_along && along <= ROAD_PITCH) {
                float gap = along - c.ai_distance_along - CAR_LENGTH;
                if (gap < best_gap) {
                    best_gap = gap;
                    leader_is_dynamic = (o.driver != Driver::AI);
                    leader_is_stopped_ai = (o.driver == Driver::AI
                                            && o.ai_speed < STUCK_SPEED);
                }
                continue;
            }

            glm::vec3 d2   = op_pos - (lane_origin + info.unit * ROAD_PITCH);
            float    along2 = glm::dot(d2, info.unit);
            float    lat2   = glm::dot(d2, info.right);
            if (std::abs(lat2) <= LANE_HALF_WIDTH
                && along2 >= 0.f && along2 <= ROAD_PITCH) {
                float gap = (ROAD_PITCH - c.ai_distance_along)
                            + along2 - CAR_LENGTH;
                if (gap < best_gap) {
                    best_gap = gap;
                    leader_is_dynamic = (o.driver != Driver::AI);
                    leader_is_stopped_ai = (o.driver == Driver::AI
                                            && o.ai_speed < STUCK_SPEED);
                }
            }
        }
        if (std::isfinite(best_gap)) {
            target = std::min(target,
                traffic_follow_speed_for_gap(best_gap, c.ai_profile));
        }
    }

    bool blocked = std::isfinite(best_gap)
                && best_gap < STUCK_GAP
                && c.ai_speed < STUCK_SPEED;
    if (blocked) {
        c.ai_blocked_timer += dt;
        c.ai_honk_timer += dt;
    } else {
        c.ai_blocked_timer = std::max(0.f, c.ai_blocked_timer - dt * 2.f);
        c.ai_honk_timer = std::max(0.f, c.ai_honk_timer - dt * 2.f);
    }
    c.ai_honking = blocked && c.ai_honk_timer >= c.ai_profile.honk_after;

    IntersectionId next = TrafficLaneGraph::lane_end(c.ai_lane);
    LightPhase next_phase = light_phase(next.i, next.j, c.ai_lane.dir,
                                        time_seconds);
    bool light_is_hard_red = (next_phase == LightPhase::Red
                              && c.ai_distance_along < ROAD_PITCH - STOP_BACK);
    bool jam_pass_allowed =
        leader_is_dynamic
        || (leader_is_stopped_ai
            && !light_is_hard_red
            && traffic_profile_may_pass_jam(c.ai_profile,
                                            c.ai_blocked_timer));

    if (blocked && jam_pass_allowed
        && c.ai_blocked_timer >= c.ai_profile.patience_seconds
        && c.ai_lane_change == LaneChangeIntent::None
        && ai_safe_to_shift(c, c.ai_profile.safe_lane_gap)) {
        c.ai_lane_change = LaneChangeIntent::AroundBlocker;
        c.ai_pass_until_distance = std::min(ROAD_PITCH - 10.f,
            std::max(c.ai_distance_along + 24.f,
                     c.ai_distance_along + best_gap + CAR_LENGTH + 10.f));
        c.ai_state = TrafficAgentState::AvoidObstacle;
    }
    if (c.ai_lane_change == LaneChangeIntent::AroundBlocker
        && (c.ai_distance_along >= c.ai_pass_until_distance
            || c.ai_distance_along > ROAD_PITCH - 14.f)) {
        c.ai_lane_change = LaneChangeIntent::ReturnToLane;
    }

    {
        float target_offset = 0.f;
        if (c.ai_lane_change == LaneChangeIntent::AroundBlocker)
            target_offset = -2.f * lane_offset_;
        float diff = target_offset - c.ai_lateral_offset;
        float step = std::min(std::abs(diff), SWERVE_RATE * dt);
        float prev = c.ai_lateral_offset;
        c.ai_lateral_offset += (diff > 0.f) ? step : -step;
        c.ai_lateral_rate = (c.ai_lateral_offset - prev) / std::max(dt, 1e-5f);
        if (c.ai_lane_change == LaneChangeIntent::ReturnToLane
            && std::abs(c.ai_lateral_offset) < 0.05f) {
            c.ai_lane_change = LaneChangeIntent::None;
            c.ai_lateral_offset = 0.f;
        }
        if (std::abs(c.ai_lateral_offset) > lane_offset_ * 0.25f) {
            target = std::min(target, 5.0f);
            if (c.ai_state != TrafficAgentState::AvoidObstacle)
                c.ai_state = TrafficAgentState::LaneChange;
        }
    }

    if (blocked && c.ai_blocked_timer > c.ai_profile.patience_seconds + 6.f) {
        c.ai_state = TrafficAgentState::BlockedRecovery;
        target = std::min(target, 1.2f);
    } else if (std::isfinite(best_gap)) {
        c.ai_state = blocked ? TrafficAgentState::Queued
                             : TrafficAgentState::FollowLeader;
    } else if (c.ai_state != TrafficAgentState::AvoidObstacle
               && c.ai_state != TrafficAgentState::LaneChange) {
        c.ai_state = TrafficAgentState::Cruise;
    }

    {
        bool must_stop = (next_phase == LightPhase::Red);
        if (next_phase == LightPhase::Yellow) {
            float dist_to_stop = (ROAD_PITCH - STOP_BACK) - c.ai_distance_along;
            must_stop = traffic_should_stop_for_yellow(dist_to_stop,
                                                       c.ai_speed,
                                                       c.ai_profile);
        }
        if (must_stop) {
            float stop_at = ROAD_PITCH - STOP_BACK;
            float gap = stop_at - c.ai_distance_along;
            if (gap > 0.f) {
                target = std::min(target,
                    traffic_follow_speed_for_gap(gap, c.ai_profile));
                c.ai_state = (gap < 10.f) ? TrafficAgentState::Queued
                                          : TrafficAgentState::ApproachSignal;
            }
        } else if (c.ai_distance_along > ROUTE_REFRESH_AT) {
            c.ai_state = TrafficAgentState::YieldIntersection;
        }
    }

    if (c.ai_in_turn) c.ai_state = TrafficAgentState::TraverseIntersection;

    float diff = target - c.ai_speed;
    float delta = (diff > 0.f) ? std::min(diff, c.ai_profile.accel * dt)
                                : std::max(diff, -c.ai_profile.brake * dt);
    c.ai_speed += delta;
    if (c.ai_speed < 0.f) c.ai_speed = 0.f;
}

void TrafficSystem::ai_advance(Car& c, float dt) {
    if (!ai_route_valid(c)) {
        c.driver = Driver::Parked;
        c.ai_state = TrafficAgentState::PhysicsFallback;
        c.vehicle.set_inputs(0.f, 0.f, 0.f, true);
        return;
    }

    ai_extend_route(c);
    c.ai_distance_along += c.ai_speed * dt;
    while (c.ai_distance_along >= ROAD_PITCH) {
        c.ai_distance_along -= ROAD_PITCH;
        c.ai_prev_lane = c.ai_lane;
        if (c.ai_route_index + 1u < c.ai_route.lanes.size()) {
            ++c.ai_route_index;
            c.ai_lane = c.ai_route.lanes[c.ai_route_index];
        } else if (graph_) {
            TrafficLaneGraph lane_graph(*graph_);
            c.ai_lane = lane_graph.choose_next_lane(c.ai_lane, rng_);
            c.ai_route.lanes.clear();
            c.ai_route.lanes.push_back(c.ai_lane);
            c.ai_route_index = 0;
        }
        c.ai_in_turn = (c.ai_prev_lane.dir != c.ai_lane.dir);
        c.ai_lateral_offset = 0.f;
        c.ai_lateral_rate = 0.f;
        c.ai_pass_until_distance = 0.f;
        c.ai_lane_change = LaneChangeIntent::None;
        ai_extend_route(c);
    }

    // Match the Bezier post-arc length in update_ai_kinematic (TURN_POST = 8 m).
    if (c.ai_in_turn && c.ai_distance_along > 8.f)
        c.ai_in_turn = false;

    c.ai_next_lane = c.ai_lane;
    if (c.ai_route_index + 1u < c.ai_route.lanes.size())
        c.ai_next_lane = c.ai_route.lanes[c.ai_route_index + 1u];
    TrafficTurnKind next_turn =
        TrafficLaneGraph::turn_kind(c.ai_lane.dir, c.ai_next_lane.dir);
    c.ai_turn_signal_left = (next_turn == TrafficTurnKind::Left);
    c.ai_turn_signal_right = (next_turn == TrafficTurnKind::Right);
}

void TrafficSystem::update_ai_kinematic(Car& c, float dt, double time_seconds) {
    ai_update_speed(c, dt, time_seconds);
    ai_advance(c, dt);
    if (c.driver != Driver::AI) return;

    TrafficLaneGraph lane_graph(*graph_);
    TrafficDirInfo info = traffic_dir_info(c.ai_lane.dir);

    // Turn region: a quadratic Bezier through the corner so position and
    // heading evolve together (the previous linear blend held velocity
    // direction constant while yaw rotated separately, which read as a
    // mid-intersection "snap"). The arc starts TURN_PRE before the
    // intersection on the from-lane and ends TURN_POST past it on the
    // to-lane. P1 is where the two lane tangent lines meet, so the Bezier's
    // tangent matches each lane's direction at the endpoints.
    constexpr float TURN_PRE  = 8.f;
    constexpr float TURN_POST = 8.f;
    constexpr float TURN_LEN  = TURN_PRE + TURN_POST;

    bool   in_turn_region = false;
    LaneId from_id{}, to_id{};
    float  turn_t = 0.f;

    if (c.ai_in_turn) {
        // Just past the lane handoff: turn_t covers the post-intersection
        // half of the arc.
        from_id = c.ai_prev_lane;
        to_id   = c.ai_lane;
        turn_t  = (TURN_PRE + c.ai_distance_along) / TURN_LEN;
        in_turn_region = true;
    } else if (c.ai_lane != c.ai_next_lane
               && c.ai_lane.dir != c.ai_next_lane.dir) {
        // Approaching the intersection on the from-lane: start arcing as
        // soon as we cross the TURN_PRE threshold. U-turns have parallel
        // lane tangents (no intersection point), so fall through to the
        // straight lane_pose path for those.
        TrafficTurnKind k = TrafficLaneGraph::turn_kind(c.ai_lane.dir,
                                                        c.ai_next_lane.dir);
        if (k != TrafficTurnKind::UTurn) {
            float dist_to_int = ROAD_PITCH - c.ai_distance_along;
            if (dist_to_int < TURN_PRE) {
                from_id = c.ai_lane;
                to_id   = c.ai_next_lane;
                turn_t  = (TURN_PRE - dist_to_int) / TURN_LEN;
                in_turn_region = true;
            }
        }
    }

    glm::vec3 xz;
    float yaw;

    if (in_turn_region) {
        turn_t = std::clamp(turn_t, 0.f, 1.f);
        TrafficDirInfo from_info = traffic_dir_info(from_id.dir);
        TrafficDirInfo to_info   = traffic_dir_info(to_id.dir);
        glm::vec3 isect = RoadGraph::intersection_pos(
            from_id.i + from_info.di, from_id.j + from_info.dj, 0.f);
        glm::vec3 A = isect + from_info.right * lane_offset_; // end of from
        glm::vec3 B = isect + to_info.right   * lane_offset_; // start of to
        // Tangent intersection. For perpendicular L/R turns this is the
        // inside corner; the Bezier curve sweeps from P0 to P2 staying on
        // the convex side, so left turns cut wide through the box and right
        // turns hug the corner — matches the look of real lane geometry.
        float     s  = glm::dot(B - A, from_info.unit);
        glm::vec3 P1 = A + from_info.unit * s;
        glm::vec3 P0 = A - from_info.unit * TURN_PRE;
        glm::vec3 P2 = B + to_info.unit   * TURN_POST;

        float u = turn_t, omu = 1.f - u;
        xz = omu*omu*P0 + 2.f*u*omu*P1 + u*u*P2;
        glm::vec3 tan = 2.f*omu*(P1 - P0) + 2.f*u*(P2 - P1);
        // Engine convention: yaw 0/90/180/270° = S/W/N/E with body +Z fwd
        // mapped via the rotation. atan2(-tan.x, -tan.z) reproduces the
        // table in traffic_dir_info().
        if (glm::length(tan) > 1e-4f)
            yaw = glm::degrees(std::atan2(-tan.x, -tan.z));
        else
            yaw = info.yaw_deg;
    } else {
        xz = lane_graph.lane_pose(c.ai_lane, c.ai_distance_along,
                                  lane_offset_, c.ai_lateral_offset);
        yaw = info.yaw_deg;
    }

    float ground = Heightmap::sample(xz.x, xz.z);
    const auto& ma = assets_->models[static_cast<std::size_t>(c.model_id)];
    glm::vec3 pos{xz.x, ground + ma.ride_height_at_rest, xz.z};

    float dev = 0.f;
    if (std::abs(c.ai_lateral_rate) > 1e-3f) {
        constexpr float MAX_DEV = glm::radians(30.f);
        float v_fwd_floor = std::max(c.ai_speed, 0.5f);
        dev = std::atan2(c.ai_lateral_rate, v_fwd_floor);
        dev = std::clamp(dev, -MAX_DEV, MAX_DEV);
    }
    glm::quat rot = glm::angleAxis(glm::radians(yaw) + dev,
                                    glm::vec3{0.f, 1.f, 0.f});
    c.vehicle.set_kinematic_pose(pos, rot);

    if (assets_->wheel_visible_radius > 1e-4f) {
        c.wheel_spin_rad += c.ai_speed * dt
                          / assets_->wheel_visible_radius;
    }
}

// =============================================================================
// AI recovery after collision
// =============================================================================

void TrafficSystem::try_ai_recover(Car& c, float dt) {
    // Only Parked cars carrying an intact AI route are eligible. (Player
    // demotions and route-invalidation despawns also use PhysicsFallback,
    // but they don't set ai_recovery_pending.)
    if (!c.ai_recovery_pending || c.driver != Driver::Parked) return;

    c.ai_recovery_timer += dt;

    // Give the chassis a moment to bleed off the impact before we test for
    // a re-attach. Tuned by feel: short enough that the player still sees
    // the AI try to keep going, long enough that the recovery snap doesn't
    // happen mid-bounce.
    constexpr float RECOVERY_DELAY  = 1.5f;   // s before first attempt
    constexpr float MAX_RECOVERY    = 8.0f;   // s before we give up entirely
    constexpr float MAX_SPEED       = 6.0f;   // m/s — must have slowed down
    constexpr float MIN_UPRIGHT_DOT = 0.6f;   // chassis up vs world up
    constexpr float MAX_LANE_OFFSET = 5.0f;   // m perp distance from lane line

    if (c.ai_recovery_timer < RECOVERY_DELAY) return;

    if (c.ai_recovery_timer > MAX_RECOVERY) {
        // Wreck never recovered — leave it parked permanently. The spawner
        // will keep AI population topped up regardless.
        c.ai_recovery_pending = false;
        return;
    }

    // Need a loaded lane to snap onto. If the area has streamed out, just
    // wait — try_ai_recover will run again next frame.
    if (!graph_ || !ai_route_valid(c)) return;

    glm::vec3 pos = c.vehicle.position();
    glm::vec3 up_world{0.f, 1.f, 0.f};
    if (glm::dot(c.vehicle.up(), up_world) < MIN_UPRIGHT_DOT) return;
    if (c.vehicle.speed() > MAX_SPEED) return;

    // Project the chassis onto its assigned lane. If the impact knocked
    // the car too far sideways or behind/past the lane segment, bail and
    // try again next frame (the car may still be sliding into range).
    TrafficLaneGraph lane_graph(*graph_);
    TrafficLane      L     = lane_graph.lane(c.ai_lane, lane_offset_);
    glm::vec3        delta = pos - L.start;
    float along = glm::dot(delta, L.unit);
    if (along < 0.f || along > ROAD_PITCH) return;
    glm::vec3 perp = delta - L.unit * along;
    perp.y = 0.f;
    if (glm::length(perp) > MAX_LANE_OFFSET) return;

    // All clear — snap pose to the lane and hand control back to the AI
    // script. Reset transient lane-change / turn state so the agent
    // doesn't think it's mid-manoeuvre.
    glm::vec3 snap_xz = lane_graph.lane_pose(c.ai_lane, along,
                                             lane_offset_, 0.f);
    float ground = Heightmap::sample(snap_xz.x, snap_xz.z);
    const auto& ma = assets_->models[static_cast<std::size_t>(c.model_id)];
    glm::vec3 snap_pos{snap_xz.x, ground + ma.ride_height_at_rest, snap_xz.z};

    TrafficDirInfo info = traffic_dir_info(c.ai_lane.dir);
    glm::quat snap_rot = glm::angleAxis(glm::radians(info.yaw_deg),
                                        glm::vec3{0.f, 1.f, 0.f});
    c.vehicle.set_kinematic_pose(snap_pos, snap_rot);

    c.ai_distance_along       = along;
    c.ai_speed                = 0.f;
    c.ai_in_turn              = false;
    c.ai_lateral_offset       = 0.f;
    c.ai_lateral_rate         = 0.f;
    c.ai_pass_until_distance  = 0.f;
    c.ai_lane_change          = LaneChangeIntent::None;
    c.ai_blocked_timer        = 0.f;
    c.ai_state                = TrafficAgentState::Cruise;
    c.ai_recovery_pending     = false;
    c.ai_recovery_timer       = 0.f;
    c.driver                  = Driver::AI;
}

// =============================================================================
// Player / Parked physics
// =============================================================================

void TrafficSystem::update_police_dynamic(Car& c, float dt) {
    (void)dt;
    glm::vec3 to_target = police_target_pos_ - c.vehicle.position();
    to_target.y = 0.f;
    float dist = glm::length(to_target);
    if (dist < 1e-3f) {
        c.vehicle.set_inputs(0.f, 1.f, 0.f, false);
        return;
    }

    glm::vec3 dir = to_target / dist;
    float ahead = glm::dot(c.vehicle.forward(), dir);
    float side  = glm::dot(c.vehicle.right(), dir);
    float speed = c.vehicle.speed();

    float steer = std::clamp(side * 2.2f, -1.f, 1.f);
    float throttle = 1.f;
    float brake = 0.f;

    // If the target is behind us, brake into a turn first; once nearly
    // stopped, reverse so officers can recover from missed passes.
    if (ahead < -0.25f) {
        if (speed > 5.f) {
            throttle = 0.f;
            brake = 0.75f;
        } else {
            throttle = -0.65f;
            brake = 0.f;
        }
    }

    // Don't endlessly shove at walking speed when already on top of the
    // player; brake unless we are lined up for an actual ram.
    if (dist < 8.f && ahead > 0.3f) {
        throttle = 0.35f;
        if (speed > 10.f) brake = 0.5f;
    }

    bool handbrake = std::abs(side) > 0.75f && ahead > 0.1f && speed > 14.f;
    c.vehicle.set_inputs(throttle, brake, steer, handbrake);
}

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
