// Terrain level of detail: the coarse lattice, and the seam it reopens.
//
// docs/architecture.md states the seam rule and states it absolutely: never
// "fix" a seam by averaging across chunks, because chunk meshes are built at
// absolute world coordinates and neighbours therefore evaluate height_at() at
// the identical coordinate and close exactly. LOD is the first thing in this
// engine that can break that, because two levels do not share every coordinate
// along their common edge.
//
// So this suite exists to hold the line in two directions at once:
//
//   1. Where two chunks DO share a coordinate — same level, or a coarse
//      vertex that also exists on the fine side — the agreement is still BIT
//      IDENTICAL. Not close. Identical. The moment that becomes approximate,
//      something has started filtering, and a filter is the averaging the rule
//      forbids wearing a different hat.
//
//   2. Where they do not share a coordinate, the gap is real, and the skirt is
//      measured against it directly: the higher chunk's curtain must reach at
//      least as far down as the lower chunk's edge, at every point along every
//      boundary, over real terrain. That is the actual guarantee. A test that
//      merely asserted "a skirt exists" would pass on a skirt of one
//      millimetre.
//
// Everything below runs the real build_chunk() over the real height field.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "city/map.h"
#include "terrain/chunk.h"
#include "terrain/heightmap.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// THE MAP SEED, not an arbitrary one. This is the world that ships, and after
// PENG-41 the height field is a function of the authored terrain operators as
// well as the noise — so a seam suite on some other seed is measuring a
// landscape nobody will ever drive on.
constexpr uint64_t kSeed = city::kMapSeed;

// Chunks chosen to sit on the STEEPEST ground the map has, not on convenient
// ground.
//
// This list was generic at first — a scatter of coordinates over the noise —
// and when the map landed underneath it, every probe came out on a district
// plate or a gentle slope. The suite still passed, with every skirt sitting on
// its 0.50 m floor, which is the signature of a test that has stopped
// testing anything: the cracks it measured were small because the terrain it
// measured was flat.
//
// A Carve is a near-vertical wall and it is the case that can actually defeat a
// skirt, because a level 3 lattice samples every 8 m and can step across a
// feature the level 0 lattice resolves in full. So the probes now sit on them
// deliberately. Coordinates are src/city/terrain_ops.h centres divided by
// kChunkMetres.
const ChunkCoord kProbeChunks[] = {
    {-24, 21},   // Marrow's quarry pit: 60 m into the side of a 120 m hill,
                 // 180 m radius. The steepest authored feature on the map.
    {-23, 20},   // its rim, where the wall meets undisturbed ground
    {-25, -10},  // the Ostend harbour basin, carved for ship draught
    {13, -23},   // Ferrone Hill: a Bench, so a stack of terrace risers
    {19, 11},    // the stadium berm in Nickel Heights, a Mound
    {2, 33},     // Camber Point, near the channel that severs the peninsula
    {0, 0},      // and the spawn point, which had better also work
    {-11, -9}, {14, 3},  // two off-plate noise chunks, for the ordinary case
};

// Vertices of a chunk edge, in the mesh's own row-major order.
enum class Edge { MinX, MaxX, MinZ, MaxZ };

std::vector<glm::vec3> edge_vertices(const ChunkMesh& m, Edge e) {
    const int v = lod_verts(m.lod);
    std::vector<glm::vec3> out;
    out.reserve(static_cast<std::size_t>(v));
    for (int k = 0; k < v; ++k) {
        int i = 0, j = 0;
        switch (e) {
            case Edge::MinX: i = 0;         j = k; break;
            case Edge::MaxX: i = v - 1;     j = k; break;
            case Edge::MinZ: i = k;         j = 0; break;
            case Edge::MaxZ: i = k;         j = v - 1; break;
        }
        out.push_back(m.vertices[static_cast<std::size_t>(j * v + i)].position);
    }
    return out;
}

