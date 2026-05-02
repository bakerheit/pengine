#include "world/city_layout.h"

#include <algorithm>
#include <random>

namespace pengine {

namespace {

// ROAD_PITCH, STREET_WIDTH, ROADS_PER_CELL come from city_layout.h.
constexpr float ROAD_THICKNESS = 0.10f;
constexpr float ROAD_TILE_M    = 4.f;
constexpr float WINDOW_TILE_M  = 3.f;

constexpr glm::vec3 ROAD_TINT  {0.18f, 0.19f, 0.21f};
const     glm::vec3 BUILDING_TINTS[] = {
    {0.85f, 0.84f, 0.80f}, // bone / concrete
    {0.72f, 0.66f, 0.55f}, // sandstone
    {0.55f, 0.60f, 0.68f}, // blue-grey glass
    {0.78f, 0.55f, 0.45f}, // brick
    {0.62f, 0.62f, 0.62f}, // mid-grey
};

void push_road(std::vector<ObjectDef>& out, float x0, float z0,
               float x1, float z1, float ground_y, const CityTextures& tex,
               float y_offset) {
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
    float base_y = ground_y - 0.05f;
    float cy     = base_y + h * 0.5f;
    def.transform.position = {cx, cy, cz};
    def.transform.scale    = {w, h, d};
    def.tint               = tint;
    def.texture            = tex.building;
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

    std::uint32_t seed = static_cast<std::uint32_t>(coord.x) * 0x9E3779B1u
                       ^ static_cast<std::uint32_t>(coord.z) * 0x85EBCA77u;
    std::mt19937 rng(seed);
    auto frand = [&](float lo, float hi) {
        return lo + (hi - lo) * (static_cast<float>(rng() & 0xFFFFu) / 65535.f);
    };

    // ---- Roads --------------------------------------------------------------
    // Each cell owns ROADS_PER_CELL NS + ROADS_PER_CELL EW road slabs.
    // Centerlines at world positions {ox, ox+64, ox+128, ox+192} (and same for z).
    for (int i = 0; i < ROADS_PER_CELL; ++i) {
        float xc = ox + static_cast<float>(i) * ROAD_PITCH;
        float zc = oz + static_cast<float>(i) * ROAD_PITCH;
        // NS road (along Z, varying x).
        push_road(out.visuals,
                  xc - STREET_WIDTH * 0.5f, oz,
                  xc + STREET_WIDTH * 0.5f, oz + cell_size,
                  ground_y, tex, /*y_offset=*/ 0.010f);
        // EW road (along X, varying z).
        push_road(out.visuals,
                  ox, zc - STREET_WIDTH * 0.5f,
                  ox + cell_size, zc + STREET_WIDTH * 0.5f,
                  ground_y, tex, /*y_offset=*/ 0.005f);
    }

    // ---- Building plots: ROADS_PER_CELL × ROADS_PER_CELL --------------------
    // Plot (i, j) is the block bounded by roads i and i+1 on x, and j and j+1
    // on z. Plot center at world (ox + (i+0.5)*PITCH, oz + (j+0.5)*PITCH).
    constexpr float SIDEWALK      = 3.f;
    constexpr int   SUBGRID       = 2;
    constexpr float SUB_FILL_PROB = 0.78f;
    constexpr float TOWER_PROB    = 0.10f;

    auto pick_tint = [&]() -> const glm::vec3& {
        return BUILDING_TINTS[
            static_cast<size_t>(rng() % (sizeof(BUILDING_TINTS) /
                                          sizeof(BUILDING_TINTS[0])))];
    };

    const float plot_size = ROAD_PITCH - STREET_WIDTH; // 56 m

    for (int j = 0; j < ROADS_PER_CELL; ++j) {
        for (int i = 0; i < ROADS_PER_CELL; ++i) {
            float plot_cx = ox + (static_cast<float>(i) + 0.5f) * ROAD_PITCH;
            float plot_cz = oz + (static_cast<float>(j) + 0.5f) * ROAD_PITCH;

            float ax0 = plot_cx - plot_size * 0.5f + SIDEWALK;
            float ax1 = plot_cx + plot_size * 0.5f - SIDEWALK;
            float az0 = plot_cz - plot_size * 0.5f + SIDEWALK;
            float az1 = plot_cz + plot_size * 0.5f - SIDEWALK;
            float avail_x = ax1 - ax0;
            float avail_z = az1 - az0;
            if (avail_x < 6.f || avail_z < 6.f) continue;

            // Tower variant.
            if (frand(0.f, 1.f) < TOWER_PROB) {
                float w = frand(avail_x * 0.65f, avail_x * 0.92f);
                float d = frand(avail_z * 0.65f, avail_z * 0.92f);
                float h = frand(35.f, 70.f);
                push_building(out.visuals, out.collisions,
                              plot_cx, plot_cz, w, d, h,
                              ground_y, pick_tint(), tex);
                continue;
            }

            float sub_w = avail_x / SUBGRID;
            float sub_d = avail_z / SUBGRID;
            for (int sz = 0; sz < SUBGRID; ++sz) {
                for (int sx = 0; sx < SUBGRID; ++sx) {
                    if (frand(0.f, 1.f) > SUB_FILL_PROB) continue;

                    float sx0 = ax0 + static_cast<float>(sx) * sub_w;
                    float sz0 = az0 + static_cast<float>(sz) * sub_d;
                    constexpr float SUB_MARGIN = 0.4f;
                    float w = frand(sub_w * 0.55f, sub_w - 2.f * SUB_MARGIN);
                    float d = frand(sub_d * 0.55f, sub_d - 2.f * SUB_MARGIN);
                    float h = frand(6.f, 32.f);

                    float free_x = sub_w - w - 2.f * SUB_MARGIN;
                    float free_z = sub_d - d - 2.f * SUB_MARGIN;
                    float cx = sx0 + SUB_MARGIN + w * 0.5f
                             + (free_x > 0.f ? frand(0.f, free_x) : 0.f);
                    float cz = sz0 + SUB_MARGIN + d * 0.5f
                             + (free_z > 0.f ? frand(0.f, free_z) : 0.f);

                    push_building(out.visuals, out.collisions,
                                  cx, cz, w, d, h,
                                  ground_y, pick_tint(), tex);
                }
            }
        }
    }

    return out;
}

} // namespace pengine
