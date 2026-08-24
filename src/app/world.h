#pragma once

#include <cstdint>
#include <vector>

#include "gfx/renderer.h"
#include "gfx/road_meshes.h"
#include "road/road_graph.h"
#include "scene/scene.h"
#include "terrain/streamer.h"

namespace apricot {

// The host half of terrain streaming: build, upload, deliver, free.
//
// The POLICY is not here. Which chunks should exist, at what level, in what
// order, and when to let go of them all live in terrain/Streamer, which is
// sim-side and headless-testable and has a suite that drives it around a world
// for two thousand steps. What is left over is the part that genuinely needs a
// GL context — turning a ChunkMesh into a MeshId and giving the MeshId back —
// and that is all this file does.
//
// It replaces app/demo_scene.{h,cpp}, which was a 420 m disc and 1400 boxes and
// said in its own header that it was meant to be deleted.
//
// SINGLE THREADED, ON PURPOSE, FOR NOW. build_chunk() is pure and thread safe
// precisely so this could farm the work out, and the budgets exist so that not
// doing so is survivable: steady state at 40 m/s needs about 5.6 chunks a
// second against a ceiling of far more. Threading it is a real follow-up and
// not a pretend one, but it is not this ticket, and a thread pool added
// speculatively would be a thread pool nobody had measured the need for.
class World {
public:
    struct Stats {
        // This frame.
        int chunks_built = 0;
        int quads_built = 0;
        int chunks_refitted = 0;
        int chunks_evicted = 0;
        int meshes_freed = 0;
        int instances_activated = 0;
        bool budget_exhausted = false;

        // Right now.
        std::size_t resident_chunks = 0;
        std::size_t resident_by_lod[kMaxChunkLod + 1] = {0, 0, 0, 0};
        std::size_t live_meshes = 0;
        std::size_t mesh_bytes = 0;
    };

    bool init(Renderer& renderer, uint64_t seed, const StreamerConfig& cfg);

    // Drop every node and every mesh. Must run while the GL context is alive.
    void shutdown(Scene& scene, Renderer& renderer);

    // One frame of residency. Steps the streamer, then builds and uploads
    // everything it asked for and frees everything it handed back.
    void update(Scene& scene, Renderer& renderer, glm::vec3 focus,
                StepMode mode = StepMode::Budgeted);

    // Load the prime ring with the budgets off, and do not return until the
    // streamer says the world under `focus` is ready.
    //
    // This is the cold start and the mission teleport. Without it the player is
    // dropped into a hole and the world arrives around them over the following
    // second, which docs/design/pinatty.md calls a blocker in a way steady
    // state streaming is not. Returns how many fill steps it took.
    //
    // IT TAKES NO CLOCK AND REPORTS NO TIME. App owns the program's only wall
    // clock; the caller times this and resets the frame clock afterwards, or
    // the fill is charged to the next frame as dropped sim time.
    int fill(Scene& scene, Renderer& renderer, glm::vec3 focus);

    // Bake `spines` into ribbons, upload them and put them in the scene.
    //
    // NOTHING IN THE ENGINE SUPPLIES SPINES YET. src/road/road_graph.h says so
    // itself: the spine tables belong to the map module (PENG-41) and this
    // module takes them as a parameter, so wiring is one call the day they
    // land. Until then App passes an empty list and no road draws. That is not
    // a stub — the bake, the upload and the draw are all real and all run; they
    // are simply run over nothing.
    //
    // An empty spine list is a success and produces no geometry. Returns false
    // only if a bake that HAD geometry failed to reach the GPU.
    bool set_roads(Renderer& renderer, Scene& scene,
                   const std::vector<RoadSpine>& spines);

    const Streamer& streamer() const { return streamer_; }
    const RoadMeshes& roads() const { return roads_; }
    const Stats& stats() const { return stats_; }

private:
    // Terrain shares ONE material across every chunk and every level, so the
    // batcher collapses the whole visible ring into a handful of draws keyed by
    // mesh. Per-chunk materials would give every chunk its own batch key and
    // instancing would quietly stop finding runs.
    Streamer streamer_{0};
    ScenePrototypes proto_;
    RoadMeshes roads_;
    uint64_t seed_ = 0;

    std::vector<MeshId> released_scratch_;
    Stats stats_;

};

}  // namespace apricot
