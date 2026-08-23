// Scene culling and draw-batch planning.
//
// Both halves fail quietly rather than loudly, which is why they are pinned
// here:
//
//   * A frustum plane that never rejects costs performance and nothing else,
//     so nobody notices. A plane that rejects too eagerly punches a hole in
//     the world that only appears at one camera angle.
//   * A batch planner that finds no runs still draws a correct frame. It just
//     draws it one object at a time, and the only symptom is a number in a
//     profile that nobody is looking at.
//
// Every test here builds a REAL Scene and culls it with a REAL Frustum
// extracted from a real projection, rather than hand-feeding planes. A
// consumer test on hand-built inputs passes happily while the real producer
// feeds garbage.

#include <algorithm>
#include <cstdint>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "core/aabb.h"
#include "core/frustum.h"
#include "scene/draw_batch.h"
#include "scene/scene.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// A unit-ish box centred on the origin in local space. Node transforms move it.
AABB unit_box(float half = 0.5f) {
    AABB b;
    b.expand(glm::vec3{-half});
    b.expand(glm::vec3{half});
    return b;
}

Transform at(glm::vec3 p) {
    Transform t;
    t.position = p;
    return t;
}

Renderable renderable(MeshId mesh, MaterialId material) {
    Renderable r;
    r.mesh = mesh;
    r.material = material;
    return r;
}

// An orthographic frustum makes the six planes exactly addressable: in world
// space it is the box x,y in [-10, 10] and z in [-100, -1]. A perspective
// frustum would work too, but then "just outside the left plane" depends on
// depth and the test stops being about the plane.
Frustum ortho_frustum() {
    const glm::mat4 proj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 100.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3{0.0f, 0.0f, 0.0f},
                                       glm::vec3{0.0f, 0.0f, -1.0f},
                                       glm::vec3{0.0f, 1.0f, 0.0f});
    return Frustum::from_view_proj(proj * view);
}

bool visible_at(Scene& scene, const Frustum& f, glm::vec3 pos) {
    scene.clear();
    const NodeId id = scene.create(renderable(1, 1), at(pos), unit_box());
    scene.update();
    const Scene::CullResult& r = scene.cull(f, glm::vec3{0.0f}, 0.0f);
    REQUIRE(r.total == 1);
    return !r.visible.empty() && r.visible[0] == id;
}

// --- the six planes ------------------------------------------------------------
void every_frustum_plane_rejects_and_only_its_own_side() {
    Scene scene;
    const Frustum f = ortho_frustum();

    // Dead centre of the volume.
    REQUIRE_MSG(visible_at(scene, f, glm::vec3{0.0f, 0.0f, -50.0f}),
                "a box in the middle of the frustum was culled", "centre");

    struct Case {
        const char* name;
        glm::vec3 outside;  // must be culled
        glm::vec3 inside;   // just the other side of the same plane, must not be
    };
    // Each pair differs only along the axis that plane owns, so a failure names
    // exactly one plane. The `inside` half is what catches a plane that has
    // been made over-eager: without it, a cull() that rejected everything would
    // pass all six rejection checks.
    const Case cases[] = {
        {"left",   {-20.0f,   0.0f, -50.0f}, { -9.0f,   0.0f, -50.0f}},
        {"right",  { 20.0f,   0.0f, -50.0f}, {  9.0f,   0.0f, -50.0f}},
        {"bottom", {  0.0f, -20.0f, -50.0f}, {  0.0f,  -9.0f, -50.0f}},
        {"top",    {  0.0f,  20.0f, -50.0f}, {  0.0f,   9.0f, -50.0f}},
        {"near",   {  0.0f,   0.0f,  10.0f}, {  0.0f,   0.0f,  -2.0f}},
        {"far",    {  0.0f,   0.0f, -150.0f}, { 0.0f,   0.0f, -99.0f}},
    };

    for (const Case& c : cases) {
        REQUIRE_MSG(!visible_at(scene, f, c.outside),
                    "a box outside this plane was not culled", c.name);
        REQUIRE_MSG(visible_at(scene, f, c.inside),
                    "a box inside this plane was culled anyway", c.name);
    }
    apricot_test::pass("all six frustum planes reject their own side only");
}

