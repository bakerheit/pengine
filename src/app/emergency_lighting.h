#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
#include "app/player_car_catalog.h"
#include "core/transform.h"
#include "gfx/lighting.h"

namespace apricot {
inline bool has_police_lightbar(PlayerCarId model) {
    return is_municipal_cruiser_91(model);
}
inline std::array<float,2> police_flash_power(uint64_t step, bool enabled) {
    if (!enabled) return {0,0};
    const auto phase=step%120;
    const auto pulse=phase%60;
    const float power=(pulse<14 || (pulse>=22 && pulse<36)) ? 1.f : 0.f;
    return phase<60 ? std::array<float,2>{power,0} : std::array<float,2>{0,power};
}
inline std::array<glm::vec3,2> police_lightbar_centres(PlayerCarId model) {
    model=canonical_player_car_id(model);
    if (model==PlayerCarId::MunicipalCruiser91A)
        return {{{-.425f,1.715f,-.110f},{.425f,1.715f,-.110f}}};
    if (model==PlayerCarId::MunicipalCruiser91B)
        return {{{-.420f,1.695f,-.030f},{.420f,1.695f,-.030f}}};
    if (model==PlayerCarId::MunicipalCruiser91C)
        return {{{-.375f,1.690f,-.245f},{.375f,1.690f,-.245f}}};
    if (model==PlayerCarId::MunicipalCruiser91D)
        return {{{-.390f,1.810f,-.050f},{.390f,1.810f,-.050f}}};
    if (model==PlayerCarId::MunicipalCruiser91E)
        return {{{-.405f,1.630f,-.130f},{.405f,1.630f,-.130f}}};
    return {{{-.46f,1.7625f,-.13f},{.46f,1.7625f,-.13f}}};
}
inline void append_police_lights(std::vector<TrafficSpotLight>& lights,
                                 const Transform& body, uint64_t step, bool enabled,
                                 PlayerCarId model=PlayerCarId::MunicipalCruiser91C) {
    const auto powers=police_flash_power(step,enabled);
    const auto centres=police_lightbar_centres(model);
    const glm::vec3 colors[]={{1,.015f,.005f},{.015f,.08f,1}};
    for (std::size_t bank=0;bank<2;++bank) {
        if (powers[bank]<=0) continue;
        const auto origin=body.transform_point(centres[bank]);
        for (const glm::vec3 direction : {glm::vec3{1,-.65f,0},glm::vec3{-1,-.65f,0},
             glm::vec3{0,-.65f,1},glm::vec3{0,-.65f,-1}}) {
            lights.push_back({glm::vec4{origin,18},
                glm::vec4{glm::normalize(body.rotation*direction),5.0f*powers[bank]},
                glm::vec4{colors[bank],.45f}});
        }
    }
}
} // namespace apricot
