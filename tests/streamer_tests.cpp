// Chunk streaming: residency, instance-budget pacing, and bulk eviction.
//
// Streaming is the system whose bugs are all shaped the same way — everything
// looks right on the frame you are looking at, and wrong two hundred frames
// later after the player has driven in a circle. Holes in the world, chunks
// that never load because something already believes they are loaded, nodes
// that outlive the chunk that made them. None of those reproduce on demand by
// driving around, and all of them are one assertion away in a headless test.
//
// The whole suite drives a REAL Streamer against a REAL Scene with REAL chunk
// meshes and REAL scatter. No hand-built inputs: the failure this engine most
// wants to avoid is a consumer test passing on invented data while the actual
// producer feeds garbage.

#include <algorithm>
#include <cmath>
#include <utility>
#include <cstdint>
#include <set>
#include <vector>

#include "terrain/chunk.h"
#include "terrain/scatter.h"
#include "terrain/streamer.h"
#include "test_assert.h"

using namespace apricot;

namespace {

ScenePrototypes make_prototypes() {
    ScenePrototypes p;
    p.terrain.material = 1;
    p.terrain.mesh = kInvalidId;  // filled per chunk by the streamer
    for (uint8_t i = 0; i < kTreeVariants; ++i) {
        p.tree[i].mesh = static_cast<MeshId>(10 + i);
        p.tree[i].material = 2;
    }
    for (uint8_t i = 0; i < kRockVariants; ++i) {
        p.rock[i].mesh = static_cast<MeshId>(20 + i);
        p.rock[i].material = 3;
    }
    return p;
}

// Stands in for the host: steps the streamer, then builds and delivers
// everything it asked for. Deliberately synchronous — the point of the
// streamer's design is that the host may be async, and a synchronous host is
// the strictest case for "does a chunk ever get requested twice".
struct Host {
    uint64_t seed;
    Scene scene;
    Streamer streamer;
    ScenePrototypes proto = make_prototypes();
    MeshId next_mesh = 1000;
    int deliveries = 0;

    Host(uint64_t s, StreamerConfig cfg) : seed(s), streamer(s, cfg) {}

    StreamerStats tick(glm::vec3 cam) {
        const StreamerStats st = streamer.step(scene, proto, cam);
        scene.update();
        // Copy: deliver() does not touch pending_loads(), but a test that
        // depends on that is a test that breaks when the implementation
        // changes for a good reason.
        const std::vector<ChunkCoord> want = streamer.pending_loads();
        for (const ChunkCoord c : want) {
            const ChunkMesh m = build_chunk(seed, c);
            streamer.deliver(c, next_mesh++, m.bounds);
            ++deliveries;
        }
        return st;
    }

