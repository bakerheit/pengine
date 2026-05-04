#include "game/traffic.h"

#include "game/car_models.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

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
    constexpr const char* TEXTURE_PATH = ASSETS_DIR "/Vehicles_psx/Wheel/wheel.png";
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
constexpr int   ROUTE_LANES      = 8;

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
    // Wheel asset is shared across every car model.
    Mesh    wheel_mesh;
    Texture wheel_tex;
    bool    wheel_ok            = false;
    float   wheel_visual_scale  = 1.f;
    float   wheel_visible_radius = 0.f;

    // Per-car-model resources. Indexed by Car::model_id (which corresponds
    // to the slot of CAR_MODELS[]). Each model is loaded independently — if
    // one fails, init() bails for the whole TrafficSystem.
    struct ModelAssets {
        Mesh                 body_mesh;
        std::vector<Texture> paints;          // sized to def.paint_count
        bool                 body_ok = false;

        glm::vec3 body_visual_scale  {1.f};   // uniform scale to fit chassis length
        glm::vec3 body_visual_offset {0.f};   // chassis-local offset for body child
        glm::vec3 wheel_mount[4]     {};      // chassis-local positions

        // Where the chassis-centre sits above the ground at static rest, for
        // AI cars (kinematic) to line up with player visually. Per-model
        // because chassis dimensions and wheel/suspension tuning differ.
        float ride_height_at_rest = 1.0f;

        // Suspension extension at static rest. AI cars never run wheel
        // raycasts so their Wheel::visual_drop stays zero — wheels would
        // render at mount level (too high). We stamp this value in
        // sync_visuals for AI cars so they match the physics-settled rest:
        //   static_compression = mass * |gravity| / (4 * spring_k)
        //   static_visual_drop = suspension_rest - static_compression
        float static_visual_drop = 0.f;
    };
    std::vector<ModelAssets> models;   // sized to NUM_CAR_MODELS at init()
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
        float wheel_chassis_y = -ref.chassis_full_extents.y * 0.5f
                                 - ref.suspension_rest;
        ma.body_visual_scale  = glm::vec3{s};
        ma.body_visual_offset = {
            -(mn.x + mx.x) * 0.5f * s,
            wheel_chassis_y - def.arch_centre_y_native * s,
            -(mn.z + mx.z) * 0.5f * s,
        };
        ma.ride_height_at_rest = ref.chassis_full_extents.y * 0.5f
                               + ref.suspension_rest
                               + ref.wheel_radius;
        const float static_compress =
            (ref.chassis_mass * std::abs(ref.gravity))
                / (4.f * ref.spring_k);
        ma.static_visual_drop =
            std::max(0.f, ref.suspension_rest - static_compress);

        // Wheel mount positions (chassis-local) from this model's mesh-native
        // wheel positions, scaled by the body-fit factor `s`.
        const float mount_y  = -ref.chassis_full_extents.y * 0.5f;
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

