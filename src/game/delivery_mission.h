#pragma once

#include <cmath>

#include <glm/glm.hpp>

#include "city/authored_staff.h"
#include "game/save_game.h"

namespace apricot {

inline constexpr float kDeliveryInteractionRadiusM = 2.0f;

inline bool mission_car_cue_visible(MissionStage stage, bool on_foot) {
    return stage == MissionStage::DeliveryNeedsCar && on_foot;
}

inline bool enter_mission_car(MissionStage& stage) {
    if (stage != MissionStage::DeliveryNeedsCar) return false;
    stage = MissionStage::DeliveryActive;
    return true;
}

struct FloatingMissionCue {
    glm::vec2 screen{};
    bool visible = false;
};

inline FloatingMissionCue project_mission_cue(glm::vec3 world,
                                               const glm::mat4& view_projection,
                                               glm::vec2 canvas) {
    const glm::vec4 clip = view_projection * glm::vec4(world, 1.0f);
    if (clip.w <= 0.05f || std::fabs(clip.x) > clip.w ||
        std::fabs(clip.y) > clip.w || std::fabs(clip.z) > clip.w ||
        canvas.x <= 0 || canvas.y <= 0) return {};
    const glm::vec2 ndc = glm::vec2{clip} / clip.w;
    return {{(ndc.x * .5f + .5f) * canvas.x,
             (.5f - ndc.y * .5f) * canvas.y}, true};
}

inline bool delivery_contact(const glm::vec3& player_position, bool on_foot) {
    if (!on_foot) return false;
    const glm::vec3 devon = city::devon_position();
    const float dx = player_position.x - devon.x;
    const float dz = player_position.z - devon.z;
    return dx * dx + dz * dz <=
               kDeliveryInteractionRadiusM * kDeliveryInteractionRadiusM &&
           std::fabs(player_position.y - devon.y) <= 1.0f;
}

inline bool complete_delivery(MissionStage& stage,
                              const glm::vec3& player_position,
                              bool on_foot) {
    if (stage != MissionStage::DeliveryActive ||
        !delivery_contact(player_position, on_foot)) {
        return false;
    }
    stage = MissionStage::DeliveryComplete;
    return true;
}

}  // namespace apricot
