#include "terrain/streamer.h"

#include <algorithm>
#include <cmath>

#include "core/transform.h"

namespace apricot {
namespace {

int sq_distance(ChunkCoord a, ChunkCoord b) {
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

// Nearest-first, with a coordinate tie-break so the order is a pure function of
// the inputs. Two machines given the same camera position must produce the same
// load order, or a replay diverges on which chunk arrived first.
bool nearer(ChunkCoord a, ChunkCoord b, ChunkCoord centre) {
    const int da = sq_distance(a, centre);
    const int db = sq_distance(b, centre);
    if (da != db) return da < db;
    if (a.x != b.x) return a.x < b.x;
    return a.z < b.z;
}

}  // namespace

StreamerConfig Streamer::normalised(StreamerConfig cfg) {
    // The ring radii are enforced rather than trusted, exactly as
    // evict_radius > load_radius already was. A caller who writes {10, 4, 20}
    // by accident does not get a world with an inverted level ring; a
    // non-monotonic table would make lod_for() answer differently depending on
    // which comparison happened to fire first, which is a nondeterminism in the
    // one module whose whole premise is determinism.
    //
    // The level 0 ring has a FLOOR, and it is a correctness floor rather than a
    // quality one. Ring membership is a circular test on squared distance, so a
    // radius of 1 covers the four edge-adjacent chunks and leaves the four
    // DIAGONAL ones (squared distance 2) at level 1. A car sitting near a chunk
    // corner has wheels in a diagonal neighbour, and physics reconstructs the
    // level 0 lattice analytically without consulting the streamer — so that
    // wheel would rest on a surface the renderer is not drawing. 2 is the
    // smallest radius that contains the whole 8-neighbourhood.
    //
    // Found by tests/streamer_lod_tests.cpp, which asserted the neighbourhood
    // rather than only the chunk under the car. It was worth the extra loop.
    if (cfg.lod_ring[0] < kMinLevelZeroRingChunks) {
        cfg.lod_ring[0] = kMinLevelZeroRingChunks;
    }
    for (int i = 1; i < kMaxChunkLod; ++i) {
        if (cfg.lod_ring[i] < cfg.lod_ring[i - 1]) {
            cfg.lod_ring[i] = cfg.lod_ring[i - 1];
        }
    }
    // load_radius is deliberately NOT raised to reach the outermost ring. A
    // caller who wants a small world gets a small world in which the coarse
    // levels simply never occur, which is exactly what a test wanting the old
    // single-level behaviour should get. Clamping it upward here would silently
    // load a 20-chunk radius for anyone who asked for 2.
    if (cfg.evict_radius <= cfg.load_radius) {
        cfg.evict_radius = cfg.load_radius + 1;
    }
    if (cfg.prime_radius > cfg.load_radius) {
        cfg.prime_radius = cfg.load_radius;
    }
    if (cfg.prime_radius < 0) cfg.prime_radius = 0;
    return cfg;
}

int Streamer::lod_about(ChunkCoord c, ChunkCoord centre) const {
    const int d2 = sq_distance(c, centre);
    for (int l = 0; l < kMaxChunkLod; ++l) {
        const int r = cfg_.lod_ring[l];
        if (d2 <= r * r) return l;
    }
    return kMaxChunkLod;
}

int Streamer::lod_for(ChunkCoord c) const { return lod_about(c, centre_); }

int Streamer::resident_lod(ChunkCoord c) const {
    const auto it = resident_.find(c);
    return it == resident_.end() ? -1 : it->second.lod;
}

void Streamer::residency_by_lod(std::size_t out[kMaxChunkLod + 1]) const {
    for (int l = 0; l <= kMaxChunkLod; ++l) out[l] = 0;
    for (const auto& kv : resident_) {
        const int l = kv.second.lod;
        if (l >= 0 && l <= kMaxChunkLod) ++out[l];
    }
}

bool Streamer::ready(glm::vec3 camera_pos) const {
    const ChunkCoord centre = chunk_at(camera_pos.x, camera_pos.z);
    const int r = cfg_.prime_radius;
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dz * dz > r * r) continue;
            const ChunkCoord c{centre.x + dx, centre.z + dz};
            const auto it = resident_.find(c);
            if (it == resident_.end()) return false;