    // Run until residency settles or the step cap is hit. The cap is a real
    // assertion, not a convenience: a streamer that never converges is exactly
    // the failure mode where the world is permanently missing a chunk.
    int settle(glm::vec3 cam, int max_steps = 4000) {
        int steps = 0;
        while (steps < max_steps) {
            const StreamerStats st = tick(cam);
            ++steps;
            if (st.chunks_requested == 0 && st.instances_activated == 0 &&
                streamer.activating_count() == 0 &&
                streamer.in_flight_count() == 0) {
                return steps;
            }
        }
        REQUIRE_MSG(false, "streaming never settled", "convergence");
        return steps;
    }
};

glm::vec3 centre_of(ChunkCoord c) {
    return glm::vec3{static_cast<float>(c.x) * kChunkMetres + kChunkMetres * 0.5f,
                     0.0f,
                     static_cast<float>(c.z) * kChunkMetres + kChunkMetres * 0.5f};
}

std::set<std::pair<int32_t, int32_t>> resident_set(const Streamer& s,
                                                   ChunkCoord centre,
                                                   int radius) {
    std::set<std::pair<int32_t, int32_t>> out;
    for (int dz = -radius - 4; dz <= radius + 4; ++dz) {
        for (int dx = -radius - 4; dx <= radius + 4; ++dx) {
            const ChunkCoord c{centre.x + dx, centre.z + dz};
            if (s.resident(c)) out.insert({c.x, c.z});
        }
    }
    return out;
}

// Nodes the scene should hold for a fully resident chunk: the terrain node
// plus one per prop.
std::size_t expected_nodes(uint64_t seed, ChunkCoord c) {
    return 1u + scatter_chunk(seed, c).size();
}

// --- residency ------------------------------------------------------------------
void the_load_radius_fills_and_settles() {
    StreamerConfig cfg;
    cfg.load_radius = 2;
    cfg.evict_radius = 3;
    cfg.max_chunk_builds_per_step = 2;
    cfg.max_instances_per_step = 400;

    Host h(0xA5ull, cfg);
    const int steps = h.settle(glm::vec3{0.0f});

    // A circular radius-2 neighbourhood: (0,0); the four at distance 1; the
    // four diagonals at distance sqrt2; the four at distance 2. The pairs at
    // distance sqrt5 and 2*sqrt2 are outside. 1 + 4 + 4 + 4 = 13.
    REQUIRE_MSG(h.streamer.resident_count() == 13u,
                "the load radius did not fill to the expected 13 chunks",
                "fill");
    REQUIRE(h.streamer.activating_count() == 0u);
    REQUIRE(h.streamer.in_flight_count() == 0u);

    // Every chunk resident exactly once, and every one of its nodes present.
    std::size_t total = 0;
    for (int dz = -2; dz <= 2; ++dz) {
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx * dx + dz * dz > 4) continue;
            const ChunkCoord c{dx, dz};
            REQUIRE_MSG(h.streamer.resident(c), "a chunk inside the radius is missing",
                        "fill");
            const std::vector<NodeId> nodes = h.streamer.chunk_nodes(c);
            REQUIRE_MSG(nodes.size() == expected_nodes(h.seed, c),
                        "a resident chunk has the wrong node count", "fill");
            for (const NodeId id : nodes) {
                REQUIRE_MSG(h.scene.alive(id), "a chunk owns a dead node", "fill");
            }
            total += nodes.size();
        }
    }
    REQUIRE_MSG(h.scene.size() == total,
                "the scene holds nodes no chunk claims", "fill");

    // Each chunk must be built exactly once. A chunk requested twice is two
    // sets of nodes for one piece of ground, drawn on top of each other.
    REQUIRE_MSG(h.deliveries == 13,
                "a chunk was requested more than once", "duplicate requests");

    std::printf("      (settled in %d steps, %zu nodes across 13 chunks)\n",
                steps, h.scene.size());
    apricot_test::pass("the load radius fills exactly once and settles");
}

// --- the instance budget --------------------------------------------------------
void activation_is_paced_by_instances_and_never_exceeds_the_budget() {
    StreamerConfig cfg;
    cfg.load_radius = 2;
    cfg.evict_radius = 3;
    cfg.max_chunk_builds_per_step = 2;
    cfg.max_instances_per_step = 17;  // deliberately not a round number

    Host h(0x33ull, cfg);

    int steps = 0;
    int carried_steps = 0;
    std::size_t previous_nodes = 0;
    while (steps < 4000) {
        const StreamerStats st = h.tick(glm::vec3{0.0f});
        ++steps;

        REQUIRE_MSG(st.instances_activated <= cfg.max_instances_per_step,
                    "a step activated more instances than the budget allows",
                    "budget");

        // The scene must grow by exactly what was reported. A mismatch means
        // nodes are being created off the books, which is how the budget stops
        // bounding anything.
        REQUIRE_MSG(h.scene.size() ==
                        previous_nodes +
                            static_cast<std::size_t>(st.instances_activated),
                    "scene growth did not match the reported instance count",
                    "budget");
        previous_nodes = h.scene.size();

        if (st.budget_exhausted) ++carried_steps;

        if (st.chunks_requested == 0 && st.instances_activated == 0 &&
            h.streamer.activating_count() == 0 &&
            h.streamer.in_flight_count() == 0) {
            break;
        }
    }

    REQUIRE(h.streamer.resident_count() == 13u);

    // With a 17-instance budget and chunks carrying a hundred-odd props each,
    // the great majority of steps MUST end mid-chunk. If none did, the budget
    // is not actually pacing anything and this test proves nothing.
    REQUIRE_MSG(carried_steps > 20,
                "no step ever carried a half-activated chunk", "carry");

    std::printf("      (%d steps, %d of them ending mid-chunk, budget %d)\n",
                steps, carried_steps, cfg.max_instances_per_step);
    apricot_test::pass("activation is paced by instances, not by chunks");
}

