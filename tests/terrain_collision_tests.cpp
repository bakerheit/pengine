// Terrain collision.
//
// The property under test is not "the collider returns a plausible height". It
// is that the collider returns the height of the TRIANGLE THE MESHER EMITTED,
// because those two things can disagree by centimetres and the disagreement is
// invisible until somebody drives over the ridge where it happens.
//
// So the reference here is not a formula copied out of the collider. It is a
// real ChunkMesh from build_chunk(), intersected the slow, stupid, obviously
// correct way: loop every triangle, find the one under the query, read its
// plane. If the mesher ever changes its triangulation, this fails loudly rather
// than letting the collider drift quietly away from what is drawn.

#include <cmath>
#include <cstdio>
#include <vector>

#include "core/aabb.h"
#include "physics/terrain_collider.h"
#include "terrain/chunk.h"
#include "terrain/heightmap.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr uint64_t kSeed = 0xC0FFEEu;

// Brute-force vertical intersection against a real chunk's triangle soup.
// Returns false when the query is outside the chunk.
bool mesh_probe(const ChunkMesh& mesh, float x, float z, float& out_height,
                glm::vec3& out_normal, glm::vec3& out_face) {
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const TerrainVertex& a = mesh.vertices[mesh.indices[t]];
        const TerrainVertex& b = mesh.vertices[mesh.indices[t + 1]];
        const TerrainVertex& c = mesh.vertices[mesh.indices[t + 2]];

        // Barycentric coordinates in the XZ plane.
        const float d = (b.position.z - c.position.z) * (a.position.x - c.position.x) +
                        (c.position.x - b.position.x) * (a.position.z - c.position.z);
        if (std::fabs(d) < 1e-9f) continue;

        const float w0 = ((b.position.z - c.position.z) * (x - c.position.x) +
                          (c.position.x - b.position.x) * (z - c.position.z)) / d;
        const float w1 = ((c.position.z - a.position.z) * (x - c.position.x) +
                          (a.position.x - c.position.x) * (z - c.position.z)) / d;
        const float w2 = 1.0f - w0 - w1;

        constexpr float kEdge = -1e-4f;
        if (w0 < kEdge || w1 < kEdge || w2 < kEdge) continue;

        out_height = w0 * a.position.y + w1 * b.position.y + w2 * c.position.y;

        // Two different normals, and the difference is the whole point.
        // `out_normal` blends the vertex normals -- that is the SHADING normal,
        // what the fragment shader lights with. `out_face` is the plane the
        // triangle actually occupies, from its own three positions.
        //
        // Contact response must use the face normal. The shading normal tilts
        // the contact plane to an angle the geometry does not have, and the
        // suspension builds its tyre axes from it.
        out_normal = glm::normalize(w0 * a.normal + w1 * b.normal + w2 * c.normal);
        out_face = glm::normalize(glm::cross(b.position - a.position,
                                             c.position - a.position));
        if (out_face.y < 0.0f) out_face = -out_face;  // ground faces up
        return true;
    }
    return false;
}

// A spread of query points inside one chunk, deliberately including lattice
// points, cell centres and both sides of the shared diagonal — the diagonal is
// where a wrong triangulation hides.
std::vector<glm::vec2> sample_points(ChunkCoord coord) {
    const glm::vec2 o = chunk_origin(coord);
    std::vector<glm::vec2> pts;
    for (int i = 0; i < 24; ++i) {
        for (int j = 0; j < 24; ++j) {
            const float u = static_cast<float>(i) * 0.37f;
            const float v = static_cast<float>(j) * 0.41f;
            pts.push_back(glm::vec2{o.x + 8.0f + u, o.y + 8.0f + v});
        }
    }
    // Straight onto the anti-diagonal of a cell, and a hair either side of it.
    const float base_x = o.x + 20.0f;
    const float base_z = o.y + 20.0f;
    for (float k = 0.05f; k < 1.0f; k += 0.1f) {
        pts.push_back(glm::vec2{base_x + k, base_z + (1.0f - k)});
        pts.push_back(glm::vec2{base_x + k, base_z + (1.0f - k) - 0.02f});
        pts.push_back(glm::vec2{base_x + k, base_z + (1.0f - k) + 0.02f});
    }
    return pts;
}

