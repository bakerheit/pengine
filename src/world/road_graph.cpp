#include "world/road_graph.h"

#include <algorithm>
#include <iterator>

namespace pengine {

void RoadGraph::add_cell(CellCoord coord) {
    std::lock_guard<std::mutex> lk(cells_mu_);
    cells_.insert(coord);
}

void RoadGraph::remove_cell(CellCoord coord) {
    std::lock_guard<std::mutex> lk(cells_mu_);
    cells_.erase(coord);
}

int RoadGraph::loaded_cell_count() const {
    std::lock_guard<std::mutex> lk(cells_mu_);
    return static_cast<int>(cells_.size());
}

CellCoord RoadGraph::intersection_cell(int i, int j) {
    // Cell ownership: cell cx contains intersections with i ∈ [cx*4, cx*4+4).
    // For non-negative i, integer division == floor division.
    int cx = (i >= 0) ? (i / ROADS_PER_CELL)
                       : -((-i + ROADS_PER_CELL - 1) / ROADS_PER_CELL);
    int cz = (j >= 0) ? (j / ROADS_PER_CELL)
                       : -((-j + ROADS_PER_CELL - 1) / ROADS_PER_CELL);
    return {cx, cz};
}

bool RoadGraph::is_intersection_loaded(int i, int j) const {
    CellCoord c = intersection_cell(i, j);
    std::lock_guard<std::mutex> lk(cells_mu_);
    return cells_.count(c) > 0;
}

int RoadGraph::outgoing(int i, int j, std::array<GridDir, 4>& out) const {
    std::size_t n = 0;
    if (is_intersection_loaded(i + 1, j)) out[n++] = GridDir::East;
    if (is_intersection_loaded(i, j + 1)) out[n++] = GridDir::North;
    if (is_intersection_loaded(i - 1, j)) out[n++] = GridDir::West;
    if (is_intersection_loaded(i, j - 1)) out[n++] = GridDir::South;
    return static_cast<int>(n);
}

std::vector<CellCoord> RoadGraph::loaded_cells() const {
    std::lock_guard<std::mutex> lk(cells_mu_);
    return {cells_.begin(), cells_.end()};
}

std::vector<std::pair<int,int>> RoadGraph::loaded_intersections() const {
    std::vector<std::pair<int,int>> out;
    std::lock_guard<std::mutex> lk(cells_mu_);
    out.reserve(cells_.size() * ROADS_PER_CELL * ROADS_PER_CELL);
    for (const auto& c : cells_) {
        for (int dj = 0; dj < ROADS_PER_CELL; ++dj)
            for (int di = 0; di < ROADS_PER_CELL; ++di)
                out.emplace_back(c.x * ROADS_PER_CELL + di,
                                  c.z * ROADS_PER_CELL + dj);
    }
    return out;
}

bool RoadGraph::random_loaded_intersection(std::mt19937& rng,
                                            int& out_i, int& out_j) const {
    std::lock_guard<std::mutex> lk(cells_mu_);
    if (cells_.empty()) return false;
    auto it = cells_.begin();
    std::advance(it, rng() % cells_.size());
    int local_i = static_cast<int>(rng() % static_cast<unsigned>(ROADS_PER_CELL));
    int local_j = static_cast<int>(rng() % static_cast<unsigned>(ROADS_PER_CELL));
    out_i = it->x * ROADS_PER_CELL + local_i;
    out_j = it->z * ROADS_PER_CELL + local_j;
    return true;
}

} // namespace pengine
