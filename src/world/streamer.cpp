#include "world/streamer.h"

#include <cfloat>
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
        // PBD-033: persist any Map Builder edits before tearing the cell
        // down. After this point the in-memory state is gone, so any cell
        // marked dirty must be flushed to its IPL or the edits vanish on
        // reload. `save_dirty_cell` is a no-op for clean cells.
        save_dirty_cell(job.coord);
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

            // PBD-026: keep the original IPL record + a world-space AABB so
            // the Map Builder inspector can pick by XZ without walking
            // SceneNode (which doesn't carry model_id). We compute the AABB
            // once at activation; instances are static so the transform
            // doesn't change after this point.
            AABB local{m->local_bounds.min, m->local_bounds.max};
            lc.instances.push_back(inst);
            lc.instance_world_aabbs.push_back(local.transform(inst.transform.matrix()));
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

// PBD-034: snapshot the currently-loaded cell coordinates so the Map Builder
// can compute the per-frame evict delta. Same thread invariant as the other
// `loaded_`-readers — caller is expected to be on the main thread post-pump.
std::vector<CellCoord> Streamer::loaded_cell_coords() const {
    std::vector<CellCoord> out;
    out.reserve(loaded_.size());
    for (const auto& kv : loaded_) out.push_back(kv.first);
    return out;
}

// PBD-026: geometric instance pick for the Map Builder inspector.
//
// Thread safety: `loaded_` is only mutated inside `pump()`, which is itself
// main-thread-only. The background loader thread only touches
// `load_queue_`/`evict_queue_` (under `queue_mutex_`) and `cam_pos_` (under
// `cam_mutex_`). Callers on the main thread (after `pump()` returns) therefore
// see a stable `loaded_` and don't need to hold a lock.
//
// Algorithm: only the cell containing the XZ point and its 8 neighbours can
// hold an AABB that overlaps the point (instances rarely cross cell
// boundaries, but a large building anchored near an edge can — checking
// neighbours is cheap insurance and matches the "stream by Chebyshev radius"
// model). For each instance whose XZ extent contains the point, keep the one
// with the highest `aabb.max.y` (topmost). That matches the inspector's
// mental model under the near-vertical camera.
// PBD-031: Map Builder placement entry point. Mirrors the per-instance work
// `pump()` does inside its load loop, but for a single InstanceDef into an
// already-loaded cell. See header for full invariants.
bool Streamer::add_instance(CellCoord cell, const InstanceDef& inst) {
    auto it = loaded_.find(cell);
    if (it == loaded_.end()) return false;

    const ModelDef* m = registry_ ? registry_->get(inst.model_id) : nullptr;
    if (!m || !m->mesh) {
        PE_WARN("Streamer::add_instance: cell (%d,%d) unresolved model id %u",
                cell.x, cell.z, inst.model_id);
        return false;
    }

    LoadedCell& lc = it->second;

    // Visible scene node — mirrors the per-instance work inside `pump()`.
    SceneNode* n = scene_->create_node_static(nullptr, cell);
    n->transform = inst.transform;
    glm::vec2 uv = (inst.uv_scale_override.x > 0.f || inst.uv_scale_override.y > 0.f)
                    ? inst.uv_scale_override : m->uv_scale;
    n->renderable = Renderable{m->mesh, m->local_bounds, m->tint, uv, m->texture};
    n->mark_dirty();
    lc.nodes.push_back(n);

    // Pick metadata (parallel-array invariant: `instances[i]` <->
    // `instance_world_aabbs[i]`).
    AABB local{m->local_bounds.min, m->local_bounds.max};
    AABB world = local.transform(inst.transform.matrix());
    lc.instances.push_back(inst);
    lc.instance_world_aabbs.push_back(world);

    // Collision: buildings only. Roads/walks/props don't participate today;
    // matches the `load_or_generate_cell` filter (Building flag → AABB).
    if (collision_ && (m->flags & ModelFlag::Building) != 0) {
        collision_->add_building(cell, world);
    }

    // PBD-033: mark the cell as having un-persisted edits. The next evict
    // (or `save_all_dirty_cells` from the MapBuilder exit) will flush this
    // to disk. Setting after the success path so a failed add doesn't
    // dirty the cell unnecessarily.
    lc.dirty = true;

    return true;
}

