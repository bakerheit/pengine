#include "terrain/streamer.h"

#include <algorithm>

namespace apricot {

void Streamer::update(glm::vec3 camera_pos) {
    loads_.clear();
    evicts_.clear();

    const ChunkCoord centre = chunk_at(camera_pos.x, camera_pos.z);
    const int load_r = cfg_.load_radius;
    const int evict_r = std::max(cfg_.evict_radius, load_r + 1);

    // --- what is missing inside the load radius ------------------------------
    // Collected with a squared-distance key so the budget spends itself on the
    // chunks the player is about to reach, not on whichever corner of the
    // square the loop happened to visit first.
    struct Candidate {
        ChunkCoord coord;
        int dist_sq;
    };
    std::vector<Candidate> wanted;

    for (int dz = -load_r; dz <= load_r; ++dz) {
        for (int dx = -load_r; dx <= load_r; ++dx) {
            const int d2 = dx * dx + dz * dz;
            if (d2 > load_r * load_r) continue;  // circular, not square

            const ChunkCoord c{centre.x + dx, centre.z + dz};
            if (resident_.count(c) != 0 || in_flight_.count(c) != 0) continue;
            wanted.push_back(Candidate{c, d2});
        }
    }

    std::sort(wanted.begin(), wanted.end(),
              [](const Candidate& a, const Candidate& b) {
                  // Tie-break on coordinate so the order is fully determined by
                  // the inputs. Two machines given the same camera position
                  // must produce the same load order.
                  if (a.dist_sq != b.dist_sq) return a.dist_sq < b.dist_sq;
                  if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
                  return a.coord.z < b.coord.z;
              });

    const std::size_t budget =
        cfg_.max_loads_per_update > 0
            ? static_cast<std::size_t>(cfg_.max_loads_per_update)
            : wanted.size();

    for (std::size_t i = 0; i < wanted.size() && i < budget; ++i) {
        loads_.push_back(wanted[i].coord);
        in_flight_.insert(wanted[i].coord);
    }

    // --- what has fallen outside the evict radius ----------------------------
    for (const ChunkCoord& c : resident_) {
        const int dx = c.x - centre.x;
        const int dz = c.z - centre.z;
        if (dx * dx + dz * dz > evict_r * evict_r) evicts_.push_back(c);
    }
    std::sort(evicts_.begin(), evicts_.end(), [](ChunkCoord a, ChunkCoord b) {
        // unordered_set iteration order is not guaranteed; sort so the
        // eviction list is deterministic too.
        if (a.x != b.x) return a.x < b.x;
        return a.z < b.z;
    });
}

void Streamer::mark_resident(ChunkCoord c) {
    in_flight_.erase(c);
    resident_.insert(c);
}

void Streamer::mark_evicted(ChunkCoord c) {
    resident_.erase(c);
    in_flight_.erase(c);
}

}  // namespace apricot
