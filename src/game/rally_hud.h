#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "game/conditions.h"
#include "game/ghost.h"
#include "game/rally.h"

namespace apricot {

// The HUD, as PLAIN DATA.
//
// Nothing in here draws, measures a string, or knows what a screen is. It is
// the numbers a dial, a lap board and a minimap need, computed once from the
// sim state, so the renderer's job is layout and the sim's job is truth. That
// split is also why this file lives in apricot_sim at all: the HUD's contents
// are testable headlessly, and a wrong split delta is a failing test rather
// than something somebody notices on a stream.
//
// Rebuild it once per RENDER frame, not once per sim step — it is a view, and
// nothing reads back from it.

// Where the tachometer's needle ends. A display constant, not a physics one:
// physics/vehicle.h's VehicleTuning has no engine curve yet, so there is no
// authoritative redline to read. Move this to the tuning when there is one.
inline constexpr float kDisplayRedlineRpm = 7800.0f;

struct HudModel {
    // --- the dials -----------------------------------------------------------
    float speed_kph = 0.0f;
    float speed_ms = 0.0f;
    int32_t gear = 1;
    float engine_rpm = 0.0f;
    float rpm_fraction = 0.0f;  // [0, 1], ready to sweep a needle with

    // --- the lap board -------------------------------------------------------
    double lap_time = 0.0;
    double total_time = 0.0;
    int lap = 0;
    int target_laps = 3;
    bool lap_started = false;
    bool finished = false;

    int gate_index = 0;  // the gate the car owes next
    int gate_count = 0;

    bool has_best = false;
    double best_lap = 0.0;

    // Delta against the best lap at the last gate crossed: positive is slower.
    // has_split_delta is false until a gate has been crossed on a lap for
    // which the best has a comparable split — an unknown delta is left absent
    // rather than shown as zero, because zero means "dead level" and that is a
    // very different thing to tell a driver.
    bool has_split_delta = false;
    double split_delta = 0.0;
    int split_gate = -1;  // which gate the delta refers to

    // --- conditions ----------------------------------------------------------
    Conditions conditions;

    // --- minimap, in world XZ ------------------------------------------------
    // The route polyline, one point per gate, in route order. CLOSED loops are
    // not repeated back to the first point — `route_closed` says whether to
    // join the ends, so a renderer never has to guess and never draws a stray
    // segment on a point-to-point stage.
    std::vector<glm::vec2> route_points;
    bool route_closed = true;

    glm::vec2 car_xz{0.0f};
    glm::vec2 car_heading_xz{0.0f, -1.0f};  // unit, for an arrow

    bool has_ghost = false;
    glm::vec2 ghost_xz{0.0f};

    // Bounding box of the route in world XZ, so a renderer can fit the map to
    // its box without walking the polyline again every frame. Equal to
    // car_xz when there is no route.
    glm::vec2 bounds_min{0.0f};
    glm::vec2 bounds_max{0.0f};
};

// Fill `out` from the sim. `ghost` may be null (no ghost armed yet).
//
// An out-parameter rather than a return value because this runs every render
// frame and the vectors inside keep their capacity across calls — a HUD that
// allocates sixty times a second is a HUD that shows up in a profile.
void build_hud(const RallyState& state, const GhostCar* ghost, HudModel& out);

// "1:23.456". Fixed width, always minutes:seconds.millis, negative clamped to
// zero — a lap board that changes width as the clock passes a minute makes the
// whole panel jump.
void format_lap_time(double seconds, char* buf, std::size_t n);

// "+1.23" / "-0.44" for a split delta. Always signed: an unsigned delta on a
// timing screen is a bug report waiting to happen.
void format_delta(double seconds, char* buf, std::size_t n);

}  // namespace apricot
