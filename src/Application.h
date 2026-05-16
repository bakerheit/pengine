#pragma once

#include <string>
#include <unordered_set>
#include <vector>

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

    // PBD-032: Map Builder tool mode. Mutually exclusive: at any time the
    // editor is either placing a new instance from the palette or deleting
    // the instance under the cursor. The world-click handler dispatches on
    // this enum; the bar at the bottom of the screen exposes a button for
    // each mode plus the asset palette (Place mode uses `map_palette_
    // selection_`; Delete mode ignores it).
    enum class MapTool { Place, Delete };

    // PBD-051: Map Builder footer-error channel disambiguation. v1 had a
    // single boolean-ish flash timer that lit up "BAD CELL COORD" no matter
    // what actually went wrong, including five different failure paths that
    // had nothing to do with cell coords. Recording the kind alongside the
    // timer lets the footer render an honest message per cause without
    // adding a second on-screen channel. New failure paths add a new enum
    // value + footer-string case.
    enum class MapErrorKind {
        None,
        BadCellCoord,    // cell-jump prompt clamped or unparseable
        PlaceRejected,   // streamer.add_instance returned false on a click
        DeleteRejected,  // streamer.remove_instance returned false on a click
        UndoFailed,      // apply_undo couldn't apply the inverse
        RedoFailed,      // apply_redo couldn't re-apply
    };

    // PBD-042: Map Builder placement size preset. Three named uniform-scale
    // multipliers selectable from the bottom bar (S/M/L buttons) or hotkeys
    // 3/4/5. Multiplies `InstanceDef.transform.scale` at placement time;
    // already-placed instances are unaffected (per Phase A scope — Phase B
    // will add an edit affordance). Default Small (1.0×) preserves today's
    // placement behaviour. Free scale + rotation are PBD-043.
    enum class SizePreset { Small, Medium, Large };

    static constexpr float size_preset_factor(SizePreset p) {
        // Architect's suggested multipliers. Uniform across XYZ.
        switch (p) {
            case SizePreset::Small:  return 1.0f;
            case SizePreset::Medium: return 2.0f;
            case SizePreset::Large:  return 3.5f;
        }
        return 1.0f;
    }

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

    // PBD-032: bottom-bar UI for Map Builder. The bar spans the full
    // viewport width and is anchored above the centre footer. It contains
    // a row of tool buttons on the left (Place / Delete) and an asset
    // palette to their right (one slot per ModelRegistry entry).
    //
    // Hit-testing is done by building a flat list of clickable regions
    // once per frame in `compute_map_builder_bar_layout` and consulting
    // that list (a) in `process_map_builder_events` on left-click to
    // decide whether the click hits a bar region or falls through to the
    // world, and (b) in `render_map_builder` to draw the same geometry.
    // Pixel coordinates throughout — matches the DPI-scaled mouse coords
    // PBD-031 settled on.
    enum class MapBarHitKind { None, ToolButton, AssetSlot, SizePreset };
    struct MapBarHit {
        MapBarHitKind kind = MapBarHitKind::None;
        // For ToolButton: the MapTool value the button selects.
        // For AssetSlot: the index into the sorted-by-id palette list.
        int index = 0;
    };
    struct MapBarLayout {
        // Outer bar rect (background quad bounds).
        glm::vec2 bar_min_px {0.f};
        glm::vec2 bar_max_px {0.f};
        // Per-region hit-test geometry, drawn left-to-right. The first
        // two entries are the tool buttons (Place, Delete); the rest are
        // asset slots in sorted-id order.
        struct Region {
            glm::vec2     min_px {0.f};
            glm::vec2     max_px {0.f};
            MapBarHitKind kind   = MapBarHitKind::None;
            int           index  = 0;
        };
        std::vector<Region> regions;
    };
    MapBarLayout compute_map_builder_bar_layout() const;
    MapBarHit    hit_test_map_builder_bar(const MapBarLayout& layout,
                                          int x_px, int y_px) const;
    // PBD-035: single source of truth for the Map Builder palette ordering.
    // Returns ModelRegistry entries sorted by id (the underlying storage is
    // an unordered_map). Read-only; callers cache the result if they need
    // it more than once per frame. The bar layout (indices), the placement
    // path, and the ghost-AABB resolver all index into this same ordering.
    std::vector<const ModelDef*> sorted_palette() const;
    // DPI helper: SDL hands mouse coords in logical pixels (window size),
    // but the bar layout is in physical pixels (drawable size). Scale up
    // matching what PBD-031 did for the world-pick unproject.
    void scale_mouse_to_drawable(int mx_logical, int my_logical,
                                  int& mx_draw, int& my_draw) const;

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
    static constexpr float MAP_CAM_YAW_DEG        = -90.f;
    static constexpr float MAP_CAM_ALT_MIN        = 40.f;
    static constexpr float MAP_CAM_ALT_MAX        = 1500.f;
    static constexpr float MAP_CAM_WHEEL_STEP     = 0.10f;  // fraction of altitude per tick
    // Pitch is runtime-adjustable (R + wheel). Default top-down; clamped to a
    // range that keeps the unproject well-conditioned (ray.y not near zero) and
    // avoids inverting the world from below the ground plane.
    static constexpr float MAP_CAM_PITCH_DEFAULT  = -89.f;
    static constexpr float MAP_CAM_PITCH_MIN      = -89.f;  // most top-down
    static constexpr float MAP_CAM_PITCH_MAX      = -15.f;  // most oblique
    static constexpr float MAP_CAM_TILT_STEP_DEG  = 5.f;    // degrees per wheel tick
    float map_cam_pitch_deg_ = MAP_CAM_PITCH_DEFAULT;
    // Last wall-clock tick of the map-builder loop; used to derive a real-time
    // dt for pan/zoom (gameplay's fixed-timestep clock isn't appropriate here
    // because we don't tick a simulation while in MapBuilder).
    TimePoint map_last_frame_ {};

    // PBD-030: asset palette selection. Index into the sorted-by-id list of
    // ModelRegistry entries displayed in the Map Builder sidebar. PBD-031 will
    // consume this to know which model to plop on click; v1 (this ticket) just
    // renders the highlight. Reset to 0 on entering MapBuilder. Wraps on
    // Up/Down navigation. WASD is left to the camera pan path (see PBD-024) to
    // avoid a key conflict; the palette uses arrow keys only.
    int map_palette_selection_ = 0;

    // PBD-027: typed cell-jump input. When `map_input_active_` is true, the
    // map-builder event pump diverts keystrokes into `map_input_buf_` instead
    // of treating them as WASD pan, and pops up a "GO TO CELL: <buf>_" prompt
    // in the corner. Enter commits (parse "X,Z", clamp, re-centre camera);
    // Esc cancels. The streamer naturally pages in the target cell on its
    // next 100ms poll once map_cam_pos_ has moved.
    bool          map_input_active_   = false;
    std::string   map_input_buf_;
    // Brief on-screen flash when an editor action failed. Set at commit-
    // time, decays in update_map_builder. PBD-051: paired with
    // `map_err_kind_` so the footer renders a cause-specific message
    // instead of a one-size-fits-all "BAD CELL COORD".
    float         map_input_err_flash_s_ = 0.f;
    MapErrorKind  map_err_kind_          = MapErrorKind::None;

    // PBD-031: Map Builder cursor + placement state.
    //
    // `map_mouse_world_xz_` is the unprojected mouse position on the y=0 plane,
    // computed once per render frame in `render_map_builder` (where the
    // view/proj matrix is built) and stashed for the next `update_map_builder`
    // tick to consume on a click. `map_mouse_valid_` is false until the first
    // mouse motion event arrives or the unproject fails (e.g. ray points
    // upward away from the ground — won't happen at -89° pitch but the guard
    // is cheap).
    //
    // `map_place_pending_` is set in process_map_builder_events on a
    // SDL_MOUSEBUTTONDOWN (left) and consumed by update_map_builder. Edge-
    // triggered by virtue of being event-driven; one click → one place.
    glm::vec2 map_mouse_world_xz_ {0.f, 0.f};
    bool      map_mouse_valid_    = false;
    bool      map_place_pending_  = false;

    // PBD-032: Map Builder tool mode + delete-click latch. Initialised to
    // Place on MapBuilder enter; toggled by clicking the Place/Delete
    // buttons in the bottom bar. `map_delete_pending_` is the sibling of
    // `map_place_pending_` — set in `process_map_builder_events` on a
    // world-area left click while in Delete mode, consumed by
    // `update_map_builder` after `streamer_.pump()` runs.
    MapTool   map_tool_           = MapTool::Place;
    bool      map_delete_pending_ = false;

    // PBD-042: active uniform scale preset for placement. Defaults to Small
    // (1.0×) — preserves today's behaviour for users who never touch the
    // buttons. Reset to Small on MapBuilder entry. Consumed by:
    //   - the ghost-AABB renderer (so the preview reflects what'll land), and
    //   - the placement path in update_map_builder (multiplies
    //     `inst.transform.scale` before `streamer_.add_instance`).
    SizePreset map_size_preset_   = SizePreset::Small;

    // PBD-043: free uniform-scale multiplier and yaw rotation for placement.
    // Stacks on top of the size preset: effective scale = preset_factor ×
    // free_scale. Defaults of 1.0× / 0° preserve pre-PBD-043 behaviour for
    // users who never touch Shift+wheel / Q / E. Per the ticket: persistent
    // across mode changes, clicks, and Esc — only an explicit user adjust or
    // app restart resets them. Reset to defaults on MapBuilder entry.
    //
    // Free scale is driven by Shift+wheel (multiplicative step, same feel as
    // altitude zoom); yaw by Q (CCW) / E (CW) in 15° detents. Yaw is stored
    // in degrees here for cheap legibility in the caption / wrap math; the
    // quat is constructed at placement and ghost-render time only.
    float map_place_free_scale_ = 1.0f;
    float map_place_yaw_deg_    = 0.0f;
    static constexpr float MAP_PLACE_FREE_SCALE_MIN  = 0.25f;
    static constexpr float MAP_PLACE_FREE_SCALE_MAX  = 8.0f;
    static constexpr float MAP_PLACE_FREE_SCALE_STEP = 0.10f; // per wheel tick
    static constexpr float MAP_PLACE_YAW_DETENT_DEG  = 15.0f;

    // PBD-034: Map Builder undo/redo command stack. The vocabulary is two
    // inverse pairs — Place's inverse is "remove the instance at the saved
    // (cell, instance_def)"; Delete's inverse is "re-add it." We snapshot
    // the full `InstanceDef` (position, scale, rotation, model id) into each
    // command so redo restores exactly what was placed/removed.
    //
    // Stack semantics match standard text-editor conventions:
    //   - Every successful place/delete pushes onto `undo_stack_` and clears
    //     `redo_stack_` (any new edit invalidates the redo history).
    //   - Ctrl+Z pops `undo_stack_`, applies the inverse, pushes the same
    //     command onto `redo_stack_`.
    //   - Ctrl+Y pops `redo_stack_`, re-applies, pushes back onto `undo_stack_`.
    //
    // Fixed depth (`MAX_UNDO_DEPTH`) — when the undo stack is full we drop
    // the oldest entry (FIFO from the front) before pushing. Memory bound is
    // 64 × sizeof(EditCommand) ≈ a few KB; negligible.
    //
    // V1 simplification: when a cell evicts, any undo entry referencing it
    // is no longer safely replayable (the streamer's `loaded_[cell]` is gone
    // and the on-disk IPL would clash on re-add). `last_loaded_cells_`
    // tracks which cells were resident last frame; in `update_map_builder`
    // we recompute the loaded set after `pump()` and drop matching entries
    // from both stacks. Resets — like the placement state — on
    // `enter_app_state(MapBuilder)`.
    struct EditCommand {
        enum class Kind { Place, Delete };
        Kind        kind;
        CellCoord   cell;
        InstanceDef instance;
    };
    static constexpr std::size_t MAX_UNDO_DEPTH = 64;
    std::vector<EditCommand> undo_stack_;
    std::vector<EditCommand> redo_stack_;
    std::unordered_set<CellCoord, CellCoordHash> last_loaded_cells_;

    // Push a fresh edit onto the undo stack, clear the redo stack (standard
    // "new edit invalidates redo history"), and cap depth at MAX_UNDO_DEPTH
    // by dropping the oldest entry.
    void push_edit_command(EditCommand cmd);
    // Pop from undo, apply the inverse via the streamer, push onto redo.
    // No-op if the undo stack is empty or the target cell isn't loaded
    // (defensive — `drop_evicted_commands` should keep that from happening,
    // but the streamer-call return values still gate the redo push).
    void apply_undo();
    // Symmetric: pop from redo, re-apply, push onto undo.
    void apply_redo();
    // Walk both stacks and erase any entry whose `cell` isn't in
    // `currently_loaded`. Called from `update_map_builder` immediately after
    // `streamer_.pump()` when an eviction is detected.
    void drop_evicted_commands(
        const std::unordered_set<CellCoord, CellCoordHash>& currently_loaded);

    // PBD-037: Map Builder bar hover state. Refreshed every frame in
    // `update_map_builder` by re-hit-testing the (DPI-scaled) mouse against
    // the current bar layout; consumed by `render_map_builder` to draw a
    // subtle highlight behind the hovered region. `kind == None` means
    // "no hover" (mouse over world, or over the bar but in a gap between
    // regions). Independent of selection / active-tool — hover is its own
    // visual layer that sits behind both rings.
    MapBarHit map_bar_hover_      {};

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
