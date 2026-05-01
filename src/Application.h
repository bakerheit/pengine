#pragma once

#include <glm/glm.hpp>

#include "core/time.h"
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

class Application {
public:
    bool init();
    int  run();
    void shutdown();

private:
    void process_events();
    void update(double dt);
    void render(double alpha);

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

    bool  walk_mode_      = false;
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
