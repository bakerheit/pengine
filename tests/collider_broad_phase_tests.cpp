// The collider's prop buckets change no answer.
//
// TerrainCollider used to walk every static box and every paved ground rect
// on every probe_down() and every line_of_sight_blocked(). The buckets exist
// to stop that (terrain_collider.h has the measurements from a five-star
// chase), and they are only admissible if they are a PURE speed-up: the same
// GroundHit to the last bit, the same sight answer, for every query. A
// changed grip coefficient under one wheel is a desynced replay.
//
// So the reference here is not a formula rewritten from the implementation.
// It is the collider itself with set_broad_phase(false), which is the linear
// scan the buckets replaced, run over a world built to hit the places a grid
// goes wrong: props exactly on bucket edges and on negative coordinates,
// props straddling many buckets, rotated props and rects, coplanar tops of
// DIFFERENT materials (where only the visit order decides the answer),
// slabs too big to bucket, disabled props, parked-vehicle props, and
// kinematic props dragged across buckets between queries.
//
// An index that quietly fell back to reading everything would pass every one
// of those comparisons, so the last case checks that it narrows at all.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "core/aabb.h"
#include "core/rng.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr uint64_t kSeed = 0xB0C5EEDu;
constexpr float kCell = 16.0f;  // must match the collider's bucket size
constexpr float kHalfWorld = 600.0f;

Surface surface_of(uint64_t v) { return static_cast<Surface>(v % 4u); }

uint32_t float_bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

// Bit-for-bit: NaN == NaN and -0 != +0, which is exactly what a replay sees.
bool same_hit(const TerrainCollider::GroundHit& a, const TerrainCollider::GroundHit& b) {
    const auto same3 = [](glm::vec3 x, glm::vec3 y) {
        return float_bits(x.x) == float_bits(y.x) && float_bits(x.y) == float_bits(y.y) &&
               float_bits(x.z) == float_bits(y.z);
    };
    return a.hit == b.hit && float_bits(a.distance) == float_bits(b.distance) &&
           same3(a.point, b.point) && same3(a.normal, b.normal) &&
           a.material == b.material && float_bits(a.grip) == float_bits(b.grip) &&
           a.prop == b.prop && a.road == b.road &&
           float_bits(a.snow_depth_m) == float_bits(b.snow_depth_m);
}

// A coordinate that is often exactly on a bucket edge, or one float step
// either side of it, and otherwise anywhere.
float awkward_coordinate(Rng& rng) {
    const float roll = rng.next_float();
    const float edge = kCell * static_cast<float>(rng.next_int(-36, 36));
    if (roll < 0.15f) return edge;
    if (roll < 0.22f) return std::nextafter(edge, -1.0e9f);
    if (roll < 0.29f) return std::nextafter(edge, 1.0e9f);
    return rng.range(-kHalfWorld, kHalfWorld);
}

struct TestWorld {
    TerrainCollider collider{kSeed};
    std::vector<std::size_t> kinematic;
    std::vector<std::size_t> kinematic_oriented;
};

