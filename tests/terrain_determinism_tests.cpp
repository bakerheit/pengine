// Terrain determinism.
//
// The world is a pure function of one 64-bit seed. If this suite fails, that
// stopped being true: a save file no longer names the world it was recorded
// in, a replay desyncs, a chunk generates differently depending on which
// direction the player drove in from, and "it looks different on my machine"
// becomes a legitimate bug report nobody can act on.
//
// Every comparison here is BIT-EQUALITY, not approximate equality. That is the
// point. A height that agrees to six decimal places is a height that disagrees,
// and the disagreement compounds: a wheel contact a millimetre out becomes a
// landing a metre out becomes a different race. REQUIRE_NEAR has no place in
// this file and there should never be one here.
//
// Treat a failure as a stop-the-line event, not as a test to update. If a
// generator change is deliberate, the pins below must be regenerated AND every
// existing replay and save seed is invalidated. This suite exists to make sure
// that cost is paid on purpose.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "terrain/chunk.h"
#include "terrain/heightmap.h"
#include "terrain/scatter.h"
#include "terrain/surface.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// Exact float comparison via its bit pattern. Spelled this way rather than
// with `==` so that the intent is unmistakable to the next reader, and so a
// NaN (which compares unequal to itself) is caught rather than silently
// passing a `!=` check.
uint32_t bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

void require_normal(uint64_t seed, float x, float z, uint32_t bx, uint32_t by,
                    uint32_t bz) {
    const glm::vec3 n = normal_at(seed, x, z);
    REQUIRE_MSG(bits(n.x) == bx, "normal.x drifted", "golden normal");
    REQUIRE_MSG(bits(n.y) == by, "normal.y drifted", "golden normal");
    REQUIRE_MSG(bits(n.z) == bz, "normal.z drifted", "golden normal");
}

void require_weights(uint64_t seed, float x, float z, uint32_t br, uint32_t bg,
                     uint32_t bgr, uint32_t bs) {
    const SurfaceSample s = surface_at(seed, x, z);
    REQUIRE_MSG(bits(s.weights.x) == br, "rock weight drifted", "golden weights");
    REQUIRE_MSG(bits(s.weights.y) == bg, "gravel weight drifted", "golden weights");
    REQUIRE_MSG(bits(s.weights.z) == bgr, "grass weight drifted", "golden weights");
    REQUIRE_MSG(bits(s.weights.w) == bs, "sand weight drifted", "golden weights");
}

void require_first_prop(uint64_t seed, ChunkCoord c, int kind, int variant,
                        uint32_t px, uint32_t py, uint32_t pz, uint32_t yaw,
                        uint32_t scale) {
    const std::vector<ScatterProp> v = scatter_chunk(seed, c);
    REQUIRE_MSG(!v.empty(), "chunk produced no props", "golden prop");
    const ScatterProp& p = v.front();
    REQUIRE_MSG(static_cast<int>(p.kind) == kind, "prop kind drifted", "golden prop");
    REQUIRE_MSG(static_cast<int>(p.variant) == variant, "prop variant drifted",
                "golden prop");
    REQUIRE_MSG(bits(p.position.x) == px, "prop x drifted", "golden prop");
    REQUIRE_MSG(bits(p.position.y) == py, "prop y drifted", "golden prop");
    REQUIRE_MSG(bits(p.position.z) == pz, "prop z drifted", "golden prop");
    REQUIRE_MSG(bits(p.yaw) == yaw, "prop yaw drifted", "golden prop");
    REQUIRE_MSG(bits(p.scale) == scale, "prop scale drifted", "golden prop");
}

