#pragma once

#include <glm/glm.hpp>
#include <cfloat>

namespace pengine {

struct AABB {
    glm::vec3 min = { FLT_MAX,  FLT_MAX,  FLT_MAX};
    glm::vec3 max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    glm::vec3 center()  const { return (min + max) * 0.5f; }
    glm::vec3 extents() const { return (max - min) * 0.5f; }
    bool valid() const { return min.x <= max.x; }

    void expand(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    // Returns a new AABB enclosing this one after being transformed by m.
    // Arvo's method: transform the center and project each axis of the extents.
    AABB transform(const glm::mat4& m) const {
        glm::vec3 c = center();
        glm::vec3 e = extents();

        glm::vec3 new_c{m[3][0] + m[0][0]*c.x + m[1][0]*c.y + m[2][0]*c.z,
                        m[3][1] + m[0][1]*c.x + m[1][1]*c.y + m[2][1]*c.z,
                        m[3][2] + m[0][2]*c.x + m[1][2]*c.y + m[2][2]*c.z};

        glm::vec3 new_e{std::abs(m[0][0])*e.x + std::abs(m[1][0])*e.y + std::abs(m[2][0])*e.z,
                        std::abs(m[0][1])*e.x + std::abs(m[1][1])*e.y + std::abs(m[2][1])*e.z,
                        std::abs(m[0][2])*e.x + std::abs(m[1][2])*e.y + std::abs(m[2][2])*e.z};

        return {new_c - new_e, new_c + new_e};
    }
};

} // namespace pengine
