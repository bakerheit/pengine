#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/aabb.h"
#include "app/vehicle_plate_visual.h"
#include "core/transform.h"
#include "app/road_sign_mesh.h"
#include "app/traffic_visual_layout.h"
#include "road/lane_graph.h"
#include "scene/scene.h"
#include "traffic/crowd.h"
#include "gfx/lighting.h"
#include "game/roadside_fixture_debris.h"
#include "game/traffic_signal_damage.h"
#include "physics/vehicle.h"

namespace apricot {

class Renderer;
class TerrainCollider;

// Host-side presentation for the sim-owned Crowd. It ports the legacy
// Probable Cause wheel-less bodies + shared moving wheel setup and the old
// cantilever traffic-head layout. Model/paint stay keyed by (lane key, slot).
// Registration also includes departure generation, so replacements get a new
// plate without depending on activation order.
class TrafficVisual {
public:
    bool init(Renderer& renderer, Scene& scene, const LaneGraph& lanes,
              const CrowdTuning& tuning, TerrainCollider& collider);
    void sync(Scene& scene, const Crowd& crowd, const LaneGraph& lanes,
              int64_t step, float headlight_level, glm::vec3 focus,
              float presentation_radius_m = 0.0f, float alpha = 1.0f);
    // Diagnostic views only. The --overhead QA camera sits far enough above the
    // road that the normal vehicle budget culls every car; raising it for that
    // view keeps traffic in a frame whose whole purpose is to show traffic.
    // Normal play never calls this and keeps kTrafficVehicleDrawDistanceM.
    void set_vehicle_draw_distance(float metres) {
        vehicle_draw_distance_ = metres;
    }
    float vehicle_draw_distance() const { return vehicle_draw_distance_; }

    // Drop only transient traffic-car presentation. Authored roadside fixtures
    // stay resident across cutscenes and session resets.
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
        RoadsideDebrisState debris;
    };
    SignalStatus signal_status(std::size_t index) const {
        const auto& rig = signals_.at(index);
        return {rig.damage, rig.base, rig.collider_id, rig.all, rig.standing,
                rig.debris};
    }
    struct RoadsideFixtureStatus {
        TrafficSignalDamage damage;
        glm::vec3 base;
        std::size_t collider_id;
        std::array<NodeId,4> nodes;
        std::array<Transform,4> standing;
        std::size_t part_count = 0;
        RoadsideDebrisState debris;
    };
    RoadsideFixtureStatus street_lamp_status(std::size_t index) const {
        const auto& rig = street_lamps_.at(index);
        return {rig.damage, rig.base, rig.collider_id, rig.nodes,
                rig.standing, rig.nodes.size(), rig.debris};
    }
    RoadsideFixtureStatus stop_sign_status(std::size_t index) const {
        const auto& rig = stop_signs_.at(index);
        RoadsideFixtureStatus out;
        out.damage = rig.damage;
        out.base = rig.base;
        out.collider_id = rig.collider_id;
        out.part_count = rig.nodes.size();
        out.debris = rig.debris;
        for (std::size_t i = 0; i < rig.nodes.size(); ++i) {
            out.nodes[i] = rig.nodes[i];
            out.standing[i] = rig.standing[i];
        }
        return out;
    }
    // Call once before uploading headlights(), using the render camera and
    // visible sky's night level. Adds nearby static downlights to that list.
    void sync_street_lights(Scene& scene, glm::vec3 camera_position, float night_level);
    const TrafficVisualLayout& vehicle_layout(const VehicleAgent& agent) const {
        return models_[static_cast<std::size_t>(traffic_vehicle_kind(agent))].layout;
    }
    Transform vehicle_body_transform(const VehicleAgent& agent) const {
        Transform chassis;
        chassis.position=agent.pos;
        chassis.rotation=glm::quat(glm::vec3{0.0f,
            std::atan2(-agent.fwd.x,-agent.fwd.z),0.0f});
        return chassis*vehicle_layout(agent).body;
    }
    PoliceOfficerVehicleLayout police_officer_vehicle_layout() const;
    const AABB& vehicle_source_bounds(const VehicleAgent& agent) const {
        return models_[static_cast<std::size_t>(traffic_vehicle_kind(agent))].bounds;
    }
    MaterialId vehicle_paint(const VehicleAgent& agent) const {
        const auto& paints=models_[static_cast<std::size_t>(traffic_vehicle_kind(agent))].paints;
        return paints[(traffic_vehicle_identity_hash(agent.lane_key,agent.slot)>>8)%paints.size()];
    }

    VehicleRegistration vehicle_registration(const VehicleAgent& agent) const;