// --- pinned values -----------------------------------------------------------
// GOLDEN VALUES, as exact IEEE bit patterns. These are the outputs of the
// current generator, pinned so that ANY change to the noise, the octave counts,
// the mix weights, the island falloff or the material thresholds is caught
// HERE rather than discovered as a silently different world three tickets
// later.
void golden_values() {
    REQUIRE(bits(height_at(0ull, 0.0000f, 0.0000f)) == 0x4274EC79u);
    REQUIRE(bits(height_at(1ull, 100.0000f, -250.0000f)) == 0x4258A6A4u);
    REQUIRE(bits(height_at(3735928559ull, -640.0000f, 320.5000f)) == 0x41A20C96u);
    REQUIRE(bits(height_at(12648430ull, 37.2500f, 991.7500f)) == 0xC13BB95Du);

    require_normal(0ull, 0.0000f, 0.0000f, 0x3B689F3Eu, 0x3F7FFF2Au, 0x3B6B7F3Bu);
    require_normal(1ull, 100.0000f, -250.0000f, 0x3F0F6044u, 0x3F456E3Fu, 0xBE9AECCFu);
    require_normal(3735928559ull, -640.0000f, 320.5000f, 0x3DCFA80Fu, 0x3F72A62Cu, 0xBE9AB53Du);
    require_normal(12648430ull, 37.2500f, 991.7500f, 0xBB942448u, 0x3F7C75DAu, 0x3E29A186u);

    require_weights(0ull, 0.0000f, 0.0000f, 0x00000000u, 0x3EB79230u, 0x3F2436E8u, 0x00000000u);
    require_weights(1ull, 100.0000f, -250.0000f, 0x3ED5397Au, 0x3F156343u, 0x00000000u, 0x00000000u);
    require_weights(3735928559ull, -640.0000f, 320.5000f, 0x00000000u, 0x3B69C3FCu, 0x3F7F163Cu, 0x00000000u);
    require_weights(12648430ull, 37.2500f, 991.7500f, 0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u);

    REQUIRE(scatter_chunk(0ull, ChunkCoord{0, 0}).size() == 99u);
    require_first_prop(0ull, ChunkCoord{0, 0}, 0, 0, 0x4118BEB0u, 0x42747EDEu,
                       0x40052770u, 0x40C79FEDu, 0x3F8F357Cu);
    REQUIRE(scatter_chunk(1ull, ChunkCoord{3, -2}).size() == 152u);
    require_first_prop(1ull, ChunkCoord{3, -2}, 1, 0, 0x434101E9u, 0x42465568u,
                       0xC2FEFB48u, 0x4016C910u, 0x3F9FD0F8u);
    REQUIRE(scatter_chunk(12648430ull, ChunkCoord{-5, 7}).size() == 156u);
    require_first_prop(12648430ull, ChunkCoord{-5, 7}, 0, 2, 0xC39E5830u,
                       0x423C0749u, 0x43E16D8Au, 0x3F39F3A6u, 0x3FADCF45u);

    apricot_test::pass("golden terrain values unchanged");
}

// --- the headline: generate twice, compare every bit -------------------------
// THE ticket requirement. Two independent generations from one seed must agree
// bit for bit across heights, normals, materials and scatter.
void generated_twice_is_bit_identical() {
    constexpr uint64_t kSeed = 0x5EEDull;

    const ChunkCoord coords[] = {{0, 0}, {5, -3}, {-11, 8}, {31, 31}};
    for (const ChunkCoord c : coords) {
        const ChunkMesh a = build_chunk(kSeed, c);
        const ChunkMesh b = build_chunk(kSeed, c);

        REQUIRE_MSG(a.vertices.size() == b.vertices.size(),
                    "vertex count differed between generations", "chunk");
        REQUIRE_MSG(a.indices.size() == b.indices.size(),
                    "index count differed between generations", "chunk");

        // A single memcmp over the whole vertex array covers position, normal,
        // uv and material weights at once, and covers any field added later
        // without anyone remembering to extend this test. TerrainVertex is
        // trivially copyable and fully initialised by the mesher, so there is
        // no padding hazard here — but that is exactly why the per-field loop
        // below also exists: if padding ever does appear, the memcmp fails
        // first and the loop tells you it was padding rather than data.
        REQUIRE_MSG(std::memcmp(a.vertices.data(), b.vertices.data(),
                                a.vertices.size() * sizeof(TerrainVertex)) == 0,
                    "vertex bytes differed between generations", "chunk");

        for (std::size_t i = 0; i < a.vertices.size(); ++i) {
            const TerrainVertex& va = a.vertices[i];
            const TerrainVertex& vb = b.vertices[i];
            REQUIRE_MSG(bits(va.position.y) == bits(vb.position.y),
                        "height differed between generations", "height");
            REQUIRE_MSG(bits(va.normal.x) == bits(vb.normal.x) &&
                            bits(va.normal.y) == bits(vb.normal.y) &&
                            bits(va.normal.z) == bits(vb.normal.z),
                        "normal differed between generations", "normal");
            REQUIRE_MSG(bits(va.material_weights.x) == bits(vb.material_weights.x) &&
                            bits(va.material_weights.y) == bits(vb.material_weights.y) &&
                            bits(va.material_weights.z) == bits(vb.material_weights.z) &&
                            bits(va.material_weights.w) == bits(vb.material_weights.w),
                        "material weights differed between generations", "material");
        }

        REQUIRE_MSG(a.indices == b.indices, "index buffer differed", "chunk");

        const std::vector<ScatterProp> pa = scatter_chunk(kSeed, c);
        const std::vector<ScatterProp> pb = scatter_chunk(kSeed, c);
        REQUIRE_MSG(pa.size() == pb.size(), "prop count differed", "scatter");
        for (std::size_t i = 0; i < pa.size(); ++i) {
            REQUIRE_MSG(pa[i].kind == pb[i].kind, "prop kind differed", "scatter");
            REQUIRE_MSG(pa[i].variant == pb[i].variant, "prop variant differed",
                        "scatter");
            REQUIRE_MSG(pa[i].ground == pb[i].ground, "prop ground differed",
                        "scatter");
            REQUIRE_MSG(bits(pa[i].position.x) == bits(pb[i].position.x) &&
                            bits(pa[i].position.y) == bits(pb[i].position.y) &&
                            bits(pa[i].position.z) == bits(pb[i].position.z),
                        "prop position differed", "scatter");
            REQUIRE_MSG(bits(pa[i].yaw) == bits(pb[i].yaw), "prop yaw differed",
                        "scatter");
            REQUIRE_MSG(bits(pa[i].scale) == bits(pb[i].scale),
                        "prop scale differed", "scatter");
        }
    }
    apricot_test::pass("two generations from one seed are bit-identical");
}