void a_node_with_no_bounds_never_draws() {
    Scene scene;
    const Frustum f = ortho_frustum();

    // An inverted (never-expanded) AABB passes every plane test, because every
    // comparison against +/-FLT_MAX succeeds. Left unchecked it would draw at
    // every distance, forever, from anywhere.
    scene.create(renderable(1, 1), at(glm::vec3{0.0f, 0.0f, -50.0f}), AABB{});
    scene.update();
    const Scene::CullResult& r = scene.cull(f, glm::vec3{0.0f}, 0.0f);
    REQUIRE(r.total == 1);
    REQUIRE_MSG(r.visible.empty(), "a node with invalid bounds was drawn",
                "invalid bounds");
    REQUIRE(r.culled == 1);
    apricot_test::pass("a node with invalid bounds is never drawn");
}

void authored_invisibility_and_removal_are_respected() {
    Scene scene;
    const Frustum f = ortho_frustum();

    const NodeId a = scene.create(renderable(1, 1), at(glm::vec3{0, 0, -50}), unit_box());
    const NodeId b = scene.create(renderable(1, 1), at(glm::vec3{1, 0, -50}), unit_box());
    const NodeId c = scene.create(renderable(1, 1), at(glm::vec3{2, 0, -50}), unit_box());
    scene.update();

    scene.get(b)->visible = false;
    scene.remove(c);

    const Scene::CullResult& r = scene.cull(f, glm::vec3{0.0f}, 0.0f);
    // A hidden node is not "culled", it is not a candidate at all: counting it
    // makes the cull statistics lie about how much work the frustum saved.
    REQUIRE(r.total == 1);
    REQUIRE(r.visible.size() == 1u);
    REQUIRE(r.visible[0] == a);
    apricot_test::pass("hidden and removed nodes are not candidates");
}

// --- distance ------------------------------------------------------------------
void distance_culling_only_ever_shortens() {
    Scene scene;
    // A frustum big enough that only distance decides anything.
    const glm::mat4 proj = glm::ortho(-5000.0f, 5000.0f, -5000.0f, 5000.0f,
                                      1.0f, 10000.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3{0.0f, 0.0f, 0.0f},
                                       glm::vec3{0.0f, 0.0f, -1.0f},
                                       glm::vec3{0.0f, 1.0f, 0.0f});
    const Frustum f = Frustum::from_view_proj(proj * view);
    const glm::vec3 cam{0.0f, 0.0f, 0.0f};

    // near = 50 m out, far = 500 m out.
    const NodeId near_id =
        scene.create(renderable(1, 1), at(glm::vec3{0, 0, -50}), unit_box());
    const NodeId far_id =
        scene.create(renderable(1, 1), at(glm::vec3{0, 0, -500}), unit_box());
    scene.update();

    auto sees = [&](const Scene::CullResult& r, NodeId id) {
        return std::find(r.visible.begin(), r.visible.end(), id) !=
               r.visible.end();
    };

    // Global limit of 100 m: near survives, far does not.
    {
        const Scene::CullResult& r = scene.cull(f, cam, 100.0f);
        REQUIRE(sees(r, near_id));
        REQUIRE_MSG(!sees(r, far_id), "global distance limit did not apply",
                    "global");
    }

    // max_dist <= 0 disables distance culling entirely (the debug fly-cam).
    {
        const Scene::CullResult& r = scene.cull(f, cam, 0.0f);
        REQUIRE(sees(r, near_id));
        REQUIRE_MSG(sees(r, far_id), "distance culling ran when disabled",
                    "disabled");
    }

    // A per-node limit SHORTER than the global one applies.
    scene.get(near_id)->max_draw_distance = 10.0f;
    {
        const Scene::CullResult& r = scene.cull(f, cam, 1000.0f);
        REQUIRE_MSG(!sees(r, near_id), "a shorter per-node limit was ignored",
                    "shorten");
    }

    // A per-node limit LONGER than the global one must NOT extend visibility.
    // This is the rule the whole streaming budget leans on: one authored value
    // must not be able to drag an object in from the far side of the world.
    scene.get(near_id)->max_draw_distance = 0.0f;
    scene.get(far_id)->max_draw_distance = 100000.0f;
    {
        const Scene::CullResult& r = scene.cull(f, cam, 100.0f);
        REQUIRE_MSG(!sees(r, far_id),
                    "a per-node limit extended visibility past the global one",
                    "extend");
    }
    apricot_test::pass("per-node draw distance only ever shortens");
}

