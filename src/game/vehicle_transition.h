#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace apricot {

enum class VehicleTransitionDirection { None, Enter, Exit };
enum class VehicleTransitionPhase { Approach, Reach, Traverse, Settle };
inline constexpr uint32_t kVehicleTransitionApproachTicks = 75;
inline constexpr uint32_t kVehicleTransitionReachTicks = 42;
inline constexpr uint32_t kVehicleTransitionTraverseTicks = 141;
inline constexpr uint32_t kVehicleTransitionSettleTicks = 42;
inline constexpr uint32_t kVehicleTransitionTicks = kVehicleTransitionApproachTicks +
    kVehicleTransitionReachTicks + kVehicleTransitionTraverseTicks + kVehicleTransitionSettleTicks;

struct VehicleTransitionState {
    VehicleTransitionDirection direction = VehicleTransitionDirection::None;
    uint32_t tick = 0;
    bool active() const { return direction != VehicleTransitionDirection::None; }
};

struct VehicleTransitionSample {
    VehicleTransitionPhase phase = VehicleTransitionPhase::Approach;
    float approach = 0;
    float reach = 0;
    float traverse = 0;
    float settle = 0;
    float progress = 0;
    VehicleTransitionDirection direction = VehicleTransitionDirection::None;
};

inline float vehicle_transition_ease(float value) {
    const float t = std::clamp(value, 0.f, 1.f);
    return t*t*(3.f-2.f*t);
}

inline float vehicle_transition_door_open(const VehicleTransitionSample& sample) {
    if (sample.direction == VehicleTransitionDirection::Exit)
        return sample.reach * (1.f - sample.settle);
    return vehicle_transition_ease((sample.reach - .3f) / .7f) * (1.f - sample.settle);
}

// alpha interpolates the previous/current fixed tick, exactly like car poses.
// Exit opens first, steps out, closes, then walks clear. No wall clock enters this state.
inline VehicleTransitionSample sample_vehicle_transition(
        const VehicleTransitionState& state, float alpha = 1.f) {
    VehicleTransitionSample out;
    if (!state.active()) return out;
    const float a = std::isfinite(alpha) ? std::clamp(alpha, 0.f, 1.f) : 1.f;
    const float elapsed = std::min(float(kVehicleTransitionTicks),
        state.tick ? float(state.tick-1)+a : 0.f);
    out.progress = elapsed / float(kVehicleTransitionTicks);
    out.direction = state.direction;
    if (state.direction == VehicleTransitionDirection::Exit) {
        out.approach = 1.f - vehicle_transition_ease((elapsed - 270.f) / 30.f);
        out.reach = vehicle_transition_ease(elapsed / 48.f);
        out.traverse = 1.f - std::clamp((elapsed - 48.f) / 174.f, 0.f, 1.f);
        out.settle = vehicle_transition_ease((elapsed - 222.f) / 48.f);
        out.phase = elapsed < 48.f ? VehicleTransitionPhase::Reach
            : elapsed < 222.f ? VehicleTransitionPhase::Traverse
            : elapsed < 270.f ? VehicleTransitionPhase::Settle : VehicleTransitionPhase::Approach;
        return out;
    }
    const float t = elapsed;
    constexpr float reach_begin=float(kVehicleTransitionApproachTicks);
    constexpr float traverse_begin=reach_begin+float(kVehicleTransitionReachTicks);
    constexpr float settle_begin=traverse_begin+float(kVehicleTransitionTraverseTicks);
    out.approach = vehicle_transition_ease(t / reach_begin);
    out.reach = vehicle_transition_ease((t-reach_begin) / float(kVehicleTransitionReachTicks));
    out.traverse = std::clamp((t-traverse_begin) / float(kVehicleTransitionTraverseTicks), 0.f, 1.f);
    out.settle = vehicle_transition_ease((t-settle_begin) / float(kVehicleTransitionSettleTicks));
    out.phase = t < reach_begin ? VehicleTransitionPhase::Approach
        : t < traverse_begin ? VehicleTransitionPhase::Reach
        : t < settle_begin ? VehicleTransitionPhase::Traverse : VehicleTransitionPhase::Settle;
    return out;
}

// Returns true once, on the completion edge. Owner commits or clears state.
inline bool advance_vehicle_transition(VehicleTransitionState& state) {
    if (!state.active() || state.tick >= kVehicleTransitionTicks) return false;
    return ++state.tick == kVehicleTransitionTicks;
}

} // namespace apricot
