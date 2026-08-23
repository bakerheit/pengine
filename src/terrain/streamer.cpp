#include "terrain/streamer.h"

#include <algorithm>
#include <cmath>

#include "core/transform.h"

namespace apricot {
namespace {

int chebyshev_sq_distance(ChunkCoord a, ChunkCoord b) {
    const int dx = a.x - b.x;
    const int dz = a.z - b.z;
    return dx * dx + dz * dz;
}

// Local bounds for a prop, UNSCALED. Scene::update applies the node's
// transform (including the prop's scale) to produce world bounds, so scaling
// here as well would square it and cull the prop far too late.
AABB prop_local_bounds(PropKind kind) {
    const PropDims& d = prop_dims(kind);
    AABB b;
    b.expand(glm::vec3{-d.radius, 0.0f, -d.radius});
    b.expand(glm::vec3{d.radius, d.height, d.radius});
    return b;
}

const Renderable& prototype_for(const ScenePrototypes& proto,
                                const ScatterProp& p) {
    if (p.kind == PropKind::Tree) {
        const std::size_t i = p.variant < kTreeVariants ? p.variant : 0u;
        return proto.tree[i];
    }
    const std::size_t i = p.variant < kRockVariants ? p.variant : 0u;
    return proto.rock[i];
}

}  // namespace

std::size_t Streamer::activating_count() const {
    return delivered_.size() + (active_ ? 1u : 0u);
}

std::vector<NodeId> Streamer::chunk_nodes(ChunkCoord c) const {
    std::vector<NodeId> out;
    const auto it = resident_.find(c);
    if (it == resident_.end()) return out;
    out.reserve(it->second.prop_nodes.size() + 1u);
    out.push_back(it->second.terrain_node);
    out.insert(out.end(), it->second.prop_nodes.begin(),
               it->second.prop_nodes.end());
    return out;
}

bool Streamer::outside_evict_radius(ChunkCoord c) const {
    const int r = std::max(cfg_.evict_radius, cfg_.load_radius + 1);
    return chebyshev_sq_distance(c, centre_) > r * r;
}

void Streamer::plan(glm::vec3 camera_pos) {
    loads_.clear();
    centre_ = chunk_at(camera_pos.x, camera_pos.z);

    const int load_r = cfg_.load_radius;

    // Collected with a squared-distance key so the budget spends itself on the
    // chunks the player is about to reach, not on whichever corner of the
    // square the loop happened to visit first.
    struct Candidate {
        ChunkCoord coord;
        int dist_sq;
    };
    std::vector<Candidate> wanted;

    for (int dz = -load_r; dz <= load_r; ++dz) {
        for (int dx = -load_r; dx <= load_r; ++dx) {
            const int d2 = dx * dx + dz * dz;
            if (d2 > load_r * load_r) continue;  // circular, not square

            const ChunkCoord c{centre_.x + dx, centre_.z + dz};
            if (resident_.count(c) != 0 || in_flight_.count(c) != 0) continue;

            // Already delivered and waiting to activate: wanted, but not
            // wanted AGAIN. Asking for it twice would build it twice and
            // activate two sets of nodes for one chunk.
            const bool queued =
                (active_ && active_->coord == c) ||
                std::any_of(delivered_.begin(), delivered_.end(),
                            [c](const Activating& a) { return a.coord == c; });
            if (queued) continue;

            wanted.push_back(Candidate{c, d2});
        }
    }

    std::sort(wanted.begin(), wanted.end(),
              [](const Candidate& a, const Candidate& b) {
                  // Tie-break on coordinate so the order is fully determined
                  // by the inputs. Two machines given the same camera position
                  // must produce the same load order.
                  if (a.dist_sq != b.dist_sq) return a.dist_sq < b.dist_sq;
                  if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
                  return a.coord.z < b.coord.z;
              });

    const std::size_t budget =
        cfg_.max_chunk_builds_per_step > 0
            ? static_cast<std::size_t>(cfg_.max_chunk_builds_per_step)
            : wanted.size();

    for (std::size_t i = 0; i < wanted.size() && i < budget; ++i) {
        loads_.push_back(wanted[i].coord);
        in_flight_.insert(wanted[i].coord);
    }
}

void Streamer::deliver(ChunkCoord c, MeshId chunk_mesh, const AABB& bounds) {
    // Only a chunk we actually asked for and have not since given up on. A
    // delivery that fails this test is stale — the camera moved far enough
    // that we evicted the request while the caller was still building it — and
    // activating it would put a chunk outside the evict radius into the scene
    // with nothing tracking it back out again.
    if (in_flight_.erase(c) == 0) return;

    Activating a;
    a.coord = c;
    a.mesh = chunk_mesh;
    a.bounds = bounds;
    // Scatter is regenerated here rather than carried through the host,
    // because it is pure in (seed, coord): passing it across the thread
    // boundary would cost a copy to deliver a value we can recompute exactly.
    a.props = scatter_chunk(seed_, c);
    a.prop_nodes.reserve(a.props.size());
    delivered_.push_back(std::move(a));
}

void Streamer::evict(Scene& scene) {
    evicted_.clear();
    doomed_scratch_.clear();

    // --- resident chunks: ONE sweep, collecting nodes as we go ---------------
    // Node ids go into a single contiguous list and are removed in one call.
    // The pattern this avoids is removing a chunk's several hundred nodes one
    // at a time from a loop that also has to locate each one; that is
    // O(chunk x world) and it freezes the frame on every boundary crossing,
    // which is the one moment the player is definitely moving.
    for (auto it = resident_.begin(); it != resident_.end();) {
        if (!outside_evict_radius(it->first)) {
            ++it;
            continue;
        }
        evicted_.push_back(it->first);
        if (it->second.terrain_node != kInvalidId) {
            doomed_scratch_.push_back(it->second.terrain_node);
        }
        doomed_scratch_.insert(doomed_scratch_.end(),
                               it->second.prop_nodes.begin(),
                               it->second.prop_nodes.end());
        it = resident_.erase(it);
    }

    // --- delivered but not started: ONE sweep --------------------------------
    // Nothing has been instantiated for these yet, so there are no nodes to
    // collect; they just stop being wanted.
    delivered_.erase(
        std::remove_if(delivered_.begin(), delivered_.end(),
                       [this](const Activating& a) {
                           return outside_evict_radius(a.coord);
                       }),
        delivered_.end());

    // --- the half-activated chunk -------------------------------------------
    // Its partial nodes are real and in the scene, so they have to be
    // collected like any other. Dropping the Activating without removing them
    // leaks nodes that nothing owns and nothing will ever evict.
    if (active_ && outside_evict_radius(active_->coord)) {
        if (active_->terrain_node != kInvalidId) {
            doomed_scratch_.push_back(active_->terrain_node);
        }
        doomed_scratch_.insert(doomed_scratch_.end(),
                               active_->prop_nodes.begin(),
                               active_->prop_nodes.end());
        active_.reset();
    }

    // --- requests we are giving up on: ONE sweep -----------------------------
    for (auto it = in_flight_.begin(); it != in_flight_.end();) {
        if (outside_evict_radius(*it)) {
            it = in_flight_.erase(it);
        } else {
            ++it;
        }
    }

    // unordered_map iteration order is not guaranteed, so sort the reported
    // list. Callers and tests read it; an order that varies between runs would
    // make this the one non-deterministic thing in the module.
    std::sort(evicted_.begin(), evicted_.end(), [](ChunkCoord a, ChunkCoord b) {
        if (a.x != b.x) return a.x < b.x;
        return a.z < b.z;
    });

    if (!doomed_scratch_.empty()) scene.remove_many(doomed_scratch_);
}

void Streamer::activate(Scene& scene, const ScenePrototypes& proto,
                        StreamerStats& stats) {
    int budget = cfg_.max_instances_per_step > 0 ? cfg_.max_instances_per_step
                                                 : INT32_MAX;

    while (budget > 0) {
        if (!active_) {
            if (delivered_.empty()) break;

            // Nearest first, so the chunk under the player beats the ring
            // around them. Ties broken on coordinate to keep the choice a pure
            // function of the inputs.
            std::size_t best = 0;
            int best_d = chebyshev_sq_distance(delivered_[0].coord, centre_);
            for (std::size_t i = 1; i < delivered_.size(); ++i) {
                const int d = chebyshev_sq_distance(delivered_[i].coord, centre_);
                const bool nearer =
                    d < best_d ||
                    (d == best_d &&
                     (delivered_[i].coord.x < delivered_[best].coord.x ||
                      (delivered_[i].coord.x == delivered_[best].coord.x &&
                       delivered_[i].coord.z < delivered_[best].coord.z)));
                if (nearer) {
                    best = i;
                    best_d = d;
                }
            }

            active_ = std::move(delivered_[best]);
            delivered_.erase(delivered_.begin() +
                             static_cast<std::ptrdiff_t>(best));
        }

        // The ground before the things standing on it. If the budget runs out
        // mid-chunk, a chunk with terrain and some of its trees looks like a
        // clearing; a chunk with trees and no terrain looks like a bug.
        if (!active_->terrain_done) {
            Transform t;  // identity: chunk vertices are already world-space
            Renderable r = proto.terrain;
            r.mesh = active_->mesh;

            const NodeId id = scene.create(r, t, active_->bounds);
            if (SceneNode* n = scene.get(id)) {
                n->max_draw_distance = cfg_.terrain_draw_distance;
            }
            active_->terrain_node = id;
            active_->terrain_done = true;
            --budget;
            ++stats.instances_activated;
        }

        while (budget > 0 && active_->cursor < active_->props.size()) {
            const ScatterProp& p = active_->props[active_->cursor];

            Transform t;
            t.position = p.position;
            t.rotation = glm::angleAxis(p.yaw, glm::vec3{0.0f, 1.0f, 0.0f});
            t.scale = glm::vec3{p.scale};

            const NodeId id = scene.create(prototype_for(proto, p), t,
                                           prop_local_bounds(p.kind));
            if (SceneNode* n = scene.get(id)) {
                n->max_draw_distance = prop_dims(p.kind).draw_distance;
            }
            active_->prop_nodes.push_back(id);

            ++active_->cursor;
            --budget;
            ++stats.instances_activated;
        }

        if (active_->cursor < active_->props.size()) {
            // Out of budget part way through. The chunk stays in active_ with
            // its cursor where it stopped, and resumes at exactly this prop
            // next step. It is NOT resident yet and must not be: a chunk that
            // counts as resident before its props exist is one that will never
            // get them, because the next plan sees nothing missing.
            stats.budget_exhausted = true;
            break;
        }

        Resident done;
        done.terrain_node = active_->terrain_node;
        done.prop_nodes = std::move(active_->prop_nodes);
        resident_.emplace(active_->coord, std::move(done));
        active_.reset();
        ++stats.chunks_completed;
    }
}

StreamerStats Streamer::step(Scene& scene, const ScenePrototypes& proto,
                             glm::vec3 camera_pos) {
    StreamerStats stats;

    plan(camera_pos);
    stats.chunks_requested = static_cast<int>(loads_.size());

    // Eviction is UNBUDGETED and runs before activation. See the ordering note
    // on step() in the header: it is cheap because it is bulk, and deferring
    // it is what lets a stale eviction meet a fresh re-load.
    const std::size_t before = scene.size();
    evict(scene);
    stats.chunks_evicted = static_cast<int>(evicted_.size());
    stats.nodes_evicted = static_cast<int>(before - scene.size());

    activate(scene, proto, stats);
    return stats;
}

}  // namespace apricot
