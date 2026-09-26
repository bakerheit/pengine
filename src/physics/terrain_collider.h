#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"
#include "physics/surface.h"  // brings in terrain/surface.h: Surface, surface_grip
#include "terrain/chunk.h"

namespace apricot {

struct RoadCollision;
class SnowClearanceField;
class SnowShelterField;

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
    // Keep broad bounds for culling, but retain the exact yaw for vehicle,
    // character, support and camera-ray narrow-phase tests.
    bool oriented = false;
    AABB local_bounds{};
    glm::vec3 centre{0.0f};
    glm::vec2 axis_x{1.0f, 0.0f};
    glm::vec2 axis_z{0.0f, 1.0f};
    // Stable kinematic slots may be disabled while their car is player-driven.
    bool enabled = true;
    bool is_vehicle = false;  // parked vehicle, not a wall or other world prop
    // Precipitation cover only (SnowShelterField): a roof with no walls under
    // it, such as a fuel canopy, so wind drifts snow in from its edges. Every
    // other cover is treated as enclosed and stays bare beneath.
    bool open_sided = false;
    // Breakaway props report a hit through VehicleState. The owner disables
    // this stable slot BETWEEN steps; step_vehicle remains const in the world.
    float breakaway_speed = 0.0f;
    uint32_t breakaway_id = UINT32_MAX;

    const AABB& collision_bounds() const { return oriented ? local_bounds : bounds; }
    glm::vec3 local_direction(glm::vec3 v) const {
        if (!oriented) return v;
        const glm::vec2 xz{v.x, v.z};
        return {glm::dot(xz, axis_x), v.y, glm::dot(xz, axis_z)};
    }
    glm::vec3 local_point(glm::vec3 p) const {
        return oriented ? local_direction(p - centre) : p;
    }
    glm::vec3 world_direction(glm::vec3 v) const {
        if (!oriented) return v;
        const glm::vec2 xz = axis_x * v.x + axis_z * v.z;
        return {xz.x, v.y, xz.y};
    }
};

// A thin authored ground plane, usually the visible top of a paved lot. Unlike
// StaticBox this keeps its rotation in XZ, so a six-degree city plot does not
// gain invisible axis-aligned corners that characters can stand on.
struct StaticGroundRect {
    glm::vec2 centre{0.0f};
    glm::vec2 axis_x{1.0f, 0.0f};
    glm::vec2 axis_z{0.0f, 1.0f};
    glm::vec2 half_extents{0.5f};
    float height = 0.0f;
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

    // Ground height / surface normal in metres at a world XZ. At zero snow
    // depth this is the meshed surface exactly; positive physical snow raises
    // the contact height without changing the terrain face normal. Pure.
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

    // Is the straight segment from `from` to `to` blocked by terrain or by a
    // static box? A VISIBILITY question, deliberately not answered by
    // raycast().
    //
    // raycast() exists for contact: it marches the height field in 25 cm steps
    // because a wheel resolving against the wrong quarter-metre is a car that
    // never settles. Line of sight has no such contract, and paying contact
    // precision for it was measured at 0.208 ms a call — 86 per cent of which
    // was 400 height() samples over a 100 m ray that, across flat city ground,
    // find nothing at all. Five calls a sim step, twelve steps a frame at the
    // clamp, and that is a 140 ms frame spent proving the ground is not in the
    // way.
    //
    // Two things make this cheap where raycast() is not. Static boxes are
    // tested FIRST and exactly: in a city most blocked views are blocked by a
    // building, and that answer costs a box scan and no terrain work at all.
    // What remains marches at the terrain's own lattice spacing rather than a
    // quarter of it — the drawn surface is piecewise linear between vertices
    // one metre apart, so a quarter-metre sample is four samples inside a
    // triangle whose shape is already decided by its corners.
    //
    // The precision given up is a terrain notch under a metre wide, which
    // costs an officer seeing you through a dip nobody can see on screen.
    // Pure, and deterministic for a given seed and box set.
    bool line_of_sight_blocked(glm::vec3 from, glm::vec3 to,
                               float slack_metres = 0.08f) const;
    glm::vec3 normal(float x, float z) const;

