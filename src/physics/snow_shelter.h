#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include "physics/terrain_collider.h"

namespace apricot {

// The drift falloff shared with lit.frag, compiled here from the shader's own
// source so the two cannot disagree. See that file for the model.
namespace snow_drift_shader {
using glm::clamp;
using glm::max;
#define SNOW_DRIFT_LINKAGE inline
#include "../../assets/shaders/snow_drift.glsl"
#undef SNOW_DRIFT_LINKAGE
}  // namespace snow_drift_shader

inline float snow_drift_exposure(float edge_distance_m, float clearance_m) {
    return snow_drift_shader::snow_drift_exposure(edge_distance_m, clearance_m);
}

// How much of the open-ground snow reaches a point, from the authored cover
// above it. 1 is open sky. An enclosed roof (StaticBox::open_sided false: a
// building's roof or ceiling) makes everything beneath it 0, so no interior
// ever collects snow. An open-sided roof lets drift in from its edges, see
// snow_drift_exposure(). Under several roofs the most sheltering one wins.
// Own the exact roof snapshot so streaming and collider vector growth cannot
// invalidate queries. Render consumes these same boxes, including yaw.
class SnowShelterField {
public:
    static constexpr float kCellMetres = 32.0f;
    static constexpr float kRoofClearanceM = 0.01f;

    void build(const std::vector<StaticBox>& roofs) {
        boxes_.clear();
        cells_.clear();
        large_boxes_.clear();
        for (const auto& roof : roofs) {
            if (!roof.enabled || roof.is_vehicle || !valid(roof.bounds)) continue;
            if (roof.oriented && (!valid(roof.local_bounds) ||
                !finite(roof.centre) || !std::isfinite(roof.axis_x.x) ||
                !std::isfinite(roof.axis_x.y) || !std::isfinite(roof.axis_z.x) ||
                !std::isfinite(roof.axis_z.y))) continue;
            const uint32_t index = static_cast<uint32_t>(boxes_.size());
            boxes_.push_back(roof);
            const ChunkCoord lo = cell_at(roof.bounds.min.x, roof.bounds.min.z);
            const ChunkCoord hi = cell_at(roof.bounds.max.x, roof.bounds.max.z);
            const int64_t width = int64_t{hi.x} - lo.x + 1;
            const int64_t depth = int64_t{hi.z} - lo.z + 1;
            // A malformed continent-sized cover must not allocate an enormous
            // index. Ordinary roofs all use the spatial path.
            if (width > 4096 || depth > 4096 || width * depth > 4096) {
                large_boxes_.push_back(index);
                continue;
            }
            for (int64_t z = lo.z; z <= hi.z; ++z)
                for (int64_t x = lo.x; x <= hi.x; ++x)
                    cells_[{static_cast<int32_t>(x), static_cast<int32_t>(z)}].push_back(index);
        }
    }

    const std::vector<StaticBox>& boxes() const { return boxes_; }

    // Geometric: is there any authored cover above this point at all? Says
    // nothing about drift; exposure() is what snow depth and shading use.
    bool covered(float x, float base_y, float z) const {
        const glm::vec3 point{x, base_y, z};
        if (!finite(point)) return false;
        const auto cell = cells_.find(cell_at(x, z));
        if (cell != cells_.end())
            for (uint32_t index : cell->second)
                if (beneath(boxes_[index], point)) return true;
        for (uint32_t index : large_boxes_)
            if (beneath(boxes_[index], point)) return true;
        return false;
    }

    // [0, 1] fraction of the open-ground snow that reaches this point.
    // lit.frag's snow_shelter_exposure() walks the same roofs with the same
    // function; SnowShelterGrid::exposure() pins the packed copy against this.
    float exposure(float x, float base_y, float z) const {
        const glm::vec3 point{x, base_y, z};
        if (!finite(point)) return 1.0f;
        float result = 1.0f;
        const auto visit = [&](uint32_t index) {
            const StaticBox& roof = boxes_[index];
            float edge = 0.0f;
            if (!beneath(roof, point, &edge)) return;
            if (!roof.open_sided) { result = 0.0f; return; }
            const float clearance = roof.bounds.min.y - kRoofClearanceM - point.y;
            result = std::min(result, snow_drift_exposure(edge, clearance));
        };
        const auto cell = cells_.find(cell_at(x, z));
        if (cell != cells_.end())
            for (uint32_t index : cell->second) {
                visit(index);
                if (result <= 0.0f) return 0.0f;
            }
        for (uint32_t index : large_boxes_) {
            visit(index);
            if (result <= 0.0f) return 0.0f;
        }
        return result;
    }

private:
    static bool finite(glm::vec3 point) {
        return std::isfinite(point.x) && std::isfinite(point.y) &&
               std::isfinite(point.z);
    }
    static bool valid(const AABB& bounds) {
        return bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y &&
               bounds.min.z <= bounds.max.z && finite(bounds.min) && finite(bounds.max);
    }
    static int32_t cell_index(float value) {
        const double cell = std::floor(static_cast<double>(value) / kCellMetres);
        return static_cast<int32_t>(std::clamp(cell,
            static_cast<double>(std::numeric_limits<int32_t>::min()),
            static_cast<double>(std::numeric_limits<int32_t>::max())));
    }
    static ChunkCoord cell_at(float x, float z) {
        return {cell_index(x), cell_index(z)};
    }
    // Optionally reports the horizontal distance in from the nearest edge of
    // the roof footprint: its yawed rectangle, or its bounds when unrotated.
    static bool beneath(const StaticBox& roof, glm::vec3 point,
                        float* edge_distance = nullptr) {
        if (point.y >= roof.bounds.min.y - kRoofClearanceM ||
            point.x < roof.bounds.min.x || point.x > roof.bounds.max.x ||
            point.z < roof.bounds.min.z || point.z > roof.bounds.max.z) return false;
        const AABB& rect = roof.collision_bounds();
        const glm::vec3 local = roof.local_point(point);
        if (roof.oriented &&
            !(local.x >= rect.min.x && local.x <= rect.max.x &&
              local.z >= rect.min.z && local.z <= rect.max.z)) return false;
        if (edge_distance)
            *edge_distance = std::min(std::min(local.x - rect.min.x, rect.max.x - local.x),
                                      std::min(local.z - rect.min.z, rect.max.z - local.z));
        return true;
    }

    std::vector<StaticBox> boxes_;
    std::unordered_map<ChunkCoord, std::vector<uint32_t>, ChunkCoordHash> cells_;
    std::vector<uint32_t> large_boxes_;
};

}  // namespace apricot
