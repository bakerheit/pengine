#include "app/character_visual.h"
#include "app/ped_impact_pose.h"
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
// The stride lengths that used to live here are now CharacterAnimTuning's
// walk_stride_m / sprint_stride_m, at the same values, so the clock that reads
// them and the state machine that chooses the clip cannot drift apart.
constexpr float kPedSurfaceLift = DRAPE_EPS_M + SIDEWALK_KERB_M;
// Above this an ambient walker is running. The animator applies its own
// threshold too; this is the crowd's own "they broke into a run" signal.
constexpr float kPedRunSpeed = 3.0f;
// The player has no lane key, so it needs one stable identity of its own for
// the hash-derived idle variety. A literal, never a clock and never a counter.
constexpr uint64_t kPlayerAnimIdentity = 0x504C4159'45520001ull;

// One identity per ambient person, used for BOTH the model choice and every
// deterministic animation decision. Keeping it in one function is what stops
// the two drifting apart into a character who changes their idle when they
// change their shirt.
uint64_t ped_identity(const PedAgent& agent) {
    return splitmix64_mix(agent.lane_key ^
                          (static_cast<uint64_t>(agent.slot) << 32));
}

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

// Knockdown ragdoll feel. All of it is presentation: the sim decided this
// person is Downed, for how long, and WHERE THEY END UP. Nothing here can
// change any of the three.
//
// Note what is not in this list: the launch. PedAgent::impact_velocity is the
// throw, the crowd integrates it, and the ragdoll is handed the same vector —
// so there is one answer to "how hard was that" rather than two constants
// here quietly disagreeing with two in PedLifeTuning about where a body lands.
//
// How quickly the figure closes on the position the sim gives it, as a time
// constant in seconds. Long enough to be invisible during a tumble, short
// enough that the two agree well before anybody stands up.
constexpr float kRagdollAnchorTau = 0.30f;
// How long the settled ragdoll takes to become the stand-up clip.
//
// Short enough that the body is not visibly rubber, long enough that the
// morph is a movement rather than a cut. The clip runs from its own start
// throughout: this fades the POSE, it does not delay the get-up.
constexpr float kGetUpFadeS = 0.28f;
constexpr float kRagdollSpinMin = 2.6f;
constexpr float kRagdollSpinMax = 6.4f;
constexpr float kRagdollSpinJitter = 2.2f;
const RagdollTuning kRagdollTuning{};

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
    // The whole registered set, not three clips. CharacterClipSet::load()
    // already refuses a clip that leaves a channel unbound, which is the
    // failure that produces a half-animated body and reads as a rig bug.
    if (!out.clips.load(out.skeleton)) {
        AP_ERROR("character visual: animation set did not bind to '%s'",
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
    for (const CharacterClipInfo& info : kCharacterClips) {
        // Only the locomotion clips are planted. A death or a get-up ENDS on
        // the ground on purpose; lifting it to meet a walk cycle's lowest foot
        // would leave the body hovering above the pavement.
        out.plants[static_cast<std::size_t>(info.clip)] = info.planted
            ? locomotion_plant_offset(source, out.skeleton,
                                      out.clips.clip(info.clip), scale)
            : 0.0f;
    }
    // Ragdoll binding. NOT fatal if it fails: a model whose skeleton is not a
    // humanoid this can drive still walks, talks and dies on a clip. It just
    // does not fall with physics, and the log says which one and why.
    if (!ragdoll_rig_build(out.skeleton, scale, out.ragdoll)) {
        AP_WARN("character visual: '%s' will use the canned knockdown; its "
                "skeleton did not bind a ragdoll", root.c_str());
    }
    if (!weapon_pose_detail::palm_socket(out.skeleton, "Right",
                                         out.right_hand_bone,
                                         out.right_hand_socket)) {
        out.right_hand_bone = -1;
    }

    out.loaded = true;
    AP_INFO("character visual: loaded '%s' (%d bones, %d clips, %.3f m walk "
            "plant)", root.c_str(), out.skeleton.bone_count(),
            kCharacterClipCount,
            static_cast<double>(
                out.plants[static_cast<std::size_t>(CharacterClip::Walk)]));
    return true;
}

void CharacterVisual::sample_pose(const Model& model,
                                  const CharacterAnimSample& sample,
                                  Pose& out) const {
    evaluate_character_pose(model.clips, model.skeleton, sample, parts_a_,
                            parts_b_, out.local);
    model.skeleton.compute_skin_matrices(out.local, out.skin);
    skin_matrices_to_dual_quaternions(
        out.skin, out.dual_real, out.dual_part);
}

void CharacterVisual::sample_clip(const Model& model, CharacterClip clip,
                                  float time, Pose& out) const {
    CharacterAnimSample sample;
    sample.clip = clip;
    sample.from = clip;
    sample.time = time;
    sample.from_time = time;
    sample.blend = 1.0f;
    sample.root = character_clip_info(clip).root;
    sample.anchor_xz = model.clips.anchor_xz(clip);
    sample_pose(model, sample, out);
}

void CharacterVisual::advance_rig(Rig& rig, const Model& model,
                                  const CharacterAnimInput& input,
                                  double sim_seconds,
                                  const Transform& root, bool bullet_fall) const {
    // dt comes off the SIM clock, never a wall clock. A rig seen for the first
    // time this frame owes zero seconds, not whatever the last one owed.
    const float dt = rig.started
        ? static_cast<float>(std::max(0.0, sim_seconds - rig.last_sim_seconds))
        : 0.0f;
    rig.last_sim_seconds = sim_seconds;
    rig.started = true;
    rig.animator.advance(model.clips, input, dt);
    rig.world = model_transform(root, model,
                                rig.animator.plant(model.plants));
    sample_pose(model, pedestrian_impact_sample(rig.animator.sample(), bullet_fall), rig.pose);
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

    player_right_hand_bone_ = player_model_.right_hand_bone;
    player_right_hand_socket_ = player_model_.right_hand_socket;

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

    player_sim_seconds_ = 0.0;
    player_started_ = false;
    player_animator_.reset();
    player_visible_ = true;
    const Transform root = facing_transform(
        player.position, character_forward(player.facing_yaw));
    player_world_ = model_transform(root, player_model_, 0.0f);
    sample_clip(player_model_, CharacterClip::Idle, 0.0f, player_pose_);
    sync_staff(0.0, glm::vec3{0.0f}, 0.0f);
    AP_INFO("character visual: player plus %zu supplied skinned civilian models "
            "ready", npc_models_.size());
    AP_INFO("character visual: %zu supplied police uniform models ready",
            police_models_.size());
    return true;
}

CharacterVisual::Rig CharacterVisual::create_rig(const PedAgent& agent) const {
    Rig rig;
    rig.lane_key = agent.lane_key;
    rig.slot = agent.slot;
    rig.generation = agent.generation;
    rig.model = static_cast<std::size_t>(
        ped_identity(agent) % npc_models_.size());
    return rig;
}

void CharacterVisual::sync_rig(Rig& rig, const PedAgent& agent,
                               double sim_seconds,
                               const TerrainCollider* world) const {
    const Model& model = npc_models_[rig.model];
    Transform root = facing_transform(agent.pos, agent.fwd);
    root.position.y += kPedSurfaceLift;

    // Downed and Dead hold the fall; Rising releases it into the stand-up
    // clip, and Dead never does. The incoming direction selects
    // forward/backward for an authored bullet fall, while a car impact keeps
    // the physical tumbling response.
    CharacterAnimInput input = pedestrian_impact_input(
        ped_holds_impact_pose(agent.activity), agent.impact_from_bullet,
        glm::dot(agent.impact_dir_xz, glm::vec2{agent.fwd.x, agent.fwd.z}) < 0.0f);
    input.identity = ped_identity(agent);
    input.speed_mps = ped_is_floored(agent.activity)
        ? 0.0f : std::max(agent.speed_mps, 0.0f);
    input.sprinting = input.speed_mps > kPedRunSpeed;
    // dt is read before advance_rig consumes it, because the ragdoll and the
    // animator owe the same seconds and must not disagree about how many.
    const float dt = rig.started
        ? static_cast<float>(std::max(0.0, sim_seconds - rig.last_sim_seconds))
        : 0.0f;

    // The animator runs EVERY frame, ragdoll or not. It is what holds the
    // knockdown state while the body is on the floor and what decides the
    // get-up when the crowd puts this person back on their feet; stopping it
    // for the duration would mean rebuilding that state on the way out.
    advance_rig(rig, model, input, sim_seconds, root, agent.impact_from_bullet);

    if (input.downed && model.ragdoll.valid && world != nullptr &&
        !rig.pose.local.empty()) {
        // Knocked down again mid-recovery: the old fade is about a pose this
        // body no longer holds.
        rig.getup_fade_s = 0.0f;
        rig.landed.clear();
        step_ragdoll(rig, model, agent, world, dt);
        return;
    }

    // On their feet again. The animator has already entered the stand-up clip
    // — what it has NOT got is any idea what shape the body was in when it
    // stopped being physics, so left alone it cuts from a sprawl to the clip's
    // authored first frame.
    if (rig.ragdolling) begin_getup(rig, model);
    if (rig.getup_fade_s > 0.0f) blend_getup(rig, model, dt);
}

void CharacterVisual::begin_getup(Rig& rig, const Model& model) const {
    rig.ragdolling = false;
    rig.getup_fade_s = 0.0f;
    rig.landed.clear();
    if (!rig.ragdoll.active || !model.ragdoll.valid) return;

    // The landed pose, read out of the SOLVER rather than saved off last
    // frame's buffer: the nodes are still exactly where they came to rest, and
    // rig.world is the same root the animator is now using, so the two pose
    // sets are already in one frame and the fade is a plain part-wise blend.
    ragdoll_bone_poses(model.ragdoll, rig.ragdoll, rig.world.matrix(),
                       fade_local_);
    if (fade_local_.size() != rig.pose.local.size()) return;
    decompose_local_poses(fade_local_, rig.landed);
    rig.getup_fade_s = kGetUpFadeS;
}

void CharacterVisual::blend_getup(Rig& rig, const Model& model, float dt) const {
    rig.getup_fade_s = std::max(0.0f, rig.getup_fade_s - dt);
    if (rig.landed.size() != rig.pose.local.size()) {
        rig.getup_fade_s = 0.0f;
        rig.landed.clear();
        return;
    }
    // 0 at the moment the body stopped being physics, 1 at the end of the
    // fade. Smoothstepped so it leaves the landed pose gently — a linear ramp
    // starts the whole body moving on one frame and reads as a twitch.
    const float t =
        std::clamp(1.0f - rig.getup_fade_s / kGetUpFadeS, 0.0f, 1.0f);
    const float weight = t * t * (3.0f - 2.0f * t);

    // Blend the FULLY EVALUATED poses, decomposed. Not the clip's raw parts:
    // by this point evaluate_character_pose() has already applied the stand-up
    // clip's root anchoring, and blending before that and re-anchoring after
    // would snap the root to the clip's own XZ — which is the position pop
    // this whole path exists to remove, arriving one layer down.
    decompose_local_poses(rig.pose.local, fade_parts_);
    ragdoll_getup_blend(rig.landed, fade_parts_, weight, fade_parts_);
    compose_local_poses(fade_parts_, rig.pose.local);
    model.skeleton.compute_skin_matrices(rig.pose.local, rig.pose.skin);
    skin_matrices_to_dual_quaternions(rig.pose.skin, rig.pose.dual_real,
                                      rig.pose.dual_part);
    if (rig.getup_fade_s <= 0.0f) rig.landed.clear();
}

void CharacterVisual::step_ragdoll(Rig& rig, const Model& model,
                                   const PedAgent& agent,
                                   const TerrainCollider* world,
                                   float dt) const {
    if (!rig.ragdolling) {
        // HAND OVER FROM THE FRAME THE ANIMATOR WAS SHOWING, not from bind.
        // advance_rig() has just filled rig.pose.local and rig.world for this
        // frame, and seeding off those is what makes the switch invisible; a
        // bind pose snaps the body to a T for one frame and reads as a glitch
        // from thirty metres away.
        std::array<glm::vec3, kRagdollJointCount> pose{};
        ragdoll_sample_pose(model.ragdoll, rig.pose.local,
                            rig.world.matrix(), pose);

        // The launch is the SIM's launch, not a second opinion about it. The
        // crowd already threw this body — it is integrating the same vector
        // under the same gravity to decide where the person ends up — so
        // taking any other number here would mean the ragdoll and the get-up
        // spot disagree, and the anchor below would spend the fall dragging
        // one to the other.
        const glm::vec3 launch = agent.impact_velocity;
        const glm::vec2 dir = agent.impact_dir_xz;

        // Tumble. Keyed to (person, which knockdown this is) so the same
        // victim hit twice falls differently and any given fall replays
        // identically — hash_coord, never a stream, for the reason
        // core/rng.h gives.
        Rng rng{hash_coord(ped_identity(agent),
                           static_cast<int32_t>(agent.activity_decisions), 0)};
        // Principally about the axis across the direction of travel, which is
        // the pitch that carries somebody over a bonnet. The rest is jitter,
        // so nobody rotates about a perfectly clean axis.
        const glm::vec3 across{-dir.y, 0.0f, dir.x};
        const glm::vec3 spin =
            across * rng.range(kRagdollSpinMin, kRagdollSpinMax) +
            glm::vec3{rng.unit_float(), rng.unit_float(), rng.unit_float()} *
                kRagdollSpinJitter;

        ragdoll_launch(rig.ragdoll, model.ragdoll.figure, pose, launch, spin);
        rig.ragdolling = rig.ragdoll.active;
        if (!rig.ragdolling) return;
    }

    ragdoll_step(rig.ragdoll, model.ragdoll.figure, kRagdollTuning, world,
                 nullptr, dt);

    // Back onto the position the sim owns. agent.pos already carries the
    // crowd's own thrown-body offset, so this is a correction of centimetres
    // and not a leash.
    if (dt > 0.0f) {
        ragdoll_anchor_xz(rig.ragdoll, {agent.pos.x, agent.pos.z},
                          1.0f - std::exp(-dt / kRagdollAnchorTau));
    }

    // THE ANIMATOR'S ROOT, not one of our own at the pelvis.
    //
    // Re-rooting here was tidier for precision and made the get-up crossfade
    // impossible: the ragdoll's bone poses were then relative to an
    // identity-rotation frame at the hips while the clip's were relative to a
    // frame facing agent.fwd, and blending two local poses that do not share a
    // frame re-rotates one of them by the difference. Sharing the root makes
    // the fade a plain part-wise blend and costs nothing — the sim anchor
    // above already keeps the pelvis within centimetres of agent.pos, so the
    // bone-local numbers stay small anyway.
    ragdoll_bone_poses(model.ragdoll, rig.ragdoll, rig.world.matrix(),
                       rig.pose.local);
    model.skeleton.compute_skin_matrices(rig.pose.local, rig.pose.skin);
    skin_matrices_to_dual_quaternions(rig.pose.skin, rig.pose.dual_real,
                                      rig.pose.dual_part);
}

void CharacterVisual::sync(const Crowd& crowd,
                           const PlayerCharacterState& previous_player,
                           const PlayerCharacterState& player, float alpha,
                           int64_t step, bool player_visible, glm::vec3 focus,
                           float presentation_radius_m,
                           const TerrainCollider* world,
                           bool player_dead) {
    driver_visible_ = false;
    const float blend = std::clamp(alpha, 0.0f, 1.0f);
    const float player_yaw = mixed_angle(
        previous_player.facing_yaw, player.facing_yaw, blend);
    Transform player_root = facing_transform(
        glm::mix(previous_player.position, player.position, blend),
        character_forward(player_yaw));
    const double sim_seconds =
        (static_cast<double>(step) + static_cast<double>(blend)) * kSimDt;
    const float dt = player_started_
        ? static_cast<float>(std::max(0.0, sim_seconds - player_sim_seconds_))
        : 0.0f;
    player_sim_seconds_ = sim_seconds;
    player_started_ = true;

    // THE TRANSLATION, player side. Same ten lines, different source.
    CharacterAnimInput input;
    input.identity = kPlayerAnimIdentity;
    input.speed_mps = player_dead ? 0.0f
        : glm::length(glm::vec2{player.velocity.x, player.velocity.z});
    input.sprinting = !player_dead && player.sprinting;
    input.grounded = player_dead || player.grounded;
    // The climb reads straight off the sim state that is moving the body, so
    // the clip cannot disagree with the traverse about where it has got to.
    input.climbing = !player_dead && player.climb.running();
    input.climb_progress = player.climb.duration_s > 0.0f
        ? player.climb.elapsed_s / player.climb.duration_s
        : 0.0f;
    // A corpse throws no punches, and a jab latched on the frame the player
    // died would otherwise fire out of the body on the way down.
    const bool swing = player_punch_.consume();
    input.punch = swing && !player_dead;
    // `dead` selects the authored fall in the animator, exactly as it does for
    // a shot pedestrian in app/ped_impact_pose.h.
    input.dead = player_dead;
    player_animator_.advance(player_model_.clips, input, dt);
    if (player_animator_.consume_punch_contact()) player_punch_contact_ = true;

    player_world_ = model_transform(player_root, player_model_,
                                    player_animator_.plant(player_model_.plants));
    sample_pose(player_model_, player_animator_.sample(), player_pose_);
    if (player_visible && apply_player_weapon_pose(
            player_model_.skeleton, player_model_.local.scale.y,
            player_weapon_pose_, player_pose_.local, weapon_pose_scratch_)) {
        player_model_.skeleton.compute_skin_matrices(player_pose_.local, player_pose_.skin);
        skin_matrices_to_dual_quaternions(player_pose_.skin, player_pose_.dual_real,
                                          player_pose_.dual_part);
    }
    player_visible_ = player_visible;

    sync_ambient(crowd.peds(), sim_seconds, focus, presentation_radius_m, world);
}

void CharacterVisual::sync_ambient(const std::vector<PedAgent>& agents,
                                   double sim_seconds, glm::vec3 focus,
                                   float presentation_radius_m,
                                   const TerrainCollider* world) {
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
            // Same pair, different departure: a different person. Start them
            // fresh rather than inheriting a ragdoll or a get-up fade.
            if (!same_departure(rig.lane_key, rig.slot, rig.generation,
                                agent.lane_key, agent.slot, agent.generation))
                rig = create_rig(agent);
        } else {
            rig = create_rig(agent);
        }
        sync_rig(rig, agent, sim_seconds, world);
        next.push_back(std::move(rig));
    }
    rigs_ = std::move(next);
    sync_staff(sim_seconds, focus, presentation_radius_m);
}

