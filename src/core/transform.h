#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace apricot {

// Position / rotation / scale, in that composition order (T * R * S).
//
// Rotation is a quaternion, not Euler angles. A rally car pitches, rolls and
// yaws simultaneously on every jump; Euler storage gimbal-locks on the exact
// manoeuvre the game is about. Euler input is available as a setter for
// authoring and debug UI, but it never round-trips back out.
struct Transform {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // identity (w, x, y, z)
    glm::vec3 scale{1.0f, 1.0f, 1.0f};

    glm::mat4 matrix() const {
        glm::mat4 m = glm::translate(glm::mat4{1.0f}, position);
        m *= glm::mat4_cast(rotation);
        return glm::scale(m, scale);
    }

    void set_euler_deg(float pitch, float yaw, float roll) {
        rotation = glm::quat(glm::vec3{glm::radians(pitch), glm::radians(yaw),
                                       glm::radians(roll)});
    }

    // Basis vectors. -Z is forward, matching the GL convention the camera and
    // projection maths already use; mixing that up puts the car in reverse.
    glm::vec3 forward() const { return rotation * glm::vec3{0.0f, 0.0f, -1.0f}; }
    glm::vec3 right() const   { return rotation * glm::vec3{1.0f, 0.0f, 0.0f}; }
    glm::vec3 up() const      { return rotation * glm::vec3{0.0f, 1.0f, 0.0f}; }
};

}  // namespace apricot
