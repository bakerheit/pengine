#pragma once

#include <cstddef>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "terrain/chunk.h"

namespace apricot {

// Chunk residency: decides which chunks should exist right now, and hands the
// caller a budgeted list of work.
//
// The streamer PLANS. It does not build meshes and it does not upload them —
// build_chunk() is pure and thread-safe precisely so the host can farm the
// work out however it likes. Keeping the policy here, free of GL and of
// threads, is what makes "does the world load ahead of the player" a headless
// test rather than a thing you squint at while driving.
//
// Load and evict radii are deliberately different. A single radius means a
// player idling exactly on a boundary thrashes the same chunk in and out
// forever; the gap is the hysteresis that stops it.

struct StreamerConfig {
    // Chunks loaded in each direction from the camera's chunk.
    int load_radius = 4;

    // Must be > load_radius. See the hysteresis note above.
    int evict_radius = 6;

    // Ceiling on chunks handed out per update. Loading is spread over frames
    // so crossing a boundary does not dump a few hundred mesh builds into one
    // frame — the hitch that produces is far more noticeable than the chunk
    // arriving two frames later.
    // TODO(streaming ticket): budget by VERTEX COUNT, not chunk count. A flat
    // per-chunk budget is only correct while every chunk costs the same, which
    // stops being true the moment terrain LOD or scatter props land.
    int max_loads_per_update = 2;
};

class Streamer {
public:
    explicit Streamer(StreamerConfig cfg = {}) : cfg_(cfg) {}

    // Recompute residency for a camera position. Pure in its inputs: no clock
    // read, no allocation beyond the work lists. Call once per sim step.
    void update(glm::vec3 camera_pos);

    // Chunks that should be built this update, NEAREST FIRST. The caller
    // builds them (however it likes) and reports back via mark_resident().
    const std::vector<ChunkCoord>& pending_loads() const { return loads_; }

    // Chunks that have left the evict radius. The caller drops their meshes
    // and reports back via mark_evicted().
    const std::vector<ChunkCoord>& pending_evictions() const { return evicts_; }

    // Residency bookkeeping. Split from update() on purpose: a chunk is not
    // resident when the streamer *asked* for it, it is resident when the work
    // actually finished — which may be several frames later on a worker
    // thread. Marking it early is how a chunk ends up permanently missing.
    void mark_resident(ChunkCoord c);
    void mark_evicted(ChunkCoord c);

    bool resident(ChunkCoord c) const { return resident_.count(c) != 0; }
    std::size_t resident_count() const { return resident_.size(); }

    const StreamerConfig& config() const { return cfg_; }

private:
    StreamerConfig cfg_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> resident_;
    std::unordered_set<ChunkCoord, ChunkCoordHash> in_flight_;
    std::vector<ChunkCoord> loads_;
    std::vector<ChunkCoord> evicts_;
};

}  // namespace apricot