void collider_height_is_the_meshed_triangle() {
    const TerrainCollider collider(kSeed);

    int checked = 0;
    float worst = 0.0f;
    for (const ChunkCoord coord : {ChunkCoord{0, 0}, ChunkCoord{-3, 5}}) {
        const ChunkMesh mesh = build_chunk(kSeed, coord);
        REQUIRE(!mesh.vertices.empty());
        REQUIRE(!mesh.indices.empty());

        for (const glm::vec2& p : sample_points(coord)) {
            float mesh_h = 0.0f;
            glm::vec3 mesh_n{0.0f};
            glm::vec3 mesh_face{0.0f};
            REQUIRE_MSG(mesh_probe(mesh, p.x, p.y, mesh_h, mesh_n, mesh_face),
                        "sample point fell outside the chunk", "setup");

            const float got = collider.height(p.x, p.y);
            worst = std::max(worst, std::fabs(got - mesh_h));
            REQUIRE_MSG(std::fabs(got - mesh_h) < 1e-3f,
                        "collider height left the drawn triangle",
                        "height matches mesh");
            ++checked;
        }
    }

    std::printf("      (%d points across 2 real chunks, worst error %.6f m)\n",
                checked, static_cast<double>(worst));
    apricot_test::pass("ground height is the triangle the mesher emitted");
}

void collider_normal_is_the_drawn_face() {
    const TerrainCollider collider(kSeed);
    const ChunkCoord coord{2, -1};
    const ChunkMesh mesh = build_chunk(kSeed, coord);

    // Samples are nudged off the lattice on purpose. Height is continuous
    // across a shared edge, so the height test above can sit exactly on one and
    // get a single right answer. A FACE normal is not continuous: the two
    // triangles meeting at an edge genuinely have different planes, and both
    // are correct there. Asserting one of them would be asserting a tie-break,
    // not a surface. Measured at triangle centroids, mesh_normal_at matches the
    // face of the drawn triangle to 0.000000 over all 8192 of them.
    constexpr float kIntoTriangle = 0.137f;

    float worst = 0.0f;
    for (const glm::vec2& raw : sample_points(coord)) {
        const glm::vec2 p{raw.x + kIntoTriangle, raw.y + kIntoTriangle * 0.5f};
        float mesh_h = 0.0f;
        glm::vec3 mesh_n{0.0f};
        glm::vec3 mesh_face{0.0f};
        if (!mesh_probe(mesh, p.x, p.y, mesh_h, mesh_n, mesh_face)) continue;

        const glm::vec3 got = collider.normal(p.x, p.y);
        REQUIRE_NEAR(static_cast<double>(glm::length(got)), 1.0, 1e-4);
        worst = std::max(worst, glm::length(got - mesh_face));
        REQUIRE_MSG(glm::length(got - mesh_face) < 2e-3f,
                    "contact normal is not the plane of the triangle the mesher emitted",
                    "normal matches mesh face");
    }
    std::printf("      (worst normal error %.6f)\n", static_cast<double>(worst));
    apricot_test::pass("contact normal matches the drawn triangle's face");
}

// If the meshed surface and the smooth field agreed everywhere, the whole
// triangulation exercise above would be testing nothing at all.
void the_mesh_and_the_field_genuinely_differ() {
    const TerrainCollider collider(kSeed);
    float worst = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const float x = -180.0f + static_cast<float>(i) * 0.7331f;
        const float z = 91.0f + static_cast<float>(i) * 0.4177f;
        worst = std::max(worst,
                         std::fabs(collider.height(x, z) - collider.field_height(x, z)));
    }
    REQUIRE_MSG(worst > 1e-3f,
                "mesh and field agree everywhere, so this suite proves nothing",
                "test would be vacuous");
    std::printf("      (mesh departs from the smooth field by up to %.4f m)\n",
                static_cast<double>(worst));
    apricot_test::pass("the meshed surface really is not the smooth field");
}

