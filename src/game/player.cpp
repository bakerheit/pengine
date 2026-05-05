#include "game/player.h"

#include <algorithm>
#include <cmath>

#include <SDL.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "audio/audio_engine.h"
#include "core/log.h"
#include "platform/input.h"
#include "physics/world_collision.h"
#include "render/camera.h"
#include "render/shader.h"
#include "render/spring_arm.h"
#include "scene/scene.h"
#include "scene/scene_node.h"
#include "world/road_grid.h"

namespace pengine {

bool Player::init(Scene& scene, const std::string& assets_root,
                  const glm::vec3& spawn) {
    scene_ = &scene;
    spawn_ = spawn;
    character_.teleport(spawn);

    // Skinned mesh + skeleton + walk animation. Mandatory trio; failing any
    // of these falls back to the cube renderable below.
    character_skinned_ =
        load_skinned_emesh(assets_root + "/models/characters/character_01.emesh",
                           skinned_mesh_) &&
        skeleton_.load (assets_root + "/models/characters/character_01.eskel") &&
        anim_walk_.load(assets_root + "/models/characters/walking.eanim",
                        skeleton_);
    if (!character_skinned_)
        PE_WARN("Falling back to cube character (skinning load failed)");

    // Sprint loop is optional: if it fails to load we just stay on the walk
    // anim while shift is held (movement still speeds up).
    if (character_skinned_ &&
        !anim_sprint_.load(assets_root + "/models/characters/sprint.eanim",
                           skeleton_)) {
        PE_WARN("Sprint animation failed to load; shift will still speed up movement");
    }

    // Breathing idle loop is optional: if missing we fall back to bind pose
    // with the walk anim's root rotation (see render path).
    if (character_skinned_ &&
        !anim_idle_.load(assets_root + "/models/characters/breathing_idle.eanim",
                         skeleton_)) {
        PE_WARN("Idle animation failed to load; using bind pose for idle");
    }

    if (character_skinned_) {
        walk_bones_.left_upleg  = skeleton_.find_bone("mixamorig:LeftUpLeg");
        walk_bones_.right_upleg = skeleton_.find_bone("mixamorig:RightUpLeg");
        walk_bones_.left_leg    = skeleton_.find_bone("mixamorig:LeftLeg");
        walk_bones_.right_leg   = skeleton_.find_bone("mixamorig:RightLeg");
        walk_bones_.left_arm    = skeleton_.find_bone("mixamorig:LeftArm");
        walk_bones_.right_arm   = skeleton_.find_bone("mixamorig:RightArm");
        walk_bones_.spine       = skeleton_.find_bone("mixamorig:Spine");
        PE_INFO("Walk bones: LU=%d RU=%d LK=%d RK=%d LA=%d RA=%d S=%d",
                walk_bones_.left_upleg, walk_bones_.right_upleg,
                walk_bones_.left_leg,   walk_bones_.right_leg,
                walk_bones_.left_arm,   walk_bones_.right_arm,
                walk_bones_.spine);
    }

    // Pistol locomotion + Glock equip wiring. Pistol anims rebind against
    // the player skeleton; missing bone-name channels silently fall back
    // to bind pose, so the player won't T-pose if a future asset breaks.
    if (character_skinned_) {
        if (!anim_pistol_walk_.load(
                assets_root + "/models/characters/pistol_walk.eanim",
                skeleton_))
            PE_WARN("Pistol walk anim failed; armed walk will fall back to unarmed");
        if (!anim_pistol_run_.load(
                assets_root + "/models/characters/pistol_run.eanim",
                skeleton_))
            PE_WARN("Pistol run anim failed; armed sprint will fall back to unarmed");
        if (!anim_pistol_idle_.load(
                assets_root + "/models/characters/pistol_idle.eanim",
                skeleton_))
            PE_WARN("Pistol idle anim failed; armed idle will fall back to unarmed");

        // Right-hand bone: cached so we can recover its world transform
        // each frame from the skin matrices (skin = world * inv_bind, so
        // world = skin * inverse(inv_bind); inv_bind is constant, invert
        // it once at init).
        right_hand_bone_idx_ = skeleton_.find_bone("mixamorig:RightHand");
        if (right_hand_bone_idx_ >= 0) {
            right_hand_bind_world_ = glm::inverse(
                skeleton_.bone(right_hand_bone_idx_).inv_bind);
        } else {
            PE_WARN("RightHand bone not found; gun render disabled");
        }
    }

    if (!texture_.load_file(assets_root + "/characters/Characters_psx/Textures/Character_01.png"))
        PE_WARN("Falling back to checker for character (texture load failed)");

    // Character: pose root (feet pos + facing yaw) with a visual child that
    // carries model offset + uniform scale. The skinned mesh is drawn out-of-
    // band by render() (Scene::draw can't handle skinning today). If
    // skinning load failed, the visual node stays renderable-less and the
    // application's cube fallback kicks in.
    character_node_        = scene_->create_node();
    character_visual_node_ = scene_->create_node(character_node_);
    if (character_skinned_) {
        glm::vec3 mn = skinned_mesh_.bounds_min();
        glm::vec3 mx = skinned_mesh_.bounds_max();
        float h_native = std::max(0.001f, mx.y - mn.y);
        constexpr float TARGET_HEIGHT = 1.8f;
        character_model_scale_  = TARGET_HEIGHT / h_native;
        character_model_offset_ = {
            -(mn.x + mx.x) * 0.5f * character_model_scale_,
            -mn.y                  * character_model_scale_,
            -(mn.z + mx.z) * 0.5f * character_model_scale_,
        };
        PE_INFO("Character: skinned y=[%.3f,%.3f] -> scale %.4f anim=%.2fs",
                mn.y, mx.y, character_model_scale_, anim_walk_.duration());
    }

    return true;
}

void Player::shutdown() {
    texture_.destroy();
    // Skeleton, animations, skinned_mesh have RAII destructors.
    scene_                 = nullptr;
    character_node_        = nullptr;
    character_visual_node_ = nullptr;
}

void Player::sync_scene_to_initial_pose() {
    sync_character_scene();
}

bool Player::update_on_foot(float dt, const Input& input, Camera& camera,
                            SpringArm& spring,
                            const WorldCollision& world_col,
                            AudioEngine& audio,
                            float mouse_dx, float mouse_dy) {
    spring.apply_mouse(mouse_dx, mouse_dy);
    spring.anchor = character_.eye_position();
    spring.update(world_col);

    sprinting_ = input.down(SDL_SCANCODE_LSHIFT) || input.down(SDL_SCANCODE_RSHIFT);
    if (input.pressed(SDL_SCANCODE_E)) armed_ = !armed_;
    bool wants_fire = armed_ && input.mouse_pressed(SDL_BUTTON_LEFT);

    character_.update(dt, input, spring.yaw_deg, sprinting_, world_col);

    // Face the direction we're walking when moving; otherwise face the
    // camera. Lerp gently so quick mouse turns don't snap the body around.
    // Without the idle branch, releasing W mid-mouse-turn freezes facing
    // partway through the lerp, leaving the character pointing off to the
    // side of where the camera is now looking.
    glm::vec3 vh = character_.velocity();
    vh.y = 0.f;
    float target = (glm::length(vh) > 0.5f)
                    ? glm::degrees(std::atan2(vh.z, vh.x))
                    : spring.yaw_deg;
    // wrap to [-180, 180]
    float diff = std::fmod(target - character_facing_yaw_deg_ + 540.f, 360.f) - 180.f;
    character_facing_yaw_deg_ += diff * std::min(1.f, 12.f * dt);

    camera.position = spring.camera_position;
    camera.yaw      = spring.yaw_deg;
    camera.pitch    = spring.pitch_deg;

    // Footstep audio: tick a timer paced by current ground speed so steps
    // come faster when sprinting. Gated to (a) actually moving, (b) on
    // the ground, and (c) the foot is over a paved surface — grass / dirt
    // shouldn't trigger the concrete sample.
    {
        glm::vec3 step_vh = character_.velocity();
        step_vh.y = 0.f;
        float speed = glm::length(step_vh);
        const bool moving = speed > 0.5f && character_.grounded();
        if (moving) {
            // ~0.55 s/step at MOVE_SPEED; scales inversely with speed so
            // sprint cadence quickens automatically.
            float interval = 0.55f * CharacterController::MOVE_SPEED
                                   / std::max(speed, 0.5f);
            footstep_timer_ += dt;
            if (footstep_timer_ >= interval) {
                footstep_timer_ -= interval;
                glm::vec3 fp = character_.feet_position();
                if (is_paved_surface(fp.x, fp.z))
                    audio.play_footstep_concrete();
            }
        } else {
            footstep_timer_ = 0.f;
        }
    }

    return wants_fire;
}

void Player::update_pose(double dt, double world_time) {
    // Advance the walk-anim time accumulator (seconds). Mixamo loops are
    // 1 stride each, so we scale by current speed / reference speed of the
    // anim that's actually playing — keeps stride frequency tied to ground
    // speed regardless of which clip is sampled.
    // Idle: freeze on first frame instead of playing in place.
    bool char_moving;
    bool use_sprint;
    {
        glm::vec3 vh = character_.velocity();
        vh.y = 0.f;
        float speed = glm::length(vh);
        char_moving = speed > 0.5f;
        use_sprint  = sprinting_ && char_moving &&
                      anim_sprint_.duration() > 0.f;
        float ref = use_sprint ? CharacterController::SPRINT_SPEED
                               : CharacterController::MOVE_SPEED;
        if (char_moving) walk_phase_ += dt * double(speed / ref);
        else             walk_phase_  = 0.0;   // freeze at first frame
    }

    if (character_skinned_) {
        const int n = skeleton_.bone_count();
        char_local_poses_.resize(static_cast<std::size_t>(n));

        // Pick the anim driving the current frame. Walking and sprint loop
        // at speed-tied stride frequency; idle plays the breathing-idle
        // loop in real time. When armed, swap to the pistol-locomotion
        // variants — same phase / cadence semantics, just different rigs.
        // Each armed slot falls back to its unarmed counterpart if the
        // pistol anim failed to load.
        auto pick_armed = [this](const Animation& armed,
                                  const Animation& unarmed)
                            -> const Animation* {
            return (armed_ && armed.duration() > 0.f) ? &armed : &unarmed;
        };
        const Animation* anim_to_use = nullptr;
        float            phase       = 0.f;
        if (char_moving) {
            anim_to_use = use_sprint
                ? pick_armed(anim_pistol_run_,  anim_sprint_)
                : pick_armed(anim_pistol_walk_, anim_walk_);
            phase       = static_cast<float>(walk_phase_);
        } else if (anim_idle_.duration() > 0.f) {
            anim_to_use = pick_armed(anim_pistol_idle_, anim_idle_);
            phase       = static_cast<float>(world_time);
        }

        if (anim_to_use && anim_to_use->duration() > 0.f) {
            anim_to_use->sample(phase, skeleton_, char_local_poses_);
            // Strip root motion: gameplay drives world position, so the
            // root bone's translation must come from the bind pose, not
            // the anim.
            for (int b = 0; b < n; ++b) {
                if (skeleton_.bone(b).parent < 0) {
                    glm::vec3 bind_t{skeleton_.bone(b).bind_local[3]};
                    char_local_poses_[static_cast<std::size_t>(b)][3] =
                        glm::vec4{bind_t, 1.f};
                }
            }
        } else {
            // No anim available — fall back to bind pose.
            for (int b = 0; b < n; ++b)
                char_local_poses_[static_cast<std::size_t>(b)] =
                    skeleton_.bone(b).bind_local;
        }

        skeleton_.compute_skin_matrices(char_local_poses_,
                                         char_skin_matrices_);
    }

    sync_character_scene();
}

void Player::compute_procedural_walk_pose(float phase, bool moving) {
    const int n = skeleton_.bone_count();
    char_local_poses_.resize(static_cast<std::size_t>(n));

    // Default: bind pose for every bone.
    for (int b = 0; b < n; ++b)
        char_local_poses_[static_cast<std::size_t>(b)] =
            skeleton_.bone(b).bind_local;

    if (!moving) return;

    constexpr float TWO_PI = 6.2831853f;
    // Drive ~2 strides per second of phase. With phase = walk_phase_ in
    // seconds (advances at speed/WALK_REF), this gives 2 strides/sec at
    // full speed.
    float a = phase * TWO_PI * 2.f;
    float s = std::sin(a);

    auto rotate_local = [&](int b, const glm::vec3& axis, float angle) {
        if (b < 0) return;
        char_local_poses_[static_cast<std::size_t>(b)] =
            skeleton_.bone(b).bind_local *
            glm::mat4_cast(glm::angleAxis(angle, axis));
    };

    float thigh = glm::radians(30.f) * s;
    float arm   = glm::radians(25.f) * s;
    float knee_l = glm::radians(45.f) * std::max(0.f, -s);
    float knee_r = glm::radians(45.f) * std::max(0.f,  s);

    rotate_local(walk_bones_.left_upleg,  {1, 0, 0}, +thigh);
    rotate_local(walk_bones_.right_upleg, {1, 0, 0}, -thigh);
    rotate_local(walk_bones_.left_leg,    {1, 0, 0}, knee_l);
    rotate_local(walk_bones_.right_leg,   {1, 0, 0}, knee_r);
    rotate_local(walk_bones_.left_arm,    {1, 0, 0}, -arm);
    rotate_local(walk_bones_.right_arm,   {1, 0, 0}, +arm);
}

void Player::sync_character_scene() {
    if (!character_node_) return;

    // Pose root at feet, rotated by facing yaw.
    glm::vec3 feet = character_.feet_position();
    character_node_->transform.position = feet;
    character_node_->transform.rotation =
        glm::angleAxis(glm::radians(-character_facing_yaw_deg_ + 90.f),
                        glm::vec3{0.f, 1.f, 0.f});
    character_node_->transform.scale    = {1.f, 1.f, 1.f};
    character_node_->mark_dirty();

    // Visual child: model offset + uniform scale (skeletal animation handles
    // body motion). Cube fallback if no skinned model.
    if (character_skinned_) {
        character_visual_node_->transform.position = character_model_offset_;
        character_visual_node_->transform.rotation = glm::quat{1.f, 0.f, 0.f, 0.f};
        character_visual_node_->transform.scale    = glm::vec3{character_model_scale_};
    } else {
        character_visual_node_->transform.position = {0.f, 0.9f, 0.f};
        character_visual_node_->transform.rotation = glm::quat{1.f, 0.f, 0.f, 0.f};
        character_visual_node_->transform.scale    = {0.55f, 1.8f, 0.4f};
    }
    character_visual_node_->mark_dirty();
}

void Player::render(Shader& skinned, const glm::mat4& view_proj,
                    const glm::vec3& cam_pos,
                    const Texture& fallback_tex) const {
    if (!skinned_ready() || !visible()) return;

    skinned.use();
    skinned.set("u_view_proj",   view_proj);
    skinned.set("u_cam_pos",     cam_pos);
    skinned.set("u_light_dir",   glm::normalize(glm::vec3{0.6f, 1.f, 0.4f}));
    skinned.set("u_light_color", glm::vec3{1.f, 0.95f, 0.85f});
    skinned.set("u_ambient",     glm::vec3{0.18f, 0.22f, 0.28f});
    skinned.set("u_diffuse",     0);
    skinned.set("u_tint",        glm::vec3{1.f});
    skinned.set("u_uv_scale",    glm::vec2{1.f, 1.f});
    skinned.set("u_model",       character_visual_node_->world_matrix());
    skinned.set("u_normal_mat",
        glm::mat3(glm::inverseTranspose(character_visual_node_->world_matrix())));
    int n = static_cast<int>(char_skin_matrices_.size());
    if (n > 64) n = 64;
    skinned.set_mat4_array("u_bones", char_skin_matrices_.data(), n);

    if (texture_.id()) texture_.bind(0);
    else               fallback_tex.bind(0);
    skinned_mesh_.draw();
}

void Player::set_visible(bool v) {
    if (character_node_) character_node_->visible = v;
}

bool Player::visible() const {
    return character_node_ && character_node_->visible;
}

glm::mat4 Player::right_hand_world_xform() const {
    if (!has_right_hand()) return glm::mat4{1.f};
    if (right_hand_bone_idx_ >= static_cast<int>(char_skin_matrices_.size()))
        return glm::mat4{1.f};
    glm::mat4 bone_world = char_skin_matrices_[
        static_cast<std::size_t>(right_hand_bone_idx_)] * right_hand_bind_world_;
    return character_visual_node_->world_matrix() * bone_world;
}

void Player::apply_damage(float dmg) {
    health_ = std::max(0.f, health_ - dmg);
}

void Player::respawn() {
    character_.teleport(spawn_);
    health_ = 100.f;
}

} // namespace pengine