            // Resident at the WRONG level is not ready. Resuming on a level 3
            // chunk under the car puts it on an 8 m chord while physics
            // reconstructs the 1 m lattice, and the car sinks into ground it is
            // visibly standing on.
            //
            // LEVELS ARE MEASURED ABOUT THE POSITION PASSED IN, NOT ABOUT THE
            // LAST PLANNED CENTRE. That distinction is the entire value of this
            // function and it was wrong at first. lod_for() answers relative to
            // centre_, which only moves when plan() runs — so immediately after
            // a teleport it still describes the world the player just left. Ask
            // it about the destination and it says every chunk there is already
            // at the level it wants, because relative to the OLD centre it is.
            //
            // The symptom was silent and exactly backwards from a crash: the
            // teleport reported "filled in 0 steps / 0.0 ms" and resumed on
            // whatever coarse ground happened to be lying around. It only
            // showed up because a warp inside the already-loaded radius was
            // measured, and the fill it was supposed to prove cost nothing at
            // all. A fill that never runs looks exactly like a fill that is
            // very fast.
            if (it->second.lod != lod_about(c, centre)) return false;
        }
    }
    return true;
}

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

void Streamer::release(MeshId id) {
    if (id != kInvalidId) released_.push_back(id);
}

void Streamer::take_released_meshes(std::vector<MeshId>& out) {
    out.insert(out.end(), released_.begin(), released_.end());
    released_.clear();
}

bool Streamer::outside_evict_radius(ChunkCoord c) const {
    const int r = std::max(cfg_.evict_radius, cfg_.load_radius + 1);
    return sq_distance(c, centre_) > r * r;
}

