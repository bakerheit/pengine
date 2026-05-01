#include "world/city_layout.h"

#include <algorithm>
#include <random>

namespace pengine {

namespace {

constexpr int   BLOCKS_PER_CELL = 4;
constexpr float STREET_WIDTH    = 8.f;
constexpr float ROAD_THICKNESS  = 0.10f;
// Building palette tints (subtle variation; multiplied with checker texture).
constexpr glm::vec3 ROAD_TINT  {0.18f, 0.19f, 0.21f};
const     glm::vec3 BUILDING_TINTS[] = {
    {0.85f, 0.84f, 0.80f}, // bone / concrete
    {0.72f, 0.66f, 0.55f}, // sandstone
    {0.55f, 0.60f, 0.68f}, // blue-grey glass
    {0.78f, 0.55f, 0.45f}, // brick
    {0.62f, 0.62f, 0.62f}, // mid-grey
};

constexpr float ROAD_TILE_M  = 4.f;  // metres per asphalt tile
constexpr float WINDOW_TILE_M = 3.f; // metres per window tile

// Add a road slab covering [x0,x1] x [z0,z1] at ground_y, top flush with
// terrain. Slab is centred vertically on (ground_y - ROAD_THICKNESS/2).
void push_road(std::vector<ObjectDef>& out, float x0, float z0,
               float x1, float z1, float ground_y, const CityTextures& tex,
               float y_offset = 0.005f) {
    float w = x1 - x0;
    float d = z1 - z0;
    ObjectDef def;
    def.transform.position = {(x0 + x1) * 0.5f,
                               ground_y - ROAD_THICKNESS * 0.5f + y_offset,
                               (z0 + z1) * 0.5f};
    def.transform.scale    = {w, ROAD_THICKNESS, d};
    def.tint               = ROAD_TINT;
    def.texture            = tex.road;
    def.uv_scale           = {w / ROAD_TILE_M, d / ROAD_TILE_M};
    out.push_back(def);
}

void push_building(std::vector<ObjectDef>& out, std::vector<AABB>& aabbs,
                    float cx, float cz, float w, float d, float h, float ground_y,
                    const glm::vec3& tint, const CityTextures& tex) {
    ObjectDef def;
    float base_y = ground_y - 0.05f;        // sink slightly so it never floats
    float cy     = base_y + h * 0.5f;
    def.transform.position = {cx, cy, cz};
    def.transform.scale    = {w, h, d};
    def.tint               = tint;
    def.texture            = tex.building;
    // One window tile per WINDOW_TILE_M m, derived from the largest face so
    // sides line up reasonably. Cube faces share UV scale so we accept some
    // stretching on faces with mismatched aspect ratios.
    def.uv_scale = {w / WINDOW_TILE_M, h / WINDOW_TILE_M};
    out.push_back(def);

    AABB a;
    a.min = {cx - w * 0.5f, base_y,     cz - d * 0.5f};
    a.max = {cx + w * 0.5f, base_y + h, cz + d * 0.5f};
    aabbs.push_back(a);
}

} // namespace

CityCellLayout generate_city_cell(CellCoord coord, float cell_size, float ground_y,
                                   const CityTextures& tex) {
    CityCellLayout out;

    const float ox = static_cast<float>(coord.x) * cell_size;
    const float oz = static_cast<float>(coord.z) * cell_size;
    const float block_size =
        (cell_size - (BLOCKS_PER_CELL + 1) * STREET_WIDTH) / BLOCKS_PER_CELL;

    // Deterministic RNG for buildings.
    std::uint32_t seed = static_cast<std::uint32_t>(coord.x) * 0x9E3779B1u
                       ^ static_cast<std::uint32_t>(coord.z) * 0x85EBCA77u;
    std::mt19937 rng(seed);
    auto frand = [&](float lo, float hi) {
        return lo + (hi - lo) * (static_cast<float>(rng() & 0xFFFFu) / 65535.f);
    };
    auto irand = [&](int lo, int hi) {
        return lo + static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1));
    };

    // ---- Roads: 5 east-west + 5 north-south slabs --------------------------
    // Each road is a full-cell slab so adjacent cells' roads tile seamlessly.
    for (int i = 0; i <= BLOCKS_PER_CELL; ++i) {
        float pitch = block_size + STREET_WIDTH;
        float s     = static_cast<float>(i) * pitch;

        // East-west road (along X).
        push_road(out.visuals, ox, oz + s,
                  ox + cell_size, oz + s + STREET_WIDTH, ground_y, tex,
                  /*y_offset=*/ 0.005f);
        // North-south road (along Z). Slight Y offset over the EW slabs to
        // avoid z-fighting at intersections.
        push_road(out.visuals, ox + s, oz,
                  ox + s + STREET_WIDTH, oz + cell_size, ground_y, tex,
                  /*y_offset=*/ 0.010f);
    }

    // ---- Buildings: 1–4 per block ------------------------------------------
    // Sidewalk margin: leave 3 m around the block perimeter free of buildings.
    constexpr float SIDEWALK = 3.f;

    for (int bz = 0; bz < BLOCKS_PER_CELL; ++bz) {
        for (int bx = 0; bx < BLOCKS_PER_CELL; ++bx) {
            float bx0 = ox + STREET_WIDTH + bx * (block_size + STREET_WIDTH);
            float bz0 = oz + STREET_WIDTH + bz * (block_size + STREET_WIDTH);
            float bx1 = bx0 + block_size;
            float bz1 = bz0 + block_size;

            // Buildable area inside sidewalk margin.
            float ax0 = bx0 + SIDEWALK, ax1 = bx1 - SIDEWALK;
            float az0 = bz0 + SIDEWALK, az1 = bz1 - SIDEWALK;
            float avail_x = ax1 - ax0;
            float avail_z = az1 - az0;
            if (avail_x < 6.f || avail_z < 6.f) continue;

            int n = irand(1, 4);
            for (int i = 0; i < n; ++i) {
                // Random footprint that fits.
                float w = std::min(avail_x, frand(8.f, 24.f));
                float d = std::min(avail_z, frand(8.f, 24.f));
                float h = frand(6.f, 38.f);

                // Random centre within the buildable area.
                float cx = frand(ax0 + w * 0.5f, ax1 - w * 0.5f);
                float cz = frand(az0 + d * 0.5f, az1 - d * 0.5f);

                const glm::vec3& tint = BUILDING_TINTS[
                    static_cast<size_t>(rng() % (sizeof(BUILDING_TINTS) /
                                                  sizeof(BUILDING_TINTS[0])))];
                push_building(out.visuals, out.collisions,
                              cx, cz, w, d, h, ground_y, tint, tex);
            }
        }
    }

    return out;
}

} // namespace pengine