// The DRAWN height along a chunk edge at parameter t in [0, 1].
//
// Along a boundary the mesh is exactly the straight segment between two
// consecutive edge vertices — the quad diagonal never crosses the boundary —
// so this is the surface a player standing at the seam actually sees, not an
// approximation of it.
float drawn_edge_height(const std::vector<glm::vec3>& edge, float t) {
    const int segments = static_cast<int>(edge.size()) - 1;
    const float scaled = t * static_cast<float>(segments);
    int seg = static_cast<int>(std::floor(scaled));
    if (seg < 0) seg = 0;
    if (seg > segments - 1) seg = segments - 1;
    const float local = scaled - static_cast<float>(seg);
    const float a = edge[static_cast<std::size_t>(seg)].y;
    const float b = edge[static_cast<std::size_t>(seg) + 1u].y;
    return a + (b - a) * local;
}

// Face normal of triangle `t` of a mesh.
glm::vec3 face_normal(const ChunkMesh& m, std::size_t t) {
    const glm::vec3 a = m.vertices[m.indices[t * 3u + 0u]].position;
    const glm::vec3 b = m.vertices[m.indices[t * 3u + 1u]].position;
    const glm::vec3 c = m.vertices[m.indices[t * 3u + 2u]].position;
    const glm::vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    return len > 0.0f ? n / len : glm::vec3{0.0f};
}

glm::vec3 triangle_centroid(const ChunkMesh& m, std::size_t t) {
    const glm::vec3 a = m.vertices[m.indices[t * 3u + 0u]].position;
    const glm::vec3 b = m.vertices[m.indices[t * 3u + 1u]].position;
    const glm::vec3 c = m.vertices[m.indices[t * 3u + 2u]].position;
    return (a + b + c) / 3.0f;
}

// --- 1. the coarse lattice is a SUBSET of the fine one -----------------------

void a_coarse_chunk_samples_the_same_lattice_bit_for_bit() {
    for (const ChunkCoord c : kProbeChunks) {
        const ChunkMesh fine = build_chunk(kSeed, c, 0);
        const int fine_v = lod_verts(0);

        for (int lod = 1; lod <= kMaxChunkLod; ++lod) {
            const ChunkMesh coarse = build_chunk(kSeed, c, lod);
            const int v = lod_verts(lod);
            const int stride = lod_step(lod);

            for (int j = 0; j < v; ++j) {
                for (int i = 0; i < v; ++i) {
                    const glm::vec3 got =
                        coarse.vertices[static_cast<std::size_t>(j * v + i)]
                            .position;
                    const glm::vec3 want =
                        fine.vertices[static_cast<std::size_t>(
                                          (j * stride) * fine_v + i * stride)]
                            .position;

                    // Bit identity, deliberately not REQUIRE_NEAR. If a coarse
                    // vertex is merely CLOSE to the fine one at the same
                    // coordinate, the mesher has stopped being a pure sample of
                    // height_at() and started computing something of its own.
                    REQUIRE_MSG(got.x == want.x && got.y == want.y &&
                                    got.z == want.z,
                                "a coarse vertex is not the fine vertex at the "
                                "same world coordinate",
                                "lod subset");
                }
            }
        }
    }
}

// --- 2. same level: the original guarantee is untouched ----------------------

void neighbours_at_the_same_level_still_close_exactly() {
    for (int lod = 0; lod <= kMaxChunkLod; ++lod) {
        for (const ChunkCoord c : kProbeChunks) {
            const ChunkCoord east{c.x + 1, c.z};
            const ChunkMesh a = build_chunk(kSeed, c, lod);
            const ChunkMesh b = build_chunk(kSeed, east, lod);

            const std::vector<glm::vec3> a_edge = edge_vertices(a, Edge::MaxX);
            const std::vector<glm::vec3> b_edge = edge_vertices(b, Edge::MinX);
            REQUIRE_MSG(a_edge.size() == b_edge.size(),
                        "shared edges have different vertex counts",
                        "same-level seam");

            for (std::size_t k = 0; k < a_edge.size(); ++k) {
                REQUIRE_MSG(a_edge[k].x == b_edge[k].x &&
                                a_edge[k].y == b_edge[k].y &&
                                a_edge[k].z == b_edge[k].z,
                            "a shared edge vertex is not bit-identical between "
                            "neighbours at the same level",
                            "same-level seam");
            }
        }
    }
}

// --- 3. the skirt hangs the right way ----------------------------------------

