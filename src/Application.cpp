#include "Application.h"

#include <SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_set>
#include <vector>

#include "core/log.h"
#include "game/police_system.h"
#include "game/vehicle_effects.h"
#include "render/debug_overlay.h"
#include "render/mesh.h"
#include "scene/frustum.h"
#include "scene/scene_node.h"
#include "world/heightmap.h"
#include "world/model_def.h"
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
    if (!menu_.init(ASSETS_DIR)) return false;
    if (!text_.init(ASSETS_DIR)) return false;

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

        if (app_state_ == AppState::Playing) {
            process_events();
            auto tick = clock_.advance();
            for (int i = 0; i < tick.updates; ++i)
                update(clock_.fixed_dt);
            render(tick.alpha);
        } else if (app_state_ == AppState::MapBuilder) {
            // Map Builder is a "world view with no simulation" state: gameplay
            // doesn't tick (traffic / peds frozen at init pose) but we *do*
            // drive the streamer with the editor camera, so cells page in/out
            // as the user pans. The fixed-timestep clock is drained so
            // resuming gameplay doesn't catch up on map-builder time; pan/zoom
            // use a real wall-clock dt instead.
            process_map_builder_events();
            (void)clock_.advance();
            TimePoint now = Clock::now();
            float dt = std::chrono::duration<float>(now - map_last_frame_).count();
            map_last_frame_ = now;
            if (dt > 0.1f) dt = 0.1f; // clamp big stalls (alt-tab etc.)
            if (dt < 0.f)  dt = 0.f;
            update_map_builder(dt);
            render_map_builder();
        } else {
            // Menu state: no gameplay simulation, no 3D scene render. We
            // still drain the fixed-timestep clock so that resuming gameplay
            // doesn't try to catch up on time spent in the menu.
            process_menu_events();
            (void)clock_.advance();
            render_menu();
        }

        double frame_ms =
            std::chrono::duration<double, std::milli>(Clock::now() - frame_start).count();
        if (frame_ms > max_frame_ms_) max_frame_ms_ = frame_ms;

        ++fps_frames_;
        if (seconds_since(stats_start_) >= 1.0) {
            if (app_state_ != AppState::Playing) {
                char title[200];
                if (app_state_ == AppState::MapBuilder) {
                    // Include cam world position + altitude + loaded cell
                    // count for debugging pan/stream behaviour (PBD-024).
                    auto st = streamer_.stats();
                    std::snprintf(title, sizeof(title),
                                  "pengine | map builder | fps:%d"
                                  " | cam x=%.0f z=%.0f alt=%.0f | cells:%d",
                                  fps_frames_,
                                  map_cam_pos_.x, map_cam_pos_.z, map_cam_pos_.y,
                                  st.loaded_cells);
                } else {
                    const char* label = (app_state_ == AppState::MainMenu)
                                      ? "main menu" : "dev tools";
                    std::snprintf(title, sizeof(title),
                                  "pengine | %s | fps:%d",
                                  label, fps_frames_);
                }
                SDL_SetWindowTitle(window_.sdl(), title);
                fps_frames_   = 0;
                max_frame_ms_ = 0.0;
                stats_start_  = Clock::now();
                continue;
            }
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
    menu_.shutdown();
    text_.shutdown();
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

// ---------------------------------------------------------------------------
// Menu state (PBD-016)
// ---------------------------------------------------------------------------
//
// The main menu is the first interactive screen on launch. Gameplay
// subsystems are still initialised at startup (the world streams, the
// streamer warms, etc.) but the per-frame simulation is paused and the 3D
// scene is not drawn while we're in a menu state. "New Game" transitions to
// Playing; "Dev Tools" drops into a submenu whose only entry (Map Builder)
// is a placeholder.

namespace {

constexpr const char* MAIN_ITEMS[] = {
    "New Game",
    "Dev Tools",
};
constexpr int MAIN_ITEM_COUNT =
    static_cast<int>(sizeof(MAIN_ITEMS) / sizeof(MAIN_ITEMS[0]));

constexpr const char* DEVTOOLS_ITEMS[] = {
    "Map Builder",
    "Back",
};
constexpr int DEVTOOLS_ITEM_COUNT =
    static_cast<int>(sizeof(DEVTOOLS_ITEMS) / sizeof(DEVTOOLS_ITEMS[0]));

} // namespace

void Application::enter_app_state(AppState s) {
    if (app_state_ == s) return;
    // PBD-027: ensure the SDL text-input session is closed if we were mid-
    // typing when the state changed (e.g. the user hit Backspace/B to leave
    // MapBuilder while the prompt was open). Otherwise SDL would keep
    // synthesising TEXTINPUT events into the next state's pump.
    if (map_input_active_) {
        SDL_StopTextInput();
        map_input_active_ = false;
        map_input_buf_.clear();
    }
    app_state_      = s;
    menu_selection_ = 0;

    if (s == AppState::Playing) {
        // Reset the fixed-timestep accumulator so we don't catch up on time
        // spent in the menu.
        clock_.accumulator = 0.0;
        clock_.last        = Clock::now();
    } else {
        // Release the mouse if it was captured by gameplay.
        if (mouse_captured_) {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            mouse_captured_ = false;
        }
    }

    if (s == AppState::MapBuilder) {
        // Default: hover over the world centre at altitude high enough to
        // show several cells. WorldConfig::cell_size×world_cells gives the
        // world extent; centre is (cells*size/2). Altitude 600m with the
        // default 60° FOV shows ~700m of width on a 16:9 viewport — i.e. a
        // ~3×3 chunk of 256m cells, which is the streamer's load radius.
        WorldConfig wc;
        float cx = (static_cast<float>(wc.world_cells_x) * wc.cell_size) * 0.5f;
        float cz = (static_cast<float>(wc.world_cells_z) * wc.cell_size) * 0.5f;
        map_cam_pos_    = glm::vec3{cx, 600.f, cz};
        map_last_frame_ = Clock::now();
        // PBD-030: start with the first palette entry highlighted. Selection
        // is not preserved across MapBuilder enter/exit — until plopping lands
        // (PBD-031) there's no behaviour to preserve, and resetting keeps the
        // state machine simple.
        map_palette_selection_ = 0;
    }
}

void Application::activate_menu_selection() {
    if (app_state_ == AppState::MainMenu) {
        if (menu_selection_ == 0) {
            // New Game -> enter gameplay. The engine has already initialised
            // the world; we just start ticking it.
            enter_app_state(AppState::Playing);
        } else if (menu_selection_ == 1) {
            enter_app_state(AppState::DevToolsMenu);
        }
    } else if (app_state_ == AppState::DevToolsMenu) {
        if (menu_selection_ == 0) {
            // Map Builder: EPIC-001 / PBD-023 scaffold. The state is empty
            // for now — PBD-024 onward will furnish it (world render,
            // top-down camera, picking, etc.).
            enter_app_state(AppState::MapBuilder);
        } else if (menu_selection_ == 1) {
            enter_app_state(AppState::MainMenu);
        }
    }
}

void Application::process_menu_events() {
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
            default:
                input_.handle_event(e);
                break;
        }
    }

    if (input_.down(SDL_SCANCODE_LCTRL) && input_.pressed(SDL_SCANCODE_Q))
        running_ = false;

    // List navigation: MainMenu / DevToolsMenu only. MapBuilder has its own
    // event pump (process_map_builder_events) — this function is only invoked
    // for the two list-menu states.
    const int item_count = (app_state_ == AppState::MainMenu)
                         ? MAIN_ITEM_COUNT
                         : DEVTOOLS_ITEM_COUNT;

    if (input_.pressed(SDL_SCANCODE_UP) || input_.pressed(SDL_SCANCODE_W)) {
        menu_selection_ = (menu_selection_ - 1 + item_count) % item_count;
    }
    if (input_.pressed(SDL_SCANCODE_DOWN) || input_.pressed(SDL_SCANCODE_S)) {
        menu_selection_ = (menu_selection_ + 1) % item_count;
    }
    if (input_.pressed(SDL_SCANCODE_RETURN) ||
        input_.pressed(SDL_SCANCODE_KP_ENTER) ||
        input_.pressed(SDL_SCANCODE_SPACE)) {
        activate_menu_selection();
    }

    if (input_.pressed(SDL_SCANCODE_ESCAPE) ||
        input_.pressed(SDL_SCANCODE_BACKSPACE) ||
        input_.pressed(SDL_SCANCODE_B)) {
        if (app_state_ == AppState::DevToolsMenu) {
            enter_app_state(AppState::MainMenu);
        }
        // Esc on the main menu is a no-op (no parent to back out to). Ctrl+Q
        // remains the quit shortcut.
    }
}

