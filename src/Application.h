#pragma once

#include <glm/glm.hpp>

#include "core/time.h"
#include "platform/input.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/mesh.h"
#include "render/shader.h"
#include "render/texture.h"

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

    Window  window_;
    Input   input_;
    FixedTimestep clock_;

    // Phase 1 scene
    Shader  lit_shader_;
    Mesh    cube_mesh_;
    Mesh    plane_mesh_;
    Texture checker_tex_;
    Camera  camera_;

    float   cube_angle_     = 0.f;  // radians
    bool    mouse_captured_ = false;
    bool    running_        = false;

    // 1 Hz frame counter
    TimePoint fps_start_{};
    int       fps_frames_ = 0;
};

} // namespace pengine
