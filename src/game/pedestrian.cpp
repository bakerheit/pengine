#include "game/pedestrian.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/log.h"
#include "physics/world_collision.h"
#include "render/mesh.h"
#include "render/shader.h"
#include "scene/aabb.h"
#include "scene/frustum.h"
#include "scene/scene.h"
#include "scene/scene_node.h"
#include "world/road_graph.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

namespace {

constexpr float TARGET_HEIGHT_M = 1.8f;     // mirror player setup
constexpr int   MAX_BONES       = 64;       // skinned shader uniform-array limit
constexpr int   POLICE_MODEL_SLOT = 4;

// Phase-2 anim variety / footstep tuning.
constexpr float PED_REF_SPEED        = 1.4f;  // m/s — walk anim's "design" speed
constexpr float PED_REF_SPRINT_SPEED = 4.5f;  // m/s — sprint anim's "design" speed
constexpr float PED_IDLE_PROBABILITY = 0.10f; // chance of pausing at intersection
constexpr float PED_IDLE_MIN_S       = 1.0f;
constexpr float PED_IDLE_MAX_S       = 3.0f;
// Sprint variety: small fraction of spawned peds run instead of walk
// (people late for the train, joggers, kids). Different anim, faster
// ground speed, faster step cadence — same edge / handoff logic.
constexpr float PED_SPRINT_PROBABILITY = 0.10f;
constexpr float PED_SPRINT_MIN_MPS     = 4.0f;
constexpr float PED_SPRINT_MAX_MPS     = 5.5f;
constexpr float POLICE_CHASE_MPS        = 6.5f;   // sprint after the player
constexpr float POLICE_SHOOT_RANGE      = 32.f;
constexpr float POLICE_SHOOT_PERIOD     = 1.15f;
constexpr float POLICE_EXIT_SECONDS     = 0.65f;
constexpr float POLICE_REENTER_RADIUS   = 1.2f;
// Aim spread, sampled uniformly inside a disk perpendicular to the firing
// direction at the target's distance. Linear ramp from MIN at 0 m to
// MIN + GAIN at POLICE_SHOOT_RANGE — so close-range cops are dangerous,
// long-range cops mostly miss.
constexpr float POLICE_MIN_SPREAD_M     = 0.08f;
constexpr float POLICE_SPREAD_GAIN_M    = 1.8f;
// Shared anims — bound against each model's own skeleton in init(). All
// ped FBXs share Mixamo bone naming so this resolves cleanly across
// models. If a future model breaks this assumption it'll just stand
// motionless / not sprint (channels silently drop), no T-pose.
constexpr const char* PED_WALK_ANIM_PATH =
    ASSETS_DIR "/models/characters/walking.eanim";
constexpr const char* PED_IDLE_ANIM_PATH =
    ASSETS_DIR "/models/characters/breathing_idle.eanim";
constexpr const char* PED_SPRINT_ANIM_PATH =
    ASSETS_DIR "/models/characters/sprint.eanim";
// Pistol locomotion — police-only. Same set the player uses when armed
// (Application.cpp:141-152), rebound against the police skeleton.
constexpr const char* PED_PISTOL_WALK_ANIM_PATH =
    ASSETS_DIR "/models/characters/pistol_walk.eanim";
constexpr const char* PED_PISTOL_RUN_ANIM_PATH =
    ASSETS_DIR "/models/characters/pistol_run.eanim";
constexpr const char* PED_PISTOL_IDLE_ANIM_PATH =
    ASSETS_DIR "/models/characters/pistol_idle.eanim";
// Death anim — shared across all ped models, played once when HP hits 0.
// Phase clamps to (duration - eps) so the corpse pose holds at the last
// frame instead of looping back to standing.
constexpr const char* PED_DYING_ANIM_PATH =
    ASSETS_DIR "/models/characters/dying_backwards.eanim";
constexpr const char* PED_DYING_FORWARD_ANIM_PATH =
    ASSETS_DIR "/models/characters/dying_forwards.eanim";
constexpr float PED_DEATH_DESPAWN_S = 8.0f;
// Trim the lead-in flinch on the dying anim. Mixamo's "Dying Backwards"
// spends the first beat winding up before the actual fall begins; jump
// past it so death reads as more reactive to the bullet.
constexpr float PED_DYING_TRIM_S = 0.4f;
// Gunshot panic. Civilian peds within FLEE_RADIUS_M of the shot origin
// switch to Sprinting (along the sidewalk, U-turning if the sound is
// ahead of them) for FLEE_DURATION_S, then revert to walking. The
// dead-zone keeps peds whose travel is roughly perpendicular to the
// sound from flipping arbitrarily.
constexpr float PED_FLEE_RADIUS_M     = 25.f;
constexpr float PED_FLEE_DURATION_MIN_S = 4.0f;
constexpr float PED_FLEE_DURATION_MAX_S = 7.0f;
constexpr float PED_FLEE_SPEED_MPS    = 5.5f;
constexpr float PED_FLEE_UTURN_DOT    = 0.5f;

// Phase-1 model roster. The character FBXs themselves only ship a static
// bind pose (Mixamo's convention is to keep the actual motion data in
// separate anim FBXs), so we DON'T load a per-character .eanim. Instead
// every model rebinds the shared walking / sprint / idle .eanim against
// its own skeleton. All five rigs share the standard mixamorig:* bone
// naming, so Animation::load resolves the channels cleanly.
struct PedModelDef {
    const char* mesh_path;       // .emesh + .eskel (same basename)
    const char* eskel_path;
    const char* texture_path;
};
constexpr PedModelDef PED_MODELS[] = {
    {ASSETS_DIR "/models/characters/ped_03.emesh",
     ASSETS_DIR "/models/characters/ped_03.eskel",
     ASSETS_DIR "/characters/Characters_psx/Textures/Character_03.png"},
    {ASSETS_DIR "/models/characters/ped_07.emesh",
     ASSETS_DIR "/models/characters/ped_07.eskel",
     ASSETS_DIR "/characters/Characters_psx/Textures/Character_07.png"},
    {ASSETS_DIR "/models/characters/ped_f03.emesh",
     ASSETS_DIR "/models/characters/ped_f03.eskel",
     ASSETS_DIR "/characters/Characters_psx/Textures/Character_Female_03.png"},
    {ASSETS_DIR "/models/characters/ped_f07.emesh",
     ASSETS_DIR "/models/characters/ped_f07.eskel",
     ASSETS_DIR "/characters/Characters_psx/Textures/Character_Female_07.png"},
    {ASSETS_DIR "/models/characters/ped_17_police.emesh",
     ASSETS_DIR "/models/characters/ped_17_police.eskel",
     ASSETS_DIR "/characters/Characters_psx/Textures/Character_17_Female_Police.png"},
};
constexpr int PED_MODEL_COUNT = sizeof(PED_MODELS) / sizeof(PED_MODELS[0]);

inline float frand(std::mt19937& rng, float lo, float hi) {
    return lo + (hi - lo) * (static_cast<float>(rng() & 0xFFFFu) / 65535.f);
}

} // namespace

