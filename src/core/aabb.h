#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace apricot {

// Direction components smaller than this are treated as exactly parallel by
// the ray test, so the parallel case never goes near a reciprocal.
//
// MEASURED, not assumed. A ray running exactly along a face has dir[a] == 0
// and origin[a] exactly on a slab bound, and the textbook `1/dir` form then
// computes (bound - origin) * inf as 0 * inf, which is NaN. What happens next
// depends entirely on the ARGUMENT ORDER of std::min/std::max, because both
// are specified as a single `<` comparison and every comparison against NaN is
// false:
//
//     std::max(accumulator, nan) -> accumulator   NaN silently swallowed
//     std::max(nan, accumulator) -> nan           NaN propagates
//
// So the naive version is correct only while the accumulator happens to be
// written first, and it is a hit-with-a-NaN-distance the moment somebody
// reorders two arguments that look interchangeable. Not a miss — a HIT whose
// t is NaN, which the caller then multiplies into a position. Depending on an
// undocumented argument order to stay correct is not a design, it is a fuse.
// The explicit branch below makes the parallel case exact by construction.
inline constexpr float kRayParallelEpsilon = 1e-8f;

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

    // True when b is entirely inside this box. An invalid (never-expanded) b is
    // the empty set, and the empty set is contained in everything — saying
    // otherwise makes "contains" disagree with "every point of b is inside".
    bool contains(const AABB& b) const {
        if (!b.valid()) return true;
        return b.min.x >= min.x && b.max.x <= max.x &&
               b.min.y >= min.y && b.max.y <= max.y &&
               b.min.z >= min.z && b.max.z <= max.z;
    }

    bool intersects(const AABB& b) const {
        return min.x <= b.max.x && max.x >= b.min.x &&
               min.y <= b.max.y && max.y >= b.min.y &&
               min.z <= b.max.z && max.z >= b.min.z;
    }

    // Non-mutating merge. expand() is the in-place form; this one exists so a
    // fold over child bounds reads as an expression rather than a loop with a
    // scratch variable.
    AABB merged(const AABB& b) const {
        AABB out = *this;
        out.expand(b);
        return out;
    }

    glm::vec3 size() const { return valid() ? (max - min) : glm::vec3{0.0f}; }

    // Grown equally on all six faces. Negative margins shrink, and are allowed
    // to invert the box — an over-shrunk box reports !valid() rather than
    // silently clamping to a degenerate slab that still passes cull tests.
    AABB expanded(float margin) const {
        if (!valid()) return *this;
        return AABB{min - glm::vec3{margin}, max + glm::vec3{margin}};
    }

    // Nearest point of the box to p; p itself when p is inside.
    glm::vec3 closest_point(const glm::vec3& p) const {
        return glm::clamp(p, min, max);
    }

    // Result of a ray/box query. `t_near` is the entry parameter along the ray
    // and `t_far` the exit, both in units of `dir` — so a NORMALISED dir gives
    // distances in metres and an unnormalised one does not. For a ray starting
    // inside the box, t_near is clamped to the search range and t_far is the
    // exit; that is the pair a hit-scan wants, and callers relying on "t_near
    // is where the surface is" must check inside().
    struct RayHit {
        bool hit = false;
        float t_near = 0.0f;
        float t_far = 0.0f;

        // The ray began inside the box, so t_near is the range start rather
        // than a surface crossing.
        bool inside = false;

        explicit operator bool() const { return hit; }
    };

    // Slab method, over the parameter range [t_min, t_max].
    //
    // The three-axis loop takes an explicit parallel branch rather than leaning
    // on IEEE infinities — see kRayParallelEpsilon above for the measurement
    // behind that choice. t_near and t_far are guaranteed finite on a hit,
    // which tests/math_tests.cpp asserts directly.
    RayHit intersect_ray(const glm::vec3& origin, const glm::vec3& dir,
                         float t_min = 0.0f, float t_max = FLT_MAX) const {
        RayHit out;
        if (!valid()) return out;

        float near_t = t_min;
        float far_t = t_max;

        for (int a = 0; a < 3; ++a) {
            if (std::fabs(dir[a]) < kRayParallelEpsilon) {
                // Parallel to this slab: the ray never crosses either face, so
                // it can only hit at all if it already lies between them. The
                // bounds are INCLUSIVE, which is what makes a ray grazing
                // exactly along a face count as a hit — the face is part of
                // the box.
                if (origin[a] < min[a] || origin[a] > max[a]) return out;
                continue;
            }

            const float inv = 1.0f / dir[a];
            float t1 = (min[a] - origin[a]) * inv;
            float t2 = (max[a] - origin[a]) * inv;
            if (t1 > t2) std::swap(t1, t2);

            near_t = std::max(near_t, t1);
            far_t = std::min(far_t, t2);
            if (near_t > far_t) return out;
        }

        out.hit = true;
        out.t_near = near_t;
        out.t_far = far_t;
        out.inside = contains(origin);
        return out;
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
