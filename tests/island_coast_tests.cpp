// The map's zero-height contour IS the 3D coast. Do not replace it with a
// second radius/polygon: that would disagree with the map and flood its roads.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "city/airport.h"
#include "city/terrain_ops.h"
#include "game/map_geometry.h"
#include "physics/terrain_collider.h"
#include "terrain/chunk.h"
#include "terrain/heightmap.h"
#include "terrain/surface.h"
#include "test_assert.h"

using namespace apricot;

namespace {
constexpr uint64_t kSeed = city::kMapSeed;

void ocean_surrounds_the_authored_map() {
    // Include every LOD-0 perimeter vertex, not just four convenient corners.
    float highest = kMinHeightMetres;
    for (int offset = -3072; offset <= 3072; ++offset) {
        const float p = static_cast<float>(offset);
        for (const city::Vec2 q : {city::Vec2{p, -city::kWorldHalfMetres},
                                  {p, city::kWorldHalfMetres},
                                  {-city::kWorldHalfMetres, p},
                                  {city::kWorldHalfMetres, p}}) {
            const float h = height_at(kSeed, q.x, q.z);
            REQUIRE(h < kSeaLevelMetres - 30.0f);
            highest = std::max(highest, h);
        }
    }
    // Outside all authored operators the seabed must stay submerged for
    // other seeds too. A missing falloff cannot pass this as an infinite plain.
    for (uint64_t seed : {0ull, 1ull, 0xDEADBEEFull, 0xC0FFEEull}) {
        for (float p : {-8192.0f, -4096.0f, 4096.0f, 8192.0f}) {
            REQUIRE(height_at(seed, p, 0.0f) < -30.0f);
            REQUIRE(height_at(seed, 0.0f, p) < -30.0f);
        }
    }
    int land = 0, samples = 0;
    for (int z = -3072; z <= 3072; z += 16) {
        for (int x = -3072; x <= 3072; x += 16) {
            const float h = height_at(kSeed, static_cast<float>(x),
                                      static_cast<float>(z));
            REQUIRE(std::isfinite(h));
            REQUIRE(h >= kMinHeightMetres && h <= kMaxHeightMetres);
            land += h > kSeaLevelMetres;
            ++samples;
        }
    }
    const float fraction = static_cast<float>(land) / static_cast<float>(samples);
    REQUIRE(fraction > 0.41f && fraction < 0.44f);
    std::printf("  map land %.3f%%; highest perimeter seabed %.3f m\n",
                100.0f * fraction, highest);
    apricot_test::pass("ocean encloses the full map without shrinking its landmass");
    for (float x=1000;x<3000;x+=1) {
        if (height_at(kSeed,x,0)>1 && height_at(kSeed,x+15,0)<0) {
            std::printf("  east coast visual QA: %.0f 0 height %.2f\n",x,height_at(kSeed,x,0));
            break;
        }
    }
}

void airfield_reclamation_survives_the_channel() {
    const auto& site = city::kAirportSite;
    const float cx = site.origin.x + site.lot_centre.x;
    const float cz = site.origin.z + site.lot_centre.z;
    float lowest = kMaxHeightMetres;
    // Derive the complete lot from its authored data, including all corners.
    for (int zi = 0; zi <= 94; ++zi) {
        for (int xi = 0; xi <= 200; ++xi) {
            const float x = cx + site.lot_width_m * (static_cast<float>(xi) / 200.0f - 0.5f);
            const float z = cz + site.lot_depth_m * (static_cast<float>(zi) / 94.0f - 0.5f);
            const float h = height_at(kSeed, x, z);
            REQUIRE_MSG(h >= site.ground_m - 0.001f,
                        "channel bank cut into the authored airport lot", "airport coast");
            lowest = std::min(lowest, h);
        }
    }
    // Before the reclamation order fix this northwest lot point was -4.98 m.
    REQUIRE(height_at(kSeed, -350.0f, 1960.0f) == site.ground_m);
    REQUIRE(height_at(kSeed, 150.0f, 2140.0f) == site.ground_m);
    REQUIRE(height_at(kSeed, 0.0f, 2430.0f) == site.ground_m);
    std::printf("  lowest airport lot %.3f m\n", lowest);
    apricot_test::pass("whole authored airport lot remains on dry land");
}

void authored_water_and_roads_survive() {
    REQUIRE(height_at(kSeed, -2380.0f, -580.0f) == -11.0f); // harbour
    REQUIRE(height_at(kSeed, -1600.0f, -1180.0f) == -13.0f); // Kessel
    REQUIRE(height_at(kSeed, -650.0f, 1820.0f) == -7.0f); // Camber channel
    REQUIRE(height_at(kSeed, 0.0f, 0.0f) == 12.0f);
    int samples = 0;
    for (const auto& road : city::kRoads) {
        // Bridges intentionally span water. The existing city-map suite
        // separately checks the causeway remains the sole land connection.
        if (city::road_structure_is_decked(road.structure)) continue;
        for (int i = 0; i + 1 < road.count; ++i) {
            const auto a = road.path[i], b = road.path[i + 1];
            const float dx = b.x - a.x, dz = b.z - a.z;
            const float length = std::sqrt(dx * dx + dz * dz);
            const int steps = static_cast<int>(std::ceil(length / 8.0f));
            for (int j = 0; j <= steps; ++j) {
                const float t = static_cast<float>(j) / static_cast<float>(steps);
                const float x = a.x + dx * t, z = a.z + dz * t;
                REQUIRE_MSG(height_at(kSeed, x, z) > kSeaLevelMetres,
                            "ground road submerged by coast", road.name);
                ++samples;
            }
        }
    }
    std::printf("  checked %d ground-road samples\n", samples);
    apricot_test::pass("authored channels stay wet and ground roads stay dry");
}

void coast_mesh_map_and_collision_agree() {
    // Actual sign crossings along the east beach and south airport coast.
    const std::array<std::array<city::Vec2, 2>, 2> transects{{
        {{{2350.0f, 0.0f}, {2600.0f, 0.0f}}},
        {{{0.0f, 2500.0f}, {0.0f, 2800.0f}}}
    }};
    TerrainCollider collider(kSeed);
    for (const auto& line : transects) {
        city::Vec2 dry = line[0], wet = line[1];
        REQUIRE(height_at(kSeed, dry.x, dry.z) > 0.0f);
        REQUIRE(height_at(kSeed, wet.x, wet.z) < 0.0f);
        for (int i = 0; i < 20; ++i) {
            const city::Vec2 mid{(dry.x + wet.x) * 0.5f, (dry.z + wet.z) * 0.5f};
            if (height_at(kSeed, mid.x, mid.z) > 0.0f) dry = mid;
            else wet = mid;
        }
        REQUIRE(std::fabs(height_at(kSeed, wet.x, wet.z)) < 0.001f);
        const ChunkCoord coord = chunk_at(wet.x, wet.z);
        const ChunkMesh mesh = build_chunk(kSeed, coord);
        bool found_crossing = false;
        for (std::size_t i = 0; i < mesh.surface_index_count; i += 3) {
            const auto& a = mesh.vertices[mesh.indices[i]].position;
            const auto& b = mesh.vertices[mesh.indices[i + 1]].position;
            const auto& c = mesh.vertices[mesh.indices[i + 2]].position;
            const float low = std::min({a.y, b.y, c.y});
            const float high = std::max({a.y, b.y, c.y});
            if (low > 0.0f || high <= 0.0f) continue;
            found_crossing = true;
            // Compare contact to the actual emitted triangle's barycentre.
            const glm::vec3 centre = (a + b + c) / 3.0f;
            REQUIRE_NEAR(collider.height(centre.x, centre.z), centre.y, 0.0002f);
            const auto hit = collider.probe_down({centre.x, 20.0f, centre.z}, 100.0f);
            REQUIRE(hit.hit);
            REQUIRE_NEAR(hit.point.y, centre.y, 0.0002f);
            const MapSlice slice = slice_map_triangle(
                {{{a.x, a.z}, {b.x, b.z}, {c.x, c.z}}},
                {{a.y, b.y, c.y}}, kSeaLevelMetres);
            REQUIRE(slice.crossing_count == 2);
            for (const auto p : slice.crossings)
                REQUIRE_NEAR(collider.height(p.x, p.y), kSeaLevelMetres, 0.0002f);
        }
        REQUIRE(found_crossing);
        // Classifier already provides a beach; the render path must use it.
        REQUIRE(surface_at(kSeed, wet.x, wet.z).weights.w > 0.5f);
        for (int lod = 0; lod <= kMaxChunkLod; ++lod) {
            const auto a = build_chunk(kSeed, coord, lod);
            const auto repeat = build_chunk(kSeed, coord, lod);
            const auto neighbour = build_chunk(kSeed, {coord.x + 1, coord.z}, lod);
            const int n = lod_verts(lod);
            for (int row = 0; row < n; ++row) {
                const auto& edge = a.vertices[static_cast<std::size_t>(row * n + n - 1)];
                const auto& other = neighbour.vertices[static_cast<std::size_t>(row * n)];
                REQUIRE(std::memcmp(&edge.position, &other.position, sizeof(glm::vec3)) == 0);
            }
            REQUIRE(a.indices == repeat.indices);
            for (std::size_t i = 0; i < a.vertices.size(); ++i)
                REQUIRE(std::memcmp(&a.vertices[i].position, &repeat.vertices[i].position,
                                    sizeof(glm::vec3)) == 0);
        }
    }
    apricot_test::pass("real coast triangles agree with map slicing, collision and every LOD seam");
}
} // namespace

int main() {
    ocean_surrounds_the_authored_map();
    airfield_reclamation_survives_the_channel();
    authored_water_and_roads_survive();
    coast_mesh_map_and_collision_agree();
    return apricot_test::done("island_coast_tests");
}