// MapBuilder event pump — split from process_menu_events (PBD-024) now that
// the state has real input (WASD pan + wheel zoom). The split was flagged in
// the PBD-023 follow-up as the right refactor when MapBuilder grew input;
// this is that moment. Keeps the menu pump small and lets the map-builder
// path own its own keybindings without per-event app_state_ branches.
//
// PBD-027 adds a sub-mode: when `map_input_active_` is true, keystrokes are
// captured as text into `map_input_buf_` (via SDL_TEXTINPUT events) instead of
// being routed through the normal Input keymap. Enter commits the typed cell
// coord and recentres the camera; Esc cancels. SDL_StartTextInput is required
// to actually receive SDL_TEXTINPUT events on most platforms; it also enables
// IME if the user has one configured (we ignore that — only ASCII digits and
// commas are meaningful here, anything else is dropped during the commit).
//
// PBD-027 is the first use of SDL_TEXTINPUT in this codebase; if a second
// text-input UI shows up we should hoist the buffer + StartTextInput plumbing
// onto Input. For now, one local string + a bool is enough.
void Application::process_map_builder_events() {
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
            case SDL_TEXTINPUT:
                if (map_input_active_) {
                    // SDL hands us a UTF-8 string; we only accept ASCII digits,
                    // commas, and spaces. Drop everything else silently. The
                    // 16-char cap is far more than "NN,NN" needs and keeps the
                    // buffer bounded against pathological IME paste.
                    for (const char* p = e.text.text; *p; ++p) {
                        const char c = *p;
                        const bool ok = (c >= '0' && c <= '9') ||
                                         c == ',' || c == ' ';
                        if (ok && map_input_buf_.size() < 16) {
                            map_input_buf_.push_back(c);
                        }
                    }
                } else {
                    // Don't route into Input — text events have no scancode
                    // semantics. The KEYDOWN events that accompany them are
                    // what Input cares about, and those are handled below.
                    input_.handle_event(e);
                }
                break;
            case SDL_KEYDOWN:
                if (map_input_active_) {
                    // Backspace handling has to live here because SDL doesn't
                    // emit a SDL_TEXTINPUT for it.
                    if (e.key.keysym.sym == SDLK_BACKSPACE &&
                        !map_input_buf_.empty()) {
                        map_input_buf_.pop_back();
                    }
                    // Enter/Esc are handled below via input_.pressed() so the
                    // existing "pressed this frame" model keeps working.
                    input_.handle_event(e);
                } else {
                    input_.handle_event(e);
                }
                break;
            default:
                input_.handle_event(e);
                break;
        }
    }

    if (input_.down(SDL_SCANCODE_LCTRL) && input_.pressed(SDL_SCANCODE_Q))
        running_ = false;

    if (map_input_active_) {
        // While in text-input mode: only Enter/Esc do anything. WASD/zoom are
        // suppressed (handled by update_map_builder reading map_input_active_).
        if (input_.pressed(SDL_SCANCODE_RETURN) ||
            input_.pressed(SDL_SCANCODE_KP_ENTER)) {
            // Parse loosely. strtol skips leading whitespace; we accept any
            // run of "<digits>[ws]*,[ws]*<digits>". Anything else flashes the
            // error indicator and leaves the camera where it was.
            const char* s = map_input_buf_.c_str();
            char* end1 = nullptr;
            long  x = std::strtol(s, &end1, 10);
            bool  ok = (end1 != s);
            long  z = 0;
            if (ok) {
                while (*end1 == ' ') ++end1;
                if (*end1 != ',') ok = false;
                else {
                    ++end1;
                    char* end2 = nullptr;
                    z = std::strtol(end1, &end2, 10);
                    if (end2 == end1) ok = false;
                }
            }
            if (ok) {
                WorldConfig wc;
                // Clamp to valid cell range. Out-of-range inputs still jump —
                // they just jump to the edge — and we flash the indicator so
                // the user knows the value was massaged.
                long cx_l = std::clamp<long>(x, 0, wc.world_cells_x - 1);
                long cz_l = std::clamp<long>(z, 0, wc.world_cells_z - 1);
                bool clamped = (cx_l != x || cz_l != z);

                // Re-centre on the cell's centre, not its corner. Altitude is
                // preserved so the user keeps the same zoom across jumps.
                map_cam_pos_.x = (static_cast<float>(cx_l) + 0.5f) * wc.cell_size;
                map_cam_pos_.z = (static_cast<float>(cz_l) + 0.5f) * wc.cell_size;
                if (clamped) map_input_err_flash_s_ = 0.75f;
                PE_INFO("Map Builder: jump to cell (%ld, %ld)%s",
                        cx_l, cz_l, clamped ? " [clamped]" : "");
            } else {
                map_input_err_flash_s_ = 0.75f;
                PE_INFO("Map Builder: unparseable cell input '%s'",
                        map_input_buf_.c_str());
            }
            map_input_buf_.clear();
            map_input_active_ = false;
            SDL_StopTextInput();
        } else if (input_.pressed(SDL_SCANCODE_ESCAPE)) {
            map_input_buf_.clear();
            map_input_active_ = false;
            SDL_StopTextInput();
        }
        return;
    }

    // ----- Normal mode -----
    if (input_.pressed(SDL_SCANCODE_G)) {
        // Enter cell-jump input mode. SDL_StartTextInput is required to
        // actually receive SDL_TEXTINPUT events on most platforms; first use
        // in this codebase.
        map_input_active_ = true;
        map_input_buf_.clear();
        SDL_StartTextInput();
        return; // suppress any other key that fired this frame
    }

    if (input_.pressed(SDL_SCANCODE_ESCAPE) ||
        input_.pressed(SDL_SCANCODE_BACKSPACE) ||
        input_.pressed(SDL_SCANCODE_B)) {
        enter_app_state(AppState::DevToolsMenu);
    }

    // PBD-030: asset palette navigation. Up/Down only — WASD is bound to the
    // camera pan in update_map_builder, and binding W/S to the palette would
    // fight that. Arrow keys wrap at the ends. No-op when the palette is
    // empty (defensive; the registry has 8 entries today, but bailing here
    // means we never divide by zero on the wrap).
    const int palette_count = static_cast<int>(world_models_.size());
    if (palette_count > 0) {
        if (input_.pressed(SDL_SCANCODE_UP)) {
            map_palette_selection_ =
                (map_palette_selection_ - 1 + palette_count) % palette_count;
        }
        if (input_.pressed(SDL_SCANCODE_DOWN)) {
            map_palette_selection_ =
                (map_palette_selection_ + 1) % palette_count;
        }
    }
}