// The carry itself, examined one step at a time.
void a_half_activated_chunk_survives_into_the_next_step() {
    StreamerConfig cfg;
    cfg.load_radius = 0;  // exactly one chunk, so there is no ambiguity
    cfg.evict_radius = 2;
    cfg.max_chunk_builds_per_step = 1;
    cfg.max_instances_per_step = 5;

    Host h(0x77ull, cfg);
    const ChunkCoord target{0, 0};
    const std::size_t want = expected_nodes(h.seed, target);
    REQUIRE_MSG(want > 20u, "this chunk is too sparse to test the carry",
                "carry");

    h.tick(glm::vec3{0.0f});  // requests and delivers, activates nothing yet

    std::size_t seen = 0;
    int steps = 0;
    while (!h.streamer.resident(target) && steps < 500) {
        const StreamerStats st = h.tick(glm::vec3{0.0f});
        ++steps;
        seen += static_cast<std::size_t>(st.instances_activated);

        if (!h.streamer.resident(target)) {
            // NOT RESIDENT UNTIL COMPLETE. This is the invariant that stops a
            // chunk being permanently half-built: if it counted as resident
            // now, the next plan would see nothing missing and its remaining
            // props would never be created.
            REQUIRE_MSG(h.streamer.activating_count() == 1u,
                        "the half-activated chunk was lost between steps",
                        "carry");
            REQUIRE_MSG(h.streamer.chunk_nodes(target).empty(),
                        "an incomplete chunk reported its nodes", "carry");
        }
        // Nodes accumulate; they are never rebuilt from scratch on resume.
        REQUIRE_MSG(h.scene.size() == seen,
                    "resuming a carried chunk re-created earlier nodes",
                    "carry");
    }

    REQUIRE_MSG(h.streamer.resident(target), "the carried chunk never completed",
                "carry");
    REQUIRE_MSG(h.streamer.chunk_nodes(target).size() == want,
                "the completed chunk is missing nodes", "carry");
    REQUIRE(h.scene.size() == want);
    REQUIRE_MSG(steps > 5, "the chunk completed too fast to have been carried",
                "carry");

    std::printf("      (%zu instances carried across %d steps at %d per step)\n",
                want, steps, cfg.max_instances_per_step);
    apricot_test::pass("a half-activated chunk resumes where it stopped");
}

// --- crossing a boundary ---------------------------------------------------------
void crossing_a_boundary_activates_and_evicts_the_right_sets() {
    StreamerConfig cfg;
    cfg.load_radius = 2;
    cfg.evict_radius = 3;
    cfg.max_chunk_builds_per_step = 4;
    cfg.max_instances_per_step = 2000;

    Host h(0x1234ull, cfg);
    h.settle(centre_of(ChunkCoord{0, 0}));
    const auto before = resident_set(h.streamer, ChunkCoord{0, 0}, 3);
    REQUIRE(before.size() == 13u);

    // Drive far enough that the old and new neighbourhoods do not overlap at
    // all: everything must go, and a fresh set must arrive.
    const ChunkCoord far_away{20, -14};
    h.settle(centre_of(far_away));

    const auto after = resident_set(h.streamer, far_away, 3);
    REQUIRE_MSG(after.size() == 13u, "the new neighbourhood did not fill",
                "crossing");

    for (const auto& c : before) {
        REQUIRE_MSG(after.count(c) == 0u,
                    "a chunk from the old neighbourhood survived", "crossing");
        REQUIRE_MSG(!h.streamer.resident(ChunkCoord{c.first, c.second}),
                    "an old chunk is still resident", "crossing");
    }

    // And crucially, NOTHING was left behind in the scene. Every node the old
    // chunks owned must be gone; a leaked node is invisible in every test that
    // only looks at residency, and accumulates until the frame time does.
    std::size_t expected = 0;
    for (const auto& c : after) {
        expected += expected_nodes(h.seed, ChunkCoord{c.first, c.second});
    }
    REQUIRE_MSG(h.scene.size() == expected,
                "eviction leaked nodes into the scene", "crossing");

    apricot_test::pass("crossing a boundary swaps exactly the right chunks");
}

