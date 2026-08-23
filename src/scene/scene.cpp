#include "scene/scene.h"

#include <algorithm>

#include "scene/draw_batch.h"

namespace apricot {

NodeId Scene::create(const Renderable& r, const Transform& t,
                     const AABB& local_bounds) {
    NodeId id;
    if (!free_slots_.empty()) {
        id = free_slots_.back();
        free_slots_.pop_back();
    } else {
        id = static_cast<NodeId>(nodes_.size());
        nodes_.emplace_back();
    }

    SceneNode& n = nodes_[id];
    n = SceneNode{};
    n.renderable = r;
    n.local = t;
    n.local_bounds = local_bounds;
    n.active = true;
    n.visible = true;

    dirty_.push_back(id);
    ++live_count_;
    return id;
}

void Scene::remove(NodeId id) {
    if (!alive(id)) return;
    nodes_[id].active = false;
    free_slots_.push_back(id);
    --live_count_;
}

void Scene::remove_many(const std::vector<NodeId>& ids) {
    // One pass, and alive() is re-checked per id rather than trusted. A
    // duplicate in the list would otherwise push the same slot onto the free
    // list twice, and the second create() after that would hand the same id to
    // two callers — two live nodes aliasing one slot, which presents as a
    // random object vanishing when an unrelated one is deleted.
    for (const NodeId id : ids) {
        if (!alive(id)) continue;
        nodes_[id].active = false;
        free_slots_.push_back(id);
        --live_count_;
    }
}

void Scene::clear() {
    nodes_.clear();
    free_slots_.clear();
    dirty_.clear();
    live_count_ = 0;
    cull_scratch_.visible.clear();
}

bool Scene::alive(NodeId id) const {
    return id < nodes_.size() && nodes_[id].active;
}

SceneNode* Scene::get(NodeId id) {
    return alive(id) ? &nodes_[id] : nullptr;
}

const SceneNode* Scene::get(NodeId id) const {
    return alive(id) ? &nodes_[id] : nullptr;
}

void Scene::set_transform(NodeId id, const Transform& t) {
    if (!alive(id)) return;
    nodes_[id].local = t;
    dirty_.push_back(id);
}

void Scene::update() {
    for (const NodeId id : dirty_) {
        if (!alive(id)) continue;  // removed after being dirtied
        SceneNode& n = nodes_[id];
        n.world = n.local.matrix();
        n.world_bounds = n.local_bounds.valid()
                             ? n.local_bounds.transformed(n.world)
                             : AABB{};
    }
    dirty_.clear();
}

const Scene::CullResult& Scene::cull(const Frustum& frustum, glm::vec3 cam_pos,
                                     float max_dist) {
    CullResult& out = cull_scratch_;
    out.visible.clear();
    out.total = 0;
    out.culled = 0;

    const bool distance_cull = max_dist > 0.0f;
    const float global_sq = max_dist * max_dist;

    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        const SceneNode& n = nodes_[i];
        if (!n.active || !n.visible) continue;
        ++out.total;

        // An inverted (never-expanded) box passes every plane test, so it
        // would draw at any distance, forever. Treat it as nothing to draw.
        if (!n.world_bounds.valid()) {
            ++out.culled;
            continue;
        }

        if (distance_cull) {
            // A per-node limit only ever SHORTENS visibility. Letting it
            // extend past the global limit means a single authored value can
            // defeat the streaming budget from the far side of the world.
            float limit_sq = global_sq;
            if (n.max_draw_distance > 0.0f) {
                const float d = std::min(n.max_draw_distance, max_dist);
                limit_sq = d * d;
            }
            // Squared distance from the camera to the CLOSEST POINT of the
            // box: zero on each axis where the camera is between the slabs.
            // See the note on cull() in the header for why this is not the
            // centre.
            const glm::vec3 d{
                std::max({n.world_bounds.min.x - cam_pos.x, 0.0f,
                          cam_pos.x - n.world_bounds.max.x}),
                std::max({n.world_bounds.min.y - cam_pos.y, 0.0f,
                          cam_pos.y - n.world_bounds.max.y}),
                std::max({n.world_bounds.min.z - cam_pos.z, 0.0f,
                          cam_pos.z - n.world_bounds.max.z})};
            if (glm::dot(d, d) > limit_sq) {
                ++out.culled;
                continue;
            }
        }

        if (frustum.cull(n.world_bounds)) {
            ++out.culled;
            continue;
        }

        out.visible.push_back(static_cast<NodeId>(i));
    }

    // Sort by batch key so equal keys land adjacent. plan_draw_batches() can
    // then collapse contiguous runs with a single linear pass; without this
    // sort it finds almost nothing and instancing quietly does no work.
    //
    // The id tie-break is not cosmetic. std::sort is not stable, so equal keys
    // come out in an unspecified order that can differ between standard
    // library versions and even between two calls on the same data. This world
    // is a pure function of a seed; the instance order handed to the renderer
    // has to be one too, or a replay renders differently from the run it
    // recorded and no golden test can ever be written.
    std::sort(out.visible.begin(), out.visible.end(),
              [this](NodeId a, NodeId b) {
                  const uint64_t ka = batch_key(nodes_[a].renderable);
                  const uint64_t kb = batch_key(nodes_[b].renderable);
                  if (ka != kb) return ka < kb;
                  return a < b;
              });

    return out;
}

}  // namespace apricot
