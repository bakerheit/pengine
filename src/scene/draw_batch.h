#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "scene/scene.h"

namespace apricot {

// Batch planning for the instanced draw path. Pure, GL-free and headless-
// testable: it walks the cull-sorted visible list and partitions it into
// contiguous runs. The renderer executes the plan; it does not make it. One
// truth, shared by the renderer and the tests.

// The batch key. Two nodes may share a draw ONLY if their keys are equal —
// mesh and material are per-BATCH state, uploaded once per instanced draw.
// tint and uv_scale are deliberately absent: they ride per-instance
// attributes, so nodes differing only in those still collapse together. That
// is what merges a scattered hillside into a single draw.
//
// Anything added to Renderable that must differ per node has to be added to
// the per-instance payload, NOT to this key. Adding it here instead is how a
// batching system silently degrades to one draw per object while still
// "working".
constexpr uint64_t batch_key(const Renderable& r) {
    return (static_cast<uint64_t>(r.material) << 32) |
           static_cast<uint64_t>(r.mesh);
}

// A node with no mesh or no material draws nothing and can never batch.
constexpr bool batchable(const Renderable& r) {
    return r.mesh != kInvalidId && r.material != kInvalidId;
}

// Shortest run worth switching to the instanced path. Below this, the program
// switch plus the instance-buffer upload cost more than the draw calls saved.
// Tunable — A/B it in-game, do not theorise about it.
inline constexpr int kMinInstancedRun = 4;

struct DrawBatch {
    std::size_t first = 0;      // index into the visible span
    int count = 0;              // run length
    bool instanced = false;     // true = one instanced draw for the whole run
    uint64_t key = 0;           // batch key; meaningless when !instanced
};

// Partition [0, visible.size()) into contiguous batches.
//
// Requires `visible` to be sorted by batch_key() — Scene::cull guarantees
// that, and it is the whole reason equal keys are adjacent and collapsible.
// Feeding an unsorted list is not an error and will not crash; it just finds
// almost no runs, which reads in a profile as "instancing does nothing".
//
// Runs of at least `min_run` batchable same-key nodes become instanced
// batches. Everything between them coalesces into plain batches that the
// renderer walks one node at a time.
std::vector<DrawBatch> plan_draw_batches(const Scene& scene,
                                         const std::vector<NodeId>& visible,
                                         int min_run = kMinInstancedRun);

}  // namespace apricot
