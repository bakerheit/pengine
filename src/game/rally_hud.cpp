#include "game/rally_hud.h"

#include <cmath>
#include <cstdio>

#include "core/transform.h"

namespace apricot {

void build_hud(const RallyState& state, const GhostCar* ghost, HudModel& out) {
    // --- dials ---------------------------------------------------------------
    const glm::vec3 v = state.car.velocity;
    out.speed_ms = std::sqrt(glm::dot(v, v));
    out.speed_kph = out.speed_ms * 3.6f;
    out.gear = state.car.gear;
    out.engine_rpm = state.car.engine_rpm;
    out.rpm_fraction =
        glm::clamp(state.car.engine_rpm / kDisplayRedlineRpm, 0.0f, 1.0f);

    // --- lap board -----------------------------------------------------------
    out.lap_time = state.timing.lap_time;
    out.total_time = state.timing.total_time;
    out.lap = state.timing.lap;
    out.target_laps = state.timing.target_laps;
    out.lap_started = state.timing.lap_started;
    out.finished = state.timing.finished;

    out.gate_index = state.next_checkpoint;
    out.gate_count = static_cast<int>(state.route.checkpoints.size());

    out.has_best = state.best.valid;
    out.best_lap = state.best.valid ? state.best.lap_time : 0.0;

    out.has_split_delta = false;
    out.split_delta = 0.0;
    out.split_gate = -1;

    // The last gate crossed is the last split recorded. Compared against the
    // best lap's split at the SAME gate index, which is why both vectors are
    // indexed by gate — see LapTiming::splits.
    if (state.best.valid && !state.timing.splits.empty()) {
        const std::size_t last = state.timing.splits.size() - 1;
        if (last > 0 && last < state.best.splits.size()) {
            out.has_split_delta = true;
            out.split_delta = state.timing.splits[last] - state.best.splits[last];
            out.split_gate = static_cast<int>(last);
        }
    }

    out.conditions = state.conditions;

    // --- minimap -------------------------------------------------------------
    out.route_points.clear();
    out.route_points.reserve(state.route.checkpoints.size());
    for (const Checkpoint& cp : state.route.checkpoints) {
        out.route_points.push_back(glm::vec2{cp.position.x, cp.position.z});
    }
    out.route_closed = state.route.closed;

    out.car_xz = glm::vec2{state.car.position.x, state.car.position.z};

    // Heading from the car's ORIENTATION, not its velocity: a car sliding
    // sideways is still pointing somewhere, and a minimap arrow that snaps
    // round mid-drift is worse than no arrow.
    Transform car_tf;
    car_tf.rotation = state.car.orientation;
    const glm::vec3 fwd = car_tf.forward();
    const float fwd_len = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
    out.car_heading_xz = (fwd_len > 1e-5f)
                             ? glm::vec2{fwd.x / fwd_len, fwd.z / fwd_len}
                             : glm::vec2{0.0f, -1.0f};

    out.has_ghost = ghost != nullptr && ghost->active && !ghost->finished;
    out.ghost_xz = out.has_ghost ? glm::vec2{ghost_car(*ghost).position.x,
                                             ghost_car(*ghost).position.z}
                                 : out.car_xz;

    if (out.route_points.empty()) {
        out.bounds_min = out.car_xz;
        out.bounds_max = out.car_xz;
    } else {
        out.bounds_min = out.route_points.front();
        out.bounds_max = out.route_points.front();
        for (const glm::vec2& p : out.route_points) {
            out.bounds_min = glm::min(out.bounds_min, p);
            out.bounds_max = glm::max(out.bounds_max, p);
        }
    }
}

void format_lap_time(double seconds, char* buf, std::size_t n) {
    if (n == 0) return;
    if (!(seconds > 0.0)) seconds = 0.0;  // also catches NaN

    // Milliseconds first, then split: rounding after the division lets 59.9996
    // print as "0:60.000".
    const long long millis = static_cast<long long>(seconds * 1000.0 + 0.5);
    const long long minutes = millis / 60000;
    const long long rest = millis % 60000;
    std::snprintf(buf, n, "%lld:%02lld.%03lld", minutes, rest / 1000,
                  rest % 1000);
}

void format_delta(double seconds, char* buf, std::size_t n) {
    if (n == 0) return;
    if (!(seconds == seconds)) seconds = 0.0;  // NaN

    const char sign = (seconds < 0.0) ? '-' : '+';
    const double mag = (seconds < 0.0) ? -seconds : seconds;
    std::snprintf(buf, n, "%c%.2f", sign, mag);
}

}  // namespace apricot
