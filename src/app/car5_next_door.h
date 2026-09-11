#pragma once

#include <glm/vec3.hpp>

namespace apricot {

// Car 5 source coordinates: +X driver side, +Y up, +Z nose. Keep these in sync
// with DOOR in tools/car5_next_spec.py, which is what cuts the panel out of
// the cooked shell.
//
// These are NOT metres. Car 5 is an import, and the catalog fits it with
// scale_x = half_track / wheel_x and scale_z = 2*half_wheelbase / wheel span,
// which for this body is .751 across and .664 along. Anything measured against
// another car has to come through that conversion or it lands in the wrong
// place -- the source units are roughly a third larger than the world metre.
inline const glm::vec3 kCar5NextDoorHinge{1.196f, .62f, 1.315f};
inline const glm::vec3 kCar5NextDoorHandle{1.230f, 1.24f, .07f};
inline constexpr float kCar5NextDoorRearZ = -.18f;    // the B-pillar shut line
inline constexpr float kCar5NextDoorFrontZ = 1.33f;   // the front fender seam
inline constexpr float kCar5NextDoorSillY = .62f;
inline constexpr float kCar5NextDoorTopY = 1.88f;
inline constexpr float kCar5NextDoorMaxAngle =
    68.f * 3.14159265358979323846f / 180.f;
inline constexpr float kCar5NextDoorOpenRadians = -kCar5NextDoorMaxAngle;

}  // namespace apricot
