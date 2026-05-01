#pragma once

#include <vector>
#include <optional>

#include "scene/aabb.h"
#include "scene/transform.h"

namespace pengine {

class Mesh;
class Shader;

struct Renderable {
    const Mesh* mesh       = nullptr;
    AABB        local_aabb;       // in node-local space
};

class SceneNode {
public:
    SceneNode() = default;

    SceneNode(const SceneNode&)            = delete;
    SceneNode& operator=(const SceneNode&) = delete;

    Transform transform;
    std::optional<Renderable> renderable;
    bool      visible = true;

    // Hierarchy (non-owning pointers; Scene owns the nodes).
    SceneNode*              parent   = nullptr;
    std::vector<SceneNode*> children;

    // Mark this node and all descendants dirty (world transform needs recompute).
    void mark_dirty();

    // Recompute world transform if dirty (recurses up to parent first).
    void update();

    const glm::mat4& world_matrix() const { return world_mat_; }
    const AABB&      world_aabb()   const { return world_aabb_; }
    bool             is_dirty()     const { return dirty_; }

private:
    mutable glm::mat4 world_mat_  = glm::mat4{1.f};
    mutable AABB      world_aabb_;
    mutable bool      dirty_      = true;
};

} // namespace pengine
