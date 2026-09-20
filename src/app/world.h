#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "gfx/renderer.h"
#include "gfx/road_meshes.h"
#include "physics/terrain_collider.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "scene/scene.h"
#include "terrain/streamer.h"
#include "traffic/crowd.h"
#include "city/building_access.h"
#include "city/interior_streaming.h"
#include "city/residential_doors.h"
#include "city/quickbite_doors.h"
#include "city/skyscraper_window_lighting.h"

namespace apricot {

struct AircraftState;
struct HelicopterState;
class WreckExplosion;

// Render-only handle for one authored window pane. The address is stable city
// data; NodeId is only its current scene slot. lod keeps the last near pattern
// while this building is far away, then hands it back unchanged on approach.
struct SkyscraperWindowRuntime {
    NodeId node = kInvalidId;
    city::SkyscraperWindowAddress address{};
    bool hide_when_dark = true;
    glm::vec2 building_position{0.0f};
    city::SkyscraperWindowLodState lod{};
    city::SkyscraperWindowLight target_light{};
    city::SkyscraperWindowPresentationState presentation{};
};

// The host half of terrain streaming: build, upload, deliver, free.
//
// The POLICY is not here. Which chunks should exist, at what level, in what
// order, and when to let go of them all live in terrain/Streamer, which is
// sim-side and headless-testable and has a suite that drives it around a world
// for two thousand steps. What is left over is the part that genuinely needs a
// GL context — turning a ChunkMesh into a MeshId and giving the MeshId back —
// and that is all this file does.
//
// It replaces app/demo_scene.{h,cpp}, which was a 420 m disc and 1400 boxes and
// said in its own header that it was meant to be deleted.
//
// SINGLE THREADED, ON PURPOSE, FOR NOW. build_chunk() is pure and thread safe
// precisely so this could farm the work out, and the budgets exist so that not
// doing so is survivable: steady state at 40 m/s needs about 5.6 chunks a
// second against a ceiling of far more. Threading it is a real follow-up and
// not a pretend one, but it is not this ticket, and a thread pool added
// speculatively would be a thread pool nobody had measured the need for.
class World {
public:
    struct Stats {
        // This frame.
        int chunks_built = 0;
        int quads_built = 0;
        int chunks_refitted = 0;
        int chunks_evicted = 0;
        int meshes_freed = 0;
        int instances_activated = 0;
        bool budget_exhausted = false;

        // Right now.
        std::size_t resident_chunks = 0;
        std::size_t resident_by_lod[kMaxChunkLod + 1] = {0, 0, 0, 0};
        std::size_t live_meshes = 0;
        std::size_t mesh_bytes = 0;
    };

    bool init(Renderer& renderer, uint64_t seed, const StreamerConfig& cfg);

    // Drop every node and every mesh. Must run while the GL context is alive.
    void shutdown(Scene& scene, Renderer& renderer);

    // One frame of residency. Steps the streamer, then builds and uploads
    // everything it asked for and frees everything it handed back.
    void update(Scene& scene, Renderer& renderer, glm::vec3 focus,
                StepMode mode = StepMode::Budgeted);

    // Load the prime ring with the budgets off, and do not return until the
    // streamer says the world under `focus` is ready.
    //
    // This is the cold start and the mission teleport. Without it the player is
    // dropped into a hole and the world arrives around them over the following
    // second, which docs/design/pinatty.md calls a blocker in a way steady
    // state streaming is not. Returns how many fill steps it took.
    //
    // IT TAKES NO CLOCK AND REPORTS NO TIME. App owns the program's only wall
    // clock; the caller times this and resets the frame clock afterwards, or
    // the fill is charged to the next frame as dropped sim time.
    int fill(Scene& scene, Renderer& renderer, glm::vec3 focus);

