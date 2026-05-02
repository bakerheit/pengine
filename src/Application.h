#pragma once

#include <glm/glm.hpp>

#include "core/time.h"
#include "game/traffic.h"
#include "game/vehicle.h"
#include "physics/character_controller.h"
#include "physics/world_collision.h"
#include "platform/input.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/debug_draw.h"
#include "render/mesh.h"
#include "render/shader.h"
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
    void sync_vehicle_scene();
    void sync_character_scene();

    Window         window_;
    Input          input_;
    FixedTimestep  clock_;

    Shader         lit_shader_;
    Mesh           cube_mesh_;
    Texture        checker_tex_;
    Texture        asphalt_tex_;
    Texture        grass_tex_;
    Texture        facade_tex_;
    Camera         camera_;
    Scene          scene_;
    Streamer       streamer_;

    WorldCollision      world_collision_;
    CharacterController character_;
    DebugDraw           debug_draw_;
    SpringArm           spring_;
    RoadGraph           road_graph_;
    TrafficSystem       traffic_;

    Vehicle             vehicle_;
    SceneNode*          chassis_node_        = nullptr;
    SceneNode*          chassis_visual_node_ = nullptr;
    SceneNode*          wheel_nodes_[4]      = {nullptr, nullptr, nullptr, nullptr};

    SceneNode*          character_node_      = nullptr;
    float               character_facing_yaw_deg_ = -90.f;

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
};

} // namespace pengine