bool CharacterVisual::police_rig_probe(uint64_t lane_key, uint32_t slot,
                                      AmbientRigProbe& out) const {
    for (const PoliceRig& rig : police_rigs_) {
        if (rig.character.lane_key != lane_key || rig.character.slot != slot)
            continue;
        out.generation = rig.character.generation;
        out.model = rig.character.model;
        out.ragdolling = rig.character.ragdolling;
        out.getup_running = rig.character.getup_fade_s > 0.0f ||
                            !rig.character.landed.empty();
        out.started = rig.character.started;
        return true;
    }
    return false;
}

bool CharacterVisual::ambient_rig_probe(uint64_t lane_key, uint32_t slot,
                                       AmbientRigProbe& out) const {
    for (const Rig& rig : rigs_) {
        if (rig.lane_key != lane_key || rig.slot != slot) continue;
        out.generation = rig.generation;
        out.model = rig.model;
        out.ragdolling = rig.ragdolling;
        out.getup_running = rig.getup_fade_s > 0.0f || !rig.landed.empty();
        out.started = rig.started;
        out.clip_time_s = rig.animator.sample().time;
        return true;
    }
    return false;
}

void CharacterVisual::sync_staff(double sim_seconds, glm::vec3 focus,
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
        // A clerk stands still, so their whole performance is the idle. The
        // animator's per-identity phase, rate and idle breaks are what stop two
        // shops full of staff breathing in unison.
        CharacterAnimInput input;
        input.identity = staff.identity;
        advance_rig(rig, npc_models_[rig.model], input, sim_seconds,
                    facing_transform(position,
                                     city::authored_staff_forward(staff)));
        next.push_back(std::move(rig));
    }
    staff_rigs_ = std::move(next);
}

