#include "scene/scene.h"

#include <functional>
#include <glm/gtc/matrix_inverse.hpp>

#include "render/mesh.h"
#include "render/shader.h"

namespace pengine {

SceneNode* Scene::create_node(SceneNode* parent) {
    auto node = std::make_unique<SceneNode>();
    node->parent = parent;
    if (parent) parent->children.push_back(node.get());

    SceneNode* ptr = node.get();
    nodes_.push_back(std::move(node));
    if (!parent) roots_.push_back(ptr);
    return ptr;
}

void Scene::update() {
    // Process parents before children via a recursive lambda.
    std::function<void(SceneNode*)> walk = [&](SceneNode* n) {
        if (n->parent && n->parent->is_dirty()) walk(n->parent);
        n->update();
        for (SceneNode* c : n->children) walk(c);
    };
    for (SceneNode* r : roots_) walk(r);
}

Scene::CullResult Scene::cull(const Frustum& frustum) const {
    CullResult cr;
    cr.total = static_cast<int>(nodes_.size());
    for (const auto& node : nodes_) {
        if (!node->renderable) continue;
        if (frustum.cull(node->world_aabb())) {
            ++cr.culled;
        } else {
            cr.visible.push_back(node.get());
        }
    }
    return cr;
}

void Scene::draw(const CullResult& cr, Shader& shader) const {
    for (const SceneNode* node : cr.visible) {
        if (!node->renderable || !node->renderable->mesh) continue;
        const glm::mat4& model = node->world_matrix();
        glm::mat3 nm = glm::mat3(glm::inverseTranspose(model));
        shader.set("u_model",      model);
        shader.set("u_normal_mat", nm);
        node->renderable->mesh->draw();
    }
}

} // namespace pengine
