#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "app/player_car_catalog.h"
#include "app/plow_kit_mesh.h"
#include "game/vehicle_paint.h"
#include "app/vehicle_plate_visual.h"
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
    const VehicleRegistration& registration() const { return plate_.registration; }
    void set_registration(Scene& scene, const VehicleRegistration& registration);
    // snow_load: the car's own snow (game/vehicle_snow_load.h). Negative keeps
    // the world snow path, which is what the tool labs use.
    void sync(Scene& scene, const VehicleTuning& tuning,
              const VehicleState& previous, const VehicleState& current,
              float alpha, float headlight_level, float brake_level,
              float snow_load = -1.0f) const;
    // Apply after sync; fraction is already eased by the transition owner.
    void sync_driver_door(Scene& scene, float open_fraction) const;
    void sync_passenger_door(Scene& scene, float open_fraction) const;
    // 0 latches the folding top to the windshield header, 1 stows it. A car
    // with no top ignores this; see app/mistral_soft_top.h.
    void sync_soft_top(Scene& scene, float stowed) const;
    HeadlightRig headlights(const VehicleState& previous,
                            const VehicleState& current, float alpha,
                            float level) const;
    void sync_emergency(Scene& scene, uint64_t step, bool enabled) const;
    // PLOW TRUCKS. The blade's lift, 0 down to 1 raised, as the sim steps it;
    // sync() poses the kit from it. sync() leaves every lamp in the kit dark,
    // which is what a parked copy keeps; the driven truck then lights them.
    bool has_plow() const { return models_[static_cast<std::size_t>(active_car_)].plow; }
    void set_plow_raised(float raised) { plow_raised_ = raised; }
    void sync_plow_lights(Scene& scene, uint64_t step, bool light_bar,
                          float headlight_level) const;
    void destroy(Scene& scene);
    // Clone only scene instances; immutable mesh/texture handles are shared.
    void clone_parked(Scene& scene, PlayerCarVisual& out) const;
    // PAINT. A car wears its factory material — the catalog atlas after
    // select(), or the traffic livery it was taken in — until it is resprayed,
    // when it wears a paintable material from the App's pool. This class holds
    // only plain data about that and points nodes at materials. It never names
    // the pool: the tool labs compile this file without it. select() resets all
    // of it; clone_parked() carries it to the parked copy.
    void set_factory_paint(Scene& scene, MaterialId material, uint8_t paint_base);
    void apply_respray(Scene& scene, MaterialId material, PaintColor colour);
    void clear_respray(Scene& scene);
    // Points the paint at a material without changing what the car wears: the
    // booth preview and the spray reveal. Restore with committed paint after.
    void preview_body_material(Scene& scene, MaterialId material);
    const std::optional<PaintColor>& respray() const { return respray_; }
    uint8_t paint_base() const { return paint_base_; }
    MaterialId factory_material() const { return factory_material_; }
    // The atlas the car wears: its paint base's (app/vehicle_paint_catalog.h).
    const char* worn_atlas() const;
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
        VehiclePlateMounts plate_mounts;
        MeshId body_mesh = kInvalidId;
        MaterialId body_material = kInvalidId;
        AABB body_bounds;
        MeshId driver_door_mesh = kInvalidId;
        AABB driver_door_bounds;
        MeshId passenger_door_mesh = kInvalidId;
        AABB passenger_door_bounds;
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
        bool plow = false;
        std::array<MeshId, kPlowPartCount> plow_meshes{};
        std::array<AABB, kPlowPartCount> plow_bounds{};
        glm::vec3 plow_pivot{0.0f};
        float plow_lift_radians = 0.0f;
        glm::vec3 plow_paint{1.0f};
        std::array<glm::vec3, 2> plow_lamps{};  // source space, -x then +x
    };

    bool load_model(Renderer& renderer, const PlayerCarDefinition& definition,
                    Model& out);
    // Fits the plow kit to the base truck's cooked body and uploads it.
    bool load_plow_kit(Renderer& renderer, const PlayerCarDefinition& definition,
                       Model& out);
    void sync_plow(Scene& scene, const Transform& body) const;
    // Every node that samples the body atlas: body, doors, soft top, the four
    // lamps and any custom wheels. Emergency nodes and glass copy the body
    // renderable in sync().
    void point_paint_nodes(Scene& scene, MaterialId material);
    std::optional<PaintColor> respray_;
    uint8_t paint_base_ = 0;
    MaterialId factory_material_ = kInvalidId;

    std::array<Model, kPlayerCarCount> models_{};
    Renderer* plate_renderer_ = nullptr;
    MaterialId plate_material_ = kInvalidId;
    VehiclePlateVisual plate_;
    uint64_t registered_car_key_ = 0;
    bool registration_issued_ = false;
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
    NodeId passenger_door_node_ = kInvalidId;
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
    std::array<NodeId, kPlowPartCount> plow_nodes_{};
    MaterialId plow_gloss_material_ = kInvalidId;
    MaterialId plow_matte_material_ = kInvalidId;
    float plow_raised_ = 0.0f;
};

}  // namespace apricot