    // The underlying SMOOTH height field, un-triangulated. This is what the
    // mesher samples at its lattice points, so it is the right thing for
    // anything reasoning about terrain shape (route layout, spawn scatter) and
    // the WRONG thing for contact. Contact goes through height()/probe_down().
    float field_height(float x, float z) const;
    glm::vec3 field_normal(float x, float z) const;

    // Uniform physical snow accumulated on walkable ground. This raises
    // terrain, roads and authored ground slabs, but never static-box tops such
    // as roofs, buildings or obstacles. Invalid/negative depths become zero.
    void set_snow_collision_depth(float depth_metres);
    float snow_collision_depth() const { return snow_collision_depth_metres_; }
    // The field must outlive this collider; publish/update only between steps.
    // Raw depth includes the compressed layer omitted from physical contact.
    void set_snow_clearance(const SnowClearanceField* field, float raw_depth);
    // Authored roofs suppress accumulation below their underside. The field
    // must outlive the collider and be rebuilt only between simulation steps.
    void set_snow_shelter(const SnowShelterField* field) { snow_shelter_ = field; }
    float snow_depth_at(float x, float base_y, float z) const;

    // --- props ---------------------------------------------------------------
    // Static geometry is registered ONCE at world setup and never touched
    // during a step. step_vehicle() takes this object by const reference and
    // is pure in it; anything that mutates the collider mid-run breaks replay.
    void add_static_box(const AABB& bounds, Surface material = Surface::Rock);
    void add_static_oriented_box(glm::vec3 centre, glm::vec3 half_extents,
                                 float yaw, Surface material = Surface::Rock);
    void clear_static_boxes();
    const std::vector<StaticBox>& static_boxes() const { return boxes_; }

    // BOXES AND GROUND RECTS ARE BUCKETED ON A 16 m GRID, AND A QUERY READS
    // ONLY THE BUCKETS IT TOUCHES.
    //
    // Before this, probe_down() and line_of_sight_blocked() walked every box
    // and every paved rect in the city on every call — some 14,000 boxes and
    // 2,000 rects on the island — to answer a question about one point. One
    // car's four wheels could afford it. A police chase could not, because a
    // chase multiplies the queries: a cruiser stuck behind a queue re-plans
    // its way round at 10 Hz, and the siren planner (traffic/emergency.cpp)
    // checks every 0.85 m of each candidate arc with six probes and a scan of
    // every box. Measured over a scripted 75 s five-star chase, that planner
    // cost 2.2-2.7 s and its worst single step 17-22 ms, which with two or
    // three steps owed is a 25-55 ms frame. Bucketed, the same chase costs
    // 0.47 s and 2.7 ms, and the sim hashes identically step for step.
    //
    // THE ORDER IS PART OF THE ANSWER. A point query visits its candidates in
    // ascending slot order, exactly as the linear scan did, because a tie is
    // settled by order: of two coplanar tops the lower slot wins, and it is
    // that slot's material the tyre grips. Visit in bucket order instead and
    // two coplanar props of different materials swap grip under a wheel, which
    // a recorded tape reports as the physics changing.
    //
    // boxes_near() is the broad phase for callers with their own narrow test:
    // every box whose broad XZ bounds may touch the rectangle, each once, in
    // ascending slot order. It is a superset; the caller keeps its exact test.
    void boxes_near(glm::vec2 min_xz, glm::vec2 max_xz,
                    std::vector<uint32_t>& out) const;
    // Off, every query walks every box and rect exactly as it did before the
    // buckets existed. For the suite that proves the buckets change nothing,
    // and for measuring what they save; nothing in the game turns them off.
    void set_broad_phase(bool enabled) { broad_phase_ = enabled; }

    // Stable slots for kinematic props. Publish their deterministic poses
    // between sim steps, never during a vehicle/character query. Restore the
    // same poses with game state when replaying a run.
    std::size_t add_kinematic_box(const AABB& bounds);
    bool set_kinematic_box(std::size_t id, const AABB& bounds);
    bool set_kinematic_enabled(std::size_t id, bool enabled);
    bool set_kinematic_vehicle(std::size_t id, bool is_vehicle);
    bool set_kinematic_breakaway(std::size_t id, uint32_t owner_id, float speed);
    std::size_t add_kinematic_oriented_box(glm::vec3 centre, glm::vec3 half_extents,
                                           float yaw);
    bool set_kinematic_oriented_box(std::size_t id, glm::vec3 centre,
                                    glm::vec3 half_extents, float yaw);