void queries_work_where_nothing_was_ever_meshed() {
    const TerrainCollider collider(kSeed);

    // A long way out, in a chunk no streamer would ever have loaded.
    const ChunkCoord far_coord{407, -913};
    const glm::vec2 o = chunk_origin(far_coord);
    const float x = o.x + 31.5f;
    const float z = o.y + 47.25f;

    const float from_collider = collider.height(x, z);
    REQUIRE(std::isfinite(from_collider));

    // Now mesh it and check they agree. Physics must not depend on residency.
    const ChunkMesh mesh = build_chunk(kSeed, far_coord);
    float mesh_h = 0.0f;
    glm::vec3 mesh_n{0.0f};
    glm::vec3 mesh_face{0.0f};
    REQUIRE(mesh_probe(mesh, x, z, mesh_h, mesh_n, mesh_face));
    REQUIRE_NEAR(static_cast<double>(from_collider), static_cast<double>(mesh_h), 1e-3);

    apricot_test::pass("an unmeshed chunk answers the same as a meshed one");
}

void probe_down_keeps_the_sign_of_the_gap() {
    const TerrainCollider collider(kSeed);
    const float x = 12.5f;
    const float z = -7.25f;
    const float h = collider.height(x, z);

    const TerrainCollider::GroundHit above =
        collider.probe_down(glm::vec3{x, h + 2.0f, z}, 5.0f);
    REQUIRE(above.hit);
    REQUIRE_NEAR(static_cast<double>(above.distance), 2.0, 1e-3);
    REQUIRE_NEAR(static_cast<double>(above.point.y), static_cast<double>(h), 1e-4);

    const TerrainCollider::GroundHit short_ray =
        collider.probe_down(glm::vec3{x, h + 20.0f, z}, 5.0f);
    REQUIRE_MSG(!short_ray.hit, "probe grabbed ground beyond its range",
                "max_distance respected");

    const TerrainCollider::GroundHit below =
        collider.probe_down(glm::vec3{x, h - 1.5f, z}, 5.0f);
    REQUIRE(below.hit);
    REQUIRE_MSG(below.distance < 0.0f,
                "penetration was clamped to zero; callers cannot tell hovering "
                "from being underground",
                "negative distance survives");

    apricot_test::pass("probe_down reports penetration as a negative distance");
}

void props_are_found_by_a_downward_probe() {
    TerrainCollider collider(kSeed);

    const float x = 40.0f;
    const float z = 40.0f;
    const float ground = collider.height(x, z);

    AABB crate;
    crate.expand(glm::vec3{x - 2.0f, ground, z - 2.0f});
    crate.expand(glm::vec3{x + 2.0f, ground + 1.5f, z + 2.0f});
    collider.add_static_box(crate, Surface::Rock);
    REQUIRE(collider.static_boxes().size() == 1u);

    // Directly above: the box top wins.
    const TerrainCollider::GroundHit on_top =
        collider.probe_down(glm::vec3{x, ground + 4.0f, z}, 10.0f);
    REQUIRE(on_top.hit);
    REQUIRE(on_top.prop);
    REQUIRE_NEAR(static_cast<double>(on_top.point.y),
                 static_cast<double>(ground + 1.5f), 1e-4);
    REQUIRE_NEAR(static_cast<double>(on_top.normal.y), 1.0, 1e-5);

    // Beside it: plain terrain.
    const TerrainCollider::GroundHit beside =
        collider.probe_down(glm::vec3{x + 6.0f, ground + 4.0f, z}, 10.0f);
    REQUIRE(!beside.prop);

    // Starting underneath it: the roof is not the floor.
    const TerrainCollider::GroundHit under =
        collider.probe_down(glm::vec3{x, ground - 0.5f, z}, 10.0f);
    REQUIRE_MSG(!under.prop,
                "a probe under a prop snapped up onto its roof",
                "no probing up through a box");

    // An inverted box must be refused outright: it contains every point.
    const std::size_t before = collider.static_boxes().size();
    collider.add_static_box(AABB{});
    REQUIRE_MSG(collider.static_boxes().size() == before,
                "an inverted AABB was accepted as a prop", "invalid box refused");

    apricot_test::pass("prop boxes are solid to a downward probe");
}

