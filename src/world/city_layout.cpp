#include "world/city_layout.h"

#include <algorithm>
#include <cmath>
#include <random>

#include <glm/gtc/quaternion.hpp>

#include "world/heightmap.h"
#include "world/world_model_ids.h"

namespace pengine {

namespace {

// ROAD_PITCH, STREET_WIDTH, ROADS_PER_CELL come from city_layout.h.
constexpr float ROAD_THICKNESS = 0.40f;
constexpr float ROAD_TILE_M    = 4.f;
constexpr float WINDOW_TILE_M  = 3.f;

// Long thin slabs are subdivided along their long axis at this spacing.
// 8 m matches the terrain mesh's vertex stride (TerrainChunk::RES = 32 over
// a 256 m cell), so segment endpoints land on terrain grid lines and the
// piecewise-linear ramp matches the bilinear terrain mesh exactly along
// each segment's centerline.
constexpr float SEGMENT_M      = 8.f;

// Sidewalks: slabs sitting 15 cm above the local ground (visible curb).
constexpr float SIDEWALK_CURB_H    = 0.15f;
constexpr float SIDEWALK_THICKNESS = 0.40f;
constexpr float SIDEWALK_TILE_M    = 1.0f;

// Building base is buried this deep below the plot's centre heightmap.
constexpr float BUILDING_BASE_BURY = 1.0f;

// Place a slab as a tilted ramp from `start` to `end` (3D, with heightmap-y).
// `lateral_w` is the slab's width perpendicular to the ramp direction. `lift`
// is how far the slab top sits above the (start..end) line along the slope
// normal — small for roads, sidewalk-curb height for sidewalks.
//
// Adjacent segments that share an endpoint (and therefore both sample the
// heightmap at the same world XZ) meet exactly at that endpoint, so the
// ramp is C0-continuous along its long axis even though each segment is
// itself flat.
void push_slope_slab(std::vector<InstanceDef>& out,
                      uint32_t model_id,
                      glm::vec3 start, glm::vec3 end,
                      float lateral_w, float thickness, float lift,
                      float tile_per_m_lateral, float tile_per_m_long) {
    glm::vec3 dir = end - start;
    float length = glm::length(dir);
    if (length < 1e-4f) return;
    glm::vec3 dir_unit = dir / length;

    // Horizontal lateral axis (perpendicular to travel direction in the XZ
    // plane). We then bend it up by the lateral terrain slope so the slab
    // rolls to match a side-sloped hillside instead of leaving its high
    // edge buried under terrain and its low edge floating.
    glm::vec3 world_up{0.f, 1.f, 0.f};
    glm::vec3 lateral_h = glm::cross(world_up, dir_unit);
    float lat_len = glm::length(lateral_h);
    if (lat_len < 1e-4f) {
        // Vertical ramp — won't happen for our roads, but stay safe.
        lateral_h = glm::vec3{1.f, 0.f, 0.f};
    } else {
        lateral_h /= lat_len;
    }

    // Sample heightmap at the slab's two lateral edges (mid-length, ±lateral_w/2
    // along the horizontal lateral axis). The cube is rigid so we only get one
    // roll per segment — but with 8m segments the lateral slope is essentially
    // linear and a single roll matches it within a few cm.
    glm::vec3 mid_xz   = (start + end) * 0.5f;
    glm::vec3 left_xz  = mid_xz - lateral_h * (lateral_w * 0.5f);
    glm::vec3 right_xz = mid_xz + lateral_h * (lateral_w * 0.5f);
    float h_left  = Heightmap::sample(left_xz.x,  left_xz.z);
    float h_right = Heightmap::sample(right_xz.x, right_xz.z);

    // Cap the lateral roll at ±30° (~58 % slope) so a single freak heightmap
    // pixel can't tip a slab on edge.
    constexpr float MAX_ROLL_TAN = 0.577f;
    float roll_per_m = std::clamp((h_right - h_left) / lateral_w,
                                   -MAX_ROLL_TAN, MAX_ROLL_TAN);
    glm::vec3 lateral = glm::normalize(lateral_h + world_up * roll_per_m);
    glm::vec3 normal  = glm::normalize(glm::cross(dir_unit, lateral));

    // Build the rotation matrix whose columns are the world-space directions
    // that the cube's local +X / +Y / +Z axes should map to.
    glm::mat3 rot_basis(lateral, normal, dir_unit);
    glm::quat rot = glm::quat_cast(rot_basis);

    glm::vec3 pos = mid_xz + normal * (lift - thickness * 0.5f);

    InstanceDef inst;
    inst.model_id           = model_id;
    inst.transform.position = pos;
    inst.transform.rotation = rot;
    inst.transform.scale    = {lateral_w, thickness, length};
    inst.uv_scale_override  = {lateral_w * tile_per_m_lateral,
                                length    * tile_per_m_long};
    out.push_back(inst);
}

// Sorted list of road-segment endpoints along [t0, t1] including:
//   - The boundaries t0 and t1.
//   - Every intersection centre (multiple of ROAD_PITCH) inside the range.
//   - The two band edges (centre ± ROAD_HALF_WIDTH) of every intersection.
//   - SEGMENT_M-spaced fillers between adjacent special points.
//
// The band-edge endpoints are the critical part: paired with sample_for_slab,
// the segment that spans an intersection's plateau region [centre-4m, centre+4m]
// has both endpoints sampled at the same plateau Y → the slab is flat across
// the intersection and NS/EW slabs converge to a shared elevation rather than
// rendering at each road's natural grade.
std::vector<float> road_endpoints(float t0, float t1) {
    std::vector<float> e;
    e.reserve(64);
    e.push_back(t0);
    e.push_back(t1);

    int k0 = static_cast<int>(std::floor((t0 - ROAD_HALF_WIDTH) / ROAD_PITCH));
    int k1 = static_cast<int>(std::ceil ((t1 + ROAD_HALF_WIDTH) / ROAD_PITCH));
    for (int k = k0; k <= k1; ++k) {
        float ic = static_cast<float>(k) * ROAD_PITCH;
        for (float t : {ic - ROAD_HALF_WIDTH, ic, ic + ROAD_HALF_WIDTH}) {
            if (t > t0 + 1e-3f && t < t1 - 1e-3f) e.push_back(t);
        }
    }
    std::sort(e.begin(), e.end());
    e.erase(std::unique(e.begin(), e.end(),
                        [](float a, float b){ return std::abs(a - b) < 1e-3f; }),
            e.end());

    std::vector<float> out;
    out.reserve(e.size() * 3);
    out.push_back(e.front());
    for (std::size_t i = 1; i < e.size(); ++i) {
        float gap = e[i] - e[i - 1];
        int   n   = std::max(1, static_cast<int>(std::ceil(gap / SEGMENT_M)));
        for (int j = 1; j < n; ++j) {
            out.push_back(e[i - 1] + gap * static_cast<float>(j) /
                                       static_cast<float>(n));
        }
        out.push_back(e[i]);
    }
    return out;
}

// NS road slab. Endpoints come from road_endpoints() so segments fall on
// intersection band edges; height comes from Heightmap::sample_for_slab so
// segments inside an intersection band are flat at the plateau.
void push_road_ns(std::vector<InstanceDef>& out,
                   float xc, float z0, float z1, float lift) {
    constexpr float tile_per_m = 1.f / ROAD_TILE_M;
    std::vector<float> zs_list = road_endpoints(z0, z1);
    for (std::size_t i = 1; i < zs_list.size(); ++i) {
        float zs = zs_list[i - 1];
        float ze = zs_list[i];
        glm::vec3 start{xc, Heightmap::sample_for_slab(xc, zs), zs};
        glm::vec3 end  {xc, Heightmap::sample_for_slab(xc, ze), ze};
        push_slope_slab(out, world_ids::RoadSlab, start, end,
                        STREET_WIDTH, ROAD_THICKNESS, lift,
                        tile_per_m, tile_per_m);
    }
}

void push_road_ew(std::vector<InstanceDef>& out,
                   float zc, float x0, float x1, float lift) {
    constexpr float tile_per_m = 1.f / ROAD_TILE_M;
    std::vector<float> xs_list = road_endpoints(x0, x1);
    for (std::size_t i = 1; i < xs_list.size(); ++i) {
        float xs = xs_list[i - 1];
        float xe = xs_list[i];
        glm::vec3 start{xs, Heightmap::sample_for_slab(xs, zc), zc};
        glm::vec3 end  {xe, Heightmap::sample_for_slab(xe, zc), zc};
        push_slope_slab(out, world_ids::RoadSlab, start, end,
                        STREET_WIDTH, ROAD_THICKNESS, lift,
                        tile_per_m, tile_per_m);
    }
}

// Sidewalk strip: rectangle (x0,z0)-(x1,z1). Long axis = whichever dimension
// is bigger. Subdivided into ramped segments along that axis.
void push_sidewalk_strip(std::vector<InstanceDef>& out,
                          float x0, float z0, float x1, float z1) {
    float w = x1 - x0;
    float d = z1 - z0;
    if (w <= 0.f || d <= 0.f) return;
    constexpr float tile_per_m = 1.f / SIDEWALK_TILE_M;

    if (d >= w) {
        // Long axis = z, lateral along x.
        float xc = (x0 + x1) * 0.5f;
        int n_segs = std::max(1, static_cast<int>(std::ceil(d / SEGMENT_M)));
        for (int i = 0; i < n_segs; ++i) {
            float t0 = static_cast<float>(i)     / static_cast<float>(n_segs);
            float t1 = static_cast<float>(i + 1) / static_cast<float>(n_segs);
            float zs = z0 + d * t0;
            float ze = z0 + d * t1;
            glm::vec3 start{xc, Heightmap::sample_for_slab(xc, zs), zs};
            glm::vec3 end  {xc, Heightmap::sample_for_slab(xc, ze), ze};
            push_slope_slab(out, world_ids::SidewalkSlab, start, end,
                            w, SIDEWALK_THICKNESS, SIDEWALK_CURB_H,
                            tile_per_m, tile_per_m);
        }
    } else {
        // Long axis = x, lateral along z.
        float zc = (z0 + z1) * 0.5f;
        int n_segs = std::max(1, static_cast<int>(std::ceil(w / SEGMENT_M)));
        for (int i = 0; i < n_segs; ++i) {
            float t0 = static_cast<float>(i)     / static_cast<float>(n_segs);
            float t1 = static_cast<float>(i + 1) / static_cast<float>(n_segs);
            float xs = x0 + w * t0;
            float xe = x0 + w * t1;
            glm::vec3 start{xs, Heightmap::sample_for_slab(xs, zc), zc};
            glm::vec3 end  {xe, Heightmap::sample_for_slab(xe, zc), zc};
            push_slope_slab(out, world_ids::SidewalkSlab, start, end,
                            d, SIDEWALK_THICKNESS, SIDEWALK_CURB_H,
                            tile_per_m, tile_per_m);
        }
    }
}

// Buildings stay axis-aligned (concrete towers don't lean on a slope).
// Base is buried below the plot's centre heightmap so the wall stays in
// contact with the terrain mesh at every footprint corner.
void push_building(std::vector<InstanceDef>& insts, std::vector<AABB>& aabbs,
                    float cx, float cz, float w, float d, float h,
                    float plot_ground_y, uint32_t model_id) {
    float base_y  = plot_ground_y - BUILDING_BASE_BURY;
    float top_y   = plot_ground_y + h;
    float cy      = (base_y + top_y) * 0.5f;
    float scale_y = top_y - base_y;

    InstanceDef inst;
    inst.model_id           = model_id;
    inst.transform.position = {cx, cy, cz};
    inst.transform.scale    = {w, scale_y, d};
    inst.uv_scale_override  = {w / WINDOW_TILE_M, scale_y / WINDOW_TILE_M};
    insts.push_back(inst);

    AABB a;
    a.min = {cx - w * 0.5f, base_y, cz - d * 0.5f};
    a.max = {cx + w * 0.5f, top_y,  cz + d * 0.5f};
    aabbs.push_back(a);
}

}  // namespace

CityCellLayout generate_city_cell(CellCoord coord, float cell_size) {
    CityCellLayout out;

    const float ox = static_cast<float>(coord.x) * cell_size;
    const float oz = static_cast<float>(coord.z) * cell_size;

    std::uint32_t seed = static_cast<std::uint32_t>(coord.x) * 0x9E3779B1u
                       ^ static_cast<std::uint32_t>(coord.z) * 0x85EBCA77u;
    std::mt19937 rng(seed);
    auto frand = [&](float lo, float hi) {
        return lo + (hi - lo) * (static_cast<float>(rng() & 0xFFFFu) / 65535.f);
    };

    // ---- Roads (slope-tilted ramped segments) -------------------------------
    for (int i = 0; i < ROADS_PER_CELL; ++i) {
        float xc = ox + static_cast<float>(i) * ROAD_PITCH;
        float zc = oz + static_cast<float>(i) * ROAD_PITCH;
        push_road_ns(out.instances, xc, oz, oz + cell_size, /*lift=*/0.040f);
        // EW road slightly above NS so intersections pick a clear winner.
        push_road_ew(out.instances, zc, ox, ox + cell_size, /*lift=*/0.035f);
    }

    // ---- Building plots: ROADS_PER_CELL × ROADS_PER_CELL --------------------
    constexpr float SIDEWALK      = 3.f;
    constexpr int   SUBGRID       = 2;
    constexpr float SUB_FILL_PROB = 0.78f;
    constexpr float TOWER_PROB    = 0.10f;

    auto pick_building_id = [&]() -> uint32_t {
        return world_ids::BuildingFirst +
               (rng() % world_ids::BuildingCount);
    };

    const float plot_size = ROAD_PITCH - STREET_WIDTH;  // 56 m

    for (int j = 0; j < ROADS_PER_CELL; ++j) {
        for (int i = 0; i < ROADS_PER_CELL; ++i) {
            float plot_cx = ox + (static_cast<float>(i) + 0.5f) * ROAD_PITCH;
            float plot_cz = oz + (static_cast<float>(j) + 0.5f) * ROAD_PITCH;

            float plot_ground_y = Heightmap::sample(plot_cx, plot_cz);

            float px0 = plot_cx - plot_size * 0.5f;
            float px1 = plot_cx + plot_size * 0.5f;
            float pz0 = plot_cz - plot_size * 0.5f;
            float pz1 = plot_cz + plot_size * 0.5f;

            float ax0 = px0 + SIDEWALK;
            float ax1 = px1 - SIDEWALK;
            float az0 = pz0 + SIDEWALK;
            float az1 = pz1 - SIDEWALK;

            push_sidewalk_strip(out.instances, px0, pz0, px1, az0); // N
            push_sidewalk_strip(out.instances, px0, az1, px1, pz1); // S
            push_sidewalk_strip(out.instances, px0, az0, ax0, az1); // W
            push_sidewalk_strip(out.instances, ax1, az0, px1, az1); // E

            float avail_x = ax1 - ax0;
            float avail_z = az1 - az0;
            if (avail_x < 6.f || avail_z < 6.f) continue;

            if (frand(0.f, 1.f) < TOWER_PROB) {
                float w = frand(avail_x * 0.65f, avail_x * 0.92f);
                float d = frand(avail_z * 0.65f, avail_z * 0.92f);
                float h = frand(35.f, 70.f);
                push_building(out.instances, out.collisions,
                              plot_cx, plot_cz, w, d, h, plot_ground_y,
                              pick_building_id());
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

                    push_building(out.instances, out.collisions,
                                  cx, cz, w, d, h, plot_ground_y,
                                  pick_building_id());
                }
            }
        }
    }

    return out;
}

float city_ground_sample(float x, float z) {
    float base = Heightmap::sample(x, z);

    int   i        = static_cast<int>(std::floor(x / ROAD_PITCH));
    int   j        = static_cast<int>(std::floor(z / ROAD_PITCH));
    float plot_cx  = (static_cast<float>(i) + 0.5f) * ROAD_PITCH;
    float plot_cz  = (static_cast<float>(j) + 0.5f) * ROAD_PITCH;
    float dx       = std::abs(x - plot_cx);
    float dz       = std::abs(z - plot_cz);

    constexpr float plot_half  = ROAD_PITCH * 0.5f - STREET_WIDTH * 0.5f;
    constexpr float SIDEWALK   = 3.f;
    constexpr float inner_half = plot_half - SIDEWALK;

    bool inside_plot  = (dx <= plot_half  && dz <= plot_half);
    bool inside_inner = (dx <= inner_half && dz <= inner_half);
    if (inside_plot && !inside_inner) {
        return base + SIDEWALK_CURB_H;
    }
    return base;
}

}  // namespace pengine