void distance_is_measured_to_the_nearest_point_of_the_box() {
    Scene scene;
    const glm::mat4 proj = glm::ortho(-5000.0f, 5000.0f, -5000.0f, 5000.0f,
                                      1.0f, 10000.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3{0.0f, 0.0f, 0.0f},
                                       glm::vec3{0.0f, 0.0f, -1.0f},
                                       glm::vec3{0.0f, 1.0f, 0.0f});
    const Frustum f = Frustum::from_view_proj(proj * view);

    // A terrain-chunk-sized box: 64 m across, near edge 80 m from the camera,
    // centre 112 m away. With a 100 m limit a centre-distance test culls it
    // while a third of it is still comfortably on screen — the ground vanishing
    // ahead of the player.
    AABB chunk;
    chunk.expand(glm::vec3{-32.0f, -5.0f, -144.0f});
    chunk.expand(glm::vec3{32.0f, 5.0f, -80.0f});

    const NodeId id = scene.create(renderable(1, 1), Transform{}, chunk);
    scene.update();

    const Scene::CullResult& r = scene.cull(f, glm::vec3{0.0f}, 100.0f);
    REQUIRE_MSG(!r.visible.empty() && r.visible[0] == id,
                "a box whose near edge is inside the limit was culled",
                "closest point");

    // And a box containing the camera is never distance-culled, however small
    // the limit. A centre test gets this wrong for anything large.
    scene.clear();
    AABB around;
    around.expand(glm::vec3{-500.0f});
    around.expand(glm::vec3{500.0f});
    const NodeId big = scene.create(renderable(1, 1), Transform{}, around);
    scene.update();
    const Scene::CullResult& r2 = scene.cull(f, glm::vec3{0.0f}, 1.0f);
    REQUIRE_MSG(!r2.visible.empty() && r2.visible[0] == big,
                "a box containing the camera was distance-culled",
                "closest point");

    apricot_test::pass("distance is measured to the box, not to its centre");
}

// --- ordering ------------------------------------------------------------------
void cull_output_is_sorted_and_deterministic() {
    Scene scene;
    const Frustum f = ortho_frustum();

    // Created in deliberately scrambled key order.
    const MeshId meshes[] = {7, 2, 7, 4, 2, 9, 4, 2};
    const MaterialId mats[] = {3, 1, 1, 2, 1, 3, 2, 3};
    for (int i = 0; i < 8; ++i) {
        scene.create(renderable(meshes[i], mats[i]),
                     at(glm::vec3{static_cast<float>(i) * 0.1f, 0.0f, -50.0f}),
                     unit_box(0.05f));
    }
    scene.update();

    std::vector<NodeId> first;
    {
        const Scene::CullResult& r = scene.cull(f, glm::vec3{0.0f}, 0.0f);
        REQUIRE(r.visible.size() == 8u);
        first = r.visible;
    }

    // Non-decreasing by batch key, and strictly increasing by id within a key.
    for (std::size_t i = 1; i < first.size(); ++i) {
        const uint64_t ka = batch_key(scene.get(first[i - 1])->renderable);
        const uint64_t kb = batch_key(scene.get(first[i])->renderable);
        REQUIRE_MSG(ka <= kb, "cull output is not sorted by batch key", "sort");
        if (ka == kb) {
            REQUIRE_MSG(first[i - 1] < first[i],
                        "equal keys are not ordered by id", "tie-break");
        }
    }

    // std::sort is not stable, so without the id tie-break this could differ
    // between calls, between library versions, or between two machines running
    // the same seed. In a world that is a pure function of a seed, the draw
    // order has to be one too.
    for (int rep = 0; rep < 8; ++rep) {
        const Scene::CullResult& r = scene.cull(f, glm::vec3{0.0f}, 0.0f);
        REQUIRE_MSG(r.visible == first, "cull order varied between calls",
                    "determinism");
    }
    apricot_test::pass("cull output is key-sorted and fully deterministic");
}

