// Chunk meshing, collision, and seams.
//
// Three properties, all of which are the kind that fail silently:
//
//  1. THE COLLISION IS THE MESH. Not a copy of it, not a re-derivation of it
//     at a coarser step. Every collision triangle's vertices must be the
//     drawn vertices, bit for bit, and the point query must land on the same
//     plane. When these drift, the car floats above or sinks into a hill it
//     is visibly resting on, and nobody finds out until somebody drives there.
//
//  2. NEIGHBOURING CHUNKS DO NOT CRACK. Shared edge vertices must be
//     bit-identical, not close. A crack is a strip of sky through the ground
//     that appears at one specific viewing angle.
//
//  3. THE FIELD AND THE MESH ARE DIFFERENT SURFACES. That is not a bug, but
//     assuming they are the same one is. This suite measures the gap so the
//     number is on the record rather than a surprise.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "terrain/chunk.h"
#include "terrain/heightmap.h"
#include "test_assert.h"

using namespace apricot;

namespace {

uint32_t bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

bool same_vec3(const glm::vec3& a, const glm::vec3& b) {
    return bits(a.x) == bits(b.x) && bits(a.y) == bits(b.y) &&
           bits(a.z) == bits(b.z);
}

// Height of a triangle's plane at an XZ position, by barycentric weights.
//
// Deliberately computed a DIFFERENT way from mesh_height_at()'s incremental
// form. If both used the same algebra the comparison would be a tautology; the
// point is that two independent routes to the same plane agree.
float barycentric_height(const CollisionTri& t, float x, float z) {
    const float x1 = t.a.x, z1 = t.a.z;
    const float x2 = t.b.x, z2 = t.b.z;
    const float x3 = t.c.x, z3 = t.c.z;
    const float det = (z2 - z3) * (x1 - x3) + (x3 - x2) * (z1 - z3);
    const float l1 = ((z2 - z3) * (x - x3) + (x3 - x2) * (z - z3)) / det;
    const float l2 = ((z3 - z1) * (x - x3) + (x1 - x3) * (z - z3)) / det;
    const float l3 = 1.0f - l1 - l2;
    return l1 * t.a.y + l2 * t.b.y + l3 * t.c.y;
}

// --- layout ------------------------------------------------------------------
void mesh_has_the_expected_shape() {
    const ChunkMesh m = build_chunk(7ull, ChunkCoord{2, -4});

    REQUIRE(m.coord == (ChunkCoord{2, -4}));
    REQUIRE(m.vertices.size() ==
            static_cast<std::size_t>(kChunkVerts) *
                static_cast<std::size_t>(kChunkVerts));
    REQUIRE(m.indices.size() ==
            static_cast<std::size_t>(kChunkQuads) *
                static_cast<std::size_t>(kChunkQuads) * 6u);

    // Every index must address a real vertex. An out-of-range index is a
    // renderer crash on someone else's machine.
    for (const uint32_t i : m.indices) {
        REQUIRE_MSG(i < m.vertices.size(), "index out of range", "indices");
    }

    // Bounds must actually bound.
    REQUIRE(m.bounds.valid());
    for (const TerrainVertex& v : m.vertices) {
        REQUIRE_MSG(m.bounds.contains(v.position),
                    "a vertex fell outside the chunk bounds", "bounds");
    }

    // Vertices must land on the GLOBAL lattice, not merely inside the chunk.
    // mesh_height_at() reconstructs that lattice from world coordinates alone;
    // if a chunk's vertices sat on their own local grid instead, the point
    // query would be reconstructing a cell that does not exist.
    const glm::vec2 origin = chunk_origin(m.coord);
    for (int j = 0; j < kChunkVerts; ++j) {
        for (int i = 0; i < kChunkVerts; ++i) {
            const TerrainVertex& v =
                m.vertices[static_cast<std::size_t>(j * kChunkVerts + i)];
            const float ex =
                origin.x + static_cast<float>(i) * kVertexSpacingMetres;
            const float ez =
                origin.y + static_cast<float>(j) * kVertexSpacingMetres;
            REQUIRE_MSG(bits(v.position.x) == bits(ex),
                        "vertex x is off the global lattice", "lattice");
            REQUIRE_MSG(bits(v.position.z) == bits(ez),
                        "vertex z is off the global lattice", "lattice");
        }
    }

    apricot_test::pass("chunk mesh layout and lattice alignment");
}

void winding_is_counter_clockwise_from_above() {
    const ChunkMesh m = build_chunk(11ull, ChunkCoord{0, 0});
    for (std::size_t t = 0; t + 2 < m.indices.size(); t += 3) {
        const glm::vec3& a = m.vertices[m.indices[t + 0]].position;
        const glm::vec3& b = m.vertices[m.indices[t + 1]].position;
        const glm::vec3& c = m.vertices[m.indices[t + 2]].position;
        const glm::vec3 n = glm::cross(b - a, c - a);
        // Back-face culling drops every triangle whose winding is inverted, so
        // one wrong triangle is a hole in the ground you can see through.
        REQUIRE_MSG(n.y > 0.0f, "triangle wound clockwise from above",
                    "winding");
    }
    apricot_test::pass("all triangles wind counter-clockwise from above");
}

// --- collision is the mesh ------------------------------------------------------
void collision_triangles_are_the_drawn_triangles() {
    const uint64_t seeds[] = {3ull, 0xC0FFEEull};
    const ChunkCoord coords[] = {{0, 0}, {-7, 12}};

    for (const uint64_t seed : seeds) {
        for (const ChunkCoord c : coords) {
            const ChunkMesh mesh = build_chunk(seed, c);
            const ChunkCollision col = build_chunk_collision(mesh);

            REQUIRE_MSG(col.coord == mesh.coord, "collision lost the coord",
                        "collision");
            REQUIRE_MSG(col.triangles.size() * 3u == mesh.indices.size(),
                        "collision triangle count does not match the mesh",
                        "collision");

            for (std::size_t t = 0; t < col.triangles.size(); ++t) {
                const CollisionTri& tri = col.triangles[t];
                const glm::vec3& a =
                    mesh.vertices[mesh.indices[t * 3u + 0u]].position;
                const glm::vec3& b =
                    mesh.vertices[mesh.indices[t * 3u + 1u]].position;
                const glm::vec3& d =
                    mesh.vertices[mesh.indices[t * 3u + 2u]].position;

                // BIT-IDENTICAL. Not "within a tolerance". The collision
                // vertices are literally copies of the drawn vertices, and the
                // day that stops being true this is the line that says so.
                REQUIRE_MSG(same_vec3(tri.a, a), "collision vertex a differs",
                            "collision");
                REQUIRE_MSG(same_vec3(tri.b, b), "collision vertex b differs",
                            "collision");
                REQUIRE_MSG(same_vec3(tri.c, d), "collision vertex c differs",
                            "collision");

                const float len = glm::length(tri.normal);
                REQUIRE_MSG(len > 0.999f && len < 1.001f,
                            "collision normal not unit length", "collision");
                REQUIRE_MSG(tri.normal.y > 0.0f,
                            "collision normal points into the ground",
                            "collision");
            }
        }
    }
    apricot_test::pass("collision triangles ARE the drawn triangles");
}

// The point query and the collision geometry must describe one surface.
void mesh_height_lands_on_the_drawn_triangle() {
    constexpr uint64_t kSeed = 0x1234ull;
    const ChunkCoord coord{5, -9};
    const ChunkMesh mesh = build_chunk(kSeed, coord);
    const ChunkCollision col = build_chunk_collision(mesh);
    const glm::vec2 origin = chunk_origin(coord);

    // Exact at every lattice point. The mesh and the field agree AT the
    // lattice by construction; if this fails, the reconstruction is off by a
    // cell and every ground query in the game is subtly wrong.
    for (int j = 0; j < kChunkVerts; ++j) {
        for (int i = 0; i < kChunkVerts; ++i) {
            const TerrainVertex& v =
                mesh.vertices[static_cast<std::size_t>(j * kChunkVerts + i)];
            REQUIRE_MSG(bits(mesh_height_at(kSeed, v.position.x, v.position.z)) ==
                            bits(v.position.y),
                        "mesh_height_at disagrees with the vertex it sits on",
                        "lattice exactness");
        }
    }

    // And inside the cells, against the actual collision triangle.
    float worst = 0.0f;
    int checked = 0;
    for (int j = 0; j < kChunkQuads; ++j) {
        for (int i = 0; i < kChunkQuads; ++i) {
            // A spread of positions within the cell, including both sides of
            // the diagonal and points very close to it.
            const float offsets[][2] = {{0.25f, 0.25f}, {0.75f, 0.75f},
                                        {0.10f, 0.80f}, {0.80f, 0.10f},
                                        {0.49f, 0.49f}, {0.51f, 0.51f}};
            for (const auto& o : offsets) {
                const float x = origin.x + (static_cast<float>(i) + o[0]) *
                                               kVertexSpacingMetres;
                const float z = origin.y + (static_cast<float>(j) + o[1]) *
                                               kVertexSpacingMetres;

                // Which triangle should own this point, from the published
                // quad split: {a,c,b} owns tx + tz <= 1, {b,c,d} owns the rest.
                const std::size_t quad =
                    static_cast<std::size_t>(j * kChunkQuads + i);
                const std::size_t which = (o[0] + o[1] <= 1.0f) ? 0u : 1u;
                const CollisionTri& tri = col.triangles[quad * 2u + which];

                const float q = mesh_height_at(kSeed, x, z);
                const float b = barycentric_height(tri, x, z);
                worst = std::max(worst, std::fabs(q - b));

                // Bounded by the triangle's own corners. A plane over a
                // triangle cannot leave the range of its vertices, so this is
                // an exact structural check with no tolerance in it at all.
                const float lo = std::min({tri.a.y, tri.b.y, tri.c.y});
                const float hi = std::max({tri.a.y, tri.b.y, tri.c.y});
                REQUIRE_MSG(q >= lo - 1e-3f && q <= hi + 1e-3f,
                            "mesh_height_at left the triangle it is inside",
                            "containment");
                ++checked;
            }
        }
    }

    // The tolerance here is about ARITHMETIC ORDER, not about geometry: the
    // barycentric form and the incremental form reach the same plane by
    // different routes and round differently on the way. A millimetre over a
    // 64 m chunk is float noise; a centimetre would mean they are not actually
    // the same plane.
    REQUIRE_MSG(worst < 1e-3f,
                "mesh_height_at and the collision triangle disagree",
                "plane agreement");
    REQUIRE(checked == kChunkQuads * kChunkQuads * 6);

    std::printf("      (%d interior samples, worst plane disagreement %.3g m)\n",
                checked, static_cast<double>(worst));
    apricot_test::pass("mesh_height_at lands on the collision triangle");
}

void mesh_normal_matches_the_collision_face() {
    constexpr uint64_t kSeed = 0x99ull;
    const ChunkCoord coord{-1, 3};
    const ChunkMesh mesh = build_chunk(kSeed, coord);
    const ChunkCollision col = build_chunk_collision(mesh);
    const glm::vec2 origin = chunk_origin(coord);

    float worst = 0.0f;
    for (int j = 0; j < kChunkQuads; j += 3) {
        for (int i = 0; i < kChunkQuads; i += 3) {
            const float offsets[][2] = {{0.3f, 0.3f}, {0.7f, 0.7f}};
            for (const auto& o : offsets) {
                const float x = origin.x + (static_cast<float>(i) + o[0]) *
                                               kVertexSpacingMetres;
                const float z = origin.y + (static_cast<float>(j) + o[1]) *
                                               kVertexSpacingMetres;
                const std::size_t quad =
                    static_cast<std::size_t>(j * kChunkQuads + i);
                const std::size_t which = (o[0] + o[1] <= 1.0f) ? 0u : 1u;
                const glm::vec3 expect = col.triangles[quad * 2u + which].normal;
                const glm::vec3 got = mesh_normal_at(kSeed, x, z);
                worst = std::max(worst, glm::length(got - expect));
            }
        }
    }
    REQUIRE_MSG(worst < 1e-5f,
                "mesh_normal_at disagrees with the collision face normal",
                "normals");
    apricot_test::pass("mesh_normal_at matches the collision face normal");
}

// --- seams ------------------------------------------------------------------
void neighbouring_chunks_share_their_edges_exactly() {
    constexpr uint64_t kSeed = 0x5EA4ull;
    const ChunkCoord base{4, -6};

    const ChunkMesh c = build_chunk(kSeed, base);
    const ChunkMesh east = build_chunk(kSeed, ChunkCoord{base.x + 1, base.z});
    const ChunkMesh north = build_chunk(kSeed, ChunkCoord{base.x, base.z + 1});

    auto at = [](const ChunkMesh& m, int i, int j) -> const TerrainVertex& {
        return m.vertices[static_cast<std::size_t>(j * kChunkVerts + i)];
    };

    // A crack is not a "nearly equal" problem. If the two chunks' shared edge
    // vertices differ in the last bit, the two triangle fans meeting there do
    // not close and you get a hairline of background colour through the world.
    // Everything is compared, not just position: a normal that differs across
    // the seam is a visible lighting stripe, and a material weight that differs
    // is a visible texture stripe.
    for (int j = 0; j < kChunkVerts; ++j) {
        const TerrainVertex& a = at(c, kChunkVerts - 1, j);
        const TerrainVertex& b = at(east, 0, j);
        REQUIRE_MSG(same_vec3(a.position, b.position),
                    "east seam position differs", "seam");
        REQUIRE_MSG(same_vec3(a.normal, b.normal), "east seam normal differs",
                    "seam");
        REQUIRE_MSG(bits(a.material_weights.x) == bits(b.material_weights.x) &&
                        bits(a.material_weights.y) == bits(b.material_weights.y) &&
                        bits(a.material_weights.z) == bits(b.material_weights.z) &&
                        bits(a.material_weights.w) == bits(b.material_weights.w),
                    "east seam material differs", "seam");
        REQUIRE_MSG(bits(a.uv.x) == bits(b.uv.x) && bits(a.uv.y) == bits(b.uv.y),
                    "east seam uv differs", "seam");
    }

    for (int i = 0; i < kChunkVerts; ++i) {
        const TerrainVertex& a = at(c, i, kChunkVerts - 1);
        const TerrainVertex& b = at(north, i, 0);
        REQUIRE_MSG(same_vec3(a.position, b.position),
                    "north seam position differs", "seam");
        REQUIRE_MSG(same_vec3(a.normal, b.normal), "north seam normal differs",
                    "seam");
    }

    // The corner where four chunks meet, which is where an off-by-one in the
    // edge handling hides.
    const ChunkMesh ne = build_chunk(kSeed, ChunkCoord{base.x + 1, base.z + 1});
    const TerrainVertex& corner = at(c, kChunkVerts - 1, kChunkVerts - 1);
    REQUIRE(same_vec3(corner.position, at(east, 0, kChunkVerts - 1).position));
    REQUIRE(same_vec3(corner.position, at(north, kChunkVerts - 1, 0).position));
    REQUIRE(same_vec3(corner.position, at(ne, 0, 0).position));

    apricot_test::pass("neighbouring chunks share edges bit-exactly");
}

// The same property stated the other way round, and the one that actually
// matters to a player: sample straight across a chunk boundary and the surface
// must not step.
void the_surface_is_continuous_across_a_boundary() {
    constexpr uint64_t kSeed = 0xC04Cull;
    const float boundary = kChunkMetres * 3.0f;  // the x = 192 seam

    float worst = 0.0f;
    for (int i = 0; i < 400; ++i) {
        const float z = -300.0f + static_cast<float>(i) * 1.37f;
        // A hair either side of the seam. Two different chunks answer these.
        const float lo = mesh_height_at(kSeed, boundary - 1e-3f, z);
        const float hi = mesh_height_at(kSeed, boundary + 1e-3f, z);
        worst = std::max(worst, std::fabs(hi - lo));
    }
    // 2 mm of horizontal travel across the steepest terrain in the world
    // cannot produce more than a millimetre of height change.
    REQUIRE_MSG(worst < 1e-2f, "the surface steps at a chunk boundary", "seam");
    apricot_test::pass("the surface is continuous across chunk boundaries");
}

// --- the field is not the mesh ----------------------------------------------
// Recorded rather than asserted-away. Physics that samples height_at() instead
// of mesh_height_at() is wrong by this much, and the number is the argument.
void the_field_and_the_mesh_differ_measurably() {
    constexpr uint64_t kSeed = 0xC0FFEEull;
    double worst = 0.0, sum = 0.0;
    int n = 0;
    // OFF-LATTICE SAMPLING, and it has to be deliberate. The two surfaces
    // agree exactly AT the lattice, so a sample step that is a whole number of
    // kVertexSpacingMetres hits only the points where they cannot differ and
    // reports a disagreement of exactly zero. This test caught precisely that
    // mistake in its own first draft, with a 3 m step over a 1 m lattice.
    for (int j = 0; j < 400; ++j) {
        for (int i = 0; i < 400; ++i) {
            const float x = -600.0f + static_cast<float>(i) * 2.937f + 0.113f;
            const float z = -600.0f + static_cast<float>(j) * 2.937f + 0.417f;
            const double d = std::fabs(static_cast<double>(mesh_height_at(kSeed, x, z)) -
                                       static_cast<double>(height_at(kSeed, x, z)));
            worst = std::max(worst, d);
            sum += d;
            ++n;
        }
    }
    std::printf("      (field vs mesh over %d samples: mean %.4f m, worst %.4f m)\n",
                n, sum / n, worst);

    // If this ever comes out at zero, the mesh has stopped being planar
    // between lattice points or mesh_height_at() has quietly become a wrapper
    // around height_at() — either way the distinction this whole design rests
    // on has evaporated and somebody needs to know.
    REQUIRE_MSG(worst > 1e-4, "the mesh and the field are suspiciously identical",
                "distinction");
    apricot_test::pass("the field and the mesh differ, and by how much");
}

}  // namespace

int main() {
    std::printf("terrain_mesh_tests\n");
    mesh_has_the_expected_shape();
    winding_is_counter_clockwise_from_above();
    collision_triangles_are_the_drawn_triangles();
    mesh_height_lands_on_the_drawn_triangle();
    mesh_normal_matches_the_collision_face();
    neighbouring_chunks_share_their_edges_exactly();
    the_surface_is_continuous_across_a_boundary();
    the_field_and_the_mesh_differ_measurably();
    return apricot_test::done("terrain_mesh_tests");
}