PedestrianSystem::PedestrianSystem() : rng_(0x9EDA11Eu) {}
PedestrianSystem::~PedestrianSystem() = default;

bool PedestrianSystem::init(Scene& scene, RoadGraph& graph, int target_count) {
    scene_         = &scene;
    graph_         = &graph;
    target_count_  = target_count;
    models_.reserve(PED_MODEL_COUNT);

    for (int i = 0; i < PED_MODEL_COUNT; ++i) {
        const PedModelDef& def = PED_MODELS[i];
        auto m = std::make_unique<ModelAsset>();
        m->police_model = (i == POLICE_MODEL_SLOT);

        if (!load_skinned_emesh(def.mesh_path, m->mesh)) {
            PE_WARN("PedestrianSystem: skinned mesh load failed: %s",
                    def.mesh_path);
            continue;
        }
        if (!m->skeleton.load(def.eskel_path)) {
            PE_WARN("PedestrianSystem: skeleton load failed: %s",
                    def.eskel_path);
            continue;
        }
        // Walk is the shared Mixamo Walking.fbx loop, rebinding against
        // this model's own skeleton. Same bone names = clean resolution.
        if (!m->walk.load(PED_WALK_ANIM_PATH, m->skeleton)) {
            PE_WARN("PedestrianSystem: walk anim load failed for %s",
                    def.eskel_path);
            continue;
        }
        // Idle + sprint are shared (same Mixamo rig), each bound against
        // this model's skeleton. Optional — if either fails, peds simply
        // won't pause / won't sprint, no T-pose because Animation::sample
        // falls back to bind pose for missing channels.
        m->idle_loaded = m->idle.load(PED_IDLE_ANIM_PATH, m->skeleton);
        if (!m->idle_loaded) {
            PE_WARN("PedestrianSystem: idle anim load failed for %s "
                    "(peds will skip pauses)", def.eskel_path);
        }
        m->sprint_loaded = m->sprint.load(PED_SPRINT_ANIM_PATH, m->skeleton);
        if (!m->sprint_loaded) {
            PE_WARN("PedestrianSystem: sprint anim load failed for %s "
                    "(peds will skip running)", def.eskel_path);
        }
        m->dying_loaded = m->dying.load(PED_DYING_ANIM_PATH, m->skeleton);
        if (!m->dying_loaded) {
            PE_WARN("PedestrianSystem: dying anim load failed for %s "
                    "(corpse will hold idle pose)", def.eskel_path);
        } else {
            // Cache the root bone's translation at the trim point of the
            // dying anim — that's the frame compute_pose anchors against,
            // so peds starting their death don't pop short. Sampling at
            // t=0 instead would bake in the lead-in flinch we're trimming.
            std::vector<glm::mat4> t0_pose;
            m->dying.sample(PED_DYING_TRIM_S, m->skeleton, t0_pose);
            for (int b = 0; b < m->skeleton.bone_count(); ++b) {
                if (m->skeleton.bone(b).parent < 0) {
                    m->dying_t0_root_trans =
                        glm::vec3(t0_pose[static_cast<std::size_t>(b)][3]);
                    break;
                }
            }
        }
        m->dying_forward_loaded =
            m->dying_forward.load(PED_DYING_FORWARD_ANIM_PATH, m->skeleton);
        if (!m->dying_forward_loaded) {
            PE_WARN("PedestrianSystem: dying_forward anim load failed for %s "
                    "(back-shot kills will use backward fall)", def.eskel_path);
        } else {
            std::vector<glm::mat4> t0_pose;
            m->dying_forward.sample(PED_DYING_TRIM_S, m->skeleton, t0_pose);
            for (int b = 0; b < m->skeleton.bone_count(); ++b) {
                if (m->skeleton.bone(b).parent < 0) {
                    m->dying_forward_t0_root_trans =
                        glm::vec3(t0_pose[static_cast<std::size_t>(b)][3]);
                    break;
                }
            }
        }

        // Police carry the same Glock the player does, attached to the
        // shared mixamorig:RightHand bone. Cache the bone index + the
        // inverse of its inv-bind matrix so render() can recover the
        // bone's world transform from the per-ped skin matrices.
        if (m->police_model) {
            m->right_hand_bone_idx =
                m->skeleton.find_bone("mixamorig:RightHand");
            if (m->right_hand_bone_idx >= 0) {
                m->right_hand_bind_world = glm::inverse(
                    m->skeleton.bone(m->right_hand_bone_idx).inv_bind);
            } else {
                PE_WARN("PedestrianSystem: RightHand bone missing on "
                        "police rig; gun render disabled");
            }

            // Pistol locomotion — same anims as the player's armed mode.
            // Each load is optional; compute_pose falls back to civilian
            // walk/idle/sprint if any individual anim fails to bind.
            m->pistol_walk_loaded =
                m->pistol_walk.load(PED_PISTOL_WALK_ANIM_PATH, m->skeleton);
            if (!m->pistol_walk_loaded)
                PE_WARN("PedestrianSystem: pistol_walk anim load failed "
                        "for police (chase will use civilian walk)");
            m->pistol_run_loaded =
                m->pistol_run.load(PED_PISTOL_RUN_ANIM_PATH, m->skeleton);
            if (!m->pistol_run_loaded)
                PE_WARN("PedestrianSystem: pistol_run anim load failed "
                        "for police (chase will use civilian sprint)");
            m->pistol_idle_loaded =
                m->pistol_idle.load(PED_PISTOL_IDLE_ANIM_PATH, m->skeleton);
            if (!m->pistol_idle_loaded)
                PE_WARN("PedestrianSystem: pistol_idle anim load failed "
                        "for police (shoot will use civilian idle)");
        }

        Texture tex;
        if (!tex.load_file(def.texture_path)) {
            PE_WARN("PedestrianSystem: texture load failed: %s",
                    def.texture_path);
            // Fall through with no texture; render will fall back to checker.
        } else {
            m->textures.push_back(std::move(tex));
        }

        // Mirror the player's "scale to 1.8 m, centre X/Z, lift Y so feet
        // sit at y=0" setup (Application.cpp:186-196). Skeletal anim drives
        // body motion; this transform just plants the rig on the ground.
        glm::vec3 mn = m->mesh.bounds_min();
        glm::vec3 mx = m->mesh.bounds_max();
        float h_native = std::max(0.001f, mx.y - mn.y);
        m->model_scale  = TARGET_HEIGHT_M / h_native;
        m->visual_offset = {
            -(mn.x + mx.x) * 0.5f * m->model_scale,
            -mn.y                  * m->model_scale,
            -(mn.z + mx.z) * 0.5f * m->model_scale,
        };

        PE_INFO("PedestrianSystem: loaded model[%d] '%s' "
                "bones=%d anim=%.2fs scale=%.3f",
                i, def.mesh_path, m->skeleton.bone_count(),
                m->walk.duration(), m->model_scale);
        models_.push_back(std::move(m));
    }

    if (models_.empty()) {
        PE_WARN("PedestrianSystem: no models loaded; system disabled.");
        return false;
    }
    PE_INFO("PedestrianSystem: %zu model(s) loaded, target=%d",
            models_.size(), target_count_);
    return true;
}

