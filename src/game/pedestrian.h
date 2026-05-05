#pragma once

#include <cstdint>
#include <memory>
#include <random>
#include <vector>

#include <glm/glm.hpp>

#include "game/pedestrian_ai.h"
#include "render/animation.h"
#include "render/skeleton.h"
#include "render/skinned_mesh.h"
#include "render/texture.h"

namespace pengine {

struct Frustum;
class RoadGraph;
class Scene;
class SceneNode;
class Shader;

// Kinematic crowd of AI pedestrians wandering loaded sidewalks. Mirrors
// TrafficSystem's spawn-near-camera / kinematic-update-per-frame /
// despawn-at-distance pattern, but parameterised on the road grid at a
// sidewalk lateral offset (PED_SIDEWALK_OFFSET) instead of a lane offset.
//
// One scene-graph subtree per ped (root = feet+facing, child = mesh
// offset+scale). Skinned-mesh draws are issued out-of-band via render()
// because Scene::draw can't skin.
class PedestrianSystem {
public:
    PedestrianSystem();
    ~PedestrianSystem();
    PedestrianSystem(const PedestrianSystem&)            = delete;
    PedestrianSystem& operator=(const PedestrianSystem&) = delete;

    bool init(Scene& scene, RoadGraph& graph, int target_count);
    void shutdown();

    // Per-frame: spawn budget + advance + despawn. Listener / camera_pos is
    // also the basis for the spawn ring (spawn_min_..spawn_max_) and the
    // despawn radius (despawn_dist_).
    void update(float dt, const glm::vec3& camera_pos);

    // Out-of-band skinned draw — one call per ped after frustum reject.
    // Caller doesn't need to bind the shader; we do it.
    void render(Shader& skinned_shader, const glm::mat4& view_proj,
                const glm::vec3& cam_pos, const Texture& fallback_tex,
                const Frustum& frustum) const;

    int active() const;

    // Per-frame step events (one entry per half-stride crossing this
    // frame). Drained by the caller into the audio engine so peds and
    // footsteps stay decoupled. Cleared at the start of update().
    struct StepEvent { glm::vec3 pos; };
    const std::vector<StepEvent>& step_events() const { return step_events_; }

    // Hitscan against living peds. Returns the nearest ped whose body box
    // is intersected within `max_dist`. ped_idx is valid only until the
    // next update() / kill() — the underlying vector is mutated by both.
    struct RayHit {
        bool        hit      = false;
        float       t        = 0.f;
        glm::vec3   position {0.f};
        std::size_t ped_idx  = 0;
    };
    RayHit raycast(const glm::vec3& origin, const glm::vec3& dir,
                   float max_dist) const;

    // Immediate despawn (player shot the ped). Same teardown as natural
    // despawn — scene nodes freed, ped removed from peds_ via swap-erase
    // semantics. Out-of-range idx is a no-op.
    void kill(std::size_t ped_idx);

private:
    struct ModelAsset {
        SkinnedMesh           mesh;
        Skeleton              skeleton;
        Animation             walk;
        Animation             idle;            // optional; may be empty
        Animation             sprint;          // optional; shared FBX rebound per skel
        bool                  idle_loaded   = false;
        bool                  sprint_loaded = false;
        std::vector<Texture>  textures;        // variant palette
        float                 model_scale   = 1.f;
        glm::vec3             visual_offset {0.f};
    };

    enum class State : std::uint8_t { Walking, Idle, Sprinting };

    struct Pedestrian {
        int        model_id   = 0;
        int        texture_id = 0;
        LaneId     edge{};                 // (i,j) + outgoing dir
        float      side       = +1.f;      // +1 right of travel, -1 left
        float      dist_along = 0.f;
        float      yaw_deg    = 0.f;
        float      anim_phase = 0.f;       // seconds into current anim
        float      speed_mps  = 1.4f;
        State      state      = State::Walking;
        float      idle_remaining = 0.f;   // seconds left in idle pause
        // Tracks the last half-stride bucket we emitted a footstep for.
        // When floor(anim_phase / step_period) advances past this, fire
        // a new step. -1 = never fired (suppress the spawn-frame step
        // so a fresh ped doesn't pop a footstep before its first move).
        int        last_step_bucket = -1;
        SceneNode* root_node   = nullptr;
        SceneNode* visual_node = nullptr;
        std::vector<glm::mat4> local_poses;
        std::vector<glm::mat4> skin_matrices;
    };

    bool try_spawn(const glm::vec3& camera_pos);
    void destroy_at(std::size_t idx);
    bool edge_loaded(const LaneId& edge) const;
    void advance(Pedestrian& p, float dt);
    void compute_pose(Pedestrian& p);
    void sync_visual(Pedestrian& p);

    Scene*     scene_ = nullptr;
    RoadGraph* graph_ = nullptr;

    int        target_count_ = 30;
    float      spawn_min_    = 18.f;   // m — keep peds out of the player's lap
    float      spawn_max_    = 90.f;   // m — far edge of spawn ring
    float      despawn_dist_ = 130.f;  // m — past this we evict

    // Asset loads can fail (mesh-format mismatch, missing texture); we
    // hold them by unique_ptr so the surviving subset still works and
    // SkinnedMesh's non-movable GL handles never need to migrate slots.
    std::vector<std::unique_ptr<ModelAsset>> models_;
    std::vector<std::unique_ptr<Pedestrian>> peds_;
    std::vector<StepEvent>                    step_events_;
    std::mt19937                              rng_;
};

} // namespace pengine