void CharacterVisual::prepare_boat_transition(const PlayerCharacterState& shore) {
    if(!player_model_.loaded) {boat_standing_pose_={};return;}
    const auto root=facing_transform(shore.position,character_forward(shore.facing_yaw));
    boat_standing_world_=model_transform(root,player_model_,0);
    sample_clip(player_model_,CharacterClip::Idle,0,boat_standing_pose_);
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
    const double sim_seconds =
        (static_cast<double>(step) + static_cast<double>(blend)) * kSimDt;
    sync_police_units(crowd.vehicles(), traffic, blend, step, sim_seconds,
                      focus, presentation_radius_m);
}

void CharacterVisual::sync_police_units(
    const std::vector<VehicleAgent>& units, const TrafficVisual& traffic,
    float blend, int64_t step, double sim_seconds, glm::vec3 focus,
    float presentation_radius_m) {
    std::vector<PoliceRig> next;
    next.reserve(units.size());
    police_weapon_sockets_.clear();
    std::size_t old = 0;
    for (const auto& agent : units) {
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
        bool reused = false;
        if (old < police_rigs_.size() &&
            police_rigs_[old].character.lane_key == agent.lane_key &&
            police_rigs_[old].character.slot == agent.slot) {
            rig = std::move(police_rigs_[old++]);
            // One officer rig follows its unit through every occupancy phase,
            // but only while it IS that unit. A new departure at the same pair
            // is a different cruiser with a different officer in it.
            reused = same_departure(
                rig.character.lane_key, rig.character.slot,
                rig.character.generation,
                agent.lane_key, agent.slot, agent.generation);
            if (!reused) rig = PoliceRig{};
        }
        if (!reused) {
            rig.character.lane_key = agent.lane_key;
            rig.character.slot = agent.slot;
            rig.character.generation = agent.generation;
            rig.character.model = static_cast<std::size_t>(splitmix64_mix(
                agent.lane_key ^ (static_cast<uint64_t>(agent.slot) << 32)) %
                police_models_.size());
        }
        const Model& model = police_models_[rig.character.model];
        const float phase = static_cast<float>(
            (agent.lane_key ^ static_cast<uint64_t>(agent.slot)) & 255u) /
            256.0f;
        const float idle_time = (static_cast<float>(step) + blend) *
            static_cast<float>(kSimDt) +
            phase * model.clips.duration(CharacterClip::Idle);
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
                sample_clip(model, CharacterClip::Idle, idle_time, idle);
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

            // THE TRANSLATION, officer side.
            //
            // A shot officer takes the SAME authored bullet fall a civilian
            // takes, selected the same way — the crowd's impact direction
            // against this body's facing. Anything else and the only person in
            // the city you can shoot without knocking down is the one wearing
            // the uniform.
            const bool downed = police_officer_downed(officer);
            CharacterAnimInput input = pedestrian_impact_input(
                downed, officer.impact_from_bullet,
                officer.impact_dir_xz.x * std::sin(officer.heading) -
                    officer.impact_dir_xz.y * std::cos(officer.heading) < 0.0f);
            input.identity = splitmix64_mix(
                agent.lane_key ^ (static_cast<uint64_t>(agent.slot) << 32));
            input.speed_mps = downed ? 0.0f
                                     : step_distance / static_cast<float>(kSimDt);
            input.sprinting = input.speed_mps > kPedRunSpeed;
            advance_rig(rig.character, model, input, sim_seconds, root,
                        officer.impact_from_bullet);
            if (officer.armed) {
                PlayerWeaponPose weapon;
                weapon.weapon = WeaponId::Pistol;
                weapon.equip_blend = 1.0f;
                weapon.aim_blend = 1.0f;
                weapon.recoil = officer.weapon_flash_ticks > 0 ? 1.0f : 0.0f;
                VehicleDriverPose scratch;
                if (apply_player_weapon_pose(model.skeleton, model.local.scale.y,
                        weapon, rig.character.pose.local, scratch)) {
                    model.skeleton.compute_skin_matrices(
                        rig.character.pose.local, rig.character.pose.skin);
                    skin_matrices_to_dual_quaternions(
                        rig.character.pose.skin, rig.character.pose.dual_real,
                        rig.character.pose.dual_part);
                }
            }
        }
        if (!rig.in_vehicle && officer.armed && model.right_hand_bone >= 0 &&
            static_cast<std::size_t>(model.right_hand_bone) <
                rig.character.pose.skin.size()) {
            const std::size_t hand = static_cast<std::size_t>(model.right_hand_bone);
            const glm::mat4 global = rig.character.pose.skin[hand] *
                glm::inverse(model.skeleton.bone(model.right_hand_bone).inverse_bind);
            glm::mat4 socket = rig.character.world.matrix() * global *
                model.right_hand_socket;
            bool valid = true;
            for (int axis = 0; axis < 3; ++axis) {
                const float length = glm::length(glm::vec3{socket[axis]});
                if (!(length > 1e-6f) || !std::isfinite(length)) {
                    valid = false;
                    break;
                }
                socket[axis] = glm::vec4{glm::vec3{socket[axis]} / length, 0.0f};
            }
            if (valid) police_weapon_sockets_.push_back(
                {agent.lane_key, agent.slot, socket, officer.weapon_flash_ticks});
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
    const float max_distance_squared = npc_draw_distance_ * npc_draw_distance_;
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
    police_weapon_sockets_.clear();
    player_pose_ = Pose{};
    player_weapon_pose_ = PlayerWeaponPose{};
    weapon_pose_scratch_ = VehicleDriverPose{};
    boat_standing_pose_ = Pose{};
    player_animator_.reset();
    player_started_ = false;
    player_sim_seconds_ = 0.0;
    player_punch_.clear();
    player_punch_contact_ = false;
    player_visible_ = false;
    driver_visible_ = false;
    driver_pose_ = VehicleDriverPose{};
    last_draw_count_ = 0;
    last_police_draw_count_ = 0;
}

}  // namespace apricot
