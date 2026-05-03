#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "scene/frustum.h"
#include "scene/scene_node.h"
#include "world/cell_coord.h"

namespace pengine {

class Shader;

class Scene {
public:
    // ---- Dynamic nodes (default) ------------------------------------------
    // Anything that moves each frame — cars, character, wheels. Always culled
    // with a linear scan (kept small, <500 nodes total).
    SceneNode* create_node(SceneNode* parent = nullptr);

    // ---- Static nodes (cell-bucketed) -------------------------------------
    // Buildings, terrain, traffic lights — geometry that never moves after
    // placement. Registered into a per-cell bucket so cull() can skip entire
    // cells that are outside the camera frustum.
    //
    // `cell` is the CellCoord the node belongs to. The cell's world AABB is
    // derived analytically from the grid coordinate and the configured cell
    // size — no per-node union is needed, so the AABB is always valid.
    SceneNode* create_node_static(SceneNode* parent, CellCoord cell);

    // Evict the entire bucket for a cell (used by Streamer when a cell unloads).
    // Caller is still responsible for calling remove_node() on each node for
    // proper ownership cleanup.
    void remove_static_cell(CellCoord cell) { static_buckets_.erase(cell); }

    // Configure the world cell size so that cell AABBs are computed correctly.
    // Call once after WorldConfig is known (before any create_node_static).
    void set_static_cell_size(float s) { static_cell_size_ = s; }

    // Remove a node (and detach from its parent/bucket). O(n) in bucket/list size.
    void remove_node(SceneNode* node);

    // Propagate dirty flags and recompute world transforms top-down.
    void update();

    struct CullResult {
        std::vector<const SceneNode*> visible;
        int total  = 0;
        int culled = 0;
    };

    // Two-tier cull: skip whole cells first (cheap AABB test), then check
    // individual nodes in passing cells. Dynamic nodes always checked linearly.
    CullResult cull(const Frustum& frustum) const;

    // Convenience: draw all visible renderables.
    // Caller must have bound the shader and set VP + light uniforms.
    // Sets u_model and u_normal_mat per draw.
    void draw(const CullResult& cr, Shader& shader) const;

private:
    std::vector<std::unique_ptr<SceneNode>> nodes_; // ownership
    std::vector<SceneNode*>                 roots_; // dynamic root nodes

    // Static geometry buckets.
    std::unordered_map<CellCoord, std::vector<SceneNode*>, CellCoordHash>
        static_buckets_;
    float static_cell_size_ = 256.f;

    // Dynamic nodes (cars, character, wheel children) — linear scan always.
    std::vector<SceneNode*> dynamic_nodes_;

    // Static nodes that need their initial world matrix computed. Flushed once
    // per cell-load inside Scene::update(), then cleared. Static nodes are never
    // added to roots_ — they don't move, so re-visiting them each frame is waste.
    std::vector<SceneNode*> pending_static_;
};

} // namespace pengine
