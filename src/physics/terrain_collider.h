#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace apricot {

// Collision against the height field.
//
// Deliberately NOT a copy of the terrain data. It evaluates the same pure
// height_at() the mesher does, so the ground the car touches is by
// construction the ground the player sees. A collider built from a parallel
// re-derivation — a downsampled grid, a cached tile — drifts from the visual
// mesh, and the resulting "the car floats on that hill" bug is invisible
// until someone drives there.
//
// Queries are valid ANYWHERE, including in chunks that have never been meshed.
// Physics must not depend on streaming state.
class TerrainCollider {
public:
    explicit TerrainCollider(uint64_t seed) : seed_(seed) {}

    uint64_t seed() const { return seed_; }

    // Ground height / surface normal in metres at a world XZ. Pure.
    float height(float x, float z) const;
    glm::vec3 normal(float x, float z) const;

    struct GroundHit {
        bool hit = false;
        // Distance from the probe origin down to the surface. NEGATIVE when
        // the origin is already below ground — callers use the sign to tell
        // "hovering" from "penetrating", so do not clamp it to zero.
        float distance = 0.0f;
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
    };

    // Straight-down probe from `origin`. Analytic rather than marched: the
    // surface is a height field, so the answer is one height() evaluation and
    // a subtraction. `max_distance` bounds how far below the origin counts as
    // a hit; it exists so a suspension ray does not grab terrain 400 m down a
    // cliff face.
    GroundHit probe_down(glm::vec3 origin, float max_distance) const;

private:
    uint64_t seed_;
};

}  // namespace apricot
