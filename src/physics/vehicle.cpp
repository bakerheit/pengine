#include "physics/vehicle.h"

#include <algorithm>
#include <cmath>

namespace apricot {

VehicleState step_vehicle(const VehicleState& state, const VehicleTuning& tuning,
                          const InputFrame& input,
                          const TerrainCollider& collider, float dt) {
    VehicleState next = state;

    // --- steering ------------------------------------------------------------
    // Rate-limited toward the requested angle. Real, and not a placeholder:
    // the rest of the body is.
    const float target = glm::clamp(input.steer, -1.0f, 1.0f) * tuning.max_steer;
    const float max_delta = tuning.steer_rate * dt;
    next.steer_angle =
        state.steer_angle +
        glm::clamp(target - state.steer_angle, -max_delta, max_delta);

    // ------------------------------------------------------------------------
    // PLACEHOLDER INTEGRATION — see the header. Gravity plus a hard rest on
    // the terrain surface. No suspension spring, no damper, no tyre forces, no
    // engine torque: throttle, brake and handbrake are read but have no effect
    // on motion yet, and the car will not actually drive.
    // TODO(vehicle dynamics ticket): replace everything below with the real
    // four-ray suspension + tyre model.
    // ------------------------------------------------------------------------
    next.velocity.y -= tuning.gravity * dt;
    next.position += next.velocity * dt;

    const float rest_height = tuning.suspension_rest + tuning.wheel_radius;
    const TerrainCollider::GroundHit hit =
        collider.probe_down(next.position, rest_height);

    if (hit.hit && hit.distance < rest_height) {
        next.position.y = hit.point.y + rest_height;
        // Kill only downward velocity. Zeroing the whole vector would also
        // stop horizontal motion, which would make the stub look like a
        // working handbrake and hide that there are no tyre forces at all.
        next.velocity.y = std::max(next.velocity.y, 0.0f);
    }

    for (int i = 0; i < kWheelCount; ++i) {
        WheelState& w = next.wheels[static_cast<std::size_t>(i)];
        w.grounded = hit.hit;
        w.contact_point = hit.point;
        w.contact_normal = hit.normal;
        w.suspension_length = tuning.suspension_rest;
    }

    return next;
}

}  // namespace apricot
