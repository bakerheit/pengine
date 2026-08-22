#include "scene/draw_batch.h"

namespace apricot {

std::vector<DrawBatch> plan_draw_batches(const Scene& scene,
                                         const std::vector<NodeId>& visible,
                                         int min_run) {
    std::vector<DrawBatch> out;
    const std::size_t count = visible.size();
    const std::size_t run_min =
        min_run > 0 ? static_cast<std::size_t>(min_run) : std::size_t{1};

    // Nodes not swept into an instanced run coalesce into the surrounding
    // plain stretch, so the renderer walks one uninterrupted per-node loop
    // rather than a batch record per stray object.
    std::size_t plain_start = 0;
    auto flush_plain = [&](std::size_t end) {
        if (end > plain_start) {
            out.push_back(DrawBatch{plain_start,
                                    static_cast<int>(end - plain_start),
                                    false, 0});
        }
    };

    std::size_t i = 0;
    while (i < count) {
        const SceneNode* n = scene.get(visible[i]);
        if (!n || !batchable(n->renderable)) {
            ++i;
            continue;
        }

        const uint64_t key = batch_key(n->renderable);

        // Extend the same-key run. The cull sort keeps equal keys adjacent, so
        // this finds the entire candidate batch in one sweep.
        std::size_t j = i + 1;
        while (j < count) {
            const SceneNode* m = scene.get(visible[j]);
            if (!m || !batchable(m->renderable) ||
                batch_key(m->renderable) != key) {
                break;
            }
            ++j;
        }

        if (j - i >= run_min) {
            flush_plain(i);
            out.push_back(DrawBatch{i, static_cast<int>(j - i), true, key});
            plain_start = j;
        }
        // A run shorter than min_run is left inside the plain stretch: the
        // per-node path is cheaper than a program switch for two objects.
        i = j;
    }

    flush_plain(count);
    return out;
}

}  // namespace apricot
