#pragma once

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

#include <glm/glm.hpp>

#include "city/start_area.h"

namespace apricot::city {

inline constexpr float kInteriorTrafficPresentationRadiusM = 90.0f;
inline constexpr float kInteriorNpcPresentationRadiusM = 55.0f;
inline constexpr float kInteriorExitMarginM = 1.5f;

// A render-residency volume derived from the same authored floor slab the
// character walks on. It deliberately does not change Crowd membership: an
// indoor presentation LOD must not permanently retire traffic at the door.
struct InteriorStreamingVolume {
    const char* name = nullptr;
    glm::vec2 centre{0.0f};
    float floor_m = 0.0f;
    float width_m = 0.0f;
    float depth_m = 0.0f;
    float cos_yaw = 1.0f;
    float sin_yaw = 0.0f;
};

inline bool is_interior_floor(const StartPart& part) {
    return part.name && std::strstr(part.name, "interior floor");
}

inline InteriorStreamingVolume interior_streaming_volume(
        const StartSite& site, const StartPart& floor) {
    const float local_yaw = glm::radians(floor.yaw_deg);
    const float local_cos = std::cos(local_yaw);
    const float local_sin = std::sin(local_yaw);
    return {
        floor.name,
        {site.origin.x + site.cos_yaw * floor.centre.x +
             site.sin_yaw * floor.centre.z,
         site.origin.z - site.sin_yaw * floor.centre.x +
             site.cos_yaw * floor.centre.z},
        site.ground_m + floor.bottom_m + floor.height_m,
        floor.width_m,
        floor.depth_m,
        site.cos_yaw * local_cos - site.sin_yaw * local_sin,
        site.sin_yaw * local_cos + site.cos_yaw * local_sin,
    };
}

inline void append_interior_streaming_volumes(
        const StartSite& site, const StartPart* parts, std::size_t count,
        std::vector<InteriorStreamingVolume>& out) {
    for (std::size_t i = 0; i < count; ++i) {
        if (is_interior_floor(parts[i]))
            out.push_back(interior_streaming_volume(site, parts[i]));
    }
}

inline bool contains(const InteriorStreamingVolume& volume,
                     glm::vec3 position, float margin_m = 0.0f) {
    // PlayerCharacterState.position is the feet point. The upper bound keeps
    // an aircraft over a shop from switching the city to indoor residency.
    if (position.y < volume.floor_m - 0.75f ||
        position.y > volume.floor_m + 8.0f)
        return false;
    const glm::vec2 delta{position.x - volume.centre.x,
                          position.z - volume.centre.y};
    const float local_x = volume.cos_yaw * delta.x -
                          volume.sin_yaw * delta.y;
    const float local_z = volume.sin_yaw * delta.x +
                          volume.cos_yaw * delta.y;
    return std::fabs(local_x) <= volume.width_m * 0.5f + margin_m &&
           std::fabs(local_z) <= volume.depth_m * 0.5f + margin_m;
}

// A non-positive radius means the existing outdoor behavior: present every
// active agent. Positive radii are horizontal because terrain height should
// not change whether a street outside a building is loaded.
inline bool within_presentation_radius(glm::vec3 position, glm::vec3 focus,
                                       float radius_m) {
    if (radius_m <= 0.0f) return true;
    const glm::vec2 delta{position.x - focus.x, position.z - focus.z};
    return glm::dot(delta, delta) <= radius_m * radius_m;
}

}  // namespace apricot::city