    std::size_t car_count() const { return rigs_.size(); }
    std::size_t parked_car_count() const { return parked_rigs_.size(); }
    std::size_t signal_head_count() const { return signals_.size(); }
    std::size_t street_lamp_count() const { return street_lamps_.size(); }
    std::size_t stop_sign_count() const { return stop_signs_.size(); }
    std::size_t road_sign_count() const { return road_sign_count_; }
    const std::vector<TrafficSpotLight>& headlights() const { return headlights_; }

private:
    struct Model {
        VehiclePlateMounts plate_mounts;
        MeshId mesh = kInvalidId;
        MeshId driver_door_mesh = kInvalidId;
        AABB driver_door_bounds;
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
        int64_t generation = 0;
        VehiclePlateVisual plate;
        uint64_t lane_key = 0;
        uint32_t slot = 0;
        std::size_t model = 0;
        NodeId body = kInvalidId;
        NodeId driver_door = kInvalidId;
        std::array<NodeId, 4> wheels{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId};
        std::array<NodeId, 4> lamps{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId};
        std::array<NodeId, 2> emergency{
            kInvalidId, kInvalidId};
        std::array<NodeId, 3> snowplow_details{kInvalidId, kInvalidId, kInvalidId};
        std::array<NodeId, 6> glass{
            kInvalidId, kInvalidId, kInvalidId, kInvalidId, kInvalidId, kInvalidId};
    };

    struct SignalRig {
        TrafficSignalDamage damage;
        RoadsideDebrisState debris;
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
        TrafficSignalDamage damage;
        RoadsideDebrisState debris;
        glm::vec3 base{0.0f};
        std::size_t collider_id = static_cast<std::size_t>(-1);
        std::array<NodeId, 4> nodes{};
        std::array<Transform, 4> standing{};
        glm::vec3 bulb_position{0.0f};
    };

    struct StopSignRig {
        TrafficSignalDamage damage;
        RoadsideDebrisState debris;
        glm::vec3 base{0.0f};
        std::size_t collider_id = static_cast<std::size_t>(-1);
        std::array<NodeId, kRoadSignPartCount> nodes{};
        std::array<Transform, kRoadSignPartCount> standing{};
    };

    enum class BreakawayFixtureKind : uint8_t {
        Signal,
        StreetLamp,
        StopSign,
    };
    struct BreakawayFixtureOwner {
        BreakawayFixtureKind kind = BreakawayFixtureKind::Signal;
        std::size_t index = 0;
    };

    bool load_model(Renderer& renderer, const char* mesh_path,
                    const char* const* paint_paths, std::size_t paint_count,
                    float arch_centre_y_native, float wheel_x_native,
                    float wheel_front_z_native, float wheel_rear_z_native,
                    Model& out);
    // `kind` is passed rather than derived from the agent because an ambient
    // parked car is drawn through a stationary shell agent, and its kind comes
    // from the parked recipe (parked_vehicle_kind), not the driving one.
    Rig create_rig(Scene& scene, const VehicleAgent& agent,
                   TrafficVehicleKind kind, bool parked=false) const;
    void destroy_rig(Scene& scene, Rig& rig) const;
    // `parked`: no brake lamps. The driving rule lights them whenever the
    // controller asks for less than cruise, which a car with no controller
    // would satisfy forever.
    void sync_rig(Scene& scene, Rig& rig, const VehicleAgent& agent,
                  const LaneGraph& lanes, int64_t step,
                  float headlight_level, float alpha,
                  bool parked = false) const;
    void build_signals(Scene& scene, const LaneGraph& lanes,
                       TerrainCollider& collider);
    void build_street_lamps(Scene& scene, const LaneGraph& lanes,
                            TerrainCollider& collider);
    void build_road_controls(Scene& scene, const LaneGraph& lanes,
                             TerrainCollider& collider);

    Renderer* plate_renderer_ = nullptr;
    MaterialId plate_material_ = kInvalidId;
    std::map<uint64_t,city::StateId> registration_states_;
    std::array<Model, 9> models_{};
    std::array<MeshId, 3> snowplow_detail_meshes_{};
    std::array<AABB, 3> snowplow_detail_bounds_{};
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
    float vehicle_draw_distance_ = kTrafficVehicleDrawDistanceM;
    std::vector<Rig> rigs_;
    // Ambient kerbside parked cars (PENG-48), keyed like rigs_ on
    // (lane_key, slot) and reconciled the same way against
    // Crowd::ambient_parked(), which the crowd keeps sorted on that pair.
    std::vector<Rig> parked_rigs_;
    std::vector<TrafficSpotLight> headlights_;
    std::size_t vehicle_headlight_count_ = 0;
    std::vector<SignalRig> signals_;
    std::vector<StreetLampRig> street_lamps_;
    std::vector<StopSignRig> stop_signs_;
    std::vector<BreakawayFixtureOwner> breakaway_fixture_owners_;
    std::array<std::array<MeshId, kRoadSignPartCount>, 3> road_sign_meshes_{};
    std::array<std::array<AABB, kRoadSignPartCount>, 3> road_sign_bounds_{};
    MeshId yield_marking_mesh_ = kInvalidId;
    AABB yield_marking_bounds_;
    std::vector<NodeId> road_control_nodes_;
    std::size_t road_sign_count_ = 0;
};

}  // namespace apricot
