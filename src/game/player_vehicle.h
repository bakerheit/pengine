#pragma once

#include <glm/glm.hpp>

#include "game/vehicle.h"
#include "render/mesh.h"
#include "render/texture.h"

namespace pengine {

class Scene;
class SceneNode;
class WorldCollision;

// Player-driven vehicle: rigid-body Vehicle + car5 body model + four
// independently driven wheel scene nodes. Owns its assets, scene nodes,
// and the suspension overrides used for the player car. AI traffic cars
// share the body mesh / paints / scale via the read-only accessors.
//
// Tuning knobs (paint paths, body-arch alignment, wheel anchor positions,
// suspension stiffness) live in player_vehicle.cpp's anonymous namespace.
//
// Lifecycle:
//   1. init(scene, cube_mesh, checker_tex)  — load assets, compute scale
//   2. spawn(pos, yaw_deg)                  — rigid body + scene nodes
//   3. each frame:
//        set_inputs(...)                    — driver controls
//        substep(dt, world)  (×N substeps)  — physics
//        update_visuals(dt)                 — push state to scene graph
class PlayerVehicle {
public:
    bool init(Scene& scene, const Mesh& cube_mesh, const Texture& checker_tex);
    void spawn(const glm::vec3& pos, float yaw_deg);

    void set_inputs(float throttle, float brake, float steer, bool handbrake);
    void substep(float dt, const WorldCollision& world);
    void update_visuals(float dt);

    // ---- World / camera queries -------------------------------------------
    glm::vec3 position()   const { return vehicle_.position(); }
    glm::vec3 forward()    const { return vehicle_.forward(); }
    glm::vec3 right()      const { return vehicle_.right(); }
    bool      airborne()   const { return vehicle_.airborne(); }
    float     speed_kmh()  const { return vehicle_.speed_kmh(); }
    glm::vec3 linear_vel() const { return vehicle_.body().linear_vel; }

    // Debug-draw / direct rigid-body access. Prefer the typed accessors
    // above; this is for diagnostics only.
    const Vehicle& vehicle() const { return vehicle_; }

    // ---- Asset sharing for traffic AI -------------------------------------
    bool             body_loaded()         const { return body_loaded_; }
    const Mesh*      body_mesh()           const { return &body_mesh_; }
    glm::vec3        body_visual_scale()   const { return body_visual_scale_; }
    float            body_yaw_offset_deg() const;
    int              paint_count()         const { return 3; }
    const Texture*   paint(int idx)        const;

private:
    bool load_assets_();
    void create_scene_nodes_();

    Scene*         scene_       = nullptr;
    const Mesh*    cube_mesh_   = nullptr;       // fallback for missing assets
    const Texture* checker_tex_ = nullptr;

    Vehicle        vehicle_;

    // Body
    Mesh           body_mesh_;
    Texture        body_paints_[3];              // [0]=default, [1]=green, [2]=grey
    bool           body_loaded_         = false;
    glm::vec3      body_visual_scale_   {1.f};
    glm::vec3      body_visual_offset_  {0.f};

    // Wheels
    Mesh           wheel_mesh_;
    Texture        wheel_tex_;
    bool           wheel_loaded_         = false;
    float          wheel_visual_scale_   = 1.f;
    float          wheel_visible_radius_ = 0.f;
    double         wheel_spin_rad_       = 0.0;

    // Scene nodes (owned by Scene, raw refs here).
    SceneNode*     chassis_node_        = nullptr;
    SceneNode*     chassis_visual_node_ = nullptr;
    SceneNode*     wheel_nodes_[4]      = {nullptr, nullptr, nullptr, nullptr};
};

} // namespace pengine
