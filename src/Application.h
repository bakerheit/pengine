#pragma once

#include <glm/glm.hpp>

#include "core/time.h"
#include "platform/input.h"
#include "platform/window.h"
#include "render/camera.h"
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

    Window   window_;
    Input    input_;
    FixedTimestep clock_;

    // Phase 4 world
    Shader   lit_shader_;
    Mesh     cube_mesh_;
    Texture  checker_tex_;
    Camera   camera_;
    Scene    scene_;
    Streamer streamer_;

    bool  mouse_captured_ = false;
    bool  running_        = false;

    // Per-second stats
    TimePoint stats_start_{};
    int       fps_frames_  = 0;
    double    max_frame_ms_ = 0.0; // worst frame time in the current second
    TimePoint last_frame_{};
};

} // namespace pengine
