#pragma once

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "core/aabb.h"
#include "scene/scene.h"
#include "terrain/chunk.h"
#include "terrain/scatter.h"

namespace apricot {

// Chunk residency: which chunks exist right now, at what level of detail, and
// which scene nodes and meshes they own.
//
// The streamer PLANS and it OWNS NODES. It does not build meshes and it does
// not upload them — build_chunk() is pure and thread-safe precisely so the
// host can farm that work out however it likes. Keeping the policy here, free
// of the host layer and of threads, is what makes "does the world load ahead
// of the player, does it let go behind them, and does it let go of the GPU
// memory too" a headless test rather than a thing you squint at while driving.
//
// GENERATED CHUNKS ARE NEVER WRITTEN TO DISK. There is no cache path in this
// file and there must not be one. A chunk is a pure function of
// (map, seed, coord, lod), so regenerating is cheaper than reading; and the
// moment a generated chunk has an on-disk copy, a stale file silently outranks
// a code change and the world stops matching the generator that defines it.

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

// One chunk the host should build, at the level the plan chose for it.
//
// The level rides on the request rather than being recomputed by the host,
// because the host may build on another thread several frames later, by which
// time the camera has moved and lod_for() would answer differently. A chunk
// built at one level and recorded as another is a permanent mismatch: the
// streamer would refit it forever, every step, and the profile would show
// meshing that never converges.
struct ChunkRequest {
    ChunkCoord coord;
    int lod = 0;

    friend bool operator==(const ChunkRequest& a, const ChunkRequest& b) {
        return a.coord == b.coord && a.lod == b.lod;
    }
};

// Smallest level 0 ring radius the streamer will accept, in chunks.
//
// Ring membership is a circular test on squared distance, so radius 1 leaves
// the four DIAGONAL neighbours (squared distance 2) at level 1 — and a car near
// a chunk corner has wheels in one of those. Physics reconstructs the level 0
// lattice without consulting the streamer, so that wheel would rest on ground
// the renderer is not drawing. 2 is the smallest radius containing the whole
// 8-neighbourhood, and StreamerConfig is clamped to it rather than trusted.
inline constexpr int kMinLevelZeroRingChunks = 2;

struct StreamerConfig {
    // Chunks loaded in each direction from the camera's chunk. This is the
    // OUTER radius, at the coarsest level.
    int load_radius = 36;

    // Must be > load_radius. A single radius means a player idling exactly on
    // a boundary thrashes the same chunk in and out forever; the gap is the
    // hysteresis that stops it.
    int evict_radius = 40;

    // ---- level of detail ----------------------------------------------------
    //
    // Outer radius in chunks of each level below the coarsest. Entry L is the
    // last radius at which level L is used, so with {4, 10, 20} a chunk within
    // 4 is level 0, out to 10 is level 1, out to 20 is level 2, and everything
    // beyond is level 3. Strictly increasing; the streamer sorts and enforces
    // that rather than trusting it.
    //
    // WHY THESE NUMBERS. docs/design/pinatty.md measured a 2.5 km full-detail
    // ring at 4794 chunks, 1.34 GB and 11.1 s of meshing, and called terrain
    // LOD a hard prerequisite rather than polish. These radii are 256 m / 640 m
    // / 1280 m / 2304 m and they hold the same view distance at a fraction of
    // both numbers, because a level 3 chunk is 1/41 of a level 0 chunk's bytes.
    //
    // THE INNER RING IS NOT A FREE PARAMETER. Physics reconstructs the LEVEL 0
    // lattice analytically (mesh_height_at), so the drawn chunk under the car
    // must be level 0 or the car rests on a surface it is not drawn on — the
    // one thing "collision derives from the geometry that draws" forbids.
    // lod_ring[0] must therefore comfortably exceed anything that can touch
    // the ground, and 4 chunks is 256 m of margin around a car.
    int lod_ring[kMaxChunkLod] = {4, 10, 20};

    // Chunks above this level carry TERRAIN ONLY.
    //
    // This is the draw-distance tiering docs/design/pinatty.md asks for, and it
    // is the difference between a resident world of ~25k static instances and
    // one of ~500k. Scene::cull() is a flat vector scan measured at 0.278 ms
    // for 60k nodes and 1.449 ms for 200k; the recommendation there was to cap
    // near 60k with distance tiers rather than build a BVH, and this is the
    // knob that does it.
    //
    // Scatter is pure in (seed, coord) and does NOT depend on the level, so a
    // chunk changing level either keeps exactly the props it had or crosses
    // this line and gains or loses all of them. There is no partial reshuffle.
    int max_scatter_lod = 1;

    // ---- budgets ------------------------------------------------------------

    // Ceiling on MESH BUILDS handed to the caller per step, as a count.
    //
    // Generous, because the quad budget below is the one that actually bounds
    // the work and this only stops a pathological step. 16 level 3 chunks are
    // 1024 quads between them — a quarter of one level 0 chunk — so a count
    // budget tight enough to matter near the player starves the far rings: at
    // 2 a step, the 2796 level 3 chunks of the shipping config take 1400 steps
    // to arrive, and a teleport leaves the horizon empty for twenty seconds.
    int max_chunk_builds_per_step = 16;

