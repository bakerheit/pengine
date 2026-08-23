// What the renderer assumes about the batch plan.
//
// scene/draw_batch.h makes the plan and gfx/renderer.cpp executes it. The
// renderer walks it ONCE, straight through, and never checks whether the plan
// covered everything — checking per frame would cost more than the batching
// saves. So the coverage guarantee has to be pinned here instead, or a plan
// that quietly skips a stretch of the visible list becomes a hole in the world
// that appears only at certain camera angles.
//
// This drives the REAL producer end to end: a real Scene, filled the way the
// demo world fills it, culled by a real Frustum from a real projection, then
// planned by the real planner. A hand-built visible list would be sorted the
// way this file happened to sort it, which is exactly the assumption under
// test.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "core/rng.h"
#include "scene/draw_batch.h"
#include "scene/scene.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// A scene shaped like the demo world: many nodes sharing a few mesh/material
// pairs, scattered, plus a few one-off nodes and a few with nothing to draw.
void fill_scene(Scene& scene, uint64_t seed, int count) {
    const AABB unit{glm::vec3{-0.5f}, glm::vec3{0.5f}};

    for (int i = 0; i < count; ++i) {
        Rng r = rng_at(seed, i, 0, 0x5CE1u);

        Renderable rend;
        if (i % 97 == 0) {
            // A node with nothing to draw. These exist in any real scene
            // (spawn markers, trigger volumes) and the planner has to step
            // over them without losing its place.
            rend.mesh = kInvalidId;
            rend.material = kInvalidId;
        } else {
            rend.mesh = static_cast<MeshId>(i % 3);
            rend.material = static_cast<MaterialId>(i % 4);
        }
        // Differ per node in exactly the fields the batch key ignores.
        rend.tint = glm::vec4{r.next_float(), r.next_float(), r.next_float(), 1.0f};
        rend.uv_scale = glm::vec2{r.range(1.0f, 4.0f), r.range(1.0f, 4.0f)};

        Transform t;
        t.position = glm::vec3{r.range(-300.0f, 300.0f), r.range(0.0f, 20.0f),
                               r.range(-300.0f, 300.0f)};
        t.scale = glm::vec3{r.range(1.0f, 6.0f), r.range(1.0f, 9.0f),
                            r.range(1.0f, 6.0f)};

        scene.create(rend, t, unit);
    }
    scene.update();
}

Frustum camera_frustum(glm::vec3 eye, glm::vec3 target) {
    // Guard the degenerate look. When the view direction is nearly parallel to
    // world up, lookAt's basis collapses and every plane comes out garbage, so
    // the cull rejects the entire world and the suite "passes" by testing
    // nothing. gfx/camera.h clamps pitch for exactly this reason; a test that
    // builds its own view matrix has to do the same or it is not testing the
    // same thing the game does.
    glm::vec3 dir = target - eye;
    const float flat = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    REQUIRE_MSG(flat > 1e-3f, "test camera is looking straight down", "camera");

    const glm::mat4 proj =
        glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.15f, 800.0f);
    const glm::mat4 view = glm::lookAt(eye, target, glm::vec3{0.0f, 1.0f, 0.0f});
    return Frustum::from_view_proj(proj * view);
}

void the_plan_covers_the_visible_list_exactly_once() {
    Scene scene;
    fill_scene(scene, 0xA5EED0FFC0FFEE11ull, 2000);

    // Several camera placements, because the coverage bug that matters is the
    // one that only appears at certain angles.
    // None of these sits directly above the target: a camera looking straight
    // down collapses lookAt's basis and culls the world, and a suite that culls
    // the world passes by testing nothing.
    const glm::vec3 eyes[] = {
        {0.0f, 12.0f, 90.0f},
        {250.0f, 40.0f, 250.0f},
        {-400.0f, 5.0f, 10.0f},
        {40.0f, 300.0f, 260.0f},   // steep, but not vertical
    };

    for (const glm::vec3& eye : eyes) {
        const Scene::CullResult& culled =
            scene.cull(camera_frustum(eye, glm::vec3{0.0f, 5.0f, 0.0f}), eye,
                       700.0f);
        const std::vector<DrawBatch> plan = plan_draw_batches(scene, culled.visible);

        // Contiguous, starting at zero, ending at the end. This is exactly what
        // lets the renderer walk the plan without tracking what it has covered.
        std::size_t cursor = 0;
        for (const DrawBatch& b : plan) {
            REQUIRE_MSG(b.first == cursor, "plan has a gap or an overlap",
                        "coverage");
            REQUIRE_MSG(b.count > 0, "empty batch in the plan", "coverage");
            cursor = b.first + static_cast<std::size_t>(b.count);
        }
        REQUIRE_MSG(cursor == culled.visible.size(),
                    "plan does not reach the end of the visible list",
                    "coverage");
    }
    apricot_test::pass("the plan partitions the visible list with no gaps");
}