// --- batching ------------------------------------------------------------------
void batches_collapse_at_exactly_the_expected_boundaries() {
    Scene scene;
    const Frustum f = ortho_frustum();

    // Built so the SORTED order is known exactly. batch_key packs material into
    // the high word and mesh into the low, so the sort runs material-major.
    //
    //   key (mat 1, mesh 1) x5  -> a run of 5, instanced
    //   key (mat 1, mesh 2) x2  -> too short, absorbed into the plain stretch
    //   key (mat 2, mesh 1) x4  -> exactly kMinInstancedRun, instanced
    //   key (mat 3, mesh 9) x1  -> too short, trailing plain stretch
    struct Group { MaterialId material; MeshId mesh; int count; };
    const Group groups[] = {{2, 1, 4}, {1, 2, 2}, {3, 9, 1}, {1, 1, 5}};

    int placed = 0;
    for (const Group& g : groups) {
        for (int i = 0; i < g.count; ++i) {
            scene.create(renderable(g.mesh, g.material),
                         at(glm::vec3{static_cast<float>(placed) * 0.05f, 0.0f,
                                      -50.0f}),
                         unit_box(0.02f));
            ++placed;
        }
    }
    scene.update();

    const Scene::CullResult& r = scene.cull(f, glm::vec3{0.0f}, 0.0f);
    REQUIRE(r.visible.size() == 12u);

    const std::vector<DrawBatch> b = plan_draw_batches(scene, r.visible);

    REQUIRE_MSG(b.size() == 4u, "unexpected number of batches", "boundaries");

    REQUIRE(b[0].instanced);
    REQUIRE(b[0].first == 0u);
    REQUIRE(b[0].count == 5);
    REQUIRE(b[0].key == ((uint64_t{1} << 32) | uint64_t{1}));

    REQUIRE_MSG(!b[1].instanced, "a 2-node run was instanced", "min run");
    REQUIRE(b[1].first == 5u);
    REQUIRE(b[1].count == 2);

    REQUIRE(b[2].instanced);
    REQUIRE(b[2].first == 7u);
    REQUIRE_MSG(b[2].count == kMinInstancedRun,
                "a run of exactly kMinInstancedRun was not instanced",
                "min run");
    REQUIRE(b[2].key == ((uint64_t{2} << 32) | uint64_t{1}));

    REQUIRE(!b[3].instanced);
    REQUIRE(b[3].first == 11u);
    REQUIRE(b[3].count == 1);

    apricot_test::pass("batches collapse at exactly the expected boundaries");
}

void batches_tile_the_visible_range_without_gaps() {
    Scene scene;
    const Frustum f = ortho_frustum();

    // A messy mix, including nodes that can never batch. The structural
    // invariant has to hold whatever the input: every visible index belongs to
    // exactly one batch. A gap is an object that is never drawn; an overlap is
    // an object drawn twice, which shows up as z-fighting rather than as a
    // missing object and is much harder to trace.
    const MeshId meshes[] = {1, 1, 1, 1, 2, kInvalidId, 3, 3, 3, 3, 3, 4, 2, 2};
    const MaterialId mats[] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 5, kInvalidId};
    for (int i = 0; i < 14; ++i) {
        scene.create(renderable(meshes[i], mats[i]),
                     at(glm::vec3{static_cast<float>(i) * 0.05f, 0.0f, -50.0f}),
                     unit_box(0.02f));
    }
    scene.update();

    const Scene::CullResult& r = scene.cull(f, glm::vec3{0.0f}, 0.0f);
    REQUIRE(r.visible.size() == 14u);

    for (int min_run = 1; min_run <= 6; ++min_run) {
        const std::vector<DrawBatch> b =
            plan_draw_batches(scene, r.visible, min_run);
        std::size_t next = 0;
        for (const DrawBatch& d : b) {
            REQUIRE_MSG(d.first == next, "batches are not contiguous", "tiling");
            REQUIRE_MSG(d.count > 0, "empty batch emitted", "tiling");
            next += static_cast<std::size_t>(d.count);

            // An instanced batch must be genuinely uniform, or the renderer
            // uploads one mesh and draws a different one for most of the run.
            if (d.instanced) {
                REQUIRE_MSG(d.count >= min_run,
                            "instanced batch shorter than min_run", "tiling");
                for (int k = 0; k < d.count; ++k) {
                    const SceneNode* n =
                        scene.get(r.visible[d.first + static_cast<std::size_t>(k)]);
                    REQUIRE_MSG(n != nullptr, "batch referenced a dead node",
                                "tiling");
                    REQUIRE_MSG(batchable(n->renderable),
                                "an unbatchable node was instanced", "tiling");
                    REQUIRE_MSG(batch_key(n->renderable) == d.key,
                                "an instanced batch mixed two keys", "tiling");
                }
            }
        }
        REQUIRE_MSG(next == r.visible.size(),
                    "batches did not cover the whole visible range", "tiling");
    }
    apricot_test::pass("batches tile the visible range with no gaps or overlaps");
}

