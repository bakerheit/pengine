#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"
#include "core/skeletal_animation.h"
#include "core/transform.h"
#include "game/character.h"
#include "gfx/camera.h"
#include "gfx/lighting.h"
#include "gfx/shader.h"
#include "gfx/skinned_mesh.h"
#include "gfx/sky_env.h"
#include "gfx/texture.h"
#include "traffic/crowd.h"
#include "app/vehicle_driver_pose.h"
#include "app/vehicle_transition_pose.h"
#include "app/boat_driver_pose.h"

namespace apricot {

class TrafficVisual;

// Player and ambient-person presentation using the original Pengine skeletal
// path: one bind mesh, continuously sampled bone tracks, and dual-quaternion
// skinning. Simulation stays in PlayerCharacterState/Crowd; this class only
// turns their interpolated state into GPU poses.
class CharacterVisual {
public:
    bool init(const PlayerCharacterState& player);
    void sync(const Crowd& crowd,
              const PlayerCharacterState& previous_player,
              const PlayerCharacterState& player,
              float alpha, int64_t step, bool player_visible,
              glm::vec3 focus, float presentation_radius_m = 0.0f);
    // Call after sync and car sync. Uses the car's fitted render transform,
    // never raw physics position. nullptr/other cars/exit suppress the driver.
    void sync_driver(PlayerCarId car, bool occupied,
                     const Transform* rendered_body);
    void prepare_boat_transition(const PlayerCharacterState& shore);
    void sync_boat_driver(const Transform* body,bool occupied,float steering,float time);
    void sync_boat_transition(const Transform* body,const BoatTransitionState& state,float alpha);
    void sync_transition(PlayerCarId car, const Transform* body,
                         const VehicleTransitionState& state, float alpha);
    // Uses the exact fitted traffic body that drives the cruiser's door.
    // One stable officer rig follows its unit through every occupancy phase.
    void sync_police(const Crowd& crowd, const TrafficVisual& traffic,
                     float alpha, int64_t step, glm::vec3 focus,
                     float presentation_radius_m = 0.0f);
    void render(const Camera& camera, const SkyEnv& environment,
                const HeadlightRig& headlights,
                const CanopyLightRig& canopy_lights) const;
    void destroy();

    // Rigid palm socket in world metres. Available only for the on-foot rig;
    // follows the sampled hand pose, never a guessed player-root offset.
    bool player_right_hand_transform(glm::mat4& out) const;

    std::size_t npc_count() const {
        return rigs_.size() + staff_rigs_.size() + police_rigs_.size();
    }
    std::size_t ambient_npc_count() const { return rigs_.size(); }
    std::size_t staff_count() const { return staff_rigs_.size(); }
    std::size_t police_count() const { return police_rigs_.size(); }
    int last_draw_count() const { return last_draw_count_; }
    int last_police_draw_count() const { return last_police_draw_count_; }

private:
    struct Pose {
        std::vector<glm::mat4> local;
        std::vector<glm::mat4> skin;
        std::vector<glm::vec4> dual_real;
        std::vector<glm::vec4> dual_part;
    };

    struct Model {
        SkinnedMesh mesh;
        Texture texture;
        Skeleton skeleton;
        Animation idle;
        Animation walk;
        Animation sprint;
        AABB bounds;
        Transform local;
        float walk_plant = 0.0f;
        float sprint_plant = 0.0f;
        bool loaded = false;
    };

    struct Rig {
        uint64_t lane_key = 0;
        uint32_t slot = 0;
        std::size_t model = 0;
        int64_t last_step = 0;
        float walk_time = 0.0f;
        Transform world;
        Pose pose;
    };

    struct PoliceRig {
        Rig character;
        bool in_vehicle = true;
    };

    bool load_model(const std::string& root, float height_m, Model& out);
    static void sample_pose(const Model& model, const Animation& animation,
                            float time, Pose& out);
    static Transform model_transform(const Transform& root, const Model& model,
                                     float plant_offset);
    Rig create_rig(const PedAgent& agent, int64_t step) const;
    void sync_rig(Rig& rig, const PedAgent& agent, float alpha,
                  int64_t step) const;
    void sync_staff(int64_t step, float alpha, glm::vec3 focus,
                    float presentation_radius_m);
    bool draw_model(const Model& model, const Pose& pose,
                    const Transform& world, const Camera& camera,
                    bool cull_bind_bounds = true) const;

    Shader shader_;
    Model player_model_;
    std::array<Model, 18> npc_models_{};
    std::array<Model, 2> police_models_{};
    Pose player_pose_;
    Transform player_world_;
    float player_walk_time_ = 0.0f;
    float last_player_distance_ = 0.0f;
    bool player_visible_ = false;
    int player_right_hand_bone_ = -1;
    glm::mat4 player_right_hand_socket_{1.0f};
    VehicleDriverPose driver_pose_;
    Pose boat_standing_pose_;
    Transform boat_standing_world_;
    bool driver_visible_ = false;
    std::vector<Rig> rigs_;
    std::vector<Rig> staff_rigs_;
    std::vector<PoliceRig> police_rigs_;
    mutable int last_draw_count_ = 0;
    mutable int last_police_draw_count_ = 0;
};

}  // namespace apricot
