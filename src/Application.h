#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "core/time.h"
#include "game/player_vehicle.h"
#include "game/traffic.h"
#include "physics/character_controller.h"
#include "physics/world_collision.h"
#include "platform/input.h"
#include "platform/window.h"
#include "render/animation.h"
#include "render/camera.h"
#include "render/debug_draw.h"
#include "render/mesh.h"
#include "render/shader.h"
#include "render/skeleton.h"
#include "render/skinned_mesh.h"
#include "render/spring_arm.h"
#include "render/texture.h"
#include "scene/scene.h"
#include "world/road_graph.h"
#include "world/streamer.h"
#include "world/world_defs.h"

namespace pengine {

class SceneNode;

class Application {
public:
    enum class Mode { OnFoot, InVehicle, DebugFly };

    bool init();
    int  run();
    void shutdown();

private:
    void process_events();
    void update(double dt);
    void render(double alpha);

    void enter_mode(Mode m);
    void try_toggle_vehicle();
    void update_on_foot(float dt, float mdx, float mdy);
    void update_in_vehicle(float dt, float mdx, float mdy);
    void sync_character_scene();
    void compute_procedural_walk_pose(float phase, bool moving);

    Window         window_;
    Input          input_;
    FixedTimestep  clock_;

    Shader         lit_shader_;
    Shader         skinned_shader_;
    Mesh           cube_mesh_;

    SkinnedMesh    character_skinned_mesh_;
    Skeleton       character_skeleton_;
    Animation      character_anim_;
    bool           character_skinned_ = false;
    Texture        checker_tex_;
    Texture        asphalt_tex_;
    Texture        grass_tex_;
    Texture        facade_tex_;
    Texture        sidewalk_tex_;
    Texture        character_tex_;
    Camera         camera_;
    Scene          scene_;
    Streamer       streamer_;

    WorldCollision      world_collision_;
    CharacterController character_;
    DebugDraw           debug_draw_;
    SpringArm           spring_;
    RoadGraph           road_graph_;
    TrafficSystem       traffic_;

    PlayerVehicle       player_vehicle_;

    SceneNode*          character_node_      = nullptr;  // pose root: feet pos + facing
    SceneNode*          character_visual_node_ = nullptr; // child: model offset + scale
    float               character_facing_yaw_deg_ = -90.f;
    glm::vec3           character_model_offset_{0.f, 0.f, 0.f};
    float               character_model_scale_ = 1.f;
    double              walk_phase_ = 0.0;  // seconds of walk-anim time accumulator
    std::vector<glm::mat4> char_local_poses_;
    std::vector<glm::mat4> char_skin_matrices_;

    // Cached limb-bone indices, resolved after skeleton load. Used by the
    // procedural walk-cycle pose generator (the asset's baked animation is a
    // static pose, so we drive these directly).
    struct WalkBones {
        int left_upleg = -1,  right_upleg = -1;
        int left_leg   = -1,  right_leg   = -1;
        int left_arm   = -1,  right_arm   = -1;
        int spine      = -1;
    } walk_bones_;

    Mode  mode_           = Mode::OnFoot;
    Mode  saved_mode_     = Mode::OnFoot; // last non-debug mode
    bool  mouse_captured_ = false;
    bool  running_        = false;
    bool  can_enter_car_  = false;        // updated each frame, used by HUD

    static constexpr float ENTRY_RADIUS = 4.0f; // metres for F-to-enter

    TimePoint stats_start_{};
    int       fps_frames_  = 0;
    double    max_frame_ms_ = 0.0;
    TimePoint last_frame_{};

    bool      last_ray_hit_  = false;
    float     last_ray_dist_ = 0.f;

    double    world_time_    = 0.0;  // accumulates fixed-timestep seconds
};

} // namespace pengine