void Streamer::plan(glm::vec3 camera_pos, StepMode mode) {
    loads_.clear();
    centre_ = chunk_at(camera_pos.x, camera_pos.z);

    // Fill mode plans only the prime ring. That is what bounds the caller's
    // fill loop: without it, an unbudgeted plan requests the whole 2.5 km ring
    // and the "fill before resume" path becomes a half-second stall of its own.
    const int load_r =
        mode == StepMode::Fill ? cfg_.prime_radius : cfg_.load_radius;

    // Collected with a squared-distance key so the budget spends itself on the
    // chunks the player is about to reach, not on whichever corner of the
    // square the loop happened to visit first.
    //
    // The vector is a member reused across steps. At load_radius 36 this sweep
    // visits 5329 cells and can want thousands of them, and a local vector
    // would allocate and free that every step forever.
    std::vector<Candidate>& wanted = wanted_scratch_;
    wanted.clear();

    for (int dz = -load_r; dz <= load_r; ++dz) {
        for (int dx = -load_r; dx <= load_r; ++dx) {
            const int d2 = dx * dx + dz * dz;
            if (d2 > load_r * load_r) continue;  // circular, not square

            const ChunkCoord c{centre_.x + dx, centre_.z + dz};
            const int want = lod_for(c);

            // Resident AT THE RIGHT LEVEL is the only kind of resident that
            // counts. Resident at the wrong one is a refit: it stays on screen
            // the whole time, so this is not a hole being opened, it is a
            // rebuild being queued.
            const auto res = resident_.find(c);
            if (res != resident_.end() && res->second.lod == want) continue;

            // Already asked for AT THIS LEVEL. If it is in flight at a
            // different level, the request is stale and asking again at the
            // right one is correct; deliver() will drop the stale one and hand
            // its mesh back to be freed.
            const auto flight = in_flight_.find(c);
            if (flight != in_flight_.end() && flight->second == want) continue;

            // Already delivered and waiting to activate at this level: wanted,
            // but not wanted AGAIN. Asking twice would build it twice and
            // activate two sets of nodes for one chunk.
            const bool queued =
                (active_ && active_->coord == c && active_->lod == want) ||
                std::any_of(delivered_.begin(), delivered_.end(),
                            [c, want](const Activating& a) {
                                return a.coord == c && a.lod == want;
                            });
            if (queued) continue;

            wanted.push_back(Candidate{c, want, d2});
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

    const bool unbudgeted = mode == StepMode::Fill;
    const std::size_t count_budget =
        (unbudgeted || cfg_.max_chunk_builds_per_step <= 0)
            ? wanted.size()
            : static_cast<std::size_t>(cfg_.max_chunk_builds_per_step);
    const int quad_budget = (unbudgeted || cfg_.max_build_quads_per_step <= 0)
                                ? INT32_MAX
                                : cfg_.max_build_quads_per_step;

    int quads_spent = 0;
    for (std::size_t i = 0; i < wanted.size() && loads_.size() < count_budget;
         ++i) {
        const int q = lod_quads(wanted[i].lod) * lod_quads(wanted[i].lod);

        // The quad budget stops AT the first chunk that would overrun it, but
        // never refuses the very first chunk of a step. A level 0 chunk is 4096
        // quads; a budget set below that would otherwise request nothing,
        // forever, and the world would simply never load — which reads as "the
        // streamer is broken" rather than as "that number is too small".
        if (!loads_.empty() && quads_spent + q > quad_budget) break;

        loads_.push_back(ChunkRequest{wanted[i].coord, wanted[i].lod});
        in_flight_[wanted[i].coord] = wanted[i].lod;
        quads_spent += q;
    }
}

void Streamer::deliver(ChunkCoord c, int lod, MeshId chunk_mesh,
                       const AABB& bounds) {
    // Only a chunk we actually asked for, at the level we asked for it, and
    // have not since given up on. A delivery that fails this test is stale —
    // the camera moved far enough that we evicted the request, or crossed a
    // ring so the level changed, while the caller was still building it.
    //
    // The mesh is real and uploaded either way, so a dropped delivery hands it
    // straight to the released list. Dropping it silently was the leak: the
    // host would have no idea it still owned that upload, and a player driving
    // in circles across a ring boundary would accumulate them steadily.
    const auto it = in_flight_.find(c);
    if (it == in_flight_.end() || it->second != lod) {
        release(chunk_mesh);
        return;
    }
    in_flight_.erase(it);

    Activating a;
    a.coord = c;
    a.lod = lod;
    a.mesh = chunk_mesh;
    a.bounds = bounds;

    const auto res = resident_.find(c);
    a.refit = res != resident_.end();

    // Scatter is regenerated here rather than carried through the host,
    // because it is pure in (seed, coord): passing it across the thread
    // boundary would cost a copy to deliver a value we can recompute exactly.
    //
    // Note it does NOT depend on the level. A chunk changing level therefore
    // either keeps exactly the props it had or crosses max_scatter_lod and
    // gains or loses all of them; there is no partial reshuffle to reconcile.
    if (lod <= cfg_.max_scatter_lod) {
        a.props = scatter_chunk(seed_, c);
        a.prop_nodes.reserve(a.props.size());
    }

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
        // The GPU half of eviction. Without this the node count comes back down
        // and the memory does not, which is the version of a streaming leak
        // that looks fine in every debug overlay this engine has.
        release(it->second.mesh);
        it = resident_.erase(it);
    }

    // --- delivered but not started: ONE sweep --------------------------------
    // Nothing has been instantiated for these yet, so there are no nodes to
    // collect — but the host has already uploaded each one's mesh, so those do
    // have to go back.
    delivered_.erase(
        std::remove_if(delivered_.begin(), delivered_.end(),
                       [this](const Activating& a) {
                           if (!outside_evict_radius(a.coord)) return false;
                           release(a.mesh);
                           return true;
                       }),
        delivered_.end());

    // --- the half-activated chunk -------------------------------------------
    // Its partial nodes are real and in the scene, so they have to be
    // collected like any other. Dropping the Activating without removing them
    // leaks nodes that nothing owns and nothing will ever evict.
    //
    // For a REFIT, active_->mesh was handed to the resident entry the moment
    // the terrain node was re-pointed and set to kInvalidId here, so the
    // resident sweep above has already released it and release() below is a
    // no-op. Exactly one owner, either way.
    if (active_ && outside_evict_radius(active_->coord)) {
        if (active_->terrain_node != kInvalidId) {
            doomed_scratch_.push_back(active_->terrain_node);
        }
        doomed_scratch_.insert(doomed_scratch_.end(),
                               active_->prop_nodes.begin(),
                               active_->prop_nodes.end());
        release(active_->mesh);
        active_.reset();
    }

    // --- requests we are giving up on: ONE sweep -----------------------------
    // No mesh to release: nothing has been built for these yet. A delivery that
    // arrives after this point fails the in_flight_ test in deliver() and
    // releases itself there.
    for (auto it = in_flight_.begin(); it != in_flight_.end();) {
        if (outside_evict_radius(it->first)) {
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

int Streamer::activate_props(Scene& scene, const ScenePrototypes& proto,
                             int& budget) {
    int made = 0;
    while (budget > 0 && active_->cursor < active_->props.size()) {
        const ScatterProp& p = active_->props[active_->cursor];

        Transform t;
        t.position = p.position;
        t.rotation = glm::angleAxis(p.yaw, glm::vec3{0.0f, 1.0f, 0.0f});
        t.scale = glm::vec3{p.scale};

        const NodeId id =
            scene.create(prototype_for(proto, p), t, prop_local_bounds(p.kind));
        if (SceneNode* n = scene.get(id)) {
            n->max_draw_distance = prop_dims(p.kind).draw_distance;
        }
        active_->prop_nodes.push_back(id);

        ++active_->cursor;
        --budget;
        ++made;
    }
    return made;
}

void Streamer::activate(Scene& scene, const ScenePrototypes& proto,
                        StreamerStats& stats, StepMode mode) {
    int budget = (mode == StepMode::Fill || cfg_.max_instances_per_step <= 0)
                     ? INT32_MAX
                     : cfg_.max_instances_per_step;

    while (budget > 0) {
        if (!active_) {
            if (delivered_.empty()) break;

            // Nearest first, so the chunk under the player beats the ring
            // around them. Ties broken on coordinate to keep the choice a pure
            // function of the inputs.
            std::size_t best = 0;
            for (std::size_t i = 1; i < delivered_.size(); ++i) {
                if (nearer(delivered_[i].coord, delivered_[best].coord,
                           centre_)) {
                    best = i;
                }
            }

            active_ = std::move(delivered_[best]);
            delivered_.erase(delivered_.begin() +
                             static_cast<std::ptrdiff_t>(best));
        }

        if (!active_->terrain_done) {
            if (active_->refit) {
                // --- a resident chunk changing level -------------------------
                //
                // The terrain node is RE-POINTED, never destroyed and rebuilt.
                // That is the whole reason a ring crossing is invisible: the
                // chunk is on screen at its old level right up to the frame it
                // is on screen at its new one, with no frame in between where
                // the ground is missing. Rebuilding through create/remove would
                // put a chunk-sized hole under the player for however long the
                // activation took.
                const auto res = resident_.find(active_->coord);
                if (res == resident_.end()) {
                    // Evicted between delivery and activation. Nothing to
                    // re-point, so hand the mesh back rather than stranding it.
                    release(active_->mesh);
                    active_.reset();
                    continue;
                }

                Resident& r = res->second;
                if (SceneNode* n = scene.get(r.terrain_node)) {
                    n->renderable.mesh = active_->mesh;
                    n->local_bounds = active_->bounds;
                    scene.set_transform(r.terrain_node, n->local);
                }

                release(r.mesh);   // the level it used to be
                r.mesh = active_->mesh;
                r.lod = active_->lod;
                active_->mesh = kInvalidId;  // ownership moved; see evict()

                // Crossing max_scatter_lod outward: the props go, in bulk. This
                // is unbudgeted like any other removal — a removal is a sweep,
                // and pacing it would keep a half-populated chunk on screen for
                // no benefit.
                if (active_->props.empty() && !r.prop_nodes.empty()) {
                    scene.remove_many(r.prop_nodes);
                    r.prop_nodes.clear();
                }
                // Crossing it inward: the chunk already has its props if it had
                // any, and scatter does not depend on level, so there is
                // nothing to add unless it had none.
                if (!active_->props.empty() && !r.prop_nodes.empty()) {
                    active_->props.clear();
                    active_->cursor = 0;
                }

                ++stats.chunks_refitted;
                active_->terrain_done = true;
                --budget;
                ++stats.instances_activated;
            } else {
                // The ground before the things standing on it. If the budget
                // runs out mid-chunk, a chunk with terrain and some of its
                // trees looks like a clearing; a chunk with trees and no
                // terrain looks like a bug.
                Transform t;  // identity: chunk vertices are already world-space
                Renderable rend = proto.terrain;
                rend.mesh = active_->mesh;

                const NodeId id = scene.create(rend, t, active_->bounds);
                if (SceneNode* n = scene.get(id)) {
                    n->max_draw_distance = cfg_.terrain_draw_distance;
                }
                active_->terrain_node = id;
                active_->terrain_done = true;
                --budget;
                ++stats.instances_activated;
            }
        }

        stats.instances_activated += activate_props(scene, proto, budget);

        if (active_->cursor < active_->props.size()) {
            // Out of budget part way through. The chunk stays in active_ with
            // its cursor where it stopped, and resumes at exactly this prop
            // next step. It is NOT resident yet and must not be: a chunk that
            // counts as resident before its props exist is one that will never
            // get them, because the next plan sees nothing missing.
            stats.budget_exhausted = true;
            break;
        }

        if (active_->refit) {
            // The entry already exists; the level and mesh were updated when
            // the terrain node was re-pointed. Only newly created props remain
            // to be handed over.
            const auto res = resident_.find(active_->coord);
            if (res != resident_.end() && !active_->prop_nodes.empty()) {
                res->second.prop_nodes.insert(res->second.prop_nodes.end(),
                                              active_->prop_nodes.begin(),
                                              active_->prop_nodes.end());
            }
        } else {
            Resident done;
            done.lod = active_->lod;
            done.mesh = active_->mesh;
            done.terrain_node = active_->terrain_node;
            done.prop_nodes = std::move(active_->prop_nodes);
            resident_.emplace(active_->coord, std::move(done));
        }

        active_.reset();
        ++stats.chunks_completed;
    }
}

StreamerStats Streamer::step(Scene& scene, const ScenePrototypes& proto,
                             glm::vec3 camera_pos, StepMode mode) {
    StreamerStats stats;

    plan(camera_pos, mode);
    stats.chunks_requested = static_cast<int>(loads_.size());
    for (const ChunkRequest& r : loads_) {
        stats.quads_requested += lod_quads(r.lod) * lod_quads(r.lod);
    }

    // Eviction is UNBUDGETED and runs before activation. See the ordering note
    // on step() in the header: it is cheap because it is bulk, and deferring
    // it is what lets a stale eviction meet a fresh re-load.
    const std::size_t before = scene.size();
    evict(scene);
    stats.chunks_evicted = static_cast<int>(evicted_.size());
    stats.nodes_evicted = static_cast<int>(before - scene.size());

    activate(scene, proto, stats, mode);
    return stats;
}

}  // namespace apricot
