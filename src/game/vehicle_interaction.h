#pragma once
#include <cmath>
#include <limits>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "physics/vehicle.h"

namespace apricot {
inline constexpr float kVehicleEntryReach = 1.65f;
inline constexpr float kVehicleEntryMaxSpeed = 1.5f;
// An empty car needs physical brakes, not the driver's brake/reverse pedal.
// Keep suspension and collision response alive, and leave the saved driving
// profile untouched so arcade reverse works normally when the player returns.
inline VehicleState step_unoccupied_vehicle(const VehicleState& state,
                                            VehicleTuning tuning,
                                            const TerrainCollider& collider,
                                            float dt) {
    tuning.arcade_reverse = false;
    InputFrame parking_input{};
    parking_input.brake = 1.0f;
    parking_input.handbrake = 1.0f;
    return step_vehicle(state, tuning, parking_input, collider, dt);
}
// Door distance, not a centre-radius test: long vans cannot be entered from
// the bonnet, roof, or the other side of the street. Both front doors work.
inline float vehicle_entry_distance(glm::vec3 person, glm::vec3 centre,
                                    glm::quat rotation, float half_width,
                                    float half_length, float ground_y,
                                    float speed) {
    if (speed > kVehicleEntryMaxSpeed || !std::isfinite(speed) ||
        person.y < ground_y-.55f || person.y > ground_y+.85f ||
        (rotation*glm::vec3{0,1,0}).y < .5f) return std::numeric_limits<float>::infinity();
    const glm::vec3 local=glm::inverse(rotation)*(person-centre);
    if (std::fabs(local.x)<half_width+.05f) return std::numeric_limits<float>::infinity();
    const float side=std::fabs(local.x)-(half_width+.35f);
    const float along=local.z+half_length*.22f;
    return std::sqrt(side*side+along*along);
}
} // namespace apricot