void PedestrianSystem::shutdown() {
    if (scene_) {
        for (auto& p : peds_) {
            if (p->visual_node) scene_->remove_node(p->visual_node);
            if (p->root_node)   scene_->remove_node(p->root_node);
        }
    }
    peds_.clear();
    models_.clear();
    scene_ = nullptr;
    graph_ = nullptr;
}

int PedestrianSystem::active() const {
    return static_cast<int>(peds_.size());
}

bool PedestrianSystem::edge_loaded(const LaneId& edge) const {
    if (!graph_) return false;
    if (!graph_->is_intersection_loaded(edge.i, edge.j)) return false;
    TrafficDirInfo info = traffic_dir_info(edge.dir);
    return graph_->is_intersection_loaded(edge.i + info.di,
                                          edge.j + info.dj);
}

void PedestrianSystem::update(float dt, const glm::vec3& camera_pos,
                              const WorldCollision& world_col) {
    if (!scene_ || !graph_ || models_.empty()) return;

    // Per-frame VFX/audio scratch. advance() appends step events; the
    // caller (Application) drains them after update().
    step_events_.clear();
    police_shots_.clear();
    police_vehicle_events_.clear();

    // Despawn pass: drop peds whose edge unloaded or who drifted past the
    // far ring. Forward iter is fine; destroy_at swaps with the back.
    // Dying peds are exempt — their death timer governs cleanup, otherwise
    // a corpse mid-animation could be ripped out when the player runs
    // past the far ring.
    for (std::size_t k = peds_.size(); k-- > 0;) {
        Pedestrian& p = *peds_[k];
        if (p.state == State::Dying) continue;
        if (p.role == Role::Police) continue;
        glm::vec3 wp = sidewalk_pose(p.edge, p.dist_along, p.side);
        float dx = wp.x - camera_pos.x, dz = wp.z - camera_pos.z;
        bool too_far = (dx*dx + dz*dz) > (despawn_dist_ * despawn_dist_);
        if (too_far || !edge_loaded(p.edge)) destroy_at(k);
    }

    // Spawn budget: small per-frame so a freshly streamed area doesn't
    // dump 30 peds at once, and cheap loops aren't a hot path.
    int budget = 2;
    while (static_cast<int>(peds_.size()) < target_count_ && budget-- > 0) {
        if (!try_spawn(camera_pos)) break;
    }

    for (std::size_t k = peds_.size(); k-- > 0;) {
        Pedestrian& p = *peds_[k];
        if (p.state == State::Dying) {
            // Death anim plays once and freezes at the last frame so the
            // body holds the final pose. death_timer drives the despawn.
            // Clamp against whichever anim is actually playing — forward
            // fall is shorter than backward, and using the wrong duration
            // lets anim_phase grow past the active anim's end, where
            // Animation::sample fmod-wraps it and the corpse loops back
            // to the start of the fall.
            const ModelAsset& dm =
                *models_[static_cast<std::size_t>(p.model_id)];
            float dur = 0.f;
            if (p.dying_forward && dm.dying_forward_loaded)
                dur = dm.dying_forward.duration();
            else if (dm.dying_loaded)
                dur = dm.dying.duration();
            else if (dm.dying_forward_loaded)
                dur = dm.dying_forward.duration();
            if (dur > 0.f) {
                p.anim_phase += dt;
                if (p.anim_phase > dur - 0.01f) p.anim_phase = dur - 0.01f;
            }
            p.death_timer += dt;
            sync_visual(p);
            compute_pose(p);
            if (p.death_timer >= PED_DEATH_DESPAWN_S) destroy_at(k);
            continue;
        }
        if (p.role == Role::Police) {
            advance_police(p, dt, world_col);
            if (p.state_timer < -9000.f) destroy_at(k);
        } else {
            advance(p, dt);
        }
    }
}

void PedestrianSystem::set_police_context(int wanted_level,
                                           const glm::vec3& target_pos) {
    police_wanted_level_ = std::clamp(wanted_level, 0, 5);
    police_target_pos_ = target_pos;
}

bool PedestrianSystem::has_police_for_car(const void* car_id) const {
    if (!car_id) return false;
    for (const auto& p : peds_) {
        if (p && p->role == Role::Police && p->source_car_id == car_id)
            return true;
    }
    return false;
}

