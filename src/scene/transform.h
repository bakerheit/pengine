#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace pengine {

struct Transform {
    glm::vec3 position = {0.f, 0.f, 0.f};
    glm::quat rotation = glm::quat(1.f, 0.f, 0.f, 0.f); // identity
    glm::vec3 scale    = {1.f, 1.f, 1.f};

    glm::mat4 matrix() const {
        glm::mat4 m = glm::translate(glm::mat4{1.f}, position);
        m *= glm::mat4_cast(rotation);
        m  = glm::scale(m, scale);
        return m;
    }

    void set_euler_deg(float pitch, float yaw, float roll) {
        rotation = glm::quat(glm::vec3(
            glm::radians(pitch), glm::radians(yaw), glm::radians(roll)));
    }

    glm::vec3 forward() const {
        return rotation * glm::vec3{0.f, 0.f, -1.f};
    }
};

} // namespace pengine
