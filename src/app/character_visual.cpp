#include "app/character_visual.h"
#include "app/traffic_visual.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include <glm/gtc/quaternion.hpp>

#include "city/road_types.h"
#include "city/authored_staff.h"
#include "city/interior_streaming.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/fixed_step.h"
#include "core/log.h"
#include "core/rng.h"
#include "gfx/sky.h"

namespace apricot {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;
constexpr float kNpcDrawDistance = 175.0f;
constexpr float kWalkStrideMetres = 1.35f;
// Cover more ground per cycle while keeping the original 5.45 m/s run cadence.
constexpr float kSprintStrideMetres = 2.25f * (6.25f / 5.45f);
constexpr float kPedSurfaceLift = DRAPE_EPS_M + SIDEWALK_KERB_M;

bool identity_less(uint64_t ak, uint32_t as, uint64_t bk, uint32_t bs) {
    return ak != bk ? ak < bk : as < bs;
}

float wrapped_delta(float from, float to) {
    float delta = to - from;
    while (delta > kPi) delta -= kTwoPi;
    while (delta < -kPi) delta += kTwoPi;
    return delta;
}

float mixed_angle(float from, float to, float alpha) {
    return from + wrapped_delta(from, to) * alpha;
}

Transform facing_transform(glm::vec3 position, glm::vec3 forward) {
    Transform transform;
    transform.position = position;
    const float length = glm::length(glm::vec2{forward.x, forward.z});
    if (length > 1e-5f) {
        transform.rotation = character_root_rotation(
            std::atan2(forward.x, -forward.z));
    }
    return transform;
}

}  // namespace

bool CharacterVisual::load_model(const std::string& root, float height_m,
                                 Model& out) {
    SkinnedEmesh source;
    if (!read_skinned_emesh(asset_path(root + "skin.emesh"), source) ||
        !out.skeleton.load(asset_path(root + "skin.eskel")) ||
        !out.skeleton.accepts(source)) {
        AP_ERROR("character visual: rigged model '%s' is invalid", root.c_str());
        return false;
    }
    if (!out.idle.load(asset_path(
            "models/characters/psx_pack/animations/idle.eanim"),
            out.skeleton) ||
        !out.walk.load(asset_path(
            "models/characters/psx_pack/animations/walk.eanim"),
            out.skeleton) ||
        !out.sprint.load(asset_path(
            "models/characters/psx_pack/animations/sprint.eanim"),
            out.skeleton)) {
        AP_ERROR("character visual: animation set did not bind to '%s'",
                 root.c_str());
        return false;
    }
    if (out.idle.unresolved_channels() != 0 ||
        out.walk.unresolved_channels() != 0 ||
        out.sprint.unresolved_channels() != 0) {
        AP_ERROR("character visual: animation set has missing bones for '%s'",
                 root.c_str());
        return false;
    }
    if (!out.mesh.upload(source) ||
        !out.texture.load_file(asset_path(root + "body.png"))) {
        AP_ERROR("character visual: mesh or texture upload failed for '%s'",
                 root.c_str());
        return false;
    }

    out.bounds = source.bounds;
    const float source_height = source.bounds.size().y;
    if (!(source_height > 1e-4f)) return false;
    const float scale = height_m / source_height;
    out.local.scale = glm::vec3{scale};
    out.local.rotation = glm::angleAxis(kPi, glm::vec3{0.0f, 1.0f, 0.0f});
    out.local.position = {
        -source.bounds.center().x * scale,
        -source.bounds.min.y * scale,
        -source.bounds.center().z * scale,
    };
    out.walk_plant = locomotion_plant_offset(
        source, out.skeleton, out.walk, scale);
    out.sprint_plant = locomotion_plant_offset(
        source, out.skeleton, out.sprint, scale);
    out.loaded = true;
    AP_INFO("character visual: loaded '%s' (%d bones, %.3f m walk plant)",
            root.c_str(), out.skeleton.bone_count(),
            static_cast<double>(out.walk_plant));
    return true;
}

void CharacterVisual::sample_pose(const Model& model,
                                  const Animation& animation, float time,
                                  Pose& out) {
    animation.sample(time, model.skeleton, out.local);
    strip_root_motion_xz(model.skeleton, out.local);
    model.skeleton.compute_skin_matrices(out.local, out.skin);
    skin_matrices_to_dual_quaternions(
        out.skin, out.dual_real, out.dual_part);
}

