#pragma once

#include <glm/glm.hpp>

namespace pengine {

class WorldCollision;

// Third-person follow camera. Orbits an `anchor` world position with mouse-
// controlled yaw/pitch, then casts a ray from anchor toward the desired
// position and pulls in if blocked.
//
// Use:
//   spring_.anchor = target;
//   spring_.apply_mouse(dx, dy);
//   spring_.update(world);          // computes camera_position
//   camera.position = spring_.camera_position;
//   camera.yaw = spring_.yaw_deg;
//   camera.pitch = spring_.pitch_deg;
class SpringArm {
public:
    // ---- Configuration -----------------------------------------------------
    glm::vec3 anchor       = {0.f, 0.f, 0.f};
    float     desired_dist = 6.f;
    float     min_dist     = 0.6f;
    float     skin         = 0.4f;        // pull-in margin from blocking surface
    float     sensitivity  = 0.12f;       // deg per pixel
    float     pitch_min    = -85.f;
    float     pitch_max    =  60.f;

    // ---- Mutable state -----------------------------------------------------
    float     yaw_deg      = -90.f;       // facing -Z by default
    float     pitch_deg    =  -10.f;

    // ---- Outputs (set by update()) -----------------------------------------
    glm::vec3 camera_position{0.f, 0.f, 0.f};
    float     actual_dist  = 0.f;

    void apply_mouse(float dx, float dy);

    // Direction the camera looks (toward anchor).
    glm::vec3 forward() const;

    // Cast from anchor along (-forward) for desired_dist; pull in on hit.
    void update(const WorldCollision& world);
};

} // namespace pengine
