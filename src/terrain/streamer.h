#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"
#include "scene/scene.h"
#include "terrain/chunk.h"
#include "terrain/scatter.h"

namespace apricot {

// Chunk residency: which chunks exist right now, and which scene nodes they
// own.
//
// The streamer PLANS and it OWNS NODES. It does not build meshes and it does
// not upload them — build_chunk() is pure and thread-safe precisely so the
// host can farm that work out however it likes. Keeping the policy here, free
// of the host layer and of threads, is what makes "does the world load ahead
// of the player, and does it let go behind them" a headless test rather than a
// thing you squint at while driving.
//
// GENERATED CHUNKS ARE NEVER WRITTEN TO DISK. There is no cache path in this
// file and there must not be one. A chunk is a pure function of (seed, coord),
// so regenerating is cheaper than reading; and the moment a generated chunk
// has an on-disk copy, a stale file silently outranks a code change and the
// world stops matching the generator that supposedly defines it.

// What the streamer instantiates when a chunk activates.
//
// The sim has no idea what a MeshId or MaterialId means — the host fills these
// in once, after it has uploaded its models, and the sim carries them as
// opaque integers. That is the whole reason this class is testable without
// anything on screen.
struct ScenePrototypes {
    // Material for terrain chunks. The MESH id is per-chunk and arrives
    // through deliver(); only the material and the per-instance fields come
    // from here.
    Renderable terrain;

    Renderable tree[kTreeVariants];
    Renderable rock[kRockVariants];
};

struct StreamerConfig {
    // Chunks loaded in each direction from the camera's chunk.
    int load_radius = 4;

    // Must be > load_radius. A single radius means a player idling exactly on
    // a boundary thrashes the same chunk in and out forever; the gap is the
    // hysteresis that stops it.
    int evict_radius = 6;

    // Ceiling on MESH BUILDS handed to the caller per step. This budgets CPU
    // meshing work, which is a different resource from node creation and is
    // why it is a different number.
    int max_chunk_builds_per_step = 2;

    // Ceiling on INSTANCES activated per step — scene nodes created, counting
    // the chunk's own terrain node as one.
    //
    // THIS IS THE IMPORTANT BUDGET, and it is denominated in instances rather
    // than chunks on purpose. "N chunks per step" is only a bound on work
    // while every chunk costs the same, which stopped being true the moment
    // scatter landed: a wooded valley chunk carries a couple of hundred props
    // and a bare rock chunk carries none. Budgeting by chunk means the frame
    // that crosses into forest does ten times the work of the one before it,
    // which is precisely the hitch the budget existed to prevent. A chunk that
    // runs out of budget half way is CARRIED to the next step and resumes at
    // the exact prop it stopped on.
    int max_instances_per_step = 384;

    // Draw distance in metres for terrain chunks. 0 means "use the caller's
    // global distance". Per-node limits only ever shorten — see Scene::cull.
    float terrain_draw_distance = 0.0f;
};

// What one step did. Returned rather than logged so tests can assert on it.
struct StreamerStats {
    int chunks_requested = 0;   // handed to the caller to build this step
    int chunks_evicted = 0;
    int nodes_evicted = 0;
    int instances_activated = 0;
    int chunks_completed = 0;
    bool budget_exhausted = false;  // true if a chunk was carried to next step
};

class Streamer {
public:
    explicit Streamer(uint64_t seed, StreamerConfig cfg = {})
        : seed_(seed), cfg_(cfg) {}

    uint64_t seed() const { return seed_; }
    const StreamerConfig& config() const { return cfg_; }

    // One sim step of residency. The internal order is fixed and deliberate:
    //
    //   1. PLAN     recompute what should be resident around the camera and
    //               refill pending_loads().
    //   2. EVICT    unbudgeted and in bulk, including anything half-activated
    //               or merely delivered.
    //   3. ACTIVATE spend the instance budget on delivered chunks.
    //
    // Evicting before activating, within one step, is not a detail. The bug it
    // avoids: if an eviction can sit in a queue while a NEWER re-load of the
    // same coordinate is activated, the eviction then tears down the fresh
    // chunk and leaves a permanent hole that no later plan will fix, because
    // the streamer already believes that chunk is resident. Recomputing
    // eviction from the current camera position every step, and applying it
    // immediately, makes a stale eviction unrepresentable.
    //
    // The caller builds pending_loads() between steps — on whatever thread it
    // likes — and reports back through deliver().
    StreamerStats step(Scene& scene, const ScenePrototypes& proto,
                       glm::vec3 camera_pos);

    // Chunks the caller should build, NEAREST FIRST.
    const std::vector<ChunkCoord>& pending_loads() const { return loads_; }

    // Coordinates evicted by the most recent step. Telemetry and tests.
    const std::vector<ChunkCoord>& last_evictions() const { return evicted_; }

    // Report a built chunk back. Takes the mesh's id and bounds, not the mesh:
    // the streamer needs to know how big it is and what to draw, and has no
    // business holding vertex data the host has already uploaded.
    //
    // A delivery for a chunk that is no longer wanted — because the camera
    // moved on while it was being built — is DROPPED, not activated. It cannot
    // leave a hole: the next plan sees the chunk is neither resident nor in
    // flight and simply asks again.
    void deliver(ChunkCoord c, MeshId chunk_mesh, const AABB& bounds);

    bool resident(ChunkCoord c) const { return resident_.count(c) != 0; }
    std::size_t resident_count() const { return resident_.size(); }

    // Chunks delivered but not yet fully turned into scene nodes, including
    // the one carried across this step's budget.
    std::size_t activating_count() const;

    // Chunks requested but not yet delivered.
    std::size_t in_flight_count() const { return in_flight_.size(); }

    // Nodes owned by a resident chunk, terrain node first. Empty for a chunk
    // that is not fully resident. For tests and for anything that needs to
    // address a chunk's contents.
    std::vector<NodeId> chunk_nodes(ChunkCoord c) const;

private:
    // A chunk that has been delivered and is being turned into scene nodes.
    // The cursor is the entire "carry a half-activated chunk" mechanism.
    struct Activating {
        ChunkCoord coord;
        MeshId mesh = kInvalidId;
        AABB bounds;
        std::vector<ScatterProp> props;
        std::size_t cursor = 0;      // next prop index to instantiate
        bool terrain_done = false;
        NodeId terrain_node = kInvalidId;
        std::vector<NodeId> prop_nodes;
    };

    struct Resident {
        NodeId terrain_node = kInvalidId;
        std::vector<NodeId> prop_nodes;
    };

    void plan(glm::vec3 camera_pos);
    void evict(Scene& scene);
    void activate(Scene& scene, const ScenePrototypes& proto,
                  StreamerStats& stats);

    bool outside_evict_radius(ChunkCoord c) const;

    uint64_t seed_;
    StreamerConfig cfg_;

    ChunkCoord centre_{0, 0};

    std::unordered_map<ChunkCoord, Resident, ChunkCoordHash> resident_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> in_flight_;

    // Delivered, not yet started. Nearest-first selection happens at
    // activation time rather than at delivery time, so a chunk that became the
    // nearest while it sat in this queue still gets priority.
    std::vector<Activating> delivered_;

    // The one chunk mid-activation, carried between steps. At most one: two
    // half-built chunks would both be invisible to the player for twice as
    // long, for no gain.
    std::optional<Activating> active_;

    std::vector<ChunkCoord> loads_;
    std::vector<ChunkCoord> evicted_;

    // Reused across steps so eviction does not allocate on the frame it is
    // most likely to be expensive.
    std::vector<NodeId> doomed_scratch_;
};

}  // namespace apricot