Transform CharacterVisual::model_transform(const Transform& root,
                                           const Model& model,
                                           float plant_offset) {
    Transform local = model.local;
    local.position.y += plant_offset;
    return root * local;
}

bool CharacterVisual::init(const PlayerCharacterState& player) {
    if (!shader_.build_from_files("shaders/skinned_character.vert",
                                  "shaders/skinned_character.frag")) {
        AP_ERROR("character visual: skinned shader failed to build");
        return false;
    }
    if (!load_model("models/characters/psx_pack/player_male_01/",
                    1.76f, player_model_)) {
        return false;
    }

    player_right_hand_bone_ = player_model_.skeleton.find_bone("mixamorig:RightHand");
    const int index = player_model_.skeleton.find_bone("mixamorig:RightHandIndex1");
    if (player_right_hand_bone_ >= 0 && index >= 0) {
        const auto& hand = player_model_.skeleton.bone(player_right_hand_bone_);
        const glm::mat4 bind_hand = glm::inverse(hand.inverse_bind);
        const glm::vec3 wrist{bind_hand[3]};
        const glm::vec3 knuckle{glm::inverse(player_model_.skeleton.bone(index).inverse_bind)[3]};
        const glm::vec3 forward = glm::normalize(knuckle - wrist);
        const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3{0, 0, 1}));
        const glm::vec3 up = glm::cross(right, forward);
        glm::mat4 socket{1.0f};
        socket[0] = glm::vec4{right, 0};
        socket[1] = glm::vec4{up, 0};
        socket[2] = glm::vec4{-forward, 0};
        socket[3] = glm::vec4{glm::mix(wrist, knuckle, .70f), 1};
        player_right_hand_socket_ = hand.inverse_bind * socket;
    } else {
        player_right_hand_bone_ = -1;
    }

    static constexpr std::array<const char*, 18> kNames = {
        "civilian_male_03", "civilian_male_05", "civilian_male_07",
        "civilian_male_09", "civilian_female_03", "civilian_female_05",
        "civilian_female_07", "civilian_female_09",
        "civilian_male_06", "civilian_male_08", "civilian_male_10",
        "civilian_male_11", "civilian_male_13", "civilian_male_14",
        "civilian_male_15", "civilian_male_17_police",
        "civilian_female_04", "civilian_female_14",
    };
    for (std::size_t i = 0; i < npc_models_.size(); ++i) {
        const std::string root = std::string(
            "models/characters/psx_pack/") + kNames[i] + "/";
        if (!load_model(root, 1.72f, npc_models_[i])) return false;
    }

    // Both supplied uniform skins use the proven police body/animation rig.
    // Keep this pool separate: a civilian's random outfit is never an officer.
    static constexpr std::array<const char*, 2> kPoliceNames = {
        "police_male_17", "police_male_19",
    };
    for (std::size_t i = 0; i < police_models_.size(); ++i) {
        const std::string root = std::string{
            "models/characters/psx_pack/"} + kPoliceNames[i] + "/";
        // The seated and door-contact solvers use a 1.76 m person too. Never
        // let the officer change height when stepping out of the cruiser.
        if (!load_model(root, 1.76f, police_models_[i])) return false;
    }

    last_player_distance_ = player.distance_walked_m;
    player_walk_time_ = 0.0f;
    player_visible_ = true;
    const Transform root = facing_transform(
        player.position, character_forward(player.facing_yaw));
    player_world_ = model_transform(root, player_model_, 0.0f);
    sample_pose(player_model_, player_model_.idle, 0.0f, player_pose_);
    sync_staff(0, 0.0f, glm::vec3{0.0f}, 0.0f);
    AP_INFO("character visual: player plus %zu supplied skinned civilian models "
            "ready", npc_models_.size());
    AP_INFO("character visual: %zu supplied police uniform models ready",
            police_models_.size());
    return true;
}

CharacterVisual::Rig CharacterVisual::create_rig(
    const PedAgent& agent, int64_t step) const {
    Rig rig;
    rig.lane_key = agent.lane_key;
    rig.slot = agent.slot;
    const uint64_t identity = splitmix64_mix(
        agent.lane_key ^ (static_cast<uint64_t>(agent.slot) << 32));
    rig.model = static_cast<std::size_t>(identity % npc_models_.size());
    rig.last_step = step;
    const Model& model = npc_models_[rig.model];
    rig.walk_time = static_cast<float>(identity & 0xFFFFu) / 65536.0f *
                    model.walk.duration();
    return rig;
}