void build(TestWorld& w, int props = 6000) {
    TerrainCollider& c = w.collider;
    Rng rng{0x5EEDBEEFu};
    // Small props, walls and buildings. Tops quantised to half a metre so
    // coplanar tops are common, and materials drawn independently so a
    // coplanar pair usually disagrees about what the ground is made of.
    for (int i = 0; i < props; ++i) {
        const float x = awkward_coordinate(rng);
        const float z = awkward_coordinate(rng);
        const float roll = rng.next_float();
        const float half = roll < 0.6f ? rng.range(0.2f, 3.0f)
                         : roll < 0.95f ? rng.range(3.0f, 24.0f)
                                        : rng.range(24.0f, 90.0f);
        const float depth = rng.range(0.2f, 1.0f) * half + 0.1f;
        const float ground = c.height(x, z);
        const float top = std::round((ground + rng.range(0.1f, 12.0f)) * 2.0f) * 0.5f;
        const Surface material = surface_of(rng.next_u64());
        if (rng.next_float() < 0.35f) {
            c.add_static_oriented_box({x, (top + ground - 1.0f) * 0.5f, z},
                                      {half, (top - ground + 1.0f) * 0.5f, depth},
                                      rng.range(-3.2f, 3.2f), material);
        } else {
            // Edges exactly on bucket boundaries half the time.
            const bool snap = rng.next_float() < 0.5f;
            const float x0 = snap ? kCell * std::floor(x / kCell) : x - half;
            const float z0 = snap ? kCell * std::floor(z / kCell) : z - depth;
            const float x1 = snap ? x0 + kCell * static_cast<float>(rng.next_int(1, 3)) : x + half;
            const float z1 = snap ? z0 + kCell * static_cast<float>(rng.next_int(1, 2)) : z + depth;
            c.add_static_box({{x0, ground - 1.0f, z0}, {x1, top, z1}}, material);
        }
    }
    // Two slabs too big to bucket, one each side of the small props in slot
    // order, so every query merges the oversized list into its bucket. Their
    // tops are underground: coplanar ties with them are made on purpose in
    // coplanar_tops_resolve_by_slot_order(), and at eye height they would
    // block every sight line and make that comparison vacuous.
    c.add_static_box({{-2400.0f, -500.0f, -2400.0f}, {2400.0f, -80.0f, 2400.0f}},
                     Surface::Sand);
    for (int i = 0; i < 400; ++i) {
        const float x = awkward_coordinate(rng);
        const float z = awkward_coordinate(rng);
        const float ground = c.height(x, z);
        const float top = std::round((ground + rng.range(0.1f, 3.0f)) * 2.0f) * 0.5f;
        c.add_static_box({{x - 1.5f, ground - 1.0f, z - 1.5f}, {x + 1.5f, top, z + 1.5f}},
                         surface_of(rng.next_u64()));
    }
    c.add_static_oriented_box({0.0f, -330.0f, 0.0f}, {2000.0f, 250.0f, 1800.0f}, 0.4f,
                              Surface::Gravel);
    // Kinematic slots: parked vehicles, breakaway props, and plain movers.
    for (int i = 0; i < 80; ++i) {
        const float x = awkward_coordinate(rng);
        const float z = awkward_coordinate(rng);
        const float y = c.height(x, z);
        if (i % 2 == 0) {
            const std::size_t id =
                c.add_kinematic_box({{x - 2.0f, y - 0.5f, z - 1.0f}, {x + 2.0f, y + 1.5f, z + 1.0f}});
            if (i % 4 == 0) c.set_kinematic_vehicle(id, true);
            if (i % 8 == 2) c.set_kinematic_enabled(id, false);
            w.kinematic.push_back(id);
        } else {
            const std::size_t id = c.add_kinematic_oriented_box(
                {x, y + 0.5f, z}, {2.2f, 1.0f, 1.0f}, rng.range(-3.2f, 3.2f));
            if (i % 6 == 1) c.set_kinematic_breakaway(id, static_cast<uint32_t>(i), 4.0f);
            w.kinematic_oriented.push_back(id);
        }
    }
    // Paved plots: rotated, overlapping, often at the same height with
    // different materials, plus one too large to bucket.
    for (int i = 0; i < 1500; ++i) {
        const float x = awkward_coordinate(rng);
        const float z = awkward_coordinate(rng);
        const float height = std::round((c.height(x, z) + rng.range(-0.2f, 0.6f)) * 4.0f) * 0.25f;
        const float roll = rng.next_float();
        const glm::vec2 half = roll < 0.8f
            ? glm::vec2{rng.range(1.0f, 12.0f), rng.range(1.0f, 12.0f)}
            : glm::vec2{rng.range(12.0f, 120.0f), rng.range(4.0f, 60.0f)};
        const float yaw = rng.next_float() < 0.3f ? 0.0f : rng.range(-3.2f, 3.2f);
        c.add_static_ground_rect({x, z}, height, half, yaw, surface_of(rng.next_u64()));
    }
    c.add_static_ground_rect({0.0f, 0.0f}, 1.0f, {3000.0f, 3000.0f}, 0.1f, Surface::Grass);
}

