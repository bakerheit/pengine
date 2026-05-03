#include "game/traffic.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "core/log.h"
#include "physics/world_collision.h"
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

// ---- Car5 body model -------------------------------------------------------
// Mesh has +Z forward in author space; we apply YAW_OFFSET_DEG to flip into
// the engine's -Z forward convention. ARCH_CENTRE_Y_NATIVE is the body's
// wheel-arch midline in mesh-y, used to pin the body inside the wheels.
namespace Car5 {
    constexpr const char* MESH_PATH      = ASSETS_DIR "/models/vehicles/car5.emesh";
    constexpr const char* PAINT_PATHS[3] = {
        ASSETS_DIR "/Vehicles_psx/Car 05/car5.png",
        ASSETS_DIR "/Vehicles_psx/Car 05/car5_green.png",
        ASSETS_DIR "/Vehicles_psx/Car 05/car5_grey.png",
    };
    constexpr float YAW_OFFSET_DEG       = 180.f;
    constexpr float ARCH_CENTRE_Y_NATIVE = 0.45f;
    constexpr float WHEEL_X_NATIVE       = 1.038f;
    constexpr float WHEEL_ZF_NATIVE      = 2.254f;
    constexpr float WHEEL_ZR_NATIVE      = 1.813f;
}

// ---- Separate wheel mesh ---------------------------------------------------
namespace WheelAsset {
    constexpr const char* MESH_PATH    = ASSETS_DIR "/models/vehicles/wheel.emesh";
    constexpr const char* TEXTURE_PATH = ASSETS_DIR "/Vehicles_psx/Wheel/wheel.png";
    constexpr float       VISIBLE_RADIUS = 0.275f;
}

// ---- Suspension overrides --------------------------------------------------
// Stiffer than Vehicle defaults: less pitch under accel/brake, less roll on
// tight corners. Applied to every car so a stolen AI handles the same.
namespace SuspensionOverride {
    constexpr float SPRING_K = 130000.f;
    constexpr float DAMPER_K =  13000.f;  // ζ ≈ 0.93 — just shy of critical
}

// ---- AI lane-follow tuning -------------------------------------------------
constexpr float ACCEL_MAX     = 5.f;
constexpr float BRAKE_MAX     = 9.f;
constexpr float TIME_HEADWAY  = 1.4f;
constexpr float MIN_GAP       = 2.5f;
constexpr float STOP_BACK     = 6.f;

// ---- Traffic-light cycle ---------------------------------------------------
constexpr float LIGHT_PERIOD  = 20.f;
constexpr float LIGHT_HALF    = LIGHT_PERIOD * 0.5f;
constexpr float YELLOW_TIME   = 2.0f;

