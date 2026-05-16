#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "render/mesh.h"
#include "scene/aabb.h"
#include "world/cell_coord.h"
#include "world/instance_def.h"
#include "world/terrain.h"
#include "world/world_defs.h"

namespace pengine {

class ModelRegistry;
class RoadGraph;
class Scene;
class SceneNode;
class Texture;
class WorldCollision;

class Streamer {
public:
    struct Stats {
        int loaded_cells = 0;
        int total_nodes  = 0;
    };

    // `registry`     — owns model defs (mesh, texture, bounds) referenced by IPL.
    // `terrain_tex`  — texture used for the per-cell heightmap mesh (step 3 removes this).
    // `cell_cache`   — directory for IPL files. Empty = "<ASSETS_DIR>/world/cells".
    //                  Cells without an IPL are generated procedurally and saved here.
    void init(const WorldConfig& cfg, Scene* scene,
              WorldCollision* collision, const ModelRegistry* registry,
              const Texture* terrain_tex,
              RoadGraph* road_graph = nullptr,
              std::filesystem::path cell_cache = {});
    void shutdown();

    void pump(glm::vec3 cam_pos);

    Stats stats() const;

    // Result of a geometric pick. Returned by `query_instance_at` when a
    // streamed instance's world-space AABB contains the query XZ point.
    // `cell` is the cell that owned the instance at activation time.
    // PBD-032: `instance_index` is the position in `loaded_[cell].instances`
    // at pick time. Stable only until the next mutation of that cell
    // (`pump()`, `add_instance`, `remove_instance`). The Map Builder uses
    // it to feed `remove_instance` on the same frame as the pick, before
    // any other mutation can run, so the staleness window is closed.
    struct PickResult {
        bool        hit            = false;
        CellCoord   cell           {};
        InstanceDef instance       {};   // copy — caller can outlive the streamer
        AABB        world_aabb     {};   // post-transform world-space bounds
        std::size_t instance_index = 0;  // valid iff `hit`
    };

    // Find the topmost streamed instance whose world-space AABB contains
    // (world_x, *, world_z). "Topmost" = the one with the highest aabb.max.y,
    // which under the top-down inspector camera reads as "the thing the cursor
    // is over." Safe to call from the main thread: only `pump()` mutates
    // `loaded_`, and `pump()` is itself main-thread-only.
    PickResult query_instance_at(float world_x, float world_z) const;

    // PBD-031: append a single InstanceDef to an already-loaded cell at
    // runtime (Map Builder placement path). Main-thread-only — the loader
    // thread doesn't touch `loaded_`; only `pump()` does, and callers of
    // `add_instance` are expected to be on the same thread (same invariant
    // as `query_instance_at`).
    //
    // Parallel-array invariant: every entry in `LoadedCell` lives across
    // three pieces — `nodes` (visible scene node), `instances` (the IPL
    // record itself), `instance_world_aabbs` (post-transform world-space
    // bounds for picking). All three are appended together so the new
    // instance is immediately pickable by `query_instance_at`. If the model
    // carries the Building flag we also forward the world AABB to
    // `WorldCollision::add_building` so freshly placed buildings collide.
    //
    // Returns true on success, false if (a) the cell isn't currently loaded
    // (placement off-screen / over an unloaded cell), or (b) the model id
    // can't be resolved through the registry. Caller can use the return to
    // gate UI feedback.
    //
    // Persistence: this only mutates the in-memory `loaded_` state. The
    // on-disk IPL is unchanged, so the placement is lost on the next
    // evict-then-reload of the cell. PBD-033 fixes that.
    bool add_instance(CellCoord cell, const InstanceDef& inst);

    // PBD-032: remove a single InstanceDef from a loaded cell at runtime
    // (Map Builder delete path). Inverse of `add_instance`; same threading
    // invariants (main thread only). `index` is into `loaded_[cell].instances`
    // — typically obtained from `query_instance_at().instance_index`.
    //
    // Parallel-array invariant: removes from `nodes`, `instances`, and
    // `instance_world_aabbs` in lockstep using swap-and-pop so each array's
    // size shrinks by exactly one and no other index moves except the last.
    // Also tears down the scene node (`Scene::remove_node`) and, if the
    // model carried the Building flag, drops the corresponding world AABB
    // from `WorldCollision` via the new `remove_building`.
    //
    // Returns false if the cell isn't loaded or the index is out of range.
    // Persistence: same caveat as `add_instance` — only the in-memory
    // `loaded_[cell]` is changed; on next evict-reload the on-disk IPL
    // wins. PBD-033 closes that.
    bool remove_instance(CellCoord cell, std::size_t index);

