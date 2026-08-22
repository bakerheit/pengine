#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/input_frame.h"
#include "physics/terrain_collider.h"

namespace apricot {

inline constexpr int kWheelCount = 4;

// Wheel order is fixed and load-bearing — replay tapes and tuning files index
// by it. Front-left, front-right, rear-left, rear-right.
enum WheelIndex : int {
    kWheelFrontLeft = 0,
    kWheelFrontRight = 1,
    kWheelRearLeft = 2,
    kWheelRearRight = 3,
};

struct WheelState {
    glm::vec3 contact_point{0.0f};
    glm::vec3 contact_normal{0.0f, 1.0f, 0.0f};

    // Current compressed length of the suspension, in metres.
    float suspension_length = 0.0f;

    // Wheel rotation about its axle, in radians. Visual only — accumulate it
    // here rather than in the renderer so a replay reproduces the wheel
    // rotation exactly instead of deriving it from frame time.
    float spin = 0.0f;

    bool grounded = false;
};

// Everything the car IS at an instant. Plain data on purpose: this is what a
// replay diff compares and what a checkpoint restore overwrites, so it must be
// copyable and complete. No pointers, no back-references, no cached handles.
struct VehicleState {
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 angular_velocity{0.0f};

    // Smoothed steering actually applied, in radians. Distinct from the raw
    // InputFrame::steer: a rally car's wheels do not snap to full lock, and
    // interpolating in the renderer instead would make the physics disagree
    // with what the player sees.
    float steer_angle = 0.0f;

    float engine_rpm = 0.0f;
    int32_t gear = 1;

    std::array<WheelState, kWheelCount> wheels{};
};

// Handling constants. Separated from VehicleState because tuning is shared and
// immutable during a run, while state is per-car and mutated every step.
struct VehicleTuning {
    float mass_kg = 1200.0f;
    float gravity = 9.81f;

    // Chassis half-extents used to place the four suspension rays.
    float half_wheelbase = 1.35f;   // metres, front/back of centre
    float half_track = 0.78f;       // metres, left/right of centre

    float suspension_rest = 0.42f;  // metres
    float suspension_travel = 0.22f;
    float spring_k = 42000.0f;      // N/m
    float damper_c = 4200.0f;       // N.s/m

    float wheel_radius = 0.34f;

    float max_steer = 0.58f;        // radians at full lock (~33 degrees)
    float steer_rate = 4.0f;        // radians/second toward the target
    float engine_force = 9000.0f;   // N at full throttle
    float brake_force = 14000.0f;   // N at full brake
    float drag = 0.42f;             // quadratic, N per (m/s)^2
    float rolling_resistance = 12.0f;
};

// Advance the car by exactly one sim step.
//
// PURE FUNCTION OF ITS ARGUMENTS. Returns the next state rather than mutating,
// and reads NO wall clock, no global, no random source, and nothing about
// frame rate. `dt` is always kSimDt in practice, but it is a parameter so the
// function stays testable at other rates and so nobody is tempted to reach for
// a timer inside.
//
// That purity is the whole feature: replaying a tape of InputFrames through
// this function must reproduce a run exactly, and a ghost car is just the same
// function fed a different tape. The first `std::chrono` call added below this
// line silently ends that, and the symptom is "replays desync after a minute".
//
// PLACEHOLDER BODY. The current implementation integrates gravity and rests
// the chassis on the terrain so the plumbing is exercised end to end. There is
// no suspension force, no tyre model, no engine curve and no weight transfer —
// it does not drive like a car and is not meant to yet.
// TODO(vehicle dynamics ticket): implement four-ray suspension, a slip-based
// tyre model, engine/gearbox torque and weight transfer. Keep this signature.
VehicleState step_vehicle(const VehicleState& state, const VehicleTuning& tuning,
                          const InputFrame& input,
                          const TerrainCollider& collider, float dt);

}  // namespace apricot