// ---- Direction lookup ------------------------------------------------------
// yaw_deg rotates the car's local -Z forward into world motion via R_y(yaw).
// `right_offset` is the +x perpendicular (lane offset is to the driver's
// right under right-hand traffic).
struct DirInfo {
    int       di, dj;
    glm::vec3 right_offset;
    float     yaw_deg;
};
inline DirInfo dir_info(GridDir d) {
    switch (d) {
        case GridDir::East:  return {+1,  0, { 0,0,-1}, 270.f};
        case GridDir::North: return { 0, +1, {+1,0, 0}, 180.f};
        case GridDir::West:  return {-1,  0, { 0,0,+1},  90.f};
        case GridDir::South: return { 0, -1, {-1,0, 0},   0.f};
    }
    return {1, 0, {0,0,-1}, 270.f};
}
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
bool light_is_green(int i, int j, GridDir car_dir, double t) {
    return light_phase(i, j, car_dir, t) == LightPhase::Green;
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
    glm::vec3 p  = v.position();
    glm::vec3 ax = v.right();        // body +X
    glm::vec3 az = -v.forward();     // body +Z (forward() is body -Z)
    o.center     = {p.x, p.z};
    o.half_ext   = {v.chassis_full_extents.x * 0.5f,
                    v.chassis_full_extents.z * 0.5f};
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
// Assets — loaded once at init, shared across every Car.
// =============================================================================
struct TrafficSystem::Assets {
    Mesh    body_mesh;
    Texture body_paints[3];
    bool    body_ok = false;

    Mesh    wheel_mesh;
    Texture wheel_tex;
    bool    wheel_ok = false;

    glm::vec3 body_visual_scale  {1.f}; // uniform scale to fit chassis length
    glm::vec3 body_visual_offset {0.f}; // chassis-local offset for body child
    float     wheel_visual_scale  = 1.f; // scale for wheel children
    float     wheel_visible_radius = 0.f;
    glm::vec3 wheel_mount[4]      {};   // chassis-local positions

    // Where the chassis-centre sits above the ground at static rest. Used
    // by AI cars (kinematic) so they line up with the player car visually.
    float ride_height_at_rest = 1.0f;

    // Suspension extension at static rest. AI cars never run wheel raycasts
    // (kinematic), so their `Wheel::visual_drop` stays at zero — wheels would
    // render at mount-level (too high). We stamp this value in sync_visuals
    // for AI cars so they match what the physics-driven player would settle to.
    //   static_compression = mass * |gravity| / (4 * spring_k)
    //   static_visual_drop = suspension_rest - static_compression
    float static_visual_drop = 0.f;
};

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

    // ---- Body mesh + paints ------------------------------------------------
    bool body_ok = load_static_emesh(Car5::MESH_PATH, assets_->body_mesh);
    for (int i = 0; body_ok && i < 3; ++i)
        body_ok = assets_->body_paints[i].load_file(Car5::PAINT_PATHS[i]);
    assets_->body_ok = body_ok;
    if (!body_ok) {
        PE_WARN("Car5 load failed; cars will fall back to cube visuals");
        return false;
    }

    // Body uniform scale + body_visual offset.
    {
        glm::vec3 mn = assets_->body_mesh.bounds_min();
        glm::vec3 mx = assets_->body_mesh.bounds_max();
        // A reference Vehicle just to get the same chassis_full_extents the
        // cars use (the field is configured on Vehicle's defaults).
        Vehicle ref;
        float native_length = std::max(0.001f, mx.z - mn.z);
        float target_length = ref.chassis_full_extents.z;
        float s             = target_length / native_length;
        float wheel_chassis_y = -ref.chassis_full_extents.y * 0.5f - ref.suspension_rest;
        assets_->body_visual_scale  = glm::vec3{s};
        assets_->body_visual_offset = {
            -(mn.x + mx.x) * 0.5f * s,
            wheel_chassis_y - Car5::ARCH_CENTRE_Y_NATIVE * s,
            -(mn.z + mx.z) * 0.5f * s,
        };
        assets_->ride_height_at_rest = ref.chassis_full_extents.y * 0.5f
                                     + ref.suspension_rest
                                     + ref.wheel_radius;
        const float static_compress =
            (ref.chassis_mass * std::abs(ref.gravity))
                / (4.f * SuspensionOverride::SPRING_K);
        assets_->static_visual_drop =
            std::max(0.f, ref.suspension_rest - static_compress);
        PE_INFO("Car5 model: native=%.2fx%.2fx%.2f scale=%.3f ride=%.2f",
                mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, s,
                assets_->ride_height_at_rest);
    }

    // ---- Wheel mesh + texture ---------------------------------------------
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

    // ---- Wheel mount positions (chassis-local) ----------------------------
    {
        Vehicle ref;
        float mount_y = -ref.chassis_full_extents.y * 0.5f;
        const float s        = assets_->body_visual_scale.x;
        const float wheel_x  = Car5::WHEEL_X_NATIVE  * s;
        const float wheel_zf = Car5::WHEEL_ZF_NATIVE * s; // chassis-local -Z
        const float wheel_zr = Car5::WHEEL_ZR_NATIVE * s; // chassis-local +Z
        assets_->wheel_mount[0] = {-wheel_x, mount_y, -wheel_zf}; // FL
        assets_->wheel_mount[1] = {+wheel_x, mount_y, -wheel_zf}; // FR
        assets_->wheel_mount[2] = {-wheel_x, mount_y, +wheel_zr}; // RL
        assets_->wheel_mount[3] = {+wheel_x, mount_y, +wheel_zr}; // RR
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

TrafficSystem::Car* TrafficSystem::create_car_at_pose(const glm::vec3& pos,
                                                      float yaw_deg,
                                                      int paint_idx,
                                                      Driver driver) {
    if (!scene_ || !assets_) return nullptr;
    auto car = std::make_unique<Car>();
    car->driver    = driver;
    car->paint_idx = paint_idx;

    // Configure the rigid body. spawn() resets velocity + sets default
    // wheel mounts; we then override wheel mounts to the car5 positions.
    car->vehicle.spring_k = SuspensionOverride::SPRING_K;
    car->vehicle.damper_k = SuspensionOverride::DAMPER_K;
    car->vehicle.spawn(pos, yaw_deg);
    for (int wi = 0; wi < 4; ++wi)
        car->vehicle.set_wheel_mount(wi, assets_->wheel_mount[wi]);

    // ---- Scene graph: chassis (rigid-body pose) → body_visual + 4 wheels --
    car->chassis_node     = scene_->create_node();
    car->body_visual_node = scene_->create_node(car->chassis_node);

    if (assets_->body_ok) {
        AABB local;
        local.min = assets_->body_mesh.bounds_min();
        local.max = assets_->body_mesh.bounds_max();
        int pi = paint_idx < 0 ? 0 : (paint_idx > 2 ? 2 : paint_idx);
        car->body_visual_node->renderable = Renderable{
            &assets_->body_mesh, local, glm::vec3{1.f, 1.f, 1.f},
            glm::vec2{1.f, 1.f}, &assets_->body_paints[pi]};
    } else if (light_vis_.cube_mesh) {
        AABB cube_aabb;
        cube_aabb.min = light_vis_.cube_mesh->bounds_min();
        cube_aabb.max = light_vis_.cube_mesh->bounds_max();
        car->body_visual_node->renderable = Renderable{
            light_vis_.cube_mesh, cube_aabb,
            glm::vec3{0.85f, 0.20f, 0.18f}, glm::vec2{1.f, 1.f},
            light_vis_.checker_tex};
    }

    // Wheels.
    const Mesh*    wm = assets_->wheel_ok ? &assets_->wheel_mesh
                                          : light_vis_.cube_mesh;
    const Texture* wt = assets_->wheel_ok ? &assets_->wheel_tex
                                          : light_vis_.checker_tex;
    AABB waabb;
    waabb.min = wm ? wm->bounds_min() : -glm::vec3{0.5f};
    waabb.max = wm ? wm->bounds_max() :  glm::vec3{0.5f};
    glm::vec3 wtint = assets_->wheel_ok ? glm::vec3{1.f}
                                         : glm::vec3{0.10f, 0.10f, 0.10f};
    for (int wi = 0; wi < 4; ++wi) {
        car->wheel_nodes[wi] = scene_->create_node(car->chassis_node);
        // When the wheel asset loaded, wheels are drawn in one instanced call
        // via TrafficSystem::draw_wheels — leave renderable empty so Scene::draw
        // skips them. The cube fallback still goes through the per-node path.
        if (!assets_->wheel_ok) {
            car->wheel_nodes[wi]->renderable = Renderable{
                wm, waabb, wtint, glm::vec2{1.f, 1.f}, wt};
        }
    }

    Car* raw = car.get();
    cars_.push_back(std::move(car));
    sync_visuals(*raw); // initial sync so first frame renders correctly
    return raw;
}

TrafficSystem::Car* TrafficSystem::spawn_player_car(const glm::vec3& pos,
                                                    float yaw_deg) {
    Car* c = create_car_at_pose(pos, yaw_deg, /*paint_idx=*/ 0, Driver::Player);
    if (c) player_car_ = c;
    return c;
}

void TrafficSystem::destroy_car(std::size_t idx) {
    if (idx >= cars_.size()) return;
    Car& c = *cars_[idx];
    if (player_car_ == &c) player_car_ = nullptr;
    if (scene_) {
        for (SceneNode* w : c.wheel_nodes) if (w) scene_->remove_node(w);
        if (c.body_visual_node) scene_->remove_node(c.body_visual_node);
        if (c.chassis_node)     scene_->remove_node(c.chassis_node);
    }
    cars_[idx] = std::move(cars_.back());
    cars_.pop_back();
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
    // Promote target. If it was AI, seed velocity from its lane direction
    // so the engine doesn't see a discontinuity (the rigid body had been
    // moving at AI speed via kinematic stamping, but body_.linear_vel was
    // never updated to match). Vehicle::body_ is private; we use forward()
    // and apply via the input pipeline.
    if (target->driver == Driver::AI) {
        DirInfo info = dir_info(target->ai_dir);
        glm::vec3 unit{static_cast<float>(info.di), 0.f,
                       static_cast<float>(info.dj)};
        // Vehicle has no public velocity setter; skip seeding for now. The
        // car will start from rest under physics — acceptable for this pass.
        (void)unit;
    }
    target->driver = Driver::Player;
    target->vehicle.set_inputs(0.f, 0.f, 0.f, /*handbrake=*/ false);
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
                // it picks up real physics. From here on it's a free-floating
                // chassis (lane script no longer drives it); the spawner
                // refills the AI population toward target_ai_count_, so this
                // doesn't deplete traffic. We deliberately do NOT promote on
                // AI ↔ AI contact — that would turn every traffic jam into a
                // pile of inert wrecks.
                bool a_was_ai = (a.driver == Driver::AI);
                bool b_was_ai = (b.driver == Driver::AI);
                if (a_was_ai && !b_was_ai) a.driver = Driver::Parked;
                if (b_was_ai && !a_was_ai) b.driver = Driver::Parked;

                bool a_dyn = (a.driver != Driver::AI);
                bool b_dyn = (b.driver != Driver::AI);

                if (a_dyn && b_dyn) {
                    // Both dynamic — separate proportional to inverse mass and
                    // exchange momentum along the contact normal.
                    float ma = a.vehicle.body().mass;
                    float mb = b.vehicle.body().mass;
                    float mt = ma + mb;
                    a.vehicle.translate( n * (push * (mb / mt)));
                    b.vehicle.translate(-n * (push * (ma / mt)));

                    glm::vec3 v_rel = a.vehicle.body().linear_vel
                                    - b.vehicle.body().linear_vel;
                    float v_n = glm::dot(v_rel, n);
                    if (v_n < 0.f) {
                        float jmag = -(1.f + RESTITUTION) * v_n
                                     / (1.f / ma + 1.f / mb);
                        a.vehicle.apply_impulse_central( n * jmag);
                        b.vehicle.apply_impulse_central(-n * jmag);
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

bool TrafficSystem::try_spawn_ai(const glm::vec3& camera_pos) {
    if (!graph_ || graph_->loaded_cell_count() == 0) return false;

    for (int attempt = 0; attempt < 16; ++attempt) {
        int i, j;
        if (!graph_->random_loaded_intersection(rng_, i, j)) return false;

        std::array<GridDir, 4> options;
        int n = graph_->outgoing(i, j, options);
        if (n == 0) continue;
        GridDir dir = options[rng_() % static_cast<unsigned>(n)];
        DirInfo di  = dir_info(dir);

        float along = static_cast<float>(rng_() & 0xFFFFu) / 65535.f * ROAD_PITCH;

        glm::vec3 from_pos = RoadGraph::intersection_pos(i, j, 0.f);
        glm::vec3 unit{static_cast<float>(di.di), 0.f, static_cast<float>(di.dj)};
        glm::vec3 spawn_xz = from_pos + unit * along + di.right_offset * lane_offset_;

        float dx = spawn_xz.x - camera_pos.x;
        float dz = spawn_xz.z - camera_pos.z;
        float dist = std::sqrt(dx*dx + dz*dz);
        if (dist < spawn_min_ || dist > spawn_max_) continue;

        // Place the chassis at the lane position, lifted to chassis-centre
        // height so the visual matches a player-driven car.
        float ground = Heightmap::sample(spawn_xz.x, spawn_xz.z);
        glm::vec3 spawn_pos{spawn_xz.x,
                             ground + assets_->ride_height_at_rest,
                             spawn_xz.z};
        // Match Vehicle::spawn yaw_deg argument: chassis rotation = R_y(yaw+90°)
        // and we want chassis rotation to put -Z forward at the motion
        // direction, i.e. R_y(info.yaw_deg).
        float yaw_deg = di.yaw_deg - 90.f;

        int paint_idx = static_cast<int>(rng_() % 3u);
        Car* c = create_car_at_pose(spawn_pos, yaw_deg, paint_idx, Driver::AI);
        if (!c) return false;

        c->ai_i = i; c->ai_j = j;
        c->ai_dir = dir;
        c->ai_distance_along = along;
        c->ai_target_speed = 9.f
            + static_cast<float>(rng_() & 0xFFFu) / 4096.f * 7.f; // 9..16 m/s
        c->ai_speed = c->ai_target_speed * 0.6f;
        return true;
    }
    return false;
}

void TrafficSystem::ai_update_speed(Car& c, float dt, double time_seconds) {
    // Free-flow target.
    float target = c.ai_target_speed;

    // Leader: same segment + lane + direction, ahead of us.
    {
        float best_gap = std::numeric_limits<float>::infinity();
        for (const auto& op : cars_) {
            const Car& o = *op;
            if (&o == &c) continue;
            if (o.driver != Driver::AI) continue;
            if (o.ai_i != c.ai_i || o.ai_j != c.ai_j || o.ai_dir != c.ai_dir) continue;
            if (o.ai_distance_along <= c.ai_distance_along) continue;
            constexpr float CAR_LENGTH = 4.f;
            float gap = o.ai_distance_along - c.ai_distance_along - CAR_LENGTH;
            if (gap < best_gap) best_gap = gap;
        }
        if (std::isfinite(best_gap)) {
            float t = (best_gap - MIN_GAP) / TIME_HEADWAY;
            target = std::min(target, std::max(0.f, t));
        }
    }

    // Traffic light at next intersection.
    {
        DirInfo info = dir_info(c.ai_dir);
        int next_i = c.ai_i + info.di;
        int next_j = c.ai_j + info.dj;
        if (!light_is_green(next_i, next_j, c.ai_dir, time_seconds)) {
            float stop_at = ROAD_PITCH - STOP_BACK;
            float gap     = stop_at - c.ai_distance_along;
            if (gap > 0.f) {
                float t = (gap - MIN_GAP) / TIME_HEADWAY;
                target = std::min(target, std::max(0.f, t));
            }
        }
    }

    // Approach target with bounded accel / brake.
    float diff = target - c.ai_speed;
    float delta = (diff > 0.f) ? std::min(diff, ACCEL_MAX * dt)
                                : std::max(diff, -BRAKE_MAX * dt);
    c.ai_speed += delta;
    if (c.ai_speed < 0.f) c.ai_speed = 0.f;
}

void TrafficSystem::ai_advance(Car& c, float dt) {
    c.ai_distance_along += c.ai_speed * dt;
    while (c.ai_distance_along >= ROAD_PITCH) {
        c.ai_distance_along -= ROAD_PITCH;
        DirInfo cur = dir_info(c.ai_dir);
        c.ai_i += cur.di;
        c.ai_j += cur.dj;

        std::array<GridDir, 4> options;
        int n = graph_->outgoing(c.ai_i, c.ai_j, options);
        if (n == 0) {
            c.ai_dir = opposite(c.ai_dir);
            c.ai_distance_along = 0.f;
            continue;
        }
        GridDir back = opposite(c.ai_dir);
        std::array<GridDir, 4> non_uturn;
        std::size_t m = 0;
        for (std::size_t k = 0; k < static_cast<std::size_t>(n); ++k)
            if (options[k] != back) non_uturn[m++] = options[k];
        c.ai_dir = (m > 0) ? non_uturn[rng_() % m] : back;
    }
}

void TrafficSystem::update_ai_kinematic(Car& c, float dt, double time_seconds) {
    ai_update_speed(c, dt, time_seconds);
    ai_advance(c, dt);

    // Stamp the rigid body's pose from the lane state. Wheel-roll spin uses
    // the AI speed.
    DirInfo info = dir_info(c.ai_dir);
    glm::vec3 from = RoadGraph::intersection_pos(c.ai_i, c.ai_j, 0.f);
    glm::vec3 unit{static_cast<float>(info.di), 0.f, static_cast<float>(info.dj)};
    glm::vec3 xz   = from + unit * c.ai_distance_along
                          + info.right_offset * lane_offset_;
    float ground   = Heightmap::sample(xz.x, xz.z);

    glm::vec3 pos{xz.x, ground + assets_->ride_height_at_rest, xz.z};
    glm::quat rot = glm::angleAxis(glm::radians(info.yaw_deg),
                                    glm::vec3{0.f, 1.f, 0.f});
    c.vehicle.set_kinematic_pose(pos, rot);

    if (assets_->wheel_visible_radius > 1e-4f) {
        c.wheel_spin_rad += c.ai_speed * dt
                          / assets_->wheel_visible_radius;
    }
}

// =============================================================================
// Player / Parked physics
// =============================================================================

void TrafficSystem::integrate_player_or_parked(Car& c, float dt,
                                                const WorldCollision& world) {
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
    if (assets_->body_ok && c.body_visual_node) {
        c.body_visual_node->transform.position = assets_->body_visual_offset;
        c.body_visual_node->transform.rotation = glm::angleAxis(
            glm::radians(Car5::YAW_OFFSET_DEG), glm::vec3{0.f, 1.f, 0.f});
        c.body_visual_node->transform.scale    = assets_->body_visual_scale;
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
            float drop = kinematic ? assets_->static_visual_drop
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
    // far from the camera. Player and Parked cars are NEVER despawned —
    // they stay where the player left them.
    for (std::size_t k = cars_.size(); k-- > 0;) {
        Car& c = *cars_[k];
        if (c.driver != Driver::AI) continue;
        bool unloaded = !graph_->is_intersection_loaded(c.ai_i, c.ai_j);
        glm::vec3 p = c.vehicle.position();
        float dx = p.x - camera_pos.x, dz = p.z - camera_pos.z;
        bool too_far = (dx*dx + dz*dz) > (despawn_dist_ * despawn_dist_);
        if (unloaded || too_far) destroy_car(k);
    }

    // Spawn AI cars to fill toward the target count.
    int ai_count = 0;
    for (const auto& cp : cars_) if (cp->driver == Driver::AI) ++ai_count;
    int budget = 2;
    while (ai_count < target_ai_count_ && budget-- > 0) {
        if (!try_spawn_ai(camera_pos)) break;
        ++ai_count;
    }

    // Per-driver update. sync_visuals is deferred until after collision
    // resolution so that the rendered pose reflects the post-resolve state
    // (otherwise overlapping cars would render mid-collision for one frame).
    for (auto& cp : cars_) {
        Car& c = *cp;
        switch (c.driver) {
            case Driver::AI:
                update_ai_kinematic(c, dt, time_seconds);
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