void every_skirt_face_points_out_of_the_chunk() {
    for (int lod = 0; lod <= kMaxChunkLod; ++lod) {
        const ChunkMesh m = build_chunk(kSeed, kProbeChunks[0], lod);

        const std::size_t first = m.surface_index_count / 3u;
        const std::size_t total = m.indices.size() / 3u;
        REQUIRE_MSG(total > first, "the mesh has no skirt triangles at all",
                    "skirt winding");

        const glm::vec2 origin = chunk_origin(m.coord);
        const glm::vec2 centre{origin.x + kChunkMetres * 0.5f,
                               origin.y + kChunkMetres * 0.5f};

        for (std::size_t t = first; t < total; ++t) {
            const glm::vec3 n = face_normal(m, t);
            const glm::vec3 p = triangle_centroid(m, t);

            // Vertical curtain: the face must be side-on, not a lid.
            REQUIRE_MSG(std::fabs(n.y) < 1e-3f,
                        "a skirt face is not vertical", "skirt winding");

            // ...and it must face AWAY from the chunk. A skirt wound inward is
            // culled away by GL_CULL_FACE and is therefore not a skirt at all,
            // while looking in every buffer dump exactly like one.
            const glm::vec2 outward{p.x - centre.x, p.z - centre.y};
            const float dot = n.x * outward.x + n.z * outward.y;
            REQUIRE_MSG(dot > 0.0f,
                        "a skirt face points into the chunk; under back-face "
                        "culling it draws nothing",
                        "skirt winding");
        }
    }
}

// --- 4. THE ONE THAT MATTERS: the skirt covers the LOD crack -----------------

void the_skirt_covers_every_crack_at_a_level_boundary() {
    // Every level pairing that a ring boundary can produce, including the 3:1
    // jump a teleport can briefly leave behind before the rings settle.
    struct Pair {
        int fine;
        int coarse;
    };
    const Pair pairs[] = {{0, 1}, {1, 2}, {2, 3}, {0, 2}, {1, 3}, {0, 3}};

    // Sampled far more densely than either lattice, so the test looks between
    // the vertices — which is the only place a crack can be.
    constexpr int kSamples = 512;

    float worst_gap = 0.0f;
    float worst_margin = 1e30f;

    for (const Pair p : pairs) {
        // Per pairing, because the aggregate hides which transition is tight.
        // The streamer's concentric rings only ever produce a ONE level step
        // between neighbours; the wider pairings are here as a stress case, and
        // knowing which line is thin is the difference between "the margin is
        // fine" and "the margin is fine for anything that can actually happen".
        float pair_gap = 0.0f;
        float pair_margin = 1e30f;

        for (const ChunkCoord c : kProbeChunks) {
            const ChunkCoord east{c.x + 1, c.z};

            const ChunkMesh a = build_chunk(kSeed, c, p.fine);
            const ChunkMesh b = build_chunk(kSeed, east, p.coarse);

            const std::vector<glm::vec3> a_edge = edge_vertices(a, Edge::MaxX);
            const std::vector<glm::vec3> b_edge = edge_vertices(b, Edge::MinX);

            for (int s = 0; s <= kSamples; ++s) {
                const float t = static_cast<float>(s) /
                                static_cast<float>(kSamples);
                const float ha = drawn_edge_height(a_edge, t);
                const float hb = drawn_edge_height(b_edge, t);

                // Whichever side is HIGHER at this point is the side whose
                // curtain has to reach down to the other. The lower chunk's
                // skirt hangs below the gap and does nothing for it, which is
                // why this is not a max() over the two depths.
                const bool fine_is_higher = ha > hb;
                const float gap = fine_is_higher ? ha - hb : hb - ha;
                const float depth =
                    fine_is_higher ? a.skirt_depth : b.skirt_depth;

                REQUIRE_MSG(gap <= depth,
                            "a chunk's skirt is shorter than the crack it has "
                            "to cover at a level boundary",
                            "lod seam");

                pair_gap = std::max(pair_gap, gap);
                pair_margin = std::min(pair_margin, depth - gap);
            }
        }

        std::printf("    [lod seam] lod %d|%d  worst crack %6.3f m  margin %6.3f m\n",
                    p.fine, p.coarse, static_cast<double>(pair_gap),
                    static_cast<double>(pair_margin));
        worst_gap = std::max(worst_gap, pair_gap);
        worst_margin = std::min(worst_margin, pair_margin);
    }

    // A margin that has quietly collapsed to nothing is a test about to start
    // failing on someone else's terrain tuning, so say the number out loud
    // rather than only asserting the sign of it.
    REQUIRE_MSG(worst_margin > 0.0f,
                "some boundary is covered with exactly zero margin",
                "lod seam");
    std::printf("    [lod seam] worst crack %.3f m, thinnest skirt margin %.3f m\n",
                static_cast<double>(worst_gap),
                static_cast<double>(worst_margin));
}

