#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>
#include <chrono>
#include <cstdio>

#include "core/log.h"
#include "render/mesh.h"
#include "scene/frustum.h"
#include "world/world_defs.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

bool Application::init() {
    WindowConfig cfg;
    cfg.title = "pengine [phase 4: world streaming]";
    if (!window_.init(cfg)) return false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.10f, 0.13f, 0.18f, 1.f);

    if (!lit_shader_.load(ASSETS_DIR "/shaders/lit.vert",
                          ASSETS_DIR "/shaders/lit.frag")) return false;

    {
        std::vector<Vertex>   verts;
        std::vector<uint32_t> idxs;
        make_cube(verts, idxs, 0.5f);
        cube_mesh_.upload(verts, idxs);
    }

    checker_tex_.load_checkerboard(128);

    // World: 16×16 cells @ 256 m = 4096 × 4096 m. Start near centre.
    WorldConfig world_cfg;
    float centre = world_cfg.world_cells_x * world_cfg.cell_size * 0.5f;
    camera_.position   = {centre, 30.f, centre};
    camera_.yaw        = -90.f;
    camera_.pitch      = -20.f;
    camera_.move_speed = 80.f;
    camera_.far_z      = 2000.f;

    streamer_.init(world_cfg, &scene_, &cube_mesh_);

    stats_start_ = Clock::now();
    last_frame_  = Clock::now();
    running_     = true;

    PE_INFO("Phase 4 ready. World %dx%d cells @ %.0f m (%.0f x %.0f km). "
            "Click window to capture mouse. ESC = release. Ctrl+Q = quit.",
            world_cfg.world_cells_x, world_cfg.world_cells_z,
            world_cfg.cell_size,
            world_cfg.world_cells_x * world_cfg.cell_size / 1000.f,
            world_cfg.world_cells_z * world_cfg.cell_size / 1000.f);
    return true;
}

int Application::run() {
    while (running_) {
        TimePoint frame_start = Clock::now();

        process_events();
        auto tick = clock_.advance();
        for (int i = 0; i < tick.updates; ++i)
            update(clock_.fixed_dt);
        render(tick.alpha);

        double frame_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - frame_start).count();
        if (frame_ms > max_frame_ms_) max_frame_ms_ = frame_ms;

        ++fps_frames_;
        if (seconds_since(stats_start_) >= 1.0) {
            auto st = streamer_.stats();
            char title[256];
            std::snprintf(title, sizeof(title),
                          "pengine  |  cells: %d  nodes: %d  fps: %d  worst: %.1f ms",
                          st.loaded_cells, st.total_nodes, fps_frames_, max_frame_ms_);
            SDL_SetWindowTitle(window_.sdl(), title);
            PE_INFO("cells: %d  nodes: %d  fps: %d  worst: %.1f ms",
                    st.loaded_cells, st.total_nodes, fps_frames_, max_frame_ms_);
            fps_frames_   = 0;
            max_frame_ms_ = 0.0;
            stats_start_  = Clock::now();
        }
    }
    return 0;
}

void Application::shutdown() {
    streamer_.shutdown();
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

    streamer_.pump(camera_.position);
    scene_.update();
}

void Application::render(double /*alpha*/) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = static_cast<float>(window_.width()) /
                   static_cast<float>(window_.height());
    glm::mat4 vp = camera_.view_proj(aspect);

    Frustum frustum = Frustum::from_view_proj(vp);
    Scene::CullResult cr = scene_.cull(frustum);

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
