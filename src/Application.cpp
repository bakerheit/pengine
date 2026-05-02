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
        case Application::Mode::OnFoot:    return "on-foot";
        case Application::Mode::InVehicle: return "drive";
        case Application::Mode::DebugFly:  return "fly";
    }
    return "?";
}

bool Application::init() {
    WindowConfig cfg;
    cfg.title = "pengine [phase 7: player + chase cam + enter/exit]";
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
    asphalt_tex_.load_asphalt();
    grass_tex_.load_grass();
    facade_tex_.load_facade();

    // Player character mesh + texture.
    if (!character_model_.load(ASSETS_DIR "/models/characters/character_01.emesh")) {
        PE_WARN("Falling back to cube character (model load failed)");
    }
    if (!character_tex_.load_file(ASSETS_DIR "/Characters_psx/Textures/Character_01.png")) {
        PE_WARN("Falling back to checker for character (texture load failed)");
    }

    Heightmap::set_seed(1337u);

    WorldConfig world_cfg;
    // Spawn at the central intersection of cell (8,8). The cell layout puts
    // a 4-way intersection at (cell_origin + half), well clear of any
    // building plot or the cell-edge ramp zone.
    float cell = world_cfg.cell_size;
    int   ci   = world_cfg.world_cells_x / 2;
    int   cj   = world_cfg.world_cells_z / 2;
    glm::vec3 intersection{(static_cast<float>(ci) + 0.5f) * cell, 0.f,
                           (static_cast<float>(cj) + 0.5f) * cell};

    camera_.move_speed = 80.f;
    camera_.far_z      = 2000.f;

    WorldTextures world_tex{&grass_tex_, &asphalt_tex_, &facade_tex_};
    streamer_.init(world_cfg, &scene_, &cube_mesh_, &world_collision_,
                    world_tex, &road_graph_);
    traffic_.init(&scene_, &cube_mesh_, &checker_tex_, &road_graph_,
                  /*target_count=*/ 30);

    glm::vec3 spawn = intersection;
    spawn.y = Heightmap::sample(spawn.x, spawn.z);
    character_.teleport(spawn);

    // Car at the next intersection one block east — 62 m along the central
    // E-W road. Lands cleanly on a road slab.
    glm::vec3 car_spawn = intersection + glm::vec3{62.f, 0.f, 0.f};
    car_spawn.y = Heightmap::sample(car_spawn.x, car_spawn.z) + 1.5f;
    vehicle_.spawn(car_spawn, /*yaw_deg=*/ -90.f);

    AABB cube_aabb;
    cube_aabb.min = cube_mesh_.bounds_min();
    cube_aabb.max = cube_mesh_.bounds_max();

    // Build renderables with an explicit texture so they don't accidentally
    // inherit whatever the previous draw bound.
    auto make_renderable = [&](const glm::vec3& tint) {
        return Renderable{&cube_mesh_, cube_aabb, tint,
                          glm::vec2{1.f, 1.f}, &checker_tex_};
    };

    // Vehicle scene nodes.
    chassis_node_         = scene_.create_node();
    chassis_visual_node_  = scene_.create_node(chassis_node_);
    chassis_visual_node_->renderable = make_renderable({0.85f, 0.20f, 0.18f}); // red car
    for (int i = 0; i < 4; ++i) {
        wheel_nodes_[i] = scene_.create_node(chassis_node_);
        wheel_nodes_[i]->renderable = make_renderable({0.10f, 0.10f, 0.10f}); // dark tyres
    }

    // Character: pose root (feet pos + facing yaw) with a visual child that
    // applies model-specific offset/scale. Falls back to a tan cube if the
    // model didn't load.
    character_node_ = scene_.create_node();
    character_visual_node_ = scene_.create_node(character_node_);
    if (!character_model_.submeshes().empty()) {
        const Mesh& m = character_model_.submeshes()[0].mesh;
        glm::vec3 mn = m.bounds_min();
        glm::vec3 mx = m.bounds_max();
        float h_native = std::max(0.001f, mx.y - mn.y);
        constexpr float TARGET_HEIGHT = 1.8f;
        character_model_scale_  = TARGET_HEIGHT / h_native;
        // After scale, the model spans y in [scale*mn.y, scale*mx.y]. Shift
        // by -scale*mn.y so its feet sit at y=0 of the parent. Centre XZ
        // similarly.
        character_model_offset_ = {
            -(mn.x + mx.x) * 0.5f * character_model_scale_,
            -mn.y                  * character_model_scale_,
            -(mn.z + mx.z) * 0.5f * character_model_scale_,
        };
        AABB local{ {mn.x, mn.y, mn.z}, {mx.x, mx.y, mx.z} };
        character_visual_node_->renderable = Renderable{
            &m, local, glm::vec3{1.f}, glm::vec2{1.f, 1.f},
            character_tex_.id() ? &character_tex_ : &checker_tex_};
        PE_INFO("Character model: native y=[%.3f,%.3f] -> scale %.4f offset y=%.3f",
                mn.y, mx.y, character_model_scale_, character_model_offset_.y);
    } else {
        character_visual_node_->renderable = make_renderable({0.65f, 0.55f, 0.45f});
    }

    sync_vehicle_scene();
    sync_character_scene();

    // Spring arm starts behind the character looking forward (yaw=-90 = -Z).
    spring_.yaw_deg      = -90.f;
    spring_.pitch_deg    = -10.f;
    spring_.desired_dist = 4.5f;
    spring_.anchor       = character_.eye_position();
    spring_.update(world_collision_);
    camera_.position = spring_.camera_position;
    camera_.yaw      = spring_.yaw_deg;
    camera_.pitch    = spring_.pitch_deg;

    stats_start_ = Clock::now();
    last_frame_  = Clock::now();
    running_     = true;

    PE_INFO("Phase 7 ready. WASD walk, mouse look, Space jump. F enter/exit car. "
            "C toggle debug fly. ESC release mouse. Ctrl+Q quit.");
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
                          "pengine | %s | cells:%d bld:%d traffic:%d"
                          " | fps:%d worst:%.1fms | car:%.0fkm/h%s%s",
                          mode_name(mode_),
                          st.loaded_cells, world_collision_.building_count(),
                          traffic_.active(),
                          fps_frames_, max_frame_ms_,
                          vehicle_.speed_kmh(),
                          vehicle_.airborne() ? " AIR" : "",
                          can_enter_car_ ? "  [F to enter]" : "");
            SDL_SetWindowTitle(window_.sdl(), title);
            PE_INFO("%s  fps=%d worst=%.1fms  car=%.0fkm/h%s  traffic=%d%s",
                    mode_name(mode_), fps_frames_, max_frame_ms_,
                    vehicle_.speed_kmh(),
                    vehicle_.airborne() ? " AIR" : " GND",
                    traffic_.active(),
                    can_enter_car_ ? "  ENTER" : "");
            fps_frames_   = 0;
            max_frame_ms_ = 0.0;
            stats_start_  = Clock::now();
        }
    }
    return 0;
}