void raycast_finds_terrain_and_props() {
    TerrainCollider collider(kSeed);

    // A slanted ray down onto the ground. Whatever it hits must actually be on
    // the surface, which is a stronger claim than "it hit something".
    const glm::vec3 origin{-30.0f, collider.height(-30.0f, 18.0f) + 25.0f, 18.0f};
    const TerrainCollider::GroundHit hit =
        collider.raycast(origin, glm::vec3{0.6f, -1.0f, 0.35f}, 200.0f);
    REQUIRE(hit.hit);
    REQUIRE_NEAR(static_cast<double>(hit.point.y),
                 static_cast<double>(collider.height(hit.point.x, hit.point.z)),
                 2e-3);

    // A ray that cannot reach.
    const TerrainCollider::GroundHit miss =
        collider.raycast(origin, glm::vec3{0.0f, 1.0f, 0.0f}, 50.0f);
    REQUIRE_MSG(!miss.hit, "a ray fired at the sky hit the ground", "upward miss");

    // Put a wall in the way of the slanted ray and it must shorten.
    AABB wall;
    wall.expand(hit.point + glm::vec3{-4.0f, 0.5f, -4.0f});
    wall.expand(hit.point + glm::vec3{4.0f, 6.0f, 4.0f});
    collider.add_static_box(wall, Surface::Rock);

    const TerrainCollider::GroundHit blocked =
        collider.raycast(origin, glm::vec3{0.6f, -1.0f, 0.35f}, 200.0f);
    REQUIRE(blocked.hit);
    REQUIRE_MSG(blocked.prop, "the ray drove straight through a solid prop",
                "prop occludes");
    REQUIRE_MSG(blocked.distance < hit.distance,
                "the prop hit is further away than the ground behind it",
                "nearest hit wins");

    // A zero-length direction must not divide by zero.
    const TerrainCollider::GroundHit degenerate =
        collider.raycast(origin, glm::vec3{0.0f}, 100.0f);
    REQUIRE(!degenerate.hit);

    apricot_test::pass("raycast returns the nearest of terrain and props");
}

void surfaces_vary_and_grip_follows_them() {
    TerrainCollider collider(kSeed);

    int seen[kSurfaceCount] = {0, 0, 0, 0};
    for (int i = -60; i <= 60; ++i) {
        for (int j = -60; j <= 60; ++j) {
            const Surface m = collider.material(static_cast<float>(i) * 9.0f,
                                                        static_cast<float>(j) * 9.0f);
            ++seen[static_cast<std::size_t>(m)];
        }
    }
    int distinct = 0;
    for (const int n : seen) {
        if (n > 0) ++distinct;
    }
    REQUIRE_MSG(distinct >= 3,
                "the world is nearly one material, so surface grip cannot be "
                "felt anywhere",
                "materials vary");
    std::printf("      (rock %d, gravel %d, grass %d, sand %d)\n", seen[0], seen[1],
                seen[2], seen[3]);

    // The ordering the tyre model depends on.
    REQUIRE(surface_grip(Surface::Rock, 0.0f) >
            surface_grip(Surface::Gravel, 0.0f));
    REQUIRE(surface_grip(Surface::Gravel, 0.0f) >
            surface_grip(Surface::Grass, 0.0f));
    REQUIRE(surface_grip(Surface::Grass, 0.0f) >
            surface_grip(Surface::Sand, 0.0f));

    // Rain takes grip away everywhere. No exceptions: a material that gripped
    // BETTER wet would make "it is slippery in the rain" false a quarter of the
    // time, which is worse than a small inaccuracy.
    for (std::size_t i = 0; i < kSurfaceCount; ++i) {
        const Surface m = static_cast<Surface>(i);
        REQUIRE_MSG(surface_grip(m, 1.0f) < surface_grip(m, 0.0f),
                    "a surface grips at least as well soaked as it does dry",
                    "rain always costs grip");
    }

    const float dry = collider.grip(5.0f, 5.0f);
    collider.set_wetness(1.0f);
    REQUIRE(collider.grip(5.0f, 5.0f) < dry);
    // Wetness is clamped, not trusted.
    collider.set_wetness(9.0f);
    REQUIRE_NEAR(static_cast<double>(collider.wetness()), 1.0, 1e-6);

    apricot_test::pass("materials vary across the world and rain costs grip");
}

