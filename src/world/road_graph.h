#pragma once

#include <array>
#include <mutex>
#include <random>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "world/cell_coord.h"
#include "world/city_layout.h"

namespace pengine {

// Compass direction along the road grid.
enum class GridDir : int { East = 0, North = 1, West = 2, South = 3 };

inline GridDir opposite(GridDir d) {
    return static_cast<GridDir>((static_cast<int>(d) + 2) & 3);
}

// Aggregates per-cell road grid data into a global directed graph. The world
// is a uniform grid: intersections at world (i*ROAD_PITCH, _, j*ROAD_PITCH)
// for integer i, j. An intersection is loaded iff its containing cell is
// loaded.
//
// All public methods are main-thread safe (locked internally). The streamer
// pump and traffic update both run on the main thread today; the lock is for
// future cross-thread use (AI on a worker).
class RoadGraph {
public:
    void add_cell(CellCoord coord);
    void remove_cell(CellCoord coord);

    // Is intersection (i, j) currently in a loaded cell?
    bool is_intersection_loaded(int i, int j) const;

    // World position of intersection (i, j) at the given ground height.
    static glm::vec3 intersection_pos(int i, int j, float ground_y) {
        return {static_cast<float>(i) * ROAD_PITCH, ground_y,
                static_cast<float>(j) * ROAD_PITCH};
    }

    // Cell containing intersection (i, j). The intersection at the boundary
    // (i % ROADS_PER_CELL == 0) belongs to the eastern/northern cell; we
    // treat (i, j) as belonging to cell (i/4, j/4).
    static CellCoord intersection_cell(int i, int j);

    // List the (delta_i, delta_j) directions out of (i, j) whose neighbour
    // intersection is also loaded. Used by traffic AI to pick turns.
    int outgoing(int i, int j, std::array<GridDir, 4>& out) const;

    // Random intersection inside any currently-loaded cell. Returns false if
    // no cells are loaded.
    bool random_loaded_intersection(std::mt19937& rng, int& out_i, int& out_j) const;

    int loaded_cell_count() const;

private:
    mutable std::mutex                                cells_mu_;
    std::unordered_set<CellCoord, CellCoordHash>     cells_;
};

} // namespace pengine
