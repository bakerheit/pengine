#pragma once

#include <glm/glm.hpp>

namespace pengine {

class Input;

class Camera {
public:
    glm::vec3 position  = {0.f, 2.f, 6.f};
    float     yaw       = -90.f; // degrees, -90 = looking toward -Z
    float     pitch     =  -15.f; // degrees
    float     fov       =  60.f;
    float     near_z    =   0.1f;
    float     far_z     = 800.f;
    float     move_speed = 6.f;
    float     look_speed = 0.12f; // degrees per pixel

    glm::mat4 view()            const;
    glm::mat4 proj(float aspect) const;
    glm::mat4 view_proj(float aspect) const;

    glm::vec3 forward() const;
    glm::vec3 right()   const;

    // mouse_dx/dy in pixels (relative mode). dt in seconds.
    void update(float dt, const Input& input, float mouse_dx, float mouse_dy);
};

} // namespace pengine
