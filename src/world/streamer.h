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
