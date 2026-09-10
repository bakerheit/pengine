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
#include "app/character_animation.h"
#include "app/presentation_budgets.h"
#include "physics/ragdoll_rig.h"
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
#include "app/weapon_pose.h"

namespace apricot {

class TrafficVisual;

// Player and ambient-person presentation using the original Pengine skeletal
// path: one bind mesh, continuously sampled bone tracks, and dual-quaternion
// skinning. Simulation stays in PlayerCharacterState/Crowd; this class only
// turns their interpolated state into GPU poses.
class CharacterVisual {
public:
    // Diagnostic views only. The --overhead QA camera sits far enough above the
    // pavement that the normal NPC budget culls every ambient character, which
    // is how that view came to show empty crossings. Normal play never calls
    // this and keeps kAmbientNpcDrawDistanceM.
    void set_npc_draw_distance(float metres) { npc_draw_distance_ = metres; }
    float npc_draw_distance() const { return npc_draw_distance_; }

    struct PoliceWeaponSocket {
        uint64_t lane_key = 0;
        uint32_t slot = 0;
        glm::mat4 hand_world{1.0f};
        uint16_t flash_ticks = 0;
    };
    bool init(const PlayerCharacterState& player);
    // `world` is the collider knocked-down pedestrians fall onto. Passing
    // nullptr is legal and means no ragdoll: bodies hold the knockdown clip's
    // last frame instead, which is what happens before the terrain exists.
    void sync(const Crowd& crowd,
              const PlayerCharacterState& previous_player,
              const PlayerCharacterState& player,
              float alpha, int64_t step, bool player_visible,
              glm::vec3 focus, float presentation_radius_m = 0.0f,
              const TerrainCollider* world = nullptr);
    // The ambient reconciliation, driven from an explicit agent list rather
    // than the live crowd. sync() calls this; a host QA check calls it directly
    // to stage a departure change at one (lane_key, slot) that the live crowd
    // cannot be asked to produce on demand. Behaviour is identical either way —
    // this is the real reconciliation, not a copy of it.
    void sync_ambient(const std::vector<PedAgent>& agents, double sim_seconds,
                      glm::vec3 focus, float presentation_radius_m,
                      const TerrainCollider* world);

    // Read-only view of one ambient rig, for host QA. Reports the per-person
    // state that must NOT survive a departure change at the same pair.
    struct AmbientRigProbe {
        int64_t generation = 0;
        std::size_t model = 0;
        bool ragdolling = false;
        bool getup_running = false;
        bool started = false;
        // The animator's accumulated time in its current clip. This is what
        // actually distinguishes a REUSED rig from a rebuilt one: `started` is
        // true again the moment a fresh rig takes its first advance, but a
        // fresh animator has barely any clip time behind it.
        float clip_time_s = 0.0f;
    };
    bool ambient_rig_probe(uint64_t lane_key, uint32_t slot,
                           AmbientRigProbe& out) const;
    // Same seam and probe for the distinct police-officer reconciliation.
    void sync_police_units(const std::vector<VehicleAgent>& units,
                           const TrafficVisual& traffic, float blend,
                           int64_t step, double sim_seconds, glm::vec3 focus,
                           float presentation_radius_m);
    bool police_rig_probe(uint64_t lane_key, uint32_t slot,
                          AmbientRigProbe& out) const;

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
    const std::vector<PoliceWeaponSocket>& police_weapon_sockets() const {
        return police_weapon_sockets_;
    }

    // Call before sync. The pose layers over on-foot animation; driver and
    // vehicle transition solvers still own their complete seated/entry poses.
    void set_player_weapon_pose(WeaponId weapon, float equip_blend,
                                float aim_blend, float recoil,
                                float reload_progress, bool reloading,
                                float pitch = 0.0f) {
        player_weapon_pose_ = {weapon, equip_blend, aim_blend, recoil,
                               reload_progress, reloading, pitch};
    }

    // --- melee ------------------------------------------------------------
    // The caller city/character_punch.h was written for. Hold the button; the
    // animator finds the rising edge, picks the fist (alternating), runs the
    // phase clock and opens the contact window mid-swing.
    // LatchedPress, not a plain bool: a press that releases inside the same
    // render frame must still throw a jab. See its comment for the measurement.
    void set_player_punch_held(bool held) { player_punch_.set(held); }
    // TRUE EXACTLY ONCE per swing, on the first frame the fist is extended.
    // Clearing on read is the one-shot hit latch: without it a single jab
    // registers on every frame the arm is out.
    bool consume_player_punch_contact() {
        const bool hit = player_punch_contact_;
        player_punch_contact_ = false;
        return hit;
    }
    bool player_punching() const { return player_animator_.punching(); }
    const char* player_anim_state() const {
        return character_anim_state_name(player_animator_.state());
    }