void Application::shutdown() {
    traffic_.shutdown();
    streamer_.shutdown();
    SDL_SetRelativeMouseMode(SDL_FALSE);
    debug_draw_.shutdown();
    lit_shader_.destroy();
    cube_mesh_.destroy();
    checker_tex_.destroy();
    asphalt_tex_.destroy();
    grass_tex_.destroy();
    facade_tex_.destroy();
    character_tex_.destroy();
    window_.shutdown();
}

void Application::enter_mode(Mode m) {
    if (mode_ == m) return;

    if (m == Mode::OnFoot) {
        // Make sure character is grounded before re-entering on-foot.
        spring_.desired_dist = 4.5f;
        spring_.anchor       = character_.eye_position();
        character_node_->visible = true;
    } else if (m == Mode::InVehicle) {
        spring_.desired_dist = 8.5f;
        // Match camera yaw to vehicle heading so the player faces the road.
        glm::vec3 vfwd = vehicle_.forward();
        spring_.yaw_deg = glm::degrees(std::atan2(vfwd.z, vfwd.x));
        spring_.pitch_deg = -12.f;
        character_node_->visible = false;
    }
    mode_ = m;
}

void Application::try_toggle_vehicle() {
    if (mode_ == Mode::OnFoot) {
        if (can_enter_car_) enter_mode(Mode::InVehicle);
    } else if (mode_ == Mode::InVehicle) {
        // Exit to the left side of the car, snapped to terrain.
        glm::vec3 exit = vehicle_.position() + vehicle_.right() * (-2.5f);
        exit.y = Heightmap::sample(exit.x, exit.z);
        character_.teleport(exit);
        enter_mode(Mode::OnFoot);
    }
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

    if (input_.pressed(SDL_SCANCODE_F)) try_toggle_vehicle();

    if (input_.pressed(SDL_SCANCODE_C)) {
        if (mode_ == Mode::DebugFly) {
            enter_mode(saved_mode_);
        } else {
            saved_mode_ = mode_;
            mode_ = Mode::DebugFly;
            // Hand the camera back to the user; nothing else to set up.
        }
    }
}