// Generation order must not matter. This is the property streaming depends on:
// a chunk approached from the north generates identically to one approached
// from the south, and a chunk regenerated after eviction comes back the same.
void generation_is_order_independent() {
    constexpr uint64_t kSeed = 0xA11CEull;

    std::vector<uint32_t> forward;
    for (int32_t z = -2; z <= 2; ++z) {
        for (int32_t x = -2; x <= 2; ++x) {
            const ChunkMesh m = build_chunk(kSeed, ChunkCoord{x, z});
            for (const TerrainVertex& v : m.vertices) {
                forward.push_back(bits(v.position.y));
            }
        }
    }

    // Walk the same grid backwards and interleave an unrelated chunk between
    // every step, so any hidden cache or accumulated state would show up.
    std::size_t idx = forward.size();
    for (int32_t z = 2; z >= -2; --z) {
        for (int32_t x = 2; x >= -2; --x) {
            (void)build_chunk(kSeed, ChunkCoord{999, 999});
            const ChunkMesh m = build_chunk(kSeed, ChunkCoord{x, z});
            idx -= m.vertices.size();
            for (std::size_t i = 0; i < m.vertices.size(); ++i) {
                REQUIRE_MSG(bits(m.vertices[i].position.y) == forward[idx + i],
                            "reverse traversal produced a different height",
                            "order independence");
            }
        }
    }
    REQUIRE(idx == 0u);
    apricot_test::pass("generation order does not affect the world");
}

// A single seed bit must change the world, or two players on "different" seeds
// are racing the same island.
void seeds_are_separated() {
    int differing = 0;
    for (int32_t z = -3; z <= 3; ++z) {
        for (int32_t x = -3; x <= 3; ++x) {
            const float wx = static_cast<float>(x) * 137.0f;
            const float wz = static_cast<float>(z) * 137.0f;
            if (bits(height_at(1ull, wx, wz)) != bits(height_at(2ull, wx, wz))) {
                ++differing;
            }
        }
    }
    REQUIRE_MSG(differing == 49, "two seeds agreed somewhere", "seed separation");

    // And one flipped bit of the seed, not just a different number.
    REQUIRE(bits(height_at(0x1000000000000000ull, 12.0f, 34.0f)) !=
            bits(height_at(0x1000000000000001ull, 12.0f, 34.0f)));
    apricot_test::pass("seeds produce separated worlds");
}