TrafficSystem::Car* TrafficSystem::create_car_at_pose(const glm::vec3& pos,
                                                      float yaw_deg,
                                                      int model_id,
                                                      int paint_idx,
                                                      Driver driver) {
    if (!scene_ || !assets_) return nullptr;
    if (model_id < 0 || model_id >= NUM_CAR_MODELS) model_id = 0;
    const CarModelDef& def = CAR_MODELS[model_id];
    auto&              ma  = assets_->models[static_cast<std::size_t>(model_id)];
    if (!ma.body_ok) return nullptr;

    auto car = std::make_unique<Car>();
    car->driver    = driver;
    car->model_id  = model_id;
    // Clamp paint index to this model's paint palette.
    car->paint_idx = std::clamp(paint_idx, 0, def.paint_count - 1);

    // Apply per-model tuning (mass, max_speed, engine_force, …) before spawn.
    apply_model_tuning(car->vehicle, def);

    // Configure the rigid body. spawn() resets velocity + sets default
    // wheel mounts; we then override wheel mounts to this model's positions.
    car->vehicle.spawn(pos, yaw_deg);
    for (int wi = 0; wi < 4; ++wi)
        car->vehicle.set_wheel_mount(wi, ma.wheel_mount[wi]);

    // ---- Scene graph: chassis (rigid-body pose) → body_visual + 4 wheels --
    car->chassis_node     = scene_->create_node();
    car->body_visual_node = scene_->create_node(car->chassis_node);

    if (ma.body_ok) {
        AABB local;
        local.min = ma.body_mesh.bounds_min();
        local.max = ma.body_mesh.bounds_max();
        car->body_visual_node->renderable = Renderable{
            &ma.body_mesh, local, glm::vec3{1.f, 1.f, 1.f},
            glm::vec2{1.f, 1.f},
            &ma.paints[static_cast<std::size_t>(car->paint_idx)]};
    } else if (light_vis_.cube_mesh) {
        AABB cube_aabb;
        cube_aabb.min = light_vis_.cube_mesh->bounds_min();
        cube_aabb.max = light_vis_.cube_mesh->bounds_max();
        car->body_visual_node->renderable = Renderable{
            light_vis_.cube_mesh, cube_aabb,
            glm::vec3{0.85f, 0.20f, 0.18f}, glm::vec2{1.f, 1.f},
            light_vis_.checker_tex};
    }

    // Dynamic wheels — skipped entirely for models that already have wheels
    // baked into the body mesh, otherwise we'd render two sets at the same
    // wheel arches. draw_wheels and sync_visuals both null-check the
    // wheel_nodes, so leaving them null disables both paths cleanly.
    if (!def.body_has_built_in_wheels) {
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
            // When the wheel asset loaded, wheels are drawn in one instanced
            // call via TrafficSystem::draw_wheels — leave renderable empty
            // so Scene::draw skips them. Cube fallback uses the per-node path.
            if (!assets_->wheel_ok) {
                car->wheel_nodes[wi]->renderable = Renderable{
                    wm, waabb, wtint, glm::vec2{1.f, 1.f}, wt};
            }
        }
    }

    Car* raw = car.get();
    cars_.push_back(std::move(car));
    sync_visuals(*raw); // initial sync so first frame renders correctly
    return raw;
}