    // Bake `spines` into ribbons, upload them and put them in the scene.
    //
    // App passes city::map_spines() — Pinatty's authored road network, 99
    // spines and 53.6 km of centreline. This stays a PARAMETER rather than a
    // call into src/city/ so the whole chain can be driven from a test with a
    // different network, which is exactly what tests/city_roads_tests.cpp does.
    //
    // An empty spine list is a success and produces no geometry. Returns false
    // only if a bake that HAD geometry failed to reach the GPU.
    bool set_roads(Renderer& renderer, Scene& scene, TerrainCollider& collider,
                   const std::vector<RoadSpine>& spines);

    // Put the authored downtown sites and Camber Point airport into the world,
    // and register the same solid pieces with vehicle physics.
    bool set_starting_area(Renderer& renderer, Scene& scene,
                           TerrainCollider& collider);
    void sync_bank_vault(Scene& scene, TerrainCollider& collider, float openness);
    void reset_session_objects(Scene& scene, TerrainCollider& collider);
    void step_house_doors(Scene& scene,TerrainCollider& collider,
        const PlayerCharacterState* actor,const CharacterTuning& tuning,
        const InputFrame& input,float dt);
    const std::vector<city::ResidentialDoor>& house_doors() const { return house_doors_; }
    const std::vector<HouseDoorState>& house_door_states() const { return house_door_states_; }
    // Aircraft meshes use local +Z for the nose. Sync between simulation
    // queries; this preserves the explicit collision toggle, even on a crash.
    void sync_aircraft(Scene& scene, TerrainCollider& collider,
                       const AircraftState& state);
    // Temporarily exclude all aircraft boxes from flight/camera queries, then
    // restore them. Crashed aircraft remain solid unless explicitly disabled.
    void enable_aircraft_collision(TerrainCollider& collider, bool enabled);
    // The Halberd gunship shares the aircraft's +Z-nose frame, but its rotor
    // is its own node: the disc is cooked recentred on its hub so this can
    // spin it about Y without the airframe following it round.
    void sync_helicopter(Scene& scene, TerrainCollider& collider,
                         const struct HelicopterState& state);
    void enable_helicopter_collision(TerrainCollider& collider, bool enabled);
    // The wreck's fireball. The pool is fixed and always resident, so a blast
    // never allocates on the frame the player is watching it happen.
    void sync_wreck_explosion(Scene& scene, const WreckExplosion& blast);
    void sync_boat(Scene& scene, TerrainCollider& collider, const struct BoatState& state);
    void enable_boat_collision(TerrainCollider& collider, bool enabled);
    const Transform* rendered_boat_transform(const Scene& scene) const {
        const auto* node=scene.get(boat_node_);
        return node && node->visible?&node->local:nullptr;
    }

    // Advance the active traffic set around `focus` at the authoritative sim
    // step. Traffic is lane-graph simulation; its GPU nodes live in the app's
    // TrafficVisual and never feed back into these decisions.
    void step_traffic(int64_t step, const VehicleState& player,
                      const OnFootTrafficHazard* on_foot_player = nullptr);
    void set_police_context(
            int wanted_level, glm::vec3 target,
            const std::vector<VisiblePoliceIdentity>& visible_police = {}) {
        crowd_.set_police_context(wanted_level, target, visible_police);
    }
    void set_police_officer_context(bool target_on_foot, bool target_armed,
                                    glm::vec2 target_velocity,
                                    const TerrainCollider* collider) {
        crowd_.set_police_officer_context(target_on_foot,target_armed,
                                          target_velocity,collider);
    }
    void set_police_vehicle_tuning(const VehicleTuning& tuning) {
        crowd_.set_police_vehicle_tuning(tuning);
    }
    void set_police_officer_vehicle_layout(const PoliceOfficerVehicleLayout& layout) {
        crowd_.set_police_officer_vehicle_layout(layout);
    }
    bool report_police_vehicle_hit(VisiblePoliceIdentity cruiser) {
        return crowd_.report_police_vehicle_hit(cruiser);
    }
    bool resolve_traffic_collision(VehicleState& player,
                                   float player_half_width_m,
                                   float player_half_length_m,
                                   float player_mass_kg,
                                   float player_body_damage_gain=1.f);