// --- shape invariants ---------------------------------------------------------
void heights_stay_inside_their_analytic_bounds() {
    constexpr uint64_t kSeed = 0xB0A7ull;
    // Well past the island, so the sea floor is included.
    for (int j = 0; j <= 160; ++j) {
        for (int i = 0; i <= 160; ++i) {
            const float x = (static_cast<float>(i) / 80.0f - 1.0f) * 2200.0f;
            const float z = (static_cast<float>(j) / 80.0f - 1.0f) * 2200.0f;
            const float h = height_at(kSeed, x, z);
            REQUIRE_MSG(h >= kMinHeightMetres, "height below analytic minimum",
                        "bounds");
            REQUIRE_MSG(h <= kMaxHeightMetres, "height above analytic maximum",
                        "bounds");
            REQUIRE_MSG(h == h, "height was NaN", "bounds");
        }
    }
    apricot_test::pass("heights respect the analytic bounds");
}

// It has to be an ISLAND: land in the middle, water all the way around, and
// the boundary is the sea rather than a wall the player bounces off.
void the_world_is_an_island() {
    const uint64_t seeds[] = {1ull, 0xC0FFEEull, 0xDEADBEEFull};
    for (const uint64_t seed : seeds) {
        int land_at_centre = 0;
        for (int a = 0; a < 32; ++a) {
            const float t = static_cast<float>(a) * 0.19634954f;  // 2pi/32
            const float r = 250.0f;
            if (height_at(seed, r * std::cos(t), r * std::sin(t)) >
                kSeaLevelMetres) {
                ++land_at_centre;
            }
        }
        REQUIRE_MSG(land_at_centre >= 24,
                    "the middle of the island is mostly water", "island");

        // Every bearing must be open water well beyond the island radius. If
        // even one is not, the falloff has a hole in it and the playable area
        // is not actually bounded.
        for (int a = 0; a < 64; ++a) {
            const float t = static_cast<float>(a) * 0.09817477f;  // 2pi/64
            const float r = kIslandRadiusMetres * 1.15f;
            const float h = height_at(seed, r * std::cos(t), r * std::sin(t));
            REQUIRE_MSG(h < kSeaLevelMetres, "land found beyond the island radius",
                        "island");
        }
    }
    apricot_test::pass("the world is an island bounded by water");
}

// The car spawns at the world origin. Every seed must therefore put dry,
// drivable land there — not "most seeds". This is checked across a wide spread
// of seeds because the failure it guards against is exactly the one nobody
// finds in testing: seed 1 works, seed 2 works, and the seed some player rolls
// six months from now drops them into a lagoon.
void every_seed_spawns_on_dry_land() {
    for (uint64_t s = 0; s < 200; ++s) {
        // A mix of small sequential seeds and widely scattered ones. Small
        // seeds are what tests and defaults use; scattered ones are what the
        // game will actually roll.
        const uint64_t seeds[] = {s, s * 0x9E3779B97F4A7C15ull,
                                  ~s * 0xD6E8FEB86659FD93ull};
        for (const uint64_t seed : seeds) {
            const float h = height_at(seed, 0.0f, 0.0f);
            REQUIRE_MSG(h > kSeaLevelMetres + 2.0f,
                        "the spawn point was not dry land", "spawn");

            // And drivable, not the face of a cliff.
            REQUIRE_MSG(normal_at(seed, 0.0f, 0.0f).y > 0.7f,
                        "the spawn point is too steep to start on", "spawn");

            // The surrounding area too — spawning on a one-metre island is
            // not meaningfully better than spawning in the sea.
            for (int a = 0; a < 8; ++a) {
                const float t = static_cast<float>(a) * 0.78539816f;  // 2pi/8
                const float r = 60.0f;
                REQUIRE_MSG(height_at(seed, r * std::cos(t), r * std::sin(t)) >
                                kSeaLevelMetres,
                            "the spawn area is not surrounded by land", "spawn");
            }
        }
    }
    apricot_test::pass("every seed spawns on dry, drivable land");
}

// A height field cannot overhang, so this is an invariant and not a preference.
// A normal with y <= 0 means the gradient maths inverted somewhere, and it
// would light the terrain from underneath and point contact normals into the
// ground.
void normals_are_unit_and_upward() {
    constexpr uint64_t kSeed = 0x77ull;
    for (int j = -60; j <= 60; ++j) {
        for (int i = -60; i <= 60; ++i) {
            const float x = static_cast<float>(i) * 23.0f;
            const float z = static_cast<float>(j) * 23.0f;
            const glm::vec3 n = normal_at(kSeed, x, z);
            REQUIRE_MSG(n.y > 0.0f, "terrain normal pointed downward", "normal");
            const float len = glm::length(n);
            REQUIRE_MSG(len > 0.999f && len < 1.001f, "normal was not unit length",
                        "normal");
        }
    }
    apricot_test::pass("normals are unit length and face upward");
}

