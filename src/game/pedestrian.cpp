#include "game/pedestrian.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/log.h"
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

void PedestrianSystem::update(float dt, const glm::vec3& camera_pos) {
    if (!scene_ || !graph_ || models_.empty()) return;

    // Per-frame VFX/audio scratch. advance() appends step events; the
    // caller (Application) drains them after update().
    step_events_.clear();

    // Despawn pass: drop peds whose edge unloaded or who drifted past the
    // far ring. Forward iter is fine; destroy_at swaps with the back.
    for (std::size_t k = peds_.size(); k-- > 0;) {
        Pedestrian& p = *peds_[k];
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

    for (auto& p : peds_) advance(*p, dt);
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

        int model_id = static_cast<int>(rng_() %
                          static_cast<std::uint32_t>(models_.size()));
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

void PedestrianSystem::kill(std::size_t ped_idx) {
    destroy_at(ped_idx);
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
        const glm::vec3 wp = sidewalk_pose(p.edge, p.dist_along, p.side);
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

void PedestrianSystem::advance(Pedestrian& p, float dt) {
    const ModelAsset& m = *models_[static_cast<std::size_t>(p.model_id)];

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
    glm::vec3 wp = sidewalk_pose(p.edge, p.dist_along, p.side);

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
    if      (p.state == State::Idle      && m.idle_loaded)   anim = &m.idle;
    else if (p.state == State::Sprinting && m.sprint_loaded) anim = &m.sprint;
    else if (m.walk.duration() > 0.f)                        anim = &m.walk;
    if (!anim || anim->duration() <= 0.f) return;

    anim->sample(p.anim_phase, m.skeleton, p.local_poses);

    // Strip root motion (mirror Application.cpp:554-560). Game logic
    // drives world position; the anim's root translation would otherwise
    // drift the rig away from where we just placed it.
    int n = m.skeleton.bone_count();
    for (int b = 0; b < n; ++b) {
        if (m.skeleton.bone(b).parent < 0) {
            glm::vec3 bind_t{m.skeleton.bone(b).bind_local[3]};
            p.local_poses[static_cast<std::size_t>(b)][3] =
                glm::vec4{bind_t, 1.f};
        }
    }

    m.skeleton.compute_skin_matrices(p.local_poses, p.skin_matrices);
}

void PedestrianSystem::render(Shader& skinned_shader,
                              const glm::mat4& view_proj,
                              const glm::vec3& cam_pos,
                              const Texture& fallback_tex,
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
    }
}

} // namespace pengine