void CharacterVisual::sync_rig(Rig& rig, const PedAgent& agent, float alpha,
                               int64_t step) const {
    const Model& model = npc_models_[rig.model];
    const int64_t elapsed_steps = std::max<int64_t>(0, step - rig.last_step);
    rig.last_step = step;
    const float speed = std::max(agent.speed_mps, 0.0f);
    const float seconds = static_cast<float>(elapsed_steps) *
                          static_cast<float>(kSimDt);
    rig.walk_time += seconds * speed / kWalkStrideMetres *
                     model.walk.duration();

    Transform root = facing_transform(agent.pos, agent.fwd);
    root.position.y += kPedSurfaceLift;
    if (speed <= 0.08f) {
        const float identity_phase = static_cast<float>(
            (agent.lane_key ^ static_cast<uint64_t>(agent.slot)) & 255u) /
            256.0f;
        const float idle_time = (static_cast<float>(step) + alpha) *
            static_cast<float>(kSimDt) +
            identity_phase * model.idle.duration();
        rig.world = model_transform(root, model, 0.0f);
        sample_pose(model, model.idle, idle_time, rig.pose);
    } else {
        const float render_ahead = alpha * static_cast<float>(kSimDt) * speed /
                                   kWalkStrideMetres * model.walk.duration();
        rig.world = model_transform(root, model, model.walk_plant);
        sample_pose(model, model.walk, rig.walk_time + render_ahead, rig.pose);
    }
}

void CharacterVisual::sync(const Crowd& crowd,
                           const PlayerCharacterState& previous_player,
                           const PlayerCharacterState& player, float alpha,
                           int64_t step, bool player_visible, glm::vec3 focus,
                           float presentation_radius_m) {
    driver_visible_ = false;
    const float blend = std::clamp(alpha, 0.0f, 1.0f);
    const float player_yaw = mixed_angle(
        previous_player.facing_yaw, player.facing_yaw, blend);
    Transform player_root = facing_transform(
        glm::mix(previous_player.position, player.position, blend),
        character_forward(player_yaw));
    const float walked = glm::mix(previous_player.distance_walked_m,
                                  player.distance_walked_m, blend);
    const float distance_delta = std::max(0.0f, walked - last_player_distance_);
    last_player_distance_ = walked;

    const float speed = glm::length(glm::vec2{player.velocity.x,
                                              player.velocity.z});
    const Animation* animation = &player_model_.idle;
    float animation_time = (static_cast<float>(step) + blend) *
                           static_cast<float>(kSimDt);
    float plant = 0.0f;
    if (speed > 0.08f) {
        const bool sprinting = player.sprinting;
        animation = sprinting ? &player_model_.sprint : &player_model_.walk;
        const float stride = sprinting ? kSprintStrideMetres
                                       : kWalkStrideMetres;
        player_walk_time_ += distance_delta / stride * animation->duration();
        animation_time = player_walk_time_;
        plant = sprinting ? player_model_.sprint_plant
                          : player_model_.walk_plant;
    }
    player_world_ = model_transform(player_root, player_model_, plant);
    sample_pose(player_model_, *animation, animation_time, player_pose_);
    player_visible_ = player_visible;

    const std::vector<PedAgent>& agents = crowd.peds();
    std::vector<Rig> next;
    next.reserve(presentation_radius_m > 0.0f
                     ? std::min<std::size_t>(agents.size(), 64u)
                     : agents.size());
    std::size_t old = 0;
    for (const PedAgent& agent : agents) {
        if (!city::within_presentation_radius(
                agent.pos, focus, presentation_radius_m))
            continue;
        while (old < rigs_.size() &&
               identity_less(rigs_[old].lane_key, rigs_[old].slot,
                             agent.lane_key, agent.slot)) {
            ++old;
        }
        Rig rig;
        if (old < rigs_.size() && rigs_[old].lane_key == agent.lane_key &&
            rigs_[old].slot == agent.slot) {
            rig = std::move(rigs_[old]);
            ++old;
        } else {
            rig = create_rig(agent, step);
        }
        sync_rig(rig, agent, blend, step);
        next.push_back(std::move(rig));
    }
    rigs_ = std::move(next);
    sync_staff(step, blend, focus, presentation_radius_m);
}