    // Register the exact horizontal top of a rotated authored slab. This is a
    // support surface, not a body collider: walls still use StaticBox.
    void add_static_ground_rect(glm::vec2 centre, float height,
                                glm::vec2 half_extents, float yaw_radians,
                                Surface material = Surface::Rock);
    void clear_static_ground_rects();
    const std::vector<StaticGroundRect>& static_ground_rects() const {
        return ground_rects_;
    }

    // Install the horizontal collision surfaces produced from the exact baked
    // road mesh. This includes raised sidewalk slabs and excludes vertical
    // kerb faces by construction (build_road_collision owns that rule).
    // Indexed by terrain chunk so four suspension probes do not scan the
    // entire 129k-triangle city road bake every sim step.
    void set_road_collision(const RoadCollision& road);
    void clear_road_collision();
    std::size_t road_triangle_count() const { return road_surfaces_.size(); }

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

        // True when the surface came from the baked road geometry. A sidewalk
        // can therefore be distinguished from the terrain under it in tests
        // without mislabelling it as a prop box.
        bool road = false;
        // Raw pack on the selected ground surface, zero on static prop tops.
        float snow_depth_m = 0.0f;
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
    enum class ProbeVehicles { Include, Exclude };
    // Ground decals can ignore vehicle bodies without changing collision for
    // suspension, characters or any other caller.
    GroundHit probe_down(glm::vec3 origin, float max_distance,
                         ProbeVehicles vehicles = ProbeVehicles::Include) const;

    // General ray. `dir` need not be normalised. Terrain is marched and then
    // bisected — a height field has no closed-form intersection for an
    // arbitrary direction — while prop boxes get an exact slab test. Used for
    // anything that is not a suspension ray; the suspension itself uses
    // probe_down(), which is exact.
    GroundHit raycast(glm::vec3 origin, glm::vec3 dir, float max_distance) const;

private:
    struct RoadSurface {
        CollisionTri geom;
        Surface material = Surface::Rock;
    };

    bool road_surface_at(float x, float z, float origin_y, float max_distance,
                         float& out_y, glm::vec3& out_normal,
                         Surface& out_material, float& out_snow_depth) const;
    float local_snow_collision_depth(float x, float base_y, float z) const;

    // The buckets over one list (boxes_ or ground_rects_). Each bucket holds
    // slots in ascending order. A prop too large to bucket sensibly lives in
    // `oversized` instead, and every query reads that list as well.
    struct PropGrid {
        std::unordered_map<uint64_t, std::vector<uint32_t>> cells;
        std::vector<uint32_t> oversized;
        void clear() {
            cells.clear();
            oversized.clear();
        }
        void insert(uint32_t slot, glm::vec2 min_xz, glm::vec2 max_xz);
        void erase(uint32_t slot, glm::vec2 min_xz, glm::vec2 max_xz);
    };
    void index_box(std::size_t slot);
    void unindex_box(std::size_t slot);
    // Every prop whose bucket covers the point, ascending, each once. With
    // the buckets off, or at a point that is not a place, every prop.
    template <class Visit>
    void visit_props_at(const PropGrid& grid, std::size_t count, float x,
                        float z, Visit&& visit) const;
    // Every box whose buckets the XZ segment a-b passes through, ascending.
    void boxes_along(glm::vec2 a, glm::vec2 b, std::vector<uint32_t>& out) const;
    void all_boxes(std::vector<uint32_t>& out) const;

    uint64_t seed_;
    std::vector<StaticBox> boxes_;
    PropGrid box_grid_;
    std::vector<std::size_t> kinematic_boxes_;
    std::vector<StaticGroundRect> ground_rects_;
    PropGrid rect_grid_;
    bool broad_phase_ = true;
    std::vector<RoadSurface> road_surfaces_;
    std::vector<std::size_t> road_solid_slots_;
    std::unordered_map<ChunkCoord, std::vector<uint32_t>, ChunkCoordHash>
        road_cells_;
    std::vector<SurfacePaint> paint_;
    float wetness_ = 0.0f;
    float snow_collision_depth_metres_ = 0.0f;
    const SnowClearanceField* snow_clearance_ = nullptr;
    const SnowShelterField* snow_shelter_ = nullptr;
    float raw_snow_depth_metres_ = 0.0f;
};

}  // namespace apricot