    // PBD-033 / PBD-048: persistence. A cell becomes "dirty" the moment Map
    // Builder mutates it (add_instance / remove_instance, and eventually
    // update_instance in Phase B). Eviction in `pump()` calls
    // `save_dirty_cell` before tearing the cell down; `save_all_dirty_cells`
    // is the exit-from-MapBuilder hook.
    //
    // **Threading split (PBD-048):**
    //   * `save_dirty_cell` is now ASYNCHRONOUS. It snapshots
    //     `lc.instances` into a `SaveJob` queue under `queue_mutex_` and
    //     clears `dirty` immediately. The background `thread_func` drains
    //     the queue and writes via `save_ipl`. This keeps disk I/O off the
    //     render thread on cell-jump teleports (up to `(2r+1)^2` evictions
    //     per pump).
    //   * `save_all_dirty_cells` stays SYNCHRONOUS by deliberate choice.
    //     It's a one-shot, called from `Application::enter_app_state` when
    //     leaving MapBuilder; N is small (currently-loaded cells, ≤9 in
    //     typical play); and the "exit" semantics want a hard guarantee
    //     edits are on disk before the next state takes over. Routing it
    //     through the async queue would either lose that guarantee or
    //     require a join-on-shutdown style wait that defeats the point of
    //     having the worker. So this one keeps direct `save_ipl` calls.
    //
    // Both are main-thread-only (same invariant as
    // `add_instance` / `remove_instance`).
    //
    // **Snapshot invariant:** `save_dirty_cell` value-copies `lc.instances`
    // into the save job before enqueueing. The worker must not hold a
    // reference into `loaded_`; a subsequent `add_instance` /
    // `remove_instance` would invalidate it (vector reallocation +
    // swap-and-pop both move elements).
    //
    // **Shutdown drain:** `thread_func` drains any pending save jobs
    // before exiting once `running_` is cleared, so app exit through
    // `shutdown()` doesn't silently drop already-enqueued edits.
    void save_dirty_cell(CellCoord cell);
    void save_all_dirty_cells();

    // PBD-034: snapshot of currently-loaded cell coordinates. Used by the
    // Map Builder undo/redo stack to detect mid-frame evictions: each frame
    // it compares this set against last frame's, and any cell that dropped
    // out must have its pending undo/redo entries cleared (the streamer no
    // longer holds the live instance state those entries refer to).
    // Main-thread-only; same invariant as `add_instance` / `remove_instance`.
    std::vector<CellCoord> loaded_cell_coords() const;

private:
    void thread_func();

    struct LoadJob {
        CellCoord                coord;
        std::vector<InstanceDef> instances;
        std::vector<AABB>        building_aabbs;
        TerrainChunk             terrain;
    };
    struct EvictJob { CellCoord coord; };
    // PBD-048: async save job. `instances` is a value-copy snapshot of the
    // cell's `lc.instances` taken at enqueue time on the main thread. The
    // worker thread writes `instances` to `<cell_cache>/cell_X_Z.ipl` via
    // `save_ipl`. By the time the worker sees the job, the main thread may
    // have further mutated `loaded_[coord].instances` — that's fine; the
    // snapshot is what gets persisted, and the next dirty/evict cycle will
    // enqueue a fresh snapshot.
    struct SaveJob {
        CellCoord                coord;
        std::vector<InstanceDef> instances;
    };

    LoadJob load_or_generate_cell(CellCoord coord) const;

    // PBD-048: shared by the worker thread (queue drain) and `shutdown()`
    // (final drain after `running_` is cleared, before join). The main
    // thread itself never calls this — main-thread saves go through the
    // synchronous `save_all_dirty_cells` path with a direct `save_ipl`.
    void write_save_job(const SaveJob& job) const;

    mutable std::mutex    queue_mutex_;
    std::vector<LoadJob>  load_queue_;
    std::vector<EvictJob> evict_queue_;
    std::vector<SaveJob>  save_queue_;

    std::mutex  cam_mutex_;
    glm::vec3   cam_pos_ = {0.f, 0.f, 0.f};

    struct LoadedCell {
        std::vector<SceneNode*> nodes;
        Mesh                    terrain_mesh;
        // PBD-026: keep the original IPL records and their world-space AABBs
        // alongside the live scene nodes so the inspector can pick by XZ and
        // surface model_id/lod_pair/etc. Parallel arrays: `instances[i]`
        // corresponds to `instance_world_aabbs[i]`. The terrain pseudo-node
        // is intentionally not represented here (no model_id, not pickable).
        std::vector<InstanceDef> instances;
        std::vector<AABB>        instance_world_aabbs;
        // PBD-033: set by `add_instance`/`remove_instance` to mark the cell
        // as having un-persisted edits. Eviction in `pump()` saves and
        // clears this before tearing down the cell. `save_all_dirty_cells()`
        // (Esc-to-DevToolsMenu) also clears on save.
        bool                     dirty = false;
    };
    std::unordered_map<CellCoord, LoadedCell, CellCoordHash> loaded_;

    WorldConfig          cfg_;
    Scene*               scene_       = nullptr;
    WorldCollision*      collision_   = nullptr;
    const ModelRegistry* registry_    = nullptr;
    const Texture*       terrain_tex_ = nullptr;
    RoadGraph*           road_graph_  = nullptr;
    std::filesystem::path cell_cache_;

    std::atomic<bool> running_{false};
    std::thread       thread_;
};

}  // namespace pengine