bool PedestrianSystem::spawn_police_officer(const glm::vec3& exit_pos,
                                            float yaw_deg,
                                            const void* car_id) {
    if (!scene_ || models_.empty() || has_police_for_car(car_id)) return false;

    int model_id = -1;
    for (std::size_t i = 0; i < models_.size(); ++i) {
        if (models_[i] && models_[i]->police_model) {
            model_id = static_cast<int>(i);
            break;
        }
    }
    if (model_id < 0) return false;

    const ModelAsset& m = *models_[static_cast<std::size_t>(model_id)];
    auto p = std::make_unique<Pedestrian>();
    p->role          = Role::Police;
    p->model_id      = model_id;
    p->texture_id    = m.textures.empty() ? -1 : 0;
    p->world_pos     = exit_pos;
    p->vehicle_pos   = exit_pos;
    p->source_car_id = car_id;
    p->yaw_deg       = yaw_deg;
    p->speed_mps     = POLICE_CHASE_MPS;
    p->state         = State::PoliceExitVehicle;
    p->state_timer   = POLICE_EXIT_SECONDS;
    p->shoot_cooldown = 0.35f
        + frand(rng_, 0.f, POLICE_SHOOT_PERIOD * 0.4f);
    p->anim_phase = frand(rng_, 0.f, std::max(0.05f, m.walk.duration()));

    p->root_node   = scene_->create_node();
    p->visual_node = scene_->create_node(p->root_node);
    p->visual_node->transform.position = m.visual_offset;
    p->visual_node->transform.scale    = glm::vec3{m.model_scale};
    p->local_poses.resize(static_cast<std::size_t>(m.skeleton.bone_count()));
    p->skin_matrices.resize(static_cast<std::size_t>(m.skeleton.bone_count()));

    sync_visual(*p);
    compute_pose(*p);
    peds_.push_back(std::move(p));
    return true;
}

bool PedestrianSystem::try_spawn(const glm::vec3& camera_pos) {
    if (!graph_ || graph_->loaded_cell_count() == 0) return false;

    for (int attempt = 0; attempt < 16; ++attempt) {
        int i, j;
        if (!graph_->random_loaded_intersection(rng_, i, j)) return false;

        std::array<GridDir, 4> options;
        int n = graph_->outgoing(i, j, options);
        if (n == 0) continue;
        GridDir dir = options[static_cast<std::size_t>(rng_() %
                                static_cast<std::uint32_t>(n))];

        LaneId edge{i, j, dir};
        if (!edge_loaded(edge)) continue;

        float along = frand(rng_, 4.f, ROAD_PITCH - 4.f);
        float side  = (rng_() & 1u) ? +1.f : -1.f;

        glm::vec3 wp = sidewalk_pose(edge, along, side);
        float dx = wp.x - camera_pos.x, dz = wp.z - camera_pos.z;
        float d2 = dx*dx + dz*dz;
        if (d2 < spawn_min_ * spawn_min_) continue;
        if (d2 > spawn_max_ * spawn_max_) continue;

        int model_id = -1;
        for (int pick = 0; pick < 8; ++pick) {
            int candidate = static_cast<int>(rng_() %
                static_cast<std::uint32_t>(models_.size()));
            if (!models_[static_cast<std::size_t>(candidate)]->police_model) {
                model_id = candidate;
                break;
            }
        }
        if (model_id < 0) continue;
        const ModelAsset& m = *models_[static_cast<std::size_t>(model_id)];
        int texture_id = m.textures.empty() ? -1 : 0;

        auto p = std::make_unique<Pedestrian>();
        p->model_id    = model_id;
        p->texture_id  = texture_id;
        p->edge        = edge;
        p->side        = side;
        p->dist_along  = along;
        p->yaw_deg     = sidewalk_yaw_deg(edge);

        // Pick walking vs sprinting at spawn. Most peds walk; a small
        // fraction run (joggers, late commuters). Sprint requires the
        // anim to have loaded for this model, otherwise demote to walk
        // so we don't get a T-pose runner.
        bool sprint = m.sprint_loaded
                   && frand(rng_, 0.f, 1.f) < PED_SPRINT_PROBABILITY;
        if (sprint) {
            p->state     = State::Sprinting;
            p->speed_mps = frand(rng_, PED_SPRINT_MIN_MPS, PED_SPRINT_MAX_MPS);
            p->anim_phase = frand(rng_, 0.f,
                                   std::max(0.05f, m.sprint.duration()));
        } else {
            p->state     = State::Walking;
            p->speed_mps = frand(rng_, 1.0f, 1.7f);
            // Phase desync at spawn so peds aren't all on the same step.
            p->anim_phase = frand(rng_, 0.f,
                                   std::max(0.05f, m.walk.duration()));
        }

        // Scene-graph subtree — same shape as the player's character_node /
        // character_visual_node split (Application.cpp:184-196).
        p->root_node   = scene_->create_node();
        p->visual_node = scene_->create_node(p->root_node);
        p->visual_node->transform.position = m.visual_offset;
        p->visual_node->transform.scale    = glm::vec3{m.model_scale};

        p->local_poses.resize(static_cast<std::size_t>(m.skeleton.bone_count()));
        p->skin_matrices.resize(static_cast<std::size_t>(m.skeleton.bone_count()));

        sync_visual(*p);
        compute_pose(*p);

        peds_.push_back(std::move(p));
        return true;
    }
    return false;
}

void PedestrianSystem::destroy_at(std::size_t idx) {
    if (idx >= peds_.size()) return;
    Pedestrian& p = *peds_[idx];
    if (scene_) {
        if (p.visual_node) scene_->remove_node(p.visual_node);
        if (p.root_node)   scene_->remove_node(p.root_node);
    }
    peds_.erase(peds_.begin() +
                static_cast<std::ptrdiff_t>(idx));
}

void PedestrianSystem::apply_damage(std::size_t ped_idx, float amount,
                                    const glm::vec3& shot_origin) {
    if (ped_idx >= peds_.size()) return;
    Pedestrian& p = *peds_[ped_idx];
    if (p.state == State::Dying) return;
    p.health -= amount;
    if (p.health > 0.f) return;

    // Front/back hit detection. Bullet direction = (ped - shot_origin).
    // Ped forward in world space derived from yaw_deg via the same
    // convention as sidewalk_yaw_deg (atan2(unit.z, unit.x)).
    // dot(bullet_dir, ped_forward) > 0 ⇒ bullet entered from BEHIND ⇒
    // forward fall. < 0 ⇒ entered the front ⇒ backward fall.
    glm::vec3 ped_pos = (p.role == Role::Police)
        ? p.world_pos
        : sidewalk_pose(p.edge, p.dist_along, p.side);
    glm::vec3 to_ped = ped_pos - shot_origin;
    to_ped.y = 0.f;
    float yaw_rad = glm::radians(p.yaw_deg);
    glm::vec3 ped_forward{std::cos(yaw_rad), 0.f, std::sin(yaw_rad)};
    float facing_dot = glm::dot(to_ped, ped_forward);
    p.dying_forward = (facing_dot > 0.f);

    p.state            = State::Dying;
    p.anim_phase       = PED_DYING_TRIM_S;
    p.death_timer      = 0.f;
    p.last_step_bucket = -1;     // suppress further footsteps
    // Effectively disable any in-flight police behavior. shoot_cooldown
    // is consulted by advance_police, but the Dying short-circuit in
    // update() runs first; this is belt-and-braces.
    p.shoot_cooldown   = 1e9f;
}

