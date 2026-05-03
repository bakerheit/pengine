#include "scene/scene_node.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace pengine {

void SceneNode::mark_dirty() {
    dirty_ = true;
    for (SceneNode* c : children) c->mark_dirty();
}

void SceneNode::update() {
    if (!dirty_) return;

    glm::mat4 local = transform.matrix();
    world_mat_ = parent ? parent->world_matrix() * local : local;
    world_normal_mat_ = glm::mat3(glm::inverseTranspose(world_mat_));

    if (renderable) {
        world_aabb_ = renderable->local_aabb.transform(world_mat_);
    } else {
        // Non-renderable node: a degenerate AABB at the world origin.
        glm::vec3 wp{world_mat_[3]};
        world_aabb_ = {wp, wp};
    }

    dirty_ = false;
}

} // namespace pengine