    // Ceiling on mesh QUADS handed to the caller per step.
    //
    // THE SECOND DENOMINATION, and LOD is what made it necessary. A chunk-count
    // budget is only a bound on work while every chunk costs the same, which
    // stopped being true the moment a level 3 chunk became 1/64 of a level 0
    // one. Two chunks per step is either 8192 quads or 128, depending on where
    // the player is looking, and budgeting by count alone means the far rings
    // fill 64x slower than they need to while the near ones are still allowed
    // to spike.
    //
    // Both budgets apply and the tighter one wins.
    //
    // 4096 IS ONE LEVEL 0 CHUNK, AND THAT IS THE POINT. It was 8192 first, on
    // the arithmetic that two chunks a step is a modest ask. Measured in the
    // running app it was not: the far-ring fill sat at 6.0-6.5 ms of meshing
    // per frame for two hundred frames, which is a third of a 60 Hz frame spent
    // on terrain that is a kilometre away. Halving it costs nothing that
    // matters — the fill takes twice as many frames and each one is invisible
    // instead of expensive.
    //
    // The budget cannot split a chunk, so one level 0 chunk (2.3 ms) is the
    // floor on a frame that wants one. At this size a full step is ~2.5 ms
    // whatever the level: one level 0 chunk, or four level 1, or sixty-four
    // level 3. That uniformity is the useful property, and it is what a
    // count-only budget could never give.
    int max_build_quads_per_step = 4096;

    // Ceiling on INSTANCES activated per step — scene nodes created, counting
    // the chunk's own terrain node as one.
    //
    // Denominated in instances rather than chunks on purpose. A wooded valley
    // chunk carries a couple of hundred props and a bare rock chunk carries
    // none, so budgeting by chunk means the frame that crosses into forest does
    // ten times the work of the one before it — precisely the hitch the budget
    // existed to prevent. A chunk that runs out of budget half way is CARRIED
    // to the next step and resumes at the exact prop it stopped on.
    int max_instances_per_step = 384;

    // ---- cold fill ----------------------------------------------------------

    // Radius, in chunks, that must be fully resident before the world is
    // considered ready to resume.
    //
    // Startup and a mission teleport both drop a camera into a world with
    // nothing around it. Without a fill-before-resume path the player gets a
    // frame of void and then a hitch, which docs/design/pinatty.md calls a
    // blocker in a way steady-state streaming is not. StepMode::Fill spends no
    // budget and plans only this radius, so the loop is bounded: the far rings
    // arrive afterwards, under budget, as distant ground filling in — which is
    // a far cheaper thing to look at than a hole under the car.
    int prime_radius = 3;

    // Draw distance in metres for terrain chunks. 0 means "use the caller's
    // global distance". Per-node limits only ever shorten — see Scene::cull.
    float terrain_draw_distance = 0.0f;
};

// What one step did. Returned rather than logged so tests can assert on it.
struct StreamerStats {
    int chunks_requested = 0;   // handed to the caller to build this step
    int quads_requested = 0;    // their total quad cost, the second budget
    int chunks_evicted = 0;
    int chunks_refitted = 0;    // resident chunks that changed level
    int nodes_evicted = 0;
    int instances_activated = 0;
    int chunks_completed = 0;
    bool budget_exhausted = false;  // true if a chunk was carried to next step
};

// How hard a step is allowed to push.
enum class StepMode {
    // Steady state. Every budget applies.
    Budgeted,

    // Cold start and teleport. No budgets, and the plan is limited to
    // prime_radius so the caller's fill loop is bounded rather than pulling the
    // whole 2.5 km ring in one gulp.
    Fill,
};

class Streamer {
public:
    explicit Streamer(uint64_t seed, StreamerConfig cfg = {})
        : seed_(seed), cfg_(normalised(cfg)) {}

    uint64_t seed() const { return seed_; }
    const StreamerConfig& config() const { return cfg_; }

    // One sim step of residency. The internal order is fixed and deliberate:
    //
    //   1. PLAN     recompute what should be resident around the camera, and
    //               at what level, and refill pending_loads().
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
                       glm::vec3 camera_pos,
                       StepMode mode = StepMode::Budgeted);

    // Chunks the caller should build, NEAREST FIRST, each with the level to
    // build it at.
    const std::vector<ChunkRequest>& pending_loads() const { return loads_; }

    // Coordinates evicted by the most recent step. Telemetry and tests.
    const std::vector<ChunkCoord>& last_evictions() const { return evicted_; }