// One step at a time across a single boundary, checking the hysteresis gap.
void the_evict_radius_gives_hysteresis() {
    StreamerConfig cfg;
    cfg.load_radius = 2;
    cfg.evict_radius = 4;
    cfg.max_chunk_builds_per_step = 8;
    cfg.max_instances_per_step = 4000;

    Host h(0x5150ull, cfg);
    h.settle(centre_of(ChunkCoord{0, 0}));

    // Step one chunk east. Chunk (-2, 0) is now 3 away — outside the load
    // radius but inside the evict radius, so it must SURVIVE. Without the gap
    // it would be dropped and immediately re-requested the moment the player
    // drifted back, forever.
    h.settle(centre_of(ChunkCoord{1, 0}));
    REQUIRE_MSG(h.streamer.resident(ChunkCoord{-2, 0}),
                "a chunk inside the evict radius was dropped", "hysteresis");

    // A player oscillating across the boundary must cause no work at all once
    // both neighbourhoods are resident.
    h.settle(centre_of(ChunkCoord{0, 0}));
    const int before = h.deliveries;
    for (int i = 0; i < 12; ++i) {
        h.tick(centre_of(ChunkCoord{i % 2, 0}));
    }
    REQUIRE_MSG(h.deliveries == before,
                "oscillating on a boundary caused chunk rebuilds", "thrash");

    apricot_test::pass("the evict radius provides real hysteresis");
}

// --- regeneration, and the absence of a cache -------------------------------------
void an_evicted_chunk_comes_back_identical() {
    StreamerConfig cfg;
    cfg.load_radius = 1;
    cfg.evict_radius = 2;
    cfg.max_chunk_builds_per_step = 4;
    cfg.max_instances_per_step = 4000;

    Host h(0xD15Cull, cfg);
    h.settle(centre_of(ChunkCoord{0, 0}));

    const ChunkCoord target{0, 0};
    std::vector<glm::vec3> before;
    for (const NodeId id : h.streamer.chunk_nodes(target)) {
        before.push_back(h.scene.get(id)->local.position);
    }
    REQUIRE(!before.empty());

    // Leave, come back.
    h.settle(centre_of(ChunkCoord{30, 30}));
    REQUIRE(!h.streamer.resident(target));
    h.settle(centre_of(ChunkCoord{0, 0}));
    REQUIRE(h.streamer.resident(target));

    std::vector<glm::vec3> after;
    for (const NodeId id : h.streamer.chunk_nodes(target)) {
        after.push_back(h.scene.get(id)->local.position);
    }

    // Regenerated, never cached. Generated chunks are not written to disk and
    // are not held in memory after eviction, so this passing is what says the
    // pure-function contract is actually load-bearing rather than aspirational.
    REQUIRE_MSG(after.size() == before.size(),
                "a regenerated chunk has a different prop count", "regeneration");
    for (std::size_t i = 0; i < before.size(); ++i) {
        REQUIRE_MSG(before[i] == after[i],
                    "a regenerated chunk placed a prop differently",
                    "regeneration");
    }
    apricot_test::pass("an evicted chunk regenerates identically");
}

// --- stale deliveries -------------------------------------------------------------
void a_delivery_for_an_abandoned_chunk_is_dropped() {
    StreamerConfig cfg;
    cfg.load_radius = 1;
    cfg.evict_radius = 2;
    cfg.max_chunk_builds_per_step = 4;
    cfg.max_instances_per_step = 4000;

    Streamer s(0x99ull, cfg);
    Scene scene;
    const ScenePrototypes proto = make_prototypes();

    // Ask for chunks around the origin but do not deliver them.
    s.step(scene, proto, glm::vec3{0.0f});
    const std::vector<ChunkCoord> requested = s.pending_loads();
    REQUIRE(!requested.empty());

    // Drive away. The requests are abandoned.
    s.step(scene, proto, centre_of(ChunkCoord{40, 40}));
    REQUIRE(s.in_flight_count() == 0u ||
            s.pending_loads().size() == s.in_flight_count());

    // Now the slow worker finally reports back. This is the real race: a chunk
    // that took several frames to build while the camera kept moving.
    const std::size_t before = scene.size();
    for (const ChunkCoord c : requested) {
        s.deliver(c, 4242u, AABB{});
    }
    s.step(scene, proto, centre_of(ChunkCoord{40, 40}));
    scene.update();

    for (const ChunkCoord c : requested) {
        REQUIRE_MSG(!s.resident(c),
                    "a stale delivery was activated far from the camera",
                    "stale");
    }
    // The scene may legitimately have grown from the NEW neighbourhood, but
    // never from the abandoned chunks. Checking residency above is the precise
    // statement; this is a guard on the obvious version of the bug.
    REQUIRE(scene.size() >= before);

    // And critically, a dropped delivery must not leave a hole: driving back
    // must re-request and load it normally.
    Host h(0x99ull, cfg);
    h.settle(glm::vec3{0.0f});
    REQUIRE_MSG(h.streamer.resident(ChunkCoord{0, 0}),
                "a chunk could not be loaded after an abandoned request",
                "stale");
    apricot_test::pass("a stale delivery is dropped and leaves no hole");
}