void CharacterVisual::sync_staff(int64_t step, float alpha, glm::vec3 focus,
                                 float presentation_radius_m) {
    std::vector<Rig> next;
    next.reserve(std::size(city::kAuthoredStaff));
    for (const auto& staff : city::kAuthoredStaff) {
        const glm::vec3 position = city::authored_staff_position(staff);
        if (!city::within_presentation_radius(
                position, focus, presentation_radius_m))
            continue;
        const auto existing = std::find_if(
            staff_rigs_.begin(), staff_rigs_.end(), [&](const Rig& rig) {
                return rig.lane_key == staff.identity;
            });
        Rig rig = existing == staff_rigs_.end() ? Rig{} : std::move(*existing);
        rig.lane_key = staff.identity;
        rig.slot = 0;
        rig.model = staff.civilian_model;
        const Model& model = npc_models_[rig.model];
        rig.world = model_transform(facing_transform(
            position, city::authored_staff_forward(staff)),
            model, 0.0f);
        // Absolute simulation time avoids resets when the shop is streamed
        // out or the camera returns. A fixed phase separates future clerks.
        const double phase = static_cast<double>(staff.identity & 0xffffu) / 65536.0;
        const double seconds = (static_cast<double>(step) + static_cast<double>(alpha)) * kSimDt;
        const double duration = static_cast<double>(model.idle.duration());
        const float time = duration > 0.0 ?
            static_cast<float>(std::fmod(seconds + phase * duration, duration)) : 0.0f;
        sample_pose(model, model.idle, time, rig.pose);
        next.push_back(std::move(rig));
    }
    staff_rigs_ = std::move(next);
}

void CharacterVisual::prepare_boat_transition(const PlayerCharacterState& shore) {
    if(!player_model_.loaded) {boat_standing_pose_={};return;}
    const auto root=facing_transform(shore.position,character_forward(shore.facing_yaw));
    boat_standing_world_=model_transform(root,player_model_,0);
    sample_pose(player_model_,player_model_.idle,0,boat_standing_pose_);
}
void CharacterVisual::sync_boat_driver(const Transform* body,bool occupied,float steering,float time) {
    driver_visible_=false;
    if(!occupied || !body || !player_model_.loaded) return;
    driver_visible_=make_boat_driver_pose(player_model_.skeleton,player_model_.bounds,*body,
        steering,time,driver_pose_);
    if(driver_visible_) player_visible_=false;
}
void CharacterVisual::sync_boat_transition(const Transform* body,const BoatTransitionState& state,float alpha) {
    driver_visible_=false;
    if(!body || !state.active() || !player_model_.loaded) return;
    driver_visible_=make_boat_transition_pose(player_model_.skeleton,player_model_.bounds,*body,
        boat_standing_world_,boat_standing_pose_.local,boat_transition_fraction(state,alpha),driver_pose_);
    if(driver_visible_) player_visible_=false;
}

void CharacterVisual::sync_driver(PlayerCarId car, bool occupied,
                                  const Transform* rendered_body) {
    driver_visible_ = false;
    if (!shows_vehicle_driver(car, occupied) || !rendered_body ||
        !player_model_.loaded) return;
    driver_visible_ = make_vehicle_driver_pose(
        car, player_model_.skeleton, player_model_.bounds, *rendered_body, driver_pose_);
    if (driver_visible_) player_visible_ = false;
}

void CharacterVisual::sync_transition(PlayerCarId car, const Transform* body,
                                      const VehicleTransitionState& state, float alpha) {
    driver_visible_ = false;
    if (!body || !state.active() || !player_model_.loaded) return;
    driver_visible_ = make_vehicle_transition_pose(car, player_model_.skeleton,
        player_model_.bounds, *body, player_world_, player_pose_.local,
        sample_vehicle_transition(state, alpha), driver_pose_);
    if (driver_visible_) player_visible_ = false;
}

