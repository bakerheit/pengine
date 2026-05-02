#pragma once

#include <random>
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
    void init(Scene* scene, const Mesh* cube_mesh, const Texture* car_texture,
              RoadGraph* graph, int target_count);
    void shutdown();

    // Per-frame update. Spawns up to one car / despawns out-of-range cars,
    // and advances all live cars.
    void update(float dt, const glm::vec3& camera_pos);

    int active() const { return static_cast<int>(cars_.size()); }

private:
    struct Car {
        SceneNode* node = nullptr;
        int        i = 0, j = 0;       // intersection just left
        GridDir    dir = GridDir::East;
        float      distance_along = 0.f; // metres from (i, j) toward neighbour
        float      speed = 10.f;         // m/s
        glm::vec3  tint{1.f};
    };

    bool try_spawn(const glm::vec3& camera_pos);
    void advance(std::size_t idx, float dt);
    void update_visual(Car& car);
    void destroy_car(std::size_t idx);

    Scene*         scene_       = nullptr;
    const Mesh*    mesh_        = nullptr;
    const Texture* texture_     = nullptr;
    RoadGraph*     graph_       = nullptr;

    int            target_      = 0;
    float          spawn_min_   = 25.f;
    float          spawn_max_   = 140.f;
    float          despawn_dist_ = 200.f;
    float          lane_offset_ = 2.f;   // right-of-centerline metres

    std::vector<Car> cars_;
    std::mt19937     rng_{0xCABBA9Eu};
};

} // namespace pengine
