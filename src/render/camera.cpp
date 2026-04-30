#include "render/camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <SDL_scancode.h>

#include "platform/input.h"

namespace pengine {

glm::vec3 Camera::forward() const {
    float yr = glm::radians(yaw);
    float pr = glm::radians(pitch);
    return glm::normalize(glm::vec3{
        std::cos(yr) * std::cos(pr),
        std::sin(pr),
        std::sin(yr) * std::cos(pr)
    });
}

glm::vec3 Camera::right() const {
    return glm::normalize(glm::cross(forward(), glm::vec3{0, 1, 0}));
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position, position + forward(), glm::vec3{0, 1, 0});
}

glm::mat4 Camera::proj(float aspect) const {
    return glm::perspective(glm::radians(fov), aspect, near_z, far_z);
}

glm::mat4 Camera::view_proj(float aspect) const {
    return proj(aspect) * view();
}

void Camera::update(float dt, const Input& input, float mouse_dx, float mouse_dy) {
    // Look
    yaw   += mouse_dx * look_speed;
    pitch -= mouse_dy * look_speed;
    if (pitch >  89.f) pitch =  89.f;
    if (pitch < -89.f) pitch = -89.f;

    // Move
    glm::vec3 fwd = forward();
    glm::vec3 rgt = right();
    glm::vec3 vel{0};
    if (input.down(SDL_SCANCODE_W)) vel += fwd;
    if (input.down(SDL_SCANCODE_S)) vel -= fwd;
    if (input.down(SDL_SCANCODE_D)) vel += rgt;
    if (input.down(SDL_SCANCODE_A)) vel -= rgt;
    if (input.down(SDL_SCANCODE_E)) vel += glm::vec3{0, 1, 0};
    if (input.down(SDL_SCANCODE_Q)) vel -= glm::vec3{0, 1, 0};

    if (glm::length(vel) > 0.f)
        position += glm::normalize(vel) * move_speed * dt;
}

} // namespace pengine
