#pragma once

#include <memory>
#include <vector>

#include "scene/frustum.h"
#include "scene/scene_node.h"

namespace pengine {

class Shader;

class Scene {
public:
    // Create and register a new node. Returns a non-owning pointer.
    SceneNode* create_node(SceneNode* parent = nullptr);

    // Remove a node (and detach from its parent). O(n) — acceptable at streaming scale.
    void remove_node(SceneNode* node);

    // Propagate dirty flags and recompute world transforms top-down.
    void update();

    struct CullResult {
        std::vector<const SceneNode*> visible;
        int total  = 0;
        int culled = 0;
    };

    CullResult cull(const Frustum& frustum) const;

    // Convenience: draw all visible renderables.
    // Caller must have bound the shader and set VP + light uniforms.
    // Sets u_model and u_normal_mat per draw.
    void draw(const CullResult& cr, Shader& shader) const;

private:
    std::vector<std::unique_ptr<SceneNode>> nodes_;
    std::vector<SceneNode*>                 roots_; // nodes with no parent
};

} // namespace pengine