TerrainCollider::GroundHit probe(TerrainCollider& c, bool buckets, glm::vec3 origin,
                                 float reach, TerrainCollider::ProbeVehicles vehicles) {
    c.set_broad_phase(buckets);
    const auto hit = c.probe_down(origin, reach, vehicles);
    c.set_broad_phase(true);
    return hit;
}

// Every kind of probe the game makes — wheels from just above the ground,
// characters from knee height, the planner's corner probes — plus points on
// bucket edges and points too far out to bucket, which take the linear path.
// (Not NaN: the terrain lookups under probe_down() floor to an integer
// lattice, and that is undefined for NaN with or without any buckets.)
int compare_probes(TestWorld& w, Rng& rng, int count, const char* label) {
    TerrainCollider& c = w.collider;
    int props_hit = 0;
    for (int i = 0; i < count; ++i) {
        glm::vec3 origin{awkward_coordinate(rng), 0.0f, awkward_coordinate(rng)};
        origin.y = c.height(origin.x, origin.z) + rng.range(-0.6f, 14.0f);
        if (i % 103 == 0) origin = {3.0e7f, 5.0f, -3.0e7f};
        const float reach = rng.range(0.2f, 20.0f);
        for (auto vehicles : {TerrainCollider::ProbeVehicles::Include,
                              TerrainCollider::ProbeVehicles::Exclude}) {
            const auto linear = probe(c, false, origin, reach, vehicles);
            const auto bucketed = probe(c, true, origin, reach, vehicles);
            if (!same_hit(linear, bucketed)) {
                std::printf("    %s probe %d at (%.9g, %.9g, %.9g) reach %.3f: "
                            "linear y %.9g mat %u prop %d, bucketed y %.9g mat %u prop %d\n",
                            label, i, static_cast<double>(origin.x),
                            static_cast<double>(origin.y), static_cast<double>(origin.z),
                            static_cast<double>(reach), static_cast<double>(linear.point.y),
                            static_cast<unsigned>(linear.material), linear.prop ? 1 : 0,
                            static_cast<double>(bucketed.point.y),
                            static_cast<unsigned>(bucketed.material), bucketed.prop ? 1 : 0);
                REQUIRE_MSG(false, "a bucketed probe_down differs from the linear scan", label);
            }
            if (bucketed.prop) ++props_hit;
        }
    }
    return props_hit;
}

void probe_down_is_unchanged_everywhere() {
    TestWorld w;
    build(w);
    Rng rng{0xD00Du};
    const int props = compare_probes(w, rng, 20000, "static world");
    // Without this the comparison could pass on a world the probes never
    // actually land on.
    std::printf("    %d of 40000 probes landed on a prop top\n", props);
    REQUIRE(props > 4000);
    apricot_test::pass("probe_down is bit-identical with and without buckets");
}

