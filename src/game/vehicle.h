#pragma once

#include <array>

#include <glm/glm.hpp>

#include "physics/rigid_body.h"

namespace pengine {

class WorldCollision;

struct Wheel {
    glm::vec3 mount_local{0.f};      // chassis-local mount point (suspension top)
    bool      is_steering = false;
    bool      is_driven   = false;

    // Updated each substep.
    bool      grounded         = false;
    float     compression      = 0.f;       // 0 = at rest length, +ve = compressed
    float     prev_compression = 0.f;
    glm::vec3 contact_world{0.f};
    glm::vec3 contact_normal{0.f, 1.f, 0.f};
    float     normal_force     = 0.f;       // for friction-circle / debug
    float     visual_drop      = 0.f;       // mount→wheel-centre offset, for rendering
};

class Vehicle {
public:
    // ---- Tuning (tweak freely) ---------------------------------------------
    glm::vec3 chassis_full_extents = {2.0f, 0.8f, 4.0f};
    float chassis_mass     = 1500.f;
    float wheel_radius     = 0.35f;
    float suspension_rest  = 0.40f;
    float suspension_max   = 0.65f;
    float spring_k         = 60000.f;
    float damper_k         = 3800.f;
    float engine_force     = 22000.f;     // total drive force at 0 m/s
    float reverse_force    = 13000.f;     // total reverse force at 0 m/s
    float brake_force      = 36000.f;     // total brake force
    float max_speed        = 32.f;        // m/s   (~115 km/h) forward
    float max_reverse      = 12.f;        // m/s   (~ 43 km/h) reverse
    float max_steer_rad    = glm::radians(28.f);
    float steer_lerp       = 8.f;         // per-second toward input
    float lateral_grip     = 14.f;        // per-wheel lateral velocity-kill rate
    float handbrake_grip   = 1.5f;        // rear lateral grip when handbrake held
    float linear_drag      = 0.30f;       // 1/s air drag (horizontal)
    float angular_drag     = 2.5f;        // 1/s
    float gravity          = -18.f;
    float chassis_collision_radius = 1.4f; // cylinder used vs buildings

    // ---- Lifecycle ---------------------------------------------------------
    void spawn(const glm::vec3& spawn_pos, float yaw_deg = -90.f);

    // Override the suspension-top point of one wheel after spawn. Used to
    // align raycasts with a non-cube body's actual wheel positions.
    void set_wheel_mount(int idx, const glm::vec3& mount_local) {
        if (idx >= 0 && idx < 4)
            wheels_[static_cast<std::size_t>(idx)].mount_local = mount_local;
    }

    // Per-frame inputs (clamped internally). throttle is signed:
    // +1 = forward full, -1 = reverse full.
    void set_inputs(float throttle, float brake, float steer, bool handbrake);

    // Fixed-rate physics step. Call N times per render frame.
    void substep(float dt, const WorldCollision& world);

    // ---- Queries -----------------------------------------------------------
    const RigidBody& body() const { return body_; }
    const std::array<Wheel, 4>& wheels() const { return wheels_; }

    glm::vec3 position()  const { return body_.position; }
    glm::quat orientation() const { return body_.orientation; }
    glm::vec3 forward()   const { return body_.to_world_dir({0.f, 0.f, -1.f}); }
    glm::vec3 right()     const { return body_.to_world_dir({1.f, 0.f, 0.f}); }
    glm::vec3 up()        const { return body_.to_world_dir({0.f, 1.f, 0.f}); }
    float     speed()     const { return glm::length(body_.linear_vel); }
    float     speed_kmh() const { return speed() * 3.6f; }
    bool      airborne()  const;
    float     steer_rad() const { return steer_rad_; }

private:
    RigidBody              body_;
    std::array<Wheel, 4>   wheels_;

    float throttle_  = 0.f;
    float brake_     = 0.f;
    float steer_in_  = 0.f;
    float steer_rad_ = 0.f;
    bool  handbrake_ = false;
};

} // namespace pengine
