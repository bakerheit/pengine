#pragma once

#include <cstdint>
#include <memory>
#include <random>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "game/vehicle.h"
#include "game/traffic_ai.h"
#include "render/mesh.h"
#include "render/texture.h"
#include "world/road_graph.h"

namespace pengine {

struct Frustum;
class DebugDraw;
class Scene;
class SceneNode;
class Shader;
class WorldCollision;

// All cars in the world — player, AI, and parked alike — share the same
// Car entity layout. TrafficSystem owns every car in the simulation, runs
// each one according to its current driver, and exposes a small API for
// the Application to steer "the player's car" without teleporting between
// entities. Pressing F simply transfers the Player driver flag.
class TrafficSystem {
public:
    enum class Driver : std::uint8_t { AI, Player, Parked, Police };

    // A single car. Identical layout for player and AI; the only difference
    // is which `Driver` is updating its inputs each frame.
    struct Car {
        Driver driver = Driver::AI;

        // Physics — used when driver is Player or Parked. For AI cars the
        // rigid body is kinematic: its body position/orientation are stamped
        // each frame from the lane state below.
        Vehicle vehicle;

        // Which body model this car uses (Car5, Car8, …). Selects the body
        // mesh, paint palette, wheel mount geometry, and the tuning applied
        // to `vehicle` (mass, max_speed, etc).
        int model_id = 0;

        // Paint variant index into the per-model paints list.
        int paint_idx = 0;
        bool police_unit = false;

        // ---- AI route state (used when driver == AI) ---------------------
        TrafficAgentState ai_state = TrafficAgentState::Cruise;
        DriverProfile     ai_profile{};
        TrafficRoute      ai_route;
        std::size_t       ai_route_index = 0;
        LaneId            ai_lane{};
        LaneId            ai_next_lane{};
        LaneId            ai_prev_lane{};
        float             ai_distance_along = 0.f;  // metres from lane start
        float             ai_speed          = 0.f;  // current script speed (m/s)
        float             ai_target_speed   = 12.f; // free-flow desired speed (m/s)
        bool              ai_in_turn        = false;

        // Lane-change / avoidance. One-lane-per-direction v1 means a lawful
        // lane change is a temporary, cautious move toward the centreline or
        // opposing lane to clear a static/dynamic blocker, then a return.
        LaneChangeIntent ai_lane_change = LaneChangeIntent::None;
        float            ai_lateral_offset = 0.f;
        float            ai_lateral_rate   = 0.f;
        float            ai_pass_until_distance = 0.f;
        float            ai_blocked_timer  = 0.f;
        float            ai_honk_timer     = 0.f;
        float            ai_panic_timer    = 0.f;
        bool             ai_turn_signal_left  = false;
        bool             ai_turn_signal_right = false;
        bool             ai_honking = false;

        // Set when an AI car is demoted to Parked by a collision and the
        // route state is still intact, so we can try to put the AI back in
        // control once the chassis has settled. ai_recovery_timer counts up
        // each frame the car spends in Parked + recovery_pending; the
        // recovery attempt fires once it crosses the threshold.
        bool             ai_recovery_pending = false;
        float            ai_recovery_timer   = 0.f;

        // ---- Visual scene-graph (always present) -------------------------
        SceneNode* chassis_node     = nullptr;       // rigid-body pose
        SceneNode* body_visual_node = nullptr;       // model offset / scale / yaw fix
        SceneNode* wheel_nodes[4]   = {nullptr, nullptr, nullptr, nullptr};
        float      wheel_spin_rad   = 0.f;           // rolling angle accumulator
    };

    // Visual handles owned elsewhere (e.g. cube + checker for traffic-light
    // scaffolding fallback). Body / wheel assets are loaded internally.
    struct LightVisuals {
        const Mesh*    cube_mesh   = nullptr;
        const Texture* checker_tex = nullptr;
    };

    TrafficSystem();
    ~TrafficSystem();
    TrafficSystem(const TrafficSystem&)            = delete;
    TrafficSystem& operator=(const TrafficSystem&) = delete;

    // Lifecycle.
    bool init(Scene& scene, const LightVisuals& lights,
              RoadGraph& graph, int target_ai_count);
    void shutdown();

    // Spawn the player's starting car. Returns the new Car (always non-null
    // on success). The car is created with `Driver::Player` and registered
    // as the system's player car.
    Car* spawn_player_car(const glm::vec3& pos, float yaw_deg);

    // Per-frame update. Runs AI lane-following on AI cars (kinematic),
    // physics substeps on Player/Parked cars, traffic lights, and visual
    // sync for all cars.
    void update(float dt, double time_seconds,
                const glm::vec3& camera_pos, const WorldCollision& world);

