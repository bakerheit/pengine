#include "render/spring_arm.h"

#include <algorithm>
#include <cmath>

#include "physics/world_collision.h"

namespace pengine {

void SpringArm::apply_mouse(float dx, float dy) {
    yaw_deg   += dx * sensitivity;
    pitch_deg -= dy * sensitivity;
    pitch_deg  = std::max(pitch_min, std::min(pitch_max, pitch_deg));
}

glm::vec3 SpringArm::forward() const {
    float y = glm::radians(yaw_deg);
    float p = glm::radians(pitch_deg);
    return glm::normalize(glm::vec3{
        std::cos(y) * std::cos(p),
        std::sin(p),
        std::sin(y) * std::cos(p)
    });
}

void SpringArm::update(const WorldCollision& world) {
    glm::vec3 fwd = forward();
    glm::vec3 dir = -fwd;            // from anchor toward camera

    RayHit hit  = world.raycast(anchor, dir, desired_dist);
    actual_dist = desired_dist;
    if (hit.hit) actual_dist = std::max(min_dist, hit.t - skin);

    camera_position = anchor + dir * actual_dist;
}

} // namespace pengine
