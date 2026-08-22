#pragma once

#include <glm/glm.hpp>

#include "core/frustum.h"

namespace apricot {

// View and projection for the frame.
//
// Pure maths — it holds no GL object and issues no calls. It lives in gfx
// rather than core because it is the renderer's view of the world, and letting
// sim code hold a camera is how "just cull against the camera" turns into
// gameplay depending on where you were looking.
struct Camera {
    glm::vec3 position{0.0f, 0.0f, 0.0f};

    // Radians. Yaw around +Y, pitch around the camera's right axis.
    float yaw = 0.0f;
    float pitch = 0.0f;

    float fov_y = glm::radians(60.0f);
    float near_plane = 0.15f;

    // The far plane is the render distance. Pushing it out is not free: depth
    // precision is distributed by the near/far RATIO, so doubling far while
    // near stays at 0.15 buys distance and pays in z-fighting up close.
    float far_plane = 2000.0f;

    float aspect = 16.0f / 9.0f;

    glm::vec3 forward() const;
    glm::vec3 right() const;
    glm::vec3 up() const;

    glm::mat4 view() const;
    glm::mat4 projection() const;
    glm::mat4 view_projection() const { return projection() * view(); }

    Frustum frustum() const { return Frustum::from_view_proj(view_projection()); }

    // Clamp pitch just short of vertical. Exactly +/- 90 degrees makes the
    // forward vector parallel to world up, the view matrix's basis degenerates,
    // and the camera rolls violently for one frame.
    void add_look(float dx, float dy);
};

}  // namespace apricot