void PedestrianSystem::notify_gunshot(const glm::vec3& origin) {
    if (peds_.empty()) return;
    const float r2 = PED_FLEE_RADIUS_M * PED_FLEE_RADIUS_M;

    for (auto& pp : peds_) {
        Pedestrian& p = *pp;
        if (p.role != Role::Civilian)   continue;
        if (p.state == State::Dying)    continue;

        glm::vec3 ped_pos = sidewalk_pose(p.edge, p.dist_along, p.side);
        glm::vec3 to_ped  = ped_pos - origin;
        to_ped.y = 0.f;
        float d2 = glm::dot(to_ped, to_ped);
        if (d2 > r2) continue;

        // Forward direction along the sidewalk for this ped's current edge.
        TrafficDirInfo info = traffic_dir_info(p.edge.dir);
        glm::vec3 forward{static_cast<float>(info.di), 0.f,
                           static_cast<float>(info.dj)};
        // forward is already unit-length for axis-aligned grid dirs.

        // Sound is "ahead" of the ped if dot(forward, sound - ped) > 0.
        // We want them moving away — flip the edge if forward is taking
        // them toward the source. Dead-zone via UTURN_DOT keeps roughly
        // perpendicular cases stable.
        float along = glm::dot(forward, -to_ped); // -to_ped = sound - ped
        if (along > PED_FLEE_UTURN_DOT) {
            // U-turn on the same physical sidewalk. Flipping edge.dir +
            // dist_along + side keeps the world position continuous.
            TrafficDirInfo cur = traffic_dir_info(p.edge.dir);
            int ni = p.edge.i + cur.di;
            int nj = p.edge.j + cur.dj;
            p.edge       = LaneId{ni, nj, opposite(p.edge.dir)};
            p.dist_along = ROAD_PITCH - p.dist_along;
            p.side       = -p.side;
            p.yaw_deg    = sidewalk_yaw_deg(p.edge);
        }

        // Snap any in-flight idle pause; switch to sprinting.
        p.idle_remaining   = 0.f;
        p.state            = State::Sprinting;
        p.speed_mps        = PED_FLEE_SPEED_MPS;
        p.anim_phase       = 0.f;
        p.last_step_bucket = -1;
        // max() so a second nearby shot extends panic instead of cutting
        // it short by re-rolling a smaller value.
        float new_remaining = frand(rng_, PED_FLEE_DURATION_MIN_S,
                                          PED_FLEE_DURATION_MAX_S);
        if (new_remaining > p.flee_remaining)
            p.flee_remaining = new_remaining;
    }
}

PedestrianSystem::RayHit PedestrianSystem::raycast(
        const glm::vec3& origin, const glm::vec3& dir, float max_dist) const {
    RayHit out;
    if (max_dist <= 0.f) return out;

    // Slab test. dir need not be normalised — we trust the caller. inv may
    // be ±inf when a component is zero; that's fine for the slab math as
    // long as the origin's matching axis is inside the slab.
    const glm::vec3 inv{
        dir.x != 0.f ? 1.f / dir.x : std::numeric_limits<float>::infinity(),
        dir.y != 0.f ? 1.f / dir.y : std::numeric_limits<float>::infinity(),
        dir.z != 0.f ? 1.f / dir.z : std::numeric_limits<float>::infinity(),
    };

    float closest = max_dist;
    for (std::size_t i = 0; i < peds_.size(); ++i) {
        const Pedestrian& p = *peds_[i];
        if (p.state == State::Dying) continue;
        const glm::vec3 wp = p.role == Role::Police
            ? p.world_pos
            : sidewalk_pose(p.edge, p.dist_along, p.side);
        // Tighter than the cull AABB at render(): ~0.4m horizontal radius,
        // 1.85m tall — roughly the silhouette of a standing human.
        const glm::vec3 box_min = wp + glm::vec3{-0.4f, 0.f,   -0.4f};
        const glm::vec3 box_max = wp + glm::vec3{ 0.4f, 1.85f,  0.4f};

        const glm::vec3 t1 = (box_min - origin) * inv;
        const glm::vec3 t2 = (box_max - origin) * inv;
        const glm::vec3 tmin3 = glm::min(t1, t2);
        const glm::vec3 tmax3 = glm::max(t1, t2);
        const float tmin = std::max({tmin3.x, tmin3.y, tmin3.z});
        const float tmax = std::min({tmax3.x, tmax3.y, tmax3.z});
        if (tmax < 0.f || tmin > tmax) continue;
        const float t_hit = tmin >= 0.f ? tmin : tmax;
        if (t_hit <= 0.f || t_hit >= closest) continue;

        closest = t_hit;
        out.hit = true;
        out.t = t_hit;
        out.position = origin + dir * t_hit;
        out.ped_idx = i;
    }
    return out;
}

