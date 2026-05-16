#pragma once

#include <string>

#include "audio/audio_engine.h"
#include "core/time.h"
#include "game/pedestrian.h"
#include "game/player.h"
#include "game/traffic.h"
#include "game/wanted_system.h"
#include "game/weapons.h"
#include "physics/world_collision.h"
#include "platform/input.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/debug_draw.h"
#include "render/hud.h"
#include "render/menu.h"
#include "render/text.h"
#include "render/mesh.h"
#include "render/particles.h"
#include "render/shader.h"
#include "render/spring_arm.h"
#include "render/texture.h"
#include "scene/scene.h"
#include "world/model_registry.h"
#include "world/road_graph.h"
#include "world/streamer.h"
#include "world/world_defs.h"

namespace pengine {

class Application {
public:
    enum class Mode { OnFoot, InVehicle, DebugFly };

    // Outer application state. The main menu is shown before any gameplay
    // input/update runs; "New Game" transitions to Playing. Dev Tools is a
    // submenu (PBD-016) whose only entry, "Map Builder", drops into the
    // MapBuilder state (PBD-023 onward, EPIC-001). MapBuilder is currently
    // an empty scaffold — no world interaction yet.
    enum class AppState { MainMenu, DevToolsMenu, MapBuilder, Playing };

    bool init();
    int  run();
    void shutdown();

private:
    void process_events();
    void process_menu_events();
    void process_map_builder_events();
    void update(double dt);
    void update_map_builder(float dt);
    void render(double alpha);
    void render_menu();
    void render_map_builder();

    void enter_mode(Mode m);
    void try_toggle_vehicle();
    void update_in_vehicle(float dt, float mdx, float mdy);

    void enter_app_state(AppState s);
    void activate_menu_selection();

    Window         window_;
    Input          input_;
    FixedTimestep  clock_;

    Shader         lit_shader_;
    Shader         lit_instanced_shader_;
    Shader         skinned_shader_;
    Mesh           cube_mesh_;

    Texture        checker_tex_;
    Texture        asphalt_tex_;
    Texture        grass_tex_;
    Texture        facade_tex_;
    Texture        sidewalk_tex_;

    Camera         camera_;
    Scene          scene_;
    ModelRegistry  world_models_;
    Streamer       streamer_;

    WorldCollision      world_collision_;
    DebugDraw           debug_draw_;
    Particles           particles_;
    SpringArm           spring_;
    RoadGraph           road_graph_;
    TrafficSystem       traffic_;
    PedestrianSystem    pedestrians_;
    AudioEngine         audio_;

    Player              player_;
    Weapons             weapons_;
    WantedSystem        wanted_;
    Hud                 hud_;
    Menu                menu_;
    Text                text_;

    Mode  mode_           = Mode::OnFoot;
    Mode  saved_mode_     = Mode::OnFoot; // last non-debug mode
    bool  mouse_captured_ = false;
    bool  running_        = false;
    bool  can_enter_car_  = false;        // updated each frame, used by HUD

    AppState app_state_       = AppState::MainMenu;
    int      menu_selection_  = 0;

    // Map Builder camera (PBD-024). The state is a top-down perspective camera
    // — yaw fixed at -90 (looking along -Z) and pitch fixed at near-straight-
    // down (-89). XZ pans on the ground plane; Y is altitude controlled by
    // mouse wheel. Driven by process_map_builder_events / update_map_builder;
    // applied to camera_ each frame inside render_map_builder.
    glm::vec3 map_cam_pos_ {0.f, 200.f, 0.f}; // .y is altitude
    static constexpr float MAP_CAM_PITCH_DEG = -89.f;
    static constexpr float MAP_CAM_YAW_DEG   = -90.f;
    static constexpr float MAP_CAM_ALT_MIN   = 40.f;
    static constexpr float MAP_CAM_ALT_MAX   = 1500.f;
    static constexpr float MAP_CAM_WHEEL_STEP = 0.10f; // fraction of altitude per tick
    // Last wall-clock tick of the map-builder loop; used to derive a real-time
    // dt for pan/zoom (gameplay's fixed-timestep clock isn't appropriate here
    // because we don't tick a simulation while in MapBuilder).
    TimePoint map_last_frame_ {};

    // PBD-027: typed cell-jump input. When `map_input_active_` is true, the
    // map-builder event pump diverts keystrokes into `map_input_buf_` instead
    // of treating them as WASD pan, and pops up a "GO TO CELL: <buf>_" prompt
    // in the corner. Enter commits (parse "X,Z", clamp, re-centre camera);
    // Esc cancels. The streamer naturally pages in the target cell on its
    // next 100ms poll once map_cam_pos_ has moved.
    bool          map_input_active_   = false;
    std::string   map_input_buf_;
    // Brief on-screen flash when an entered coord was clamped or unparseable.
    // Set at commit-time, decays in update_map_builder.
    float         map_input_err_flash_s_ = 0.f;

    // F-to-steal: when OnFoot and an AI car is the closest in-range target,
    // pressing F removes that AI from TrafficSystem and teleports the player
    // car to its pose. The target is recomputed every frame in update().
    // F-to-enter target. Null = no car in range. The pointer is stable
    // across frames (cars_ uses unique_ptr internally) but invalidated when
    // a car is destroyed; recompute every frame.
    TrafficSystem::Car* steal_target_ = nullptr;

    static constexpr float ENTRY_RADIUS = 4.0f; // metres for F-to-enter

    TimePoint stats_start_{};
    int       fps_frames_  = 0;
    double    max_frame_ms_ = 0.0;
    TimePoint last_frame_{};

    double    world_time_    = 0.0;  // accumulates fixed-timestep seconds
};

} // namespace pengine