void Application::render_menu() {
    // MainMenu / DevToolsMenu only — MapBuilder has its own render path
    // (render_map_builder) that draws the streamed world under a top-down
    // camera. PBD-024 split MapBuilder out of this widget when it grew real
    // input + a world view.
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    Menu::DrawState ms;
    ms.viewport_size_px = {static_cast<float>(window_.width()),
                            static_cast<float>(window_.height())};
    ms.selected_index   = menu_selection_;

    if (app_state_ == AppState::MainMenu) {
        ms.title      = "BULLDOG";
        ms.items      = MAIN_ITEMS;
        ms.item_count = MAIN_ITEM_COUNT;
        ms.footer     = "UP DOWN MOVE   ENTER SELECT   CTRL Q QUIT";
    } else /* DevToolsMenu */ {
        ms.title      = "DEV TOOLS";
        ms.items      = DEVTOOLS_ITEMS;
        ms.item_count = DEVTOOLS_ITEM_COUNT;
        ms.footer     = "ESC BACK   ENTER SELECT";
    }

    menu_.draw(ms);

    window_.swap();
}

// ---------------------------------------------------------------------------
// Map Builder (PBD-024)
// ---------------------------------------------------------------------------
//
// Top-down free-pan camera. Per the architect: not a new orthographic
// projection — instead reuse the existing perspective Camera with a steep
// pitch (-89°). Yaw is fixed so WASD always pans in world axes. Mouse wheel
// adjusts altitude. The resulting camera position drives Streamer::pump each
// frame, so cells page in/out as the user explores.
//
// Out of scope for this ticket: overlays, picking, cell jump. Those are
// PBD-025/026/027 and hang off this same render/event pair.