void PedestrianSystem::advance_police(Pedestrian& p, float dt,
                                      const WorldCollision& world_col) {
    const ModelAsset& m = *models_[static_cast<std::size_t>(p.model_id)];

    // Cylinder dims used for both movement-resolve and the kill-box raycast
    // (pedestrian.cpp:466-467). 0.4 m radius, 1.85 m tall — standing human.
    constexpr float POLICE_RADIUS = 0.4f;
    constexpr float POLICE_HEIGHT = 1.85f;

    auto resolve_against_buildings = [&](Pedestrian& q) {
        glm::vec2 fixed = world_col.resolve_cylinder_xz(
            {q.world_pos.x, q.world_pos.z},
            q.world_pos.y, POLICE_HEIGHT, POLICE_RADIUS);
        q.world_pos.x = fixed.x;
        q.world_pos.z = fixed.y;
    };

    if (police_wanted_level_ <= 3 && p.state != State::PoliceEnterVehicle) {
        p.state = State::PoliceEnterVehicle;
        p.state_timer = 0.f;
    }

    if (p.state == State::PoliceExitVehicle) {
        p.state_timer -= dt;
        p.anim_phase += dt;
        if (p.state_timer <= 0.f)
            p.state = State::PoliceChase;
        sync_visual(p);
        compute_pose(p);
        return;
    }

    glm::vec3 target = police_wanted_level_ > 3
        ? police_target_pos_
        : p.vehicle_pos;
    glm::vec3 delta = target - p.world_pos;
    delta.y = 0.f;
    float dist = glm::length(delta);
    glm::vec3 dir = dist > 1e-4f ? delta / dist : glm::vec3{0.f};
    if (dist > 1e-4f)
        p.yaw_deg = glm::degrees(std::atan2(dir.z, dir.x));

    if (p.state == State::PoliceEnterVehicle) {
        if (dist <= POLICE_REENTER_RADIUS) {
            police_vehicle_events_.push_back({p.source_car_id});
            p.state_timer = -10000.f; // update() removes after this tick
            return;
        }
        p.world_pos += dir * (POLICE_CHASE_MPS * dt);
        resolve_against_buildings(p);
        // Reference at sprint speed: pistol_run is the active anim, so a
        // walk-speed reference would wind it up to ~3x cadence (windmill).
        p.anim_phase += dt
            * std::max(0.05f, POLICE_CHASE_MPS / PED_REF_SPRINT_SPEED);
        sync_visual(p);
        compute_pose(p);
        return;
    }

    // Decide chase-vs-shoot. Shoot requires line-of-sight: a cop with a
    // building between them and the player must keep chasing (and stay in
    // the run anim) instead of standing still pretending to fire — the
    // bullet itself would be blocked by Application's raycast either way,
    // but the *posture* needs to react to LOS too.
    bool in_range = (police_wanted_level_ > 3) && (dist <= POLICE_SHOOT_RANGE);
    bool has_los  = false;
    if (in_range) {
        glm::vec3 muzzle = p.world_pos + glm::vec3{0.f, 1.45f, 0.f};
        glm::vec3 aim    = police_target_pos_ + glm::vec3{0.f, 1.2f, 0.f};
        glm::vec3 to_aim = aim - muzzle;
        float aim_dist = glm::length(to_aim);
        if (aim_dist > 1e-4f) {
            glm::vec3 dir_aim = to_aim / aim_dist;
            ::pengine::RayHit blk =
                world_col.raycast(muzzle, dir_aim, aim_dist);
            // Match Application.cpp:825's 0.6m tolerance — within a wall's
            // thickness of the target counts as clear LOS.
            has_los = !(blk.hit && blk.t < aim_dist - 0.6f);
        }
    }
    bool can_shoot = in_range && has_los;
    p.state = can_shoot ? State::PoliceShoot : State::PoliceChase;

    if (can_shoot) {
        p.anim_phase += dt;
        p.shoot_cooldown -= dt;
        if (p.shoot_cooldown <= 0.f) {
            glm::vec3 muzzle = p.world_pos + glm::vec3{0.f, 1.45f, 0.f};
            glm::vec3 aim_true = police_target_pos_
                               + glm::vec3{0.f, 1.2f, 0.f};
            glm::vec3 to_aim = aim_true - muzzle;
            float aim_dist = glm::length(to_aim);
            glm::vec3 aim_dir = aim_dist > 1e-4f
                ? to_aim / aim_dist
                : glm::vec3{0.f, 0.f, 1.f};

            // Perpendicular basis for the spread disk. A near-vertical
            // aim (cop firing up at a rooftop player) needs a non-Y up
            // reference or the cross product collapses.
            glm::vec3 up = std::abs(aim_dir.y) > 0.9f
                ? glm::vec3{1.f, 0.f, 0.f}
                : glm::vec3{0.f, 1.f, 0.f};
            glm::vec3 perp_a = glm::normalize(glm::cross(up, aim_dir));
            glm::vec3 perp_b = glm::cross(aim_dir, perp_a);

            float t_dist = std::clamp(aim_dist / POLICE_SHOOT_RANGE,
                                       0.f, 1.f);
            float spread = POLICE_MIN_SPREAD_M
                         + t_dist * POLICE_SPREAD_GAIN_M;
            // Uniform-disk sampling: sqrt(u) avoids over-density at centre.
            float ang = frand(rng_, 0.f, glm::two_pi<float>());
            float r   = spread * std::sqrt(frand(rng_, 0.f, 1.f));
            glm::vec3 jitter = perp_a * (r * std::cos(ang))
                             + perp_b * (r * std::sin(ang));

            police_shots_.push_back({muzzle, aim_true + jitter});
            p.shoot_cooldown = POLICE_SHOOT_PERIOD
                + frand(rng_, -0.15f, 0.25f);
        }
    } else {
        p.world_pos += dir * (POLICE_CHASE_MPS * dt);
        resolve_against_buildings(p);
        p.anim_phase += dt
            * std::max(0.05f, POLICE_CHASE_MPS / PED_REF_SPRINT_SPEED);
    }

    (void)m;
    sync_visual(p);
    compute_pose(p);
}