// --- 5. collision never sees a coarse mesh or a vertical face ----------------

void collision_refuses_a_coarse_mesh() {
    for (int lod = 1; lod <= kMaxChunkLod; ++lod) {
        const ChunkMesh m = build_chunk(kSeed, kProbeChunks[0], lod);
        const ChunkCollision col = build_chunk_collision(m);
        REQUIRE_MSG(col.triangles.empty(),
                    "collision was derived from a coarsened mesh, which is the "
                    "downsampled copy the engine forbids",
                    "collision lod");
    }
}

void collision_at_level_zero_holds_only_ground_triangles() {
    const ChunkMesh m = build_chunk(kSeed, kProbeChunks[0], 0);
    const ChunkCollision col = build_chunk_collision(m);

    const std::size_t ground_tris =
        static_cast<std::size_t>(kChunkQuads) *
        static_cast<std::size_t>(kChunkQuads) * 2u;
    REQUIRE_MSG(col.triangles.size() == ground_tris,
                "collision triangle count no longer matches the ground quads; "
                "a skirt face has leaked in",
                "collision lod");

    for (const CollisionTri& t : col.triangles) {
        // The skirt is vertical. If one ever reaches collision it arrives with
        // normal.y == 0 before the +Y flip and a hair above it after, so this
        // bound catches it while leaving room for the steepest real ground.
        REQUIRE_MSG(t.normal.y > 0.01f,
                    "a near-vertical triangle is in the collision set",
                    "collision lod");
    }
}

// --- 6. the cost table the LOD choice was made from -------------------------

void the_level_costs_are_what_the_tiers_were_budgeted_against() {
    const int want_verts[] = {65, 33, 17, 9};

    for (int lod = 0; lod <= kMaxChunkLod; ++lod) {
        REQUIRE_MSG(lod_verts(lod) == want_verts[lod],
                    "a level's vertex count changed; the ring budget in "
                    "streamer.h was sized against these",
                    "lod cost");

        const ChunkMesh m = build_chunk(kSeed, kProbeChunks[0], lod);
        const std::size_t grid =
            static_cast<std::size_t>(want_verts[lod] * want_verts[lod]);
        const std::size_t skirt =
            static_cast<std::size_t>(4 * (want_verts[lod] - 1));
        REQUIRE_MSG(m.vertices.size() == grid + skirt,
                    "vertex count is not grid + skirt ring", "lod cost");
        REQUIRE_MSG(m.lod == lod, "the mesh did not record its own level",
                    "lod cost");

        std::printf("    [lod cost] lod %d: %5zu verts, %6zu indices, %7zu B, "
                    "skirt %.2f m\n",
                    lod, m.vertices.size(), m.indices.size(), m.gpu_bytes(),
                    static_cast<double>(m.skirt_depth));
    }
}

// --- 7. how far the drawn ground moves per level ----------------------------

