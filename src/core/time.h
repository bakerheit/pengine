#pragma once

#include <chrono>
#include <cstdint>

namespace pengine {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

inline double seconds_since(TimePoint t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
}

// Fix-Your-Timestep accumulator. Update is called at fixed dt; render gets
// the fractional alpha in [0, 1) for state interpolation.
struct FixedTimestep {
    double fixed_dt = 1.0 / 60.0;
    double max_frame_time = 0.25;   // clamp to avoid spiral-of-death
    double accumulator = 0.0;
    TimePoint last = Clock::now();

    // Returns the number of fixed updates to run, plus the leftover alpha.
    struct Tick {
        int updates;
        double alpha;
    };

    Tick advance() {
        TimePoint now = Clock::now();
        double frame = std::chrono::duration<double>(now - last).count();
        last = now;
        if (frame > max_frame_time) frame = max_frame_time;
        accumulator += frame;

        int updates = 0;
        while (accumulator >= fixed_dt) {
            accumulator -= fixed_dt;
            ++updates;
        }
        double alpha = accumulator / fixed_dt;
        return {updates, alpha};
    }
};

} // namespace pengine
