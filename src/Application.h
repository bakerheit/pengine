#pragma once

#include <glm/glm.hpp>

#include "core/time.h"
#include "platform/input.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/material.h"
#include "render/model.h"
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

    // Phase 2 scene
    Shader  lit_shader_;
    Model   scene_model_;
    Texture checker_tex_;   // fallback diffuse for submeshes without a texture
    Camera  camera_;

    bool    mouse_captured_ = false;
    bool    running_        = false;

    TimePoint fps_start_{};
    int       fps_frames_ = 0;

    bool model_loaded_ = false;
};

} // namespace pengine
