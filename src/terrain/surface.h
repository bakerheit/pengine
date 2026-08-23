#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace apricot {

// What the ground is made of at a point, derived from the height field.
//
// PURE, like everything else in this module: surface type is a function of
// (seed, x, z) and nothing else. It is not painted, not stored and not
// authored — there is no material grid to keep in sync with the terrain,
// because the terrain IS the input. Change the height field and the beaches
// move with it, for free and without a re-bake.
//
// Two consumers with different needs, so there are two entry points:
//   * physics wants ONE query and a grip number   -> surface_kind_at()
//   * the mesher wants blend weights per vertex   -> classify_surface()
// Both go through the same classifier, so the surface the tyres grip is by
// construction the surface the shader draws.
//
// That second sentence was aspirational until PENG-40. physics had its own
// classifier and called it instead, and the two named a different material
// 40.85% of the time across the home basin. TerrainCollider::material() now
// calls surface_kind_at() and nothing else does the job twice.

// ORDER IS PART OF THE CONTRACT. It is the component order of
// SurfaceSample::weights and of the per-vertex material weights the renderer
// splats with, so inserting a value in the middle silently re-textures the
// world. Append only.
enum class Surface : uint8_t {
    Rock = 0,    // steep faces and exposed crags
    Gravel = 1,  // scree, alpine ground and moderate slopes
    Grass = 2,   // the default drivable ground
    Sand = 3,    // beaches at the water line
};

inline constexpr std::size_t kSurfaceCount = 4;

inline constexpr std::size_t surface_index(Surface s) {
    return static_cast<std::size_t>(s);
}

// Per-material properties this module owns.
//
// It used to carry `grip` and `rolling_resistance` as well, and physics
// carried its own copy of both in a table that DISAGREED — rock was 0.95 here
// and 1.15 there. Nothing outside a test ever read the pair here, so they were
// two sources of truth for the car's handling of which only one was ever
// consulted. They are gone; physics/surface.h has the only grip table, keyed
// by this enum. See PENG-40.
//
// What is left is what terrain genuinely owns: how the ground *looks*, and
// what grows on it.
struct SurfaceProperties {
    // Relative prop density, consumed by scatter. 1.0 is "as dense as this
    // material ever gets"; 0 would be bare. Lives with the material because
    // "what grows here" is a property of the ground, not of the tree.
    float scatter_density;
};

const SurfaceProperties& surface_properties(Surface s);

// Short stable name, for logs and debug overlays. Never parse this.
const char* surface_name(Surface s);

struct SurfaceSample {
    float height = 0.0f;             // metres, sea level = 0
    glm::vec3 normal{0.0f, 1.0f, 0.0f};

    // Sine of the slope angle: 0 on the flat, 1 on a vertical face. Stored as
    // a sine rather than an angle on purpose — recovering the angle needs
    // acos(), and libm's inverse trig is not correctly rounded, so an angle
    // here would put a non-deterministic value in the middle of a pure
    // function. sqrt() is IEEE-exact, so this is not.
    float slope = 0.0f;

    Surface dominant = Surface::Grass;

    // Blend weights in Surface order (rock, gravel, grass, sand). Sums to 1 by
    // construction — they come out of a cascade, not four independent scores
    // that are then normalised and hope for the best.
    glm::vec4 weights{0.0f, 0.0f, 1.0f, 0.0f};
};

// Classify from an ALREADY KNOWN height and normal.
//
// This is the primary entry point and the mesher's one: it has both values per
// vertex already, and re-deriving them would cost five more height_at() calls
// per vertex for an answer that must agree anyway. Passing them in makes
// disagreement impossible rather than unlikely.
SurfaceSample classify_surface(uint64_t seed, float x, float z, float height,
                               const glm::vec3& normal);

// Full sample at a world XZ, evaluating the height field itself.
SurfaceSample surface_at(uint64_t seed, float x, float z);

// Just the dominant material. Physics' query: one call, no blend weights, no
// allocation. Still evaluates the field, so it is valid in chunks that have
// never been meshed — grip must not depend on streaming state.
Surface surface_kind_at(uint64_t seed, float x, float z);

}  // namespace apricot
