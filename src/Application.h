#pragma once

#include <glm/glm.hpp>

#include "core/time.h"
#include "game/vehicle.h"
#include "physics/character_controller.h"
#include "physics/world_collision.h"
#include "platform/input.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/debug_draw.h"
#include "render/mesh.h"
#include "render/shader.h"
#include "render/texture.h"
#include "scene/scene.h"
#include "world/streamer.h"
#include "world/world_defs.h"

namespace pengine {

class SceneNode;

class Application {
public:
    enum class Mode { Fly, Walk, Drive };

    bool init();
    int  run();
    void shutdown();

private:
    void process_events();
    void update(double dt);
    void render(double alpha);

    void set_mode(Mode m);
    void update_chase_camera(float dt);
    void sync_vehicle_scene();

    Window         window_;
    Input          input_;
    FixedTimestep  clock_;

    Shader         lit_shader_;
    Mesh           cube_mesh_;
    Texture        checker_tex_;
    Camera         camera_;
    Scene          scene_;
    Streamer       streamer_;

    WorldCollision      world_collision_;
    CharacterController character_;
    DebugDraw           debug_draw_;

    Vehicle             vehicle_;
    SceneNode*          chassis_node_       = nullptr;
    SceneNode*          chassis_visual_node_= nullptr;
    SceneNode*          wheel_nodes_[4]     = {nullptr, nullptr, nullptr, nullptr};

    Mode  mode_           = Mode::Fly;
    bool  mouse_captured_ = false;
    bool  running_        = false;

    TimePoint stats_start_{};
    int       fps_frames_  = 0;
    double    max_frame_ms_ = 0.0;
    TimePoint last_frame_{};

    bool      last_ray_hit_  = false;
    float     last_ray_dist_ = 0.f;
};

} // namespace pengine