void every_instanced_run_really_does_share_one_key() {
    Scene scene;
    fill_scene(scene, 0xD15EA5Eull, 1500);

    const glm::vec3 eye{60.0f, 25.0f, 60.0f};
    const Scene::CullResult& culled =
        scene.cull(camera_frustum(eye, glm::vec3{0.0f}), eye, 700.0f);
    const std::vector<DrawBatch> plan = plan_draw_batches(scene, culled.visible);

    int instanced = 0;
    for (const DrawBatch& b : plan) {
        if (!b.instanced) continue;
        ++instanced;

        REQUIRE_MSG(b.count >= kMinInstancedRun,
                    "a run shorter than the minimum was marked instanced",
                    "runs");

        for (int i = 0; i < b.count; ++i) {
            const SceneNode* n =
                scene.get(culled.visible[b.first + static_cast<std::size_t>(i)]);
            REQUIRE_MSG(n != nullptr, "instanced run names a dead node", "runs");
            // The renderer takes the mesh and material for the WHOLE draw from
            // b.key. A node in the run with a different key would be drawn with
            // somebody else's geometry.
            REQUIRE_MSG(batch_key(n->renderable) == b.key,
                        "node in an instanced run has a different batch key",
                        "runs");
            REQUIRE_MSG(batchable(n->renderable),
                        "unbatchable node swept into an instanced run", "runs");
        }
    }
    REQUIRE_MSG(instanced > 0,
                "no runs collapsed at all — either the cull sort stopped "
                "sorting or the test scene stopped sharing meshes",
                "runs");
    apricot_test::pass("every instanced run is genuinely one key");
}

void walking_the_plan_draws_each_node_exactly_once() {
    Scene scene;
    fill_scene(scene, 0xBEEFCAFEull, 1800);

    const glm::vec3 eye{0.0f, 30.0f, 200.0f};
    const Scene::CullResult& culled =
        scene.cull(camera_frustum(eye, glm::vec3{0.0f}), eye, 700.0f);
    const std::vector<DrawBatch> plan = plan_draw_batches(scene, culled.visible);

    // Mirror gfx/renderer.cpp's walk exactly: instanced batches submit their
    // whole run, plain stretches submit one node at a time.
    std::vector<int> drawn(culled.visible.size(), 0);
    int submitted = 0;
    for (const DrawBatch& b : plan) {
        for (int i = 0; i < b.count; ++i) {
            const std::size_t idx = b.first + static_cast<std::size_t>(i);
            const SceneNode* n = scene.get(culled.visible[idx]);
            if (!b.instanced && (!n || !batchable(n->renderable))) continue;
            ++drawn[idx];
            ++submitted;
        }
    }

    int expected = 0;
    for (std::size_t i = 0; i < culled.visible.size(); ++i) {
        const SceneNode* n = scene.get(culled.visible[i]);
        const bool should_draw = n && batchable(n->renderable);
        if (should_draw) ++expected;
        REQUIRE_MSG(drawn[i] <= 1, "a node was submitted twice", "walk");
        if (should_draw) {
            REQUIRE_MSG(drawn[i] == 1, "a drawable node was never submitted",
                        "walk");
        }
    }
    REQUIRE(submitted == expected);
    REQUIRE_MSG(expected > 0, "the test culled everything away", "walk");
    apricot_test::pass("the renderer's walk covers every drawable node once");
}

void batching_is_worth_having() {
    // The claim the debug overlay's A/B toggle makes. If the sorted plan does
    // not collapse dramatically fewer draws than one-per-node, then either the
    // cull sort or the planner has stopped working, and the toggle would be
    // measuring nothing.
    Scene scene;
    fill_scene(scene, 0x1234567890ABCDEFull, 2000);

    // High and angled down across the field, so plenty is in shot.
    const glm::vec3 eye{0.0f, 80.0f, 320.0f};
    const Scene::CullResult& culled =
        scene.cull(camera_frustum(eye, glm::vec3{0.0f, 0.0f, 0.0f}), eye, 900.0f);
    const std::vector<DrawBatch> plan = plan_draw_batches(scene, culled.visible);

    int batched_draws = 0;
    int naive_draws = 0;
    for (const DrawBatch& b : plan) {
        if (b.instanced) {
            ++batched_draws;
            naive_draws += b.count;
            continue;
        }
        for (int i = 0; i < b.count; ++i) {
            const SceneNode* n =
                scene.get(culled.visible[b.first + static_cast<std::size_t>(i)]);
            if (!n || !batchable(n->renderable)) continue;
            ++batched_draws;
            ++naive_draws;
        }
    }

    REQUIRE_MSG(naive_draws > 100, "not enough visible geometry to judge",
                "win");
    REQUIRE_MSG(batched_draws * 4 < naive_draws,
                "batching saved less than a 4x reduction in draw calls; the "
                "cull's batch-key sort is the first suspect",
                "win");
    std::printf("       %d draws batched vs %d naive (%.1fx)\n", batched_draws,
                naive_draws,
                static_cast<double>(naive_draws) / static_cast<double>(batched_draws));
    apricot_test::pass("batching collapses the draw count by a wide margin");
}

}  // namespace

int main() {
    the_plan_covers_the_visible_list_exactly_once();
    every_instanced_run_really_does_share_one_key();
    walking_the_plan_draws_each_node_exactly_once();
    batching_is_worth_having();
    return apricot_test::done("render_batch_contract_tests");
}
