#pragma once

#include <glm/glm.hpp>

#include <cfloat>
#include <cmath>

namespace apricot {

// Axis-aligned bounding box. Default-constructs INVERTED (min = +inf,
// max = -inf) so a fresh box is empty and the first expand() sets both bounds
// without a "have I seen a point yet" flag.
struct AABB {
    glm::vec3 min{ FLT_MAX,  FLT_MAX,  FLT_MAX};
    glm::vec3 max{-FLT_MAX, -FLT_MAX, -FLT_MAX};

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extents() const { return (max - min) * 0.5f; }

    // False for a box that has never been expanded. Cull code must check this:
    // an inverted box passes every plane test and would draw forever.
    bool valid() const { return min.x <= max.x; }

    void expand(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    void expand(const AABB& b) {
        if (!b.valid()) return;
        min = glm::min(min, b.min);
        max = glm::max(max, b.max);
    }

    bool contains(const glm::vec3& p) const {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }

    bool intersects(const AABB& b) const {
        return min.x <= b.max.x && max.x >= b.min.x &&
               min.y <= b.max.y && max.y >= b.min.y &&
               min.z <= b.max.z && max.z >= b.min.z;
    }

    // Smallest AABB enclosing this one after transformation by m. Arvo's
    // method: transform the centre, then project each extent axis onto the
    // absolute value of the matrix. Cheaper and tighter than transforming all
    // eight corners.
    AABB transformed(const glm::mat4& m) const {
        const glm::vec3 c = center();
        const glm::vec3 e = extents();

        const glm::vec3 nc{
            m[3][0] + m[0][0] * c.x + m[1][0] * c.y + m[2][0] * c.z,
            m[3][1] + m[0][1] * c.x + m[1][1] * c.y + m[2][1] * c.z,
            m[3][2] + m[0][2] * c.x + m[1][2] * c.y + m[2][2] * c.z};

        const glm::vec3 ne{
            std::fabs(m[0][0]) * e.x + std::fabs(m[1][0]) * e.y + std::fabs(m[2][0]) * e.z,
            std::fabs(m[0][1]) * e.x + std::fabs(m[1][1]) * e.y + std::fabs(m[2][1]) * e.z,
            std::fabs(m[0][2]) * e.x + std::fabs(m[1][2]) * e.y + std::fabs(m[2][2]) * e.z};

        return AABB{nc - ne, nc + ne};
    }
};

}  // namespace apricot
