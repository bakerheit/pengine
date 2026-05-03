#include "world/streamer.h"

#include <chrono>
#include <cmath>
#include <random>

#include "core/log.h"
#include "physics/world_collision.h"
#include "render/mesh.h"
#include "scene/scene.h"
#include "scene/scene_node.h"
#include "world/city_layout.h"
#include "world/heightmap.h"
#include "world/ipl_loader.h"
#include "world/model_def.h"
#include "world/model_registry.h"
#include "world/road_graph.h"

#ifndef ASSETS_DIR
#define ASSETS_DIR "assets"
#endif

namespace pengine {

// ---------------------------------------------------------------------------
// Cell load: try IPL on disk; on miss, generate procedurally and save.
// ---------------------------------------------------------------------------

Streamer::LoadJob Streamer::load_or_generate_cell(CellCoord coord) const {
    LoadJob job;
    job.coord = coord;

    auto ipl = ipl_path_for_cell(cell_cache_, coord);
    bool loaded = std::filesystem::exists(ipl) && load_ipl(ipl, job.instances);

    if (!loaded) {
        CityCellLayout layout = generate_city_cell(coord, cfg_.cell_size);
        job.instances      = std::move(layout.instances);
        job.building_aabbs = std::move(layout.collisions);
        save_ipl(ipl, job.instances);
    } else {
        // Reconstitute building collision AABBs from instances. Building model
        // ids are 11..15 (per world_model_ids.h). Step 5 will move collision
        // off this side-channel onto ModelDef.
        for (const InstanceDef& inst : job.instances) {
            const ModelDef* m = registry_->get(inst.model_id);
            if (!m || (m->flags & ModelFlag::Building) == 0) continue;
            // Local cube bounds are [-0.5, +0.5]; transform to world.
            AABB local{m->local_bounds.min, m->local_bounds.max};
            job.building_aabbs.push_back(local.transform(inst.transform.matrix()));
        }
    }

    float ox = static_cast<float>(coord.x) * cfg_.cell_size;
    float oz = static_cast<float>(coord.z) * cfg_.cell_size;
    job.terrain = TerrainChunk::build(ox, oz, cfg_.cell_size);
    return job;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void Streamer::init(const WorldConfig& cfg, Scene* scene,
                     WorldCollision* collision, const ModelRegistry* registry,
                     const Texture* terrain_tex,
                     RoadGraph* road_graph,
                     std::filesystem::path cell_cache) {
    cfg_         = cfg;
    scene_       = scene;
    collision_   = collision;
    registry_    = registry;
    terrain_tex_ = terrain_tex;
    road_graph_  = road_graph;
    cell_cache_  = cell_cache.empty()
                    ? std::filesystem::path(ASSETS_DIR) / "world" / "cells"
                    : std::move(cell_cache);

    std::error_code ec;
    std::filesystem::create_directories(cell_cache_, ec);

    if (scene_) scene_->set_static_cell_size(cfg_.cell_size);
    running_.store(true, std::memory_order_release);
    thread_ = std::thread(&Streamer::thread_func, this);
    PE_INFO("Streamer started  cell_size=%.0f radius=%d  world=%dx%d cells  cache=%s",
            cfg_.cell_size, cfg_.stream_radius,
            cfg_.world_cells_x, cfg_.world_cells_z,
            cell_cache_.string().c_str());
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
                new_loads.push_back(load_or_generate_cell(c));
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
        scene_->remove_static_cell(job.coord);
        for (SceneNode* n : it->second.nodes) scene_->remove_node(n);
        if (collision_)  collision_->remove_cell(job.coord);
        if (road_graph_) road_graph_->remove_cell(job.coord);
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

        // Instances — registered as static nodes in this cell's bucket.
        for (const InstanceDef& inst : job.instances) {
            const ModelDef* m = registry_ ? registry_->get(inst.model_id) : nullptr;
            if (!m || !m->mesh) {
                PE_WARN("Streamer: cell (%d,%d) skipped instance with unresolved model id %u",
                        job.coord.x, job.coord.z, inst.model_id);
                continue;
            }
            SceneNode* n = scene_->create_node_static(nullptr, job.coord);
            n->transform = inst.transform;
            glm::vec2 uv = (inst.uv_scale_override.x > 0.f || inst.uv_scale_override.y > 0.f)
                            ? inst.uv_scale_override : m->uv_scale;
            n->renderable = Renderable{m->mesh, m->local_bounds, m->tint, uv, m->texture};
            n->mark_dirty();
            lc.nodes.push_back(n);
        }

        // Terrain (step 3 removes this entirely; ground will become more instances).
        lc.terrain_mesh.upload(job.terrain.verts, job.terrain.indices);
        AABB terrain_aabb;
        terrain_aabb.min = lc.terrain_mesh.bounds_min();
        terrain_aabb.max = lc.terrain_mesh.bounds_max();
        SceneNode* tnode = scene_->create_node_static(nullptr, job.coord);
        tnode->renderable = Renderable{nullptr, terrain_aabb,
                                        glm::vec3{1.f, 1.f, 1.f},
                                        glm::vec2{4.f, 4.f}, terrain_tex_};
        tnode->mark_dirty();
        lc.nodes.push_back(tnode);

        if (collision_)  collision_->add_cell(job.coord, std::move(job.building_aabbs));
        if (road_graph_) road_graph_->add_cell(job.coord);

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

}  // namespace pengine
