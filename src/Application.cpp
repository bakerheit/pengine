#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "render/mesh.h"
#include "scene/frustum.h"
#include "scene/scene_node.h"
#include "world/heightmap.h"
#include "world/world_defs.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

static const char* mode_name(Application::Mode m) {
    switch (m) {
        case Application::Mode::Fly:   return "fly";
        case Application::Mode::Walk:  return "walk";
        case Application::Mode::Drive: return "drive";
    }
    return "?";
}

bool Application::init() {
    WindowConfig cfg;
    cfg.title = "pengine [phase 6: vehicle]";
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

    glm::vec3 spawn{centre, 0.f, centre};
    spawn.y = Heightmap::sample(spawn.x, spawn.z);
    character_.teleport(spawn);

    // Vehicle spawn — slightly down the road from the player, on terrain.
    glm::vec3 car_spawn = spawn + glm::vec3{8.f, 0.f, 0.f};
    car_spawn.y = Heightmap::sample(car_spawn.x, car_spawn.z) + 1.5f;
    vehicle_.spawn(car_spawn, /*yaw_deg=*/ -90.f);

    // Scene nodes for the vehicle. chassis_node_ holds the unscaled pose;
    // chassis_visual_node_ is a child that applies the box scale; wheels are
    // children of chassis_node_ so they inherit pose but not box scale.
    AABB cube_aabb;
    cube_aabb.min = cube_mesh_.bounds_min();
    cube_aabb.max = cube_mesh_.bounds_max();

    chassis_node_         = scene_.create_node();
    chassis_visual_node_  = scene_.create_node(chassis_node_);
    chassis_visual_node_->renderable = Renderable{&cube_mesh_, cube_aabb};
    for (int i = 0; i < 4; ++i) {
        wheel_nodes_[i] = scene_.create_node(chassis_node_);
        wheel_nodes_[i]->renderable = Renderable{&cube_mesh_, cube_aabb};
    }

    sync_vehicle_scene();

    stats_start_ = Clock::now();
    last_frame_  = Clock::now();
    running_     = true;

    PE_INFO("Phase 6 ready. F=fly  G=walk  V=drive.  In drive: W throttle, S brake, A/D steer, Space handbrake.");
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
            char title[400];
            std::snprintf(title, sizeof(title),
                          "pengine  |  %s  |  cells:%d  bld:%d  fps:%d  worst:%.1fms"
                          "  |  car:%.0fkm/h %s  |  ray:%s %.1fm",
                          mode_name(mode_),
                          st.loaded_cells, world_collision_.building_count(),
                          fps_frames_, max_frame_ms_,
                          vehicle_.speed_kmh(), vehicle_.airborne() ? "AIR" : "ground",
                          last_ray_hit_ ? "HIT" : "miss", last_ray_dist_);
            SDL_SetWindowTitle(window_.sdl(), title);
            PE_INFO("%s  cells=%d bld=%d fps=%d worst=%.1fms  car=%.0fkm/h %s  ray=%s %.1fm",
                    mode_name(mode_),
                    st.loaded_cells, world_collision_.building_count(),
                    fps_frames_, max_frame_ms_,
                    vehicle_.speed_kmh(), vehicle_.airborne() ? "AIR" : "GND",
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

void Application::set_mode(Mode m) {
    if (mode_ == m) return;
    if (m == Mode::Walk) {
        glm::vec3 spawn = camera_.position;
        spawn.y = Heightmap::sample(spawn.x, spawn.z);
        character_.teleport(spawn);
    }
    mode_ = m;
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

    if (input_.pressed(SDL_SCANCODE_F)) set_mode(Mode::Fly);
    if (input_.pressed(SDL_SCANCODE_G)) set_mode(Mode::Walk);
    if (input_.pressed(SDL_SCANCODE_V)) set_mode(Mode::Drive);
}

void Application::update_chase_camera(float dt) {
    glm::vec3 chassis_pos = vehicle_.position();
    glm::vec3 fwd_h = vehicle_.forward();
    fwd_h.y = 0.f;
    if (glm::length(fwd_h) < 1e-3f) fwd_h = glm::vec3{1.f, 0.f, 0.f};
    fwd_h = glm::normalize(fwd_h);

    glm::vec3 desired_eye = chassis_pos - fwd_h * 8.f + glm::vec3{0.f, 3.5f, 0.f};
    float blend = 1.f - std::exp(-8.f * dt);
    camera_.position += (desired_eye - camera_.position) * blend;

    glm::vec3 look_at = chassis_pos + glm::vec3{0.f, 0.8f, 0.f};
    glm::vec3 dir = glm::normalize(look_at - camera_.position);
    camera_.yaw   = glm::degrees(std::atan2(dir.z, dir.x));
    camera_.pitch = glm::degrees(std::asin(dir.y));
}

void Application::sync_vehicle_scene() {
    if (!chassis_node_) return;
    chassis_node_->transform.position = vehicle_.position();
    chassis_node_->transform.rotation = vehicle_.orientation();
    chassis_node_->transform.scale    = {1.f, 1.f, 1.f};
    chassis_node_->mark_dirty();

    chassis_visual_node_->transform.position = {0.f, 0.f, 0.f};
    chassis_visual_node_->transform.rotation = glm::quat{1.f, 0.f, 0.f, 0.f};
    chassis_visual_node_->transform.scale    = vehicle_.chassis_full_extents;
    chassis_visual_node_->mark_dirty();

    const auto& wheels = vehicle_.wheels();
    for (std::size_t i = 0; i < 4; ++i) {
        glm::vec3 pos = wheels[i].mount_local;
        pos.y -= wheels[i].visual_drop; // drop along chassis -Y
        wheel_nodes_[i]->transform.position = pos;
        glm::quat steer{1.f, 0.f, 0.f, 0.f};
        if (wheels[i].is_steering)
            steer = glm::angleAxis(-vehicle_.steer_rad(), glm::vec3{0.f, 1.f, 0.f});
        wheel_nodes_[i]->transform.rotation = steer;
        wheel_nodes_[i]->transform.scale    = {0.32f, 2.f * vehicle_.wheel_radius,
                                                2.f * vehicle_.wheel_radius};
        wheel_nodes_[i]->mark_dirty();
    }
}

void Application::update(double dt) {
    float fdt = static_cast<float>(dt);
    lit_shader_.hot_reload();

    float mdx = mouse_captured_ ? input_.mouse_dx() : 0.f;
    float mdy = mouse_captured_ ? input_.mouse_dy() : 0.f;

    if (mode_ == Mode::Drive) {
        float thr  = input_.down(SDL_SCANCODE_W) ? 1.f : 0.f;
        float brk  = input_.down(SDL_SCANCODE_S) ? 1.f : 0.f;
        float steer = (input_.down(SDL_SCANCODE_D) ? 1.f : 0.f)
                    - (input_.down(SDL_SCANCODE_A) ? 1.f : 0.f);
        bool  hb   = input_.down(SDL_SCANCODE_SPACE);
        vehicle_.set_inputs(thr, brk, steer, hb);
    } else {
        vehicle_.set_inputs(0.f, 0.f, 0.f, false);
    }
    // Vehicle physics runs every frame at 120 Hz (2 substeps), regardless of
    // mode — the car is a persistent physics object.
    vehicle_.substep(fdt * 0.5f, world_collision_);
    vehicle_.substep(fdt * 0.5f, world_collision_);

    if (mode_ == Mode::Fly) {
        camera_.update(fdt, input_, mdx, mdy);
    } else if (mode_ == Mode::Walk) {
        Input dummy{};
        camera_.update(fdt, dummy, mdx, mdy);
        character_.update(fdt, input_, camera_.yaw, world_collision_);
        camera_.position = character_.eye_position();
    } else { // Drive
        update_chase_camera(fdt);
    }

    sync_vehicle_scene();
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

    if (mode_ == Mode::Walk) {
        debug_draw_.cylinder_xz(character_.feet_position(),
                                CharacterController::RADIUS,
                                CharacterController::HEIGHT);
    }

    // Vehicle: show wheel contact markers when grounded.
    for (const Wheel& w : vehicle_.wheels()) {
        if (w.grounded) debug_draw_.cross(w.contact_world, 0.2f);
    }

    debug_draw_.flush(vp, glm::vec3{1.f, 0.3f, 0.2f});

    window_.swap();
}

} // namespace pengine
