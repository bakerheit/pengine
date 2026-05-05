#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "physics/character_controller.h"
#include "render/animation.h"
#include "render/skeleton.h"
#include "render/skinned_mesh.h"
#include "render/texture.h"

namespace pengine {

class AudioEngine;
class Camera;
class Input;
class Scene;
class SceneNode;
class Shader;
class SpringArm;
class WorldCollision;

// Player avatar: skinned-mesh character + scene nodes + walk-cycle animation
// + on-foot controller + footstep audio + health/respawn state.
//
// Renders its own skinned mesh out-of-band (Scene::draw can't skin). The gun
// is rendered separately by Weapons using the right-hand bone matrix this
// class exposes.
class Player {
public:
    Player() = default;
    Player(const Player&)            = delete;
    Player& operator=(const Player&) = delete;

    bool init(Scene& scene, const std::string& assets_root,
              const glm::vec3& spawn);
    void shutdown();

    // On-foot input + spring-arm + character controller + camera + footsteps.
    // mouse_dx/mouse_dy are pre-gated by mouse-capture state in the caller.
    // Returns true if the fire button was pressed this tick — caller is
    // responsible for invoking Weapons::fire.
    bool update_on_foot(float dt, const Input& input, Camera& camera,
                        SpringArm& spring, const WorldCollision& world_col,
                        AudioEngine& audio,
                        float mouse_dx, float mouse_dy);

    // Per-frame animation sampling + scene-node sync. Runs every frame
    // regardless of mode so anims keep advancing while in the vehicle.
    void update_pose(double dt, double world_time);

    // Initial pose sync — called once after init() before the first frame.
    void sync_scene_to_initial_pose();

    // Skinned-mesh draw. Caller passes a fallback texture used when our
    // diffuse asset failed to load; we don't bind anything else.
    void render(Shader& skinned, const glm::mat4& view_proj,
                const glm::vec3& cam_pos, const Texture& fallback_tex) const;

    // Toggle the scene-graph visibility for vehicle entry/exit.
    void set_visible(bool v);

    // Accessors used by Application + cross-system orchestration.
    CharacterController&       controller()       { return character_; }
    const CharacterController& controller() const { return character_; }
    glm::vec3 feet_position()  const { return character_.feet_position(); }
    glm::vec3 eye_position()   const { return character_.eye_position(); }

    bool      armed() const { return armed_; }
    bool      sprinting() const { return sprinting_; }

    float     health() const { return health_; }
    void      apply_damage(float dmg);
    bool      is_dead() const { return health_ <= 0.f; }
    void      respawn();    // teleport to spawn + refill health
    void      set_spawn(const glm::vec3& s) { spawn_ = s; }

    // Yaw used by minimap / debug logs (degrees, world-space).
    float     facing_yaw_deg() const { return character_facing_yaw_deg_; }

    // World-space transform that places the right hand bone, ready for
    // Weapons::render. Returns identity if skinning failed or the bone
    // wasn't found — call has_right_hand() to gate.
    glm::mat4 right_hand_world_xform() const;
    bool      has_right_hand() const {
        return character_skinned_ && right_hand_bone_idx_ >= 0;
    }

    // True once the skinned mesh + skin matrices are ready to render.
    bool      skinned_ready() const {
        return character_skinned_ && !char_skin_matrices_.empty();
    }
    bool      visible() const;

private:
    void compute_procedural_walk_pose(float phase, bool moving);
    void sync_character_scene();

    Scene*     scene_                 = nullptr;
    SceneNode* character_node_        = nullptr;  // pose root: feet + facing
    SceneNode* character_visual_node_ = nullptr;  // child: model offset + scale

    SkinnedMesh skinned_mesh_;
    Skeleton    skeleton_;
    Animation   anim_walk_;          // walk loop (unarmed)
    Animation   anim_sprint_;        // sprint loop (unarmed)
    Animation   anim_idle_;          // breathing idle (unarmed)
    Animation   anim_pistol_walk_;   // walk loop (armed)
    Animation   anim_pistol_run_;    // run loop  (armed)
    Animation   anim_pistol_idle_;   // idle loop (armed)
    Texture     texture_;

    CharacterController character_;

    bool      character_skinned_ = false;
    bool      sprinting_         = false;
    bool      armed_             = false;

    int       right_hand_bone_idx_   = -1;
    glm::mat4 right_hand_bind_world_ {1.f};

    float     character_facing_yaw_deg_ = -90.f;
    glm::vec3 character_model_offset_   {0.f};
    float     character_model_scale_    = 1.f;
    double    walk_phase_               = 0.0;

    std::vector<glm::mat4> char_local_poses_;
    std::vector<glm::mat4> char_skin_matrices_;

    // Cached limb-bone indices, resolved after skeleton load. Used by the
    // procedural walk-cycle pose generator (the asset's baked animation is
    // a static pose, so we drive these directly).
    struct WalkBones {
        int left_upleg = -1,  right_upleg = -1;
        int left_leg   = -1,  right_leg   = -1;
        int left_arm   = -1,  right_arm   = -1;
        int spine      = -1;
    } walk_bones_;

    // Time since last on-foot footstep sound. Reset when stationary so
    // the next forward step plays after a normal cadence (rather than
    // immediately if the player happened to stop mid-stride).
    float     footstep_timer_ = 0.f;
    float     health_         = 100.f;
    glm::vec3 spawn_          {0.f};
};

} // namespace pengine
