#pragma once

#include <algorithm>
#include <cmath>

#include "core/transform.h"

namespace apricot {

// Cooked car coordinates: +X driver side, +Y up, +Z nose. Keep these in
// sync with DOOR in tools/vesper_mistral_spec.py. The mesh keeps car origin.
inline const glm::vec3 kMistralDoorHinge{1.0143f, .44f, .31f};
inline const glm::vec3 kMistralDoorHandle{1.0175f, .76f, -.52f};
inline constexpr float kMistralDoorMaxAngle = 65.f * 3.14159265358979323846f / 180.f;
inline constexpr float kMistralDoorOpenRadians = -kMistralDoorMaxAngle;
inline constexpr float kMistralDoorRearZ = -.86f;
inline constexpr float kMistralDoorFrontZ = .31f;
inline constexpr float kMistralDoorSillY = .44f;

inline Transform mistral_driver_door_transform(const Transform& body, float fraction) {
    const float open = std::isfinite(fraction) ? std::clamp(fraction, 0.f, 1.f) : 0.f;
    if (open == 0.f) return body;
    Transform door = body;
    door.rotation = body.rotation * glm::angleAxis(kMistralDoorOpenRadians * open,
                                                  glm::vec3{0,1,0});
    // Rotate the already-fitted panel rigidly about its fitted hinge. Applying
    // source rotation before nonuniform chassis scale would shear the door.
    door.position = body.transform_point(kMistralDoorHinge) -
                    door.rotation * (body.scale * kMistralDoorHinge);
    return door;
}

} // namespace apricot
