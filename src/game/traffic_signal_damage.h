#pragma once

#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>

namespace apricot {

inline constexpr float kSignalBreakSpeed = 5.5f;
inline constexpr float kStreetLampBreakSpeed = 6.5f;
inline constexpr float kStopSignBreakSpeed = 4.0f;
// Session state, independent of streamed chunks and render frames. One entry
// per authored approach (lane key), never per activation or scene-node id.
struct TrafficSignalDamage {
    uint64_t lane_key = 0;
    bool broken = false;
    glm::vec3 direction{0.0f, 0.0f, 1.0f};

    bool hit(glm::vec3 velocity, float break_speed = kSignalBreakSpeed) {
        velocity.y = 0.0f;
        const float speed = glm::length(velocity);
        if (broken || !std::isfinite(speed) || !std::isfinite(break_speed) ||
            break_speed <= 0.0f || speed < break_speed) return false;
        broken = true;
        direction = velocity / speed;
        return true;
    }
};

}  // namespace apricot