void coplanar_tops_resolve_by_slot_order() {
    // The tie, made on purpose, through the one place a bucketed query merges
    // two lists: a small box in a bucket and a slab in the oversized list.
    for (const bool slab_first : {true, false}) {
        TerrainCollider c(kSeed);
        const float y = c.height(8.0f, 8.0f) + 2.0f;
        const AABB small{{6.0f, y - 3.0f, 6.0f}, {10.0f, y, 10.0f}};
        const AABB slab{{-3000.0f, y - 3.0f, -3000.0f}, {3000.0f, y, 3000.0f}};
        if (slab_first) {
            c.add_static_box(slab, Surface::Sand);
            c.add_static_box(small, Surface::Gravel);
        } else {
            c.add_static_box(small, Surface::Gravel);
            c.add_static_box(slab, Surface::Sand);
        }
        const Surface expected = slab_first ? Surface::Sand : Surface::Gravel;
        for (float x : {6.0f, 8.0f, 10.0f}) {
            const glm::vec3 origin{x, y + 0.5f, 8.0f};
            const auto linear = probe(c, false, origin, 2.0f, TerrainCollider::ProbeVehicles::Include);
            const auto bucketed = probe(c, true, origin, 2.0f, TerrainCollider::ProbeVehicles::Include);
            REQUIRE(linear.prop && linear.material == expected);
            REQUIRE_MSG(same_hit(linear, bucketed), "a coplanar tie resolves by slot order",
                        slab_first ? "slab first" : "slab second");
        }
    }
    // The same for paved rects, where the tie also decides the snow depth.
    TerrainCollider c(kSeed);
    const float y = c.height(40.0f, 40.0f) + 0.3f;
    c.add_static_ground_rect({40.0f, 40.0f}, y, {5000.0f, 5000.0f}, 0.0f, Surface::Sand);
    c.add_static_ground_rect({40.0f, 40.0f}, y, {6.0f, 3.0f}, 0.7f, Surface::Gravel);
    const glm::vec3 origin{40.0f, y + 0.4f, 40.0f};
    const auto linear = probe(c, false, origin, 2.0f, TerrainCollider::ProbeVehicles::Include);
    const auto bucketed = probe(c, true, origin, 2.0f, TerrainCollider::ProbeVehicles::Include);
    REQUIRE(linear.material == Surface::Sand);
    REQUIRE(same_hit(linear, bucketed));
    apricot_test::pass("coplanar tops of different materials resolve by slot order, both ways");
}

void kinematic_props_follow_their_buckets() {
    TestWorld w;
    build(w);
    TerrainCollider& c = w.collider;
    Rng rng{0x4B1Eu};
    for (int round = 0; round < 40; ++round) {
        // Drag every mover somewhere new — often across several buckets,
        // sometimes by a hair across one edge — then look where it went and
        // where it was.
        for (std::size_t id : w.kinematic) {
            const float x = awkward_coordinate(rng);
            const float z = awkward_coordinate(rng);
            const float y = c.height(x, z);
            REQUIRE(c.set_kinematic_box(
                id, {{x - 2.0f, y - 0.5f, z - 1.0f}, {x + 2.0f, y + 1.5f, z + 1.0f}}));
        }
        for (std::size_t id : w.kinematic_oriented) {
            const glm::vec3 was = c.static_boxes()[id].centre;
            const float x = round % 3 == 0 ? was.x + 0.01f : awkward_coordinate(rng);
            const float z = round % 3 == 0 ? was.z : awkward_coordinate(rng);
            REQUIRE(c.set_kinematic_oriented_box(id, {x, c.height(x, z) + 0.5f, z},
                                                 {2.2f, 1.0f, 1.0f}, rng.range(-3.2f, 3.2f)));
        }
        compare_probes(w, rng, 400, "moved kinematics");
        for (std::size_t id : w.kinematic) {
            const AABB& b = c.static_boxes()[id].bounds;
            for (float fx : {0.0f, 0.5f, 1.0f})
                for (float fz : {0.0f, 0.5f, 1.0f}) {
                    const glm::vec3 origin{b.min.x + (b.max.x - b.min.x) * fx, b.max.y + 0.3f,
                                           b.min.z + (b.max.z - b.min.z) * fz};
                    for (auto vehicles : {TerrainCollider::ProbeVehicles::Include,
                                          TerrainCollider::ProbeVehicles::Exclude})
                        REQUIRE_MSG(same_hit(probe(c, false, origin, 3.0f, vehicles),
                                             probe(c, true, origin, 3.0f, vehicles)),
                                    "a moved kinematic prop is found where it now is",
                                    "kinematic");
                }
        }
    }
    apricot_test::pass("kinematic props are found where they moved to, never where they were");
}

