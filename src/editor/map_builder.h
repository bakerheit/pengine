#pragma once
//
// Map Builder editor state — Cities-Skylines-style top-down editor that drives
// the streamer with an editor camera and supports place/delete of `ModelDef`
// instances against `loaded_` cells, an undo/redo stack, and the bottom-bar UI
// (tool buttons, asset palette, size presets).
//
// Extracted out of Application in PBD-049. Application owns one MapBuilder and
// dispatches to it from `run()` when `app_state_ == AppState::MapBuilder`. All
// the `map_*_` state, the bar layout / hit-test helpers, the EditCommand
// vocabulary, and the place/delete/undo/redo plumbing live here.
//
// Dependencies are passed in via a `Deps` struct at `enter()` time so this
// header doesn't have to include the full Application transitive closure. The
// MapBuilder holds non-owning references throughout the session and releases
// them on `exit()`.
//
// PBD-049 chose `class MapBuilder` over a `MapBuilderState` struct because
// the editor's state is entirely contained in a single TU (unlike traffic,
// which split into five). Private members + a small public API beats an
// internal header full of struct definitions when there's nothing to share
// across sibling .cpps.

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/time.h"
#include "world/cell_coord.h"
#include "world/instance_def.h"
#include "world/world_defs.h"

namespace pengine {

// Forward decls — keep the include set minimal at this header level. The
// .cpp pulls in the full set.
class  Window;
class  Input;
class  Camera;
class  Scene;
class  Streamer;
class  ModelRegistry;
class  RoadGraph;
class  DebugDraw;
class  Menu;
class  Text;
class  Shader;
class  Texture;
struct ModelDef;

class MapBuilder {
public:
    // PBD-032: tool mode. Mutually exclusive — at any time the editor is
    // either placing a new instance from the palette or deleting the
    // instance under the cursor.
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

    // PBD-042: placement size preset. Three named uniform-scale multipliers
    // selectable via the bottom-bar S/M/L buttons or hotkeys 3/4/5.
    enum class SizePreset { Small, Medium, Large };

    static constexpr float size_preset_factor(SizePreset p) {
        switch (p) {
            case SizePreset::Small:  return 1.0f;
            case SizePreset::Medium: return 2.0f;
            case SizePreset::Large:  return 3.5f;
        }
        return 1.0f;
    }

    // Subsystem references the editor borrows for the duration of a
    // session. Lifetime contract: caller owns the referents and must keep
    // them alive between `enter()` and `exit()`. Non-owning; copy/move of
    // MapBuilder is disallowed because the refs would dangle.
    struct Deps {
        Window&         window;
        Input&          input;
        Camera&         camera;
        Scene&          scene;
        Streamer&       streamer;
        ModelRegistry&  models;
        RoadGraph&      road_graph;
        DebugDraw&      debug_draw;
        Menu&           menu;
        Text&           text;
        Shader&         lit_shader;
        Texture&        checker_tex;
    };

    MapBuilder()                                 = default;
    MapBuilder(const MapBuilder&)                = delete;
    MapBuilder& operator=(const MapBuilder&)     = delete;

    // Called when Application transitions into MapBuilder. Resets all
    // session-scoped state (camera at world centre at 600m, palette index
    // 0, empty undo/redo stacks, etc.).
    void enter(const Deps& deps);

    // Called when Application transitions out of MapBuilder. Flushes any
    // dirty cells (PBD-033) and closes any open SDL text-input session
    // (PBD-027). Idempotent — safe to call from `enter_app_state` when
    // the prior state wasn't MapBuilder.
    void exit();

    // PBD-054: pause/resume hooks used by the MapBuilder<->Playing round-
    // trip. `suspend` closes any open SDL text-input session but otherwise
    // leaves session state (camera, palette, tool, undo stack, deps refs)
    // intact so `resume` can restore the user to exactly where they were.
    // Unlike `exit()`, this is NOT a session boundary — dirty cells are
    // left dirty and undo/redo are NOT cleared. Pairs 1:1 with `resume`.
    void suspend();
    void resume();

