#pragma once

#include "audio/audio_engine.h"
#include "core/time.h"
#include "editor/map_builder.h"
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
    // MapBuilder state (PBD-023 onward, EPIC-001). The MapBuilder behaviour
    // lives in `MapBuilder` (PBD-049 extract); Application just owns the
    // instance and dispatches to it.
    enum class AppState { MainMenu, DevToolsMenu, MapBuilder, Playing };

    bool init();
    int  run();
    void shutdown();

private:
    void process_events();
    void process_menu_events();
    void update(double dt);
    void render(double alpha);
    void render_menu();

    void enter_mode(Mode m);
    void try_toggle_vehicle();
    void update_in_vehicle(float dt, float mdx, float mdy);

    void enter_app_state(AppState s);
    void activate_menu_selection();

    // PBD-054: Edit<->Test round-trip.
    //
    // `transition_play_here` is the MapBuilder->Playing path activated by
    // the PLAY HERE button or the T hotkey. Unlike `enter_app_state`, it
    // does NOT call `MapBuilder::exit()` (which would flush dirty cells
    // and tear down the session); instead it calls `suspend()` so the
    // editor's camera/palette/tool/undo state are preserved for the
    // return trip. The player is respawned on foot at `map_cam_pos_.xz`
    // (Y from `Heightmap::sample`) and `came_from_map_builder_` is set
    // so Playing's Esc/B-handler offers the "Back to Map Builder" path.
    //
    // `transition_back_to_map_builder` is the Playing->MapBuilder return
    // path activated by B while in Playing AND `came_from_map_builder_`
    // is true. It calls `MapBuilder::resume()` (which rebinds the same
    // deps refs the editor was using before the round-trip).
    void transition_play_here();
    void transition_back_to_map_builder();

    // PBD-054: teleport the player on foot to (xz, Heightmap::sample(xz)).
    // Resets the spring-arm anchor so the chase cam isn't pointing at the
    // old position on the first post-respawn frame. Forces Mode::OnFoot —
    // per Andrew's product direction the round-trip lands the player on
    // foot regardless of which mode they were last in.
    void respawn_player_at_xz(float wx, float wz);

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

    // PBD-049: the editor owns its own state. Application calls
    // `map_builder_.enter()` on transition into MapBuilder, then dispatches
    // process_events / update / render to it each frame until the editor
    // signals back via `consume_back_request` or another transition fires.
    MapBuilder          map_builder_;
    // Wall-clock frame timepoint for the MapBuilder dt (the fixed-timestep
    // clock isn't appropriate while in the editor — we don't tick a
    // simulation). Reset on entry to MapBuilder.
    TimePoint           map_builder_last_frame_{};

    Mode  mode_           = Mode::OnFoot;
    Mode  saved_mode_     = Mode::OnFoot; // last non-debug mode
    bool  mouse_captured_ = false;
    bool  running_        = false;
    bool  can_enter_car_  = false;        // updated each frame, used by HUD

    AppState app_state_       = AppState::MainMenu;
    int      menu_selection_  = 0;

    // PBD-054: true while the current Playing session was launched from
    // the MapBuilder (via PLAY HERE / T hotkey). Drives Playing's B-key
    // handler to offer the "Back to Map Builder" return path; cleared on
    // Main-Menu->Playing entry so a regular New Game session doesn't
    // accidentally drop the user into the editor.
    bool     came_from_map_builder_ = false;

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
