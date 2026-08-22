#include "gfx/camera.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace apricot {

glm::vec3 Camera::forward() const {
    const float cp = std::cos(pitch);
    return glm::vec3{cp * std::sin(yaw), std::sin(pitch), -cp * std::cos(yaw)};
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3{0.0f, 1.0f, 0.0f}));
}

glm::vec3 Camera::up() const {
    return glm::normalize(glm::cross(right(), forward()));
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, position + forward(), glm::vec3{0.0f, 1.0f, 0.0f});
}

glm::mat4 Camera::projection() const {
    // Guard the aspect: a minimised window reports height 0, and an aspect of
    // inf produces an all-NaN matrix that poisons the frustum and culls the
    // entire world for the rest of the session.
    const float a = aspect > 0.0f ? aspect : 1.0f;
    return glm::perspective(fov_y, a, near_plane, far_plane);
}

void Camera::add_look(float dx, float dy) {
    constexpr float kPitchLimit = 1.5533431f;  // ~89 degrees
    yaw += dx;
    pitch = std::clamp(pitch - dy, -kPitchLimit, kPitchLimit);
}

}  // namespace apricot