void the_drawn_surface_moves_by_this_much_per_level() {
    // THE NUMBER ANYTHING DRAPED ON THE TERRAIN NEEDS.
    //
    // A road ribbon is baked onto the DRAWN surface via mesh_height_at(), which
    // is level 0. If the terrain under it is drawn at level 2, the ribbon and
    // the ground it was draped on are no longer the same surface, and the road
    // floats or sinks by exactly the difference measured here. The same applies
    // to any prop, decal or footprint placed on the ground.
    //
    // It is the same rule the engine already has -- what you touch is what
    // draws -- with a second consumer, so it gets a measured bound rather than
    // an assurance. There is no assertion on the magnitude on purpose: this is
    // a measurement, and the right response to it is a draw distance, not a
    // threshold somebody tunes until the test goes quiet.
    constexpr int kSamples = 400;
    const float span = kChunkMetres * 6.0f;

    // CENTRED ON THE QUARRY, NOT ON THE ORIGIN, and that is not a detail.
    //
    // Sampling a square around (0, 0) reports mean 0.000 m and worst 0.002 m,
    // and every one of those digits is honest and useless: the origin sits on
    // Vellum Row's flattened district plate, where a coarse lattice and a fine
    // one obviously agree because there is nothing between the samples. A
    // drape budget derived from flat ground would then be applied to a
    // hillside.
    //
    // Marrow's quarry is 60 m cut into the side of a 120 m hill, which is the
    // steepest thing anybody can drape a road across.
    const glm::vec2 centre{-1480.0f, 1300.0f};

    std::printf("    [drape] drawn-surface disagreement against level 0, "
                "over the quarry at (%.0f, %.0f):\n",
                static_cast<double>(centre.x), static_cast<double>(centre.y));
    for (int lod = 1; lod <= kMaxChunkLod; ++lod) {
        float worst = 0.0f;
        double total = 0.0;
        int n = 0;
        for (int j = 0; j < kSamples; ++j) {
            for (int i = 0; i < kSamples; ++i) {
                const float x = centre.x - span * 0.5f +
                                span * static_cast<float>(i) /
                                    static_cast<float>(kSamples);
                const float z = centre.y - span * 0.5f +
                                span * static_cast<float>(j) /
                                    static_cast<float>(kSamples);
                const float d = std::fabs(mesh_height_at_lod(kSeed, x, z, lod) -
                                          mesh_height_at(kSeed, x, z));
                worst = std::max(worst, d);
                total += static_cast<double>(d);
                ++n;
            }
        }
        std::printf("      lod %d (%.0f m spacing): mean %.3f m, worst %.3f m\n",
                    lod, static_cast<double>(lod_spacing_metres(lod)),
                    total / n, static_cast<double>(worst));
    }

    // Level 0 against itself must be exactly nothing. If this ever drifts, the
    // coarse path and the level 0 path have stopped being the same function and
    // every number above is measuring the wrong thing.
    for (int k = 0; k < 500; ++k) {
        const float x = centre.x + static_cast<float>(k) * 1.7f - 400.0f;
        const float z = centre.y + static_cast<float>(k) * -2.3f + 250.0f;
        REQUIRE_MSG(mesh_height_at_lod(kSeed, x, z, 0) ==
                        mesh_height_at(kSeed, x, z),
                    "mesh_height_at_lod at level 0 is not mesh_height_at",
                    "drape");
    }
}

void an_out_of_range_level_is_clamped_not_undefined() {
    const ChunkMesh low = build_chunk(kSeed, ChunkCoord{1, 1}, -4);
    REQUIRE_MSG(low.lod == 0, "a negative level was not clamped to 0",
                "lod clamp");
    const ChunkMesh high = build_chunk(kSeed, ChunkCoord{1, 1}, 99);
    REQUIRE_MSG(high.lod == kMaxChunkLod,
                "an over-range level was not clamped to kMaxChunkLod",
                "lod clamp");
}

}  // namespace

int main() {
    std::printf("terrain_lod_tests\n");
    a_coarse_chunk_samples_the_same_lattice_bit_for_bit();
    apricot_test::pass("a coarse chunk samples the same lattice, bit for bit");
    neighbours_at_the_same_level_still_close_exactly();
    apricot_test::pass("neighbours at the same level still close exactly");
    every_skirt_face_points_out_of_the_chunk();
    apricot_test::pass("every skirt face points out of the chunk");
    the_skirt_covers_every_crack_at_a_level_boundary();
    apricot_test::pass("the skirt covers every crack at a level boundary");
    collision_refuses_a_coarse_mesh();
    apricot_test::pass("collision refuses a coarse mesh");
    collision_at_level_zero_holds_only_ground_triangles();
    apricot_test::pass("collision at level zero holds only ground triangles");
    the_level_costs_are_what_the_tiers_were_budgeted_against();
    apricot_test::pass("the level costs are what the tiers were budgeted against");
    the_drawn_surface_moves_by_this_much_per_level();
    apricot_test::pass("the drawn surface moves by a measured amount per level");
    an_out_of_range_level_is_clamped_not_undefined();
    apricot_test::pass("an out of range level is clamped, not undefined");
    return apricot_test::done("terrain_lod_tests");
}
