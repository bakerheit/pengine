#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "scene/aabb.h"
#include "world/cell_coord.h"

namespace pengine {

struct RayHit {
    bool      hit      = false;
    float     t        = 0.f;          // distance along ray
    glm::vec3 position = {0, 0, 0};
    glm::vec3 normal   = {0, 1, 0};
};

// World-space static collision registry.
// Buildings are AABBs (cubes scaled along X/Y/Z); terrain is the analytic
// Heightmap. Cells are added/removed by the streamer.
//
// All methods are main-thread; cells_ is also touched by the streamer pump,
// which is also main-thread, so no locking is needed today. Mutex kept for
// future cross-thread queries (AI, vehicles).
class WorldCollision {
public:
    void add_cell(CellCoord coord, std::vector<AABB> building_aabbs);
    void remove_cell(CellCoord coord);
    void clear();

    int  building_count() const;

    // Trace `dir` (need not be normalised) up to `max_dist` metres from
    // `origin`. Tests terrain (analytic, marched) and all loaded buildings.
    RayHit raycast(const glm::vec3& origin, const glm::vec3& dir, float max_dist) const;

    // Resolve a horizontal cylinder (radius r, vertical extent [feet_y, feet_y+height])
    // against all loaded buildings. Returns the corrected XZ position; Y is
    // unchanged. Iterates a few times to handle corner cases.
    glm::vec2 resolve_cylinder_xz(glm::vec2 pos_xz, float feet_y, float height,
                                   float radius) const;

private:
    struct Cell {
        std::vector<AABB> buildings;
    };
    mutable std::mutex                                       mu_;
    std::unordered_map<CellCoord, Cell, CellCoordHash>       cells_;
};

} // namespace pengine
