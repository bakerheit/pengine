#include "world/streamer.h"

#include <chrono>
#include <random>
#include <cmath>

#include "core/log.h"
#include "render/mesh.h"
#include "scene/scene.h"
#include "scene/scene_node.h"

namespace pengine {

// ---------------------------------------------------------------------------
// Procedural cell generation
// ---------------------------------------------------------------------------

void Streamer::generate_cell(CellCoord coord, const WorldConfig& cfg,
                              std::vector<ObjectDef>& out) {
    // Seed deterministically from cell coordinates.
    std::uint32_t seed = static_cast<std::uint32_t>(coord.x * 1000003 + coord.z * 999983);
    std::mt19937 rng(seed);
    auto frand = [&](float lo, float hi) {
        return lo + (hi - lo) * (static_cast<float>(rng() & 0xFFFFu) / 65535.f);
    };
    auto irand = [&](int lo, int hi) {
        return lo + static_cast<int>(rng() % static_cast<unsigned>(hi - lo + 1));
    };

    float ox = static_cast<float>(coord.x) * cfg.cell_size;
    float oz = static_cast<float>(coord.z) * cfg.cell_size;

    int count = irand(8, 20);
    for (int i = 0; i < count; ++i) {
        ObjectDef def;
        float px = ox + frand(4.f, cfg.cell_size - 4.f);
        float pz = oz + frand(4.f, cfg.cell_size - 4.f);
        float w  = frand(2.f, 6.f);
        float h  = frand(3.f, 18.f);

        def.transform.position = {px, h * 0.5f, pz};
        def.transform.scale    = {w, h, w};
        def.lod_near = cfg.cell_size * 1.5f;
        def.lod_far  = cfg.cell_size * 3.f;
        out.push_back(def);
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Streamer::init(const WorldConfig& cfg, Scene* scene, const Mesh* cube_mesh) {
    cfg_       = cfg;
    scene_     = scene;
    cube_mesh_ = cube_mesh;
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&Streamer::thread_func, this);
    PE_INFO("Streamer started  cell_size=%.0f radius=%d  world=%dx%d cells",
            cfg_.cell_size, cfg_.stream_radius,
            cfg_.world_cells_x, cfg_.world_cells_z);
}

void Streamer::shutdown() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

// ---------------------------------------------------------------------------
// Background thread
// ---------------------------------------------------------------------------

void Streamer::thread_func() {
    // Stream thread tracks what it believes is loaded, to avoid re-queuing.
    std::unordered_set<CellCoord, CellCoordHash> known_loaded;

    while (running_.load(std::memory_order_acquire)) {
        glm::vec3 pos;
        {
            std::lock_guard<std::mutex> lk(cam_mutex_);
            pos = cam_pos_;
        }

        CellCoord cam_cell = world_to_cell(pos.x, pos.z, cfg_.cell_size);

        // Build desired set: cells within stream_radius (Chebyshev).
        std::unordered_set<CellCoord, CellCoordHash> desired;
        int r = cfg_.stream_radius;
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                CellCoord c{cam_cell.x + dx, cam_cell.z + dz};
                // Clamp to world bounds.
                if (c.x < 0 || c.x >= cfg_.world_cells_x) continue;
                if (c.z < 0 || c.z >= cfg_.world_cells_z) continue;
                desired.insert(c);
            }
        }

        // Cells to load: in desired but not known_loaded.
        std::vector<LoadJob> new_loads;
        for (const CellCoord& c : desired) {
            if (known_loaded.count(c) == 0) {
                LoadJob job;
                job.coord = c;
                generate_cell(c, cfg_, job.defs);
                new_loads.push_back(std::move(job));
                known_loaded.insert(c);
            }
        }

        // Cells to evict: in known_loaded but not desired.
        std::vector<EvictJob> new_evicts;
        for (auto it = known_loaded.begin(); it != known_loaded.end(); ) {
            if (desired.count(*it) == 0) {
                new_evicts.push_back({*it});
                it = known_loaded.erase(it);
            } else {
                ++it;
            }
        }

        // Push to shared queues.
        if (!new_loads.empty() || !new_evicts.empty()) {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            for (auto& j : new_loads)  load_queue_.push_back(std::move(j));
            for (auto& j : new_evicts) evict_queue_.push_back(j);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ---------------------------------------------------------------------------
// Main-thread pump — call before Scene::update() each frame
// ---------------------------------------------------------------------------

void Streamer::pump(glm::vec3 cam_pos) {
    // Update the camera position for the stream thread.
    {
        std::lock_guard<std::mutex> lk(cam_mutex_);
        cam_pos_ = cam_pos;
    }

    // Drain queues (swap trick: hold lock only during swap, not during GL work).
    std::vector<LoadJob>  loads;
    std::vector<EvictJob> evicts;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        loads.swap(load_queue_);
        evicts.swap(evict_queue_);
    }

    // Process evictions first (frees memory before allocating).
    for (const EvictJob& job : evicts) {
        auto it = loaded_.find(job.coord);
        if (it == loaded_.end()) continue;
        for (SceneNode* n : it->second.nodes) scene_->remove_node(n);
        loaded_.erase(it);
    }

    // Process loads — cap per frame to avoid hitches.
    int activated = 0;
    std::vector<LoadJob> deferred;
    for (auto& job : loads) {
        if (activated >= cfg_.max_uploads_per_frame) {
            deferred.push_back(std::move(job));
            continue;
        }
        if (loaded_.count(job.coord)) continue; // already loaded (race)

        LoadedCell lc;
        AABB cube_aabb;
        cube_aabb.min = cube_mesh_->bounds_min();
        cube_aabb.max = cube_mesh_->bounds_max();

        for (const ObjectDef& def : job.defs) {
            SceneNode* n = scene_->create_node();
            n->transform   = def.transform;
            n->renderable  = Renderable{cube_mesh_, cube_aabb};
            n->mark_dirty();
            lc.nodes.push_back(n);
        }
        loaded_.emplace(job.coord, std::move(lc));
        ++activated;
    }

    // Push deferred jobs back for the next frame.
    if (!deferred.empty()) {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        for (auto& j : deferred)
            load_queue_.insert(load_queue_.begin(), std::move(j));
    }
}

Streamer::Stats Streamer::stats() const {
    int nodes = 0;
    for (const auto& kv : loaded_) nodes += static_cast<int>(kv.second.nodes.size());
    return {static_cast<int>(loaded_.size()), nodes};
}

} // namespace pengine
