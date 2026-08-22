#pragma once

#include <glm/glm.hpp>

#include "core/aabb.h"

namespace apricot {

// Six world-space frustum planes with INWARD-facing normals, extracted from a
// view-projection matrix by the Gribb-Hartmann method.
//
// Pure maths, no GL: culling is a sim-side decision (scene/scene.h) and the
// renderer only consumes the result.
struct Frustum {
    // (nx, ny, nz, d). A point is inside when dot(n, p) + d >= 0.
    glm::vec4 planes[6];

    // `m` is a column-major view-projection matrix (the glm/GL convention).
    static Frustum from_view_proj(const glm::mat4& m) {
        // Row i of a column-major matrix is {m[0][i], m[1][i], m[2][i], m[3][i]}.
        auto row = [&](int i) {
            return glm::vec4{m[0][i], m[1][i], m[2][i], m[3][i]};
        };
        const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

        Frustum f;
        f.planes[0] = r3 + r0;  // left
        f.planes[1] = r3 - r0;  // right
        f.planes[2] = r3 + r1;  // bottom
        f.planes[3] = r3 - r1;  // top
        f.planes[4] = r3 + r2;  // near
        f.planes[5] = r3 - r2;  // far

        // Normalise so plane distances are real metres — distance-based LOD
        // and fog fades read them directly.
        for (glm::vec4& p : f.planes) {
            const float len = glm::length(glm::vec3{p});
            if (len > 1e-6f) p /= len;
        }
        return f;
    }

    // True when the box is entirely OUTSIDE the frustum, i.e. should be culled.
    // Positive-vertex test: only the box corner most aligned with each plane
    // normal needs checking, so this is one dot product per plane, not eight.
    //
    // Conservative on purpose — a box straddling two planes' outside half-
    // spaces without being outside either alone reports "visible". Drawing one
    // extra box is free; dropping a visible one is a hole in the world.
    bool cull(const AABB& box) const {
        for (const glm::vec4& p : planes) {
            const glm::vec3 n{p};
            const glm::vec3 pv{n.x >= 0.0f ? box.max.x : box.min.x,
                               n.y >= 0.0f ? box.max.y : box.min.y,
                               n.z >= 0.0f ? box.max.z : box.min.z};
            if (glm::dot(n, pv) + p.w < 0.0f) return true;
        }
        return false;
    }
};

}  // namespace apricot
