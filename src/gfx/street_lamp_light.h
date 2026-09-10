#pragma once
#include <algorithm>
#include <optional>
#include "gfx/lighting.h"

namespace apricot {
inline constexpr float kStreetLampLightDistanceM=260.f;
inline glm::vec4 street_lamp_lens_tint(float night_level) {
    return {1.f,.80f,.52f,1.f+2.3f*std::clamp(night_level,0.f,1.f)};
}
inline std::optional<TrafficSpotLight> street_lamp_light(
        glm::vec3 position,glm::vec3 camera,float night_level) {
    const float power=std::clamp(night_level,0.f,1.f);
    const auto delta=position-camera;
    if(power<=.001f || glm::dot(delta,delta)>
            kStreetLampLightDistanceM*kStreetLampLightDistanceM)return std::nullopt;
    return TrafficSpotLight{glm::vec4{position,15.f},{0,-1,0,5.f*power}};
}
// Broad pools for the restaurant forecourts; keep the shared dusk/distance gate.
inline std::optional<TrafficSpotLight> parking_lamp_light(
        glm::vec3 position,glm::vec3 camera,float night_level) {
    auto light=street_lamp_light(position,camera,night_level);
    if(light) {
        light->position_range.w=22.f;
        light->direction_power.w*=2.f;
        light->color_outer.w=.65f;
    }
    return light;
}
} // namespace apricot
