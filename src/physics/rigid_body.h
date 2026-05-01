#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace pengine {

// Plain rigid body with semi-implicit Euler integration. World-space pose,
// body-local inverse inertia tensor (only valid for diagonal inertias today).
struct RigidBody {
    glm::vec3 position    = {0.f, 0.f, 0.f};
    glm::quat orientation = {1.f, 0.f, 0.f, 0.f};
    glm::vec3 linear_vel  = {0.f, 0.f, 0.f};
    glm::vec3 angular_vel = {0.f, 0.f, 0.f};

    float     mass             = 1.f;
    glm::vec3 inv_inertia_diag = {1.f, 1.f, 1.f}; // body-local

    // Force/torque accumulators (cleared each integrate()).
    glm::vec3 force_accum  = {0.f, 0.f, 0.f};
    glm::vec3 torque_accum = {0.f, 0.f, 0.f};

    // ---- Helpers ------------------------------------------------------------
    glm::mat3 body_to_world() const { return glm::mat3_cast(orientation); }

    glm::vec3 to_world_dir(const glm::vec3& body_dir) const {
        return orientation * body_dir;
    }
    glm::vec3 to_world_point(const glm::vec3& body_pt) const {
        return position + orientation * body_pt;
    }

    // Velocity of a world-space point on the body.
    glm::vec3 point_velocity(const glm::vec3& world_pt) const {
        return linear_vel + glm::cross(angular_vel, world_pt - position);
    }

    glm::mat3 inv_inertia_world() const {
        glm::mat3 R = body_to_world();
        glm::mat3 I_inv_local{0.f};
        I_inv_local[0][0] = inv_inertia_diag.x;
        I_inv_local[1][1] = inv_inertia_diag.y;
        I_inv_local[2][2] = inv_inertia_diag.z;
        return R * I_inv_local * glm::transpose(R);
    }

    // Inertia of a uniform-density box with given full extents.
    void set_box_inertia(float m, const glm::vec3& full_extents) {
        mass = m;
        float ex = full_extents.x, ey = full_extents.y, ez = full_extents.z;
        glm::vec3 I{
            (m / 12.f) * (ey * ey + ez * ez),
            (m / 12.f) * (ex * ex + ez * ez),
            (m / 12.f) * (ex * ex + ey * ey)
        };
        inv_inertia_diag = {1.f / I.x, 1.f / I.y, 1.f / I.z};
    }

    // ---- Apply forces/impulses ---------------------------------------------
    void apply_central_force(const glm::vec3& f)        { force_accum += f; }
    void apply_torque       (const glm::vec3& t)        { torque_accum += t; }
    void apply_force_at(const glm::vec3& f, const glm::vec3& world_pt) {
        force_accum  += f;
        torque_accum += glm::cross(world_pt - position, f);
    }
    void apply_impulse_at(const glm::vec3& imp, const glm::vec3& world_pt) {
        linear_vel  += imp / mass;
        angular_vel += inv_inertia_world() * glm::cross(world_pt - position, imp);
    }

    // ---- Integrate ----------------------------------------------------------
    void integrate(float dt) {
        // Linear.
        linear_vel += (force_accum / mass) * dt;
        position   += linear_vel * dt;

        // Angular.
        glm::vec3 ang_acc = inv_inertia_world() * torque_accum;
        angular_vel      += ang_acc * dt;

        // Quaternion derivative: dq/dt = 0.5 * (0, ω) * q
        glm::quat w_q{0.f, angular_vel.x, angular_vel.y, angular_vel.z};
        orientation += 0.5f * (w_q * orientation) * dt;
        orientation  = glm::normalize(orientation);

        force_accum  = {0.f, 0.f, 0.f};
        torque_accum = {0.f, 0.f, 0.f};
    }
};

} // namespace pengine
