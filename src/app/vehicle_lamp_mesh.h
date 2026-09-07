#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

#include "core/emesh_reader.h"
#include "gfx/primitives.h"

namespace apricot {

// Reserved negative UV scale selects a body-surface glow mask (lit shaders).
// This reuses the body mesh and its exact deformation, not a separate lens.
// The renderer partitions these selectors into a depth-equal surface pass;
// do not offset their geometry or clip depth from the body.
inline glm::vec2 vehicle_lamp_surface_uv(std::size_t lamp_index, int profile = 0) {
    return {-2.0f - static_cast<float>(lamp_index), static_cast<float>(profile + 1)};
}

inline constexpr float kVehicleLampSurfaceOffset = 0.0015f;

// Cast through the body in source-space Z and return the actual outer skin at
// one XY point. Using the body triangles matters on old low-poly vehicles: the
// bumper tip in the AABB can sit a long way in front of the sloped grille.
inline bool vehicle_body_surface_z(const StaticEmesh& body, float x, float y,
                                   bool front, float& out_z,
                                   glm::vec3* out_normal = nullptr) {
    bool hit = false;
    float best = front ? -std::numeric_limits<float>::max()
                       : std::numeric_limits<float>::max();
    glm::vec3 best_normal{0.0f, 0.0f, front ? 1.0f : -1.0f};

    for (std::size_t i = 0; i + 2u < body.indices.size(); i += 3u) {
        const EmeshVertex& av = body.vertices[body.indices[i]];
        const EmeshVertex& bv = body.vertices[body.indices[i + 1u]];
        const EmeshVertex& cv = body.vertices[body.indices[i + 2u]];
        const glm::vec2 a{av.px, av.py};
        const glm::vec2 b{bv.px, bv.py};
        const glm::vec2 c{cv.px, cv.py};
        const float denominator =
            (b.y - c.y) * (a.x - c.x) +
            (c.x - b.x) * (a.y - c.y);
        if (std::fabs(denominator) < 1e-7f) continue;

        const float u = ((b.y - c.y) * (x - c.x) +
                         (c.x - b.x) * (y - c.y)) / denominator;
        const float v = ((c.y - a.y) * (x - c.x) +
                         (a.x - c.x) * (y - c.y)) / denominator;
        const float w = 1.0f - u - v;
        if (u < -1e-5f || v < -1e-5f || w < -1e-5f) continue;

        glm::vec3 normal = u * glm::vec3{av.nx, av.ny, av.nz} +
                           v * glm::vec3{bv.nx, bv.ny, bv.nz} +
                           w * glm::vec3{cv.nx, cv.ny, cv.nz};
        if ((front && normal.z < 0.15f) ||
            (!front && normal.z > -0.15f)) {
            continue;
        }
        const float z = u * av.pz + v * bv.pz + w * cv.pz;
        if (!hit || (front ? z > best : z < best)) {
            hit = true;
            best = z;
            const float length = glm::length(normal);
            best_normal = length > 1e-6f
                ? normal / length
                : glm::vec3{0.0f, 0.0f, front ? 1.0f : -1.0f};
        }
    }

    if (!hit) return false;
    out_z = best;
    if (out_normal) *out_normal = best_normal;
    return true;
}

// Legacy source-fit probe for geometry diagnostics. Runtime glow MUST reuse
// the body's exact triangles with vehicle_lamp_surface_uv(), since four
// independently deformed corner samples cannot follow a nonlinear dent.
inline MeshData make_vehicle_lamp_mesh(const StaticEmesh& body,
                                       std::size_t lamp_index) {
    MeshData mesh;
    if (!body.bounds.valid() || lamp_index >= 4u) return mesh;

    const bool front = lamp_index < 2u;
    const bool left = lamp_index == 0u || lamp_index == 2u;
    const glm::vec3 centre = body.bounds.center();
    const glm::vec3 half = body.bounds.extents();
    const glm::vec3 size = body.bounds.size();
    const float width = size.x * (front ? 0.17f : 0.15f);
    const float height =
        std::max(size.y * (front ? 0.085f : 0.072f), 0.055f);
    const float centre_x =
        centre.x + (left ? 1.0f : -1.0f) * half.x * 0.60f;
    // Front lamps belong on the low fascia, not halfway up the full body AABB.
    // The rear lens band on these legacy meshes sits a little higher.
    const float centre_y =
        centre.y - size.y * (front ? 0.28f : 0.15f);
    const float x0 = centre_x - width * 0.5f;
    const float x1 = centre_x + width * 0.5f;
    const float y0 = centre_y - height * 0.5f;
    const float y1 = centre_y + height * 0.5f;
    const glm::vec2 front_corners[4] = {
        {x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const glm::vec2 rear_corners[4] = {
        {x1, y0}, {x0, y0}, {x0, y1}, {x1, y1}};
    const glm::vec2* corners = front ? front_corners : rear_corners;

    constexpr glm::vec4 kSolidRock{1.0f, 0.0f, 0.0f, 0.0f};
    mesh.vertices.reserve(4u);
    for (std::size_t i = 0; i < 4u; ++i) {
        float surface_z = 0.0f;
        glm::vec3 normal;
        if (!vehicle_body_surface_z(body, corners[i].x, corners[i].y,
                                    front, surface_z, &normal)) {
            return {};
        }
        const glm::vec3 position{
            corners[i].x, corners[i].y,
            surface_z + (front ? kVehicleLampSurfaceOffset
                               : -kVehicleLampSurfaceOffset)};
        const glm::vec2 uv{
            (i == 1u || i == 2u) ? 1.0f : 0.0f,
            i >= 2u ? 1.0f : 0.0f};
        mesh.vertices.push_back({position, normal, uv, kSolidRock});
        mesh.bounds.expand(position);
    }
    mesh.indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return mesh;
}

}  // namespace apricot