void sight_lines_are_unchanged() {
    // A tenth of the density the probes use: at full density nearly every
    // line is blocked by the first box it meets, and a line that stops at its
    // first box never tests whether the sweep found the rest.
    TestWorld w;
    build(w, 600);
    TerrainCollider& c = w.collider;
    Rng rng{0x5167u};
    int blocked = 0, total = 0;
    const auto compare = [&](glm::vec3 from, glm::vec3 to) {
        c.set_broad_phase(false);
        const bool linear = c.line_of_sight_blocked(from, to);
        c.set_broad_phase(true);
        const bool bucketed = c.line_of_sight_blocked(from, to);
        if (linear != bucketed) {
            std::printf("    sight line (%.9g, %.9g, %.9g) -> (%.9g, %.9g, %.9g): linear %d bucketed %d\n",
                        static_cast<double>(from.x), static_cast<double>(from.y),
                        static_cast<double>(from.z), static_cast<double>(to.x),
                        static_cast<double>(to.y), static_cast<double>(to.z),
                        linear ? 1 : 0, bucketed ? 1 : 0);
            REQUIRE_MSG(false, "a bucketed sight line differs from the linear scan", "sight");
        }
        blocked += linear ? 1 : 0;
        ++total;
    };
    for (int i = 0; i < 3000; ++i) {
        const float ax = awkward_coordinate(rng), az = awkward_coordinate(rng);
        glm::vec3 from{ax, c.height(ax, az) + rng.range(0.5f, 3.0f), az};
        glm::vec3 to;
        const float kind = rng.next_float();
        if (kind < 0.5f) {
            // An officer looking at a suspect, anywhere from next door to the
            // five-star detection range.
            const float range = rng.next_float() < 0.8f ? rng.range(1.0f, 150.0f)
                                                        : rng.range(150.0f, 850.0f);
            const float heading = rng.range(-3.2f, 3.2f);
            to = {ax + range * std::cos(heading), 0.0f, az + range * std::sin(heading)};
        } else if (kind < 0.65f) {
            // Along a bucket edge, where a sweep that is off by one column
            // misses whole buildings.
            const float edge = kCell * static_cast<float>(rng.next_int(-36, 36));
            from.x = edge;
            to = {edge, 0.0f, az + rng.range(-300.0f, 300.0f)};
        } else if (kind < 0.8f) {
            to = {ax + rng.range(-300.0f, 300.0f), 0.0f, az};  // axis-aligned in z
            from.z = kCell * std::round(az / kCell);
            to.z = from.z;
        } else if (kind < 0.9f) {
            to = {ax + rng.range(-1e-5f, 1e-5f), 0.0f, az + rng.range(-1e-5f, 1e-5f)};  // steep
        } else {
            to = {awkward_coordinate(rng), 0.0f, awkward_coordinate(rng)};
        }
        to.y = c.height(to.x, to.z) + rng.range(0.5f, 3.0f);
        if (kind >= 0.8f && kind < 0.9f) to.y = from.y + rng.range(-20.0f, 20.0f);
        compare(from, to);
    }
    // Starting inside a box, and ending inside one.
    for (std::size_t slot = 0; slot < 600; slot += 7) {
        const AABB& b = c.static_boxes()[slot].bounds;
        const glm::vec3 inside = b.center();
        compare(inside, inside + glm::vec3{40.0f, 1.0f, -25.0f});
        compare(inside + glm::vec3{-35.0f, 2.0f, 30.0f}, inside);
    }
    std::printf("    %d of %d sight lines blocked\n", blocked, total);
    // A comparison over lines that are all clear, or all blocked, proves
    // nothing about the boxes.
    REQUIRE(blocked > total / 10 && blocked < total - total / 10);
    apricot_test::pass("line_of_sight_blocked gives the same answer with and without buckets");
}