void PedestrianSystem::advance(Pedestrian& p, float dt) {
    const ModelAsset& m = *models_[static_cast<std::size_t>(p.model_id)];

    // Gunshot panic ticking down. Once expired, drop back to a normal
    // walk speed and Walking state — but only if we were sprinting on
    // panic (a jogger spawned in Sprinting has flee_remaining = 0 and
    // never enters this branch, so they keep running).
    if (p.flee_remaining > 0.f) {
        p.flee_remaining -= dt;
        if (p.flee_remaining <= 0.f) {
            p.flee_remaining = 0.f;
            p.state          = State::Walking;
            p.speed_mps      = frand(rng_, 1.0f, 1.7f);
            p.anim_phase     = 0.f;
            p.last_step_bucket = -1;
        }
    }

    if (p.state == State::Idle) {
        // Stand still; let the idle anim play in real time. Stop emitting
        // step events while idle (no foot strikes happening).
        p.idle_remaining -= dt;
        p.anim_phase += dt;
        if (p.idle_remaining <= 0.f) {
            p.state = State::Walking;
            p.anim_phase = 0.f;       // restart walk loop from frame 0
            p.last_step_bucket = 0;   // first step fires after one period
        }
        sync_visual(p);
        compute_pose(p);
        return;
    }

    // Pick the active locomotion anim + design speed. Sprinters use the
    // sprint loop and reference at PED_REF_SPRINT_SPEED so a 5 m/s ped
    // doesn't wind the sprint cycle into a blur. Walkers use the walk
    // loop at PED_REF_SPEED — same scaling as the player walk_phase at
    // Application.cpp:528.
    const bool is_sprint = (p.state == State::Sprinting && m.sprint_loaded);
    const Animation& loco_anim = is_sprint ? m.sprint : m.walk;
    const float      ref_speed = is_sprint ? PED_REF_SPRINT_SPEED
                                           : PED_REF_SPEED;
    float phase_rate = std::max(0.05f, p.speed_mps / ref_speed);
    p.dist_along += p.speed_mps * dt;
    p.anim_phase += dt * phase_rate;

    // Footstep emission: each half-stride boundary in anim_phase fires
    // one event (left foot, right foot). step_period derived from the
    // currently-playing anim so sprinters click out steps faster than
    // walkers. Falls back to 0.5 s if the anim's duration is missing.
    if (loco_anim.duration() > 0.f) {
        float step_period = std::max(0.05f, loco_anim.duration() * 0.5f);
        int   bucket      = static_cast<int>(p.anim_phase / step_period);
        if (p.last_step_bucket >= 0 && bucket > p.last_step_bucket) {
            int catchup = bucket - p.last_step_bucket;
            // Cap catch-up to avoid a frame-spike spawning many events.
            if (catchup > 2) catchup = 2;
            for (int k = 0; k < catchup; ++k) {
                glm::vec3 wp = sidewalk_pose(p.edge, p.dist_along, p.side);
                step_events_.push_back({wp});
            }
        }
        p.last_step_bucket = bucket;
    }

    // Edge handoff: at the far intersection, pick a random outgoing dir
    // (excluding the one we came from unless it's the only option). With
    // low probability, transition to an idle pause instead of stepping
    // straight onto the next edge.
    if (p.dist_along >= ROAD_PITCH) {
        TrafficDirInfo info = traffic_dir_info(p.edge.dir);
        int ni = p.edge.i + info.di;
        int nj = p.edge.j + info.dj;

        std::array<GridDir, 4> options;
        int n = graph_->outgoing(ni, nj, options);
        if (n == 0) {
            // Nowhere to go (unloaded neighbour) — let despawn pass clean us
            // up; until then keep the position clamped at the intersection.
            p.dist_along = ROAD_PITCH;
        } else {
            GridDir came_from = opposite(p.edge.dir);
            std::array<GridDir, 4> filtered;
            int fn = 0;
            for (int k = 0; k < n; ++k) {
                if (options[static_cast<std::size_t>(k)] != came_from) {
                    filtered[static_cast<std::size_t>(fn++)] =
                        options[static_cast<std::size_t>(k)];
                }
            }
            GridDir new_dir = (fn > 0)
                ? filtered[static_cast<std::size_t>(rng_() %
                            static_cast<std::uint32_t>(fn))]
                : came_from; // dead end: U-turn

            p.edge      = LaneId{ni, nj, new_dir};
            p.dist_along -= ROAD_PITCH;
            p.yaw_deg    = sidewalk_yaw_deg(p.edge);
            // side stays the same (peds don't switch sidewalk-side at
            // corners in v1 — that's a phase-3 behaviour).

            // Roll for an idle pause at the corner. Walkers only —
            // sprinters keep going (they're going somewhere). Skip if
            // the model has no idle anim (we'd just freeze on a walk
            // frame).
            if (p.state == State::Walking && m.idle_loaded
                && frand(rng_, 0.f, 1.f) < PED_IDLE_PROBABILITY) {
                p.state = State::Idle;
                p.idle_remaining = frand(rng_, PED_IDLE_MIN_S,
                                          PED_IDLE_MAX_S);
                p.anim_phase = 0.f; // restart idle loop from frame 0
            }
        }
    }

    sync_visual(p);
    compute_pose(p);
}

void PedestrianSystem::sync_visual(Pedestrian& p) {
    glm::vec3 wp = p.role == Role::Police
        ? p.world_pos
        : sidewalk_pose(p.edge, p.dist_along, p.side);

    // Matches the player's facing-yaw → quaternion convention at
    // Application.cpp:804-806. yaw_deg uses the traffic_dir_info table
    // (S/W/N/E = 0/90/180/270), so the body's local +Z axis ends up
    // pointing along the travel direction.
    p.root_node->transform.position = wp;
    p.root_node->transform.rotation =
        glm::angleAxis(glm::radians(-p.yaw_deg + 90.f),
                        glm::vec3{0.f, 1.f, 0.f});
    p.root_node->transform.scale    = {1.f, 1.f, 1.f};
    p.root_node->mark_dirty();
}

void PedestrianSystem::compute_pose(Pedestrian& p) {
    const ModelAsset& m = *models_[static_cast<std::size_t>(p.model_id)];

    const Animation* anim = nullptr;
    switch (p.state) {
        case State::Idle:
            if (m.idle_loaded) anim = &m.idle;
            break;
        case State::Sprinting:
            if (m.sprint_loaded) anim = &m.sprint;
            break;
        case State::PoliceShoot:
        case State::PoliceExitVehicle:
            if      (m.pistol_idle_loaded) anim = &m.pistol_idle;
            else if (m.idle_loaded)        anim = &m.idle;
            break;
        case State::PoliceChase:
        case State::PoliceEnterVehicle:
            // Police chase at sprint speed (POLICE_CHASE_MPS), so use
            // the run pose; fall back to civilian sprint, then walk.
            if      (m.pistol_run_loaded)  anim = &m.pistol_run;
            else if (m.pistol_walk_loaded) anim = &m.pistol_walk;
            else if (m.sprint_loaded)      anim = &m.sprint;
            break;
        case State::Dying:
            // Pick forward or backward fall based on entry direction.
            // Each falls back to the other dying anim, then idle.
            if (p.dying_forward && m.dying_forward_loaded)
                anim = &m.dying_forward;
            else if (m.dying_loaded)
                anim = &m.dying;
            else if (m.dying_forward_loaded)
                anim = &m.dying_forward;
            else if (m.idle_loaded)
                anim = &m.idle;
            break;
        case State::Walking:
            break;
    }
    if (!anim && m.walk.duration() > 0.f) anim = &m.walk;
    if (!anim || anim->duration() <= 0.f) return;

    anim->sample(p.anim_phase, m.skeleton, p.local_poses);

    // Root-translation handling.
    //
    // Non-dying states: strip XZ (game logic drives world XZ — the anim's
    // forward translation would otherwise compound). Keep Y from the anim
    // so each anim's authored crouch height (e.g. pistol_idle bends the
    // knees and lowers the hips ~5 cm) still puts the feet on the ground
    // instead of forcing the hips to bind and floating the bent legs up.
    //
    // Dying: anchor the rig at bind translation, then add the anim's
    // delta from its own t=0. This gives a seamless transition from the
    // previous state (no instant height pop, since Mixamo's death anim
    // authors hips ~25 cm below bind at t=0) while still letting the
    // body fall and slide back as the anim authored.
    int n = m.skeleton.bone_count();
    const bool dying_anim_active = (p.state == State::Dying)
        && (m.dying_loaded || m.dying_forward_loaded);
    if (dying_anim_active) {
        // Pick the t=0 baseline that matches whichever death anim
        // compute_pose actually selected above.
        const bool used_forward = p.dying_forward && m.dying_forward_loaded;
        const glm::vec3 t0_trans = used_forward
            ? m.dying_forward_t0_root_trans
            : m.dying_t0_root_trans;
        for (int b = 0; b < n; ++b) {
            if (m.skeleton.bone(b).parent < 0) {
                glm::vec3 bind_t{m.skeleton.bone(b).bind_local[3]};
                glm::vec3 anim_t{p.local_poses[
                    static_cast<std::size_t>(b)][3]};
                glm::vec3 delta = anim_t - t0_trans;
                p.local_poses[static_cast<std::size_t>(b)][3] =
                    glm::vec4{bind_t + delta, 1.f};
            }
        }
    } else {
        for (int b = 0; b < n; ++b) {
            if (m.skeleton.bone(b).parent < 0) {
                glm::vec3 bind_t{m.skeleton.bone(b).bind_local[3]};
                glm::vec4& col3 = p.local_poses[
                    static_cast<std::size_t>(b)][3];
                col3.x = bind_t.x;
                col3.z = bind_t.z;
                col3.w = 1.f;
            }
        }
    }

    m.skeleton.compute_skin_matrices(p.local_poses, p.skin_matrices);
}

