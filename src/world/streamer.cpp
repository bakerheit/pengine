#include "world/streamer.h"

#include <chrono>
#include <random>
#include <cmath>

#include "core/log.h"
#include "physics/world_collision.h"
#include "render/mesh.h"
#include "scene/scene.h"
#include "scene/scene_node.h"
#include "world/city_layout.h"
#include "world/heightmap.h"

namespace pengine {

// ---------------------------------------------------------------------------
// Procedural cell generation
// ---------------------------------------------------------------------------

void Streamer::generate_cell(CellCoord coord, const WorldConfig& cfg,
                              std::vector<ObjectDef>& out_defs,
                              std::vector<AABB>&      out_aabbs) {
    // Cell ground height: sample the (already cell-flattened) heightmap at
    // the cell centre.
    float ox = static_cast<float>(coord.x) * cfg.cell_size;
    float oz = static_cast<float>(coord.z) * cfg.cell_size;
    float ground_y = Heightmap::sample(ox + cfg.cell_size * 0.5f,
                                        oz + cfg.cell_size * 0.5f);

    CityCellLayout layout = generate_city_cell(coord, cfg.cell_size, ground_y);
    out_defs  = std::move(layout.visuals);
    out_aabbs = std::move(layout.collisions);
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Streamer::init(const WorldConfig& cfg, Scene* scene, const Mesh* cube_mesh,
                     WorldCollision* collision) {
    cfg_       = cfg;
    scene_     = scene;
    cube_mesh_ = cube_mesh;
    collision_ = collision;
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
    std::unordered_set<CellCoord, CellCoordHash> known_loaded;

    while (running_.load(std::memory_order_acquire)) {
        glm::vec3 pos;
        {
            std::lock_guard<std::mutex> lk(cam_mutex_);
            pos = cam_pos_;
        }

        CellCoord cam_cell = world_to_cell(pos.x, pos.z, cfg_.cell_size);

        std::unordered_set<CellCoord, CellCoordHash> desired;
        int r = cfg_.stream_radius;
        for (int dz = -r; dz <= r; ++dz) {
            for (int dx = -r; dx <= r; ++dx) {
                CellCoord c{cam_cell.x + dx, cam_cell.z + dz};
                if (c.x < 0 || c.x >= cfg_.world_cells_x) continue;
                if (c.z < 0 || c.z >= cfg_.world_cells_z) continue;
                desired.insert(c);
            }
        }

        std::vector<LoadJob> new_loads;
        for (const CellCoord& c : desired) {
            if (known_loaded.count(c) == 0) {
                LoadJob job;
                job.coord = c;
                generate_cell(c, cfg_, job.defs, job.building_aabbs);
                float ox = static_cast<float>(c.x) * cfg_.cell_size;
                float oz = static_cast<float>(c.z) * cfg_.cell_size;
                job.terrain = TerrainChunk::build(ox, oz, cfg_.cell_size);
                new_loads.push_back(std::move(job));
                known_loaded.insert(c);
            }
        }

        std::vector<EvictJob> new_evicts;
        for (auto it = known_loaded.begin(); it != known_loaded.end(); ) {
            if (desired.count(*it) == 0) {
                new_evicts.push_back({*it});
                it = known_loaded.erase(it);
            } else {
                ++it;
            }
        }

        if (!new_loads.empty() || !new_evicts.empty()) {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            for (auto& j : new_loads)  load_queue_.push_back(std::move(j));
            for (auto& j : new_evicts) evict_queue_.push_back(j);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// ---------------------------------------------------------------------------
// Main-thread pump
// ---------------------------------------------------------------------------

void Streamer::pump(glm::vec3 cam_pos) {
    {
        std::lock_guard<std::mutex> lk(cam_mutex_);
        cam_pos_ = cam_pos;
    }

    std::vector<LoadJob>  loads;
    std::vector<EvictJob> evicts;
    {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        loads.swap(load_queue_);
        evicts.swap(evict_queue_);
    }

    for (const EvictJob& job : evicts) {
        auto it = loaded_.find(job.coord);
        if (it == loaded_.end()) continue;
        for (SceneNode* n : it->second.nodes) scene_->remove_node(n);
        if (collision_) collision_->remove_cell(job.coord);
        loaded_.erase(it);
    }

    int activated = 0;
    std::vector<LoadJob> deferred;
    for (auto& job : loads) {
        if (activated >= cfg_.max_uploads_per_frame) {
            deferred.push_back(std::move(job));
            continue;
        }
        if (loaded_.count(job.coord)) continue;

        LoadedCell lc;

        // Buildings.
        AABB cube_aabb;
        cube_aabb.min = cube_mesh_->bounds_min();
        cube_aabb.max = cube_mesh_->bounds_max();
        for (const ObjectDef& def : job.defs) {
            SceneNode* n = scene_->create_node();
            n->transform  = def.transform;
            n->renderable = Renderable{cube_mesh_, cube_aabb, def.tint};
            n->mark_dirty();
            lc.nodes.push_back(n);
        }

        // Terrain. Upload first; SceneNode mesh pointer is patched after the
        // LoadedCell lands in `loaded_` (the move would invalidate any pointer
        // taken before insertion).
        lc.terrain_mesh.upload(job.terrain.verts, job.terrain.indices);
        AABB terrain_aabb;
        terrain_aabb.min = lc.terrain_mesh.bounds_min();
        terrain_aabb.max = lc.terrain_mesh.bounds_max();
        SceneNode* tnode = scene_->create_node();
        tnode->renderable = Renderable{nullptr, terrain_aabb,
                                        glm::vec3{0.55f, 0.58f, 0.50f}};
        tnode->mark_dirty();
        lc.nodes.push_back(tnode);

        if (collision_) collision_->add_cell(job.coord, std::move(job.building_aabbs));

        auto inserted = loaded_.emplace(job.coord, std::move(lc));
        tnode->renderable->mesh = &inserted.first->second.terrain_mesh;
        ++activated;
    }

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
