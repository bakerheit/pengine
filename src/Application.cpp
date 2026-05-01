#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cmath>

#include "core/log.h"
#include "scene/frustum.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

static constexpr int   GRID_W    = 32;
static constexpr int   GRID_H    = 32;
static constexpr float GRID_STEP = 3.f;

bool Application::init() {
    WindowConfig cfg;
    cfg.title = "pengine [phase 3: scene graph]";
    if (!window_.init(cfg)) return false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.10f, 0.13f, 0.18f, 1.f);

    if (!lit_shader_.load(ASSETS_DIR "/shaders/lit.vert",
                          ASSETS_DIR "/shaders/lit.frag")) return false;

    // Build the shared cube mesh and record its local AABB.
    {
        std::vector<Vertex>   verts;
        std::vector<uint32_t> idxs;
        make_cube(verts, idxs, 0.5f);
        cube_mesh_.upload(verts, idxs);
    }
    AABB cube_aabb;
    cube_aabb.min = cube_mesh_.bounds_min();
    cube_aabb.max = cube_mesh_.bounds_max();

    checker_tex_.load_checkerboard(128);

    // Populate the scene: GRID_W × GRID_H cubes on a flat plane.
    float offset_x = -GRID_W * GRID_STEP * 0.5f;
    float offset_z = -GRID_H * GRID_STEP * 0.5f;

    for (int row = 0; row < GRID_H; ++row) {
        for (int col = 0; col < GRID_W; ++col) {
            SceneNode* n = scene_.create_node();
            n->transform.position = {
                offset_x + static_cast<float>(col) * GRID_STEP,
                0.5f,
                offset_z + static_cast<float>(row) * GRID_STEP
            };
            // Slight Y scale variation so it doesn't look too uniform.
            float s = 0.8f + 0.4f * std::sin(static_cast<float>(col + row) * 0.9f);
            n->transform.scale = {1.f, s, 1.f};
            n->renderable = Renderable{&cube_mesh_, cube_aabb};
        }
    }

    scene_.update();

    camera_.position  = {0.f, 4.f, 20.f};
    camera_.pitch     = -10.f;
    camera_.move_speed = 15.f;

    PE_INFO("Phase 3: %d cubes. Click to capture mouse. ESC = release. Ctrl+Q = quit.",
            GRID_W * GRID_H);

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
            char title[128];
            std::snprintf(title, sizeof(title),
                          "pengine  |  total: %d  culled: %d  drawn: %d  |  fps: %d",
                          last_total_, last_culled_,
                          last_total_ - last_culled_,
                          fps_frames_);
            SDL_SetWindowTitle(window_.sdl(), title);
            PE_INFO("total: %d  culled: %d  drawn: %d  fps: %d",
                    last_total_, last_culled_,
                    last_total_ - last_culled_, fps_frames_);
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
    if (input_.down(SDL_SCANCODE_LCTRL) && input_.pressed(SDL_SCANCODE_Q))
        running_ = false;
}

void Application::update(double dt) {
    float fdt = static_cast<float>(dt);
    lit_shader_.hot_reload();
    float mdx = mouse_captured_ ? input_.mouse_dx() : 0.f;
    float mdy = mouse_captured_ ? input_.mouse_dy() : 0.f;
    camera_.update(fdt, input_, mdx, mdy);
}

void Application::render(double /*alpha*/) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = static_cast<float>(window_.width()) /
                   static_cast<float>(window_.height());
    glm::mat4 vp = camera_.view_proj(aspect);

    Frustum frustum = Frustum::from_view_proj(vp);
    Scene::CullResult cr = scene_.cull(frustum);
    last_total_  = cr.total;
    last_culled_ = cr.culled;

    lit_shader_.use();
    lit_shader_.set("u_view_proj",   vp);
    lit_shader_.set("u_cam_pos",     camera_.position);
    lit_shader_.set("u_light_dir",   glm::normalize(glm::vec3{0.6f, 1.f, 0.4f}));
    lit_shader_.set("u_light_color", glm::vec3{1.f, 0.95f, 0.85f});
    lit_shader_.set("u_ambient",     glm::vec3{0.08f, 0.10f, 0.14f});
    lit_shader_.set("u_diffuse",     0);

    checker_tex_.bind(0);

    scene_.draw(cr, lit_shader_);

    window_.swap();
}

} // namespace pengine