void Application::update_on_foot(float dt, float mdx, float mdy) {
    spring_.apply_mouse(mdx, mdy);
    spring_.anchor = character_.eye_position();
    spring_.update(world_collision_);

    character_.update(dt, input_, spring_.yaw_deg, world_collision_);

    // Face the direction we're walking. Lerp gently so quick mouse turns
    // don't snap the body around.
    glm::vec3 vh = character_.velocity();
    vh.y = 0.f;
    if (glm::length(vh) > 0.5f) {
        float target = glm::degrees(std::atan2(vh.z, vh.x));
        // wrap to [-180, 180]
        float diff = std::fmod(target - character_facing_yaw_deg_ + 540.f, 360.f) - 180.f;
        character_facing_yaw_deg_ += diff * std::min(1.f, 12.f * dt);
    }

    camera_.position = spring_.camera_position;
    camera_.yaw      = spring_.yaw_deg;
    camera_.pitch    = spring_.pitch_deg;
}

void Application::update_in_vehicle(float dt, float mdx, float mdy) {
    spring_.apply_mouse(mdx, mdy);
    spring_.anchor = vehicle_.position() + glm::vec3{0.f, 1.2f, 0.f};
    spring_.update(world_collision_);

    camera_.position = spring_.camera_position;
    camera_.yaw      = spring_.yaw_deg;
    camera_.pitch    = spring_.pitch_deg;
    (void)dt;
}

