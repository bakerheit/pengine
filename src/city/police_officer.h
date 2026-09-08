#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "game/vehicle_transition.h"

namespace apricot {

inline constexpr float kPoliceArrestRangeM = 2.0f;
inline constexpr float kPoliceArrestHoldSeconds = 3.0f;
// Leave room for ground height and the walking solver's stopping tolerance.
inline constexpr float kPoliceOfficerFootStandOffM = kPoliceArrestRangeM - 0.4f;

// The officer belongs to the cruiser's stable (lane_key, slot) identity. There
// is no second pedestrian spawn to duplicate when the officer changes seats.
enum class PoliceOfficerPhase : uint8_t {
    Seated, Braking, Exiting, Pursuing, Returning, Entering
};

struct PoliceOfficerState {
    PoliceOfficerPhase phase = PoliceOfficerPhase::Seated;
    glm::vec3 previous_pos{0.0f};
    glm::vec3 pos{0.0f};                 // feet / interpolated occupant root
    float previous_heading = 0.0f;
    float heading = 0.0f;               // character yaw: sin(yaw), 0, -cos(yaw)
    float distance_walked_m = 0.0f;
    glm::vec3 door_pos{0.0f};            // grounded transition standing endpoint
    float door_heading = 0.0f;
    VehicleTransitionState transition{};
    uint32_t stationary_ticks = 0;
};

// Renderer supplies these once from the actual fitted police model. Native
// model dimensions never enter the pure crowd simulation. Coordinates match a
// chassis with +X right and -Z forward; the driver's door is on the left.
struct PoliceOfficerVehicleLayout {
    glm::vec3 driver_seat_local{-0.42f, 0.0f, -0.18f};
    glm::vec3 driver_door_local{-1.88f, 0.0f, -0.18f};
    float half_width_m = 1.05f;
    float half_length_m = 2.5f;
};

inline bool police_officer_on_foot(const PoliceOfficerState& state) {
    return state.phase == PoliceOfficerPhase::Pursuing ||
           state.phase == PoliceOfficerPhase::Returning;
}

inline bool police_officer_driving_allowed(const PoliceOfficerState& state) {
    return state.phase == PoliceOfficerPhase::Seated;
}

inline float police_officer_door_open(const PoliceOfficerState& state,
                                      float alpha = 1.0f) {
    return vehicle_transition_door_open(
        sample_vehicle_transition(state.transition, alpha));
}

struct PoliceOfficerStepInput {
    bool engaged = false;
    bool target_on_foot = false;
    float target_speed_mps = 0.0f;
    float target_distance_m = 0.0f;
    float vehicle_speed_mps = 0.0f;
    float stopping_distance_m = 0.0f;
    bool safe_road_position = false;
    bool door_clear = false;
    bool at_door = false;
};

// One 120 Hz tick. Finish a started traversal even if the chase ends halfway:
// changing the direction at an arbitrary pose snaps the person through the
// door. Returning and entering always keep the cruiser physically braked.
inline void step_police_officer_phase(PoliceOfficerState& state,
                                      const PoliceOfficerStepInput& input) {
    const bool slow_suspect = input.target_on_foot ||
                             input.target_speed_mps <= 2.25f;
    const bool wants_foot_pursuit = input.engaged && slow_suspect;
    if (input.vehicle_speed_mps <= 0.08f)
        state.stationary_ticks = std::min<uint32_t>(120, state.stationary_ticks + 1);
    else state.stationary_ticks = 0;

    switch (state.phase) {
    case PoliceOfficerPhase::Seated:
        if (wants_foot_pursuit && input.safe_road_position &&
            input.target_distance_m <= 18.0f + input.stopping_distance_m)
            state.phase = PoliceOfficerPhase::Braking;
        break;
    case PoliceOfficerPhase::Braking:
        if (!wants_foot_pursuit || input.target_distance_m > 55.0f ||
            (state.stationary_ticks >= 24 && input.target_distance_m > 32.0f)) {
            state.phase = PoliceOfficerPhase::Seated;
            break;
        }
        if (state.stationary_ticks >= 24 && input.safe_road_position &&
            input.target_distance_m <= 32.0f && input.door_clear) {
            state.phase = PoliceOfficerPhase::Exiting;
            state.transition = {VehicleTransitionDirection::Exit, 0};
        }
        break;
    case PoliceOfficerPhase::Exiting:
        if (input.door_clear && state.stationary_ticks >= 24 &&
            advance_vehicle_transition(state.transition)) {
            state.transition = {};
            state.phase = wants_foot_pursuit ? PoliceOfficerPhase::Pursuing
                                           : PoliceOfficerPhase::Returning;
        }
        break;
    case PoliceOfficerPhase::Pursuing:
        if (!input.engaged || (!input.target_on_foot &&
                               input.target_speed_mps > 5.0f) ||
            input.target_distance_m > 65.0f)
            state.phase = PoliceOfficerPhase::Returning;
        break;
    case PoliceOfficerPhase::Returning:
        if (input.at_door && input.door_clear && state.stationary_ticks >= 24) {
            state.phase = PoliceOfficerPhase::Entering;
            state.transition = {VehicleTransitionDirection::Enter, 0};
        }
        break;
    case PoliceOfficerPhase::Entering:
        if (input.door_clear && state.stationary_ticks >= 24 &&
            advance_vehicle_transition(state.transition)) {
            state.transition = {};
            state.phase = PoliceOfficerPhase::Seated;
        }
        break;
    }
}

}  // namespace apricot
