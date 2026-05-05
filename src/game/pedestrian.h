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
class Mesh;
class RoadGraph;
class Scene;
class SceneNode;
class Shader;
class WorldCollision;

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
    // despawn radius (despawn_dist_). world_col gates police chase against
    // buildings (movement and line-of-sight); civilians ignore it.
    void update(float dt, const glm::vec3& camera_pos,
                const WorldCollision& world_col);

    // Police officers are spawned explicitly by Application from police cars.
    // They share the pedestrian renderer/animation path but are not part of
    // the random civilian sidewalk population.
    void set_police_context(int wanted_level, const glm::vec3& target_pos);
    bool has_police_for_car(const void* car_id) const;
    bool spawn_police_officer(const glm::vec3& exit_pos, float yaw_deg,
                              const void* car_id);

    // Out-of-band skinned draw — one call per ped after frustum reject.
    // Caller doesn't need to bind the shader; we do it. Police peds also
    // get a static gun rendered onto their right-hand bone, reusing the
    // player's mesh + tuned grip offset so the hold matches exactly.
    void render(Shader& skinned_shader, Shader& lit_shader,
                const glm::mat4& view_proj,
                const glm::vec3& cam_pos, const Texture& fallback_tex,
                const Mesh& gun_mesh, const glm::mat4& gun_grip_offset,
                const Frustum& frustum) const;

    int active() const;

    // Per-frame step events (one entry per half-stride crossing this
    // frame). Drained by the caller into the audio engine so peds and
    // footsteps stay decoupled. Cleared at the start of update().
    struct StepEvent { glm::vec3 pos; };
    const std::vector<StepEvent>& step_events() const { return step_events_; }

    struct PoliceShotEvent {
        glm::vec3 origin{0.f};
        glm::vec3 target{0.f};
    };
    const std::vector<PoliceShotEvent>& police_shots() const {
        return police_shots_;
    }

    struct PoliceVehicleEvent {
        const void* car_id = nullptr;
    };
    const std::vector<PoliceVehicleEvent>& police_vehicle_events() const {
        return police_vehicle_events_;
    }

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

    // Player bullet hit. Subtracts from the ped's HP; when HP reaches 0,
    // the ped transitions to State::Dying, plays the death animation,
    // and is destroyed once the corpse-display timer expires. Already-
    // dying or out-of-range targets are no-ops, so multiple bullets in
    // flight against the same ped don't double-fire the death.
    // shot_origin is used to choose which death anim plays: bullets that
    // entered the ped's front trigger the backward fall; from the back,
    // the forward fall.
    void apply_damage(std::size_t ped_idx, float amount,
                      const glm::vec3& shot_origin);

    // Top-down OBB the application fills in once per frame from each
    // vehicle's pose + chassis dimensions. Velocity is the world-space
    // m/s vector — process_vehicle_impacts compares its magnitude to a
    // minimum speed threshold so parked cars can't kill peds.
    struct CarHitbox {
        glm::vec3 center;
        glm::vec3 forward_xz;     // unit, world-space, Y=0
        glm::vec3 right_xz;       // unit, world-space, Y=0
        float     half_length;    // along forward
        float     half_width;     // along right
        float     half_height;    // along Y
        glm::vec3 velocity;       // m/s
    };
    // Test every living ped against every car in the list; meaningful
    // contact (car speed above CAR_HIT_MIN_SPEED_MPS) instantly kills
    // the ped and triggers the hit-by-car death anim. Safe to call
    // every frame.
    void process_vehicle_impacts(const std::vector<CarHitbox>& cars);

    // Civilian peds within hearing radius panic and sprint away from the
    // sound source: the ped flips edge direction if their current travel
    // would take them toward the gunshot, switches to Sprinting state
    // with a flee timer, and reverts to walking when the timer expires.
    // Police and dying peds ignore the event. Safe to call from outside
    // update() — the actual state changes are read on the next advance().
    void notify_gunshot(const glm::vec3& origin);

