#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "core/transform.h"

namespace apricot {

inline constexpr float kSignalBreakSpeed = 5.5f;
inline constexpr float kSignalFallSeconds = 0.85f;

// Session state, independent of streamed chunks and render frames. One entry
// per authored approach (lane key), never per activation or scene-node id.
struct TrafficSignalDamage {
    uint64_t lane_key = 0;
    bool broken = false;
    float fall_seconds = 0.0f;
    glm::vec3 direction{0.0f, 0.0f, 1.0f};
    float resting_lift = 0.0f;

    bool hit(glm::vec3 velocity) {
        velocity.y = 0.0f;
        const float speed = glm::length(velocity);
        if (broken || !std::isfinite(speed) || speed < kSignalBreakSpeed) return false;
        broken = true;
        direction = velocity / speed;
        return true;
    }
    void step(float dt) {
        if (broken && dt > 0.0f)
            fall_seconds = std::min(kSignalFallSeconds, fall_seconds + dt);
    }
    Transform pose(glm::vec3 base) const {
        Transform out;
        if (!broken) return out;
        const float t = std::clamp(fall_seconds / kSignalFallSeconds, 0.0f, 1.0f);
        // Accelerate into the ground, then stop. No frame-time spring or
        // unbounded rigid-body pile to destabilise traffic or streaming.
        const float angle = 1.57079632679f * t * t;
        out.rotation = glm::angleAxis(angle, glm::cross(glm::vec3{0,1,0}, direction));
        out.position = base - out.rotation * base + glm::vec3{0,resting_lift*t*t,0};
        return out;
    }
};

}  // namespace apricot
