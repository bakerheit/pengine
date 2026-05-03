#include "physics/character_controller.h"

#include <SDL_scancode.h>
#include <cmath>

#include "physics/world_collision.h"
#include "platform/input.h"
#include "world/city_layout.h"

namespace pengine {

void CharacterController::teleport(const glm::vec3& feet_pos) {
    pos_      = feet_pos;
    vel_      = {0.f, 0.f, 0.f};
    grounded_ = false;
}

void CharacterController::update(float dt, const Input& input, float forward_yaw_deg,
                                  bool sprint, const WorldCollision& world) {
    // ---- Horizontal input ----------------------------------------------------
    float yaw_rad = glm::radians(forward_yaw_deg);
    glm::vec3 fwd { std::cos(yaw_rad), 0.f, std::sin(yaw_rad) };
    glm::vec3 rgt { -fwd.z,            0.f, fwd.x          };

    glm::vec3 wish{0.f};
    if (input.down(SDL_SCANCODE_W)) wish += fwd;
    if (input.down(SDL_SCANCODE_S)) wish -= fwd;
    if (input.down(SDL_SCANCODE_D)) wish += rgt;
    if (input.down(SDL_SCANCODE_A)) wish -= rgt;
    float wish_len = glm::length(wish);
    if (wish_len > 1e-4f) wish /= wish_len;

    glm::vec3 horiz_vel = wish * (sprint ? SPRINT_SPEED : MOVE_SPEED);
    // Mirror horizontal wish velocity into vel_.xz so callers (e.g. the
    // walk-cycle animation) can ask "are we moving?" from velocity().
    vel_.x = horiz_vel.x;
    vel_.z = horiz_vel.z;

    // ---- Vertical (gravity + jump) ------------------------------------------
    if (grounded_ && input.pressed(SDL_SCANCODE_SPACE)) {
        vel_.y    = JUMP_SPEED;
        grounded_ = false;
    }
    vel_.y += GRAVITY * dt;

    // ---- Integrate ----------------------------------------------------------
    glm::vec3 next = pos_;
    next.x += horiz_vel.x * dt;
    next.z += horiz_vel.z * dt;
    next.y += vel_.y      * dt;

    // ---- Resolve buildings (XZ only) ----------------------------------------
    glm::vec2 xz = world.resolve_cylinder_xz({next.x, next.z}, next.y, HEIGHT, RADIUS);
    next.x = xz.x;
    next.z = xz.y;

    // ---- Resolve terrain (always above heightmap + sidewalks) --------------
    float ground_y = city_ground_sample(next.x, next.z);
    if (next.y <= ground_y) {
        next.y    = ground_y;
        vel_.y    = 0.f;
        grounded_ = true;
    } else {
        // Re-check grounded after horizontal move (edge case: walked off a slope).
        grounded_ = (next.y - ground_y) < 0.05f;
    }

    pos_ = next;
}

} // namespace pengine
