#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace apricot {

// Simulation yaw wraps at +/-pi. A plain mix crosses the opposite heading
// there, throwing an orbit camera across the player for one rendered frame.
inline float interpolate_camera_yaw(float from, float to, float alpha) {
    constexpr float turn = 6.28318530717958647692f;
    return from + std::remainder(to - from, turn) * std::clamp(alpha, 0.0f, 1.0f);
}

// Render-side third-person camera controller. It owns only presentation state:
// the vehicle simulation never reads it, so mouse movement, frame rate and a
// camera collision can never change a replay.
struct ChaseCameraPose {
    glm::vec3 target{0.0f};
    glm::vec3 collision_pivot{0.0f};
    glm::vec3 desired_eye{0.0f};
    float fov_y = glm::radians(62.0f);
};

class ChaseCameraRig {
public:
    void reset() {
        ready_ = false;
        orbit_yaw_ = 0.0f;
        orbit_pitch_ = 0.0f;
        look_idle_ = 0.0f;
        look_back_was_ = false;
        shake_ = 0.0f;
        shake_phase_ = 0.0f;
    }

    void cycle() { set_mode(mode_ + 1); }
    void set_mode(int mode) {
        mode_ = mode % 3;
        if (mode_ < 0) mode_ += 3;
    }
    int mode() const { return mode_; }

    void set_auto_recenter(bool enabled) { auto_recenter_ = enabled; }
    bool auto_recenter() const { return auto_recenter_; }
    void set_orbit(float yaw, float pitch) {
        orbit_yaw_ = wrap_angle(yaw);
        orbit_pitch_ = std::clamp(pitch, -0.32f, 0.62f);
        look_idle_ = 0.0f;
    }
    void set_camera_shake(bool enabled) {
        camera_shake_ = enabled;
        if (!enabled) { shake_ = 0.0f; shake_phase_ = 0.0f; }
    }

    const char* mode_name() const {
        static constexpr const char* kNames[3] = {"NEAR", "CHASE", "FAR"};
        return kNames[mode_];
    }

    void add_impact(float impact_speed) {
        if (!camera_shake_) return;
        const float kick = std::clamp((impact_speed - 3.0f) * 0.028f, 0.0f, 0.42f);
        shake_ = std::max(shake_, kick);
    }

    ChaseCameraPose update(glm::vec3 car_position, glm::vec3 car_forward,
                           glm::vec3 car_velocity, float yaw_rate,
                           float look_dx, float look_dy, bool look_back,
                           float dt) {
        const float safe_dt = std::clamp(dt, 0.0f, 0.1f);
        glm::vec3 flat_forward{car_forward.x, 0.0f, car_forward.z};
        const float forward_len = glm::length(flat_forward);
        if (forward_len > 1e-5f) {
            flat_forward /= forward_len;
        } else {
            flat_forward = {0.0f, 0.0f, -1.0f};
        }

        if (std::fabs(look_dx) + std::fabs(look_dy) > 1e-5f) {
            orbit_yaw_ = wrap_angle(orbit_yaw_ + look_dx);
            orbit_pitch_ = std::clamp(orbit_pitch_ + look_dy, -0.32f, 0.62f);
            look_idle_ = 0.0f;
        } else {
            look_idle_ += safe_dt;
            if (auto_recenter_ && look_idle_ > 0.85f && !look_back) {
                const float recenter = 1.0f - std::exp(-2.8f * safe_dt);
                orbit_yaw_ = approach_angle(orbit_yaw_, 0.0f, recenter);
                orbit_pitch_ = glm::mix(orbit_pitch_, 0.0f, recenter);
            }
        }

        const float car_yaw = std::atan2(flat_forward.x, -flat_forward.z);
        const float back_turn = look_back ? kPi : 0.0f;
        const float desired_heading =
            wrap_angle(car_yaw + orbit_yaw_ + back_turn +
                       std::clamp(yaw_rate * 0.10f, -0.16f, 0.16f));

        if (!ready_ || look_back != look_back_was_) {
            heading_ = desired_heading;
        } else {
            const float heading_blend = 1.0f - std::exp(-9.0f * safe_dt);
            heading_ = approach_angle(heading_, desired_heading, heading_blend);
        }
        look_back_was_ = look_back;

        const glm::vec3 camera_heading{std::sin(heading_), 0.0f,
                                       -std::cos(heading_)};
        const glm::vec3 right{-camera_heading.z, 0.0f, camera_heading.x};
        const float speed = glm::length(glm::vec2{car_velocity.x, car_velocity.z});
        const float speed01 = std::clamp(speed / 42.0f, 0.0f, 1.0f);

        static constexpr float kBack[3] = {5.6f, 7.8f, 10.4f};
        static constexpr float kHeight[3] = {2.25f, 3.05f, 4.0f};
        static constexpr float kSpeedBack[3] = {1.0f, 2.6f, 3.8f};

        // Keep the chassis itself at the visual centre. This used to aim 2.7 m
        // ahead in the default mode, add velocity lead on top, and smooth that
        // point in world space. The result made the hood the anchor and let the
        // car wander around the frame. Turn anticipation now moves the camera
        // around the car through heading_ above; its focus stays on the car.
        const glm::vec3 target =
            car_position + glm::vec3{0.0f, 0.75f, 0.0f};
        ready_ = true;

        const float back = kBack[mode_] + kSpeedBack[mode_] * speed01;
        const float orbit_vertical = std::sin(orbit_pitch_) * back;
        const float orbit_flat = std::cos(orbit_pitch_) * back;
        glm::vec3 eye = target - camera_heading * orbit_flat +
                        glm::vec3{0.0f, kHeight[mode_] + orbit_vertical, 0.0f};

        // A small deterministic presentation kick. It is frame-rate dependent
        // by design and never feeds the simulation.
        shake_phase_ += safe_dt * 41.0f;
        if (shake_ > 0.0f) {
            eye += right * (std::sin(shake_phase_) * shake_) +
                   glm::vec3{0.0f, std::sin(shake_phase_ * 1.73f) * shake_ * 0.55f,
                             0.0f};
            shake_ *= std::exp(-5.5f * safe_dt);
            if (shake_ < 0.002f) shake_ = 0.0f;
        }

        ChaseCameraPose out;
        out.target = target;
        // Obstruction starts at the car, never the ahead-looking target. The
        // look target can legitimately be beyond a wall just before impact;
        // raycasting from there would begin inside the wall and pull the
        // camera into the exact geometry it is meant to avoid.
        out.collision_pivot = target;
        out.desired_eye = eye;
        out.fov_y = glm::radians(62.0f + speed01 * 10.0f);
        return out;
    }

private:
    static constexpr float kPi = 3.14159265358979323846f;
    static constexpr float kTwoPi = 2.0f * kPi;

    static float wrap_angle(float a) {
        while (a > kPi) a -= kTwoPi;
        while (a < -kPi) a += kTwoPi;
        return a;
    }

    static float approach_angle(float from, float to, float amount) {
        return wrap_angle(from + wrap_angle(to - from) * amount);
    }

    float heading_ = 0.0f;
    float orbit_yaw_ = 0.0f;
    float orbit_pitch_ = 0.0f;
    float look_idle_ = 0.0f;
    float shake_ = 0.0f;
    float shake_phase_ = 0.0f;
    int mode_ = 1;
    bool ready_ = false;
    bool look_back_was_ = false;
    bool auto_recenter_ = true;
    bool camera_shake_ = true;
};

}  // namespace apricot
