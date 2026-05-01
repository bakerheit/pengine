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

    // Phase 3 scene
    Shader  lit_shader_;
    Mesh    cube_mesh_;   // shared geometry — all 1000 nodes point here
    Texture checker_tex_;
    Camera  camera_;
    Scene   scene_;

    bool    mouse_captured_ = false;
    bool    running_        = false;

    TimePoint fps_start_{};
    int       fps_frames_ = 0;
    int       last_total_ = 0;
    int       last_culled_ = 0;
};

} // namespace pengine
