#include "scene/scene.h"

#include <algorithm>
#include <functional>

#include "render/mesh.h"
#include "render/shader.h"
#include "render/texture.h"
#include "scene/aabb.h"

namespace pengine {

// =============================================================================
// Node creation
// =============================================================================

SceneNode* Scene::create_node(SceneNode* parent) {
    auto node = std::make_unique<SceneNode>();
    node->parent = parent;
    if (parent) parent->children.push_back(node.get());

    SceneNode* ptr = node.get();
    nodes_.push_back(std::move(node));
    // Dynamic node: add to roots_ (for Scene::update traversal) if no parent.
    if (!parent) roots_.push_back(ptr);
    // All non-static nodes go into dynamic_nodes_ for culling.
    dynamic_nodes_.push_back(ptr);
    return ptr;
}

SceneNode* Scene::create_node_static(SceneNode* parent, CellCoord cell) {
    auto node = std::make_unique<SceneNode>();
    node->parent = parent;
    if (parent) parent->children.push_back(node.get());

    SceneNode* ptr = node.get();
    nodes_.push_back(std::move(node));
    // Static nodes are NOT added to roots_. They never move, so there is
    // nothing to recompute after the initial world-matrix flush. We put them
    // in pending_static_ and compute their matrices once in update().
    pending_static_.push_back(ptr);
    // Register in the cell bucket for cell-first culling.
    static_buckets_[cell].push_back(ptr);
    return ptr;
}

// =============================================================================
// Node removal
// =============================================================================

void Scene::remove_node(SceneNode* node) {
    if (!node) return;
    // Detach from parent.
    if (node->parent) {
        auto& ch = node->parent->children;
        ch.erase(std::remove(ch.begin(), ch.end(), node), ch.end());
    } else {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), node), roots_.end());
    }
    // Remove from dynamic list (noop if it's a static node).
    dynamic_nodes_.erase(
        std::remove(dynamic_nodes_.begin(), dynamic_nodes_.end(), node),
        dynamic_nodes_.end());
    // Remove from pending-static list (noop if already flushed or dynamic).
    pending_static_.erase(
        std::remove(pending_static_.begin(), pending_static_.end(), node),
        pending_static_.end());
    // PBD-032: also unregister from per-cell static buckets. The bulk evict
    // path uses `remove_static_cell` which clears the bucket wholesale, but
    // the Map Builder delete verb needs to drop a single node without
    // touching its neighbours. Linear scan over buckets — N cells loaded
    // (≤9 today, typical Chebyshev radius) so the cost is invisible.
    for (auto& kv : static_buckets_) {
        auto& bucket = kv.second;
        bucket.erase(std::remove(bucket.begin(), bucket.end(), node),
                     bucket.end());
    }
    // Erase from ownership list (unique_ptr destructor handles cleanup).
    nodes_.erase(
        std::remove_if(nodes_.begin(), nodes_.end(),
                       [node](const std::unique_ptr<SceneNode>& n) {
                           return n.get() == node;
                       }),
        nodes_.end());
}

// =============================================================================
// Transform update
// =============================================================================

void Scene::update() {
    // ---- Static nodes: compute world matrix exactly once, at cell-load time.
    // After this flush they're never dirty again (they don't move), so they
    // never appear in roots_ and cost zero per frame thereafter.
    for (SceneNode* n : pending_static_) n->update();
    pending_static_.clear();

    // ---- Dynamic nodes: standard dirty-propagation traversal.
    // roots_ only contains parentless dynamic nodes (cars, character).
    std::function<void(SceneNode*)> walk = [&](SceneNode* n) {
        if (n->parent && n->parent->is_dirty()) walk(n->parent);
        n->update();
        for (SceneNode* c : n->children) walk(c);
    };
    for (SceneNode* r : roots_) walk(r);
}

// =============================================================================
// Culling — two-tier: static buckets first, dynamic linear second
// =============================================================================

Scene::CullResult Scene::cull(const Frustum& frustum) const {
    CullResult cr;
    cr.total = static_cast<int>(nodes_.size());

    // ---- Static: skip entire cells with one AABB test ----------------------
    // Cell AABB is derived analytically from the grid coordinate — no per-node
    // union needed, always valid, never stale. Heights are conservative bounds
    // that fit any building or terrain in the city.
    for (const auto& [coord, bucket] : static_buckets_) {
        glm::vec3 origin{
            static_cast<float>(coord.x) * static_cell_size_,
            -100.f,
            static_cast<float>(coord.z) * static_cell_size_};
        AABB cell_aabb{origin,
                       origin + glm::vec3{static_cell_size_, 500.f,
                                           static_cell_size_}};
        if (frustum.cull(cell_aabb)) {
            cr.culled += static_cast<int>(bucket.size());
            continue;
        }
        for (SceneNode* node : bucket) {
            if (!node->renderable || !node->visible) continue;
            if (frustum.cull(node->world_aabb())) ++cr.culled;
            else                                   cr.visible.push_back(node);
        }
    }

    // ---- Dynamic: always check (cars, character, wheels — <300 nodes) ------
    for (SceneNode* node : dynamic_nodes_) {
        if (!node->renderable || !node->visible) continue;
        if (frustum.cull(node->world_aabb())) ++cr.culled;
        else                                   cr.visible.push_back(node);
    }

    // Sort to maximise runs of identical material state. Scene::draw uses this
    // to skip redundant glUniform / texture binds when adjacent draws share
    // texture, tint, or uv_scale.
    std::sort(cr.visible.begin(), cr.visible.end(),
              [](const SceneNode* a, const SceneNode* b) {
        const auto& ra = *a->renderable;
        const auto& rb = *b->renderable;
        if (ra.texture != rb.texture) return ra.texture < rb.texture;
        if (ra.mesh    != rb.mesh)    return ra.mesh    < rb.mesh;
        if (ra.tint.x  != rb.tint.x)  return ra.tint.x  < rb.tint.x;
        if (ra.tint.y  != rb.tint.y)  return ra.tint.y  < rb.tint.y;
        if (ra.tint.z  != rb.tint.z)  return ra.tint.z  < rb.tint.z;
        if (ra.uv_scale.x != rb.uv_scale.x) return ra.uv_scale.x < rb.uv_scale.x;
        return ra.uv_scale.y < rb.uv_scale.y;
    });

    return cr;
}

// =============================================================================
// Draw
// =============================================================================

void Scene::draw(const CullResult& cr, Shader& shader) const {
    // Sentinel values that no real Renderable will match, so the first draw
    // always uploads its tint/uv_scale/texture.
    const Texture* last_tex   = reinterpret_cast<const Texture*>(uintptr_t{1});
    glm::vec3      last_tint  {-1.f, -1.f, -1.f};
    glm::vec2      last_scale {-1.f, -1.f};

    for (const SceneNode* node : cr.visible) {
        if (!node->renderable || !node->renderable->mesh) continue;
        const Renderable& r = *node->renderable;
        shader.set("u_model",      node->world_matrix());
        shader.set("u_normal_mat", node->world_normal_matrix());
        if (r.tint != last_tint) {
            shader.set("u_tint", r.tint);
            last_tint = r.tint;
        }
        if (r.uv_scale != last_scale) {
            shader.set("u_uv_scale", r.uv_scale);
            last_scale = r.uv_scale;
        }
        if (r.texture != last_tex) {
            if (r.texture) r.texture->bind(0);
            last_tex = r.texture;
        }
        r.mesh->draw();
    }
}

} // namespace pengine
