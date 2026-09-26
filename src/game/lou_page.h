#pragma once

#include <glm/glm.hpp>

#include "game/payphones.h"
#include "game/save_game.h"

namespace apricot {

// After Devon: Lou pages Johnny, and Johnny calls him back from a payphone.
// This sets up the next mission; it is not that mission. The call moves the
// stage to LouCalled and stops there, because what Lou says on that call has
// not been written.

// Exactly what the pager shows.
inline constexpr const char* kLouPageText = "Call me at store - Lou";

// How long after the delivery the page arrives. Longer than the Mission
// Success card (6.25 s) so the two never share the screen, short enough to
// read as Lou having just heard from Devon.
inline constexpr float kLouPageDelaySeconds = 10.0f;

// One sim step of waiting for Lou's page. `waited_s` is how long the delivery
// has been complete; it restarts whenever the stage is anything else, so a
// load or a replayed delivery waits the full delay again. Returns true on the
// one step the page goes out, having moved the stage to CallLou; the caller
// sends kLouPageText to the pager.
inline bool step_lou_page(MissionStage& stage, float& waited_s, float dt) {
    if (stage != MissionStage::DeliveryComplete) {
        waited_s = 0.0f;
        return false;
    }
    waited_s += dt;
    if (waited_s < kLouPageDelaySeconds) return false;
    waited_s = 0.0f;
    stage = MissionStage::CallLou;
    return true;
}

// Any payphone will do. The marker points at the nearest one, but a player
// who walks up to a different booth has still found a phone.
inline bool can_call_lou(MissionStage stage, const glm::vec3& feet, bool on_foot) {
    return stage == MissionStage::CallLou && payphone_in_reach(feet, on_foot) >= 0;
}

inline bool call_lou(MissionStage& stage, const glm::vec3& feet, bool on_foot) {
    if (!can_call_lou(stage, feet, on_foot)) return false;
    stage = MissionStage::LouCalled;
    return true;
}

}  // namespace apricot
