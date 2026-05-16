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
    struct PickResult {
        bool        hit      = false;
        CellCoord   cell     {};
        InstanceDef instance {};       // copy — caller can outlive the streamer
        AABB        world_aabb {};     // post-transform world-space bounds
    };

    // Find the topmost streamed instance whose world-space AABB contains
    // (world_x, *, world_z). "Topmost" = the one with the highest aabb.max.y,
    // which under the top-down inspector camera reads as "the thing the cursor
    // is over." Safe to call from the main thread: only `pump()` mutates
    // `loaded_`, and `pump()` is itself main-thread-only.
    PickResult query_instance_at(float world_x, float world_z) const;

private:
    void thread_func();

    struct LoadJob {
        CellCoord                coord;
        std::vector<InstanceDef> instances;
        std::vector<AABB>        building_aabbs;
        TerrainChunk             terrain;
    };
    struct EvictJob { CellCoord coord; };

    LoadJob load_or_generate_cell(CellCoord coord) const;

    mutable std::mutex    queue_mutex_;
    std::vector<LoadJob>  load_queue_;
    std::vector<EvictJob> evict_queue_;

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
