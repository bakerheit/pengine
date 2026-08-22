#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"
#include "core/frustum.h"
#include "core/transform.h"

namespace apricot {

// The scene graph is SIM-SIDE and knows nothing about drawing.
//
// It stores what exists, where it is and how big it is; it answers "what can
// the camera see"; and it hands that answer to the renderer as a list of
// opaque handles. It never binds, uploads or draws. Anything that would need a
// GL type in this header belongs in src/gfx/ instead.
//
// That split is what lets a headless test cull a real scene built by real
// generators — the failure mode this engine most wants to avoid is a consumer
// test passing on hand-built inputs while the real producer feeds garbage.

// Opaque handles into the renderer's resource tables. The sim treats them as
// meaningless integers it merely carries; only src/gfx/ dereferences them.
using MeshId = uint32_t;
using MaterialId = uint32_t;
using NodeId = uint32_t;

inline constexpr uint32_t kInvalidId = 0xFFFFFFFFu;

// What to draw for a node, and the fields the batcher keys on.
struct Renderable {
    MeshId mesh = kInvalidId;
    MaterialId material = kInvalidId;

    // Per-instance, so two nodes differing ONLY in these still batch together.
    // That is what collapses a hillside of differently-tinted rocks into one
    // draw. Anything added here that must differ per node has to be
    // per-instance too, or batching quietly stops working.
    glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 uv_scale{1.0f, 1.0f};
};

struct SceneNode {
    Transform local;
    glm::mat4 world{1.0f};

    // Bounds in the node's own space. world_bounds is derived in update().
    AABB local_bounds;
    AABB world_bounds;

    Renderable renderable;

    // Beyond this distance from the camera the node is culled. 0 means "use
    // the caller's global distance"; per-node values only ever SHORTEN
    // visibility, never extend it past the global limit.
    float max_draw_distance = 0.0f;

    bool active = false;   // false = a free slot in nodes_, not a live node
    bool visible = true;   // authored visibility, independent of culling
};

class Scene {
public:
    // Handles are stable: create/remove never invalidates an id handed out
    // earlier. Removed slots are recycled, so a stale NodeId can alias a new
    // node — hold ids, not pointers, and drop them when you remove.
    NodeId create(const Renderable& r, const Transform& t,
                  const AABB& local_bounds);
    void remove(NodeId id);
    void clear();

    bool alive(NodeId id) const;
    SceneNode* get(NodeId id);
    const SceneNode* get(NodeId id) const;

    // Mark a node's transform dirty. Cheaper than get()->local = t when you
    // only need the world matrix refreshed at the next update().
    void set_transform(NodeId id, const Transform& t);

    std::size_t size() const { return live_count_; }

    // Recompute world matrices and world bounds for everything dirtied since
    // the last call. Run once per sim step, before cull().
    void update();

    struct CullResult {
        // Node ids that survived, SORTED BY BATCH KEY. The sort is the entire
        // reason the renderer can collapse runs — see scene/draw_batch.h.
        std::vector<NodeId> visible;
        int total = 0;
        int culled = 0;
    };

    // Frustum + distance cull. `max_dist` <= 0 disables distance culling
    // (used by debug fly-cams that want to see the whole world at once).
    //
    // The result is owned by the Scene and reused between calls to avoid
    // re-allocating every frame; it is invalidated by the next cull().
    const CullResult& cull(const Frustum& frustum, glm::vec3 cam_pos,
                           float max_dist);

private:
    std::vector<SceneNode> nodes_;
    std::vector<NodeId> free_slots_;
    std::vector<NodeId> dirty_;
    std::size_t live_count_ = 0;
    CullResult cull_scratch_;
};

}  // namespace apricot