void an_empty_visible_list_plans_nothing() {
    Scene scene;
    const std::vector<NodeId> none;
    REQUIRE(plan_draw_batches(scene, none).empty());
    apricot_test::pass("an empty visible list plans no batches");
}

// --- node store ----------------------------------------------------------------
void bulk_removal_frees_exactly_once() {
    Scene scene;
    std::vector<NodeId> ids;
    for (int i = 0; i < 10; ++i) {
        ids.push_back(scene.create(renderable(1, 1), at(glm::vec3{0.0f}),
                                   unit_box()));
    }
    REQUIRE(scene.size() == 10u);

    // Duplicates in the list, which a streamer can produce when a chunk is
    // torn down twice. Freeing a slot twice would push it onto the free list
    // twice and hand the same id to two later callers — two live nodes
    // aliasing one slot, which presents as an unrelated object vanishing.
    std::vector<NodeId> doomed = {ids[1], ids[3], ids[3], ids[5]};
    scene.remove_many(doomed);
    REQUIRE(scene.size() == 7u);
    REQUIRE(!scene.alive(ids[1]));
    REQUIRE(!scene.alive(ids[3]));
    REQUIRE(!scene.alive(ids[5]));
    REQUIRE(scene.alive(ids[0]));

    // Three slots were freed, so the next three creates must produce three
    // DISTINCT live ids.
    const NodeId n1 = scene.create(renderable(2, 2), at(glm::vec3{0.0f}), unit_box());
    const NodeId n2 = scene.create(renderable(2, 2), at(glm::vec3{0.0f}), unit_box());
    const NodeId n3 = scene.create(renderable(2, 2), at(glm::vec3{0.0f}), unit_box());
    REQUIRE(n1 != n2 && n2 != n3 && n1 != n3);
    REQUIRE(scene.size() == 10u);

    apricot_test::pass("bulk removal frees each slot exactly once");
}

void world_bounds_follow_the_transform() {
    Scene scene;
    const NodeId id = scene.create(renderable(1, 1),
                                   at(glm::vec3{100.0f, 20.0f, -5.0f}),
                                   unit_box(2.0f));
    scene.update();
    const SceneNode* n = scene.get(id);
    REQUIRE(n->world_bounds.valid());
    REQUIRE_NEAR(static_cast<double>(n->world_bounds.center().x), 100.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(n->world_bounds.center().y), 20.0, 1e-4);
    REQUIRE_NEAR(static_cast<double>(n->world_bounds.extents().x), 2.0, 1e-4);

    // Scale must widen the world bounds, or a scaled-up prop culls as if it
    // were still its authored size and pops out at the edge of the screen.
    Transform t = at(glm::vec3{100.0f, 20.0f, -5.0f});
    t.scale = glm::vec3{3.0f};
    scene.set_transform(id, t);
    scene.update();
    REQUIRE_NEAR(static_cast<double>(scene.get(id)->world_bounds.extents().x),
                 6.0, 1e-4);

    apricot_test::pass("world bounds follow the node transform");
}

}  // namespace

int main() {
    std::printf("scene_cull_tests\n");
    every_frustum_plane_rejects_and_only_its_own_side();
    a_node_with_no_bounds_never_draws();
    authored_invisibility_and_removal_are_respected();
    distance_culling_only_ever_shortens();
    distance_is_measured_to_the_nearest_point_of_the_box();
    cull_output_is_sorted_and_deterministic();
    batches_collapse_at_exactly_the_expected_boundaries();
    batches_tile_the_visible_range_without_gaps();
    an_empty_visible_list_plans_nothing();
    bulk_removal_frees_exactly_once();
    world_bounds_follow_the_transform();
    return apricot_test::done("scene_cull_tests");
}
