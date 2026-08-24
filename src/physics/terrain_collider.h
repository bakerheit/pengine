#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"
#include "physics/surface.h"  // brings in terrain/surface.h: Surface, surface_grip
#include "terrain/chunk.h"

namespace apricot {

// Collision against the world the player can actually see.
//
// The ground here is the MESHED surface: the triangles terrain/chunk.cpp emits,
// evaluated analytically rather than stored. The height field height_at() is a
// smooth continuous function; the mesh is a lattice of samples of it joined by
// flat triangles, and between two lattice points those two things differ by up
// to a few centimetres on a steep face. Physics that trusts the smooth field
// gives you a car floating a hand's width over a visible ridge — invisible in
// a test, obvious the first time somebody drives there. So we evaluate the
// same lattice, at the same world coordinates, with the same triangulation,
// and the ground the car touches IS the ground that is drawn.
//
// This is deliberately not a COPY of the mesh. Nothing is cached and nothing is
// stored per chunk, so:
//   * Queries are valid ANYWHERE, including in chunks that have never been
//     meshed. Physics must not depend on streaming state.
//   * There is no stale-tile failure mode, because there are no tiles.
//
// terrain_collision_tests.cpp pins the agreement by building a REAL chunk with
// build_chunk() and intersecting its actual triangle soup. If the mesher's
// triangulation ever changes, that test fails loudly instead of this drifting
// quietly.

// The mesh lattice, derived from the mesher's own constants so a change to
// either is picked up here for free.
inline constexpr int kTerrainSpan = kChunkVerts - 1;
inline constexpr float kTerrainVertexMetres =
    kChunkMetres / static_cast<float>(kTerrainSpan);

// A solid prop: a static world-space box the suspension can land on.
struct StaticBox {
    AABB bounds;
    Surface material = Surface::Rock;
};

// A stretch of stage painted with a material, overriding whatever the terrain
// classifier would have said. Matched on XZ ONLY — a stage designer laying a
// gravel section should not have to know the terrain height along it, and a
// region that has to track the ground vertically would drift the moment the
// height field is retuned.
struct SurfacePaint {
    AABB region;
    Surface material = Surface::Gravel;
};

class TerrainCollider {
public:
    explicit TerrainCollider(uint64_t seed) : seed_(seed) {}

    uint64_t seed() const { return seed_; }

    // --- the drivable surface -----------------------------------------------

    // Ground height / surface normal in metres at a world XZ, ON THE MESHED
    // SURFACE. Pure. Both delegate to terrain's own reconstruction
    // (mesh_height_at / mesh_normal_at) rather than repeating it here.
    //
    // The normal is the FACE normal of the drawn triangle, not a blend of its
    // three vertex normals. This header used to claim the opposite, and argued
    // for it: a blended normal is continuous, so the tyre axes do not snap as a
    // wheel crosses a triangle edge. The argument is real and the code was
    // still wrong -- a blend is the SHADING normal, and measured against the
    // face normal it was out by up to 1.02, which for unit vectors is most of a
    // right angle. Contact was being resolved against a plane the geometry does
    // not have and the car never fully settled. They now agree to 0.000000.
    float height(float x, float z) const;
    glm::vec3 normal(float x, float z) const;

    // The underlying SMOOTH height field, un-triangulated. This is what the
    // mesher samples at its lattice points, so it is the right thing for
    // anything reasoning about terrain shape (route layout, spawn scatter) and
    // the WRONG thing for contact. Contact goes through height()/probe_down().
    float field_height(float x, float z) const;
    glm::vec3 field_normal(float x, float z) const;

    // --- props ---------------------------------------------------------------
    // Static geometry is registered ONCE at world setup and never touched
    // during a step. step_vehicle() takes this object by const reference and
    // is pure in it; anything that mutates the collider mid-run breaks replay.
    void add_static_box(const AABB& bounds, Surface material = Surface::Rock);
    void clear_static_boxes();
    const std::vector<StaticBox>& static_boxes() const { return boxes_; }

    // --- surface materials ---------------------------------------------------
    void paint_surface(const AABB& region, Surface material);
    void clear_surface_paint();

    // Material at a world XZ. Painted regions win, last paint first; otherwise
    // this is terrain's surface_kind_at() and nothing else -- the same
    // classifier the mesher splats with, so the ground the car grips is by
    // construction the ground the player sees. physics does not classify.
    Surface material(float x, float z) const;

    // Peak friction coefficient at a world XZ, wetness already applied.
    float grip(float x, float z) const;

    // --- weather -------------------------------------------------------------
    // 0 = dry, 1 = soaked. World state, not per-car state, so it lives with the
    // world. Set it between steps, never during one: a replay reproduces a run
    // only if the weather it was driven in is restored alongside the seed.
    // DO NOT WIRE THIS UP FROM `Conditions`. Weather already reaches the tyres
    // through `VehicleTuning::grip_scale`, set by `conditioned_tuning()`. Doing
    // both applies the rain twice, and the symptom — a car that is mysteriously
    // twice as slippery as the numbers say — points at neither call site.
    //
    // It also cannot be driven from `Conditions` correctly even alone: the
    // collider is passed const to the sim step and shared with any replay, so
    // per-collider wetness would let two runs of one tape experience different
    // weather. That is a desync that reads as a physics bug.
    //
    // It stays for a *static* wetness a map author sets on a region — a tunnel
    // that is always damp — which is a different quantity from today's weather.
    // Zero callers today, and that is the correct number.
    void set_wetness(float w) { wetness_ = glm::clamp(w, 0.0f, 1.0f); }
    float wetness() const { return wetness_; }

    struct GroundHit {
        bool hit = false;
        // Distance from the probe origin down to the surface. NEGATIVE when
        // the origin is already below ground — callers use the sign to tell
        // "hovering" from "penetrating", so do not clamp it to zero.
        float distance = 0.0f;
        glm::vec3 point{0.0f};
        glm::vec3 normal{0.0f, 1.0f, 0.0f};

        Surface material = Surface::Rock;
        // Peak friction coefficient here, wetness already applied. Carried on
        // the hit so a wheel does not have to re-query the surface it just
        // touched — and so the two can never disagree.
        float grip = 1.0f;

        // True when the surface found was a prop box rather than terrain.
        bool prop = false;
    };

    // Straight-down probe from `origin`. Analytic rather than marched: the
    // terrain is a height field, so the answer is one triangle evaluation and
    // a subtraction, and prop boxes reduce to a slab test. `max_distance`
    // bounds how far below the origin counts as a hit; it exists so a
    // suspension ray does not grab terrain 400 m down a cliff face.
    //
    // Only the TOP face of a prop box can be found: a downward probe that
    // starts underneath a box (origin.y < box.min.y) ignores it entirely, so
    // driving under an overhang does not snap the car onto its roof. Bridge
    // undersides are not a thing this collider models.
    GroundHit probe_down(glm::vec3 origin, float max_distance) const;

    // General ray. `dir` need not be normalised. Terrain is marched and then
    // bisected — a height field has no closed-form intersection for an
    // arbitrary direction — while prop boxes get an exact slab test. Used for
    // anything that is not a suspension ray; the suspension itself uses
    // probe_down(), which is exact.
    GroundHit raycast(glm::vec3 origin, glm::vec3 dir, float max_distance) const;

private:
    uint64_t seed_;
    std::vector<StaticBox> boxes_;
    std::vector<SurfacePaint> paint_;
    float wetness_ = 0.0f;
};

}  // namespace apricot
