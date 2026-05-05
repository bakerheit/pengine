#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>

#include "core/log.h"
#include "render/mesh.h"
#include "scene/frustum.h"
#include "scene/scene_node.h"
#include "world/cell_coord.h"
#include "world/heightmap.h"
#include "world/road_grid.h"
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
    if (!lit_instanced_shader_.load(ASSETS_DIR "/shaders/lit_instanced.vert",
                                     ASSETS_DIR "/shaders/lit.frag")) return false;
    if (!skinned_shader_.load(ASSETS_DIR "/shaders/skinned.vert",
                               ASSETS_DIR "/shaders/lit.frag")) return false;
    if (!debug_draw_.init(ASSETS_DIR)) return false;
    if (!particles_.init(ASSETS_DIR)) return false;
    if (!hud_.init(ASSETS_DIR)) return false;

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

    weapons_.init(ASSETS_DIR);

    // World model registry. Step 1 of the IDE/IPL migration: load defs from
    // disk and resolve their mesh/texture handles. Streamer still uses the
    // legacy procedural path; this is just a smoke test that the pipeline
    // works end-to-end before we cut over.
    if (!world_models_.load_ide(ASSETS_DIR "/world/streets.ide") ||
        !world_models_.resolve_assets()) {
        PE_WARN("ModelRegistry init failed (continuing with legacy world)");
    } else {
        for (const auto& [id, def] : world_models_.all()) {
            PE_INFO("  model %u '%s' mesh=%s tex=%s draw=%.0f flags=%s",
                    id, def.name.c_str(), def.mesh_path.c_str(),
                    def.texture_path.c_str(), def.draw_dist,
                    format_model_flags(def.flags));
        }
    }

    WorldConfig world_cfg;
    {
        float world_size_m =
            static_cast<float>(world_cfg.world_cells_x) * world_cfg.cell_size;
        Heightmap::init(world_size_m, ASSETS_DIR "/world/heightmap.png");
    }

    // Spawn at the central intersection of the world's middle cell — the
    // basin area between the river and the northern plateau, where the
    // procedural city generator gives us a clear 4-way intersection.
    float cell = world_cfg.cell_size;
    int   ci   = world_cfg.world_cells_x / 2;
    int   cj   = world_cfg.world_cells_z / 2;
    glm::vec3 intersection{(static_cast<float>(ci) + 0.5f) * cell, 0.f,
                           (static_cast<float>(cj) + 0.5f) * cell};

    camera_.move_speed = 80.f;
    camera_.far_z      = 2000.f;

    streamer_.init(world_cfg, &scene_, &world_collision_,
                    &world_models_, &grass_tex_, &road_graph_);
    // Traffic owns *every* car, including the player's. Pass the cube /
    // checker handles for traffic-light scaffolding; body + wheel assets are
    // loaded internally.
    TrafficSystem::LightVisuals tvis;
    tvis.cube_mesh   = &cube_mesh_;
    tvis.checker_tex = &checker_tex_;
    traffic_.init(scene_, tvis, road_graph_, /*target_ai_count=*/ 30);
    pedestrians_.init(scene_, road_graph_, /*target_count=*/ 30);

    glm::vec3 spawn = intersection;
    spawn.y = Heightmap::sample(spawn.x, spawn.z);
    if (!player_.init(scene_, ASSETS_DIR, spawn)) return false;
    player_.sync_scene_to_initial_pose();

    // Player car — one block east of the central intersection. spawn_player_car
    // creates the entity, sets driver = Player, and registers it as the car
    // input/HUD/camera should follow.
    glm::vec3 car_spawn = intersection + glm::vec3{62.f, 0.f, 0.f};
    car_spawn.y = Heightmap::sample(car_spawn.x, car_spawn.z) + 1.5f;
    traffic_.spawn_player_car(car_spawn, /*yaw_deg=*/ -90.f);

    // Spring arm starts behind the character looking forward (yaw=-90 = -Z).
    spring_.yaw_deg      = -90.f;
    spring_.pitch_deg    = -10.f;
    spring_.desired_dist = 4.5f;
    spring_.anchor       = player_.eye_position();
    spring_.update(world_collision_);
    camera_.position = spring_.camera_position;
    camera_.yaw      = spring_.yaw_deg;
    camera_.pitch    = spring_.pitch_deg;

    stats_start_ = Clock::now();
    last_frame_  = Clock::now();
    running_     = true;

    if (!audio_.init())
        PE_WARN("AudioEngine init failed — running without audio");

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
                          " | fps:%d worst:%.1fms | wanted:%d hp:%.0f | car:%.0fkm/h%s%s",
                          mode_name(mode_),
                          st.loaded_cells, world_collision_.building_count(),
                          traffic_.active(),
                          fps_frames_, max_frame_ms_, wanted_.level(),
                          player_.health(),
                          traffic_.player_car()
                              ? traffic_.player_car()->vehicle.speed_kmh() : 0.f,
                          traffic_.player_car() && traffic_.player_car()->vehicle.airborne()
                              ? " AIR" : "",
                          can_enter_car_ ? "  [F to enter]" : "");
            SDL_SetWindowTitle(window_.sdl(), title);
            PE_INFO("%s  fps=%d worst=%.1fms  wanted=%d hp=%.0f  car=%.0fkm/h%s  traffic=%d%s",
                    mode_name(mode_), fps_frames_, max_frame_ms_,
                    wanted_.level(), player_.health(),
                    traffic_.player_car()
                        ? traffic_.player_car()->vehicle.speed_kmh() : 0.f,
                    traffic_.player_car() && traffic_.player_car()->vehicle.airborne()
                        ? " AIR" : " GND",
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
    audio_.shutdown();
    traffic_.shutdown();
    pedestrians_.shutdown();
    streamer_.shutdown();
    SDL_SetRelativeMouseMode(SDL_FALSE);
    debug_draw_.shutdown();
    particles_.shutdown();
    hud_.shutdown();
    weapons_.shutdown();
    player_.shutdown();
    lit_shader_.destroy();
    lit_instanced_shader_.destroy();
    cube_mesh_.destroy();
    checker_tex_.destroy();
    asphalt_tex_.destroy();
    grass_tex_.destroy();
    facade_tex_.destroy();
    sidewalk_tex_.destroy();
    window_.shutdown();
}