void Application::update(double dt) {
    float fdt = static_cast<float>(dt);
    lit_shader_.hot_reload();

    float mdx = mouse_captured_ ? input_.mouse_dx() : 0.f;
    float mdy = mouse_captured_ ? input_.mouse_dy() : 0.f;

    // Vehicle inputs only when driving; physics always runs.
    if (mode_ == Mode::InVehicle) {
        bool w_down = input_.down(SDL_SCANCODE_W);
        bool s_down = input_.down(SDL_SCANCODE_S);
        float v_fwd = glm::dot(vehicle_.body().linear_vel, vehicle_.forward());

        float thr = 0.f, brk = 0.f;
        if (w_down && s_down) {
            brk = 1.f;
        } else if (w_down) {
            if (v_fwd < -0.5f) brk = 1.f;   // braking out of reverse
            else               thr = 1.f;
        } else if (s_down) {
            if (v_fwd >  0.5f) brk = 1.f;   // braking out of forward
            else               thr = -1.f;  // reverse
        }
        float steer = (input_.down(SDL_SCANCODE_D) ? 1.f : 0.f)
                    - (input_.down(SDL_SCANCODE_A) ? 1.f : 0.f);
        bool  hb   = input_.down(SDL_SCANCODE_SPACE);
        vehicle_.set_inputs(thr, brk, steer, hb);
    } else {
        vehicle_.set_inputs(0.f, 0.f, 0.f, false);
    }
    vehicle_.substep(fdt * 0.5f, world_collision_);
    vehicle_.substep(fdt * 0.5f, world_collision_);

    if (mode_ == Mode::DebugFly) {
        camera_.update(fdt, input_, mdx, mdy);
    } else if (mode_ == Mode::OnFoot) {
        update_on_foot(fdt, mdx, mdy);
    } else /* InVehicle */ {
        update_in_vehicle(fdt, mdx, mdy);
    }

    // Update proximity prompt for the next F press.
    can_enter_car_ = (mode_ == Mode::OnFoot)
                  && glm::distance(character_.feet_position(), vehicle_.position())
                     < ENTRY_RADIUS;

    // Advance the character walk-cycle phase (strides). Faster when moving,
    // slow idle breath when still. Read in sync_character_scene.
    {
        glm::vec3 vh = character_.velocity();
        vh.y = 0.f;
        float speed = glm::length(vh);
        constexpr float WALK_REF = 6.f;            // CharacterController::MOVE_SPEED
        double rate = (speed > 0.5f) ? double(speed / WALK_REF) : 0.30;
        walk_phase_ += dt * rate;
        if (walk_phase_ > 1024.0) walk_phase_ -= 1024.0;
    }

    sync_vehicle_scene();
    sync_character_scene();
    streamer_.pump(camera_.position);
    world_time_ += dt;
    traffic_.update(fdt, world_time_, camera_.position);
    scene_.update();
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
        pos.y -= wheels[i].visual_drop;
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

void Application::sync_character_scene() {
    if (!character_node_) return;

    // Pose root at feet, rotated by facing yaw.
    glm::vec3 feet = character_.feet_position();
    character_node_->transform.position = feet;
    character_node_->transform.rotation =
        glm::angleAxis(glm::radians(-character_facing_yaw_deg_ - 90.f),
                        glm::vec3{0.f, 1.f, 0.f});
    character_node_->transform.scale    = {1.f, 1.f, 1.f};
    character_node_->mark_dirty();

    // Walk animation: vertical bob (2 per stride) + tiny yaw zigzag (1 per
    // stride) when moving; subtle breathing bob when idle.
    glm::vec3 vh = character_.velocity();
    vh.y = 0.f;
    float speed = glm::length(vh);
    bool  moving = speed > 0.5f;
    constexpr float TWO_PI = 6.2831853f;
    float phase = static_cast<float>(walk_phase_);
    float bob_y, yaw_off_deg;
    if (moving) {
        bob_y       = std::sin(phase * TWO_PI * 2.f) * 0.045f;   // ±4.5 cm
        yaw_off_deg = std::sin(phase * TWO_PI       ) * 4.f;     // ±4°
    } else {
        bob_y       = std::sin(phase * TWO_PI       ) * 0.012f;  // breathing
        yaw_off_deg = 0.f;
    }
    glm::quat anim_yaw = glm::angleAxis(glm::radians(yaw_off_deg),
                                          glm::vec3{0.f, 1.f, 0.f});

    // Visual child: model offset + uniform scale + walk-cycle pose.
    if (!character_model_.submeshes().empty()) {
        character_visual_node_->transform.position =
            character_model_offset_ + glm::vec3{0.f, bob_y, 0.f};
        character_visual_node_->transform.rotation = anim_yaw;
        character_visual_node_->transform.scale    = glm::vec3{character_model_scale_};
    } else {
        character_visual_node_->transform.position = {0.f, 0.9f + bob_y, 0.f};
        character_visual_node_->transform.rotation = anim_yaw;
        character_visual_node_->transform.scale    = {0.55f, 1.8f, 0.4f};
    }
    character_visual_node_->mark_dirty();
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

    // Forward raycast (only really useful in DebugFly; harmless otherwise).
    if (mode_ == Mode::DebugFly) {
        glm::vec3 ray_origin = camera_.position;
        glm::vec3 ray_dir    = camera_.forward();
        RayHit    hit        = world_collision_.raycast(ray_origin, ray_dir, 200.f);
        last_ray_hit_  = hit.hit;
        last_ray_dist_ = hit.t;
        if (hit.hit) { debug_draw_.line(ray_origin, hit.position);
                       debug_draw_.cross(hit.position, 0.4f); }
        else         { debug_draw_.line(ray_origin, ray_origin + ray_dir * 200.f); }
    }

    // Wheel contact markers.
    for (const Wheel& w : vehicle_.wheels()) {
        if (w.grounded) debug_draw_.cross(w.contact_world, 0.2f);
    }

    // Enter-vehicle prompt: ring around car when in range.
    if (can_enter_car_) {
        glm::vec3 base = vehicle_.position();
        base.y -= vehicle_.chassis_full_extents.y * 0.5f;
        debug_draw_.cylinder_xz(base, ENTRY_RADIUS, 0.05f, 32);
    }

    debug_draw_.flush(vp, glm::vec3{1.f, 0.85f, 0.2f});

    window_.swap();
}

} // namespace pengine
