#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "core/input_frame.h"
#include "gfx/chase_camera.h"

namespace apricot {

inline constexpr float kDrunkGameHours = 1.5f;
inline constexpr float kGameSecondsPerDay = 2880.0f;
inline constexpr float kDrunkDurationSeconds =
    kGameSecondsPerDay * kDrunkGameHours / 24.0f;
inline constexpr int kMaxDrinks = 5;
inline constexpr float kDrunkEffectScale = 1.25f;

struct DrunkState {
    float remaining_seconds = 0.0f;
    float elapsed_seconds = 0.0f;
    int drinks = 0;

    bool active() const { return remaining_seconds > 0.0f; }
    float intensity() const {
        if (!active() || drinks <= 0) return 0.0f;
        return std::min(1.0f, 0.25f + 0.1875f * static_cast<float>(drinks - 1));
    }
};

inline void start_drunk(DrunkState& state) {
    if (!state.active()) {
        state.drinks = 0;
        state.elapsed_seconds = 0.0f;
    }
    state.drinks = std::min(state.drinks + 1, kMaxDrinks);
    state.remaining_seconds = kDrunkDurationSeconds;
}

inline void step_drunk(DrunkState& state, float dt) {
    const float safe_dt = std::max(dt, 0.0f);
    if (!state.active()) return;
    state.elapsed_seconds += safe_dt;
    state.remaining_seconds =
        std::max(0.0f, state.remaining_seconds - safe_dt);
    if (!state.active()) {
        state.drinks = 0;
        state.elapsed_seconds = 0.0f;
    }
}

// Deterministic, low-frequency over-correction. The walk axes are rotated so
// a drunk player can genuinely veer across their intended heading; steering
// gets the same sway while throttle/brake stay readable.
inline InputFrame apply_drunk_input(InputFrame input, const DrunkState& state) {
    if (!state.active()) return input;
    const float t = state.elapsed_seconds;
    const float intensity = state.intensity() * kDrunkEffectScale;
    const float sway = intensity *
        (0.46f * std::sin(t * 1.65f) +
         0.19f * std::sin(t * 3.85f + 1.1f));
    const float steer_bias =
        intensity * 0.30f * std::sin(t * 2.35f + 0.7f);

    const glm::vec2 walk_axes{input.steer, input.throttle - input.brake};
    const float c = std::cos(sway);
    const float s = std::sin(sway);
    const glm::vec2 veer{walk_axes.x * c - walk_axes.y * s,
                         walk_axes.x * s + walk_axes.y * c};
    input.steer = std::clamp(veer.x + steer_bias, -1.0f, 1.0f);
    const float signed_longitudinal = std::clamp(veer.y, -1.0f, 1.0f);
    input.throttle = std::max(signed_longitudinal, 0.0f);
    input.brake = std::max(-signed_longitudinal, 0.0f);
    return input;
}

inline void apply_drunk_camera(ChaseCameraPose& pose, const DrunkState& state) {
    if (!state.active()) return;
    const float t = state.elapsed_seconds;
    const float intensity = state.intensity() * kDrunkEffectScale;
    const glm::vec3 offset{
        (0.15f * std::sin(t * 1.25f) + 0.07f * std::sin(t * 3.4f)) * intensity,
        (0.10f * std::sin(t * 1.9f + 0.6f)) * intensity,
        0.0f};
    pose.target += offset;
    pose.desired_eye += offset * 0.35f;
    pose.fov_y += glm::radians(
        intensity * 1.6f * std::sin(t * 1.1f));
}

}  // namespace apricot
