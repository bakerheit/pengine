#pragma once

#include <algorithm>
#include <cmath>

#include "core/transform.h"

namespace apricot {

// Source +X driver side, +Y up, +Z nose. Matches DOOR in the Workman spec.
inline const glm::vec3 kWorkmanDoorHinge{.964f, .52f, .92f};
inline const glm::vec3 kWorkmanDoorHandle{.97f, 1.09f, -.20f};
inline constexpr float kWorkmanDoorRearZ = -.43f;
inline constexpr float kWorkmanDoorFrontZ = .92f;
inline constexpr float kWorkmanDoorSillY = .52f;
inline constexpr float kWorkmanDoorTopY = 1.93f;
inline constexpr float kWorkmanDoorMaxAngle = 65.f * 3.14159265358979323846f / 180.f;
inline constexpr float kWorkmanDoorOpenRadians = -kWorkmanDoorMaxAngle;

inline Transform workman_driver_door_transform(const Transform& body, float fraction) {
    const float open = std::isfinite(fraction) ? std::clamp(fraction, 0.f, 1.f) : 0.f;
    if (open == 0.f) return body;
    Transform door = body;
    door.rotation = body.rotation * glm::angleAxis(kWorkmanDoorOpenRadians * open,
                                                  glm::vec3{0,1,0});
    door.position = body.transform_point(kWorkmanDoorHinge) -
                    door.rotation * (body.scale * kWorkmanDoorHinge);
    return door;
}

} // namespace apricot
