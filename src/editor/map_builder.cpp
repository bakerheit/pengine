#include "editor/map_builder.h"

#include <SDL.h>
#include <glad/gl.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/log.h"
#include "platform/input.h"
#include "platform/window.h"
#include "render/camera.h"
#include "render/debug_draw.h"
#include "render/menu.h"
#include "render/shader.h"
#include "render/text.h"
#include "render/texture.h"
#include "scene/frustum.h"
#include "scene/scene.h"
#include "world/city_layout.h"
#include "world/heightmap.h"
#include "world/model_def.h"
#include "world/model_registry.h"
#include "world/road_grid.h"
#include "world/road_graph.h"
#include "world/streamer.h"

namespace pengine {

// ---------------------------------------------------------------------------
// Anonymous-namespace helpers (PBD-031 unproject + cell-square submit)
// ---------------------------------------------------------------------------
namespace {

// Approximate XZ half-size of the visible ground region under the top-down
// camera, given altitude and vertical FOV. Used to clip overlay generation
// to roughly what's on screen plus a small margin (1.25×) so partial cells
// at the edge still get drawn.
float visible_xz_half(float altitude_m, float fov_y_deg, float aspect) {
    float fov_y_rad = glm::radians(fov_y_deg);
    float half_z    = altitude_m * std::tan(fov_y_rad * 0.5f);
    float half_x    = half_z * aspect;
    return 1.25f * std::max(half_x, half_z);
}

// PBD-031: unproject a mouse pixel through the camera into a ray, then
// intersect that ray with the y=ground_y plane. Returns true and writes
// (x, z) on success; false if the ray is parallel to the plane (won't
// happen at the Map Builder's -89° pitch but the guard catches degenerate
// camera state).
bool unproject_mouse_to_ground(const glm::mat4& view_proj,
                                int mouse_x, int mouse_y,
                                int viewport_w, int viewport_h,
                                float ground_y,
                                glm::vec2& out_xz) {
    if (viewport_w <= 0 || viewport_h <= 0) return false;

    const float xn = (2.f * static_cast<float>(mouse_x) /
                       static_cast<float>(viewport_w)) - 1.f;
    const float yn = 1.f - (2.f * static_cast<float>(mouse_y) /
                            static_cast<float>(viewport_h));

    glm::mat4 inv = glm::inverse(view_proj);
    glm::vec4 near_h = inv * glm::vec4{xn, yn, -1.f, 1.f};
    glm::vec4 far_h  = inv * glm::vec4{xn, yn,  1.f, 1.f};
    if (near_h.w == 0.f || far_h.w == 0.f) return false;
    glm::vec3 near_w{near_h.x / near_h.w, near_h.y / near_h.w, near_h.z / near_h.w};
    glm::vec3 far_w {far_h.x  / far_h.w,  far_h.y  / far_h.w,  far_h.z  / far_h.w};

    glm::vec3 dir = far_w - near_w;
    if (std::abs(dir.y) < 1e-6f) return false;
    float t = (ground_y - near_w.y) / dir.y;
    if (t < 0.f) return false;

    glm::vec3 hit = near_w + dir * t;
    out_xz = {hit.x, hit.z};
    return true;
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

// ---------------------------------------------------------------------------
// Session enter / exit
// ---------------------------------------------------------------------------

void MapBuilder::enter(const Deps& deps) {
    deps_ = &deps;

    // Default: hover over the world centre at altitude high enough to show
    // several cells. WorldConfig::cell_size×world_cells gives the world
    // extent; centre is (cells*size/2). Altitude 600m with the default 60°
    // FOV shows ~700m of width on a 16:9 viewport — i.e. a ~3×3 chunk of
    // 256m cells, which is the streamer's load radius.
    WorldConfig wc;
    float cx = (static_cast<float>(wc.world_cells_x) * wc.cell_size) * 0.5f;
    float cz = (static_cast<float>(wc.world_cells_z) * wc.cell_size) * 0.5f;
    map_cam_pos_       = glm::vec3{cx, 600.f, cz};
    map_cam_pitch_deg_ = MAP_CAM_PITCH_DEFAULT;
    map_cam_yaw_deg_   = MAP_CAM_YAW_DEFAULT;
    map_last_frame_    = Clock::now();

    // PBD-030: start with the first palette entry highlighted. Selection
    // does not persist across MapBuilder enter/exit.
    map_palette_selection_ = 0;

    // PBD-031: clear any stale mouse-place state. `map_mouse_valid_`
    // becomes true again on the first mouse motion event.
    map_mouse_valid_   = false;
    map_place_pending_ = false;

    // PBD-032: default to Place mode so the user can plop immediately.
    map_tool_           = MapTool::Place;
    map_delete_pending_ = false;

    // PBD-042: default to the smallest preset so the first plop matches
    // pre-PBD-042 behaviour.
    map_size_preset_    = SizePreset::Small;

    // PBD-043: reset free scale + yaw on entry. Within a session these
    // persist across clicks / mode changes / Esc — but a fresh entry is
    // a clean slate, same policy as `map_size_preset_`.
    map_place_free_scale_ = 1.0f;
    map_place_yaw_deg_    = 0.0f;

    // PBD-034: undo/redo are intentionally NOT cross-session (architect's
    // "vanishes on app restart" doctrine). Re-entering MapBuilder is
    // treated as a fresh session for that purpose too: any leftover stack
    // from a previous session would point at cells we're not guaranteed
    // to still have loaded the same way.
    undo_stack_.clear();
    redo_stack_.clear();

    back_request_pending_       = false;
    play_here_request_pending_  = false;
    map_input_active_     = false;
    map_input_buf_.clear();
    map_input_err_flash_s_ = 0.f;
    map_err_kind_          = MapErrorKind::None;
}

void MapBuilder::exit() {
    // PBD-027: ensure the SDL text-input session is closed if we were
    // mid-typing when the state changed. Otherwise SDL would keep
    // synthesising TEXTINPUT events into the next state's pump.
    if (map_input_active_) {
        SDL_StopTextInput();
        map_input_active_ = false;
        map_input_buf_.clear();
    }
    // PBD-033: flush any cells the user edited during this session before
    // leaving. The evict path already saves dirty cells as they fall out
    // of the streamer's load radius, but if the user edits a cell that's
    // still loaded and Esc's out, those edits would otherwise live only
    // in memory until the next pan-induced evict.
    if (deps_) {
        deps_->streamer.save_all_dirty_cells();
    }
    deps_ = nullptr;
}

bool MapBuilder::consume_back_request() {
    bool b = back_request_pending_;
    back_request_pending_ = false;
    return b;
}

// PBD-054: pause/resume — used by the MapBuilder<->Playing round-trip so
// the user can test their edits in-game and come back to the editor with
// camera position, palette selection, tool mode, and undo stack intact.
// Unlike `exit()`, these do NOT touch dirty cells (left dirty for later
// auto-flush) or undo/redo (preserved across the round-trip).
void MapBuilder::suspend() {
    if (map_input_active_) {
        SDL_StopTextInput();
        map_input_active_ = false;
        map_input_buf_.clear();
    }
}

void MapBuilder::resume() {
    // Reset the wall-clock dt baseline so the first frame after resume
    // doesn't see an arbitrarily large dt from time spent in Playing.
    map_last_frame_ = Clock::now();
    // Edge signals are one-shot; clear them on resume so a stale request
    // from before the round-trip can't immediately re-fire.
    back_request_pending_      = false;
    play_here_request_pending_ = false;
}

bool MapBuilder::consume_play_here_request() {
    bool b = play_here_request_pending_;
    play_here_request_pending_ = false;
    return b;
}

// ---------------------------------------------------------------------------
// PBD-035 sorted palette
// ---------------------------------------------------------------------------

std::vector<const ModelDef*> MapBuilder::sorted_palette() const {
    const auto& defs = deps_->models.all();
    std::vector<const ModelDef*> ordered;
    ordered.reserve(defs.size());
    for (const auto& kv : defs) ordered.push_back(&kv.second);
    std::sort(ordered.begin(), ordered.end(),
              [](const ModelDef* a, const ModelDef* b) {
                  return a->id < b->id;
              });
    return ordered;
}

// ---------------------------------------------------------------------------
// PBD-032 bar layout / hit-test / DPI helper
// ---------------------------------------------------------------------------
//
// Layout (left-to-right):
//   [PLACE][DELETE]  [slot 0 NAME][slot 1 NAME]...  [S][M][L]
//
// Tool buttons: 72×72 px. Asset slots: 72×72 px. Size-preset buttons (S/M/L,
// PBD-042): 48×72 px, anchored on the right with a wider divider gap before
// the group. 12 px inter-region gap between adjacent regions; a wider gap
// separates the tool group from the palette and the palette from the
// preset group (PBD-038 divider pattern). PBD-036 renamed the per-slot
// label from "[NN] [F]" (id + flag chip) to "[NN NAME]" (id + asset name);
// the layout helper doesn't care, but anyone matching this comment against
// what they see on screen should.
//
// The bar sits ABOVE the centre footer (vh-200 → vh-100) rather than
// replacing it — see PBD-032 final report, premise check #4: keeping the
// footer in place preserves the cell-jump prompt and the "WASD PAN..."
// hint line so users don't lose orientation when the bar appears.

MapBuilder::MapBarLayout MapBuilder::compute_bar_layout() const {
    MapBarLayout L;
    const float vw = static_cast<float>(deps_->window.width());
    const float vh = static_cast<float>(deps_->window.height());

    const float bar_h    = 100.f;
    const float footer_h = 100.f;
    const float bar_top  = vh - footer_h - bar_h;
    const float bar_bot  = vh - footer_h;
    L.bar_min_px = {0.f, bar_top};
    L.bar_max_px = {vw, bar_bot};

    const float pad      = 14.f;
    const float gap      = 12.f;
    const float tool_sz  = 72.f;
    const float slot_sz  = 72.f;
    const float row_y0   = bar_top + (bar_h - tool_sz) * 0.5f;
    const float row_y1   = row_y0 + tool_sz;

    float x = pad;

    L.regions.push_back({{x, row_y0}, {x + tool_sz, row_y1},
                          MapBarHitKind::ToolButton,
                          static_cast<int>(MapTool::Place)});
    x += tool_sz + gap;
    L.regions.push_back({{x, row_y0}, {x + tool_sz, row_y1},
                          MapBarHitKind::ToolButton,
                          static_cast<int>(MapTool::Delete)});
    x += tool_sz + gap + 12.f;

    const float preset_sz       = 48.f;
    const float preset_gap_pre  = gap + 12.f;
    const int   n_presets       = 3;
    const float presets_width   = static_cast<float>(n_presets) * preset_sz
                                   + static_cast<float>(n_presets - 1) * gap;
    // PBD-054: rightmost slot is the PLAY HERE button. Sized wider than a
    // tool button to fit the two-word label at 20px glyph height. The
    // palette region must end one gap to the left of the presets, and the
    // presets must end one gap to the left of the play button.
    const float play_sz         = 140.f;
    const float play_gap_pre    = gap + 12.f;
    const float palette_x_limit = vw - pad - play_sz - play_gap_pre
                                   - presets_width - preset_gap_pre;

    const int n = static_cast<int>(deps_->models.size());
    for (int i = 0; i < n; ++i) {
        if (x + slot_sz > palette_x_limit) break;
        L.regions.push_back({{x, row_y0}, {x + slot_sz, row_y1},
                              MapBarHitKind::AssetSlot, i});
        x += slot_sz + gap;
    }

    x += preset_gap_pre;
    L.regions.push_back({{x, row_y0}, {x + preset_sz, row_y1},
                          MapBarHitKind::SizePreset,
                          static_cast<int>(SizePreset::Small)});
    x += preset_sz + gap;
    L.regions.push_back({{x, row_y0}, {x + preset_sz, row_y1},
                          MapBarHitKind::SizePreset,
                          static_cast<int>(SizePreset::Medium)});
    x += preset_sz + gap;
    L.regions.push_back({{x, row_y0}, {x + preset_sz, row_y1},
                          MapBarHitKind::SizePreset,
                          static_cast<int>(SizePreset::Large)});

    // PBD-054: PLAY HERE — pin to the right edge so window-resize doesn't
    // float the button mid-bar. The `index` field is unused for this kind.
    {
        const float px0 = vw - pad - play_sz;
        L.regions.push_back({{px0, row_y0}, {px0 + play_sz, row_y1},
                              MapBarHitKind::PlayHere, 0});
    }

    return L;
}

MapBuilder::MapBarHit MapBuilder::hit_test_bar(
        const MapBarLayout& layout, int x_px, int y_px) const {
    const float fx = static_cast<float>(x_px);
    const float fy = static_cast<float>(y_px);
    for (const auto& r : layout.regions) {
        if (fx >= r.min_px.x && fx <= r.max_px.x &&
            fy >= r.min_px.y && fy <= r.max_px.y) {
            return {r.kind, r.index};
        }
    }
    return {MapBarHitKind::None, 0};
}

void MapBuilder::scale_mouse_to_drawable(int mx_logical, int my_logical,
                                          int& mx_draw, int& my_draw) const {
    int win_w_logical = 0, win_h_logical = 0;
    SDL_GetWindowSize(deps_->window.sdl(), &win_w_logical, &win_h_logical);
    mx_draw = (win_w_logical > 0)
                  ? mx_logical * deps_->window.width() / win_w_logical
                  : mx_logical;
    my_draw = (win_h_logical > 0)
                  ? my_logical * deps_->window.height() / win_h_logical
                  : my_logical;
}

// ---------------------------------------------------------------------------
// Event pump (PBD-024 / PBD-027 / PBD-031 / PBD-032 / PBD-034 / PBD-041–043)
// ---------------------------------------------------------------------------

void MapBuilder::process_events(bool& running_out) {
    Input& input_ = deps_->input;
    Window& window_ = deps_->window;

    input_.begin_frame();

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                running_out = false;
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
                    for (const char* p = e.text.text; *p; ++p) {
                        const char c = *p;
                        const bool ok = (c >= '0' && c <= '9') ||
                                         c == ',' || c == ' ';
                        if (ok && map_input_buf_.size() < 16) {
                            map_input_buf_.push_back(c);
                        }
                    }
                } else {
                    input_.handle_event(e);
                }
                break;
            case SDL_KEYDOWN:
                if (map_input_active_) {
                    if (e.key.keysym.sym == SDLK_BACKSPACE &&
                        !map_input_buf_.empty()) {
                        map_input_buf_.pop_back();
                    }
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

    // PBD-051: both LCTRL and RCTRL count — same convention as the
    // undo/redo chord below.
    if ((input_.down(SDL_SCANCODE_LCTRL) || input_.down(SDL_SCANCODE_RCTRL))
        && input_.pressed(SDL_SCANCODE_Q))
        running_out = false;

    // PBD-034: undo / redo hotkeys. Both LCTRL and RCTRL count.
    if (!map_input_active_) {
        const bool undo_ctrl_down = input_.down(SDL_SCANCODE_LCTRL) ||
                                    input_.down(SDL_SCANCODE_RCTRL);
        if (undo_ctrl_down && input_.pressed(SDL_SCANCODE_Z)) apply_undo();
        if (undo_ctrl_down && input_.pressed(SDL_SCANCODE_Y)) apply_redo();
    }

    // PBD-031 / PBD-032 left-click dispatch.
    if (!map_input_active_ && input_.mouse_pressed(SDL_BUTTON_LEFT)) {
        int mx_draw = 0, my_draw = 0;
        scale_mouse_to_drawable(input_.mouse_x(), input_.mouse_y(),
                                 mx_draw, my_draw);
        MapBarLayout layout = compute_bar_layout();
        const bool inside_bar =
            static_cast<float>(mx_draw) >= layout.bar_min_px.x &&
            static_cast<float>(mx_draw) <= layout.bar_max_px.x &&
            static_cast<float>(my_draw) >= layout.bar_min_px.y &&
            static_cast<float>(my_draw) <= layout.bar_max_px.y;

        if (inside_bar) {
            MapBarHit hit = hit_test_bar(layout, mx_draw, my_draw);
            if (hit.kind == MapBarHitKind::ToolButton) {
                map_tool_ = static_cast<MapTool>(hit.index);
            } else if (hit.kind == MapBarHitKind::AssetSlot) {
                map_palette_selection_ = hit.index;
                map_tool_              = MapTool::Place;
            } else if (hit.kind == MapBarHitKind::SizePreset) {
                map_size_preset_ = static_cast<SizePreset>(hit.index);
            } else if (hit.kind == MapBarHitKind::PlayHere) {
                // PBD-054: signal Application to transition to Playing
                // with the player respawned at `map_cam_pos_.xz`.
                play_here_request_pending_ = true;
            }
            // Inside-bar-but-no-region: swallow the click.
        } else {
            if (map_tool_ == MapTool::Place) {
                map_place_pending_ = true;
            } else {
                map_delete_pending_ = true;
            }
        }
    }

    if (map_input_active_) {
        if (input_.pressed(SDL_SCANCODE_RETURN) ||
            input_.pressed(SDL_SCANCODE_KP_ENTER)) {
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
                long cx_l = std::clamp<long>(x, 0, wc.world_cells_x - 1);
                long cz_l = std::clamp<long>(z, 0, wc.world_cells_z - 1);
                bool clamped = (cx_l != x || cz_l != z);

                map_cam_pos_.x = (static_cast<float>(cx_l) + 0.5f) * wc.cell_size;
                map_cam_pos_.z = (static_cast<float>(cz_l) + 0.5f) * wc.cell_size;
                if (clamped) {
                    map_input_err_flash_s_ = 0.75f;
                    map_err_kind_          = MapErrorKind::BadCellCoord;
                }
                PE_INFO("Map Builder: jump to cell (%ld, %ld)%s",
                        cx_l, cz_l, clamped ? " [clamped]" : "");
            } else {
                map_input_err_flash_s_ = 0.75f;
                map_err_kind_          = MapErrorKind::BadCellCoord;
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
        map_input_active_ = true;
        map_input_buf_.clear();
        SDL_StartTextInput();
        return; // suppress any other key that fired this frame
    }

    if (input_.pressed(SDL_SCANCODE_ESCAPE) ||
        input_.pressed(SDL_SCANCODE_BACKSPACE) ||
        input_.pressed(SDL_SCANCODE_B)) {
        back_request_pending_ = true;
    }

    // PBD-054: T = "Test in game". Same effect as clicking PLAY HERE
    // — Application drains the signal, respawns the player on foot at
    // `cam_pos().xz`, and transitions to Playing. Suppressed while the
    // cell-jump prompt is open (handled by the early-return above).
    if (input_.pressed(SDL_SCANCODE_T)) {
        play_here_request_pending_ = true;
    }

    // PBD-041 tool-switch hotkeys.
    if (!map_input_active_) {
        if (input_.pressed(SDL_SCANCODE_1)) map_tool_ = MapTool::Place;
        if (input_.pressed(SDL_SCANCODE_2)) map_tool_ = MapTool::Delete;
        // PBD-042 size-preset hotkeys.
        if (input_.pressed(SDL_SCANCODE_3)) map_size_preset_ = SizePreset::Small;
        if (input_.pressed(SDL_SCANCODE_4)) map_size_preset_ = SizePreset::Medium;
        if (input_.pressed(SDL_SCANCODE_5)) map_size_preset_ = SizePreset::Large;

        // Reset camera to defaults — useful when you've panned/zoomed/yawed
        // far enough to feel lost. Doesn't reset palette or tool mode; just
        // pose. Home is the conventional "reset view" key.
        if (input_.pressed(SDL_SCANCODE_HOME)) {
            WorldConfig wc;
            float cx = (static_cast<float>(wc.world_cells_x) * wc.cell_size) * 0.5f;
            float cz = (static_cast<float>(wc.world_cells_z) * wc.cell_size) * 0.5f;
            map_cam_pos_       = glm::vec3{cx, 600.f, cz};
            map_cam_pitch_deg_ = MAP_CAM_PITCH_DEFAULT;
            map_cam_yaw_deg_   = MAP_CAM_YAW_DEFAULT;
        }

        // PBD-043 yaw-rotation hotkeys. Q rotates CCW; E rotates CW. 15°
        // detents per press. Ctrl+Q is the app-quit chord and fires on
        // either LCTRL or RCTRL (PBD-051), so we gate Q rotation on
        // !(LCTRL || RCTRL) to keep the chord unambiguous from either
        // side of the keyboard.
        const bool ctrl_down = input_.down(SDL_SCANCODE_LCTRL) ||
                               input_.down(SDL_SCANCODE_RCTRL);
        if (!ctrl_down && input_.pressed(SDL_SCANCODE_Q))
            map_place_yaw_deg_ += MAP_PLACE_YAW_DETENT_DEG;
        if (input_.pressed(SDL_SCANCODE_E))
            map_place_yaw_deg_ -= MAP_PLACE_YAW_DETENT_DEG;
        if (map_place_yaw_deg_ > 180.f)  map_place_yaw_deg_ -= 360.f;
        if (map_place_yaw_deg_ <= -180.f) map_place_yaw_deg_ += 360.f;
    }

    // PBD-030 asset-palette navigation.
    const int palette_count = static_cast<int>(deps_->models.size());
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

// ---------------------------------------------------------------------------
// Update (PBD-024 camera + PBD-031 unproject + PBD-032 click consumption +
//         PBD-034 evict sweep)
// ---------------------------------------------------------------------------

void MapBuilder::update(float dt) {
    Input& input_ = deps_->input;
    Window& window_ = deps_->window;
    Streamer& streamer_ = deps_->streamer;
    Scene& scene_ = deps_->scene;
    Camera& camera_ = deps_->camera;

    // Decay the cell-jump error flash regardless of input mode so it
    // always disappears after ~0.75s.
    if (map_input_err_flash_s_ > 0.f) {
        map_input_err_flash_s_ -= dt;
        if (map_input_err_flash_s_ < 0.f) {
            map_input_err_flash_s_ = 0.f;
            // PBD-051: clear the kind once the visual flash decays so a
            // stale cause can't be read out of the (now-zero-timer) state.
            map_err_kind_          = MapErrorKind::None;
        }
    }

    // PBD-027: while a cell-jump input is active, suppress pan/zoom.
    if (map_input_active_) {
        // PBD-050: route through `pump_and_sweep` so the eviction diff is
        // pre-pump-vs-post-pump within this same frame, not last-frame's
        // post-pump vs this frame's post-pump (the old ordering would
        // overwrite the snapshot before the place/delete consumer below
        // could push a fresh command targeting an already-evicted cell).
        pump_and_sweep(map_cam_pos_);
        scene_.update();
        return;
    }

    // ----- Wheel: free scale (Shift held), tilt (R held), or zoom (default) -----
    float wy = input_.wheel_y();
    if (wy != 0.f) {
        const bool shift_down = input_.down(SDL_SCANCODE_LSHIFT) ||
                                input_.down(SDL_SCANCODE_RSHIFT);
        if (shift_down) {
            float factor = std::pow(1.f + MAP_PLACE_FREE_SCALE_STEP, wy);
            map_place_free_scale_ *= factor;
            if (map_place_free_scale_ < MAP_PLACE_FREE_SCALE_MIN)
                map_place_free_scale_ = MAP_PLACE_FREE_SCALE_MIN;
            if (map_place_free_scale_ > MAP_PLACE_FREE_SCALE_MAX)
                map_place_free_scale_ = MAP_PLACE_FREE_SCALE_MAX;
        } else if (input_.down(SDL_SCANCODE_R)) {
            map_cam_pitch_deg_ += wy * MAP_CAM_TILT_STEP_DEG;
            if (map_cam_pitch_deg_ < MAP_CAM_PITCH_MIN) map_cam_pitch_deg_ = MAP_CAM_PITCH_MIN;
            if (map_cam_pitch_deg_ > MAP_CAM_PITCH_MAX) map_cam_pitch_deg_ = MAP_CAM_PITCH_MAX;
        } else {
            // Ctrl+wheel uses a much bigger multiplicative step for quick
            // traversal across the wide altitude range. Bare wheel keeps the
            // fine-grained step for precision work.
            const bool ctrl_down = input_.down(SDL_SCANCODE_LCTRL) ||
                                   input_.down(SDL_SCANCODE_RCTRL);
            const float step = ctrl_down ? MAP_CAM_WHEEL_FAST
                                         : MAP_CAM_WHEEL_STEP;
            float factor = std::pow(1.f - step, wy);
            map_cam_pos_.y *= factor;
            if (map_cam_pos_.y < MAP_CAM_ALT_MIN) map_cam_pos_.y = MAP_CAM_ALT_MIN;
            if (map_cam_pos_.y > MAP_CAM_ALT_MAX) map_cam_pos_.y = MAP_CAM_ALT_MAX;
        }
    }

    // ----- Yaw (middle-mouse drag) -----
    // Middle-mouse held + mouse motion rotates the camera yaw. Sensitivity is
    // degrees per pixel of horizontal mouse delta. Mouse_dx is per-frame delta
    // from Input; only nonzero when the mouse actually moved this frame.
    //
    // Edge-triggered SDL_SetRelativeMouseMode toggles on press/release: while
    // dragging, the cursor is hidden and pinned, so mouse_dx keeps coming in
    // without the OS cursor wandering off-screen. Standard pattern; without
    // it the cursor visibly jumps around while you yaw, which felt rough.
    // Either middle-mouse-drag (3-button mice) or right-mouse-drag (universal,
    // works on every MacBook trackpad via two-finger or corner click) rotates
    // the camera yaw. Both bindings are exposed because users have hard
    // hardware preferences here and the cost of supporting both is one extra
    // OR per branch.
    const bool yaw_drag_pressed  = input_.mouse_pressed(SDL_BUTTON_MIDDLE) ||
                                    input_.mouse_pressed(SDL_BUTTON_RIGHT);
    const bool yaw_drag_released = input_.mouse_released(SDL_BUTTON_MIDDLE) ||
                                    input_.mouse_released(SDL_BUTTON_RIGHT);
    const bool yaw_drag_down     = input_.mouse_down(SDL_BUTTON_MIDDLE) ||
                                    input_.mouse_down(SDL_BUTTON_RIGHT);
    if (yaw_drag_pressed)  SDL_SetRelativeMouseMode(SDL_TRUE);
    if (yaw_drag_released) SDL_SetRelativeMouseMode(SDL_FALSE);
    if (yaw_drag_down) {
        map_cam_yaw_deg_ += input_.mouse_dx() * MAP_CAM_YAW_DRAG_DEG_PER_PX;
    }

    // Keyboard yaw — Left/Right arrows rotate the view at a fixed rate while
    // held. Up/Down stay reserved for palette nav. This is the universal-
    // hardware fallback for users without a usable middle mouse button
    // (laptop trackpads often don't support MMB-drag).
    constexpr float YAW_KEY_DEG_PER_SEC = 90.f;
    if (input_.down(SDL_SCANCODE_LEFT))  map_cam_yaw_deg_ -= YAW_KEY_DEG_PER_SEC * dt;
    if (input_.down(SDL_SCANCODE_RIGHT)) map_cam_yaw_deg_ += YAW_KEY_DEG_PER_SEC * dt;

    // Wrap yaw into (-180, 180] for compact footer/caption rendering and to
    // keep the float bounded if the user spins forever.
    while (map_cam_yaw_deg_ >   180.f) map_cam_yaw_deg_ -= 360.f;
    while (map_cam_yaw_deg_ <= -180.f) map_cam_yaw_deg_ += 360.f;

    // ----- Pan (WASD, camera-relative) -----
    // Forward is the camera's yaw projected onto the XZ plane. At the default
    // yaw=-90 this is (0,0,-1) — same as the pre-yaw-rotation behaviour, so
    // existing muscle memory is preserved as long as you don't drag-yaw.
    const float yaw_rad = glm::radians(map_cam_yaw_deg_);
    glm::vec3 fwd_xz{std::cos(yaw_rad), 0.f, std::sin(yaw_rad)};
    glm::vec3 rgt_xz{-fwd_xz.z,         0.f, fwd_xz.x};

    glm::vec3 vel{0.f};
    if (input_.down(SDL_SCANCODE_W)) vel += fwd_xz;
    if (input_.down(SDL_SCANCODE_S)) vel -= fwd_xz;
    if (input_.down(SDL_SCANCODE_D)) vel += rgt_xz;
    if (input_.down(SDL_SCANCODE_A)) vel -= rgt_xz;

    if (glm::length(vel) > 0.f) {
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

    // PBD-050: pump + eviction sweep, with the pre-pump snapshot taken
    // inside the helper (i.e. immediately before `streamer.pump()` runs
    // for this frame). The prior implementation diffed last frame's
    // post-pump set against this frame's post-pump set, which was correct
    // in isolation but tied the freshness of `last_loaded_cells_` to the
    // ordering of update() — and the place/delete consumer below pushes
    // new commands AFTER the snapshot was already advanced.
    pump_and_sweep(map_cam_pos_);
    scene_.update();

    // PBD-031: re-run the mouse unproject here too, using the camera
    // state we just updated. The render path also unprojects (for the
    // inspector crosshair and ghost marker) — doing it twice is cheap
    // (one mat4 inverse) and lets a click that lands the same frame the
    // mouse first moved still find a valid XZ.
    {
        Camera ucam = camera_;
        ucam.position = map_cam_pos_;
        ucam.yaw      = map_cam_yaw_deg_;
        ucam.pitch    = map_cam_pitch_deg_;
        const float aspect = window_.height() > 0
                              ? static_cast<float>(window_.width()) /
                                 static_cast<float>(window_.height())
                              : 1.f;
        glm::mat4 vp = ucam.view_proj(aspect);

        int win_w_logical = 0, win_h_logical = 0;
        SDL_GetWindowSize(window_.sdl(), &win_w_logical, &win_h_logical);
        const int mx_draw = (win_w_logical > 0)
            ? input_.mouse_x() * window_.width()  / win_w_logical
            : input_.mouse_x();
        const int my_draw = (win_h_logical > 0)
            ? input_.mouse_y() * window_.height() / win_h_logical
            : input_.mouse_y();

        glm::vec2 mouse_xz;
        float ground_y = 0.f;
        bool ok = false;
        for (int iter = 0; iter < 4; ++iter) {
            if (!unproject_mouse_to_ground(vp,
                                            mx_draw, my_draw,
                                            window_.width(), window_.height(),
                                            ground_y, mouse_xz)) {
                break;
            }
            ok = true;
            float new_y = Heightmap::sample(mouse_xz.x, mouse_xz.y);
            if (std::abs(new_y - ground_y) < 0.05f) break;
            ground_y = new_y;
        }
        if (ok) {
            map_mouse_valid_   = true;
            map_mouse_world_xz_ = mouse_xz;
        }
    }

    // PBD-037: refresh bar hover state.
    {
        int mx_draw = 0, my_draw = 0;
        scale_mouse_to_drawable(input_.mouse_x(), input_.mouse_y(),
                                 mx_draw, my_draw);
        MapBarLayout layout = compute_bar_layout();
        map_bar_hover_ = hit_test_bar(layout, mx_draw, my_draw);
    }

    // PBD-031 place consumption.
    if (map_place_pending_) {
        map_place_pending_ = false;
        if (map_mouse_valid_) {
            CellCoord cell = world_to_cell(map_mouse_world_xz_.x,
                                            map_mouse_world_xz_.y,
                                            wc.cell_size);

            std::vector<const ModelDef*> ordered = sorted_palette();

            if (!ordered.empty() &&
                map_palette_selection_ >= 0 &&
                map_palette_selection_ < static_cast<int>(ordered.size())) {
                const ModelDef* sel = ordered[static_cast<std::size_t>(
                                                map_palette_selection_)];
                InstanceDef inst;
                inst.model_id = sel->id;
                // Scale first so position can anchor by the local AABB's
                // bottom Y. The proc:cube mesh has bounds_min.y = -0.5
                // (centered at origin); setting position.y = ground_y
                // directly put the cube CENTER at ground and buried half
                // the building. Anchor at ground_y - min_y*scale_y so the
                // mesh's bottom rests on the heightmap surface — matches
                // what city_layout.cpp::push_building does.
                //
                // S/M/L is hard-tuned to building footprints (see
                // size_preset_factor: house / apartment / tower). Applying
                // those factors to a road tile or sidewalk stretches it
                // into a tower-shaped slab, so gate the preset on
                // ModelFlag::Building. Non-building palette items place at
                // their native mesh scale × free-scale (wheel) only.
                const bool sel_is_building =
                    (sel->flags & ModelFlag::Building) != 0;
                const glm::vec3 preset = sel_is_building
                    ? size_preset_factor(map_size_preset_)
                    : glm::vec3{1.f, 1.f, 1.f};
                inst.transform.scale *= preset * map_place_free_scale_;
                // Match proc:facade window-tiling. push_building (in
                // city_layout.cpp) writes uv_scale_override so the facade
                // texture repeats at WINDOW_TILE_M (~3 m / window). The
                // editor used to leave the override at (0,0), which means
                // "use the model's default uv_scale" — for proc:facade that
                // stretches one window across the whole wall, so plopped
                // buildings looked like 12 m windows instead of the proc
                // city's grid of small ones. Mirror the formula here so
                // editor-placed buildings match the rest of the skyline.
                if (sel_is_building) {
                    inst.uv_scale_override = {
                        inst.transform.scale.x / WINDOW_TILE_M,
                        inst.transform.scale.y / WINDOW_TILE_M,
                    };
                }
                const float place_ground_y = Heightmap::sample(
                    map_mouse_world_xz_.x, map_mouse_world_xz_.y);
                inst.transform.position = glm::vec3{
                    map_mouse_world_xz_.x,
                    place_ground_y - sel->local_bounds.min.y * inst.transform.scale.y,
                    map_mouse_world_xz_.y};
                inst.transform.rotation = glm::angleAxis(
                    glm::radians(map_place_yaw_deg_),
                    glm::vec3{0.f, 1.f, 0.f});
                bool ok = streamer_.add_instance(cell, inst);
                if (ok) {
                    PE_INFO("Map Builder: placed model id=%u at "
                            "(%.1f, %.1f, %.1f) in cell (%d,%d)",
                            sel->id,
                            inst.transform.position.x,
                            inst.transform.position.y,
                            inst.transform.position.z,
                            cell.x, cell.z);
                    scene_.update();
                    push_edit_command({EditCommand::Kind::Place, cell, inst});
                } else {
                    // PBD-051: tag the cause so the footer can say
                    // "PLACE REJECTED" instead of the misleading
                    // "BAD CELL COORD".
                    map_input_err_flash_s_ = 0.75f;
                    map_err_kind_          = MapErrorKind::PlaceRejected;
                    PE_INFO("Map Builder: placement rejected (cell (%d,%d) "
                            "not loaded or model %u unresolved)",
                            cell.x, cell.z, sel->id);
                }
            }
        }
    }

    // PBD-032 delete consumption.
    if (map_delete_pending_) {
        map_delete_pending_ = false;
        if (map_mouse_valid_) {
            Streamer::PickResult pick = streamer_.query_instance_at(
                map_mouse_world_xz_.x, map_mouse_world_xz_.y);
            // PBD-049-era restriction limited bulldoze to ModelFlag::Building
            // because road tiles weren't user-authored — they came from the
            // procedural layout and the road graph (used by AI / heightmap)
            // wasn't coupled to the instance list, so deleting a slab was
            // cosmetic-only. The editor now places road tiles as ordinary
            // instances, so the gate became a blocker on the user's own
            // work. Removed: any picked instance is deletable.
            if (pick.hit) {
                InstanceDef saved      = pick.instance;
                CellCoord   saved_cell = pick.cell;
                bool ok = streamer_.remove_instance(pick.cell,
                                                     pick.instance_index);
                if (ok) {
                    const ModelDef* mdef =
                        deps_->models.get(pick.instance.model_id);
                    const AABB& wa = pick.world_aabb;
                    PE_INFO("Map Builder: deleted model id=%u (%s) flags=%s "
                            "from cell (%d,%d) idx %zu  click=(%.2f,%.2f)  "
                            "aabb x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f]",
                            pick.instance.model_id,
                            mdef ? mdef->name.c_str() : "?",
                            mdef ? format_model_flags(mdef->flags) : "?",
                            pick.cell.x, pick.cell.z,
                            pick.instance_index,
                            map_mouse_world_xz_.x, map_mouse_world_xz_.y,
                            wa.min.x, wa.max.x,
                            wa.min.y, wa.max.y,
                            wa.min.z, wa.max.z);
                    push_edit_command(
                        {EditCommand::Kind::Delete, saved_cell, saved});
                } else {
                    map_input_err_flash_s_ = 0.75f;
                    map_err_kind_          = MapErrorKind::DeleteRejected;
                    PE_INFO("Map Builder: delete rejected (cell (%d,%d) "
                            "index %zu unreachable)",
                            pick.cell.x, pick.cell.z, pick.instance_index);
                }
            } else {
                // Click landed on bare terrain — no instance under the
                // cursor in the 3x3 cell window. Flash + log so the user
                // can tell their click was registered and we can debug
                // the "click building, nothing happens" case. Includes
                // the cell coord so we can correlate with what's loaded.
                map_input_err_flash_s_ = 0.75f;
                map_err_kind_          = MapErrorKind::DeleteRejected;
                CellCoord cc = world_to_cell(map_mouse_world_xz_.x,
                                              map_mouse_world_xz_.y,
                                              wc.cell_size);
                PE_INFO("Map Builder: delete missed (no instance AABB contains "
                        "click=(%.2f,%.2f) in cell (%d,%d) or neighbours)",
                        map_mouse_world_xz_.x, map_mouse_world_xz_.y,
                        cc.x, cc.z);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// PBD-034 undo / redo helpers
// ---------------------------------------------------------------------------

void MapBuilder::push_edit_command(EditCommand cmd) {
    redo_stack_.clear();
    if (undo_stack_.size() >= MAX_UNDO_DEPTH) {
        undo_stack_.erase(undo_stack_.begin());
    }
    undo_stack_.push_back(std::move(cmd));
}

void MapBuilder::apply_undo() {
    if (undo_stack_.empty()) return;
    Streamer& streamer_ = deps_->streamer;
    Scene&    scene_    = deps_->scene;

    EditCommand cmd = undo_stack_.back();
    undo_stack_.pop_back();

    bool ok = false;
    if (cmd.kind == EditCommand::Kind::Place) {
        const glm::vec3& pos = cmd.instance.transform.position;
        Streamer::PickResult pick =
            streamer_.query_instance_at(pos.x, pos.z);
        if (pick.hit && pick.cell == cmd.cell &&
            pick.instance.model_id == cmd.instance.model_id) {
            ok = streamer_.remove_instance(pick.cell, pick.instance_index);
        }
        if (ok) {
            PE_INFO("Map Builder: undo Place model id=%u cell (%d,%d)",
                    cmd.instance.model_id, cmd.cell.x, cmd.cell.z);
        } else {
            PE_INFO("Map Builder: undo Place failed (cell (%d,%d) not "
                    "loaded or instance gone)",
                    cmd.cell.x, cmd.cell.z);
        }
    } else {
        ok = streamer_.add_instance(cmd.cell, cmd.instance);
        if (ok) {
            PE_INFO("Map Builder: undo Delete model id=%u cell (%d,%d)",
                    cmd.instance.model_id, cmd.cell.x, cmd.cell.z);
            scene_.update();
        } else {
            PE_INFO("Map Builder: undo Delete failed (cell (%d,%d) not "
                    "loaded or model unresolved)",
                    cmd.cell.x, cmd.cell.z);
        }
    }
    if (ok) {
        redo_stack_.push_back(std::move(cmd));
    } else {
        map_input_err_flash_s_ = 0.75f;
        map_err_kind_          = MapErrorKind::UndoFailed;
    }
}

void MapBuilder::apply_redo() {
    if (redo_stack_.empty()) return;
    Streamer& streamer_ = deps_->streamer;
    Scene&    scene_    = deps_->scene;

    EditCommand cmd = redo_stack_.back();
    redo_stack_.pop_back();

    bool ok = false;
    if (cmd.kind == EditCommand::Kind::Place) {
        ok = streamer_.add_instance(cmd.cell, cmd.instance);
        if (ok) {
            scene_.update();
            PE_INFO("Map Builder: redo Place model id=%u cell (%d,%d)",
                    cmd.instance.model_id, cmd.cell.x, cmd.cell.z);
        } else {
            PE_INFO("Map Builder: redo Place failed cell (%d,%d)",
                    cmd.cell.x, cmd.cell.z);
        }
    } else {
        const glm::vec3& pos = cmd.instance.transform.position;
        Streamer::PickResult pick =
            streamer_.query_instance_at(pos.x, pos.z);
        if (pick.hit && pick.cell == cmd.cell &&
            pick.instance.model_id == cmd.instance.model_id) {
            ok = streamer_.remove_instance(pick.cell, pick.instance_index);
        }
        if (ok) {
            PE_INFO("Map Builder: redo Delete model id=%u cell (%d,%d)",
                    cmd.instance.model_id, cmd.cell.x, cmd.cell.z);
        } else {
            PE_INFO("Map Builder: redo Delete failed cell (%d,%d)",
                    cmd.cell.x, cmd.cell.z);
        }
    }
    if (ok) {
        undo_stack_.push_back(std::move(cmd));
    } else {
        map_input_err_flash_s_ = 0.75f;
        map_err_kind_          = MapErrorKind::RedoFailed;
    }
}

void MapBuilder::drop_evicted_commands(
    const std::unordered_set<CellCoord, CellCoordHash>& currently_loaded) {
    auto drop_from = [&](std::vector<EditCommand>& stack) {
        std::size_t before = stack.size();
        stack.erase(
            std::remove_if(stack.begin(), stack.end(),
                [&](const EditCommand& c) {
                    return currently_loaded.count(c.cell) == 0;
                }),
            stack.end());
        return before - stack.size();
    };
    std::size_t dropped_undo = drop_from(undo_stack_);
    std::size_t dropped_redo = drop_from(redo_stack_);
    if (dropped_undo || dropped_redo) {
        PE_INFO("Map Builder: dropped %zu undo / %zu redo entries on "
                "cell evict",
                dropped_undo, dropped_redo);
    }
}

// PBD-050: pre-pump snapshot + pump + post-pump diff, all in one place.
//
// The race this fixes: pre-PBD-050, `update()` pumped first, then sampled
// `loaded_cell_coords()` into `now_set`, diffed `now_set` against the
// member-cached `last_loaded_cells_` (from the END of last frame's
// update), and finally overwrote `last_loaded_cells_` with `now_set`. The
// place/delete consumer ran AFTER all of that in the same `update()`.
// That left a window where:
//   1. This frame's pump evicts cell C.
//   2. The sweep correctly drops any pre-existing undo entries for C and
//      advances `last_loaded_cells_` to the post-pump set (no C).
//   3. The place/delete consumer runs and — for paths that don't bottom
//      out in `streamer.add_instance` rejecting the cell (e.g. a delete
//      whose `query_instance_at` had a hit pre-pump but whose
//      `remove_instance` reaches a now-evicted cell, or a future edit
//      path that synthesises an InstanceDef without round-tripping the
//      streamer's loaded-check) — could still push an undo entry whose
//      `.cell` is no longer loaded. The next frame's sweep would compare
//      `last_loaded_cells_` (no C) against post-pump (no C) and see "no
//      eviction this frame", so the stale entry survived.
//
// Fixing this by sampling pre-pump inside the same call as pump itself
// makes the diff frame-local: pre and post are both taken on either side
// of one pump invocation, independent of when `last_loaded_cells_` would
// have been overwritten. The member is gone (see header), so there's no
// cross-frame state for the place/delete consumer to invalidate.
//
// Allocation note: two `loaded_cell_coords()` calls + a hash set per
// pumping frame. The previous code did one `loaded_cell_coords()` + a
// hash set in the same shape, so the additional cost is one
// `std::vector<CellCoord>` for the pre-pump snapshot — bounded by
// `(2r+1)^2` cells (≤9 in current `WorldConfig`). Negligible vs pump
// itself, which does GL uploads on cell loads.
void MapBuilder::pump_and_sweep(const glm::vec3& cam_pos) {
    Streamer& streamer_ = deps_->streamer;

    // Fast path: when there's nothing on either stack, an eviction can't
    // invalidate anything. Skip the snapshot pair entirely.
    if (undo_stack_.empty() && redo_stack_.empty()) {
        streamer_.pump(cam_pos);
        return;
    }

    std::vector<CellCoord> pre = streamer_.loaded_cell_coords();
    streamer_.pump(cam_pos);
    std::vector<CellCoord> post = streamer_.loaded_cell_coords();

    std::unordered_set<CellCoord, CellCoordHash> post_set;
    post_set.reserve(post.size());
    for (CellCoord c : post) post_set.insert(c);

    bool any_evicted = false;
    for (CellCoord c : pre) {
        if (!post_set.count(c)) { any_evicted = true; break; }
    }
    if (any_evicted) drop_evicted_commands(post_set);
}

// ---------------------------------------------------------------------------
// Render (PBD-024 world + PBD-025 overlay + PBD-026/031 inspector +
//         PBD-032 bar + PBD-036 caption + PBD-040 legend)
// ---------------------------------------------------------------------------

void MapBuilder::render() {
    Window&     window_      = deps_->window;
    Input&      input_       = deps_->input;
    Camera&     camera_      = deps_->camera;
    Scene&      scene_       = deps_->scene;
    Streamer&   streamer_    = deps_->streamer;
    RoadGraph&  road_graph_  = deps_->road_graph;
    DebugDraw&  debug_draw_  = deps_->debug_draw;
    Menu&       menu_        = deps_->menu;
    Text&       text_        = deps_->text;
    Shader&     lit_shader_  = deps_->lit_shader;
    Texture&    checker_tex_ = deps_->checker_tex;
    ModelRegistry& world_models_ = deps_->models;

    glClearColor(0.45f, 0.65f, 0.85f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera_.position = map_cam_pos_;
    camera_.yaw      = map_cam_yaw_deg_;
    camera_.pitch    = map_cam_pitch_deg_;

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
        const float y_lift    = 0.5f;

        const float half = visible_xz_half(map_cam_pos_.y, 60.f, aspect);
        const float wx0  = map_cam_pos_.x - half;
        const float wx1  = map_cam_pos_.x + half;
        const float wz0  = map_cam_pos_.z - half;
        const float wz1  = map_cam_pos_.z + half;

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

        debug_draw_.clear();
        for (CellCoord c : loaded) {
            float x0 = c.x * cell_size, x1 = x0 + cell_size;
            float z0 = c.z * cell_size, z1 = z0 + cell_size;
            if (x1 < wx0 || x0 > wx1 || z1 < wz0 || z0 > wz1) continue;
            submit_cell_square(debug_draw_, c, cell_size, y_lift);
        }
        debug_draw_.flush(vp, glm::vec3{0.15f, 1.f, 0.95f});

        debug_draw_.clear();
        const float seg_len = 8.f;

        int i0 = static_cast<int>(std::floor(wx0 / ROAD_PITCH));
        int i1 = static_cast<int>(std::ceil (wx1 / ROAD_PITCH));
        int j0 = static_cast<int>(std::floor(wz0 / ROAD_PITCH));
        int j1 = static_cast<int>(std::ceil (wz1 / ROAD_PITCH));

        auto sample_road_pt = [&](float x, float z) {
            return glm::vec3{x, Heightmap::sample(x, z) + y_lift, z};
        };

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

        debug_draw_.clear();
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

    // ---- Inspector (PBD-026, mouse-cursor retrofit in PBD-031) ------------
    {
        const float wx = map_mouse_valid_ ? map_mouse_world_xz_.x : map_cam_pos_.x;
        const float wz = map_mouse_valid_ ? map_mouse_world_xz_.y : map_cam_pos_.z;
        Streamer::PickResult pick = streamer_.query_instance_at(wx, wz);

        debug_draw_.clear();
        {
            const float y_lift   = 1.0f;
            const float cs       = std::clamp(map_cam_pos_.y * 0.012f, 1.5f, 6.f);
            const float y        = Heightmap::sample(wx, wz) + y_lift;
            debug_draw_.line({wx - cs, y, wz}, {wx + cs, y, wz});
            debug_draw_.line({wx, y, wz - cs}, {wx, y, wz + cs});
        }
        const glm::vec3 crosshair_col = (map_tool_ == MapTool::Place)
            ? glm::vec3{0.2f, 1.0f, 0.2f}
            : glm::vec3{1.0f, 0.2f, 0.2f};
        debug_draw_.flush(vp, crosshair_col);

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

            debug_draw_.clear();
            debug_draw_.box(pick.world_aabb.min, pick.world_aabb.max);
            debug_draw_.flush(vp, glm::vec3{1.f, 0.3f, 1.f});
        } else {
            std::snprintf(line_buf[0], sizeof(line_buf[0]), "INSPECTOR");
            std::snprintf(line_buf[1], sizeof(line_buf[1]), "HOVER AN OBJECT");
            lines[0] = line_buf[0];
            lines[1] = line_buf[1];
            nlines   = 2;
            text_col = glm::vec3{0.92f, 0.94f, 0.98f};
        }

        Text::DrawState tl;
        tl.lines              = lines;
        tl.count              = nlines;
        tl.origin_top_left_px = {32.f,
                                  static_cast<float>(window_.height()) -
                                      200.f - 16.f -
                                      static_cast<float>(nlines) * 36.f};
        tl.glyph_h_px         = 26.f;
        tl.color              = text_col;
        tl.bg_color           = glm::vec4{0.05f, 0.05f, 0.07f, 0.70f};
        tl.bg_padding_px      = 14.f;
        tl.viewport_size_px   = {static_cast<float>(window_.width()),
                                  static_cast<float>(window_.height())};
        text_.draw_lines(tl);
    }

    // ---- Tool preview (PBD-031 + PBD-032) ---------------------------------
    if (map_mouse_valid_) {
        if (map_tool_ == MapTool::Place) {
            std::vector<const ModelDef*> ordered = sorted_palette();
            if (!ordered.empty() &&
                map_palette_selection_ >= 0 &&
                map_palette_selection_ < static_cast<int>(ordered.size())) {
                const ModelDef* sel = ordered[static_cast<std::size_t>(
                                                map_palette_selection_)];
                AABB local{sel->local_bounds.min, sel->local_bounds.max};
                Transform t;
                // Same ground-anchor math as the place handler — without
                // this the ghost preview sat with its center at ground
                // and half-buried, so the visible upper half appeared
                // offset upward from the cursor. Same Building-gate on
                // the S/M/L preset so the ghost matches what the place
                // commit will actually produce for non-building palette
                // items (roads, sidewalks, props).
                const bool sel_is_building =
                    (sel->flags & ModelFlag::Building) != 0;
                const glm::vec3 preset = sel_is_building
                    ? size_preset_factor(map_size_preset_)
                    : glm::vec3{1.f, 1.f, 1.f};
                t.scale *= preset * map_place_free_scale_;
                const float ghost_ground_y = Heightmap::sample(
                    map_mouse_world_xz_.x, map_mouse_world_xz_.y);
                t.position = glm::vec3{
                    map_mouse_world_xz_.x,
                    ghost_ground_y - sel->local_bounds.min.y * t.scale.y,
                    map_mouse_world_xz_.y};
                t.rotation = glm::angleAxis(
                    glm::radians(map_place_yaw_deg_),
                    glm::vec3{0.f, 1.f, 0.f});
                // Draw the placement preview as an oriented bounding box —
                // 8 corners of the local AABB pushed through `t.matrix()`
                // (which includes the yaw rotation). The earlier
                // `local.transform(...)` returns a WORLD-axis-aligned box,
                // which is correct for collision but visually misleading at
                // non-axis-aligned yaws — the AABB only grows, it doesn't
                // rotate, so the ghost didn't reflect what would actually
                // get placed.
                const glm::vec3& mn = local.min;
                const glm::vec3& mx = local.max;
                glm::vec3 corners_local[8] = {
                    {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z},
                    {mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z},
                    {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z},
                    {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
                };
                const glm::mat4 m = t.matrix();
                glm::vec3 c[8];
                for (int i = 0; i < 8; ++i) {
                    glm::vec4 wc = m * glm::vec4(corners_local[i], 1.0f);
                    c[i] = glm::vec3(wc) / wc.w;
                }
                debug_draw_.clear();
                // bottom face
                debug_draw_.line(c[0], c[1]);
                debug_draw_.line(c[1], c[2]);
                debug_draw_.line(c[2], c[3]);
                debug_draw_.line(c[3], c[0]);
                // top face
                debug_draw_.line(c[4], c[5]);
                debug_draw_.line(c[5], c[6]);
                debug_draw_.line(c[6], c[7]);
                debug_draw_.line(c[7], c[4]);
                // verticals
                debug_draw_.line(c[0], c[4]);
                debug_draw_.line(c[1], c[5]);
                debug_draw_.line(c[2], c[6]);
                debug_draw_.line(c[3], c[7]);
                debug_draw_.flush(vp, glm::vec3{0.2f, 1.0f, 0.2f});
            }
        } else {
            Streamer::PickResult pick = streamer_.query_instance_at(
                map_mouse_world_xz_.x, map_mouse_world_xz_.y);
            if (pick.hit) {
                debug_draw_.clear();
                debug_draw_.box(pick.world_aabb.min, pick.world_aabb.max);
                debug_draw_.flush(vp, glm::vec3{1.0f, 0.2f, 0.2f});
            }
        }
    }

    // ---- Footer hint (PBD-027 / PBD-035 / PBD-041–043 / PBD-034) ----------
    {
        char input_line[64];
        const char* footer;
        if (map_input_active_) {
            std::snprintf(input_line, sizeof(input_line),
                          "GO TO CELL: %s_   ENTER OK   ESC CANCEL",
                          map_input_buf_.c_str());
            footer = input_line;
        } else if (map_input_err_flash_s_ > 0.f) {
            // PBD-051: per-cause messaging. Five distinct failure paths
            // used to render "BAD CELL COORD" regardless of what actually
            // went wrong; now each failure tags `map_err_kind_` at the
            // set site and the footer reads it out.
            switch (map_err_kind_) {
                case MapErrorKind::PlaceRejected:
                    footer = "PLACE REJECTED"; break;
                case MapErrorKind::DeleteRejected:
                    footer = "DELETE REJECTED"; break;
                case MapErrorKind::UndoFailed:
                    footer = "UNDO FAILED"; break;
                case MapErrorKind::RedoFailed:
                    footer = "REDO FAILED"; break;
                case MapErrorKind::BadCellCoord:
                case MapErrorKind::None:
                default:
                    footer = "BAD CELL COORD"; break;
            }
        } else if (map_tool_ == MapTool::Place) {
            footer = "WASD PAN   L/R OR RMB YAW   WHEEL/CTRL+WHL ZOOM   R+WHL TILT   HOME RESET   "
                     "LMB PLACE   1=PLACE 2=DELETE   3/4/5=SIZE   "
                     "Q/E ROTATE   SHIFT+WHL SCALE   "
                     "CTRL-Z UNDO / CTRL-Y REDO   "
                     "UP/DN PALETTE   G GO TO CELL   T PLAY HERE   ESC BACK";
        } else {
            footer = "WASD PAN   L/R OR RMB YAW   WHEEL/CTRL+WHL ZOOM   R+WHL TILT   HOME RESET   "
                     "LMB DELETE   1=PLACE 2=DELETE   3/4/5=SIZE   "
                     "Q/E ROTATE   SHIFT+WHL SCALE   "
                     "CTRL-Z UNDO / CTRL-Y REDO   "
                     "UP/DN PALETTE   G GO TO CELL   T PLAY HERE   ESC BACK";
        }
        const char* footer_lines[] = {footer};
        Text::DrawState fl;
        fl.lines              = footer_lines;
        fl.count              = 1;
        const float vw = static_cast<float>(window_.width());
        const float vh = static_cast<float>(window_.height());
        const float glyph_h = 28.f;
        const float text_w  = text_.measure_width(footer, glyph_h);
        fl.origin_top_left_px = {vw * 0.5f - text_w * 0.5f, vh - 80.f};
        fl.glyph_h_px         = glyph_h;
        fl.bg_color           = glm::vec4{0.05f, 0.05f, 0.07f, 0.70f};
        fl.bg_padding_px      = 14.f;
        if (map_input_active_)
            fl.color = glm::vec3{1.0f, 0.95f, 0.55f};
        else if (map_input_err_flash_s_ > 0.f)
            fl.color = glm::vec3{1.0f, 0.4f, 0.3f};
        else
            fl.color = glm::vec3{0.92f, 0.94f, 0.98f};
        fl.viewport_size_px   = {vw, vh};
        text_.draw_lines(fl);
    }

    // ---- Bottom bar (PBD-032 / PBD-037–039 / PBD-042) ---------------------
    {
        MapBarLayout layout = compute_bar_layout();

        std::vector<const ModelDef*> ordered = sorted_palette();

        const int n = static_cast<int>(ordered.size());
        if (n > 0 && map_palette_selection_ >= n) map_palette_selection_ = 0;

        const glm::vec2 viewport_px{
            static_cast<float>(window_.width()),
            static_cast<float>(window_.height()),
        };

        const float palette_dim =
            (map_tool_ == MapTool::Delete) ? 0.45f : 1.0f;

        std::vector<Menu::Rect> rects;
        rects.reserve(3u + layout.regions.size() * 2u);

        rects.push_back(Menu::Rect{
            layout.bar_min_px,
            layout.bar_max_px,
            glm::vec3{0.06f, 0.07f, 0.10f},
        });

        // PBD-038 divider.
        {
            float tool_max_x  = -1.f;
            float asset_min_x = -1.f;
            for (const auto& r : layout.regions) {
                if (r.kind == MapBarHitKind::ToolButton) {
                    if (r.max_px.x > tool_max_x) tool_max_x = r.max_px.x;
                } else if (r.kind == MapBarHitKind::AssetSlot) {
                    if (asset_min_x < 0.f || r.min_px.x < asset_min_x)
                        asset_min_x = r.min_px.x;
                }
            }
            if (tool_max_x > 0.f && asset_min_x > tool_max_x) {
                const float cx       = 0.5f * (tool_max_x + asset_min_x);
                const float half_w   = 1.f;
                const float v_inset  = 8.f;
                rects.push_back(Menu::Rect{
                    {cx - half_w, layout.bar_min_px.y + v_inset},
                    {cx + half_w, layout.bar_max_px.y - v_inset},
                    glm::vec3{0.05f, 0.05f, 0.08f},
                });
            }
        }

        // PBD-037 hover highlight.
        if (map_bar_hover_.kind != MapBarHitKind::None) {
            const bool skip_for_palette_dim =
                (map_bar_hover_.kind == MapBarHitKind::AssetSlot) &&
                (palette_dim < 1.0f);
            if (!skip_for_palette_dim) {
                for (const auto& r : layout.regions) {
                    if (r.kind == map_bar_hover_.kind &&
                        r.index == map_bar_hover_.index) {
                        rects.push_back(Menu::Rect{
                            {r.min_px.x - 3.f, r.min_px.y - 3.f},
                            {r.max_px.x + 3.f, r.max_px.y + 3.f},
                            glm::vec3{0.30f, 0.35f, 0.45f},
                        });
                        break;
                    }
                }
            }
        }

        for (const auto& r : layout.regions) {
            const bool is_tool   = (r.kind == MapBarHitKind::ToolButton);
            const bool is_slot   = (r.kind == MapBarHitKind::AssetSlot);
            const bool is_preset = (r.kind == MapBarHitKind::SizePreset);
            const bool is_play   = (r.kind == MapBarHitKind::PlayHere);
            const MapTool tool   = static_cast<MapTool>(r.index);
            const bool tool_act  = is_tool && tool == map_tool_;
            const bool slot_act  = is_slot && r.index == map_palette_selection_;
            const SizePreset preset = static_cast<SizePreset>(r.index);
            const bool preset_act   = is_preset && preset == map_size_preset_;

            const bool draw_slot_ring = slot_act && palette_dim >= 1.0f;
            if (tool_act || draw_slot_ring || preset_act) {
                glm::vec3 ring_col;
                if (tool_act && tool == MapTool::Place) {
                    ring_col = glm::vec3{0.2f, 0.9f, 0.3f};
                } else if (tool_act && tool == MapTool::Delete) {
                    ring_col = glm::vec3{1.0f, 0.3f, 0.3f};
                } else if (preset_act) {
                    ring_col = glm::vec3{0.2f, 0.8f, 1.0f};
                } else {
                    ring_col = glm::vec3{1.0f, 0.85f, 0.2f};
                }
                rects.push_back(Menu::Rect{
                    {r.min_px.x - 4.f, r.min_px.y - 4.f},
                    {r.max_px.x + 4.f, r.max_px.y + 4.f},
                    ring_col,
                });
            }

            glm::vec3 fill;
            if (is_tool) {
                if (tool == MapTool::Place) {
                    fill = tool_act ? glm::vec3{0.10f, 0.40f, 0.15f}
                                     : glm::vec3{0.18f, 0.20f, 0.24f};
                } else {
                    fill = tool_act ? glm::vec3{0.50f, 0.12f, 0.12f}
                                     : glm::vec3{0.18f, 0.20f, 0.24f};
                }
            } else if (is_preset) {
                fill = preset_act ? glm::vec3{0.10f, 0.30f, 0.45f}
                                  : glm::vec3{0.18f, 0.20f, 0.24f};
            } else if (is_play) {
                // PBD-054: PLAY HERE — green call-to-action distinct from
                // the Place tool's darker green (the Place tool ring is the
                // brighter green; this fill stays solid even when inactive
                // because the button never has an "off" state).
                fill = glm::vec3{0.15f, 0.55f, 0.25f};
            } else /* is_slot */ {
                fill = ordered[static_cast<std::size_t>(r.index)]->tint
                       * palette_dim;
            }
            rects.push_back(Menu::Rect{r.min_px, r.max_px, fill});
        }

        menu_.draw_rects(rects.data(), static_cast<int>(rects.size()),
                          viewport_px);

        char label_buf[40];
        for (const auto& r : layout.regions) {
            const float gx = r.min_px.x + 6.f;
            const float gy = r.min_px.y + (r.max_px.y - r.min_px.y) * 0.5f - 10.f;

            if (r.kind == MapBarHitKind::ToolButton) {
                const MapTool tool = static_cast<MapTool>(r.index);
                const char* txt = (tool == MapTool::Place) ? "PLACE"
                                                            : "DELETE";
                const char* lines_one[1] = {txt};
                Text::DrawState tl;
                tl.lines              = lines_one;
                tl.count              = 1;
                tl.origin_top_left_px = {gx, gy};
                tl.glyph_h_px         = 20.f;
                tl.color              = glm::vec3{1.0f, 1.0f, 1.0f};
                tl.viewport_size_px   = viewport_px;
                text_.draw_lines(tl);
            } else if (r.kind == MapBarHitKind::SizePreset) {
                const SizePreset p = static_cast<SizePreset>(r.index);
                const char* txt = (p == SizePreset::Small)  ? "S"
                                : (p == SizePreset::Medium) ? "M"
                                                             : "L";
                const float glyph_h = 26.f;
                const float label_w = text_.measure_width(txt, glyph_h);
                const float slot_w  = r.max_px.x - r.min_px.x;
                const float lx      = r.min_px.x + (slot_w - label_w) * 0.5f;
                const float ly      = r.min_px.y +
                    (r.max_px.y - r.min_px.y) * 0.5f - glyph_h * 0.5f;
                const char* lines_one[1] = {txt};
                Text::DrawState tl;
                tl.lines              = lines_one;
                tl.count              = 1;
                tl.origin_top_left_px = {lx, ly};
                tl.glyph_h_px         = glyph_h;
                tl.color              = glm::vec3{1.0f, 1.0f, 1.0f};
                tl.viewport_size_px   = viewport_px;
                text_.draw_lines(tl);
            } else if (r.kind == MapBarHitKind::PlayHere) {
                // PBD-054: centred white label. The button width (140px) is
                // tuned for the 20px-glyph "PLAY HERE" string with a few
                // pixels of slack — no fall-through shrink path needed.
                const char* txt = "PLAY HERE";
                const float glyph_h = 20.f;
                const float label_w = text_.measure_width(txt, glyph_h);
                const float slot_w  = r.max_px.x - r.min_px.x;
                const float lx      = r.min_px.x + (slot_w - label_w) * 0.5f;
                const float ly      = r.min_px.y +
                    (r.max_px.y - r.min_px.y) * 0.5f - glyph_h * 0.5f;
                const char* lines_one[1] = {txt};
                Text::DrawState tl;
                tl.lines              = lines_one;
                tl.count              = 1;
                tl.origin_top_left_px = {lx, ly};
                tl.glyph_h_px         = glyph_h;
                tl.color              = glm::vec3{1.0f, 1.0f, 1.0f};
                tl.viewport_size_px   = viewport_px;
                text_.draw_lines(tl);
            } else if (r.kind == MapBarHitKind::AssetSlot) {
                const ModelDef* d = ordered[static_cast<std::size_t>(r.index)];
                std::snprintf(label_buf, sizeof(label_buf),
                              "%u %s", d->id, d->name.c_str());

                const float slot_w    = r.max_px.x - r.min_px.x;
                const float avail_w   = slot_w - 12.f;
                float       glyph_h   = 18.f;
                float       label_w   = text_.measure_width(label_buf, glyph_h);

                if (label_w > avail_w) {
                    const float shrunk = text_.measure_width(label_buf, 14.f);
                    if (shrunk <= avail_w) {
                        glyph_h = 14.f;
                        label_w = shrunk;
                    }
                }
                if (label_w > avail_w) {
                    glyph_h = 18.f;
                    char* name_start = std::strchr(label_buf, ' ');
                    name_start = name_start ? name_start + 1 : label_buf;
                    int len = static_cast<int>(std::strlen(label_buf));
                    while (len > static_cast<int>(name_start - label_buf) + 1 &&
                           text_.measure_width(label_buf, glyph_h) > avail_w) {
                        label_buf[len - 1] = '\0';
                        --len;
                        if (len >= 3) {
                            label_buf[len - 3] = '.';
                            label_buf[len - 2] = '.';
                            label_buf[len - 1] = '.';
                        }
                    }
                }

                const char* lines_one[1] = {label_buf};
                Text::DrawState tl;
                tl.lines              = lines_one;
                tl.count              = 1;
                tl.origin_top_left_px = {gx, gy};
                tl.glyph_h_px         = glyph_h;
                const auto luminance = [](const glm::vec3& c) {
                    return glm::dot(c, glm::vec3{0.2126f, 0.7152f, 0.0722f});
                };
                const glm::vec3 raw_tint =
                    ordered[static_cast<std::size_t>(r.index)]->tint;
                const glm::vec3 near_black{0.05f, 0.05f, 0.08f};
                const glm::vec3 near_white{0.95f, 0.95f, 0.95f};
                const glm::vec3 label_col =
                    (luminance(raw_tint) < 0.5f) ? near_white : near_black;
                tl.color              = label_col * palette_dim;
                tl.viewport_size_px   = viewport_px;
                text_.draw_lines(tl);
            }
        }
    }

    // ---- Overlay legend (PBD-040) ----------------------------------------
    {
        struct LegendEntry {
            const char* label;
            glm::vec3   color;
        };
        const LegendEntry entries[] = {
            {"LOADED CELL",     glm::vec3{0.15f, 1.0f,  0.95f}},
            {"UNLOADED CELL",   glm::vec3{0.35f, 0.35f, 0.40f}},
            {"ROAD CENTERLINE", glm::vec3{1.0f,  0.55f, 0.10f}},
            {"INTERSECTION",    glm::vec3{1.0f,  1.0f,  0.15f}},
        };
        constexpr int nrows = sizeof(entries) / sizeof(entries[0]);

        const float vw          = static_cast<float>(window_.width());
        const float glyph_h     = 20.f;
        const float row_h       = glyph_h * 1.4f;
        const float swatch_w    = 22.f;
        const float swatch_h    = 18.f;
        const float gap_px      = 10.f;
        const float pad         = 12.f;
        const float anchor_inset = 24.f;

        float max_label_w = 0.f;
        for (int i = 0; i < nrows; ++i) {
            const float w = text_.measure_width(entries[i].label, glyph_h);
            if (w > max_label_w) max_label_w = w;
        }

        const float block_w = pad * 2.f + swatch_w + gap_px + max_label_w;
        const float block_h = pad * 2.f + static_cast<float>(nrows) * row_h;

        const float origin_x = vw - block_w - anchor_inset;
        const float origin_y = anchor_inset;

        const glm::vec2 viewport_px{
            vw,
            static_cast<float>(window_.height()),
        };

        std::vector<Menu::Rect> rects;
        rects.reserve(1u + static_cast<std::size_t>(nrows));

        rects.push_back(Menu::Rect{
            {origin_x,          origin_y},
            {origin_x + block_w, origin_y + block_h},
            glm::vec3{0.05f, 0.05f, 0.07f},
        });

        for (int i = 0; i < nrows; ++i) {
            const float row_top    = origin_y + pad + static_cast<float>(i) * row_h;
            const float swatch_y0  = row_top + (row_h - swatch_h) * 0.5f - glyph_h * 0.2f;
            const float swatch_x0  = origin_x + pad;
            rects.push_back(Menu::Rect{
                {swatch_x0,            swatch_y0},
                {swatch_x0 + swatch_w, swatch_y0 + swatch_h},
                entries[i].color,
            });
        }

        menu_.draw_rects(rects.data(), static_cast<int>(rects.size()),
                          viewport_px);

        const char* lines[nrows];
        for (int i = 0; i < nrows; ++i) lines[i] = entries[i].label;

        Text::DrawState ll;
        ll.lines              = lines;
        ll.count              = nrows;
        ll.origin_top_left_px = {origin_x + pad + swatch_w + gap_px,
                                  origin_y + pad};
        ll.glyph_h_px         = glyph_h;
        ll.color              = glm::vec3{0.92f, 0.94f, 0.98f};
        ll.viewport_size_px   = viewport_px;
        text_.draw_lines(ll);
    }

    // ---- Cursor caption (PBD-036) ----------------------------------------
    if (map_mouse_valid_) {
        char         caption_buf[64];
        const char*  caption      = nullptr;
        bool         have_caption = false;

        if (map_tool_ == MapTool::Place) {
            std::vector<const ModelDef*> ordered = sorted_palette();
            if (!ordered.empty() &&
                map_palette_selection_ >= 0 &&
                map_palette_selection_ < static_cast<int>(ordered.size())) {
                const ModelDef* sel = ordered[static_cast<std::size_t>(
                                                map_palette_selection_)];
                const bool show_scale = std::fabs(map_place_free_scale_ - 1.f) > 1e-3f;
                const bool show_yaw   = std::fabs(map_place_yaw_deg_)        > 1e-3f;
                char scale_seg[24] = "";
                char yaw_seg[24]   = "";
                if (show_scale) {
                    std::snprintf(scale_seg, sizeof(scale_seg),
                                  "  x%.2f", map_place_free_scale_);
                }
                if (show_yaw) {
                    std::snprintf(yaw_seg, sizeof(yaw_seg),
                                  "  R%d",
                                  static_cast<int>(map_place_yaw_deg_));
                }
                std::snprintf(caption_buf, sizeof(caption_buf),
                              "PLACE - %s%s%s",
                              sel->name.c_str(), scale_seg, yaw_seg);
                caption      = caption_buf;
                have_caption = true;
            }
        } else {
            Streamer::PickResult pick = streamer_.query_instance_at(
                map_mouse_world_xz_.x, map_mouse_world_xz_.y);
            if (pick.hit) {
                const ModelDef* md = world_models_.get(pick.instance.model_id);
                std::snprintf(caption_buf, sizeof(caption_buf),
                              "DELETE - %s",
                              md ? md->name.c_str() : "?");
                caption      = caption_buf;
                have_caption = true;
            }
        }

        if (have_caption) {
            const float vw = static_cast<float>(window_.width());
            const float vh = static_cast<float>(window_.height());

            int mx_draw = 0, my_draw = 0;
            scale_mouse_to_drawable(input_.mouse_x(), input_.mouse_y(),
                                    mx_draw, my_draw);

            const float glyph_h = 20.f;
            const float pad     = 8.f;
            const float text_w  = text_.measure_width(caption, glyph_h);
            const float box_w   = text_w + pad * 2.f;
            const float box_h   = glyph_h + pad * 2.f;

            float x = static_cast<float>(mx_draw) + 24.f;
            float y = static_cast<float>(my_draw) + 24.f;

            const float bar_top   = vh - 200.f;
            const float right_max = vw - 8.f;
            if (x + box_w > right_max) {
                x = static_cast<float>(mx_draw) - 24.f - box_w;
                if (x < 8.f) x = right_max - box_w;
            }
            if (y + box_h > bar_top) {
                y = static_cast<float>(my_draw) - 24.f - box_h;
                if (y < 8.f) y = 8.f;
            }

            const char* caption_lines[1] = {caption};
            Text::DrawState cl;
            cl.lines              = caption_lines;
            cl.count              = 1;
            cl.origin_top_left_px = {x + pad, y + pad};
            cl.glyph_h_px         = glyph_h;
            cl.color              = glm::vec3{1.0f, 0.95f, 0.55f};
            cl.bg_color           = glm::vec4{0.05f, 0.05f, 0.07f, 0.70f};
            cl.bg_padding_px      = pad;
            cl.viewport_size_px   = {vw, vh};
            text_.draw_lines(cl);
        }
    }

    window_.swap();
}

} // namespace pengine
