#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
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

// Ray vs axis-aligned box (slab test). Returns true if the ray hits the box
// at a positive t, with `out_t` set to the entry distance (or 0 if origin
// is inside). dir need not be normalised.
static bool ray_vs_aabb(const glm::vec3& origin, const glm::vec3& dir,
                         const glm::vec3& mn, const glm::vec3& mx,
                         float& out_t) {
    constexpr float INF = std::numeric_limits<float>::infinity();
    const glm::vec3 inv{
        dir.x != 0.f ? 1.f / dir.x : INF,
        dir.y != 0.f ? 1.f / dir.y : INF,
        dir.z != 0.f ? 1.f / dir.z : INF,
    };
    const glm::vec3 t1 = (mn - origin) * inv;
    const glm::vec3 t2 = (mx - origin) * inv;
    const glm::vec3 tmin3 = glm::min(t1, t2);
    const glm::vec3 tmax3 = glm::max(t1, t2);
    const float tmin = std::max({tmin3.x, tmin3.y, tmin3.z});
    const float tmax = std::min({tmax3.x, tmax3.y, tmax3.z});
    if (tmax < 0.f || tmin > tmax) return false;
    out_t = tmin >= 0.f ? tmin : tmax;
    return out_t > 0.f;
}

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
    if (!minimap_.init(ASSETS_DIR)) return false;
    if (!particles_.init(ASSETS_DIR)) return false;
    if (!speedometer_.init(ASSETS_DIR)) return false;
    if (!crosshair_.init(ASSETS_DIR)) return false;

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

    // Player character: skinned mesh + skeleton + walk animation.
    character_skinned_ =
        load_skinned_emesh(ASSETS_DIR "/models/characters/character_01.emesh",
                           character_skinned_mesh_) &&
        character_skeleton_.load (ASSETS_DIR "/models/characters/character_01.eskel") &&
        character_anim_   .load  (ASSETS_DIR "/models/characters/walking.eanim",
                                   character_skeleton_);
    if (!character_skinned_)
        PE_WARN("Falling back to cube character (skinning load failed)");

    // Sprint loop is optional: if it fails to load we just stay on the walk
    // anim while shift is held (movement still speeds up).
    if (character_skinned_ &&
        !character_anim_sprint_.load(ASSETS_DIR "/models/characters/sprint.eanim",
                                     character_skeleton_)) {
        PE_WARN("Sprint animation failed to load; shift will still speed up movement");
    }

    // Breathing idle loop is optional: if missing we fall back to bind pose
    // with the walk anim's root rotation (see render path).
    if (character_skinned_ &&
        !character_anim_idle_.load(ASSETS_DIR "/models/characters/breathing_idle.eanim",
                                    character_skeleton_)) {
        PE_WARN("Idle animation failed to load; using bind pose for idle");
    }

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

    // Pistol locomotion + Glock equip wiring. Pistol anims rebind against
    // the player skeleton; missing bone-name channels silently fall back
    // to bind pose, so the player won't T-pose if a future asset breaks.
    if (character_skinned_) {
        if (!character_anim_pistol_walk_.load(
                ASSETS_DIR "/models/characters/pistol_walk.eanim",
                character_skeleton_))
            PE_WARN("Pistol walk anim failed; armed walk will fall back to unarmed");
        if (!character_anim_pistol_run_.load(
                ASSETS_DIR "/models/characters/pistol_run.eanim",
                character_skeleton_))
            PE_WARN("Pistol run anim failed; armed sprint will fall back to unarmed");
        if (!character_anim_pistol_idle_.load(
                ASSETS_DIR "/models/characters/pistol_idle.eanim",
                character_skeleton_))
            PE_WARN("Pistol idle anim failed; armed idle will fall back to unarmed");

        // Right-hand bone: cached so we can recover its world transform
        // each frame from the skin matrices (skin = world * inv_bind, so
        // world = skin * inverse(inv_bind); inv_bind is constant, invert
        // it once at init).
        right_hand_bone_idx_ =
            character_skeleton_.find_bone("mixamorig:RightHand");
        if (right_hand_bone_idx_ >= 0) {
            right_hand_bind_world_ = glm::inverse(
                character_skeleton_.bone(right_hand_bone_idx_).inv_bind);
        } else {
            PE_WARN("RightHand bone not found; gun render disabled");
        }

        // Static gun mesh. One static .emesh, drawn with lit_shader_.
        if (!load_static_emesh(
                ASSETS_DIR "/models/weapons/glock17.emesh", gun_mesh_)) {
            PE_WARN("Glock 17 mesh failed to load; equip will be invisible");
        } else {
            glm::vec3 mn = gun_mesh_.bounds_min();
            glm::vec3 mx = gun_mesh_.bounds_max();
            PE_INFO("Glock bounds min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) "
                    "size=(%.3f,%.3f,%.3f)",
                    mn.x, mn.y, mn.z, mx.x, mx.y, mx.z,
                    mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
        }
    }

    // Baked grip offset from live-tuning (rotation hotkeys [/]/;/' /,/.
    // then translation hotkeys with the same keys). Composition
    // (innermost-first on gun-local vertices):
    //   T(0, 5, -85) cm — slide along gun's local Z so grip lands in palm.
    //   R_z(-90)            — final roll to seat the grip.
    //   R_y(-180)           — face barrel forward.
    //   R_x(-90)            — stand the gun upright (grip down).
    gun_grip_offset_ = glm::rotate(glm::mat4{1.f}, glm::radians(-90.f),
                                    glm::vec3{1.f, 0.f, 0.f})
                     * glm::rotate(glm::mat4{1.f}, glm::radians(-180.f),
                                    glm::vec3{0.f, 1.f, 0.f})
                     * glm::rotate(glm::mat4{1.f}, glm::radians(-90.f),
                                    glm::vec3{0.f, 0.f, 1.f})
                     * glm::translate(glm::mat4{1.f},
                                       glm::vec3{0.f, 5.f, -85.f});

    if (!character_tex_.load_file(ASSETS_DIR "/characters/Characters_psx/Textures/Character_01.png"))
        PE_WARN("Falling back to checker for character (texture load failed)");

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
    character_.teleport(spawn);

    // Player car — one block east of the central intersection. spawn_player_car
    // creates the entity, sets driver = Player, and registers it as the car
    // input/HUD/camera should follow.
    glm::vec3 car_spawn = intersection + glm::vec3{62.f, 0.f, 0.f};
    car_spawn.y = Heightmap::sample(car_spawn.x, car_spawn.z) + 1.5f;
    traffic_.spawn_player_car(car_spawn, /*yaw_deg=*/ -90.f);

    AABB cube_aabb;
    cube_aabb.min = cube_mesh_.bounds_min();
    cube_aabb.max = cube_mesh_.bounds_max();

    // Build renderables with an explicit texture so they don't accidentally
    // inherit whatever the previous draw bound.
    auto make_renderable = [&](const glm::vec3& tint) {
        return Renderable{&cube_mesh_, cube_aabb, tint,
                          glm::vec2{1.f, 1.f}, &checker_tex_};
    };

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
                          " | fps:%d worst:%.1fms | car:%.0fkm/h%s%s",
                          mode_name(mode_),
                          st.loaded_cells, world_collision_.building_count(),
                          traffic_.active(),
                          fps_frames_, max_frame_ms_,
                          traffic_.player_car()
                              ? traffic_.player_car()->vehicle.speed_kmh() : 0.f,
                          traffic_.player_car() && traffic_.player_car()->vehicle.airborne()
                              ? " AIR" : "",
                          can_enter_car_ ? "  [F to enter]" : "");
            SDL_SetWindowTitle(window_.sdl(), title);
            PE_INFO("%s  fps=%d worst=%.1fms  car=%.0fkm/h%s  traffic=%d%s",
                    mode_name(mode_), fps_frames_, max_frame_ms_,
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
    minimap_.shutdown();
    particles_.shutdown();
    speedometer_.shutdown();
    crosshair_.shutdown();
    lit_shader_.destroy();
    lit_instanced_shader_.destroy();
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
        if (auto* pc = traffic_.player_car()) {
            glm::vec3 vfwd = pc->vehicle.forward();
            spring_.yaw_deg = glm::degrees(std::atan2(vfwd.z, vfwd.x));
            spring_.pitch_deg = -12.f;
        }
        character_node_->visible = false;
    }
    mode_ = m;
}

