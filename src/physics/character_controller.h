#pragma once

#include <glm/glm.hpp>

namespace pengine {

class Input;
class WorldCollision;

class CharacterController {
public:
    static constexpr float RADIUS     = 0.4f;
    static constexpr float HEIGHT     = 1.8f;
    static constexpr float EYE_HEIGHT = 1.65f;
    static constexpr float MOVE_SPEED = 6.f;     // m/s
    static constexpr float JUMP_SPEED = 5.5f;    // m/s
    static constexpr float GRAVITY    = -18.f;   // m/s^2

    void teleport(const glm::vec3& feet_pos);

    // `forward_yaw_deg` controls horizontal walk direction (camera yaw).
    void update(float dt, const Input& input, float forward_yaw_deg,
                const WorldCollision& world);

    glm::vec3 feet_position() const { return pos_; }
    glm::vec3 eye_position()  const { return pos_ + glm::vec3{0.f, EYE_HEIGHT, 0.f}; }
    glm::vec3 velocity()      const { return vel_; }
    bool      grounded()      const { return grounded_; }

private:
    glm::vec3 pos_      = {0.f, 0.f, 0.f};   // feet position
    glm::vec3 vel_      = {0.f, 0.f, 0.f};
    bool      grounded_ = false;
};

} // namespace pengine