// --- materials -----------------------------------------------------------------
void material_weights_are_a_partition() {
    constexpr uint64_t kSeed = 0x9911ull;
    for (int j = -70; j <= 70; ++j) {
        for (int i = -70; i <= 70; ++i) {
            const float x = static_cast<float>(i) * 19.0f;
            const float z = static_cast<float>(j) * 19.0f;
            const SurfaceSample s = surface_at(kSeed, x, z);

            float sum = 0.0f;
            for (glm::length_t m = 0; m < 4; ++m) {
                REQUIRE_MSG(s.weights[m] >= 0.0f, "negative material weight",
                            "weights");
                REQUIRE_MSG(s.weights[m] <= 1.0f, "material weight above one",
                            "weights");
                sum += s.weights[m];
            }
            // The cascade sums to 1 by construction, so the only slack allowed
            // is float rounding across four additions.
            REQUIRE_MSG(sum > 0.9999f && sum < 1.0001f,
                        "material weights did not sum to one", "weights");

            // The dominant material must actually be the largest weight, or
            // physics grip and the shader's splat disagree about what the
            // ground is.
            const float dom = s.weights[static_cast<glm::length_t>(
                surface_index(s.dominant))];
            for (glm::length_t m = 0; m < 4; ++m) {
                REQUIRE_MSG(dom >= s.weights[m],
                            "dominant material was not the largest weight",
                            "dominant");
            }
        }
    }
    apricot_test::pass("material weights partition unity");
}

// The two things the ticket asks for by name: sand at the water line, rock on
// the steep faces.
void beaches_are_at_the_water_line_and_cliffs_are_rock() {
    constexpr uint64_t kSeed = 0xBEAC4ull;

    int shore_samples = 0, shore_sand = 0;
    int steep_samples = 0, steep_rock = 0;

    for (int j = -160; j <= 160; ++j) {
        for (int i = -160; i <= 160; ++i) {
            const float x = static_cast<float>(i) * 9.0f;
            const float z = static_cast<float>(j) * 9.0f;
            const SurfaceSample s = surface_at(kSeed, x, z);
            if (s.height <= kSeaLevelMetres) continue;

            if (s.height < 1.5f && s.slope < 0.3f) {
                ++shore_samples;
                if (s.dominant == Surface::Sand) ++shore_sand;
            }
            if (s.slope > 0.8f) {  // steeper than ~53 degrees
                ++steep_samples;
                if (s.dominant == Surface::Rock) ++steep_rock;
            }
        }
    }

    REQUIRE_MSG(shore_samples > 100, "not enough shoreline to judge", "beach");
    REQUIRE_MSG(shore_sand * 10 >= shore_samples * 8,
                "the water line is not mostly sand", "beach");

    REQUIRE_MSG(steep_samples > 20, "not enough steep terrain to judge", "rock");
    REQUIRE_MSG(steep_rock == steep_samples,
                "a steep face was classified as something other than rock",
                "rock");

    apricot_test::pass("beaches at the water line, rock on the steep faces");
}

void surface_properties_are_sane() {
    // Grip must be ordered rock > gravel > grass > sand, because the whole
    // point of having four surfaces is that the car behaves differently on
    // them. If two are equal the material is decoration.
    const float g_rock = surface_properties(Surface::Rock).grip;
    const float g_gravel = surface_properties(Surface::Gravel).grip;
    const float g_grass = surface_properties(Surface::Grass).grip;
    const float g_sand = surface_properties(Surface::Sand).grip;
    REQUIRE(g_rock > g_gravel);
    REQUIRE(g_gravel > g_grass);
    REQUIRE(g_grass > g_sand);

    // And rolling resistance must run the other way.
    REQUIRE(surface_properties(Surface::Rock).rolling_resistance <
            surface_properties(Surface::Sand).rolling_resistance);

    for (std::size_t m = 0; m < kSurfaceCount; ++m) {
        const Surface s = static_cast<Surface>(m);
        REQUIRE_MSG(surface_properties(s).grip > 0.0f, "non-positive grip",
                    surface_name(s));
        REQUIRE_MSG(surface_properties(s).scatter_density >= 0.0f,
                    "negative scatter density", surface_name(s));
        REQUIRE_MSG(surface_name(s) != nullptr, "missing surface name", "name");
    }
    apricot_test::pass("surface properties are ordered and positive");
}

