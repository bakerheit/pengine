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

    // Bulk removal, for evicting a whole streamed chunk at once.
    //
    // This exists because eviction is the frame-spike that streaming most
    // reliably produces. Removing a chunk's several hundred nodes by calling
    // remove() from a loop that ALSO has to find each one in some container is
    // O(chunk x world); doing it here lets the streamer hand over one
    // contiguous list and sweep its own containers exactly once each.
    // Duplicate and already-dead ids are ignored rather than double-freeing a
    // slot, which would hand the same id out twice and alias two live nodes.
    void remove_many(const std::vector<NodeId>& ids);

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
        // Node ids that survived, SORTED BY BATCH KEY, then by id.
        //
        // The batch-key sort is the entire reason the renderer can collapse
        // runs — see scene/draw_batch.h. The id tie-break is for DETERMINISM:
        // std::sort is not stable, so without it the order within a run is
        // unspecified, and two runs of the same seed would hand the renderer
        // the same instances in a different order. In a world whose whole
        // premise is that a seed reproduces a run, "the draw order is
        // arbitrary" is a bug waiting for a golden-image test.
        std::vector<NodeId> visible;
        int total = 0;
        int culled = 0;
    };

    // Frustum + distance cull. `max_dist` <= 0 disables distance culling
    // (used by debug fly-cams that want to see the whole world at once).
    //
    // Distance is measured to the CLOSEST POINT of the node's world AABB, not
    // to its centre. For a small prop the two are the same; for a 64 m terrain
    // chunk the centre is up to 45 m further away than the near edge, so a
    // centre test culls chunks whose near half is still comfortably on screen
    // and the player watches the ground vanish ahead of them. It also means a
    // box containing the camera is never distance-culled, which a centre test
    // gets wrong for anything large.
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
