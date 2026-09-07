#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "core/input_frame.h"
#include "physics/terrain_collider.h"

namespace apricot {

// A forgiving fixed-step arcade aircraft, deliberately separate from cars.
// Origin is the bottom of the landing gear; authored nose is local +Z.
struct AircraftState {
    glm::vec3 position{};
    glm::vec3 velocity{};
    float yaw = 0, pitch = 0, roll = 0;
    float speed = 0, throttle = 0;
    bool grounded = true;
    bool crashed = false;
};

inline glm::quat aircraft_rotation(const AircraftState& s) {
    return glm::angleAxis(s.yaw, glm::vec3{0,1,0}) *
           glm::angleAxis(-s.pitch, glm::vec3{1,0,0}) *
           glm::angleAxis(s.roll, glm::vec3{0,0,1});
}
inline glm::vec3 aircraft_forward(const AircraftState& s) {
    return aircraft_rotation(s) * glm::vec3{0,0,1};
}
inline glm::vec3 aircraft_point(const AircraftState& s, glm::vec3 local) {
    return s.position + aircraft_rotation(s) * local;
}
inline bool aircraft_can_exit(const AircraftState& s) {
    return s.grounded && glm::length(s.velocity) < 1.0f;
}
inline bool aircraft_in_boarding_range(const AircraftState& s,
                                        glm::vec3 person, float ground) {
    if (!aircraft_can_exit(s) || s.crashed ||
        !std::isfinite(person.x + person.y + person.z + ground)) return false;
    const auto p = glm::inverse(aircraft_rotation(s)) * (person - s.position);
    // Ground-level access beneath the authored forward port door. Never board
    // through the starboard side, the roof, engines, or the whole wing radius.
    return p.x < -1.6f && p.x > -4.8f && std::fabs(p.z - 10.3f) < 2.0f &&
           person.y >= ground - .45f && person.y <= ground + .65f;
}

inline AircraftState step_aircraft(const AircraftState& previous,
                                    const InputFrame& input,
                                    const TerrainCollider& ground, float dt) {
    AircraftState s = previous;
    if (s.crashed || !(dt > 0) || !std::isfinite(dt)) return s;
    dt = std::min(dt, 1.0f / 30.0f);
    const float pitch_input = (is_held(input,kBtnShiftUp) ? 1.0f : 0.0f) -
                              (is_held(input,kBtnShiftDown) ? 1.0f : 0.0f);
    const float steer = std::clamp(input.steer, -1.0f, 1.0f);
    const float brake = std::clamp(input.handbrake, 0.0f, 1.0f);
    s.throttle = std::clamp(s.throttle +
        (input.throttle - input.brake) * .4f * dt, 0.0f, 1.0f);
    const float authority = std::clamp(s.speed / 32.0f, 0.0f, 1.0f);
    const float pitch_target = pitch_input * glm::radians(22.0f) * authority;
    s.pitch += (pitch_target - s.pitch) * std::min(1.0f, dt * 1.4f);
    if (s.grounded) {
        s.pitch = std::max(0.0f, s.pitch);
        s.roll = 0;
        s.yaw -= steer * .40f * std::clamp(s.speed / 6.0f, 0.0f, 1.0f) * dt;
    } else {
        const float target_roll = steer * glm::radians(50.0f) * authority;
        s.roll += (target_roll - s.roll) * std::min(1.0f, dt * 1.8f);
        s.yaw -= 9.81f * std::tan(s.roll) / std::max(s.speed, 25.0f) * dt;
    }
    s.yaw = std::remainder(s.yaw, glm::two_pi<float>());
    // Drag caps cruise speed; pitching up trades speed for altitude. With no
    // power a slow aircraft descends, rather than hovering at zero airspeed.
    const float acceleration = s.throttle * 8.0f - .0011f * s.speed * s.speed -
        (s.grounded ? .55f + brake * 12.0f : brake * 5.0f + 9.81f * std::sin(s.pitch));
    s.speed = std::clamp(s.speed + acceleration * dt, 0.0f, 105.0f);
    if (s.grounded && s.speed >= 32.0f && s.pitch > glm::radians(3.0f))
        s.grounded = false;
    glm::vec3 desired = aircraft_forward(s) * s.speed;
    if (s.grounded) desired.y = 0;
    else desired.y -= std::max(0.0f, 1.0f - s.speed / 32.0f) * 14.0f;
    s.velocity = glm::mix(s.velocity, desired, std::min(1.0f, dt * 3.0f));
    if (s.grounded) s.velocity = desired;
    s.position += s.velocity * dt;
    const auto support = ground.probe_down(s.position + glm::vec3{0,5,0}, 10000);
    const float floor = support.hit ? support.point.y : ground.height(s.position.x, s.position.z);
    if (s.grounded || s.position.y <= floor) {
        const bool hard_landing = !s.grounded &&
            (s.velocity.y < -8.0f || std::fabs(s.roll) > glm::radians(14.0f) ||
             std::fabs(s.pitch) > glm::radians(18.0f));
        s.position.y = floor;
        s.velocity.y = 0;
        if (!s.grounded) s.pitch = 0;
        s.grounded = true;
        s.roll = 0;
        s.crashed = hard_landing || floor < .2f;
    }
    // Sweep the nose, fuselage and both wings, not just the centre of a 28 m
    // aircraft. The host disables this aircraft's own colliders for the query.
    const glm::vec3 probes[] = {{0,3.3f,15.5f},{0,3.7f,7},{0,3.7f,-8},
        {-13.8f,3.3f,-4.5f},{13.8f,3.3f,-4.5f},
        {-7.5f,3,-1.5f},{7.5f,3,-1.5f},{0,8.5f,-13}};
    for (const auto local : probes) {
        const glm::vec3 from = aircraft_point(previous, local);
        const glm::vec3 travel = aircraft_point(s, local) - from;
        const float distance = glm::length(travel);
        if (distance < .00001f) continue;
        const auto hit = ground.raycast(from, travel / distance, distance);
        if (hit.hit && hit.distance <= distance) {
            s.position = previous.position;
            s.yaw = previous.yaw; s.pitch = previous.pitch; s.roll = previous.roll;
            s.crashed = true;
            break;
        }
    }
    if (s.crashed) { s.speed = 0; s.throttle = 0; s.velocity = {}; }
    return s;
}
} // namespace apricot