    int active() const { return static_cast<int>(cars_.size()); }

    // Read-only view of every spawned car. Storage is unique_ptr so the
    // lambda gets a raw `Car*` per slot. Stable across frames except when
    // a car despawns (the slot is removed).
    const std::vector<std::unique_ptr<Car>>& cars() const { return cars_; }

    // Closest car (any driver) to `point` within `radius`. Null if none.
    Car* find_nearest(const glm::vec3& point, float radius) const;

    // Make `target` the player car. The previous player car (if any) is
    // demoted to `Driver::Parked`. No teleport: each car keeps its current
    // pose / velocity. Returns true if a transfer happened.
    bool set_player_driver(Car* target);

    // The currently-driven car, or null if none. Camera, HUD, and input all
    // route through this pointer.
    Car*       player_car()       { return player_car_; }
    const Car* player_car() const { return player_car_; }

    // Forwards inputs to the player car's Vehicle. No-op if there's no
    // player car (e.g. between teardown and respawn).
    void set_player_inputs(float throttle, float brake, float steer,
                            bool handbrake);

    // Wanted-system hook. Application owns crime/wanted timing; TrafficSystem
    // turns that into police cars that spawn near and chase this target.
    void set_police_response(int wanted_level, const glm::vec3& target_pos);

    // Draw every visible car's wheels in a single instanced draw. Caller must
    // have bound `shader` and set its globals (view_proj, lights, diffuse=0,
    // tint=1, uv_scale=1). No-op if the wheel asset failed to load — in that
    // case wheel rendering happens through the regular Scene::draw path.
    void draw_wheels(Shader& shader, const Frustum& frustum) const;
    void debug_draw(DebugDraw& debug) const;

private:
    // Tunable visual params, derived from the car5 + Wheel.obj assets at
    // init time. Lives in the cpp's anon namespace; a pointer here is just
    // for forward reference.
    struct Assets;
    std::unique_ptr<Assets> assets_;

    // Per-frame per-car logic split by driver.
    void update_ai_kinematic(Car& c, float dt, double time_seconds);
    void update_police_dynamic(Car& c, float dt);
    void integrate_player_or_parked(Car& c, float dt,
                                     const WorldCollision& world);
    void sync_visuals(Car& c);

    // Resolve all car ↔ car overlaps. Called once per update after every
    // car has had its pose updated, before sync_visuals.
    void resolve_vehicle_collisions();

    // AI helpers (kinematic lane follow).
    void ai_update_speed(Car& c, float dt, double time_seconds);
    void ai_advance(Car& c, float dt);
    bool ai_safe_to_shift(const Car& c, float clear_dist) const;
    bool ai_route_valid(const Car& c) const;
    void ai_extend_route(Car& c);

    // Tick a Parked car flagged for recovery. Snaps it back onto its
    // assigned lane and re-promotes to Driver::AI once it has settled
    // (upright, slow, still near the lane).
    void try_ai_recover(Car& c, float dt);

    // Spawn helpers.
    bool try_spawn_ai(const glm::vec3& camera_pos);
    bool try_spawn_police();
    Car* create_car_at_pose(const glm::vec3& pos, float yaw_deg, int model_id,
                             int paint_idx, Driver driver);
    void destroy_car(std::size_t idx);

    // Traffic lights — one signal per approach direction at every loaded
    // intersection. (Unchanged from the previous implementation.)
    struct Light {
        int        i = 0, j = 0;
        int        approach = 0;       // 0..3, indexes APPROACHES[]
        SceneNode* pole    = nullptr;
        SceneNode* arm     = nullptr;
        SceneNode* housing = nullptr;
        SceneNode* bulb_r  = nullptr;
        SceneNode* bulb_y  = nullptr;
        SceneNode* bulb_g  = nullptr;
    };
    void sync_lights_to_loaded();
    void update_light_visuals(double time_seconds);
    void spawn_lights_at(int i, int j);
    void destroy_lights_at(int i, int j);

    Scene*        scene_       = nullptr;
    LightVisuals  light_vis_   {};
    RoadGraph*    graph_       = nullptr;

    int   target_ai_count_ = 0;
    int   wanted_level_    = 0;
    float police_spawn_timer_ = 0.f;
    float spawn_min_       = 25.f;
    float spawn_max_       = 140.f;
    float despawn_dist_    = 200.f;
    float lane_offset_     = 2.f;
    glm::vec3 police_target_pos_{0.f};

    std::vector<std::unique_ptr<Car>> cars_;
    Car*                              player_car_ = nullptr;

    std::vector<Light>                 lights_;
    std::unordered_set<std::uint64_t>  light_intersections_;
    std::mt19937                       rng_{0xCABBA9Eu};
};

} // namespace pengine