    // Report a built chunk back. Takes the mesh's id and bounds, not the mesh:
    // the streamer needs to know how big it is and what to draw, and has no
    // business holding vertex data the host has already uploaded.
    //
    // `lod` must be the level the request asked for. A delivery for a chunk
    // that is no longer wanted — because the camera moved on while it was
    // being built, or because the level it was built at is no longer the level
    // wanted — is DROPPED, and its mesh id goes straight onto the released
    // list so the host frees it instead of leaking it. A drop cannot leave a
    // hole: the next plan sees the chunk is neither resident at the right
    // level nor in flight, and simply asks again.
    void deliver(ChunkCoord c, int lod, MeshId chunk_mesh, const AABB& bounds);

    // ---- what the host must free -------------------------------------------
    //
    // MeshIds nothing in the scene references any more: evicted chunks,
    // dropped deliveries, and the old mesh of a chunk that changed level.
    //
    // DRAINED BY THIS CALL. It appends and then clears, so the ids are handed
    // over exactly once and there is no second call to forget. Draining rather
    // than exposing a const list is deliberate — the list is filled both by
    // step() and by deliver(), which happen in either order relative to the
    // host's frame, and a "read it after step()" contract would silently miss
    // everything deliver() added.
    void take_released_meshes(std::vector<MeshId>& out);

    // ---- residency queries --------------------------------------------------

    bool resident(ChunkCoord c) const { return resident_.count(c) != 0; }
    std::size_t resident_count() const { return resident_.size(); }

    // The level a resident chunk is currently built at, or -1 if not resident.
    int resident_lod(ChunkCoord c) const;

    // The level this chunk WANTS to be at, given the LAST PLANNED CENTRE.
    //
    // Note which centre. centre_ only moves when plan() runs, so between a
    // camera jump and the next step this still describes the world the player
    // just left. That is correct for asking "what did the last plan decide"
    // and wrong for asking "is the place I am about to be ready", which is why
    // ready() measures about the position it is handed instead. Getting those
    // two confused made a teleport report a 0 ms fill and resume on coarse
    // ground.
    int lod_for(ChunkCoord c) const;

    // Chunks resident at each level. Index is the level. For telemetry and for
    // the tier assertions in the test suite.
    void residency_by_lod(std::size_t out[kMaxChunkLod + 1]) const;

    // Every chunk within prime_radius is resident AT THE LEVEL IT WANTS.
    //
    // The level clause matters: a chunk resident at level 3 under the car is
    // resident, and it is also the wrong ground to hand physics. Resuming on it
    // would put the car on a chord.
    bool ready(glm::vec3 camera_pos) const;

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
        int lod = 0;
        MeshId mesh = kInvalidId;
        AABB bounds;
        std::vector<ScatterProp> props;
        std::size_t cursor = 0;      // next prop index to instantiate
        bool terrain_done = false;
        NodeId terrain_node = kInvalidId;
        std::vector<NodeId> prop_nodes;

        // A resident chunk changing level, rather than a new one arriving.
        // Its terrain node already exists and is re-pointed in place, so a
        // level change never opens a hole the player can see through.
        bool refit = false;
    };

    struct Resident {
        int lod = 0;
        MeshId mesh = kInvalidId;
        NodeId terrain_node = kInvalidId;
        std::vector<NodeId> prop_nodes;
    };

    static StreamerConfig normalised(StreamerConfig cfg);

    // The level `c` wants, measured about an ARBITRARY centre rather than the
    // last planned one. The centre is a parameter precisely because the two
    // callers need different ones — see the note on lod_for().
    int lod_about(ChunkCoord c, ChunkCoord centre) const;

    void plan(glm::vec3 camera_pos, StepMode mode);
    void evict(Scene& scene);
    void activate(Scene& scene, const ScenePrototypes& proto,
                  StreamerStats& stats, StepMode mode);

    // Create every prop node for the chunk in `active_`, from its cursor,
    // spending at most `budget`. Returns instances made.
    int activate_props(Scene& scene, const ScenePrototypes& proto, int& budget);

    bool outside_evict_radius(ChunkCoord c) const;
    void release(MeshId id);

    uint64_t seed_;
    StreamerConfig cfg_;

    ChunkCoord centre_{0, 0};

    std::unordered_map<ChunkCoord, Resident, ChunkCoordHash> resident_;

    // Coord -> the level it was requested at. A map rather than a set because
    // a delivery has to be checked against the level that was ASKED for, not
    // against whatever the level would be now.
    std::unordered_map<ChunkCoord, int, ChunkCoordHash> in_flight_;

    // Delivered, not yet started. Nearest-first selection happens at
    // activation time rather than at delivery time, so a chunk that became the
    // nearest while it sat in this queue still gets priority.
    std::vector<Activating> delivered_;

    // The one chunk mid-activation, carried between steps. At most one: two
    // half-built chunks would both be invisible to the player for twice as
    // long, for no gain.
    std::optional<Activating> active_;

    std::vector<ChunkRequest> loads_;
    std::vector<ChunkCoord> evicted_;
    std::vector<MeshId> released_;

    // Reused across steps so eviction does not allocate on the frame it is
    // most likely to be expensive.
    std::vector<NodeId> doomed_scratch_;
};

}  // namespace apricot
