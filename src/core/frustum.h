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
    // Index names for planes[]. The ORDER is the contract — a test that
    // rejects a box behind the near plane and a renderer that reads
    // planes[Near] for its fog range have to agree about which one that is.
    enum PlaneIndex {
        kLeft = 0,
        kRight = 1,
        kBottom = 2,
        kTop = 3,
        kNear = 4,
        kFar = 5,
        kPlaneCount = 6
    };

    // (nx, ny, nz, d). A point is inside when dot(n, p) + d >= 0.
    glm::vec4 planes[kPlaneCount];

    // `m` is a column-major view-projection matrix (the glm/GL convention).
    static Frustum from_view_proj(const glm::mat4& m) {
        // Row i of a column-major matrix is {m[0][i], m[1][i], m[2][i], m[3][i]}.
        auto row = [&](int i) {
            return glm::vec4{m[0][i], m[1][i], m[2][i], m[3][i]};
        };
        const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

        Frustum f;
        f.planes[kLeft]   = r3 + r0;
        f.planes[kRight]  = r3 - r0;
        f.planes[kBottom] = r3 + r1;
        f.planes[kTop]    = r3 - r1;
        f.planes[kNear]   = r3 + r2;
        f.planes[kFar]    = r3 - r2;

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
        // An inverted (never-expanded) box is the empty set. The positive-
        // vertex test would hand it min = +inf / max = -inf, which passes every
        // plane, and the box would be "visible" forever — the exact failure
        // aabb.h's valid() comment warns about. Answering "cull it" is both
        // correct and the only answer that cannot draw a node whose bounds
        // were never filled in.
        if (!box.valid()) return true;

        for (const glm::vec4& p : planes) {
            const glm::vec3 n{p};
            const glm::vec3 pv{n.x >= 0.0f ? box.max.x : box.min.x,
                               n.y >= 0.0f ? box.max.y : box.min.y,
                               n.z >= 0.0f ? box.max.z : box.min.z};
            if (glm::dot(n, pv) + p.w < 0.0f) return true;
        }
        return false;
    }

    // The complement of cull(), for call sites that read better in the
    // positive. Same conservative behaviour.
    bool intersects(const AABB& box) const { return !cull(box); }

    // Signed distance from a plane to a point: positive inside, negative
    // outside, in metres because from_view_proj() normalises. Distance-based
    // LOD and fog fades read this directly.
    float distance_to(PlaneIndex plane, const glm::vec3& p) const {
        const glm::vec4& q = planes[plane];
        return glm::dot(glm::vec3{q}, p) + q.w;
    }

    bool contains(const glm::vec3& p) const {
        for (const glm::vec4& q : planes) {
            if (glm::dot(glm::vec3{q}, p) + q.w < 0.0f) return false;
        }
        return true;
    }

    // True when the sphere is entirely outside. Exact for spheres, unlike the
    // box test — there is no positive-vertex approximation to make here.
    bool cull_sphere(const glm::vec3& center, float radius) const {
        for (const glm::vec4& q : planes) {
            if (glm::dot(glm::vec3{q}, center) + q.w < -radius) return true;
        }
        return false;
    }
};

}  // namespace apricot
