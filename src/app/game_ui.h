#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "app/dev_menu.h"
#include "city/map.h"
#include "game/ui_flow.h"
#include "game/map_geometry.h"
#include "game/minimap.h"
#include "game/wanted_system.h"

namespace apricot {

class Hud;

struct GameUiSnapshot {
    glm::vec3 player_position{0.0f};
    glm::vec3 player_forward{0.0f, 0.0f, -1.0f};
    std::optional<glm::vec2> mission_target;
    const char* road_name = nullptr;
    float speed_mph = 0.0f;
    float vehicle_health = 100.0f;
    int wanted_level = 0;
    // The stars FLASH while the police are searching for a suspect nobody can
    // see, and during the radio hold after a fresh crime (PENG-46). Keyed on
    // the sim step so a replay pulses identically.
    bool wanted_searching = false;
    bool wanted_report_pending = false;
    // Drawn as a bar and a countdown under the stars: spotted, losing them,
    // cooling, and how long until the stars are gone if nobody sees you.
    WantedCooldown wanted_cooldown{};
    const char* police_stop_prompt = "";
    int64_t step = 0;
    // [0, 1): midnight at 0, noon at 0.5. This is the visible sky clock.
    float time_of_day = 0.28f;
    float title_opacity = 1.0f;
    const char* save_notice = "";
    const char* weather = "CLEAR";
    const char* daylight = "DAY";

    // The frame recorder. Shown because a recorder you cannot see is one you
    // cannot trust: the question "did that run get logged?" is otherwise only
    // answerable after quitting, which is exactly too late.
    bool perf_logging = false;
    const char* perf_log_label = "";   // the session number, e.g. "004"
    int perf_marks = 0;
    // Counts down after F4 so the press has visible confirmation. Without it a
    // mark is a keystroke into a void and people press it twice.
    float perf_mark_feedback_s = 0.0f;
};

// Draws the shipped game UI through Hud's single screen-space batch. It owns
// only immutable land caches; screen transitions live in UiFlow and all GL
// resources stay owned by Hud.
class GameUi {
public:
    static constexpr float kArrestedDisplaySeconds = 4.0f;
    // How far the wanted cooldown meter pushes down whatever the HUD stacks
    // under the stars (the on-foot health bar) while any star is lit.
    static constexpr float kWantedMeterDrop = 32.0f;
    void draw_arrested(Hud& hud, float remaining_s, glm::vec2 vp) const;
    // The wanted stars exactly as the HUD draws them, right-aligned at `right`,
    // for panels that show them elsewhere (the respray booth).
    void draw_wanted_badge(Hud& hud, int wanted_level, bool flash, int64_t step,
                           float right, float top) const;

    // The "we are recording" dot, under the clock. Public so the pause screen
    // can show it too — checking whether a session is being logged is exactly
    // the kind of thing you do while paused.
    void draw_perf_recorder(Hud& hud, const GameUiSnapshot& snapshot,
                            glm::vec2 vp) const;
    void build_map();
    void draw_minimap(Hud& hud, const UiFlow& flow,
                      const GameUiSnapshot& snapshot, glm::vec2 vp) const;
    void toggle_waypoint(glm::vec2 pointer, glm::vec2 vp, bool at_centre = false);
    void clear_waypoint() { waypoint_.position.reset(); }
    const std::optional<glm::vec2>& waypoint_position() const {
        return waypoint_.position;
    }

    void update_map(glm::vec2 pan_axis, float zoom_steps,
                    glm::vec2 drag_pixels, bool recenter,
                    glm::vec3 player_position, float dt,
                    glm::vec2 viewport_px, glm::vec2 pointer_px = {-1.0f, -1.0f},
                    float wheel_zoom_steps = 0.0f);
    void open_map_view(glm::vec3 player_position, glm::vec2 viewport_px);

    void draw(Hud& hud, const UiFlow& flow, const GameUiSnapshot& snapshot,
              glm::vec2 viewport_px) const;

    void draw_dev_menu(Hud& hud, const DevMenu& menu,
                       glm::vec2 viewport_px) const;

    // Menu row under an absolute mouse position, or -1. Uses the same layout
    // function draw_menu() uses, so hover and highlight cannot drift apart.
    int hit_test(const UiFlow& flow, glm::vec2 pointer_px,
                 glm::vec2 viewport_px) const;

    std::size_t land_cell_count() const {
        return land_patches_.size();
    }

private:
    struct MapContour {
        glm::vec2 a{}, b{};
        float elevation = 0.0f;
    };

    struct MapFootprint {
        const char* name = nullptr;
        glm::vec2 corners[4]{};
        bool lot = false;
        bool dock = false;
    };

    static constexpr int kMapTerrainGrid = 1024;

    void draw_title(Hud& hud, const UiFlow& flow, glm::vec2 vp, float opacity) const;
    void draw_pause(Hud& hud, const UiFlow& flow,
                    const GameUiSnapshot& snapshot, glm::vec2 vp) const;
    void draw_settings(Hud& hud, const UiFlow& flow, glm::vec2 vp) const;
    void draw_map(Hud& hud, const UiFlow& flow,
                  const GameUiSnapshot& snapshot, glm::vec2 vp) const;
    void build_map_terrain();

    std::vector<MapPolygon> land_patches_;
    std::vector<MapContour> contours_;
    std::vector<MapContour> overview_contours_;
    std::vector<MapFootprint> footprints_;
    MapCamera map_camera_;
    MapWaypoint waypoint_;
};

}  // namespace apricot
