#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "physics/terrain_collider.h"

namespace apricot {

inline constexpr float kVehicleFluidMarkLiftM = 0.015f;

struct VehicleFluidMarkPlacement {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool road = false;
    bool prop = false;
};

// Place a spill on the highest drawn surface below its vehicle. Raw
// TerrainCollider::height() deliberately means terrain only; roads and raised
// sidewalks are baked collision triangles above it and must be found through
// probe_down() or they will draw over the mark.
inline VehicleFluidMarkPlacement vehicle_fluid_mark_placement(
    const TerrainCollider& collider, glm::vec2 position_xz, float source_y) {
    const float terrain_y = collider.height(position_xz.x, position_xz.y);
    if (!std::isfinite(source_y)) source_y = terrain_y + 1.0f;
    const float origin_y = std::max(source_y + 0.75f, terrain_y + 1.0f);
    const float reach = std::max(origin_y - terrain_y + 0.5f, 1.5f);
    const TerrainCollider::GroundHit hit = collider.probe_down(
        {position_xz.x, origin_y, position_xz.y}, reach);

    VehicleFluidMarkPlacement out;
    glm::vec3 surface_point{position_xz.x, terrain_y, position_xz.y};
    if (hit.hit) {
        surface_point = hit.point;
        out.normal = hit.normal;
        out.road = hit.road;
        out.prop = hit.prop;
    } else {
        out.normal = collider.normal(position_xz.x, position_xz.y);
    }
    const float normal_length = glm::length(out.normal);
    out.normal = normal_length > 1e-5f
        ? out.normal / normal_length
        : glm::vec3{0.0f, 1.0f, 0.0f};
    out.position = surface_point + out.normal * kVehicleFluidMarkLiftM;
    return out;
}

inline glm::quat vehicle_fluid_mark_rotation(glm::vec3 normal,
                                              float spin_radians) {
    constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};
    const float normal_length = glm::length(normal);
    normal = normal_length > 1e-5f ? normal / normal_length : kUp;
    const float dot = glm::clamp(glm::dot(kUp, normal), -1.0f, 1.0f);

    glm::quat align{1.0f, 0.0f, 0.0f, 0.0f};
    if (dot < 0.99999f) {
        if (dot <= -0.99999f) {
            align = glm::angleAxis(3.14159265f,
                                   glm::vec3{1.0f, 0.0f, 0.0f});
        } else {
            align = glm::angleAxis(std::acos(dot),
                                   glm::normalize(glm::cross(kUp, normal)));
        }
    }
    return align * glm::angleAxis(spin_radians, kUp);
}

}  // namespace apricot