void boxes_near_is_a_sorted_superset() {
    TestWorld w;
    build(w);
    const TerrainCollider& c = w.collider;
    const auto& boxes = c.static_boxes();
    Rng rng{0x9EA4u};
    std::vector<uint32_t> near;
    for (int i = 0; i < 2000; ++i) {
        const glm::vec2 centre{awkward_coordinate(rng), awkward_coordinate(rng)};
        const glm::vec2 half{rng.range(0.0f, 30.0f), rng.range(0.0f, 30.0f)};
        const glm::vec2 lo = centre - half, hi = centre + half;
        c.boxes_near(lo, hi, near);
        REQUIRE(std::is_sorted(near.begin(), near.end()));
        REQUIRE(std::adjacent_find(near.begin(), near.end()) == near.end());
        for (std::size_t slot = 0; slot < boxes.size(); ++slot) {
            const AABB& b = boxes[slot].bounds;
            const bool touches = b.min.x <= hi.x && b.max.x >= lo.x &&
                                 b.min.z <= hi.y && b.max.z >= lo.y;
            if (touches && !std::binary_search(near.begin(), near.end(),
                                               static_cast<uint32_t>(slot))) {
                std::printf("    region (%.9g, %.9g)-(%.9g, %.9g) missed slot %zu\n",
                            static_cast<double>(lo.x), static_cast<double>(lo.y),
                            static_cast<double>(hi.x), static_cast<double>(hi.y), slot);
                REQUIRE_MSG(false, "boxes_near missed a box that touches the region", "near");
            }
        }
    }
    apricot_test::pass("boxes_near returns every touching box, sorted and once each");
}

// Crowd::emergency_path_clear() asks boxes_near() for a square of half-size
// 1.5 * (length * |tangent_xz| + width * |right_xz|) around each sample, then
// runs this footprint test (copied from it) on what comes back. The square
// must contain every box the test can reject on, for any pose, on any box,
// oriented or not.
void the_planner_footprint_reach_is_conservative() {
    TestWorld w;
    build(w);
    const TerrainCollider& c = w.collider;
    const auto& boxes = c.static_boxes();
    Rng rng{0xF007u};
    std::vector<uint32_t> near;
    int overlaps = 0;  // with bucketed boxes; the two slabs overlap everything
    for (int i = 0; i < 1500; ++i) {
        const glm::vec3 position{awkward_coordinate(rng), 0.0f, awkward_coordinate(rng)};
        const float heading = rng.range(-3.2f, 3.2f);
        const float slope = rng.range(-0.3f, 0.3f);
        const glm::vec3 tangent = glm::normalize(glm::vec3{std::cos(heading), slope, std::sin(heading)});
        const glm::vec3 right{-tangent.z, 0.0f, tangent.x};
        const float length = rng.range(2.2f, 7.5f);
        const float width = rng.range(1.0f, 1.8f);
        const float reach = 1.5f * (length * glm::length(glm::vec2{tangent.x, tangent.z}) +
                                    width * glm::length(glm::vec2{right.x, right.z})) + 0.05f;
        c.boxes_near({position.x - reach, position.z - reach},
                     {position.x + reach, position.z + reach}, near);
        for (std::size_t slot = 0; slot < boxes.size(); ++slot) {
            const StaticBox& box = boxes[slot];
            const auto& b = box.collision_bounds();
            const glm::vec3 p = box.local_point(position);
            const glm::vec3 f = box.local_direction(tangent);
            const glm::vec3 r = box.local_direction(right);
            const float ex = std::fabs(f.x) * length + std::fabs(r.x) * width;
            const float ez = std::fabs(f.z) * length + std::fabs(r.z) * width;
            const bool overlap = p.x + ex > b.min.x && p.x - ex < b.max.x &&
                                 p.z + ez > b.min.z && p.z - ez < b.max.z;
            if (!overlap) continue;
            if (box.bounds.max.x - box.bounds.min.x < 400.0f) ++overlaps;
            REQUIRE_MSG(std::binary_search(near.begin(), near.end(), static_cast<uint32_t>(slot)),
                        "a box the planner footprint overlaps is outside its reach", "reach");
        }
    }
    std::printf("    %d footprint overlaps checked\n", overlaps);
    REQUIRE(overlaps > 1000);
    apricot_test::pass("the siren planner's reach covers every box its footprint can touch");
}