void Application::update_map_builder(float dt) {
    // Decay the cell-jump error flash regardless of input mode so it always
    // disappears after ~0.75s.
    if (map_input_err_flash_s_ > 0.f) {
        map_input_err_flash_s_ -= dt;
        if (map_input_err_flash_s_ < 0.f) map_input_err_flash_s_ = 0.f;
    }

    // PBD-027: while a cell-jump input is active, suppress pan/zoom so typing
    // doesn't double up (W/S/A/D, wheel) with normal camera control. The
    // streamer still pumps below so cells stay loaded around the (stationary)
    // camera.
    if (map_input_active_) {
        streamer_.pump(map_cam_pos_);
        scene_.update();
        return;
    }

    // ----- Zoom (wheel adjusts altitude) -----
    // Multiplicative step keeps zoom feel consistent at any altitude — a tick
    // takes you ~10% of the way toward/away from the ground.
    float wy = input_.wheel_y();
    if (wy != 0.f) {
        float factor = std::pow(1.f - MAP_CAM_WHEEL_STEP, wy);
        map_cam_pos_.y *= factor;
        if (map_cam_pos_.y < MAP_CAM_ALT_MIN) map_cam_pos_.y = MAP_CAM_ALT_MIN;
        if (map_cam_pos_.y > MAP_CAM_ALT_MAX) map_cam_pos_.y = MAP_CAM_ALT_MAX;
    }

    // ----- Pan (WASD on the XZ plane) -----
    // Yaw is fixed at -90 (camera "looks along -Z"), so we hard-code the
    // ground-plane basis rather than reading Camera::forward(): forward = -Z,
    // right = +X. This makes W push north (-Z) and D push east (+X) regardless
    // of any latent yaw state on camera_.
    glm::vec3 fwd_xz{0.f, 0.f, -1.f};
    glm::vec3 rgt_xz{1.f, 0.f,  0.f};

    glm::vec3 vel{0.f};
    if (input_.down(SDL_SCANCODE_W)) vel += fwd_xz;
    if (input_.down(SDL_SCANCODE_S)) vel -= fwd_xz;
    if (input_.down(SDL_SCANCODE_D)) vel += rgt_xz;
    if (input_.down(SDL_SCANCODE_A)) vel -= rgt_xz;

    if (glm::length(vel) > 0.f) {
        // Scale pan speed with altitude so the screen-space pan rate feels
        // constant at any zoom. ~1.2 × altitude per second is roughly "edge
        // of screen in <1s" with the default 60° FOV.
        float speed = 1.2f * map_cam_pos_.y;
        map_cam_pos_ += glm::normalize(vel) * speed * dt;
    }

    // Clamp XZ to world bounds so we don't wander into never-loadable space.
    WorldConfig wc;
    float world_w = static_cast<float>(wc.world_cells_x) * wc.cell_size;
    float world_d = static_cast<float>(wc.world_cells_z) * wc.cell_size;
    if (map_cam_pos_.x < 0.f)      map_cam_pos_.x = 0.f;
    if (map_cam_pos_.x > world_w)  map_cam_pos_.x = world_w;
    if (map_cam_pos_.z < 0.f)      map_cam_pos_.z = 0.f;
    if (map_cam_pos_.z > world_d)  map_cam_pos_.z = world_d;

    // Drive the streamer from the camera position. Cell load/evict is keyed
    // on (cam.x, cam.z) by Streamer; we still pass the full vec3 so future
    // distance-based culling sees the right altitude.
    streamer_.pump(map_cam_pos_);

    // Flush pending scene additions from streamer loads.
    scene_.update();
}

// PBD-025: Map Builder cell/road overlay. Draws four channels on top of the
// rendered world using DebugDraw (depth-test disabled so lines sit cleanly on
// the terrain regardless of building occlusion):
//   - loaded cell boundary squares  (bright cyan)
//   - unloaded cell boundary squares in the visible window (dim grey)
//   - road centerlines on the NS/EW grid (orange)
//   - intersection markers (bright yellow crosses)
//
// Each channel is a separate clear/submit/flush pass because DebugDraw::flush
// applies a single color to all queued lines. That's ~four GL draw calls per
// frame for the overlay; fine for our world size (32×32 cells, typically <100
// in the visible window).
namespace {

// Approximate XZ half-size of the visible ground region under the top-down
// camera, given altitude and vertical FOV. Used to clip overlay generation
// to roughly what's on screen plus a small margin. We use a generous margin
// (1.25×) so partial cells at the edge still get drawn.
float visible_xz_half(float altitude_m, float fov_y_deg, float aspect) {
    float fov_y_rad = glm::radians(fov_y_deg);
    float half_z    = altitude_m * std::tan(fov_y_rad * 0.5f);
    float half_x    = half_z * aspect;
    return 1.25f * std::max(half_x, half_z);
}

// Submit the 4 edge segments of a cell boundary square at (cx, cz) into
// debug_draw. Y is sampled at each corner from the heightmap so the square
// hugs the terrain rather than floating above it.
void submit_cell_square(DebugDraw& dd, CellCoord c, float cell_size,
                        float y_lift) {
    float x0 = c.x * cell_size;
    float x1 = x0 + cell_size;
    float z0 = c.z * cell_size;
    float z1 = z0 + cell_size;

    auto p = [&](float x, float z) {
        return glm::vec3{x, Heightmap::sample(x, z) + y_lift, z};
    };

    glm::vec3 a = p(x0, z0), b = p(x1, z0), d = p(x1, z1), e = p(x0, z1);
    dd.line(a, b);
    dd.line(b, d);
    dd.line(d, e);
    dd.line(e, a);
}

} // namespace