void Application::try_toggle_vehicle() {
    if (mode_ == Mode::OnFoot) {
        // F-to-enter: transfer the Player driver flag to the targeted car.
        // No teleport — the car the player walked up to stays in place; the
        // previously-driven car (if any) is left as Parked.
        if (steal_target_) {
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
            character_.teleport(exit);
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

void Application::update_on_foot(float dt, float mdx, float mdy) {
    spring_.apply_mouse(mdx, mdy);
    spring_.anchor = character_.eye_position();
    spring_.update(world_collision_);

    sprinting_ = input_.down(SDL_SCANCODE_LSHIFT) || input_.down(SDL_SCANCODE_RSHIFT);
    if (input_.pressed(SDL_SCANCODE_E)) armed_ = !armed_;
    if (armed_ && input_.mouse_pressed(SDL_BUTTON_LEFT)) fire_pistol();
    character_.update(dt, input_, spring_.yaw_deg, sprinting_, world_collision_);

    // Face the direction we're walking when moving; otherwise face the
    // camera. Lerp gently so quick mouse turns don't snap the body around.
    // Without the idle branch, releasing W mid-mouse-turn freezes facing
    // partway through the lerp, leaving the character pointing off to the
    // side of where the camera is now looking.
    glm::vec3 vh = character_.velocity();
    vh.y = 0.f;
    float target = (glm::length(vh) > 0.5f)
                    ? glm::degrees(std::atan2(vh.z, vh.x))
                    : spring_.yaw_deg;
    // wrap to [-180, 180]
    float diff = std::fmod(target - character_facing_yaw_deg_ + 540.f, 360.f) - 180.f;
    character_facing_yaw_deg_ += diff * std::min(1.f, 12.f * dt);

    camera_.position = spring_.camera_position;
    camera_.yaw      = spring_.yaw_deg;
    camera_.pitch    = spring_.pitch_deg;

    // Footstep audio: tick a timer paced by current ground speed so steps
    // come faster when sprinting. Gated to (a) actually moving, (b) on
    // the ground, and (c) the foot is over a paved surface — grass / dirt
    // shouldn't trigger the concrete sample.
    {
        glm::vec3 step_vh = character_.velocity();
        step_vh.y = 0.f;
        float speed = glm::length(step_vh);
        const bool moving = speed > 0.5f && character_.grounded();
        if (moving) {
            // ~0.55 s/step at MOVE_SPEED; scales inversely with speed so
            // sprint cadence quickens automatically.
            float interval = 0.55f * CharacterController::MOVE_SPEED
                                   / std::max(speed, 0.5f);
            footstep_timer_ += dt;
            if (footstep_timer_ >= interval) {
                footstep_timer_ -= interval;
                glm::vec3 fp = character_.feet_position();
                if (is_paved_surface(fp.x, fp.z))
                    audio_.play_footstep_concrete();
            }
        } else {
            footstep_timer_ = 0.f;
        }
    }
}

void Application::fire_pistol() {
    constexpr float MAX_RANGE = 200.f;
    const glm::vec3 origin = camera_.position;
    const glm::vec3 dir    = camera_.forward();

    float     closest_t = MAX_RANGE;
    glm::vec3 hit_point = origin + dir * MAX_RANGE;

    // Static world (terrain + buildings) — bullets don't pass through walls.
    RayHit world_hit = world_collision_.raycast(origin, dir, closest_t);
    if (world_hit.hit && world_hit.t < closest_t) {
        closest_t = world_hit.t;
        hit_point = world_hit.position;
    }

    // AI cars — occlude only, no damage in phase 1. The chassis box is in
    // local space; we use position ± half-extents as a generous AABB. A few
    // metres of imprecision at car-yaw extremes is fine for occlusion.
    for (const auto& car_ptr : traffic_.cars()) {
        const Vehicle& v = car_ptr->vehicle;
        const glm::vec3 half = v.chassis_full_extents * 0.5f;
        // Inflate xz by sqrt(2) so a car turned 45° still occludes its
        // full silhouette.
        constexpr float YAW_INFLATE = 1.4143f;
        const glm::vec3 box_min = v.position()
            - glm::vec3{half.x * YAW_INFLATE, half.y, half.z * YAW_INFLATE};
        const glm::vec3 box_max = v.position()
            + glm::vec3{half.x * YAW_INFLATE, half.y, half.z * YAW_INFLATE};
        float t;
        if (ray_vs_aabb(origin, dir, box_min, box_max, t) && t < closest_t) {
            closest_t = t;
            hit_point = origin + dir * t;
        }
    }

    // Pedestrians — closest one within current max range dies.
    auto ped_hit = pedestrians_.raycast(origin, dir, closest_t);
    if (ped_hit.hit) {
        hit_point = ped_hit.position;
        particles_.emit_sparks(ped_hit.position, glm::vec3{0.f, 2.f, 0.f}, 16);
        pedestrians_.kill(ped_hit.ped_idx);
    }

    audio_.play_gunshot();
    debug_draw_.line(origin + dir * 0.3f, hit_point);
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
        update_on_foot(fdt, mdx, mdy);
    } else /* InVehicle */ {
        update_in_vehicle(fdt, mdx, mdy);
    }

    // F-to-enter target: closest car (any driver) within ENTRY_RADIUS. The
    // car the player is approaching stays exactly where it is — F just
    // transfers the Player driver flag (see try_toggle_vehicle).
    steal_target_  = nullptr;
    can_enter_car_ = false;
    if (mode_ == Mode::OnFoot) {
        steal_target_ = traffic_.find_nearest(character_.feet_position(),
                                               ENTRY_RADIUS);
        can_enter_car_ = (steal_target_ != nullptr);
    }

    // Advance the walk-anim time accumulator (seconds). Mixamo loops are
    // 1 stride each, so we scale by current speed / reference speed of the
    // anim that's actually playing — keeps stride frequency tied to ground
    // speed regardless of which clip is sampled.
    // Idle: freeze on first frame instead of playing in place.
    bool char_moving;
    bool use_sprint;
    {
        glm::vec3 vh = character_.velocity();
        vh.y = 0.f;
        float speed = glm::length(vh);
        char_moving = speed > 0.5f;
        use_sprint  = sprinting_ && char_moving &&
                      character_anim_sprint_.duration() > 0.f;
        float ref = use_sprint ? CharacterController::SPRINT_SPEED
                               : CharacterController::MOVE_SPEED;
        if (char_moving) walk_phase_ += dt * double(speed / ref);
        else             walk_phase_  = 0.0;   // freeze at first frame
    }

    if (character_skinned_) {
        const int n = character_skeleton_.bone_count();
        char_local_poses_.resize(static_cast<std::size_t>(n));

        // Pick the anim driving the current frame. Walking and sprint loop
        // at speed-tied stride frequency; idle plays the breathing-idle
        // loop in real time. When armed, swap to the pistol-locomotion
        // variants — same phase / cadence semantics, just different rigs.
        // Each armed slot falls back to its unarmed counterpart if the
        // pistol anim failed to load.
        auto pick_armed = [this](const Animation& armed,
                                  const Animation& unarmed)
                            -> const Animation* {
            return (armed_ && armed.duration() > 0.f) ? &armed : &unarmed;
        };
        const Animation* anim_to_use = nullptr;
        float            phase       = 0.f;
        if (char_moving) {
            anim_to_use = use_sprint
                ? pick_armed(character_anim_pistol_run_,  character_anim_sprint_)
                : pick_armed(character_anim_pistol_walk_, character_anim_);
            phase       = static_cast<float>(walk_phase_);
        } else if (character_anim_idle_.duration() > 0.f) {
            anim_to_use = pick_armed(character_anim_pistol_idle_,
                                      character_anim_idle_);
            phase       = static_cast<float>(world_time_);
        }

        if (anim_to_use && anim_to_use->duration() > 0.f) {
            anim_to_use->sample(phase, character_skeleton_, char_local_poses_);
            // Strip root motion: gameplay drives world position, so the
            // root bone's translation must come from the bind pose, not
            // the anim.
            for (int b = 0; b < n; ++b) {
                if (character_skeleton_.bone(b).parent < 0) {
                    glm::vec3 bind_t{character_skeleton_.bone(b).bind_local[3]};
                    char_local_poses_[static_cast<std::size_t>(b)][3] =
                        glm::vec4{bind_t, 1.f};
                }
            }
        } else {
            // No anim available — fall back to bind pose.
            for (int b = 0; b < n; ++b)
                char_local_poses_[static_cast<std::size_t>(b)] =
                    character_skeleton_.bone(b).bind_local;
        }

        character_skeleton_.compute_skin_matrices(char_local_poses_,
                                                    char_skin_matrices_);
    }

    sync_character_scene();
    streamer_.pump(camera_.position);
    world_time_ += dt;
    // Single per-frame entry point: AI lane-follow (kinematic), player +
    // parked physics substeps, traffic-light state, and visual sync for
    // every car all happen here.
    traffic_.update(fdt, world_time_, camera_.position, world_collision_);
    pedestrians_.update(fdt, camera_.position);

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
            if (cp->driver != TrafficSystem::Driver::AI) continue;
            AudioEngine::TrafficSource ts;
            ts.id            = cp.get();
            ts.position      = cp->vehicle.position();
            ts.speed_kmh     = cp->ai_speed * 3.6f;
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
        pos = character_.feet_position();
        float yr = glm::radians(character_facing_yaw_deg_);
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

    // ---- Equipped weapon (static mesh attached to right-hand bone) --------
    // Recover the right-hand bone's world matrix from the skin matrices
    // we already computed above: skin = world_bone * inv_bind, so
    // world_bone = skin * inverse(inv_bind). We cached inverse(inv_bind)
    // at init since it doesn't change. Then transform up through the
    // visual node and apply the hand-tuned grip offset to align the
    // mesh origin with the palm.
    if (armed_ && character_skinned_ && right_hand_bone_idx_ >= 0
        && gun_mesh_.index_count() > 0
        && right_hand_bone_idx_ < static_cast<int>(char_skin_matrices_.size())) {
        glm::mat4 bone_world = char_skin_matrices_[
            static_cast<std::size_t>(right_hand_bone_idx_)]
                              * right_hand_bind_world_;
        // GUN_SCALE: with PreTransformVertices on, the FBX root node's
        // unit-scale (often 100 for Blender's cm→m export) gets baked
        // into vertex positions, making the gun much bigger than its
        // pre-transform size. Drop the scale until it sits right.
        constexpr float GUN_SCALE = 0.3f;
        glm::mat4 scale_mat = glm::scale(glm::mat4{1.f},
                                           glm::vec3{GUN_SCALE});
        glm::mat4 gun_world = character_visual_node_->world_matrix()
                            * bone_world * scale_mat * gun_grip_offset_;
        lit_shader_.use();
        lit_shader_.set("u_view_proj",   vp);
        lit_shader_.set("u_cam_pos",     camera_.position);
        lit_shader_.set("u_light_dir",
                         glm::normalize(glm::vec3{0.6f, 1.f, 0.4f}));
        lit_shader_.set("u_light_color", glm::vec3{1.f, 0.95f, 0.85f});
        lit_shader_.set("u_ambient",     glm::vec3{0.18f, 0.22f, 0.28f});
        lit_shader_.set("u_diffuse",     0);
        lit_shader_.set("u_model",       gun_world);
        lit_shader_.set("u_normal_mat",
                         glm::mat3(glm::inverseTranspose(gun_world)));
        checker_tex_.bind(0);
        gun_mesh_.draw();
    }

    // ---- AI pedestrians (skinned, one draw per ped) ------------------------
    pedestrians_.render(skinned_shader_, vp, camera_.position,
                        checker_tex_, frustum);

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

    // ---- Minimap (HUD overlay) ---------------------------------------------
    {
        Minimap::DrawState ms;
        ms.viewport_size_px = {static_cast<float>(window_.width()),
                                static_cast<float>(window_.height())};
        if (mode_ == Mode::InVehicle) {
            if (auto* pc = traffic_.player_car()) {
                ms.player_pos_world = pc->vehicle.position();
                glm::vec3 fwd = pc->vehicle.orientation() * glm::vec3{0.f, 0.f, -1.f};
                ms.player_yaw_deg = glm::degrees(std::atan2(fwd.x, -fwd.z));
            } else {
                ms.player_pos_world = camera_.position;
                ms.player_yaw_deg   = 0.f;
            }
        } else {
            ms.player_pos_world = character_.feet_position();
            ms.player_yaw_deg   = character_facing_yaw_deg_ + 90.f;
        }
        minimap_.draw(ms);
    }

    if (mode_ == Mode::InVehicle) {
        if (auto* pc = traffic_.player_car()) {
            Speedometer::DrawState ss;
            ss.speed_kmh = pc->vehicle.speed_kmh();
            ss.viewport_size_px = {static_cast<float>(window_.width()),
                                   static_cast<float>(window_.height())};
            speedometer_.draw(ss);
        }
    }

    if (armed_ && mode_ == Mode::OnFoot) {
        Crosshair::DrawState cs;
        cs.viewport_size_px = {static_cast<float>(window_.width()),
                               static_cast<float>(window_.height())};
        crosshair_.draw(cs);
    }

    window_.swap();
}

} // namespace pengine