TrafficSystem::Car* TrafficSystem::spawn_player_car(const glm::vec3& pos,
                                                    float yaw_deg) {
    // Player drives Car5 (model 0) by default.
    Car* c = create_car_at_pose(pos, yaw_deg, /*model_id=*/ 0,
                                 /*paint_idx=*/ 0, Driver::Player);
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
    if (target->driver == Driver::AI)
        target->ai_state = TrafficAgentState::PhysicsFallback;
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
                // it picks up real physics. From here on it's a free-floating
                // chassis (lane script no longer drives it); the spawner
                // refills the AI population toward target_ai_count_, so this
                // doesn't deplete traffic. We deliberately do NOT promote on
                // AI ↔ AI contact — that would turn every traffic jam into a
                // pile of inert wrecks.
                bool a_was_ai = (a.driver == Driver::AI);
                bool b_was_ai = (b.driver == Driver::AI);
                if (a_was_ai && !b_was_ai) {
                    a.driver = Driver::Parked;
                    a.ai_state = TrafficAgentState::PhysicsFallback;
                    a.vehicle.set_inputs(0.f, 0.f, 0.f, false);
                }
                if (b_was_ai && !a_was_ai) {
                    b.driver = Driver::Parked;
                    b.ai_state = TrafficAgentState::PhysicsFallback;
                    b.vehicle.set_inputs(0.f, 0.f, 0.f, false);
                }

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
    TrafficLaneGraph lane_graph(*graph_);

    for (int attempt = 0; attempt < 24; ++attempt) {
        int i, j;
        if (!graph_->random_loaded_intersection(rng_, i, j)) return false;

        std::array<GridDir, 4> options;
        int n = graph_->outgoing(i, j, options);
        if (n == 0) continue;
        GridDir dir = options[rng_() % static_cast<unsigned>(n)];
        LaneId start_lane{i, j, dir};
        if (!lane_graph.lane_loaded(start_lane)) continue;

        float along = static_cast<float>(rng_() & 0xFFFFu) / 65535.f * ROAD_PITCH;
        TrafficRoute route = lane_graph.make_route(start_lane, ROUTE_LANES, rng_);
        if (route.lanes.size() < 2) continue;

        glm::vec3 spawn_xz = lane_graph.lane_pose(start_lane, along,
                                                  lane_offset_, 0.f);
        float dx = spawn_xz.x - camera_pos.x;
        float dz = spawn_xz.z - camera_pos.z;
        float dist = std::sqrt(dx*dx + dz*dz);
        if (dist < spawn_min_ || dist > spawn_max_) continue;

        bool occupied = false;
        for (const auto& other : cars_) {
            glm::vec3 p = other->vehicle.position();
            float ox = p.x - spawn_xz.x;
            float oz = p.z - spawn_xz.z;
            if ((ox*ox + oz*oz) < 64.f) {
                occupied = true;
                break;
            }
        }
        if (occupied) continue;

        // Pick model by spawn weight (so e.g. trucks are rarer than sedans),
        // then a random paint within that model's palette. Done up front so
        // we can use the model's ride height when placing the chassis.
        int total_weight = 0;
        for (int m = 0; m < NUM_CAR_MODELS; ++m) total_weight += CAR_MODELS[m].spawn_weight;
        int r = static_cast<int>(rng_() % static_cast<std::uint32_t>(total_weight));
        int model_id = 0;
        for (int m = 0; m < NUM_CAR_MODELS; ++m) {
            r -= CAR_MODELS[m].spawn_weight;
            if (r < 0) { model_id = m; break; }
        }
        int paint_idx = static_cast<int>(rng_() %
                            static_cast<std::uint32_t>(
                                CAR_MODELS[model_id].paint_count));

        // Place the chassis at the lane position, lifted to chassis-centre
        // height so the visual matches a player-driven car.
        float ground = Heightmap::sample(spawn_xz.x, spawn_xz.z);
        glm::vec3 spawn_pos{spawn_xz.x,
                             ground + assets_->models[
                                 static_cast<std::size_t>(model_id)
                             ].ride_height_at_rest,
                             spawn_xz.z};
        TrafficDirInfo info = traffic_dir_info(dir);
        float yaw_deg = info.yaw_deg - 90.f;

        Car* c = create_car_at_pose(spawn_pos, yaw_deg,
                                     model_id, paint_idx, Driver::AI);
        if (!c) return false;

        c->ai_profile = random_driver_profile(rng_);
        c->ai_route = std::move(route);
        c->ai_route_index = 0;
        c->ai_lane = start_lane;
        c->ai_next_lane = c->ai_route.lanes[1];
        c->ai_prev_lane = start_lane;
        c->ai_distance_along = along;
        c->ai_target_speed = 9.f
            + static_cast<float>(rng_() & 0xFFFu) / 4096.f * 7.f; // 9..16 m/s
        c->ai_target_speed *= c->ai_profile.speed_mul;
        c->ai_speed = c->ai_target_speed * 0.6f;
        c->ai_state = TrafficAgentState::Cruise;
        return true;
    }
    return false;
}

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

    if (c.ai_in_turn && c.ai_distance_along > 14.f)
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
    glm::vec3 xz = lane_graph.lane_pose(c.ai_lane, c.ai_distance_along,
                                        lane_offset_, c.ai_lateral_offset);
    TrafficDirInfo info = traffic_dir_info(c.ai_lane.dir);
    float yaw = info.yaw_deg;

    if (c.ai_in_turn) {
        constexpr float TURN_BLEND_DIST = 14.f;
        float t = std::clamp(c.ai_distance_along / TURN_BLEND_DIST, 0.f, 1.f);
        t = t * t * (3.f - 2.f * t);
        glm::vec3 prev_end = lane_graph.lane_pose(c.ai_prev_lane, ROAD_PITCH,
                                                  lane_offset_, 0.f);
        glm::vec3 next_pose = lane_graph.lane_pose(c.ai_lane,
                                                   c.ai_distance_along,
                                                   lane_offset_, 0.f);
        xz = prev_end * (1.f - t) + next_pose * t;
        TrafficDirInfo prev = traffic_dir_info(c.ai_prev_lane.dir);
        float yaw_delta = yaw - prev.yaw_deg;
        while (yaw_delta > 180.f) yaw_delta -= 360.f;
        while (yaw_delta < -180.f) yaw_delta += 360.f;
        yaw = prev.yaw_deg + yaw_delta * t;
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
    // far from the camera. Player and Parked cars are NEVER despawned —
    // they stay where the player left them.
    for (std::size_t k = cars_.size(); k-- > 0;) {
        Car& c = *cars_[k];
        if (c.driver != Driver::AI) continue;
        bool unloaded = !ai_route_valid(c);
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