private:
    struct ModelAsset {
        SkinnedMesh           mesh;
        Skeleton              skeleton;
        Animation             walk;
        Animation             idle;            // optional; may be empty
        Animation             sprint;          // optional; shared FBX rebound per skel
        Animation             pistol_walk;     // police only
        Animation             pistol_run;      // police only
        Animation             pistol_idle;     // police only
        Animation             dying;            // backward fall (default)
        Animation             dying_forward;    // forward fall (shot in back)
        Animation             dying_hit_by_car; // body thrown by a car
        bool                  idle_loaded             = false;
        bool                  sprint_loaded           = false;
        bool                  pistol_walk_loaded      = false;
        bool                  pistol_run_loaded       = false;
        bool                  pistol_idle_loaded      = false;
        bool                  dying_loaded            = false;
        bool                  dying_forward_loaded    = false;
        bool                  dying_hit_by_car_loaded = false;
        bool                  police_model            = false;
        // Root-bone translation sampled from each dying anim at the trim
        // point. compute_pose anchors the corpse at bind translation and
        // adds (anim_t - dying_t0) so the body starts at standing height
        // (seamless transition from walk) and the anim's own delta
        // drives the fall.
        glm::vec3             dying_t0_root_trans            {0.f};
        glm::vec3             dying_forward_t0_root_trans    {0.f};
        glm::vec3             dying_hit_by_car_t0_root_trans {0.f};
        std::vector<Texture>  textures;        // variant palette
        float                 model_scale   = 1.f;
        glm::vec3             visual_offset {0.f};
        // Police rig: cached so we can recover the right-hand bone's world
        // matrix from the per-ped skin matrices each frame (same trick as
        // Application.cpp:158-165). bone_idx < 0 ⇒ no gun render.
        int                   right_hand_bone_idx  = -1;
        glm::mat4             right_hand_bind_world{1.f};
    };

    enum class Role : std::uint8_t { Civilian, Police };
    enum class State : std::uint8_t {
        Walking,
        Idle,
        Sprinting,
        PoliceExitVehicle,
        PoliceChase,
        PoliceShoot,
        PoliceEnterVehicle,
        Dying,            // HP hit 0; playing death anim, then despawn
    };

    struct Pedestrian {
        Role       role       = Role::Civilian;
        int        model_id   = 0;
        int        texture_id = 0;
        LaneId     edge{};                 // (i,j) + outgoing dir
        float      side       = +1.f;      // +1 right of travel, -1 left
        float      dist_along = 0.f;
        glm::vec3  world_pos  {0.f};       // police/freeform peds
        glm::vec3  vehicle_pos{0.f};       // police re-entry target
        const void* source_car_id = nullptr;
        float      yaw_deg    = 0.f;
        float      anim_phase = 0.f;       // seconds into current anim
        float      speed_mps  = 1.4f;
        State      state      = State::Walking;
        float      idle_remaining = 0.f;   // seconds left in idle pause
        float      state_timer = 0.f;
        float      shoot_cooldown = 0.f;
        float      health      = 100.f;
        float      death_timer = 0.f;     // seconds since entering Dying
        bool       dying_forward = false; // true ⇒ play forward-fall anim (shot from behind)
        bool       dying_hit_by_car = false; // true ⇒ play hit-by-car anim (overrides forward)
        // Ballistic body velocity in m/s. Driven by car-impact impulse,
        // integrated each frame in update()'s Dying branch with gravity
        // + ground collision + friction. Only meaningful while
        // dying_hit_by_car is true; otherwise unused.
        glm::vec3  velocity {0.f};
        float      flee_remaining = 0.f;  // gunshot panic; reverts to Walking at 0
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
    void advance_police(Pedestrian& p, float dt,
                        const WorldCollision& world_col);
    void compute_pose(Pedestrian& p);
    void sync_visual(Pedestrian& p);
    // Shared death-state transition. Caller sets the death-pose flags
    // (dying_forward / dying_hit_by_car) before invoking; this just
    // flips state to Dying and resets the timing fields.
    void enter_dying_state(Pedestrian& p);

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
    std::vector<PoliceShotEvent>              police_shots_;
    std::vector<PoliceVehicleEvent>           police_vehicle_events_;
    std::mt19937                              rng_;

    int       police_wanted_level_ = 0;
    glm::vec3 police_target_pos_{0.f};
};

} // namespace pengine
