#pragma once

#include <cstdint>
#include <random>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "world/road_graph.h"

namespace pengine {

class Mesh;
class Scene;
class SceneNode;
class Texture;

// Lightweight kinematic AI cars driving on the global road grid. Each car
// follows a lane (right-side driving), advances along its current segment,
// and picks a new direction at each intersection (no U-turn unless dead-end).
// No physics, no collision with the player or each other — just visuals.
class TrafficSystem {
public:
    struct Visuals {
        const Mesh*    light_mesh = nullptr; // cube for poles/arms/bulbs
        const Texture* light_tex  = nullptr;
        const Mesh*    car_mesh   = nullptr; // shared body model
        glm::vec3      car_scale {1.f};      // applied to car node
        glm::vec3      car_offset{0.f};      // local offset under chassis frame
        // Extra Y-rotation (degrees) to align the model's local forward with
        // travel direction. Author-dependent: +Z-forward models need -90°.
        float          car_yaw_offset_deg = 0.f;
        std::vector<const Texture*> car_paints; // ≥1; spawn picks at random
    };

    void init(Scene* scene, const Visuals& vis, RoadGraph* graph, int target_count);
    void shutdown();

    // Per-frame update. Spawns up to one car / despawns out-of-range cars,
    // and advances all live cars. `time_seconds` drives the global traffic
    // light phase clock.
    void update(float dt, double time_seconds, const glm::vec3& camera_pos);

    int active() const { return static_cast<int>(cars_.size()); }

private:
    struct Car {
        SceneNode* node = nullptr;
        int        i = 0, j = 0;       // intersection just left
        GridDir    dir = GridDir::East;
        float      distance_along = 0.f; // metres from (i, j) toward neighbour
        float      speed = 0.f;          // m/s, current
        float      target_speed = 12.f;  // m/s, free-flow desired
        int        paint_idx = 0;
    };

    bool try_spawn(const glm::vec3& camera_pos, double time_seconds);
    void update_speed(std::size_t idx, float dt, double time_seconds);
    void advance(std::size_t idx, float dt);
    void update_visual(Car& car);
    void destroy_car(std::size_t idx);

    // Traffic lights — one signal per approach direction at every loaded
    // intersection (4 per intersection). Each signal: vertical pole on the
    // corner, horizontal arm out over the approaching lane, hanging housing
    // with three stacked bulbs (red/yellow/green). The bulb whose color
    // matches the current phase is rendered bright; the other two are dim.
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

    Scene*         scene_       = nullptr;
    Visuals        vis_         {};
    RoadGraph*     graph_       = nullptr;

    int            target_      = 0;
    float          spawn_min_   = 25.f;
    float          spawn_max_   = 140.f;
    float          despawn_dist_ = 200.f;
    float          lane_offset_ = 2.f;   // right-of-centerline metres

    std::vector<Car>   cars_;
    std::vector<Light> lights_;
    std::unordered_set<std::uint64_t> light_intersections_; // packed (i,j)
    std::mt19937       rng_{0xCABBA9Eu};
};

} // namespace pengine