    // Run-loop hooks. Application calls these only when its `app_state_`
    // is MapBuilder. `process_events` may set `running_out = false` on
    // Ctrl+Q. The "user pressed Esc to back out" signal is surfaced via
    // `consume_back_request()` so the caller can drive the state machine.
    void process_events(bool& running_out);
    void update(float dt);
    void render();

    // Accessor for Application's title-bar logging and the PBD-054
    // PLAY HERE spawn site.
    const glm::vec3& cam_pos() const { return map_cam_pos_; }

    // Edge-triggered: true once after the user presses Esc/Backspace/B in
    // normal mode, then false until the next press. Application reads this
    // each frame to decide whether to transition back to DevToolsMenu.
    bool consume_back_request();

    // PBD-054: edge-triggered "PLAY HERE" signal. True once after the user
    // clicks the PLAY HERE bar button or presses T in normal mode, then
    // false until the next activation. Application reads this to drive
    // the MapBuilder->Playing round-trip (player respawn at `cam_pos().xz`,
    // on foot, with the streamer's loaded cells preserved in memory).
    bool consume_play_here_request();

private:
    // -----------------------------------------------------------------------
    // PBD-032 bar layout / hit-test
    // -----------------------------------------------------------------------
    // PBD-054 adds the PlayHere region — rightmost slot in the bar that
    // transitions to gameplay with the player respawned at `map_cam_pos_`.
    enum class MapBarHitKind { None, ToolButton, AssetSlot, SizePreset, PlayHere };
    struct MapBarHit {
        MapBarHitKind kind  = MapBarHitKind::None;
        int           index = 0;
    };
    struct MapBarLayout {
        glm::vec2 bar_min_px {0.f};
        glm::vec2 bar_max_px {0.f};
        struct Region {
            glm::vec2     min_px {0.f};
            glm::vec2     max_px {0.f};
            MapBarHitKind kind   = MapBarHitKind::None;
            int           index  = 0;
        };
        std::vector<Region> regions;
    };

    MapBarLayout compute_bar_layout() const;
    MapBarHit    hit_test_bar(const MapBarLayout& layout,
                              int x_px, int y_px) const;
    void scale_mouse_to_drawable(int mx_logical, int my_logical,
                                 int& mx_draw, int& my_draw) const;

    // PBD-035: id-sorted view over the ModelRegistry. Single source of
    // truth for palette ordering.
    std::vector<const ModelDef*> sorted_palette() const;

    // -----------------------------------------------------------------------
    // PBD-034 undo/redo
    // -----------------------------------------------------------------------
    struct EditCommand {
        enum class Kind { Place, Delete };
        Kind        kind;
        CellCoord   cell;
        InstanceDef instance;
    };
    static constexpr std::size_t MAX_UNDO_DEPTH = 64;

    void push_edit_command(EditCommand cmd);
    void apply_undo();
    void apply_redo();
    void drop_evicted_commands(
        const std::unordered_set<CellCoord, CellCoordHash>& currently_loaded);

    // PBD-050: pumps the streamer and sweeps the undo/redo stacks against
    // cells evicted by *this frame's* pump. The pre-pump snapshot is
    // captured here, immediately before `streamer.pump()`, so the sweep
    // compares "what was loaded going into pump" vs "what's loaded coming
    // out" — both samples taken inside this helper. Replaces the prior
    // `last_loaded_cells_` member, whose lifetime spanned across frames
    // and could be staled by the same-frame place/delete consumer that
    // ran after pump (and after the snapshot was already overwritten with
    // the post-pump set). No-op'd when both stacks are empty — there's
    // nothing an evict could invalidate, so we skip the two
    // `loaded_cell_coords()` allocations on the hot path.
    void pump_and_sweep(const glm::vec3& cam_pos);