    std::size_t npc_count() const {
        return rigs_.size() + staff_rigs_.size() + police_rigs_.size();
    }
    std::size_t ambient_npc_count() const { return rigs_.size(); }
    std::size_t staff_count() const { return staff_rigs_.size(); }
    std::size_t police_count() const { return police_rigs_.size(); }
    int last_draw_count() const { return last_draw_count_; }
    int last_police_draw_count() const { return last_police_draw_count_; }

private:
    float npc_draw_distance_ = kAmbientNpcDrawDistanceM;

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
        // The whole registered clip set, bound to THIS skeleton. Per model, not
        // shared: the staged rigs do not agree on bone order, so a clip set
        // bound to one and sampled against another animates the wrong limbs.
        CharacterClipSet clips;
        // Foot-plant lift per clip, measured once at load. Only the locomotion
        // clips get one; a death clip's lowest vertex is the ground and lifting
        // the body to meet it would leave the corpse hovering.
        std::array<float, kCharacterClipCount> plants{};
        AABB bounds;
        Transform local;
        // Ragdoll binding for this skeleton, built at load. Optional: a model
        // whose rig will not bind still animates, it just falls with the
        // canned clip instead of with physics.
        RagdollRig ragdoll;
        int right_hand_bone = -1;
        glm::mat4 right_hand_socket{1.0f};
        bool loaded = false;
    };

    struct Rig {
        uint64_t lane_key = 0;
        uint32_t slot = 0;
        // WHICH DEPARTURE this rig belongs to. A (lane_key, slot) pair is a
        // recurring schedule slot, so one agent can retire and the next lap's
        // agent appear at the same pair with NO absent frame in between —
        // measured at 161 pedestrian swaps in 270 s on the authored city. The
        // reconciliation below matches on identity, and everything under this
        // line is per-person: an animator mid-clip, a live ragdoll, and the
        // landed pose of a get-up crossfade. Reusing a rig across a generation
        // change would stand a brand-new pedestrian up out of the previous
        // person's sprawl, somewhere they never fell.
        int64_t generation = 0;
        std::size_t model = 0;
        // Presentation time in SIM seconds, so the animator's dt comes from the
        // sim clock rather than from a wall clock this layer is not allowed to
        // read. A rig created this frame starts with dt 0.
        double last_sim_seconds = 0.0;
        bool started = false;
        CharacterAnimator animator;
        Transform world;
        Pose pose;
        // Live physics while this person is on the floor. The animator keeps
        // running underneath — it is what decides the get-up when the crowd
        // puts them back on their feet — but while `ragdolling` is set the
        // pose above comes from the solver and not from a clip.
        RagdollState ragdoll;
        bool ragdolling = false;
        // The pose the body actually landed in, decomposed, kept for the
        // length of the get-up crossfade. Empty whenever no fade is running.
        std::vector<BonePose> landed;
        float getup_fade_s = 0.0f;
    };

    struct PoliceRig {
        Rig character;
        bool in_vehicle = true;
    };

    bool load_model(const std::string& root, float height_m, Model& out);
    // Evaluate one animator sample: the incoming clip, the outgoing clip if a
    // crossfade is running, the blend, and the clip's root policy.
    void sample_pose(const Model& model, const CharacterAnimSample& sample,
                     Pose& out) const;
    // The simple case: one clip, no fade. Used where a pose is composed by a
    // solver rather than chosen by the state machine.
    void sample_clip(const Model& model, CharacterClip clip, float time,
                     Pose& out) const;
    // Drive one rig's animator from a plain input and sample the result.
    void advance_rig(Rig& rig, const Model& model,
                     const CharacterAnimInput& input, double sim_seconds,
                     const Transform& root, bool bullet_fall = false) const;
    static Transform model_transform(const Transform& root, const Model& model,
                                     float plant_offset);
    Rig create_rig(const PedAgent& agent) const;
    // No render alpha: sim_seconds already carries the interpolation fraction,
    // so the animator's dt is sub-step smooth without a second correction.
    void sync_rig(Rig& rig, const PedAgent& agent, double sim_seconds,
                  const TerrainCollider* world) const;
    void step_ragdoll(Rig& rig, const Model& model, const PedAgent& agent,
                      const TerrainCollider* world, float dt) const;
    void begin_getup(Rig& rig, const Model& model) const;
    void blend_getup(Rig& rig, const Model& model, float dt) const;
    void sync_staff(double sim_seconds, glm::vec3 focus,
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
    CharacterAnimator player_animator_;
    double player_sim_seconds_ = 0.0;
    bool player_started_ = false;
    LatchedPress player_punch_;
    bool player_punch_contact_ = false;
    bool player_visible_ = false;
    int player_right_hand_bone_ = -1;
    glm::mat4 player_right_hand_socket_{1.0f};
    PlayerWeaponPose player_weapon_pose_;
    VehicleDriverPose weapon_pose_scratch_;
    VehicleDriverPose driver_pose_;
    Pose boat_standing_pose_;
    Transform boat_standing_world_;
    bool driver_visible_ = false;
    std::vector<Rig> rigs_;
    std::vector<Rig> staff_rigs_;
    std::vector<PoliceRig> police_rigs_;
    std::vector<PoliceWeaponSocket> police_weapon_sockets_;
    // Scratch for sample_pose(). Members rather than locals so a frame with
    // sixty rigs on screen does not allocate two vectors sixty times.
    mutable std::vector<BonePose> parts_a_;
    mutable std::vector<BonePose> parts_b_;
    // Get-up crossfade scratch. Separate from parts_a_/parts_b_ on purpose:
    // those belong to evaluate_character_pose(), which runs INSIDE the fade.
    mutable std::vector<BonePose> fade_parts_;
    mutable std::vector<glm::mat4> fade_local_;
    mutable int last_draw_count_ = 0;
    mutable int last_police_draw_count_ = 0;
};

}  // namespace apricot
