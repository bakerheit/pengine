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
inline constexpr float kPoliceBlockedApproachRangeM = 45.0f;

// An officer on foot is a person the player can shoot at. Three centre-mass
// pistol rounds put one down; the window is long enough that neutralising a
// unit is worth the extra heat it costs, and short enough that a chase does
// not quietly drain to nothing while the player hides.
inline constexpr float kPoliceOfficerHealth = 100.0f;
inline constexpr float kPoliceOfficerBulletDamage = 34.0f;
inline constexpr uint32_t kPoliceOfficerDownedTicks = 1440;  // 12 s at 120 Hz

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
    // Damage is carried by the officer, not by the cruiser: the unit keeps its
    // (lane_key, slot) identity through being shot, so no rig is orphaned and
    // no second spawn appears to replace a body.
    float health = kPoliceOfficerHealth;
    uint32_t downed_ticks = 0;
    glm::vec2 impact_dir_xz{0.0f, -1.0f};
    // Sticky, exactly like a pedestrian's. It selects the authored bullet fall
    // AND pins the clip's horizontal root for the whole fall-and-recover cycle;
    // clearing it at get-up would hand the recovery a different root than the
    // fall had and pop the body across the road.
    bool impact_from_bullet = false;
    bool armed = false;
    int64_t next_shot_step = -1;
    uint32_t shots_fired = 0;
    uint16_t weapon_flash_ticks = 0;
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

// On the ground. Holds the phase rather than clearing it, because the phase is
// what says which cruiser door this person owns; clearing it would strand the
// officer walking to a car they are no longer assigned to on the way back up.
inline bool police_officer_downed(const PoliceOfficerState& state) {
    return state.downed_ticks > 0;
}

// Only a person standing in the street can be shot. A seated officer is inside
// a body the bullet never tests, and pretending otherwise would let the player
// kill a driver through a door that stopped nothing.
inline bool police_officer_shootable(const PoliceOfficerState& state) {
    return police_officer_on_foot(state) && !police_officer_downed(state);
}

// One bullet. Returns true only for the round that puts the officer down, so
// the caller can charge the heat for THAT and not for every hit on a body
// already lying in the road.
inline bool police_officer_take_bullet(PoliceOfficerState& state, float damage,
                                       glm::vec2 direction_xz) {
    if (police_officer_downed(state) || !police_officer_on_foot(state)) return false;
    if (!std::isfinite(damage) || damage <= 0.0f) return false;
    const float length = std::sqrt(direction_xz.x * direction_xz.x +
                                   direction_xz.y * direction_xz.y);
    if (std::isfinite(length) && length > 1e-5f)
        state.impact_dir_xz = direction_xz / length;
    state.impact_from_bullet = true;
    state.health = std::max(0.0f, state.health - damage);
    if (state.health > 0.0f) return false;
    state.downed_ticks = kPoliceOfficerDownedTicks;
    state.armed = false;
    state.next_shot_step = -1;
    state.weapon_flash_ticks = 0;
    return true;
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
    bool approach_blocked = false;
    bool door_clear = false;
    bool at_door = false;
};

// One 120 Hz tick. Finish a started traversal even if the chase ends halfway:
// changing the direction at an arbitrary pose snaps the person through the
// door. Returning and entering always keep the cruiser physically braked.
inline void step_police_officer_phase(PoliceOfficerState& state,
                                      const PoliceOfficerStepInput& input) {
    // A body on the road is not making decisions. The phase is frozen for the
    // whole window — including the door transitions, which would otherwise
    // advance a traversal with nobody walking it — and health comes back with
    // the officer so the same unit can be put down again.
    if (state.downed_ticks > 0) {
        if (--state.downed_ticks == 0) state.health = kPoliceOfficerHealth;
        state.stationary_ticks =
            std::min<uint32_t>(120, state.stationary_ticks + 1);
        return;
    }
    const bool slow_suspect = input.target_on_foot ||
                             input.target_speed_mps <= 2.25f;
    const bool wants_foot_pursuit = input.engaged && slow_suspect;
    const bool stopped_in_queue = input.approach_blocked &&
                                  input.vehicle_speed_mps <= 0.08f;
    const float exit_range = stopped_in_queue ? kPoliceBlockedApproachRangeM : 32.0f;
    if (input.vehicle_speed_mps <= 0.08f)
        state.stationary_ticks = std::min<uint32_t>(120, state.stationary_ticks + 1);
    else state.stationary_ticks = 0;

    switch (state.phase) {
    case PoliceOfficerPhase::Seated:
        if (wants_foot_pursuit && input.safe_road_position &&
            (input.target_distance_m <= 18.0f + input.stopping_distance_m ||
             (stopped_in_queue && input.target_distance_m <= exit_range)))
            state.phase = PoliceOfficerPhase::Braking;
        break;
    case PoliceOfficerPhase::Braking:
        if (!wants_foot_pursuit || input.target_distance_m > 55.0f ||
            (state.stationary_ticks >= 24 && input.target_distance_m > exit_range)) {
            state.phase = PoliceOfficerPhase::Seated;
            break;
        }
        if (state.stationary_ticks >= 24 && input.safe_road_position &&
            input.target_distance_m <= exit_range && input.door_clear) {
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
