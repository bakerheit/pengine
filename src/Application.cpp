#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include "core/log.h"

// ASSETS_DIR is defined by CMake to the absolute path of the assets/ folder.
#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

bool Application::init() {
    WindowConfig cfg;
    cfg.title = "pengine [phase 1: renderer]";
    if (!window_.init(cfg)) return false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.10f, 0.13f, 0.18f, 1.f);

    if (!lit_shader_.load(ASSETS_DIR "/shaders/lit.vert",
                          ASSETS_DIR "/shaders/lit.frag")) return false;

    std::vector<Vertex> verts;
    std::vector<uint32_t> idxs;

    make_cube(verts, idxs);
    cube_mesh_.upload(verts, idxs);

    make_plane(verts, idxs, 20.f);
    plane_mesh_.upload(verts, idxs);

    checker_tex_.load_checkerboard(128);

    PE_INFO("Click the window to capture mouse. ESC to release. Ctrl+Q to quit.");
    PE_INFO("WASD to fly, Q/E for vertical. Mouse to look.");

    fps_start_ = Clock::now();
    running_   = true;
    return true;
}

int Application::run() {
    while (running_) {
        process_events();
        auto tick = clock_.advance();
        for (int i = 0; i < tick.updates; ++i)
            update(clock_.fixed_dt);
        render(tick.alpha);

        ++fps_frames_;
        if (seconds_since(fps_start_) >= 1.0) {
            PE_DEBUG("fps ~%d", fps_frames_);
            fps_frames_ = 0;
            fps_start_  = Clock::now();
        }
    }
    return 0;
}

void Application::shutdown() {
    SDL_SetRelativeMouseMode(SDL_FALSE);
    lit_shader_.destroy();
    cube_mesh_.destroy();
    plane_mesh_.destroy();
    checker_tex_.destroy();
    window_.shutdown();
}

void Application::process_events() {
    input_.begin_frame();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                running_ = false;
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    e.window.event == SDL_WINDOWEVENT_RESIZED) {
                    int w = 0, h = 0;
                    SDL_GL_GetDrawableSize(window_.sdl(), &w, &h);
                    window_.on_resize(w, h);
                    glViewport(0, 0, w, h);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (!mouse_captured_) {
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                    mouse_captured_ = true;
                }
                break;
            default:
                input_.handle_event(e);
                break;
        }
    }

    if (input_.pressed(SDL_SCANCODE_ESCAPE)) {
        if (mouse_captured_) {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            mouse_captured_ = false;
        }
    }

    // Ctrl+Q to quit
    if (input_.down(SDL_SCANCODE_LCTRL) && input_.pressed(SDL_SCANCODE_Q))
        running_ = false;
}

void Application::update(double dt) {
    float fdt = static_cast<float>(dt);

    lit_shader_.hot_reload();

    float mdx = mouse_captured_ ? input_.mouse_dx() : 0.f;
    float mdy = mouse_captured_ ? input_.mouse_dy() : 0.f;
    camera_.update(fdt, input_, mdx, mdy);

    cube_angle_ += fdt * 0.785398f; // 45°/s
}

void Application::render(double /*alpha*/) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = static_cast<float>(window_.width()) /
                   static_cast<float>(window_.height());
    glm::mat4 vp = camera_.view_proj(aspect);

    lit_shader_.use();
    lit_shader_.set("u_view_proj",   vp);
    lit_shader_.set("u_cam_pos",     camera_.position);
    lit_shader_.set("u_light_dir",   glm::normalize(glm::vec3{0.6f, 1.f, 0.4f}));
    lit_shader_.set("u_light_color", glm::vec3{1.f, 0.95f, 0.85f});
    lit_shader_.set("u_ambient",     glm::vec3{0.08f, 0.10f, 0.14f});
    lit_shader_.set("u_diffuse",     0);

    checker_tex_.bind(0);

    // Spinning cube
    glm::mat4 cube_model = glm::rotate(glm::mat4{1.f}, cube_angle_, {0.f, 1.f, 0.f});
    cube_model = glm::translate(cube_model, {0.f, 0.6f, 0.f});
    glm::mat3 cube_nm = glm::mat3(glm::transpose(glm::inverse(cube_model)));
    lit_shader_.set("u_model",      cube_model);
    lit_shader_.set("u_normal_mat", cube_nm);
    cube_mesh_.draw();

    // Ground plane
    glm::mat4 plane_model{1.f};
    glm::mat3 plane_nm = glm::mat3(1.f);
    lit_shader_.set("u_model",      plane_model);
    lit_shader_.set("u_normal_mat", plane_nm);
    plane_mesh_.draw();

    window_.swap();
}

} // namespace pengine
