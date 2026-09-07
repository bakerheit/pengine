#pragma once
#include "game/vehicle_transition.h"

namespace apricot {
inline constexpr uint32_t kBoatTransitionTicks=360;
struct BoatTransitionState {
    VehicleTransitionDirection direction=VehicleTransitionDirection::None;
    uint32_t tick=0;
    bool active() const {return direction!=VehicleTransitionDirection::None;}
};
inline float boat_transition_fraction(const BoatTransitionState& state,float alpha=1.f) {
    if(!state.active()) return 0;
    const float a=std::isfinite(alpha)?std::clamp(alpha,0.f,1.f):1.f;
    const float elapsed=std::min(float(kBoatTransitionTicks),state.tick?float(state.tick-1)+a:0.f);
    const float t=elapsed/float(kBoatTransitionTicks);
    return state.direction==VehicleTransitionDirection::Exit?1.f-t:t;
}
inline bool advance_boat_transition(BoatTransitionState& state) {
    return state.active() && state.tick<kBoatTransitionTicks && ++state.tick==kBoatTransitionTicks;
}
} // namespace apricot