void CharacterVisual::sync_police(const Crowd& crowd,
                                  const TrafficVisual& traffic, float alpha,
                                  int64_t step, glm::vec3 focus,
                                  float presentation_radius_m) {
    const float blend = std::clamp(alpha, 0.0f, 1.0f);
    std::vector<PoliceRig> next;
    next.reserve(crowd.police_unit_count());
    std::size_t old = 0;
    for (const auto& agent : crowd.vehicles()) {
        const auto& officer = agent.officer;
        const glm::vec3 presentation_position = police_officer_on_foot(officer)
            ? officer.pos : agent.pos;
        if (!agent.police_unit || !city::within_presentation_radius(
                presentation_position, focus, presentation_radius_m)) continue;
        while (old < police_rigs_.size() && identity_less(
                police_rigs_[old].character.lane_key,
                police_rigs_[old].character.slot,
                agent.lane_key, agent.slot)) ++old;
        PoliceRig rig;
        if (old < police_rigs_.size() &&
            police_rigs_[old].character.lane_key == agent.lane_key &&
            police_rigs_[old].character.slot == agent.slot) {
            rig = std::move(police_rigs_[old++]);
        } else {
            rig.character.lane_key = agent.lane_key;
            rig.character.slot = agent.slot;
            rig.character.model = static_cast<std::size_t>(splitmix64_mix(
                agent.lane_key ^ (static_cast<uint64_t>(agent.slot) << 32)) %
                police_models_.size());
        }
        const Model& model = police_models_[rig.character.model];
        const float phase = static_cast<float>(
            (agent.lane_key ^ static_cast<uint64_t>(agent.slot)) & 255u) /
            256.0f;
        const float idle_time = (static_cast<float>(step) + blend) *
            static_cast<float>(kSimDt) + phase * model.idle.duration();
        rig.in_vehicle = !police_officer_on_foot(officer);
        if (rig.in_vehicle) {
            const Transform body = traffic.vehicle_body_transform(agent);
            VehicleDriverPose occupant;
            bool posed = false;
            if (officer.transition.active()) {
                const Transform standing = model_transform(facing_transform(
                    officer.door_pos, character_forward(officer.door_heading)),
                    model, 0.0f);
                Pose idle;
                sample_pose(model, model.idle, idle_time, idle);
                posed = make_vehicle_transition_pose(
                    PlayerCarId::MunicipalCruiser91C, model.skeleton,
                    model.bounds, body, standing, idle.local,
                    sample_vehicle_transition(officer.transition, blend),
                    occupant);
            } else {
                posed = make_vehicle_driver_pose(
                    PlayerCarId::MunicipalCruiser91C, model.skeleton,
                    model.bounds, body, occupant);
            }
            if (!posed) continue;
            rig.character.world = occupant.world;
            rig.character.pose.local = std::move(occupant.local);
            rig.character.pose.skin = std::move(occupant.skin);
            rig.character.pose.dual_real = std::move(occupant.dual_real);
            rig.character.pose.dual_part = std::move(occupant.dual_part);
        } else {
            const auto position = glm::mix(officer.previous_pos, officer.pos,
                                           blend);
            const float yaw = mixed_angle(officer.previous_heading,
                                           officer.heading, blend);
            const Transform root = facing_transform(
                position, character_forward(yaw));
            const float step_distance = glm::length(glm::vec2{
                officer.pos.x - officer.previous_pos.x,
                officer.pos.z - officer.previous_pos.z});
            const float speed = step_distance / static_cast<float>(kSimDt);
            const bool sprinting = speed > 3.0f;
            const Animation& animation = speed <= 0.08f ? model.idle :
                sprinting ? model.sprint : model.walk;
            const float stride = sprinting ? kSprintStrideMetres :
                                             kWalkStrideMetres;
            const float walked = std::max(0.0f, officer.distance_walked_m -
                                               (1.0f - blend) * step_distance);
            const float time = speed <= 0.08f ? idle_time :
                (walked / stride + phase) * animation.duration();
            const float plant = speed <= 0.08f ? 0.0f :
                sprinting ? model.sprint_plant : model.walk_plant;
            rig.character.world = model_transform(root, model, plant);
            sample_pose(model, animation, time, rig.character.pose);
        }
        next.push_back(std::move(rig));
    }
    police_rigs_ = std::move(next);
}

bool CharacterVisual::draw_model(const Model& model, const Pose& pose,
                                 const Transform& world,
                                 const Camera& camera,
                                 bool cull_bind_bounds) const {
    if (!model.loaded || pose.dual_real.empty() ||
        pose.dual_real.size() != pose.dual_part.size()) {
        return false;
    }
    const AABB world_bounds = model.bounds.transformed(world.matrix())
                                  .expanded(0.35f);
    if (cull_bind_bounds && camera.frustum().cull(world_bounds)) return false;
    shader_.set_mat4("u_model", world.matrix());
    shader_.set_vec4_array("u_dq_real", pose.dual_real.data(),
                           static_cast<int>(pose.dual_real.size()));
    shader_.set_vec4_array("u_dq_dual", pose.dual_part.data(),
                           static_cast<int>(pose.dual_part.size()));
    model.texture.bind(0);
    model.mesh.draw();
    return true;
}

