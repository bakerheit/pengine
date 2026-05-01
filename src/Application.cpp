#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>
#include <chrono>
#include <cstdio>

#include "core/log.h"
#include "render/mesh.h"
#include "scene/frustum.h"
#include "world/heightmap.h"
#include "world/world_defs.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

bool Application::init() {
    WindowConfig cfg;
    cfg.title = "pengine [phase 5: terrain & collision]";
    if (!window_.init(cfg)) return false;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glClearColor(0.45f, 0.65f, 0.85f, 1.f);

    if (!lit_shader_.load(ASSETS_DIR "/shaders/lit.vert",
                          ASSETS_DIR "/shaders/lit.frag")) return false;
    if (!debug_draw_.init(ASSETS_DIR)) return false;

    {
        std::vector<Vertex>   verts;
        std::vector<uint32_t> idxs;
        make_cube(verts, idxs, 0.5f);
        cube_mesh_.upload(verts, idxs);
    }

    checker_tex_.load_checkerboard(128);

    Heightmap::set_seed(1337u);

    WorldConfig world_cfg;
    float centre = world_cfg.world_cells_x * world_cfg.cell_size * 0.5f;

    camera_.position   = {centre, 30.f, centre};
    camera_.yaw        = -90.f;
    camera_.pitch      = -15.f;
    camera_.move_speed = 80.f;
    camera_.far_z      = 2000.f;

    streamer_.init(world_cfg, &scene_, &cube_mesh_, &world_collision_);

    // Spawn the character on the terrain near the centre.
    glm::vec3 spawn{centre, 0.f, centre};
    spawn.y = Heightmap::sample(spawn.x, spawn.z);
    character_.teleport(spawn);

    stats_start_ = Clock::now();
    last_frame_  = Clock::now();
    running_     = true;

    PE_INFO("Phase 5 ready. F = toggle walk/fly. WASD/Space, mouse look. ESC release. Ctrl+Q quit.");
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
            char title[320];
            std::snprintf(title, sizeof(title),
                          "pengine  |  %s  |  cells: %d  buildings: %d  fps: %d  worst: %.1f ms  |  ray: %s %.1fm",
                          walk_mode_ ? "walk" : "fly",
                          st.loaded_cells, world_collision_.building_count(),
                          fps_frames_, max_frame_ms_,
                          last_ray_hit_ ? "HIT" : "miss",
                          last_ray_dist_);
            SDL_SetWindowTitle(window_.sdl(), title);
            PE_INFO("%s  cells=%d buildings=%d nodes=%d fps=%d worst=%.1fms ray=%s %.1fm",
                    walk_mode_ ? "walk" : "fly",
                    st.loaded_cells, world_collision_.building_count(),
                    st.total_nodes, fps_frames_, max_frame_ms_,
                    last_ray_hit_ ? "HIT" : "miss", last_ray_dist_);
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
    debug_draw_.shutdown();
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

    if (input_.pressed(SDL_SCANCODE_F)) {
        walk_mode_ = !walk_mode_;
        if (walk_mode_) {
            // Drop the character beneath the camera and snap to ground.
            glm::vec3 spawn = camera_.position;
            spawn.y = Heightmap::sample(spawn.x, spawn.z);
            character_.teleport(spawn);
        }
    }
}

void Application::update(double dt) {
    float fdt = static_cast<float>(dt);
    lit_shader_.hot_reload();

    float mdx = mouse_captured_ ? input_.mouse_dx() : 0.f;
    float mdy = mouse_captured_ ? input_.mouse_dy() : 0.f;

    if (walk_mode_) {
        // Mouse-look only (no WASD on the camera; the character handles it).
        Input dummy{}; // empty inputs => no fly translation
        camera_.update(fdt, dummy, mdx, mdy);
        character_.update(fdt, input_, camera_.yaw, world_collision_);
        camera_.position = character_.eye_position();
    } else {
        camera_.update(fdt, input_, mdx, mdy);
    }

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
    lit_shader_.set("u_ambient",     glm::vec3{0.18f, 0.22f, 0.28f});
    lit_shader_.set("u_diffuse",     0);

    checker_tex_.bind(0);
    scene_.draw(cr, lit_shader_);

    // ---- Debug overlay -----------------------------------------------------
    debug_draw_.clear();

    glm::vec3 ray_origin = camera_.position;
    glm::vec3 ray_dir    = camera_.forward();
    RayHit    hit        = world_collision_.raycast(ray_origin, ray_dir, 200.f);
    last_ray_hit_  = hit.hit;
    last_ray_dist_ = hit.t;

    if (hit.hit) {
        debug_draw_.line(ray_origin, hit.position);
        debug_draw_.cross(hit.position, 0.4f);
    } else {
        debug_draw_.line(ray_origin, ray_origin + ray_dir * 200.f);
    }

    if (walk_mode_) {
        debug_draw_.cylinder_xz(character_.feet_position(),
                                CharacterController::RADIUS,
                                CharacterController::HEIGHT);
    }

    debug_draw_.flush(vp, glm::vec3{1.f, 0.3f, 0.2f});

    window_.swap();
}

} // namespace pengine