    // -----------------------------------------------------------------------
    // Camera state (PBD-024 / PBD-027)
    // -----------------------------------------------------------------------
    glm::vec3 map_cam_pos_ {0.f, 200.f, 0.f}; // .y is altitude
    static constexpr float MAP_CAM_YAW_DEG        = -90.f;
    static constexpr float MAP_CAM_ALT_MIN        = 40.f;
    static constexpr float MAP_CAM_ALT_MAX        = 1500.f;
    static constexpr float MAP_CAM_WHEEL_STEP     = 0.10f;
    static constexpr float MAP_CAM_PITCH_DEFAULT  = -89.f;
    static constexpr float MAP_CAM_PITCH_MIN      = -89.f;
    static constexpr float MAP_CAM_PITCH_MAX      = -15.f;
    static constexpr float MAP_CAM_TILT_STEP_DEG  = 5.f;
    float     map_cam_pitch_deg_ = MAP_CAM_PITCH_DEFAULT;
    TimePoint map_last_frame_ {};

    // -----------------------------------------------------------------------
    // PBD-030 palette + PBD-027 cell-jump input
    // -----------------------------------------------------------------------
    int          map_palette_selection_ = 0;
    bool         map_input_active_      = false;
    std::string  map_input_buf_;
    // Brief on-screen flash when an editor action failed. Set at commit-
    // time, decays in update(). PBD-051: paired with `map_err_kind_` so
    // the footer renders a cause-specific message instead of a one-size-
    // fits-all "BAD CELL COORD".
    float        map_input_err_flash_s_ = 0.f;
    MapErrorKind map_err_kind_          = MapErrorKind::None;

    // -----------------------------------------------------------------------
    // PBD-031 / PBD-032 mouse pick + tool + place/delete latches
    // -----------------------------------------------------------------------
    glm::vec2 map_mouse_world_xz_ {0.f, 0.f};
    bool      map_mouse_valid_    = false;
    bool      map_place_pending_  = false;
    MapTool   map_tool_           = MapTool::Place;
    bool      map_delete_pending_ = false;

    // -----------------------------------------------------------------------
    // PBD-042 / PBD-043 size + free scale + yaw
    // -----------------------------------------------------------------------
    SizePreset map_size_preset_      = SizePreset::Small;
    float      map_place_free_scale_ = 1.0f;
    float      map_place_yaw_deg_    = 0.0f;
    static constexpr float MAP_PLACE_FREE_SCALE_MIN  = 0.25f;
    static constexpr float MAP_PLACE_FREE_SCALE_MAX  = 8.0f;
    static constexpr float MAP_PLACE_FREE_SCALE_STEP = 0.10f;
    static constexpr float MAP_PLACE_YAW_DETENT_DEG  = 15.0f;

    // -----------------------------------------------------------------------
    // PBD-034 stacks + PBD-037 hover state
    // -----------------------------------------------------------------------
    // PBD-050: removed `last_loaded_cells_`. The pre-pump snapshot is now
    // sampled fresh inside `pump_and_sweep()` (same frame as the post-pump
    // sample it's diffed against), so we no longer need to carry the prior
    // frame's set across to this frame. Removing the member also kills the
    // edge case where the snapshot would be overwritten with the post-pump
    // state mid-update, before the place/delete consumer that runs later
    // in the same frame could observe the pre-pump set.
    std::vector<EditCommand> undo_stack_;
    std::vector<EditCommand> redo_stack_;
    MapBarHit                map_bar_hover_ {};

    bool back_request_pending_ = false;
    // PBD-054: edge-triggered PLAY HERE signal, set by the bar click or
    // the T hotkey, drained by Application via `consume_play_here_request`.
    bool play_here_request_pending_ = false;

    // Non-owning. Nulled in exit().
    const Deps* deps_ = nullptr;
};

} // namespace pengine
