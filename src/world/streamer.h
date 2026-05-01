#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "world/cell_coord.h"
#include "world/world_defs.h"

namespace pengine {

class Mesh;
class Scene;
class SceneNode;

class Streamer {
public:
    struct Stats {
        int loaded_cells = 0;
        int total_nodes  = 0;
    };

    void init(const WorldConfig& cfg, Scene* scene, const Mesh* cube_mesh);
    void shutdown();

    // Main thread: call once per frame before scene.update().
    // Drains load/evict queues, caps uploads to cfg_.max_uploads_per_frame.
    void pump(glm::vec3 cam_pos);

    Stats stats() const;

private:
    // Stream thread: runs continuously, ~100 ms sleep between iterations.
    void thread_func();

    // Deterministic procedural cell generation — in a real engine this reads disk.
    static void generate_cell(CellCoord coord, const WorldConfig& cfg,
                               std::vector<ObjectDef>& out);

    // ---- Shared state (all protected by queue_mutex_) ----------------------
    struct LoadJob  { CellCoord coord; std::vector<ObjectDef> defs; };
    struct EvictJob { CellCoord coord; };

    mutable std::mutex    queue_mutex_;
    std::vector<LoadJob>  load_queue_;
    std::vector<EvictJob> evict_queue_;

    // ---- Camera position (protected by cam_mutex_) -------------------------
    std::mutex  cam_mutex_;
    glm::vec3   cam_pos_ = {0.f, 0.f, 0.f};

    // ---- Main-thread-only state --------------------------------------------
    struct LoadedCell { std::vector<SceneNode*> nodes; };
    std::unordered_map<CellCoord, LoadedCell, CellCoordHash> loaded_;

    // ---- Config + deps (init once, then read-only) -------------------------
    WorldConfig   cfg_;
    Scene*        scene_      = nullptr;
    const Mesh*   cube_mesh_  = nullptr;

    // ---- Thread lifecycle --------------------------------------------------
    std::atomic<bool> running_{false};
    std::thread       thread_;
};

} // namespace pengine