void Application::render_map_builder() {
    glClearColor(0.45f, 0.65f, 0.85f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Pose the camera. Position is the editor cam; yaw is fixed; pitch is
    // near-straight-down. Camera::forward() with yaw=-90, pitch=-89 yields
    // ~(0, -1, -0.017) — essentially straight down with a hair of -Z bias to
    // avoid lookAt's degenerate up=forward singularity.
    camera_.position = map_cam_pos_;
    camera_.yaw      = MAP_CAM_YAW_DEG;
    camera_.pitch    = MAP_CAM_PITCH_DEG;

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

    // ---- Overlay (PBD-025) -------------------------------------------------
    {
        WorldConfig wc;
        const float cell_size = wc.cell_size;
        const float y_lift    = 0.5f;  // lift overlay just above terrain

        // Visible XZ window in world units, clamped to world bounds. Y in
        // map_cam_pos_ is altitude; camera_ FOV is the default (60° per
        // Camera). If Camera::fov_y_deg changes, this still works because
        // the margin (1.25×) absorbs small mismatches.
        const float half = visible_xz_half(map_cam_pos_.y, 60.f, aspect);
        const float wx0  = map_cam_pos_.x - half;
        const float wx1  = map_cam_pos_.x + half;
        const float wz0  = map_cam_pos_.z - half;
        const float wz1  = map_cam_pos_.z + half;

        // ---- Channel 1: unloaded cells in the visible window (dim grey) ----
        // Computed by enumerating cell indices crossing the window and
        // excluding loaded ones. Has to come before loaded so the loaded
        // squares paint on top if any overlap (they shouldn't, but cheap
        // insurance).
        std::vector<CellCoord> loaded = road_graph_.loaded_cells();
        std::unordered_set<CellCoord, CellCoordHash> loaded_set(
            loaded.begin(), loaded.end());

        int ci0 = std::max(0, static_cast<int>(std::floor(wx0 / cell_size)));
        int ci1 = std::min(wc.world_cells_x - 1,
                            static_cast<int>(std::floor(wx1 / cell_size)));
        int cj0 = std::max(0, static_cast<int>(std::floor(wz0 / cell_size)));
        int cj1 = std::min(wc.world_cells_z - 1,
                            static_cast<int>(std::floor(wz1 / cell_size)));

        debug_draw_.clear();
        for (int cj = cj0; cj <= cj1; ++cj) {
            for (int ci = ci0; ci <= ci1; ++ci) {
                CellCoord c{ci, cj};
                if (loaded_set.count(c)) continue;
                submit_cell_square(debug_draw_, c, cell_size, y_lift);
            }
        }
        debug_draw_.flush(vp, glm::vec3{0.35f, 0.35f, 0.40f});

        // ---- Channel 2: loaded cells (bright cyan) -------------------------
        debug_draw_.clear();
        for (CellCoord c : loaded) {
            // Skip cells fully outside the visible window — cheap reject by
            // overlap of cell box with [wx0,wx1]×[wz0,wz1].
            float x0 = c.x * cell_size, x1 = x0 + cell_size;
            float z0 = c.z * cell_size, z1 = z0 + cell_size;
            if (x1 < wx0 || x0 > wx1 || z1 < wz0 || z0 > wz1) continue;
            submit_cell_square(debug_draw_, c, cell_size, y_lift);
        }
        debug_draw_.flush(vp, glm::vec3{0.15f, 1.f, 0.95f});

        // ---- Channel 3: road centerlines (orange) --------------------------
        // NS roads at x = i * ROAD_PITCH, EW roads at z = j * ROAD_PITCH.
        // Draw each segment as a polyline of short pieces so it follows
        // the terrain — but at typical Map Builder altitudes the carved
        // road plateau is essentially flat, so coarse sampling is fine.
        debug_draw_.clear();
        const float seg_len = 8.f;  // 8m sample spacing along centerline

        int i0 = static_cast<int>(std::floor(wx0 / ROAD_PITCH));
        int i1 = static_cast<int>(std::ceil (wx1 / ROAD_PITCH));
        int j0 = static_cast<int>(std::floor(wz0 / ROAD_PITCH));
        int j1 = static_cast<int>(std::ceil (wz1 / ROAD_PITCH));

        auto sample_road_pt = [&](float x, float z) {
            return glm::vec3{x, Heightmap::sample(x, z) + y_lift, z};
        };

        // NS roads (parallel to Z axis at fixed x).
        for (int i = i0; i <= i1; ++i) {
            float x = static_cast<float>(i) * ROAD_PITCH;
            float zA = std::max(wz0, 0.f);
            float zB = std::min(wz1, static_cast<float>(wc.world_cells_z) * cell_size);
            if (zA >= zB) continue;
            glm::vec3 prev = sample_road_pt(x, zA);
            for (float z = zA + seg_len; z < zB; z += seg_len) {
                glm::vec3 cur = sample_road_pt(x, z);
                debug_draw_.line(prev, cur);
                prev = cur;
            }
            debug_draw_.line(prev, sample_road_pt(x, zB));
        }
        // EW roads (parallel to X axis at fixed z).
        for (int j = j0; j <= j1; ++j) {
            float z = static_cast<float>(j) * ROAD_PITCH;
            float xA = std::max(wx0, 0.f);
            float xB = std::min(wx1, static_cast<float>(wc.world_cells_x) * cell_size);
            if (xA >= xB) continue;
            glm::vec3 prev = sample_road_pt(xA, z);
            for (float x = xA + seg_len; x < xB; x += seg_len) {
                glm::vec3 cur = sample_road_pt(x, z);
                debug_draw_.line(prev, cur);
                prev = cur;
            }
            debug_draw_.line(prev, sample_road_pt(xB, z));
        }
        debug_draw_.flush(vp, glm::vec3{1.f, 0.55f, 0.10f});

        // ---- Channel 4: loaded intersection markers (bright yellow) --------
        debug_draw_.clear();
        // Size scales gently with altitude so the markers stay visible when
        // zoomed out without overwhelming the view when zoomed in.
        float marker = std::clamp(map_cam_pos_.y * 0.025f, 2.f, 12.f);
        for (auto [i, j] : road_graph_.loaded_intersections()) {
            float x = static_cast<float>(i) * ROAD_PITCH;
            float z = static_cast<float>(j) * ROAD_PITCH;
            if (x < wx0 || x > wx1 || z < wz0 || z > wz1) continue;
            glm::vec3 p{x, Heightmap::sample(x, z) + y_lift, z};
            debug_draw_.cross(p, marker);
        }
        debug_draw_.flush(vp, glm::vec3{1.f, 1.f, 0.15f});
    }

    // ---- Inspector (PBD-026) -----------------------------------------------
    // Screen-centre cursor: under the top-down camera (pitch ~ -89°), a ray
    // through screen-centre hits world XZ essentially at the camera's own XZ,
    // so we skip the unproject and just pick at (cam.x, cam.z). Mouse-position
    // picking is deferred — screen-centre is enough for v1 (see PBD-026
    // ticket scope) and reads naturally as "what is dead-centre of the view."
    {
        const float wx = map_cam_pos_.x;
        const float wz = map_cam_pos_.z;
        Streamer::PickResult pick = streamer_.query_instance_at(wx, wz);

        // Centre crosshair — always drawn so the user can see what they're
        // aiming at, even when the pick misses (e.g. cursor over terrain or
        // an unloaded cell). Sized to read against both bright and dim
        // overlays.
        debug_draw_.clear();
        {
            const float y_lift   = 1.0f;
            const float cs       = std::clamp(map_cam_pos_.y * 0.012f, 1.5f, 6.f);
            const float y        = Heightmap::sample(wx, wz) + y_lift;
            debug_draw_.line({wx - cs, y, wz}, {wx + cs, y, wz});
            debug_draw_.line({wx, y, wz - cs}, {wx, y, wz + cs});
        }
        debug_draw_.flush(vp, glm::vec3{1.f, 1.f, 1.f});

        // Format the readout. C-style strings owned by a local buffer pool;
        // pointers fed to Menu::draw_text_lines outlive that single call.
        constexpr int   MAX_LINES = 8;
        char            line_buf[MAX_LINES][96];
        const char*     lines[MAX_LINES] = {};
        int             nlines = 0;
        glm::vec3       text_col;

        if (pick.hit) {
            const ModelDef* mdef = world_models_.get(pick.instance.model_id);
            const char* name = (mdef && !mdef->name.empty()) ? mdef->name.c_str()
                                                              : "(unknown)";
            const auto& p = pick.instance.transform.position;
            const auto& s = pick.instance.transform.scale;
            const uint32_t lod = pick.instance.lod_pair;

            std::snprintf(line_buf[0], sizeof(line_buf[0]),
                          "INSPECTOR");
            std::snprintf(line_buf[1], sizeof(line_buf[1]),
                          "ID  %u", pick.instance.model_id);
            std::snprintf(line_buf[2], sizeof(line_buf[2]),
                          "NAME  %s", name);
            std::snprintf(line_buf[3], sizeof(line_buf[3]),
                          "POS  (%.1f, %.1f, %.1f)", p.x, p.y, p.z);
            std::snprintf(line_buf[4], sizeof(line_buf[4]),
                          "SCL  (%.2f, %.2f, %.2f)", s.x, s.y, s.z);
            std::snprintf(line_buf[5], sizeof(line_buf[5]),
                          "CELL  (%d, %d)", pick.cell.x, pick.cell.z);
            // IPL flags: the per-instance IPL has no flags field today, only
            // an optional lod pair. Surface the lod_pair when set (0xFFFFFFFF
            // is the "unset" sentinel — round-tripped IPLs always read as
            // unset because save_ipl doesn't yet write it; tracked as a
            // follow-up). Also include the model-level flags from the
            // registry — those are what tooling actually wants when asking
            // "is this a building/road/walk."
            const char* mflags = mdef ? format_model_flags(mdef->flags) : "?";
            if (lod == 0xFFFFFFFFu) {
                std::snprintf(line_buf[6], sizeof(line_buf[6]),
                              "FLAGS  %s   LOD  NONE", mflags);
            } else {
                std::snprintf(line_buf[6], sizeof(line_buf[6]),
                              "FLAGS  %s   LOD  %u", mflags, lod);
            }
            for (int i = 0; i < 7; ++i) lines[i] = line_buf[i];
            nlines = 7;
            text_col = glm::vec3{1.0f, 0.95f, 0.55f};

            // Outline the picked AABB so the user has visual confirmation
            // the cursor and the readout are talking about the same thing.
            debug_draw_.clear();
            debug_draw_.box(pick.world_aabb.min, pick.world_aabb.max);
            debug_draw_.flush(vp, glm::vec3{1.f, 0.3f, 1.f});
        } else {
            std::snprintf(line_buf[0], sizeof(line_buf[0]), "INSPECTOR");
            std::snprintf(line_buf[1], sizeof(line_buf[1]), "NO SELECTION");
            lines[0] = line_buf[0];
            lines[1] = line_buf[1];
            nlines   = 2;
            text_col = glm::vec3{0.92f, 0.94f, 0.98f};
        }

        Text::DrawState tl;
        tl.lines              = lines;
        tl.count              = nlines;
        // Bottom-left, above the footer band. Glyph 26 / row stride ~36;
        // footer below sits at vh-80 with 28px glyphs.
        tl.origin_top_left_px = {32.f,
                                  static_cast<float>(window_.height()) -
                                      80.f - 32.f -
                                      static_cast<float>(nlines) * 36.f};
        tl.glyph_h_px         = 26.f;
        tl.color              = text_col;
        tl.bg_color           = glm::vec4{0.05f, 0.05f, 0.07f, 0.70f};
        tl.bg_padding_px      = 14.f;
        tl.viewport_size_px   = {static_cast<float>(window_.width()),
                                  static_cast<float>(window_.height())};
        text_.draw_lines(tl);
    }

    // Footer hint, rendered via the small text-helper rather than the full
    // Menu widget. Menu::draw paints a full-screen background quad (intended
    // for actual menu screens with no scene behind them) which would clobber
    // the world view here; draw_text_lines doesn't, so the footer text sits
    // cleanly over the rendered scene. (PBD-025 used menu_.draw here, which
    // is the latent bug — see PBD-026 report.)
    {
        // PBD-027: footer swaps when a cell-jump input is active. In normal
        // mode it advertises the G bind; in input mode it shows the echoed
        // buffer with a trailing underscore as a crude cursor.
        char input_line[64];
        const char* footer;
        if (map_input_active_) {
            std::snprintf(input_line, sizeof(input_line),
                          "GO TO CELL: %s_   ENTER OK   ESC CANCEL",
                          map_input_buf_.c_str());
            footer = input_line;
        } else if (map_input_err_flash_s_ > 0.f) {
            footer = "BAD CELL COORD";
        } else {
            footer = "WASD PAN   WHEEL ZOOM   G GO TO CELL   ESC BACK";
        }
        const char* footer_lines[] = {footer};
        Text::DrawState fl;
        fl.lines              = footer_lines;
        fl.count              = 1;
        // Centre on viewport using Text's actual measured width, at vh - 80.
        const float vw = static_cast<float>(window_.width());
        const float vh = static_cast<float>(window_.height());
        const float glyph_h = 28.f;
        const float text_w  = text_.measure_width(footer, glyph_h);
        fl.origin_top_left_px = {vw * 0.5f - text_w * 0.5f, vh - 80.f};
        fl.glyph_h_px         = glyph_h;
        fl.bg_color           = glm::vec4{0.05f, 0.05f, 0.07f, 0.70f};
        fl.bg_padding_px      = 14.f;
        // Bright yellow while inputting; vivid red while flashing an error;
        // near-white default for legibility over the world view.
        if (map_input_active_)
            fl.color = glm::vec3{1.0f, 0.95f, 0.55f};
        else if (map_input_err_flash_s_ > 0.f)
            fl.color = glm::vec3{1.0f, 0.4f, 0.3f};
        else
            fl.color = glm::vec3{0.92f, 0.94f, 0.98f};
        fl.viewport_size_px   = {vw, vh};
        text_.draw_lines(fl);
    }

    // ---- Asset palette (PBD-030) ------------------------------------------
    // Top-left sidebar listing every ModelRegistry entry. PBD-031 will read
    // `map_palette_selection_` to know which model to plop; this ticket only
    // renders the picker. No placement, no click handling — that's the
    // explicit out-of-scope boundary of the EPIC-002 v1 first slice.
    //
    // Layout per row:  [NN] NAME   [F] [SWATCH]
    // where NN is the zero-padded model id, NAME is the def's name, [F] is
    // the one-letter flag chip (first letter of format_model_flags' output,
    // or '-' for None), and SWATCH is a filled rect tinted with def.tint.
    //
    // ModelRegistry::all() returns an unordered_map, so registration order is
    // already lost by the time we get here. To keep the palette stable across
    // runs we sort by model id ascending. The 8 streets.ide entries today
    // are id-ordered by category in the IDE file, so sorting by id preserves
    // the author's grouping (buildings first, then road/walk).
    {
        const auto& defs = world_models_.all();
        // Sort by id for stable layout. We do this every frame; with N≈8
        // entries the cost is invisible. Switch to a cached vector on the
        // registry if N ever grows past a few dozen.
        std::vector<const ModelDef*> ordered;
        ordered.reserve(defs.size());
        for (const auto& kv : defs) ordered.push_back(&kv.second);
        std::sort(ordered.begin(), ordered.end(),
                  [](const ModelDef* a, const ModelDef* b) {
                      return a->id < b->id;
                  });

        // Defensive: clamp selection in case the registry shrank since the
        // last frame (it doesn't today, but the cost is one branch).
        const int n = static_cast<int>(ordered.size());
        if (n > 0 && map_palette_selection_ >= n) map_palette_selection_ = 0;

        if (n > 0) {
            const glm::vec2 viewport_px{
                static_cast<float>(window_.width()),
                static_cast<float>(window_.height()),
            };

            // Sidebar geometry. Anchored top-left, 24px from each edge.
            // Width is fixed at 280px which fits "NN NAME [F]" comfortably at
            // glyph_h 18 with our stroke font's pitch. Anchored top-left
            // because:
            //   - the inspector readout lives bottom-left and grows upward,
            //   - the centre footer band sits at the bottom,
            //   - the cell-jump prompt (PBD-027) reuses the footer.
            // Top-left is the only corner with nothing today, and reading a
            // vertical list anchored to a fixed corner is easier than tracking
            // the floating selection cursor.
            const float pal_x      = 24.f;
            const float pal_y      = 24.f;
            const float pal_w      = 280.f;
            const float glyph_h    = 18.f;
            const float row_h      = glyph_h * 1.45f; // matches draw_text_lines
            const float title_h    = glyph_h * 1.45f; // one header row
            const float swatch_sz  = glyph_h;
            const float inner_pad  = 12.f;

            // Build the row label strings. C-style buffers owned here so the
            // pointers we hand to draw_text_lines outlive that call.
            constexpr int   MAX_ROWS = 64;
            char            row_buf[MAX_ROWS][80];
            const char*     row_ptrs[MAX_ROWS];

            // Header row first so the user knows what the sidebar is.
            std::snprintf(row_buf[0], sizeof(row_buf[0]), "ASSETS");
            row_ptrs[0] = row_buf[0];

            const int max_data_rows = std::min(n, MAX_ROWS - 1);
            for (int i = 0; i < max_data_rows; ++i) {
                const ModelDef* d = ordered[static_cast<std::size_t>(i)];
                const char* mflags = format_model_flags(d->flags);
                // First non-pipe char of the flag string is our chip letter.
                // format_model_flags returns "NONE" / "BLDG" / "BLDG|LOD" /
                // etc; we take the first byte. NONE -> '-' so the chip column
                // doesn't shout "N" at the user.
                char chip = '-';
                if (mflags && mflags[0] && std::strcmp(mflags, "NONE") != 0) {
                    chip = mflags[0];
                }
                // Trim name to fit. "NN NAME [F]" — leave the swatch column
                // for the rect pass.
                const char* name = d->name.empty() ? "?" : d->name.c_str();
                std::snprintf(row_buf[i + 1], sizeof(row_buf[i + 1]),
                              "%2u %s [%c]",
                              d->id, name, chip);
                row_ptrs[i + 1] = row_buf[i + 1];
            }
            const int total_rows = 1 + max_data_rows;
            const float pal_h = title_h + static_cast<float>(max_data_rows) * row_h;

            // Render order (back-to-front so strokes land on top of fills):
            //   1. Dark backplate (one rect).
            //   2. Selection highlight bar (one rect on the selected row).
            //   3. Tint swatches (one rect per data row).
            //   4. Stroke text rows (header, then each data row in its own
            //      pass so selected vs unselected can use distinct colours).
            //
            // We could have folded the backplate into TextLines::bg_color, but
            // the header and per-row rendering split would force three plates
            // and three draw calls. One draw_rects + per-row text is cheaper
            // and keeps the layering predictable.
            std::vector<Menu::Rect> rects;
            rects.reserve(static_cast<std::size_t>(total_rows) + 2u);

            // Backplate. Inspector-style dark fill so the strokes read over
            // any cell colour underneath.
            rects.push_back(Menu::Rect{
                {pal_x - inner_pad, pal_y - inner_pad},
                {pal_x + pal_w + inner_pad, pal_y + pal_h + inner_pad},
                glm::vec3{0.06f, 0.07f, 0.10f},
            });

            // Selection highlight bar — spans the full backplate width on the
            // currently-selected data row. Row i (data) sits at y = pal_y +
            // title_h + i * row_h.
            if (map_palette_selection_ >= 0 &&
                map_palette_selection_ < max_data_rows) {
                float sel_top = pal_y + title_h +
                                static_cast<float>(map_palette_selection_) * row_h;
                rects.push_back(Menu::Rect{
                    {pal_x - inner_pad * 0.5f, sel_top - 2.f},
                    {pal_x + pal_w + inner_pad * 0.5f, sel_top + glyph_h + 2.f},
                    glm::vec3{0.22f, 0.20f, 0.06f},
                });
            }

            // One tint swatch per data row, right-aligned within the sidebar.
            for (int i = 0; i < max_data_rows; ++i) {
                float sw_top = pal_y + title_h +
                               static_cast<float>(i) * row_h;
                float sw_x1  = pal_x + pal_w;
                float sw_x0  = sw_x1 - swatch_sz;
                rects.push_back(Menu::Rect{
                    {sw_x0, sw_top},
                    {sw_x1, sw_top + swatch_sz},
                    ordered[static_cast<std::size_t>(i)]->tint,
                });
            }

            menu_.draw_rects(rects.data(), static_cast<int>(rects.size()),
                              viewport_px);

            // Text pass: header first in its own colour, then the data rows.
            // We render in two slices so the selected row can use a brighter
            // colour against the highlight bar without colouring every row.
            {
                const char* head_lines[1] = {row_ptrs[0]};
                Menu::TextLines hl;
                hl.lines              = head_lines;
                hl.count              = 1;
                hl.origin_top_left_px = {pal_x, pal_y};
                hl.glyph_h_px         = glyph_h;
                hl.thickness_px       = 2.f;
                hl.color              = glm::vec3{0.55f, 0.62f, 0.66f};
                hl.viewport_size_px   = viewport_px;
                menu_.draw_text_lines(hl);
            }

            for (int i = 0; i < max_data_rows; ++i) {
                const char* one[1] = {row_ptrs[i + 1]};
                Menu::TextLines rl;
                rl.lines              = one;
                rl.count              = 1;
                rl.origin_top_left_px = {pal_x,
                                          pal_y + title_h +
                                              static_cast<float>(i) * row_h};
                rl.glyph_h_px         = glyph_h;
                rl.thickness_px       = 2.f;
                rl.color = (i == map_palette_selection_)
                               ? glm::vec3{0.98f, 0.92f, 0.45f}
                               : glm::vec3{0.86f, 0.90f, 0.92f};
                rl.viewport_size_px   = viewport_px;
                menu_.draw_text_lines(rl);
            }
        }
    }

    window_.swap();
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
    if (input_.pressed(SDL_SCANCODE_M)) {
        glm::vec3 pos, fwd;
        const char* label;
        if (mode_ == Mode::InVehicle && traffic_.player_car()) {
            pos = traffic_.player_car()->vehicle.position();
            fwd = traffic_.player_car()->vehicle.orientation()
                * glm::vec3{0.f, 0.f, -1.f};
            label = "drive";
        } else {
            pos = player_.feet_position();
            float yr = glm::radians(player_.facing_yaw_deg());
            fwd = {std::cos(yr), 0.f, std::sin(yr)};
            label = (mode_ == Mode::DebugFly) ? "fly" : "on-foot";
        }
        debug_overlay::log_world_area(pos, fwd, label);
    }

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

    police::spawn_from_cars(wanted_.level(), police_target,
                             traffic_, pedestrians_);

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

    police::promote_reentered_cars(pedestrians_, traffic_);

    {
        glm::vec3 player_torso = police_target + glm::vec3{0.f, 1.2f, 0.f};
        auto shots = police::resolve_shots(pedestrians_, world_collision_,
                                            player_torso, player_,
                                            audio_, particles_, debug_draw_);
        if (shots.player_died) {
            if (mode_ == Mode::InVehicle) {
                traffic_.set_player_driver(nullptr);
                enter_mode(Mode::OnFoot);
            }
            player_.respawn();
            wanted_.reset();
        }
    }

    vehicle_effects::update(fdt, traffic_, particles_, audio_);
    particles_.update(fdt);

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
    {
        debug_overlay::RenderState ds;
        ds.in_debug_fly      = (mode_ == Mode::DebugFly);
        ds.show_enter_prompt = can_enter_car_ && steal_target_ != nullptr;
        if (ds.show_enter_prompt) {
            glm::vec3 base = steal_target_->vehicle.position();
            base.y -= steal_target_->vehicle.chassis_full_extents.y * 0.5f;
            ds.enter_prompt_base   = base;
            ds.enter_prompt_radius = ENTRY_RADIUS;
        }
        debug_overlay::render(ds, debug_draw_, camera_, vp,
                               world_collision_, traffic_);
    }

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