void Application::enter_mode(Mode m) {
    if (mode_ == m) return;

    if (m == Mode::OnFoot) {
        // Make sure character is grounded before re-entering on-foot.
        spring_.desired_dist = 4.5f;
        spring_.anchor       = player_.eye_position();
        player_.set_visible(true);
    } else if (m == Mode::InVehicle) {
        spring_.desired_dist = 8.5f;
        // Match camera yaw to vehicle heading so the player faces the road.
        if (auto* pc = traffic_.player_car()) {
            glm::vec3 vfwd = pc->vehicle.forward();
            spring_.yaw_deg = glm::degrees(std::atan2(vfwd.z, vfwd.x));
            spring_.pitch_deg = -12.f;
        }
        player_.set_visible(false);
    }
    mode_ = m;
}

void Application::try_toggle_vehicle() {
    if (mode_ == Mode::OnFoot) {
        // F-to-enter: transfer the Player driver flag to the targeted car.
        // No teleport — the car the player walked up to stays in place; the
        // previously-driven car (if any) is left as Parked.
        if (steal_target_) {
            if (steal_target_->driver == TrafficSystem::Driver::AI ||
                steal_target_->driver == TrafficSystem::Driver::Police) {
                wanted_.add_heat(2.5f);
            }
            traffic_.set_player_driver(steal_target_);
            enter_mode(Mode::InVehicle);
        }
    } else if (mode_ == Mode::InVehicle) {
        // Exit to the left side of the car, snapped to terrain. The car
        // we were just in becomes Parked (handbrake on), staying where it is.
        if (auto* pc = traffic_.player_car()) {
            glm::vec3 exit = pc->vehicle.position()
                           + pc->vehicle.right() * (-2.5f);
            exit.y = Heightmap::sample(exit.x, exit.z);
            player_.controller().teleport(exit);
        }
        traffic_.set_player_driver(nullptr); // demote previous player → Parked
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
                input_.handle_event(e);
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
    if (input_.pressed(SDL_SCANCODE_M)) log_debug_area();

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

void Application::update_in_vehicle(float dt, float mdx, float mdy) {
    spring_.apply_mouse(mdx, mdy);
    if (auto* pc = traffic_.player_car()) {
        spring_.anchor = pc->vehicle.position() + glm::vec3{0.f, 1.2f, 0.f};
    }
    spring_.update(world_collision_);

    camera_.position = spring_.camera_position;
    camera_.yaw      = spring_.yaw_deg;
    camera_.pitch    = spring_.pitch_deg;
    (void)dt;
}

void Application::update(double dt) {
    float fdt = static_cast<float>(dt);
    lit_shader_.hot_reload();
    lit_instanced_shader_.hot_reload();

    float mdx = mouse_captured_ ? input_.mouse_dx() : 0.f;
    float mdy = mouse_captured_ ? input_.mouse_dy() : 0.f;

    // Player driver inputs. Read keyboard while InVehicle and feed via the
    // CarSystem; substep physics + AI updates run inside traffic_.update().
    if (mode_ == Mode::InVehicle && traffic_.player_car()) {
        const Vehicle& pv = traffic_.player_car()->vehicle;
        bool w_down = input_.down(SDL_SCANCODE_W);
        bool s_down = input_.down(SDL_SCANCODE_S);
        float v_fwd = glm::dot(pv.body().linear_vel, pv.forward());

        float thr = 0.f, brk = 0.f;
        if (w_down && s_down) {
            brk = 1.f;
        } else if (w_down) {
            if (v_fwd < -0.5f) brk = 1.f;
            else               thr = 1.f;
        } else if (s_down) {
            if (v_fwd >  0.5f) brk = 1.f;
            else               thr = -1.f;
        }
        float steer = (input_.down(SDL_SCANCODE_D) ? 1.f : 0.f)
                    - (input_.down(SDL_SCANCODE_A) ? 1.f : 0.f);
        bool  hb   = input_.down(SDL_SCANCODE_SPACE);
        traffic_.set_player_inputs(thr, brk, steer, hb);
    } else {
        traffic_.set_player_inputs(0.f, 0.f, 0.f, /*handbrake=*/ false);
    }

    if (mode_ == Mode::DebugFly) {
        camera_.update(fdt, input_, mdx, mdy);
    } else if (mode_ == Mode::OnFoot) {
        bool wants_fire = player_.update_on_foot(fdt, input_, camera_, spring_,
                                                  world_collision_, audio_,
                                                  mdx, mdy);
        if (wants_fire) {
            glm::vec3 origin = camera_.position;
            glm::vec3 dir    = camera_.forward();
            auto fr = weapons_.fire(origin, dir, world_collision_, traffic_,
                                    pedestrians_);
            if (fr.hit_ped)
                particles_.emit_sparks(fr.hit_point, glm::vec3{0.f, 2.f, 0.f}, 16);
            audio_.play_gunshot();
            pedestrians_.notify_gunshot(origin);
            debug_draw_.line(origin + dir * 0.3f, fr.hit_point);
            wanted_.add_heat(fr.heat_delta);
        }
    } else /* InVehicle */ {
        update_in_vehicle(fdt, mdx, mdy);
    }

    // F-to-enter target: closest car (any driver) within ENTRY_RADIUS. The
    // car the player is approaching stays exactly where it is — F just
    // transfers the Player driver flag (see try_toggle_vehicle).
    steal_target_  = nullptr;
    can_enter_car_ = false;
    if (mode_ == Mode::OnFoot) {
        steal_target_ = traffic_.find_nearest(player_.feet_position(),
                                               ENTRY_RADIUS);
        can_enter_car_ = (steal_target_ != nullptr);
    }

    player_.update_pose(dt, world_time_);
    streamer_.pump(camera_.position);
    world_time_ += dt;
    wanted_.update(fdt);
    glm::vec3 police_target = player_.feet_position();
    if (mode_ == Mode::InVehicle && traffic_.player_car()) {
        police_target = traffic_.player_car()->vehicle.position();
    }
    {
        traffic_.set_police_response(wanted_.level(), police_target);
        pedestrians_.set_police_context(wanted_.level(), police_target);
    }
    // Single per-frame entry point: AI lane-follow (kinematic), player +
    // parked physics substeps, traffic-light state, and visual sync for
    // every car all happen here.
    traffic_.update(fdt, world_time_, camera_.position, world_collision_);

    if (wanted_.level() > 3) {
        for (const auto& cp : traffic_.cars()) {
            if (!cp || cp->driver != TrafficSystem::Driver::Police) continue;
            if (pedestrians_.has_police_for_car(cp.get())) continue;
            glm::vec3 car_pos = cp->vehicle.position();
            glm::vec3 d = police_target - car_pos;
            d.y = 0.f;
            if (glm::dot(d, d) > 45.f * 45.f) continue;

            glm::vec3 exit = car_pos - cp->vehicle.right() * 2.0f;
            exit.y = Heightmap::sample(exit.x, exit.z);
            glm::vec3 to_target = police_target - exit;
            to_target.y = 0.f;
            float yaw = glm::length(to_target) > 1e-4f
                ? glm::degrees(std::atan2(to_target.z, to_target.x))
                : 0.f;
            if (pedestrians_.spawn_police_officer(exit, yaw, cp.get())) {
                cp->driver = TrafficSystem::Driver::Parked;
                cp->vehicle.set_inputs(0.f, 0.f, 0.f, true);
            }
        }
    }

    pedestrians_.update(fdt, camera_.position, world_collision_);

    // Vehicle-vs-pedestrian impacts. Build a flat hitbox per car from
    // its current pose + chassis dims; a meaningful-speed contact kills
    // the ped and triggers the hit-by-car death anim.
    {
        std::vector<PedestrianSystem::CarHitbox> car_hits;
        car_hits.reserve(traffic_.cars().size());
        for (const auto& car_ptr : traffic_.cars()) {
            if (!car_ptr) continue;
            const Vehicle& v = car_ptr->vehicle;
            glm::vec3 fwd = v.forward(); fwd.y = 0.f;
            glm::vec3 rgt = v.right();   rgt.y = 0.f;
            float fl = glm::length(fwd);
            float rl = glm::length(rgt);
            if (fl < 1e-4f || rl < 1e-4f) continue;
            PedestrianSystem::CarHitbox h;
            h.center       = v.position();
            h.forward_xz   = fwd / fl;
            h.right_xz     = rgt / rl;
            // chassis_full_extents is {width(X), height(Y), length(Z)};
            // forward in vehicle space is -Z, so half-length = Z/2.
            h.half_length  = v.chassis_full_extents.z * 0.5f;
            h.half_width   = v.chassis_full_extents.x * 0.5f;
            h.half_height  = v.chassis_full_extents.y * 0.5f;
            // Approximate world velocity from speed along forward —
            // Vehicle doesn't expose linear_vel publicly and pure
            // forward speed is a fine proxy for impact damage.
            h.velocity     = v.forward() * v.speed();
            car_hits.push_back(h);
        }
        pedestrians_.process_vehicle_impacts(car_hits);
    }

    for (const auto& ev : pedestrians_.police_vehicle_events()) {
        for (const auto& cp : traffic_.cars()) {
            if (!cp || cp.get() != ev.car_id) continue;
            if (cp->driver == TrafficSystem::Driver::Parked) {
                cp->driver = TrafficSystem::Driver::Police;
                cp->vehicle.set_inputs(0.f, 0.f, 0.f, false);
            }
            break;
        }
    }

    for (const auto& shot : pedestrians_.police_shots()) {
        glm::vec3 to_player = shot.target - shot.origin;
        float dist = glm::length(to_player);
        if (dist < 1e-4f) continue;
        glm::vec3 dir = to_player / dist;
        RayHit block = world_collision_.raycast(shot.origin, dir, dist);
        if (block.hit && block.t < dist - 0.6f) continue;

        audio_.play_gunshot();
        pedestrians_.notify_gunshot(shot.origin);
        debug_draw_.line(shot.origin, shot.target);
        particles_.emit_sparks(shot.target, -dir * 2.f, 4);

        // Distance-spread aim (pedestrian.cpp:advance_police) means the
        // shot ray may now miss the player entirely. Treat the player as
        // a vertical capsule of radius PLAYER_HIT_RADIUS_M centred on
        // the same torso point the police aimed for, and only deduct HP
        // if the ray's closest approach is inside that radius.
        constexpr float PLAYER_HIT_RADIUS_M = 0.45f;
        glm::vec3 player_torso = police_target + glm::vec3{0.f, 1.2f, 0.f};
        float t_close = glm::clamp(
            glm::dot(player_torso - shot.origin, dir), 0.f, dist);
        glm::vec3 closest = shot.origin + dir * t_close;
        if (glm::length(player_torso - closest) > PLAYER_HIT_RADIUS_M)
            continue;

        player_.apply_damage(8.f);
        if (player_.is_dead()) {
            if (mode_ == Mode::InVehicle) {
                traffic_.set_player_driver(nullptr);
                enter_mode(Mode::OnFoot);
            }
            player_.respawn();
            wanted_.reset();
            break;
        }
    }

    // Spark emission + scrape audio: any car whose substep recorded a
    // scraping chassis corner over a paved surface (road or sidewalk)
    // both sprays a burst of sparks and contributes to the looping
    // metal-scrape sound. Gated to paved surfaces so dirt / grass
    // off-road slides stay silent and visually clean.
    float scrape_max_speed = 0.f;
    {
        auto try_emit = [&](const TrafficSystem::Car& c) {
            for (const auto& sc : c.vehicle.scrape_contacts()) {
                if (!is_paved_surface(sc.world_pos.x, sc.world_pos.z))
                    continue;
                // Tangential speed gates emission and density — slow drags
                // get a couple of weak sparks; high-speed scrapes shower.
                glm::vec3 tan = sc.world_vel; tan.y = 0.f;
                float tan_speed = glm::length(tan);
                if (tan_speed < 2.0f) continue;
                int count = 3 + static_cast<int>(std::min(10.f,
                                tan_speed * 0.4f));
                particles_.emit_sparks(sc.world_pos, sc.world_vel, count);
                if (tan_speed > scrape_max_speed)
                    scrape_max_speed = tan_speed;
            }
        };
        if (auto* pc = traffic_.player_car()) try_emit(*pc);
        // Parked cars (e.g. just-hit AI) might also be scraping. AI cars
        // are kinematic and never populate scrape_contacts, so the filter
        // is defensive rather than load-bearing.
        for (const auto& cp : traffic_.cars()) {
            if (!cp || cp.get() == traffic_.player_car()) continue;
            if (cp->driver != TrafficSystem::Driver::AI) try_emit(*cp);
        }
    }
    particles_.update(fdt);

    // Drive the looping metal-scrape sound off the same per-frame max.
    // Sample is already a real metal-on-concrete scrape, so pitch sits
    // near 1.0 — small speed-driven nudges add life without changing
    // the timbre. The sample itself is mastered quiet, so we drive
    // intensity well above 1.0 (miniaudio amplifies past unity) and
    // floor it with a non-zero MIN so even slow drags are audible.
    {
        constexpr float REF_SPEED   = 12.f;  // m/s for full-intensity scrape
        constexpr float MAX_VOLUME  = 4.0f;
        constexpr float MIN_VOLUME  = 1.2f;  // floor when scrape is just starting
        constexpr float BASE_PITCH  = 0.85f;
        constexpr float PITCH_RANGE = 0.30f;
        float t = std::min(1.f, scrape_max_speed / REF_SPEED);
        float intensity = (scrape_max_speed > 0.f)
                            ? MIN_VOLUME + (MAX_VOLUME - MIN_VOLUME) * t
                            : 0.f;
        float pitch     = BASE_PITCH + PITCH_RANGE * t;
        audio_.update_scrape(fdt, intensity, pitch);
    }

    scene_.update();

    {
        const bool in_veh = (mode_ == Mode::InVehicle);
        float spd     = 0.f;
        float max_spd = 1.f;
        if (in_veh) {
            if (const auto* pc = traffic_.player_car()) {
                spd     = pc->vehicle.speed_kmh();
                max_spd = pc->vehicle.max_speed * 3.6f;
            }
        }
        audio_.update(spd, max_spd, in_veh,
                      /*horn*/       input_.pressed(SDL_SCANCODE_H),
                      /*handbrake*/  input_.pressed(SDL_SCANCODE_SPACE));

        // Spatialised AI traffic: each running AI car contributes an engine
        // voice positioned at its world-space chassis. The pool inside
        // AudioEngine picks the nearest 8 within audible range and lets
        // miniaudio handle inverse-distance falloff. AI cars are kinematic
        // (their RigidBody linear_vel is zeroed each frame by
        // set_kinematic_pose), so vehicle.speed_kmh() is always 0 for
        // them — the real ground speed lives on Car.ai_speed in m/s.
        std::vector<AudioEngine::TrafficSource> sources;
        sources.reserve(traffic_.cars().size());
        for (const auto& cp : traffic_.cars()) {
            if (!cp) continue;
            // The player's own engine is mixed first-person via update()
            // above; parked / wrecked cars don't run engines.
            if (cp.get() == traffic_.player_car()) continue;
            if (cp->driver != TrafficSystem::Driver::AI &&
                cp->driver != TrafficSystem::Driver::Police) continue;
            AudioEngine::TrafficSource ts;
            ts.id            = cp.get();
            ts.position      = cp->vehicle.position();
            ts.speed_kmh     = cp->driver == TrafficSystem::Driver::AI
                             ? cp->ai_speed * 3.6f
                             : cp->vehicle.speed_kmh();
            ts.max_speed_kmh = cp->vehicle.max_speed * 3.6f;
            sources.push_back(ts);
        }
        audio_.update_traffic(camera_.position, sources);

        // Pedestrian footsteps: drain step events recorded during
        // pedestrians_.update() and play each as a spatialised one-shot.
        // Gated to paved surfaces (parity with the spark gating) so peds
        // jaywalking onto grass in phase 3 won't echo a concrete sample.
        // Listener was already positioned by update_traffic above.
        for (const auto& ev : pedestrians_.step_events()) {
            if (!is_paved_surface(ev.pos.x, ev.pos.z)) continue;
            audio_.play_ped_footstep(ev.pos);
        }
    }
}


void Application::log_debug_area() {
    // Snapshot the world geometry around the player at the moment M is
    // pressed. Easiest debug surface for roads — copy the output and feed
    // it back so I can correlate visible artifacts with carved/raw heights
    // and road-segment sample positions.
    static int counter = 0;
    ++counter;

    glm::vec3 pos;
    glm::vec3 fwd;
    if (mode_ == Mode::InVehicle && traffic_.player_car()) {
        pos = traffic_.player_car()->vehicle.position();
        fwd = traffic_.player_car()->vehicle.orientation() *
              glm::vec3{0.f, 0.f, -1.f};
    } else {
        pos = player_.feet_position();
        float yr = glm::radians(player_.facing_yaw_deg());
        fwd = {std::cos(yr), 0.f, std::sin(yr)};
    }

    CellCoord cell = world_to_cell(pos.x, pos.z, 256.f);

    // Nearest NS / EW road centerlines.
    int   ns_k = static_cast<int>(std::round(pos.x / ROAD_PITCH));
    int   ew_k = static_cast<int>(std::round(pos.z / ROAD_PITCH));
    float ns_x = static_cast<float>(ns_k) * ROAD_PITCH;
    float ew_z = static_cast<float>(ew_k) * ROAD_PITCH;

    float h_raw_p    = Heightmap::raw_sample(pos.x, pos.z);
    float h_carved_p = Heightmap::sample(pos.x, pos.z);

    PE_INFO("===== DEBUG #%d =====", counter);
    PE_INFO("Player    pos=(%.2f, %.2f, %.2f)  cell=(%d, %d)  mode=%s",
            pos.x, pos.y, pos.z, cell.x, cell.z,
            mode_ == Mode::InVehicle ? "drive" :
            mode_ == Mode::DebugFly  ? "fly"   : "on-foot");
    PE_INFO("Forward   (%.3f, %.3f, %.3f)  bearing=%.1f deg",
            fwd.x, fwd.y, fwd.z,
            glm::degrees(std::atan2(fwd.x, -fwd.z)));
    PE_INFO("Heightmap raw=%.3f  carved=%.3f  delta=%.3f",
            h_raw_p, h_carved_p, h_carved_p - h_raw_p);
    PE_INFO("Nearest   NS road x=%.0f (%+.2fm)   EW road z=%.0f (%+.2fm)",
            ns_x, pos.x - ns_x, ew_z, pos.z - ew_z);

    // 5x5 carved heightmap grid centred on the player, 4 m spacing.
    PE_INFO("Carved heightmap grid (5x5, 4m, dz row x dx col):");
    for (int dz = -2; dz <= 2; ++dz) {
        char line[256] = {};
        int  n = 0;
        for (int dx = -2; dx <= 2; ++dx) {
            float qx = pos.x + static_cast<float>(dx) * 4.f;
            float qz = pos.z + static_cast<float>(dz) * 4.f;
            n += std::snprintf(line + n,
                               sizeof(line) - static_cast<std::size_t>(n),
                               " %7.2f", Heightmap::sample(qx, qz));
        }
        PE_INFO("  z%+3d:%s", dz * 4, line);
    }

    // Sweep 4m steps for ±20m along nearest NS road centerline.
    PE_INFO("NS road slab samples at x=%.0f, z = player_z (-20 .. +20):", ns_x);
    for (int i = -5; i <= 5; ++i) {
        float qz       = pos.z + static_cast<float>(i) * 4.f;
        float carved   = Heightmap::sample(ns_x, qz);
        float raw      = Heightmap::raw_sample(ns_x, qz);
        PE_INFO("  z=%-9.2f  carved=%-7.3f  raw=%-7.3f  delta=%+.3f",
                qz, carved, raw, carved - raw);
    }

    // Same for nearest EW road centerline.
    PE_INFO("EW road slab samples at z=%.0f, x = player_x (-20 .. +20):", ew_z);
    for (int i = -5; i <= 5; ++i) {
        float qx       = pos.x + static_cast<float>(i) * 4.f;
        float carved   = Heightmap::sample(qx, ew_z);
        float raw      = Heightmap::raw_sample(qx, ew_z);
        PE_INFO("  x=%-9.2f  carved=%-7.3f  raw=%-7.3f  delta=%+.3f",
                qx, carved, raw, carved - raw);
    }

    PE_INFO("Cell IPL  assets/world/cells/cell_%d_%d.ipl", cell.x, cell.z);
    PE_INFO("===== END #%d =====", counter);
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

    // ---- Instanced wheels (one draw call covers every visible car) ---------
    lit_instanced_shader_.use();
    lit_instanced_shader_.set("u_view_proj",   vp);
    lit_instanced_shader_.set("u_cam_pos",     camera_.position);
    lit_instanced_shader_.set("u_light_dir",   glm::normalize(glm::vec3{0.6f, 1.f, 0.4f}));
    lit_instanced_shader_.set("u_light_color", glm::vec3{1.f, 0.95f, 0.85f});
    lit_instanced_shader_.set("u_ambient",     glm::vec3{0.18f, 0.22f, 0.28f});
    lit_instanced_shader_.set("u_diffuse",     0);
    lit_instanced_shader_.set("u_tint",        glm::vec3{1.f});
    lit_instanced_shader_.set("u_uv_scale",    glm::vec2{1.f, 1.f});
    traffic_.draw_wheels(lit_instanced_shader_, frustum);

    // ---- Skinned character (manual draw — Scene::draw doesn't skin) --------
    player_.render(skinned_shader_, vp, camera_.position, checker_tex_);

    // ---- Equipped weapon (static mesh attached to right-hand bone) --------
    if (player_.armed() && player_.has_right_hand() && weapons_.ready()) {
        checker_tex_.bind(0);
        weapons_.render(lit_shader_, vp, camera_.position,
                        player_.right_hand_world_xform());
    }

    // ---- AI pedestrians (skinned, one draw per ped) ------------------------
    pedestrians_.render(skinned_shader_, lit_shader_, vp, camera_.position,
                        checker_tex_, weapons_.mesh(), weapons_.grip_offset(),
                        frustum);

    // ---- Particle effects (sparks etc.) ------------------------------------
    particles_.render(vp, camera_.position,
                      static_cast<float>(window_.height()));

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

    // Wheel contact markers (player car only; AI cars are kinematic).
    if (auto* pc = traffic_.player_car()) {
        for (const Wheel& w : pc->vehicle.wheels()) {
            if (w.grounded) debug_draw_.cross(w.contact_world, 0.2f);
        }
    }

    // Enter-vehicle prompt: ring around the targeted car (parked / AI alike).
    if (can_enter_car_ && steal_target_) {
        glm::vec3 base = steal_target_->vehicle.position();
        base.y -= steal_target_->vehicle.chassis_full_extents.y * 0.5f;
        debug_draw_.cylinder_xz(base, ENTRY_RADIUS, 0.05f, 32);
    }

    if (mode_ == Mode::DebugFly)
        traffic_.debug_draw(debug_draw_);

    debug_draw_.flush(vp, glm::vec3{1.f, 0.85f, 0.2f});

    // ---- HUD overlay -------------------------------------------------------
    {
        Hud::State s;
        s.viewport_size_px = {static_cast<float>(window_.width()),
                              static_cast<float>(window_.height())};
        s.show_speedometer = (mode_ == Mode::InVehicle)
                          && traffic_.player_car() != nullptr;
        s.show_crosshair   = player_.armed() && mode_ == Mode::OnFoot;
        s.wanted_level     = wanted_.level();
        s.health           = player_.health();
        if (mode_ == Mode::InVehicle) {
            if (auto* pc = traffic_.player_car()) {
                s.player_pos_world = pc->vehicle.position();
                glm::vec3 fwd = pc->vehicle.orientation() * glm::vec3{0.f, 0.f, -1.f};
                s.player_yaw_deg   = glm::degrees(std::atan2(fwd.x, -fwd.z));
                s.speed_kmh        = pc->vehicle.speed_kmh();
            } else {
                s.player_pos_world = camera_.position;
                s.player_yaw_deg   = 0.f;
            }
        } else {
            s.player_pos_world = player_.feet_position();
            s.player_yaw_deg   = player_.facing_yaw_deg() + 90.f;
        }
        hud_.render(s);
    }

    window_.swap();
}

} // namespace pengine
