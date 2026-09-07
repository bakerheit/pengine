#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "physics/vehicle_damage.h"
#include "scene/scene.h"

namespace apricot {

class Crowd;
class Renderer;
struct VehicleMechanicalState;
class TerrainCollider;
struct VehicleState;
struct VehicleTuning;

// Bounded host-side marks left by damaged vehicle systems. Damage decides
// WHAT leaks in the sim-side helper; this class only turns those sources into
// short-lived coolant, oil and fuel spots on the ground.
class VehicleEffects {
public:
    bool init(Renderer& renderer);
    void step(Scene& scene, const TerrainCollider& collider, uint64_t step,
              const VehicleState& player, const VehicleTuning& player_tuning,
              const Crowd& traffic);
    void destroy(Scene& scene);

    std::size_t mark_count() const { return marks_.size(); }

private:
    struct Mark {
        NodeId node = kInvalidId;
        uint64_t owner = 0;
        VehicleFluidKind kind = VehicleFluidKind::Coolant;
        glm::vec2 position_xz{0.0f};
        glm::vec3 surface_position{0.0f};
        glm::vec3 surface_normal{0.0f, 1.0f, 0.0f};
        float spin_radians = 0.0f;
        float radius = 0.0f;
        uint64_t expires_step = 0;
    };

    void emit_vehicle(Scene& scene, const TerrainCollider& collider,
                      uint64_t step, uint64_t owner,
                      const VehicleDamageState& damage, const VehicleMechanicalState& mechanical,
                      glm::vec3 centre,
                      glm::vec2 right_xz, glm::vec2 forward_xz,
                      float half_width, float half_length);
    void emit_mark(Scene& scene, const TerrainCollider& collider,
                   uint64_t step, uint64_t owner,
                   const VehicleFluidLeak& leak, glm::vec2 position_xz,
                   float source_y);

    MeshId mesh_ = kInvalidId;
    MaterialId material_ = kInvalidId;
    AABB bounds_;
    std::vector<Mark> marks_;
};

}  // namespace apricot