    const Streamer& streamer() const { return streamer_; }
    const RoadMeshes& roads() const { return roads_; }
    const LaneGraph& lanes() const { return lane_graph_; }
    void set_snowplow_service(bool active) { crowd_.set_snowplow_service(active); }
    const Crowd& traffic() const { return crowd_; }
    PedShotHit shoot_ped(glm::vec3 origin, glm::vec3 direction,
                        float max_distance, int64_t step) {
        return crowd_.shoot_ped(origin,direction,max_distance,step);
    }
    PedShotHit punch_ped(glm::vec3 origin, glm::vec3 direction,
                         float reach_m, int64_t step) {
        return crowd_.punch_ped(origin,direction,reach_m,step);
    }
    bool take_traffic_vehicle(uint64_t key, uint32_t slot, VehicleAgent& out) {
        return crowd_.take_vehicle(key,slot,out);
    }
    void set_parked_vehicle_poses(std::vector<glm::vec3> positions) {
        crowd_.set_parked_vehicle_poses(std::move(positions));
    }
    const CrowdTuning& traffic_tuning() const { return crowd_tuning_; }
    const CanopyLightRig& canopy_lights() const { return canopy_lights_; }
    const std::vector<StaticBox>& precipitation_cover() const { return precipitation_cover_; }
    const Stats& stats() const { return stats_; }
    const std::vector<glm::vec3>& miandi_gas_station_lights() const { return miandi_gas_station_lights_; }
    void sync_burgerpiz_parking_lamps(Scene& scene,float night_level);
    const std::vector<glm::vec3>& burgerpiz_parking_lights() const { return burgerpiz_parking_lights_; }
    const std::vector<glm::vec3>& burgerpiz_lights() const { return burgerpiz_lights_; }
    const std::vector<glm::vec3>& residential_lights() const { return residential_lights_; }
    bool inside_authored_interior(glm::vec3 position,
                                  float margin_m = 0.0f) const;
    std::size_t interior_streaming_volume_count() const {
        return interior_streaming_volumes_.size();
    }