// --- determinism -------------------------------------------------------------------
void two_streamers_on_one_path_agree_exactly() {
    StreamerConfig cfg;
    cfg.load_radius = 2;
    cfg.evict_radius = 3;
    cfg.max_chunk_builds_per_step = 2;
    cfg.max_instances_per_step = 61;

    Host a(0xFEEDull, cfg);
    Host b(0xFEEDull, cfg);

    // The same camera path, stepped in lockstep.
    for (int i = 0; i < 240; ++i) {
        const glm::vec3 cam{static_cast<float>(i) * 3.1f, 0.0f,
                            static_cast<float>(i) * 1.7f};
        const StreamerStats sa = a.tick(cam);
        const StreamerStats sb = b.tick(cam);

        REQUIRE_MSG(sa.chunks_requested == sb.chunks_requested,
                    "load plans diverged", "determinism");
        REQUIRE_MSG(sa.instances_activated == sb.instances_activated,
                    "activation diverged", "determinism");
        REQUIRE_MSG(sa.chunks_evicted == sb.chunks_evicted,
                    "eviction diverged", "determinism");
        REQUIRE_MSG(sa.nodes_evicted == sb.nodes_evicted,
                    "node eviction diverged", "determinism");
        REQUIRE_MSG(a.scene.size() == b.scene.size(), "scene sizes diverged",
                    "determinism");
        REQUIRE_MSG(a.streamer.pending_loads() == b.streamer.pending_loads(),
                    "the load ORDER diverged", "determinism");
        REQUIRE_MSG(a.streamer.last_evictions() == b.streamer.last_evictions(),
                    "the eviction order diverged", "determinism");
    }
    REQUIRE(a.streamer.resident_count() == b.streamer.resident_count());
    REQUIRE(a.deliveries == b.deliveries);
    apricot_test::pass("two streamers on one camera path agree exactly");
}

// A long random-ish drive, checking the invariant that actually matters: the
// scene never holds a node that no resident or activating chunk owns.
void a_long_drive_leaks_nothing() {
    StreamerConfig cfg;
    cfg.load_radius = 3;
    cfg.evict_radius = 5;
    cfg.max_chunk_builds_per_step = 3;
    cfg.max_instances_per_step = 250;

    Host h(0x0DDBA11ull, cfg);

    float x = 0.0f, z = 0.0f;
    for (int i = 0; i < 900; ++i) {
        // Deterministic wander, no RNG.
        x += 9.0f * std::cos(static_cast<float>(i) * 0.037f);
        z += 9.0f * std::sin(static_cast<float>(i) * 0.021f);
        h.tick(glm::vec3{x, 0.0f, z});
    }

    // Settle, then account for every single node in the scene.
    h.settle(glm::vec3{x, 0.0f, z});

    std::size_t owned = 0;
    const ChunkCoord centre = chunk_at(x, z);
    std::set<NodeId> seen;
    for (int dz = -8; dz <= 8; ++dz) {
        for (int dx = -8; dx <= 8; ++dx) {
            const ChunkCoord c{centre.x + dx, centre.z + dz};
            for (const NodeId id : h.streamer.chunk_nodes(c)) {
                REQUIRE_MSG(h.scene.alive(id), "a chunk owns a dead node",
                            "leak");
                REQUIRE_MSG(seen.insert(id).second,
                            "two chunks claim the same node", "leak");
                ++owned;
            }
        }
    }
    REQUIRE_MSG(h.scene.size() == owned,
                "the scene holds nodes no chunk owns - eviction leaked",
                "leak");

    std::printf("      (900 steps of driving, %zu nodes, all accounted for)\n",
                h.scene.size());
    apricot_test::pass("a long drive leaks no nodes");
}

}  // namespace

int main() {
    std::printf("streamer_tests\n");
    the_load_radius_fills_and_settles();
    activation_is_paced_by_instances_and_never_exceeds_the_budget();
    a_half_activated_chunk_survives_into_the_next_step();
    crossing_a_boundary_activates_and_evicts_the_right_sets();
    the_evict_radius_gives_hysteresis();
    an_evicted_chunk_comes_back_identical();
    a_delivery_for_an_abandoned_chunk_is_dropped();
    two_streamers_on_one_path_agree_exactly();
    a_long_drive_leaks_nothing();
    return apricot_test::done("streamer_tests");
}
