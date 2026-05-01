#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "render/mesh.h"
#include "scene/aabb.h"
#include "world/cell_coord.h"
#include "world/terrain.h"
#include "world/world_defs.h"

namespace pengine {

class Scene;
class SceneNode;
class Texture;
class WorldCollision;

struct WorldTextures {
    const Texture* terrain  = nullptr;  // grass for terrain mesh
    const Texture* road     = nullptr;  // asphalt for road slabs
    const Texture* building = nullptr;  // facade with windows
};

class Streamer {
public:
    struct Stats {
        int loaded_cells = 0;
        int total_nodes  = 0;
    };

    void init(const WorldConfig& cfg, Scene* scene, const Mesh* cube_mesh,
              WorldCollision* collision, const WorldTextures& tex);
    void shutdown();

    void pump(glm::vec3 cam_pos);

    Stats stats() const;

private:
    void thread_func();

    void generate_cell(CellCoord coord, const WorldConfig& cfg,
                        std::vector<ObjectDef>& out_defs,
                        std::vector<AABB>&      out_aabbs) const;

    struct LoadJob {
        CellCoord                coord;
        std::vector<ObjectDef>   defs;
        std::vector<AABB>        building_aabbs;
        TerrainChunk             terrain;
    };
    struct EvictJob { CellCoord coord; };

    mutable std::mutex    queue_mutex_;
    std::vector<LoadJob>  load_queue_;
    std::vector<EvictJob> evict_queue_;

    std::mutex  cam_mutex_;
    glm::vec3   cam_pos_ = {0.f, 0.f, 0.f};

    struct LoadedCell {
        std::vector<SceneNode*> nodes;          // building nodes + terrain node
        Mesh                    terrain_mesh;
    };
    std::unordered_map<CellCoord, LoadedCell, CellCoordHash> loaded_;

    WorldConfig      cfg_;
    Scene*           scene_      = nullptr;
    const Mesh*      cube_mesh_  = nullptr;
    WorldCollision*  collision_  = nullptr;
    WorldTextures    tex_;

    std::atomic<bool> running_{false};
    std::thread       thread_;
};

} // namespace pengine
