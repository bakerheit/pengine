#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/aabb.h"
#include "core/transform.h"
#include "app/traffic_visual_layout.h"
#include "road/lane_graph.h"
#include "scene/scene.h"
#include "traffic/crowd.h"
#include "gfx/lighting.h"
#include "game/traffic_signal_damage.h"
#include "physics/vehicle.h"

namespace apricot {

class Renderer;
class TerrainCollider;

// Host-side presentation for the sim-owned Crowd. It ports the legacy
// Probable Cause wheel-less bodies + shared moving wheel setup and the old
// cantilever traffic-head layout. Identity stays (lane key, slot), so changing
// activation order never changes what model or paint a traffic car receives.
class TrafficVisual {
public:
    bool init(Renderer& renderer, Scene& scene, const LaneGraph& lanes,
              const CrowdTuning& tuning, TerrainCollider& collider);
    void sync(Scene& scene, const Crowd& crowd, const LaneGraph& lanes,
              int64_t step, float headlight_level, glm::vec3 focus,
              float presentation_radius_m = 0.0f);
    // Drop only transient traffic-car presentation. Authored signals and
    // street lamps stay resident across cutscenes and session resets.
    void clear_vehicles(Scene& scene);
    void destroy(Scene& scene);
    void step_signals(Scene& scene, TerrainCollider& collider,
                      const VehicleState& car, float dt);
    void reset_signals(Scene& scene, TerrainCollider& collider);
    struct SignalStatus {
        TrafficSignalDamage damage;
        glm::vec3 base;
        std::size_t collider_id;
        std::array<NodeId,7> nodes;
        std::array<Transform,7> standing;
    };
    SignalStatus signal_status(std::size_t index) const {
        const auto& rig = signals_.at(index);
        return {rig.damage, rig.base, rig.collider_id, rig.all, rig.standing};
    }
    // Call once before uploading headlights(), using the render camera and
    // visible sky's night level. Adds nearby static downlights to that list.
    void sync_street_lights(Scene& scene, glm::vec3 camera_position, float night_level);
    const TrafficVisualLayout& vehicle_layout(const VehicleAgent& agent) const {
        return models_[static_cast<std::size_t>(traffic_vehicle_kind(agent))].layout;
    }
    MaterialId vehicle_paint(const VehicleAgent& agent) const {
        const auto& paints=models_[static_cast<std::size_t>(traffic_vehicle_kind(agent))].paints;
        return paints[(traffic_vehicle_identity_hash(agent.lane_key,agent.slot)>>8)%paints.size()];
    }

    std::size_t car_count() const { return rigs_.size(); }
    std::size_t signal_head_count() const { return signals_.size(); }
    std::size_t street_lamp_count() const { return street_lamps_.size(); }
    std::size_t road_sign_count() const { return road_sign_count_; }
    const std::vector<TrafficSpotLight>& headlights() const { return headlights_; }

private:
    struct Model {
        MeshId mesh = kInvalidId;
        std::vector<MaterialId> paints;
        AABB bounds;
        TrafficVisualLayout layout;
        std::array<MeshId, 4> lamp_meshes{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId};
        std::array<AABB, 4> lamp_bounds{};
        std::array<glm::vec3, 4> lamp_origins{};
        int headlight_profile = -1;
        bool exposed_headlights = false;
        std::array<MeshId, 6> glass_meshes{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId, kInvalidId, kInvalidId};
        std::array<AABB, 6> glass_bounds{};
        MaterialId glass_material = kInvalidId;
        std::size_t glass_count = 0;
    };

    struct Rig {
        uint64_t lane_key = 0;
        uint32_t slot = 0;
        std::size_t model = 0;
        NodeId body = kInvalidId;
        std::array<NodeId, 4> wheels{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId};
        std::array<NodeId, 4> lamps{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId};
        std::array<NodeId, 2> emergency{
            kInvalidId, kInvalidId};
        std::array<NodeId, 6> glass{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId, kInvalidId, kInvalidId};
    };

    struct SignalRig {
        TrafficSignalDamage damage;
        glm::vec3 base{0.0f};
        std::size_t collider_id = static_cast<std::size_t>(-1);
        std::array<Transform, 7> standing{};
        uint32_t junction = 0;
        LaneRef incoming = kInvalidLane;
        NodeId red = kInvalidId;
        NodeId yellow = kInvalidId;
        NodeId green = kInvalidId;
        std::array<NodeId, 7> all{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId,
            kInvalidId, kInvalidId, kInvalidId};
    };

    struct StreetLampRig {
        std::array<NodeId, 4> nodes{};
        glm::vec3 bulb_position{0.0f};
    };

    bool load_model(Renderer& renderer, const char* mesh_path,
                    const char* const* paint_paths, std::size_t paint_count,
                    float arch_centre_y_native, float wheel_x_native,
                    float wheel_front_z_native, float wheel_rear_z_native,
                    Model& out);
    Rig create_rig(Scene& scene, const VehicleAgent& agent) const;
    void destroy_rig(Scene& scene, Rig& rig) const;
    void sync_rig(Scene& scene, Rig& rig, const VehicleAgent& agent,
                  const LaneGraph& lanes, int64_t step,
                  float headlight_level) const;
    void build_signals(Scene& scene, const LaneGraph& lanes,
                       TerrainCollider& collider);
    void build_street_lamps(Scene& scene, const LaneGraph& lanes,
                            TerrainCollider& collider);
    void build_road_controls(Scene& scene, const LaneGraph& lanes,
                             TerrainCollider& collider);

    std::array<Model, 8> models_{};
    MeshId wheel_mesh_ = kInvalidId;
    MaterialId wheel_material_ = kInvalidId;
    AABB wheel_bounds_;
    float native_wheel_radius_ = 1.0f;
    MeshId box_mesh_ = kInvalidId;
    MaterialId flat_material_ = kInvalidId;
    AABB box_bounds_;
    std::array<MeshId,5> signal_meshes_{};
    std::array<AABB,5> signal_bounds_{};
    MaterialId signal_material_ = kInvalidId;
    CrowdTuning tuning_{};
    std::vector<Rig> rigs_;
    std::vector<TrafficSpotLight> headlights_;
    std::size_t vehicle_headlight_count_ = 0;
    std::vector<SignalRig> signals_;
    std::vector<StreetLampRig> street_lamps_;
    std::array<std::array<MeshId, 3>, 3> road_sign_meshes_{};
    std::array<std::array<AABB, 3>, 3> road_sign_bounds_{};
    MeshId yield_marking_mesh_ = kInvalidId;
    AABB yield_marking_bounds_;
    std::vector<NodeId> road_control_nodes_;
    std::size_t road_sign_count_ = 0;
};

}  // namespace apricot
