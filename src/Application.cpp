#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
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
    if (!skinned_shader_.load(ASSETS_DIR "/shaders/skinned.vert",
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
    sidewalk_tex_.load_sidewalk();

    // Car5 model + paint variants. Mesh is shared between player + AI; each
    // car instance picks a random texture index on spawn for visual variety.
    bool car5_ok =
        load_static_emesh(ASSETS_DIR "/models/vehicles/car5.emesh", car5_mesh_) &&
        car5_paints_[0].load_file(ASSETS_DIR "/Vehicles_psx/Car 05/car5.png") &&
        car5_paints_[1].load_file(ASSETS_DIR "/Vehicles_psx/Car 05/car5_green.png") &&
        car5_paints_[2].load_file(ASSETS_DIR "/Vehicles_psx/Car 05/car5_grey.png");
    if (car5_ok) {
        // Pick a uniform scale so the model length ~= chassis length; centre
        // the model laterally and pin its body so that the natural wheel-
        // arch centre (native y=0.48 in the car5 mesh) lines up with the
        // physics wheel-centre line (mount_y - suspension_rest). That puts
        // the wheels inside the body's wheel arches at rest.
        glm::vec3 mn = car5_mesh_.bounds_min();
        glm::vec3 mx = car5_mesh_.bounds_max();
        float native_length      = std::max(0.001f, mx.z - mn.z);
        float target_length      = vehicle_.chassis_full_extents.z;
        float s                  = target_length / native_length;
        constexpr float ARCH_CY  = 0.30f;  // car5 wheel-cluster centre y, native units
        float wheel_chassis_y    = -vehicle_.chassis_full_extents.y * 0.5f
                                 -  vehicle_.suspension_rest;
        car5_visual_scale_  = glm::vec3{s};
        car5_visual_offset_ = {
            -(mn.x + mx.x) * 0.5f * s,           // centre x
             wheel_chassis_y - ARCH_CY * s,      // arch centre = physics wheel centre
            -(mn.z + mx.z) * 0.5f * s,           // centre z
        };
        PE_INFO("Car5 model: native=%.2fx%.2fx%.2f scale=%.3f",
                mx.x - mn.x, mx.y - mn.y, mx.z - mn.z, s);
    } else {
        PE_WARN("Car5 load failed; cars will fall back to cube visuals");
    }

    // Wheel mesh shared between the player's four wheels. Native wheel
    // radius is the largest |y| (or |z|) coordinate; scale uniformly to
    // match the physics wheel_radius so visible == raycast.
    bool wheel_ok =
        load_static_emesh(ASSETS_DIR "/models/vehicles/wheel.emesh", wheel_mesh_) &&
        wheel_tex_.load_file(ASSETS_DIR "/Vehicles_psx/Wheel/wheel.png");
    if (wheel_ok) {
        glm::vec3 wmn = wheel_mesh_.bounds_min();
        glm::vec3 wmx = wheel_mesh_.bounds_max();
        float native_r = std::max(wmx.y - wmn.y, wmx.z - wmn.z) * 0.5f;
        wheel_visual_scale_ = (native_r > 1e-4f)
            ? vehicle_.wheel_radius / native_r : 1.f;
    } else {
        PE_WARN("Wheel load failed; falling back to cube wheels");
    }

    // Player character: skinned mesh + skeleton + walk animation.
    character_skinned_ =
        load_skinned_emesh(ASSETS_DIR "/models/characters/character_01.emesh",
                           character_skinned_mesh_) &&
        character_skeleton_.load (ASSETS_DIR "/models/characters/character_01.eskel") &&
        character_anim_   .load  (ASSETS_DIR "/models/characters/walking.eanim",
                                   character_skeleton_);
    if (!character_skinned_)
        PE_WARN("Falling back to cube character (skinning load failed)");

    if (character_skinned_) {
        walk_bones_.left_upleg  = character_skeleton_.find_bone("mixamorig:LeftUpLeg");
        walk_bones_.right_upleg = character_skeleton_.find_bone("mixamorig:RightUpLeg");
        walk_bones_.left_leg    = character_skeleton_.find_bone("mixamorig:LeftLeg");
        walk_bones_.right_leg   = character_skeleton_.find_bone("mixamorig:RightLeg");
        walk_bones_.left_arm    = character_skeleton_.find_bone("mixamorig:LeftArm");
        walk_bones_.right_arm   = character_skeleton_.find_bone("mixamorig:RightArm");
        walk_bones_.spine       = character_skeleton_.find_bone("mixamorig:Spine");
        PE_INFO("Walk bones: LU=%d RU=%d LK=%d RK=%d LA=%d RA=%d S=%d",
                walk_bones_.left_upleg, walk_bones_.right_upleg,
                walk_bones_.left_leg,   walk_bones_.right_leg,
                walk_bones_.left_arm,   walk_bones_.right_arm,
                walk_bones_.spine);
    }

    if (!character_tex_.load_file(ASSETS_DIR "/Characters_psx/Textures/Character_01.png"))
        PE_WARN("Falling back to checker for character (texture load failed)");

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

    WorldTextures world_tex{&grass_tex_, &asphalt_tex_, &sidewalk_tex_, &facade_tex_};
    streamer_.init(world_cfg, &scene_, &cube_mesh_, &world_collision_,
                    world_tex, &road_graph_);
    TrafficSystem::Visuals tvis;
    tvis.light_mesh = &cube_mesh_;
    tvis.light_tex  = &checker_tex_;
    if (car5_ok) {
        tvis.car_mesh   = &car5_mesh_;
        tvis.car_scale  = car5_visual_scale_;
        tvis.car_yaw_offset_deg = 180.f; // car5 model forward = +Z; flip to -Z
        tvis.car_paints = {&car5_paints_[0], &car5_paints_[1], &car5_paints_[2]};
    }
    traffic_.init(&scene_, tvis, &road_graph_, /*target_count=*/ 30);

    glm::vec3 spawn = intersection;
    spawn.y = Heightmap::sample(spawn.x, spawn.z);
    character_.teleport(spawn);

    // Car at the next intersection one block east — 62 m along the central
    // E-W road. Lands cleanly on a road slab.
    glm::vec3 car_spawn = intersection + glm::vec3{62.f, 0.f, 0.f};
    car_spawn.y = Heightmap::sample(car_spawn.x, car_spawn.z) + 1.5f;
    vehicle_.spawn(car_spawn, /*yaw_deg=*/ -90.f);

    if (car5_ok) {
        // Align suspension raycasts with the car5 model's actual wheels.
        // Native wheel anchors (low-y vertex clusters per quadrant), then
        // apply scale + 180° Y rotation so model +Z front becomes chassis -Z.
        const float s        = car5_visual_scale_.x;
        const float wheel_x  = 1.038f * s;
        const float wheel_zf = 2.254f * s;  // model front (chassis-local -Z)
        const float wheel_zr = 1.813f * s;  // model rear  (chassis-local +Z)
        const float mount_y  = -vehicle_.chassis_full_extents.y * 0.5f;
        vehicle_.set_wheel_mount(0, {-wheel_x, mount_y, -wheel_zf}); // FL
        vehicle_.set_wheel_mount(1, {+wheel_x, mount_y, -wheel_zf}); // FR
        vehicle_.set_wheel_mount(2, {-wheel_x, mount_y, +wheel_zr}); // RL
        vehicle_.set_wheel_mount(3, {+wheel_x, mount_y, +wheel_zr}); // RR
    }

    AABB cube_aabb;
    cube_aabb.min = cube_mesh_.bounds_min();
    cube_aabb.max = cube_mesh_.bounds_max();

    // Build renderables with an explicit texture so they don't accidentally
    // inherit whatever the previous draw bound.
    auto make_renderable = [&](const glm::vec3& tint) {
        return Renderable{&cube_mesh_, cube_aabb, tint,
                          glm::vec2{1.f, 1.f}, &checker_tex_};
    };

    // Vehicle scene nodes. Player car uses the car5 model when available,
    // textured with the default paint. Wheels are separate nodes so suspension
    // / steering / rolling can drive them independently of the body.
    chassis_node_         = scene_.create_node();
    chassis_visual_node_  = scene_.create_node(chassis_node_);
    if (car5_ok) {
        AABB local;
        local.min = car5_mesh_.bounds_min();
        local.max = car5_mesh_.bounds_max();
        chassis_visual_node_->renderable = Renderable{
            &car5_mesh_, local, glm::vec3{1.f, 1.f, 1.f},
            glm::vec2{1.f, 1.f}, &car5_paints_[0]};
    } else {
        chassis_visual_node_->renderable = make_renderable({0.85f, 0.20f, 0.18f});
    }
    {
        const bool use_wheel_mesh = wheel_ok;
        AABB wheel_local;
        wheel_local.min = use_wheel_mesh ? wheel_mesh_.bounds_min()
                                          : -glm::vec3{0.5f};
        wheel_local.max = use_wheel_mesh ? wheel_mesh_.bounds_max()
                                          :  glm::vec3{0.5f};
        const Mesh*    wm = use_wheel_mesh ? &wheel_mesh_ : &cube_mesh_;
        const Texture* wt = use_wheel_mesh ? &wheel_tex_  : &checker_tex_;
        glm::vec3 tint = use_wheel_mesh ? glm::vec3{1.f}
                                         : glm::vec3{0.10f, 0.10f, 0.10f};
        for (int i = 0; i < 4; ++i) {
            wheel_nodes_[i] = scene_.create_node(chassis_node_);
            wheel_nodes_[i]->renderable = Renderable{
                wm, wheel_local, tint, glm::vec2{1.f, 1.f}, wt};
        }
    }

    // Character: pose root (feet pos + facing yaw) with a visual child that
    // carries model offset + uniform scale. The skinned mesh is drawn out-of-
    // band by Application::render (Scene::draw can't handle skinning today).
    // If skinning load failed, fall back to a tan cube renderable here.
    character_node_ = scene_.create_node();
    character_visual_node_ = scene_.create_node(character_node_);
    if (character_skinned_) {
        glm::vec3 mn = character_skinned_mesh_.bounds_min();
        glm::vec3 mx = character_skinned_mesh_.bounds_max();
        float h_native = std::max(0.001f, mx.y - mn.y);
        constexpr float TARGET_HEIGHT = 1.8f;
        character_model_scale_  = TARGET_HEIGHT / h_native;
        character_model_offset_ = {
            -(mn.x + mx.x) * 0.5f * character_model_scale_,
            -mn.y                  * character_model_scale_,
            -(mn.z + mx.z) * 0.5f * character_model_scale_,
        };
        // No renderable: scene::draw skips. Drawn manually in render().
        PE_INFO("Character: skinned y=[%.3f,%.3f] -> scale %.4f anim=%.2fs",
                mn.y, mx.y, character_model_scale_, character_anim_.duration());
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
    sidewalk_tex_.destroy();
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

    // Advance the walk-anim time accumulator (seconds). The Mixamo walk loop
    // is 1 s = 1 stride, so we just scale by current speed / reference speed.
    // Idle: freeze on first frame instead of playing in place.
    bool char_moving;
    {
        glm::vec3 vh = character_.velocity();
        vh.y = 0.f;
        float speed = glm::length(vh);
        constexpr float WALK_REF = 6.f;
        char_moving = speed > 0.5f;
        if (char_moving) walk_phase_ += dt * double(speed / WALK_REF);
        else             walk_phase_  = 0.0;   // freeze at first frame
    }

    if (character_skinned_) {
        if (char_moving && character_anim_.duration() > 0.f) {
            character_anim_.sample(static_cast<float>(walk_phase_),
                                   character_skeleton_, char_local_poses_);
            // Strip root motion: gameplay drives world position, so the root
            // bone's translation must come from the bind pose, not the anim.
            for (int b = 0; b < character_skeleton_.bone_count(); ++b) {
                if (character_skeleton_.bone(b).parent < 0) {
                    glm::vec3 bind_t{character_skeleton_.bone(b).bind_local[3]};
                    char_local_poses_[static_cast<std::size_t>(b)][3] =
                        glm::vec4{bind_t, 1.f};
                }
            }
        } else {
            const int n = character_skeleton_.bone_count();
            char_local_poses_.resize(static_cast<std::size_t>(n));
            for (int b = 0; b < n; ++b)
                char_local_poses_[static_cast<std::size_t>(b)] =
                    character_skeleton_.bone(b).bind_local;
        }
        character_skeleton_.compute_skin_matrices(char_local_poses_,
                                                    char_skin_matrices_);
    }

    // Roll the visible wheels at v / r. Use signed forward velocity so the
    // wheels reverse direction when reversing.
    {
        float speed_signed = glm::dot(vehicle_.forward(), vehicle_.body().linear_vel);
        wheel_spin_rad_   += static_cast<double>(speed_signed) * dt
                           / static_cast<double>(vehicle_.wheel_radius);
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

    if (car5_mesh_.index_count() > 0) {
        chassis_visual_node_->transform.position = car5_visual_offset_;
        // car5 model forward is local +Z. Chassis frame uses -Z forward (per
        // Vehicle::forward()), so flip the visual 180° around Y.
        chassis_visual_node_->transform.rotation =
            glm::angleAxis(glm::radians(180.f), glm::vec3{0.f, 1.f, 0.f});
        chassis_visual_node_->transform.scale    = car5_visual_scale_;
    } else {
        chassis_visual_node_->transform.position = {0.f, 0.f, 0.f};
        chassis_visual_node_->transform.rotation = glm::quat{1.f, 0.f, 0.f, 0.f};
        chassis_visual_node_->transform.scale    = vehicle_.chassis_full_extents;
    }
    chassis_visual_node_->mark_dirty();

    if (wheel_nodes_[0]) {
        const bool model_wheels = wheel_mesh_.index_count() > 0;
        const auto& wheels      = vehicle_.wheels();
        const float spin        = static_cast<float>(wheel_spin_rad_);
        const glm::vec3 wheel_scale = model_wheels
            ? glm::vec3{wheel_visual_scale_}
            : glm::vec3{0.32f, 2.f * vehicle_.wheel_radius,
                              2.f * vehicle_.wheel_radius};
        for (std::size_t i = 0; i < 4; ++i) {
            glm::vec3 pos = wheels[i].mount_local;
            pos.y -= wheels[i].visual_drop;
            wheel_nodes_[i]->transform.position = pos;

            // Steer (chassis Y) composed with rolling spin (wheel axle = +X).
            // Order steer * spin so the spin happens in the wheel's local frame
            // before steering rotates the wheel around chassis up.
            glm::quat steer{1.f, 0.f, 0.f, 0.f};
            if (wheels[i].is_steering) {
                steer = glm::angleAxis(-vehicle_.steer_rad(),
                                        glm::vec3{0.f, 1.f, 0.f});
            }
            glm::quat roll = model_wheels
                ? glm::angleAxis(-spin, glm::vec3{1.f, 0.f, 0.f})
                : glm::quat{1.f, 0.f, 0.f, 0.f};
            wheel_nodes_[i]->transform.rotation = steer * roll;
            wheel_nodes_[i]->transform.scale    = wheel_scale;
            wheel_nodes_[i]->mark_dirty();
        }
    }
}

void Application::compute_procedural_walk_pose(float phase, bool moving) {
    const int n = character_skeleton_.bone_count();
    char_local_poses_.resize(static_cast<std::size_t>(n));

    // Default: bind pose for every bone.
    for (int b = 0; b < n; ++b)
        char_local_poses_[static_cast<std::size_t>(b)] =
            character_skeleton_.bone(b).bind_local;

    if (!moving) return;

    constexpr float TWO_PI = 6.2831853f;
    // Drive ~2 strides per second of phase. With phase = walk_phase_ in
    // seconds (advances at speed/WALK_REF), this gives 2 strides/sec at
    // full speed.
    float a = phase * TWO_PI * 2.f;
    float s = std::sin(a);

    auto rotate_local = [&](int b, const glm::vec3& axis, float angle) {
        if (b < 0) return;
        char_local_poses_[static_cast<std::size_t>(b)] =
            character_skeleton_.bone(b).bind_local *
            glm::mat4_cast(glm::angleAxis(angle, axis));
    };

    float thigh = glm::radians(30.f) * s;
    float arm   = glm::radians(25.f) * s;
    float knee_l = glm::radians(45.f) * std::max(0.f, -s);
    float knee_r = glm::radians(45.f) * std::max(0.f,  s);

    rotate_local(walk_bones_.left_upleg,  {1, 0, 0}, +thigh);
    rotate_local(walk_bones_.right_upleg, {1, 0, 0}, -thigh);
    rotate_local(walk_bones_.left_leg,    {1, 0, 0}, knee_l);
    rotate_local(walk_bones_.right_leg,   {1, 0, 0}, knee_r);
    rotate_local(walk_bones_.left_arm,    {1, 0, 0}, -arm);
    rotate_local(walk_bones_.right_arm,   {1, 0, 0}, +arm);
}

void Application::sync_character_scene() {
    if (!character_node_) return;

    // Pose root at feet, rotated by facing yaw.
    glm::vec3 feet = character_.feet_position();
    character_node_->transform.position = feet;
    character_node_->transform.rotation =
        glm::angleAxis(glm::radians(-character_facing_yaw_deg_ + 90.f),
                        glm::vec3{0.f, 1.f, 0.f});
    character_node_->transform.scale    = {1.f, 1.f, 1.f};
    character_node_->mark_dirty();

    // Visual child: model offset + uniform scale (skeletal animation handles
    // body motion). Cube fallback if no skinned model.
    if (character_skinned_) {
        character_visual_node_->transform.position = character_model_offset_;
        character_visual_node_->transform.rotation = glm::quat{1.f, 0.f, 0.f, 0.f};
        character_visual_node_->transform.scale    = glm::vec3{character_model_scale_};
    } else {
        character_visual_node_->transform.position = {0.f, 0.9f, 0.f};
        character_visual_node_->transform.rotation = glm::quat{1.f, 0.f, 0.f, 0.f};
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

    // ---- Skinned character (manual draw — Scene::draw doesn't skin) --------
    if (character_skinned_ && character_node_->visible
        && !char_skin_matrices_.empty()) {
        skinned_shader_.use();
        skinned_shader_.set("u_view_proj",   vp);
        skinned_shader_.set("u_cam_pos",     camera_.position);
        skinned_shader_.set("u_light_dir",   glm::normalize(glm::vec3{0.6f, 1.f, 0.4f}));
        skinned_shader_.set("u_light_color", glm::vec3{1.f, 0.95f, 0.85f});
        skinned_shader_.set("u_ambient",     glm::vec3{0.18f, 0.22f, 0.28f});
        skinned_shader_.set("u_diffuse",     0);
        skinned_shader_.set("u_tint",        glm::vec3{1.f});
        skinned_shader_.set("u_uv_scale",    glm::vec2{1.f, 1.f});
        skinned_shader_.set("u_model",       character_visual_node_->world_matrix());
        skinned_shader_.set("u_normal_mat",
            glm::mat3(glm::inverseTranspose(character_visual_node_->world_matrix())));
        int n = static_cast<int>(char_skin_matrices_.size());
        if (n > 64) n = 64;
        skinned_shader_.set_mat4_array("u_bones", char_skin_matrices_.data(), n);

        if (character_tex_.id()) character_tex_.bind(0);
        else                     checker_tex_.bind(0);
        character_skinned_mesh_.draw();
    }

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