    struct SkyscraperWindowStats {
        std::size_t panes = 0;
        std::size_t lit = 0;
        std::size_t office = 0;
        std::size_t residential = 0;
        std::size_t mixed = 0;
        std::size_t dynamic_lod = 0;
        std::size_t static_lod = 0;
    };
    // Syncs only emissive facade state. It creates no tiled point/spot lights;
    // thousands of overlapping room lights would blow the forward-light grid.
    void sync_skyscraper_window_lights(Scene& scene, uint64_t session_seed,
        uint64_t absolute_step, float visible_time_of_day, float darkness,
        float time_of_day_per_step, glm::vec3 viewer_position);
    const SkyscraperWindowStats& skyscraper_window_stats() const {
        return skyscraper_window_stats_;
    }

private:
    // Terrain shares ONE material across every chunk and every level, so the
    // batcher collapses the whole visible ring into a handful of draws keyed by
    // mesh. Per-chunk materials would give every chunk its own batch key and
    // instancing would quietly stop finding runs.
    Streamer streamer_{0};
    ScenePrototypes proto_;
    RoadMeshes roads_;
    RoadGraph road_graph_;
    LaneGraph lane_graph_;
    Crowd crowd_;
    AmbientTuning ambient_tuning_;
    CrowdTuning crowd_tuning_;
    MeshId start_box_mesh_ = kInvalidId;
    MeshId start_decal_mesh_ = kInvalidId;
    MeshId start_billboard_mesh_ = kInvalidId;
    MeshId start_rounded_box_mesh_ = kInvalidId;
    MeshId start_cylinder_mesh_ = kInvalidId;
    MeshId start_gable_mesh_ = kInvalidId;
    MeshId museum_amphora_mesh_=kInvalidId;
    std::array<MeshId,4> museum_art_meshes_{kInvalidId,kInvalidId,kInvalidId,kInvalidId};
    // Atlas-cell quads and exhibit meshes for the Pinatty Museum interior,
    // released together because they are uploaded together.
    std::vector<MeshId> museum_meshes_;
    MeshId airport_garage_ramp_mesh_ = kInvalidId;
    std::vector<glm::vec3> residential_lights_;
    std::vector<city::InteriorStreamingVolume> interior_streaming_volumes_;
    std::vector<StaticBox> precipitation_cover_;
    std::vector<city::ResidentialDoor> house_doors_;
    std::vector<HouseDoorState> house_door_states_;
    std::vector<std::vector<NodeId>> house_door_nodes_;
    std::vector<std::size_t> house_door_colliders_;
    std::vector<city::QuickbiteDoor> quickbite_doors_;
    std::vector<HouseDoorState> quickbite_door_states_;
    std::vector<std::vector<NodeId>> quickbite_door_nodes_;
    std::vector<std::size_t> quickbite_door_colliders_;
    MeshId pawn_guitar_mesh_=kInvalidId;
    city::BuildingAccessBake access_layout_; // metadata only, no retained road meshes
    std::vector<MeshId> miandi_gas_station_meshes_;
    std::vector<glm::vec3> miandi_gas_station_lights_;
    std::vector<MeshId> kyjhi_phonebooth_meshes_;
    std::vector<MeshId> burgerpiz_meshes_;
    std::vector<glm::vec3> burgerpiz_lights_;
    std::vector<glm::vec3> burgerpiz_parking_lights_;
    std::vector<NodeId> burgerpiz_parking_lens_nodes_;
    std::vector<MeshId> airport_aircraft_meshes_;
    MeshId moored_boat_mesh_ = kInvalidId;
    NodeId boat_node_ = kInvalidId;
    std::vector<std::size_t> boat_colliders_;
    bool boat_collision_enabled_ = true;
    // Non-owning node list: start_nodes_ remains the sole removal owner.
    std::vector<NodeId> airport_aircraft_nodes_;
    std::vector<std::size_t> airport_aircraft_colliders_;
    bool airport_aircraft_collision_enabled_ = true;
    std::vector<MeshId> halberd_helicopter_meshes_;
    std::vector<NodeId> halberd_helicopter_nodes_;   // airframe only
    NodeId halberd_helicopter_rotor_node_ = kInvalidId;
    std::vector<std::size_t> halberd_helicopter_colliders_;
    bool halberd_helicopter_collision_enabled_ = true;
    MeshId wreck_particle_mesh_ = kInvalidId;
    MaterialId wreck_particle_material_ = kInvalidId;
    std::vector<NodeId> wreck_particle_nodes_;
    std::vector<NodeId> start_nodes_;
    std::vector<SkyscraperWindowRuntime> skyscraper_windows_;
    SkyscraperWindowStats skyscraper_window_stats_;
    uint64_t skyscraper_window_sync_bucket_ = UINT64_MAX;
    uint64_t skyscraper_window_sync_seed_ = 0;
    float skyscraper_window_sync_darkness_ = -1.0f;
    float skyscraper_window_sync_time_ = -1.0f;
    float skyscraper_window_sync_time_rate_ = 0.0f;
    uint64_t skyscraper_window_presentation_step_ = UINT64_MAX;
    std::vector<NodeId> bank_vault_nodes_;
    std::size_t bank_vault_collider_ = static_cast<std::size_t>(-1);
    float bank_vault_pose_ = -1.0f;
    CanopyLightRig canopy_lights_;
    uint64_t seed_ = 0;

    std::vector<MeshId> released_scratch_;
    Stats stats_;

};

}  // namespace apricot