// PBD-032: Map Builder delete entry point. Mirror of `add_instance` — same
// thread invariants, same parallel-array discipline.
//
// Swap-and-pop is the right shape here: it's O(1), keeps the three parallel
// arrays consistent in a single conceptual step, and only the last element's
// index changes (the caller uses the picked index *this same frame* before
// any other mutation, so we don't need to keep arbitrary indices stable
// across the operation).
bool Streamer::remove_instance(CellCoord cell, std::size_t index) {
    auto it = loaded_.find(cell);
    if (it == loaded_.end()) return false;
    LoadedCell& lc = it->second;
    if (index >= lc.instances.size()) return false;

    // Capture the data we need *before* mutating, so collision teardown
    // can match against the same AABB we registered at add time.
    const AABB world_aabb = lc.instance_world_aabbs[index];
    SceneNode* node       = lc.nodes[index];
    const uint32_t mid    = lc.instances[index].model_id;

    // Parallel-array swap-and-pop. All three arrays grow/shrink together
    // (PBD-031 invariant); preserve that by doing the same swap on each
    // before popping. If `index` is already the last slot, swap is a
    // no-op and we just pop.
    const std::size_t last = lc.instances.size() - 1;
    if (index != last) {
        std::swap(lc.nodes[index],                lc.nodes[last]);
        std::swap(lc.instances[index],            lc.instances[last]);
        std::swap(lc.instance_world_aabbs[index], lc.instance_world_aabbs[last]);
    }
    lc.nodes.pop_back();
    lc.instances.pop_back();
    lc.instance_world_aabbs.pop_back();

    // Scene node teardown. `remove_node` walks the static buckets too
    // (PBD-032 change) so the cell's static-bucket entry is dropped.
    if (scene_ && node) scene_->remove_node(node);

    // Collision: only buildings registered an AABB on the way in. Mirror
    // that filter here so we don't try to remove an AABB that was never
    // added.
    if (collision_) {
        const ModelDef* m = registry_ ? registry_->get(mid) : nullptr;
        if (m && (m->flags & ModelFlag::Building) != 0) {
            collision_->remove_building(cell, world_aabb);
        }
    }

    // PBD-033: a delete is an edit. Mirror the add_instance dirty hook so
    // the deletion makes it to disk on evict.
    lc.dirty = true;

    return true;
}

// PBD-033: persist a single cell's instances to its on-disk IPL. No-op if
// the cell isn't loaded or isn't dirty (clean cells haven't been edited
// since the last save / load, so the disk copy is already authoritative).
// Clears the dirty flag on success so a subsequent evict doesn't re-write
// the same data.
void Streamer::save_dirty_cell(CellCoord cell) {
    auto it = loaded_.find(cell);
    if (it == loaded_.end()) return;
    LoadedCell& lc = it->second;
    if (!lc.dirty) return;

    auto path = ipl_path_for_cell(cell_cache_, cell);
    if (save_ipl(path, lc.instances)) {
        lc.dirty = false;
        PE_INFO("Streamer: saved cell (%d,%d) -> %s  (%zu instances)",
                cell.x, cell.z, path.string().c_str(), lc.instances.size());
    } else {
        // Leave dirty=true so a later evict / exit retries. The save_ipl
        // path already logs a warning on open failure; no further log here.
    }
}

// PBD-033: flush all currently-loaded dirty cells. Called from
// `enter_app_state` when leaving MapBuilder so end-of-session edits
// survive even when no eviction triggers (e.g. user edits a cell and
// hits Esc without moving the camera).
void Streamer::save_all_dirty_cells() {
    for (auto& kv : loaded_) {
        if (kv.second.dirty) save_dirty_cell(kv.first);
    }
}

Streamer::PickResult Streamer::query_instance_at(float wx, float wz) const {
    PickResult best;
    float       best_top = -FLT_MAX;

    CellCoord c0 = world_to_cell(wx, wz, cfg_.cell_size);
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            CellCoord c{c0.x + dx, c0.z + dz};
            auto it = loaded_.find(c);
            if (it == loaded_.end()) continue;
            const LoadedCell& lc = it->second;
            const std::size_t n = lc.instance_world_aabbs.size();
            for (std::size_t i = 0; i < n; ++i) {
                const AABB& a = lc.instance_world_aabbs[i];
                if (wx < a.min.x || wx > a.max.x) continue;
                if (wz < a.min.z || wz > a.max.z) continue;
                if (a.max.y > best_top) {
                    best_top            = a.max.y;
                    best.hit            = true;
                    best.cell           = c;
                    best.instance       = lc.instances[i];
                    best.world_aabb     = a;
                    best.instance_index = i;
                }
            }
        }
    }
    return best;
}

}  // namespace pengine
