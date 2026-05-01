#pragma once

#include <cstddef>
#include <functional>
#include <glm/glm.hpp>
#include <cmath>

namespace pengine {

struct CellCoord {
    int x = 0;
    int z = 0;
    bool operator==(const CellCoord& o) const { return x == o.x && z == o.z; }
    bool operator!=(const CellCoord& o) const { return !(*this == o); }
};

struct CellCoordHash {
    std::size_t operator()(const CellCoord& c) const {
        // Combine with a large prime to spread bits.
        std::size_t h = static_cast<std::size_t>(c.x) * 2654435761u;
        h ^= static_cast<std::size_t>(c.z) * 2246822519u;
        return h;
    }
};

// World-space position → cell coordinate.
inline CellCoord world_to_cell(float wx, float wz, float cell_size) {
    return { static_cast<int>(std::floor(wx / cell_size)),
             static_cast<int>(std::floor(wz / cell_size)) };
}

// World-space origin (min corner) of a cell.
inline glm::vec3 cell_world_origin(CellCoord c, float cell_size) {
    return { c.x * cell_size, 0.f, c.z * cell_size };
}

// Chebyshev distance between two cell coordinates.
inline int cell_chebyshev(CellCoord a, CellCoord b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.z - b.z));
}

} // namespace pengine
