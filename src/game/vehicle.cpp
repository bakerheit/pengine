#include "game/vehicle.h"

#include <algorithm>
#include <cmath>

#include "physics/world_collision.h"
#include "world/heightmap.h"

namespace pengine {

namespace {

inline float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

// Project a world-space direction onto the ground plane defined by `n`,
// then renormalise. Returns zero if `d` is parallel to `n`.
glm::vec3 project_on_plane(const glm::vec3& d, const glm::vec3& n) {
    glm::vec3 p = d - n * glm::dot(d, n);
    float len = glm::length(p);
    if (len < 1e-5f) return glm::vec3{0.f};
    return p / len;
}

} // namespace

void Vehicle::spawn(const glm::vec3& spawn_pos, float yaw_deg) {
    body_ = RigidBody{};
    body_.set_box_inertia(chassis_mass, chassis_full_extents);
    body_.position    = spawn_pos;
    body_.orientation = glm::angleAxis(glm::radians(yaw_deg + 90.f),
                                        glm::vec3{0.f, 1.f, 0.f});
    body_.linear_vel  = {0.f, 0.f, 0.f};
    body_.angular_vel = {0.f, 0.f, 0.f};

    // Mounts at chassis bottom corners, slightly inset.
    float hx = chassis_full_extents.x * 0.5f - 0.15f;
    float hy = -chassis_full_extents.y * 0.5f;
    float hz = chassis_full_extents.z * 0.5f - 0.5f;

    wheels_[0] = Wheel{}; wheels_[0].mount_local = {-hx, hy, -hz}; wheels_[0].is_steering = true;  wheels_[0].is_driven = false; // FL
    wheels_[1] = Wheel{}; wheels_[1].mount_local = { hx, hy, -hz}; wheels_[1].is_steering = true;  wheels_[1].is_driven = false; // FR
    wheels_[2] = Wheel{}; wheels_[2].mount_local = {-hx, hy,  hz}; wheels_[2].is_steering = false; wheels_[2].is_driven = true;  // RL
    wheels_[3] = Wheel{}; wheels_[3].mount_local = { hx, hy,  hz}; wheels_[3].is_steering = false; wheels_[3].is_driven = true;  // RR

    throttle_ = brake_ = steer_in_ = steer_rad_ = 0.f;
    handbrake_ = false;
}

void Vehicle::set_inputs(float throttle, float brake, float steer, bool handbrake) {
    throttle_  = clampf(throttle, -1.f, 1.f);
    brake_     = clampf(brake,     0.f, 1.f);
    steer_in_  = clampf(steer,    -1.f, 1.f);
    handbrake_ = handbrake;
}

bool Vehicle::airborne() const {
    for (const Wheel& w : wheels_) if (w.grounded) return false;
    return true;
}

void Vehicle::substep(float dt, const WorldCollision& world) {
    // ---- Steering: lerp toward input ---------------------------------------
    float target_steer = steer_in_ * max_steer_rad;
    steer_rad_ += (target_steer - steer_rad_) * std::min(1.f, steer_lerp * dt);

    // ---- Wheel raycasts + suspension forces --------------------------------
    glm::vec3 chassis_down = body_.to_world_dir({0.f, -1.f, 0.f});
    glm::vec3 chassis_up   = -chassis_down;

    for (Wheel& w : wheels_) {
        glm::vec3 mount_w = body_.to_world_point(w.mount_local);
        float ray_len = suspension_max + wheel_radius;
        RayHit hit = world.raycast(mount_w, chassis_down, ray_len);

        if (hit.hit && hit.t <= ray_len) {
            float susp_len    = std::max(0.f, hit.t - wheel_radius);
            w.compression     = clampf(suspension_rest - susp_len, 0.f, suspension_rest);
            w.grounded        = true;
            w.contact_world   = mount_w + chassis_down * hit.t;
            w.contact_normal  = hit.normal;
            w.visual_drop     = susp_len;

            // Spring + damper. Force magnitude along chassis +Y.
            float dcompress  = (w.compression - w.prev_compression) / std::max(dt, 1e-5f);
            float force_mag  = w.compression * spring_k + dcompress * damper_k;
            force_mag        = std::max(0.f, force_mag);
            w.normal_force   = force_mag;
            body_.apply_force_at(chassis_up * force_mag, mount_w);
        } else {
            w.grounded     = false;
            w.compression  = 0.f;
            w.normal_force = 0.f;
            w.visual_drop  = suspension_rest; // hangs at rest length
        }
        w.prev_compression = w.compression;
    }

    // ---- Gravity (central) -------------------------------------------------
    body_.apply_central_force({0.f, gravity * body_.mass, 0.f});

    // ---- Drive / brake -----------------------------------------------------
    int  driven_grounded = 0;
    for (const Wheel& w : wheels_) if (w.is_driven && w.grounded) ++driven_grounded;
    if (driven_grounded == 0) driven_grounded = 1; // avoid /0

    glm::vec3 fwd_world = forward();
    float speed_signed  = glm::dot(body_.linear_vel, fwd_world);
    float drive_factor_fwd = clampf(1.f - std::max(0.f, speed_signed) / max_speed, 0.f, 1.f);
    float drive_factor_rev = clampf(1.f - std::max(0.f, -speed_signed) / max_reverse, 0.f, 1.f);

    for (Wheel& w : wheels_) {
        if (!w.grounded) continue;

        // Per-wheel ground-plane axes (steering for fronts).
        glm::vec3 wheel_fwd = body_.to_world_dir({0.f, 0.f, -1.f});
        if (w.is_steering) {
            float c = std::cos(steer_rad_), s = std::sin(steer_rad_);
            // Rotate around chassis up by steer_rad_.
            glm::vec3 wheel_right = body_.to_world_dir({1.f, 0.f, 0.f});
            wheel_fwd = wheel_fwd * c + wheel_right * s;
        }
        glm::vec3 fwd_g = project_on_plane(wheel_fwd, w.contact_normal);
        glm::vec3 lat_g = glm::normalize(glm::cross(w.contact_normal, fwd_g));
        if (glm::length(fwd_g) < 1e-4f) continue;

        // Throttle (only on driven wheels). Signed: + = forward, - = reverse.
        if (w.is_driven && throttle_ > 0.f) {
            float per_wheel = (engine_force / static_cast<float>(driven_grounded))
                              * drive_factor_fwd;
            body_.apply_force_at(fwd_g * per_wheel * throttle_, w.contact_world);
        } else if (w.is_driven && throttle_ < 0.f) {
            float per_wheel = (reverse_force / static_cast<float>(driven_grounded))
                              * drive_factor_rev;
            body_.apply_force_at(fwd_g * per_wheel * throttle_, w.contact_world);
        }

        // Brake (all wheels). Opposes current contact-point velocity along fwd_g.
        glm::vec3 v_contact = body_.point_velocity(w.contact_world);
        float v_long = glm::dot(v_contact, fwd_g);
        if (brake_ > 0.f) {
            float bf = (brake_force / 4.f) * brake_;
            // Brake force opposes longitudinal motion; clamp so we don't reverse.
            float impulse_cap = std::abs(v_long) * (body_.mass / 4.f) / std::max(dt, 1e-5f);
            float bmag = std::min(bf, impulse_cap);
            if (v_long > 0.f) bmag = -bmag;
            body_.apply_force_at(fwd_g * bmag, w.contact_world);
        } else if (throttle_ == 0.f) {
            // Engine braking + rolling resistance on coast. Strong enough to
            // hold the car on a gentle slope, weak enough not to stop it
            // unrealistically fast at speed.
            float coast_force = 250.f + std::abs(v_long) * 80.f;
            float impulse_cap = std::abs(v_long) * (body_.mass / 4.f) / std::max(dt, 1e-5f);
            float cmag = std::min(coast_force, impulse_cap);
            if (v_long > 0.f)      cmag = -cmag;
            else if (v_long < 0.f) {/* already +ve */}
            else                   cmag = 0.f;
            body_.apply_force_at(fwd_g * cmag, w.contact_world);
        }

        // Lateral grip: kill sideways velocity at the contact point. Apply as
        // an impulse this substep (capped by friction circle vs normal force).
        float v_lat = glm::dot(v_contact, lat_g);
        float grip  = lateral_grip;
        if (handbrake_ && !w.is_steering) grip = handbrake_grip; // rear loose
        // Required impulse to kill v_lat over this substep (per wheel, mass/4).
        float wanted_impulse = -v_lat * (body_.mass / 4.f) * std::min(1.f, grip * dt);
        // Friction circle cap: total horizontal friction <= mu * N.
        float mu = 1.4f;
        float max_friction_impulse = mu * w.normal_force * dt;
        wanted_impulse = clampf(wanted_impulse, -max_friction_impulse, max_friction_impulse);
        body_.apply_impulse_at(lat_g * wanted_impulse, w.contact_world);
    }

    // ---- Integrate ---------------------------------------------------------
    body_.integrate(dt);

    // ---- Drag --------------------------------------------------------------
    {
        // Linear drag on horizontal motion only (don't fight gravity).
        glm::vec3 lv = body_.linear_vel;
        glm::vec3 lv_h{lv.x, 0.f, lv.z};
        lv_h *= std::exp(-linear_drag * dt);
        body_.linear_vel = {lv_h.x, lv.y, lv_h.z};

        body_.angular_vel *= std::exp(-angular_drag * dt);
    }

    // ---- Cylinder vs buildings (XZ) ----------------------------------------
    {
        glm::vec2 xz{body_.position.x, body_.position.z};
        float feet_y = body_.position.y - chassis_full_extents.y * 0.5f - suspension_rest;
        glm::vec2 fixed = world.resolve_cylinder_xz(xz, feet_y,
                                                     chassis_full_extents.y + suspension_rest,
                                                     chassis_collision_radius);
        if (fixed != xz) {
            // Position correction: also kill velocity along the push direction.
            glm::vec3 push{fixed.x - xz.x, 0.f, fixed.y - xz.y};
            body_.position.x = fixed.x;
            body_.position.z = fixed.y;
            float plen = glm::length(push);
            if (plen > 1e-4f) {
                glm::vec3 n = push / plen;
                float vn = glm::dot(body_.linear_vel, n);
                if (vn < 0.f) body_.linear_vel -= n * vn;
            }
        }
    }

    // ---- Hard floor: don't allow chassis under terrain ----------------------
    {
        float ground = Heightmap::sample(body_.position.x, body_.position.z);
        float min_y  = ground + chassis_full_extents.y * 0.5f - 0.05f;
        if (body_.position.y < min_y) {
            body_.position.y = min_y;
            if (body_.linear_vel.y < 0.f) body_.linear_vel.y = 0.f;
        }
    }
}

} // namespace pengine