void clearing_forgets_the_buckets() {
    TerrainCollider c(kSeed);
    for (int i = 0; i < 50; ++i) {
        const float x = static_cast<float>(i) * 3.0f;
        c.add_static_box({{x, -5.0f, 0.0f}, {x + 2.0f, 50.0f, 2.0f}}, Surface::Rock);
    }
    c.add_static_ground_rect({10.0f, 1.0f}, 40.0f, {30.0f, 30.0f}, 0.2f, Surface::Sand);
    c.clear_static_boxes();
    c.clear_static_ground_rects();
    std::vector<uint32_t> near;
    c.boxes_near({-10.0f, -10.0f}, {200.0f, 10.0f}, near);
    REQUIRE(near.empty());
    // Cleared, it must answer exactly as a collider that never had props.
    const TerrainCollider bare(kSeed);
    const glm::vec3 origin{11.0f, 60.0f, 1.0f};
    REQUIRE(same_hit(c.probe_down(origin, 100.0f), bare.probe_down(origin, 100.0f)));
    const glm::vec3 a{-5.0f, c.height(-5.0f, 1.0f) + 1.5f, 1.0f};
    const glm::vec3 b{150.0f, c.height(150.0f, 1.0f) + 1.5f, 1.0f};
    REQUIRE(c.line_of_sight_blocked(a, b) == bare.line_of_sight_blocked(a, b));
    // Re-registered props are found again from slot zero.
    c.add_static_box({{0.0f, -5.0f, 0.0f}, {4.0f, 50.0f, 4.0f}}, Surface::Gravel);
    c.boxes_near({1.0f, 1.0f}, {2.0f, 2.0f}, near);
    REQUIRE(near.size() == 1 && near[0] == 0u);
    REQUIRE(c.probe_down({2.0f, 55.0f, 2.0f}, 10.0f).prop);
    apricot_test::pass("clearing the props clears their buckets, and new props start clean");
}

void the_buckets_actually_narrow() {
    TestWorld w;
    build(w);
    const TerrainCollider& c = w.collider;
    const std::size_t total = c.static_boxes().size();
    Rng rng{0xA110u};
    std::vector<uint32_t> near;
    std::size_t read = 0;
    const int queries = 500;
    for (int i = 0; i < queries; ++i) {
        const glm::vec2 centre{rng.range(-500.0f, 500.0f), rng.range(-500.0f, 500.0f)};
        c.boxes_near(centre - 6.0f, centre + 6.0f, near);
        read += near.size();
    }
    const double mean = static_cast<double>(read) / queries;
    std::printf("    a 12 m square reads %.1f of %zu boxes on average\n", mean, total);
    // The world is packed far denser than the island, so this is generous.
    // What it catches is an index that has quietly become a linear scan.
    REQUIRE(mean < 0.05 * static_cast<double>(total));
    // And with the buckets off, the same query is the linear scan.
    TestWorld off;
    build(off);
    off.collider.set_broad_phase(false);
    off.collider.boxes_near({0.0f, 0.0f}, {1.0f, 1.0f}, near);
    REQUIRE(near.size() == off.collider.static_boxes().size());
    apricot_test::pass("a small query reads a small fraction of the props");
}

}  // namespace

int main() {
    probe_down_is_unchanged_everywhere();
    coplanar_tops_resolve_by_slot_order();
    kinematic_props_follow_their_buckets();
    sight_lines_are_unchanged();
    boxes_near_is_a_sorted_superset();
    the_planner_footprint_reach_is_conservative();
    clearing_forgets_the_buckets();
    the_buckets_actually_narrow();
    return apricot_test::done("collider_broad_phase_tests");
}