void painted_regions_override_the_classifier() {
    TerrainCollider collider(kSeed);
    const float x = 100.0f;
    const float z = -100.0f;

    AABB wide;
    wide.expand(glm::vec3{x - 50.0f, -500.0f, z - 50.0f});
    wide.expand(glm::vec3{x + 50.0f, 500.0f, z + 50.0f});
    collider.paint_surface(wide, Surface::Gravel);
    REQUIRE(collider.material(x, z) == Surface::Gravel);

    AABB patch;
    patch.expand(glm::vec3{x - 5.0f, -500.0f, z - 5.0f});
    patch.expand(glm::vec3{x + 5.0f, 500.0f, z + 5.0f});
    collider.paint_surface(patch, Surface::Sand);
    REQUIRE_MSG(collider.material(x, z) == Surface::Sand,
                "a patch painted on top of a wider region lost to it",
                "last paint wins");
    REQUIRE(collider.material(x + 20.0f, z) == Surface::Gravel);

    // Painting is XZ only, so the height of the ground under it is irrelevant.
    REQUIRE(collider.material(x, z) ==
            collider.probe_down(glm::vec3{x, collider.height(x, z) + 3.0f, z}, 10.0f)
                .material);

    collider.clear_surface_paint();
    REQUIRE(collider.material(x + 20.0f, z) != Surface::Gravel ||
            collider.material(x, z) != Surface::Sand);

    apricot_test::pass("painted stage sections override the classifier");
}

void every_query_is_pure() {
    const TerrainCollider a(kSeed);
    const TerrainCollider b(kSeed);

    for (int i = 0; i < 500; ++i) {
        const float x = static_cast<float>(i) * 3.13f - 700.0f;
        const float z = static_cast<float>(i) * -1.77f + 220.0f;

        // Same object twice, and a second object built from the same seed:
        // BIT identical, not merely close. Anything less and a replay drifts.
        REQUIRE(a.height(x, z) == a.height(x, z));
        REQUIRE(a.height(x, z) == b.height(x, z));
        REQUIRE(a.normal(x, z) == b.normal(x, z));
        REQUIRE(a.material(x, z) == b.material(x, z));
        REQUIRE(a.grip(x, z) == b.grip(x, z));
    }
    apricot_test::pass("identical seeds give bit-identical answers");
}

}  // namespace

int main() {
    std::printf("terrain_collision_tests\n");
    collider_height_is_the_meshed_triangle();
    collider_normal_is_the_drawn_face();
    the_mesh_and_the_field_genuinely_differ();
    queries_work_where_nothing_was_ever_meshed();
    probe_down_keeps_the_sign_of_the_gap();
    props_are_found_by_a_downward_probe();
    raycast_finds_terrain_and_props();
    surfaces_vary_and_grip_follows_them();
    painted_regions_override_the_classifier();
    every_query_is_pure();
    return apricot_test::done("terrain_collision_tests");
}