void PedestrianSystem::render(Shader& skinned_shader, Shader& lit_shader,
                              const glm::mat4& view_proj,
                              const glm::vec3& cam_pos,
                              const Texture& fallback_tex,
                              const Mesh& gun_mesh,
                              const glm::mat4& gun_grip_offset,
                              const Frustum& frustum) const {
    if (peds_.empty() || models_.empty()) return;

    skinned_shader.use();
    skinned_shader.set("u_view_proj",   view_proj);
    skinned_shader.set("u_cam_pos",     cam_pos);
    skinned_shader.set("u_light_dir",
                        glm::normalize(glm::vec3{0.6f, 1.f, 0.4f}));
    skinned_shader.set("u_light_color", glm::vec3{1.f, 0.95f, 0.85f});
    skinned_shader.set("u_ambient",     glm::vec3{0.18f, 0.22f, 0.28f});
    skinned_shader.set("u_diffuse",     0);
    skinned_shader.set("u_tint",        glm::vec3{1.f});
    skinned_shader.set("u_uv_scale",    glm::vec2{1.f, 1.f});

    // Track which police peds passed culling so the gun pass below can
    // skip the cull / skeleton-bounds work a second time.
    std::vector<const Pedestrian*> visible_police;
    visible_police.reserve(peds_.size());

    for (const auto& pp : peds_) {
        const Pedestrian& p = *pp;
        if (!p.visual_node || p.skin_matrices.empty()) continue;

        // Cheap cull: 2 m³ AABB centred on the ped position. Sufficient
        // since we only need to skip behind-camera peds — actual mesh
        // bounds aren't worth the per-frame work for v1.
        glm::vec3 wp = p.root_node->transform.position;
        AABB box{ wp - glm::vec3{1.f, 0.f, 1.f},
                  wp + glm::vec3{1.f, 2.2f, 1.f} };
        if (frustum.cull(box)) continue;

        const ModelAsset& m = *models_[static_cast<std::size_t>(p.model_id)];
        glm::mat4 world = p.visual_node->world_matrix();
        skinned_shader.set("u_model",      world);
        skinned_shader.set("u_normal_mat",
                            glm::mat3(glm::inverseTranspose(world)));

        int nb = static_cast<int>(p.skin_matrices.size());
        if (nb > MAX_BONES) nb = MAX_BONES;
        skinned_shader.set_mat4_array("u_bones",
                                       p.skin_matrices.data(), nb);

        if (p.texture_id >= 0
            && p.texture_id < static_cast<int>(m.textures.size())
            && m.textures[static_cast<std::size_t>(p.texture_id)].id()) {
            m.textures[static_cast<std::size_t>(p.texture_id)].bind(0);
        } else {
            fallback_tex.bind(0);
        }
        m.mesh.draw();

        if (p.role == Role::Police) visible_police.push_back(&p);
    }

    // ---- Police gun pass: same matrix chain as Application.cpp:1166-1193,
    //      so the hold matches the player's exactly. -------------------------
    if (visible_police.empty() || gun_mesh.index_count() == 0) return;

    lit_shader.use();
    lit_shader.set("u_view_proj",   view_proj);
    lit_shader.set("u_cam_pos",     cam_pos);
    lit_shader.set("u_light_dir",
                    glm::normalize(glm::vec3{0.6f, 1.f, 0.4f}));
    lit_shader.set("u_light_color", glm::vec3{1.f, 0.95f, 0.85f});
    lit_shader.set("u_ambient",     glm::vec3{0.18f, 0.22f, 0.28f});
    lit_shader.set("u_diffuse",     0);
    fallback_tex.bind(0);

    // GUN_SCALE: must match Application.cpp's player gun scale or the
    // pistol will look bigger/smaller in police hands than in yours.
    constexpr float GUN_SCALE = 0.3f;
    const glm::mat4 scale_mat =
        glm::scale(glm::mat4{1.f}, glm::vec3{GUN_SCALE});

    for (const Pedestrian* pp : visible_police) {
        const Pedestrian& p = *pp;
        const ModelAsset& m = *models_[static_cast<std::size_t>(p.model_id)];
        if (m.right_hand_bone_idx < 0) continue;
        if (m.right_hand_bone_idx >=
            static_cast<int>(p.skin_matrices.size())) continue;

        glm::mat4 bone_world = p.skin_matrices[
            static_cast<std::size_t>(m.right_hand_bone_idx)]
                              * m.right_hand_bind_world;
        glm::mat4 gun_world = p.visual_node->world_matrix()
                            * bone_world * scale_mat * gun_grip_offset;

        lit_shader.set("u_model",      gun_world);
        lit_shader.set("u_normal_mat",
                        glm::mat3(glm::inverseTranspose(gun_world)));
        gun_mesh.draw();
    }
}

} // namespace pengine
