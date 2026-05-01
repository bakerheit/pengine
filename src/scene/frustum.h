#pragma once

#include <glm/glm.hpp>

#include "scene/aabb.h"

namespace pengine {

// Six frustum planes in world space, normals pointing inward.
// Extracted via Gribb-Hartmann from the view-projection matrix.
struct Frustum {
    glm::vec4 planes[6]; // (nx, ny, nz, d): dot(n, p) + d >= 0 means inside

    // Extract planes from a column-major VP matrix (OpenGL convention).
    static Frustum from_view_proj(const glm::mat4& m) {
        // Row i of m in column-major storage = { m[0][i], m[1][i], m[2][i], m[3][i] }
        auto row = [&](int i) {
            return glm::vec4{m[0][i], m[1][i], m[2][i], m[3][i]};
        };
        glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

        Frustum f;
        f.planes[0] = r3 + r0; // left
        f.planes[1] = r3 - r0; // right
        f.planes[2] = r3 + r1; // bottom
        f.planes[3] = r3 - r1; // top
        f.planes[4] = r3 + r2; // near
        f.planes[5] = r3 - r2; // far

        for (auto& p : f.planes) {
            float len = glm::length(glm::vec3{p});
            if (len > 1e-6f) p /= len;
        }
        return f;
    }

    // Returns true if the AABB is fully OUTSIDE the frustum (should be culled).
    // Uses the positive-vertex (p-vertex) test — one GL call per plane.
    bool cull(const AABB& box) const {
        for (const auto& p : planes) {
            glm::vec3 n{p};
            // p-vertex: corner of box most aligned with the plane normal
            glm::vec3 pv{n.x >= 0.f ? box.max.x : box.min.x,
                         n.y >= 0.f ? box.max.y : box.min.y,
                         n.z >= 0.f ? box.max.z : box.min.z};
            if (glm::dot(n, pv) + p.w < 0.f) return true;
        }
        return false;
    }
};

} // namespace pengine
