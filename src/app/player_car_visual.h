#pragma once

#include <array>

#include "app/player_car_catalog.h"
#include "core/transform.h"
#include "app/traffic_visual_layout.h"
#include "gfx/lighting.h"
#include "physics/vehicle.h"
#include "scene/scene.h"

namespace apricot {

class Renderer;

// Host-side visual for the player car. The body is a wheel-less cooked mesh;
// the four wheel nodes follow the real suspension lengths and the sim-owned
// steering/spin state, so playback reproduces the same wheel motion.
class PlayerCarVisual {
public:
    bool init(Renderer& renderer, Scene& scene, const VehicleTuning& tuning,
              const VehicleState& state,
              PlayerCarId initial_car = PlayerCarId::LegacyCar5);
    bool select(Scene& scene, const VehicleTuning& tuning,
                const VehicleState& state, PlayerCarId car);
    PlayerCarId active_car() const { return active_car_; }
    void sync(Scene& scene, const VehicleTuning& tuning,
              const VehicleState& previous, const VehicleState& current,
              float alpha, float headlight_level, float brake_level) const;
    // Apply after sync; fraction is already eased by the transition owner.
    void sync_driver_door(Scene& scene, float open_fraction) const;
    // 0 latches the folding top to the windshield header, 1 stows it. A car
    // with no top ignores this; see app/mistral_soft_top.h.
    void sync_soft_top(Scene& scene, float stowed) const;
    HeadlightRig headlights(const VehicleState& previous,
                            const VehicleState& current, float alpha,
                            float level) const;
    void sync_emergency(Scene& scene, uint64_t step, bool enabled) const;
    void destroy(Scene& scene);
    // Clone only scene instances; immutable mesh/texture handles are shared.
    void clone_parked(Scene& scene, PlayerCarVisual& out) const;
    void set_paint(Scene& scene, MaterialId paint);
    MaterialId paint(const Scene& scene) const {
        const auto* node=scene.get(body_node_);
        return node?node->renderable.material:kInvalidId;
    }
    const AABB& placed_body_bounds() const { return placed_body_bounds_; }
    Transform fitted_body_transform(const VehicleState& state) const {
        Transform chassis;
        chassis.position = state.position;
        chassis.rotation = state.orientation;
        return chassis * body_local_;
    }
    // sync writes this fitted/interpolated root-node transform before the
    // scene's matrix update. Reading node.world here would be one frame stale.
    const Transform* rendered_body_transform(const Scene& scene) const {
        const SceneNode* node = scene.get(body_node_);
        return node && node->visible ? &node->local : nullptr;
    }

private:
    struct Model {
        MeshId body_mesh = kInvalidId;
        MaterialId body_material = kInvalidId;
        AABB body_bounds;
        MeshId driver_door_mesh = kInvalidId;
        AABB driver_door_bounds;
        std::array<MeshId,2> soft_top_meshes{kInvalidId,kInvalidId};
        std::array<AABB,2> soft_top_bounds{};
        std::array<MeshId,6> glass_meshes{kInvalidId,kInvalidId,kInvalidId,kInvalidId,kInvalidId,kInvalidId};
        std::array<AABB,6> glass_bounds{};
        MaterialId glass_material = kInvalidId;
        std::array<MeshId, 4> lamp_meshes{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId};
        std::array<AABB, 4> lamp_bounds{};
        std::array<glm::vec3, 2> headlight_origins{};
        int headlight_profile = -1;
        bool exposed_headlights = false;
        std::array<MeshId,2> custom_wheel_meshes{kInvalidId,kInvalidId};
        std::array<AABB,2> custom_wheel_bounds{};
        std::array<float,2> custom_wheel_radii{};
    };

    bool load_model(Renderer& renderer, const PlayerCarDefinition& definition,
                    Model& out);

    std::array<Model, kPlayerCarCount> models_{};
    PlayerCarId active_car_ = PlayerCarId::LegacyCar5;
    Transform body_local_;
    AABB placed_body_bounds_;
    VehicleLampLayout lamp_layout_;
    MeshId shared_wheel_mesh_ = kInvalidId;
    MaterialId shared_wheel_material_ = kInvalidId;
    AABB shared_wheel_bounds_;
    float wheel_scale_ = 1.0f;
    float native_wheel_radius_ = 1.0f;
    NodeId body_node_ = kInvalidId;
    NodeId driver_door_node_ = kInvalidId;
    // [0] rear bow, [1] front bow: the order they hinge in.
    std::array<NodeId,2> soft_top_nodes_{kInvalidId,kInvalidId};
    std::array<NodeId,6> glass_nodes_{kInvalidId,kInvalidId,kInvalidId,kInvalidId,kInvalidId,kInvalidId};
    std::array<NodeId, kWheelCount> wheel_nodes_{
        kInvalidId, kInvalidId, kInvalidId, kInvalidId};
    MaterialId lamp_material_ = kInvalidId;
    std::array<AABB, 4> lamp_bounds_{};
    std::array<NodeId, 4> lamp_nodes_{
        kInvalidId, kInvalidId, kInvalidId, kInvalidId};
    std::array<NodeId,2> emergency_nodes_{kInvalidId,kInvalidId};
};

}  // namespace apricot