// --- scatter -------------------------------------------------------------------
void props_belong_to_exactly_one_chunk_and_sit_on_the_ground() {
    constexpr uint64_t kSeed = 0x5CA77E7ull;

    for (int32_t cz = -2; cz <= 2; ++cz) {
        for (int32_t cx = -2; cx <= 2; ++cx) {
            const ChunkCoord c{cx, cz};
            const glm::vec2 o = chunk_origin(c);
            for (const ScatterProp& p : scatter_chunk(kSeed, c)) {
                // Inside its own chunk. A prop that strays outside is either
                // emitted twice (once by each chunk) or falls in the gap when
                // only one of them is resident.
                REQUIRE_MSG(p.position.x >= o.x && p.position.x < o.x + kChunkMetres,
                            "prop escaped its chunk in x", "containment");
                REQUIRE_MSG(p.position.z >= o.y && p.position.z < o.y + kChunkMetres,
                            "prop escaped its chunk in z", "containment");

                // ON the meshed surface, exactly. Not close to it.
                REQUIRE_MSG(bits(p.position.y) ==
                                bits(mesh_height_at(kSeed, p.position.x,
                                                    p.position.z)),
                            "prop was not exactly on the meshed surface",
                            "snapping");

                // Never in the sea.
                REQUIRE_MSG(p.position.y > kSeaLevelMetres,
                            "prop placed below sea level", "sea");

                REQUIRE_MSG(p.scale > 0.0f, "prop had non-positive scale", "scale");
                const uint8_t limit = p.kind == PropKind::Tree ? kTreeVariants
                                                              : kRockVariants;
                REQUIRE_MSG(p.variant < limit, "prop variant out of range",
                            "variant");
            }
        }
    }
    apricot_test::pass("props stay in their chunk and sit on the drawn surface");
}

void scatter_density_follows_the_material() {
    constexpr uint64_t kSeed = 0xF0Full;

    // Count props by the material they stand on, across a wide area, and check
    // the ordering matches the density table. Grass is the only full-density
    // material, so it must dominate; bare rock must be the rarest.
    int by_surface[kSurfaceCount] = {0, 0, 0, 0};
    for (int32_t cz = -8; cz <= 8; ++cz) {
        for (int32_t cx = -8; cx <= 8; ++cx) {
            for (const ScatterProp& p : scatter_chunk(kSeed, ChunkCoord{cx, cz})) {
                ++by_surface[surface_index(p.ground)];
            }
        }
    }
    REQUIRE_MSG(by_surface[surface_index(Surface::Grass)] >
                    by_surface[surface_index(Surface::Gravel)],
                "grass did not carry more props than gravel", "density");
    REQUIRE_MSG(by_surface[surface_index(Surface::Gravel)] >
                    by_surface[surface_index(Surface::Rock)],
                "gravel did not carry more props than rock", "density");

    apricot_test::pass("prop density follows the surface material");
}

void scatter_never_exceeds_its_declared_cap() {
    // The streamer budgets against kMaxPropsPerChunk. If a chunk can exceed it,
    // the instance budget is a suggestion rather than a bound.
    constexpr uint64_t kSeed = 0xCA9ull;
    for (int32_t cz = -10; cz <= 10; ++cz) {
        for (int32_t cx = -10; cx <= 10; ++cx) {
            REQUIRE_MSG(scatter_chunk(kSeed, ChunkCoord{cx, cz}).size() <=
                            kMaxPropsPerChunk,
                        "a chunk produced more props than the declared cap",
                        "cap");
        }
    }
    apricot_test::pass("scatter respects kMaxPropsPerChunk");
}

}  // namespace

int main() {
    std::printf("terrain_determinism_tests\n");
    golden_values();
    generated_twice_is_bit_identical();
    generation_is_order_independent();
    seeds_are_separated();
    heights_stay_inside_their_analytic_bounds();
    the_world_is_an_island();
    every_seed_spawns_on_dry_land();
    normals_are_unit_and_upward();
    material_weights_are_a_partition();
    beaches_are_at_the_water_line_and_cliffs_are_rock();
    surface_properties_are_sane();
    props_belong_to_exactly_one_chunk_and_sit_on_the_ground();
    scatter_density_follows_the_material();
    scatter_never_exceeds_its_declared_cap();
    return apricot_test::done("terrain_determinism_tests");
}