void CharacterVisual::render(const Camera& camera, const SkyEnv& environment,
                             const HeadlightRig& headlights,
                             const CanopyLightRig& canopy_lights) const {
    last_draw_count_ = 0;
    last_police_draw_count_ = 0;
    if (!shader_.valid()) return;
    shader_.bind();
    shader_.set_mat4("u_view_proj", camera.view_projection());
    shader_.set_int("u_diffuse", 0);
    shader_.set_vec4("u_tint", glm::vec4{1.0f});
    apply_lighting(shader_, environment, camera.position, headlights,
                   canopy_lights);
    shader_.set_float("u_specular_strength", 0.0f);

    if (player_visible_ &&
        draw_model(player_model_, player_pose_, player_world_, camera)) {
        ++last_draw_count_;
    }
    if (driver_visible_) {
        // A seated character's bounds differ from its tall bind-pose AABB.
        // Skip the pedestrian culler for this single occupied-car passenger.
        shader_.set_mat4("u_model", driver_pose_.world.matrix());
        shader_.set_vec4_array("u_dq_real", driver_pose_.dual_real.data(),
                               static_cast<int>(driver_pose_.dual_real.size()));
        shader_.set_vec4_array("u_dq_dual", driver_pose_.dual_part.data(),
                               static_cast<int>(driver_pose_.dual_part.size()));
        player_model_.texture.bind(0);
        player_model_.mesh.draw();
        ++last_draw_count_;
    }
    const float max_distance_squared = kNpcDrawDistance * kNpcDrawDistance;
    for (const Rig& rig : rigs_) {
        const glm::vec3 delta = rig.world.position - camera.position;
        if (glm::dot(delta, delta) > max_distance_squared) continue;
        if (draw_model(npc_models_[rig.model], rig.pose, rig.world, camera)) {
            ++last_draw_count_;
        }
    }
    for (const Rig& rig : staff_rigs_) {
        const glm::vec3 delta = rig.world.position - camera.position;
        if (glm::dot(delta, delta) > max_distance_squared) continue;
        if (draw_model(npc_models_[rig.model], rig.pose, rig.world, camera)) ++last_draw_count_;
    }
    for (const PoliceRig& officer : police_rigs_) {
        const Rig& rig = officer.character;
        const glm::vec3 delta = rig.world.position - camera.position;
        if (glm::dot(delta, delta) > max_distance_squared) continue;
        // A seated or traversing pose lies outside the tall bind-pose box.
        // Distance-limit these few officers like the player's visible driver.
        if (draw_model(police_models_[rig.model], rig.pose, rig.world, camera,
                       !officer.in_vehicle)) {
            ++last_draw_count_;
            ++last_police_draw_count_;
        }
    }
}

bool CharacterVisual::player_right_hand_transform(glm::mat4& out) const {
    if (!player_visible_ || driver_visible_ || !player_model_.loaded ||
        player_right_hand_bone_ < 0 ||
        static_cast<std::size_t>(player_right_hand_bone_) >= player_pose_.skin.size()) return false;
    const auto index = static_cast<std::size_t>(player_right_hand_bone_);
    const glm::mat4 global = player_pose_.skin[index] *
        glm::inverse(player_model_.skeleton.bone(player_right_hand_bone_).inverse_bind);
    out = player_world_.matrix() * global * player_right_hand_socket_;
    // Character assets use native units; attachments use real metres.
    for (int axis = 0; axis < 3; ++axis) {
        const float length = glm::length(glm::vec3{out[axis]});
        if (!(length > 1e-6f) || !std::isfinite(length)) return false;
        out[axis] = glm::vec4{glm::vec3{out[axis]} / length, 0};
    }
    return true;
}

void CharacterVisual::destroy() {
    shader_.destroy();
    player_model_.mesh.destroy();
    player_model_.texture.destroy();
    for (Model& model : npc_models_) {
        model.mesh.destroy();
        model.texture.destroy();
    }
    for (Model& model : police_models_) {
        model.mesh.destroy();
        model.texture.destroy();
    }
    rigs_.clear();
    staff_rigs_.clear();
    police_rigs_.clear();
    player_pose_ = Pose{};
    boat_standing_pose_ = Pose{};
    player_visible_ = false;
    driver_visible_ = false;
    driver_pose_ = VehicleDriverPose{};
    last_draw_count_ = 0;
    last_police_draw_count_ = 0;
}

}  // namespace apricot
