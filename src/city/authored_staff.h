#pragma once

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include "city/marina.h"
#include "city/start_area.h"
#include "city/gun_store.h"

namespace apricot::city {

// Persistent authored staff have their own identities; they do not consume a
// streamed pedestrian lane or slot. Positions and facing are site-local.
struct AuthoredStaff {
    uint64_t identity;
    const char* name;
    const StartSite* site;
    Vec2 position;
    float floor_m;
    Vec2 facing;
    std::size_t civilian_model;
};

inline constexpr AuthoredStaff kAuthoredStaff[]{
    {0x48414c4c4f574159ULL, "Halloway store clerk", &kGasStationSite,
     {-1.90f, -8.65f}, .20f, {0.0f, -1.0f}, 1u},
    // Devon stays at the inner end of the shop counter for the opening
    // delivery and faces the door Johnny walks through.
    {0x4445564f4e000001ULL, "Devon", &kMarlinDockSite,
     {1.50f, 8.00f}, kMarinaDeckTop, {0.0f, -1.0f}, 2u},
    {0x42524153534c494eULL, "Brassline Arms clerk", &kGunStoreSite,
     {0,-8}, .20f, {0,1}, 3u},
};

inline constexpr std::size_t kDevonStaffIndex = 1u;

inline glm::vec3 authored_staff_position(const AuthoredStaff& staff) {
    const auto& site = *staff.site;
    return {site.origin.x + site.cos_yaw * staff.position.x + site.sin_yaw * staff.position.z,
            site.ground_m + staff.floor_m,
            site.origin.z - site.sin_yaw * staff.position.x + site.cos_yaw * staff.position.z};
}

inline glm::vec3 authored_staff_forward(const AuthoredStaff& staff) {
    const auto& site = *staff.site;
    return {site.cos_yaw * staff.facing.x + site.sin_yaw * staff.facing.z, 0.0f,
            -site.sin_yaw * staff.facing.x + site.cos_yaw * staff.facing.z};
}

inline glm::vec3 devon_position() {
    return authored_staff_position(kAuthoredStaff[kDevonStaffIndex]);
}

}  // namespace apricot::city
