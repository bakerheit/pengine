// Streaming with level of detail: rings, refits, cold fill, and the GPU
// memory that has to come back.
//
// tests/streamer_tests.cpp already covers residency, budget pacing and bulk
// eviction at a single level, and those tests still run unchanged. This suite
// is about what LOD and the host-side mesh lifetime added, and every case here
// is one that the single-level suite structurally cannot catch:
//
//   * A chunk now changes level in place. That is a rebuild of geometry the
//     player is looking at, and the only acceptable number of frames where the
//     ground is missing is zero.
//   * A chunk now owns a MESH as well as nodes. The node bookkeeping was
//     already proven; the mesh bookkeeping is new, and a leak in it is
//     invisible to every existing assertion because the node counts still
//     balance perfectly while the GPU fills up.
//   * The world now has to be fillable before the player is allowed to look at
//     it, on a cold start and after a teleport across the island.
//
// Everything runs a REAL Streamer against a REAL Scene with REAL chunk meshes
// and REAL scatter.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
    p.terrain.mesh = kInvalidId;
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

// Stands in for the host, and unlike the single-level suite's Host it also
// stands in for the host's RESOURCE TABLE. Mesh ids are minted on upload and
// retired when the streamer hands them back, so "did the world let go of its
// GPU memory" becomes a set this test can look at rather than a thing you
// discover on a long drive.
struct Host {
    uint64_t seed;
    Scene scene;
    Streamer streamer;
    ScenePrototypes proto = make_prototypes();

    MeshId next_mesh = 1000;
    std::set<MeshId> uploaded;   // live, from the host's point of view
    std::size_t total_uploads = 0;
    std::size_t total_frees = 0;
    std::size_t bytes = 0;
    std::size_t peak_bytes = 0;
    std::vector<MeshId> scratch;

    Host(uint64_t s, StreamerConfig cfg) : seed(s), streamer(s, cfg) {}

    void drain_frees() {
        scratch.clear();
        streamer.take_released_meshes(scratch);
        for (const MeshId m : scratch) {
            REQUIRE_MSG(uploaded.erase(m) == 1u,
                        "the streamer handed back a mesh id that was not live: "
                        "either a double free or an id that was never uploaded",
                        "mesh lifetime");
            ++total_frees;
        }
    }

    StreamerStats tick(glm::vec3 cam, StepMode mode = StepMode::Budgeted) {
        const StreamerStats st = streamer.step(scene, proto, cam, mode);
        scene.update();
        drain_frees();

        const std::vector<ChunkRequest> want = streamer.pending_loads();
        for (const ChunkRequest& r : want) {
            const ChunkMesh m = build_chunk(seed, r.coord, r.lod);
            const MeshId id = next_mesh++;
            uploaded.insert(id);
            ++total_uploads;
            bytes += m.gpu_bytes();
            streamer.deliver(r.coord, r.lod, id, m.bounds);
        }
        // deliver() can drop a stale one and hand its mesh straight back, so
        // drain again rather than waiting for the next step.
        drain_frees();
        peak_bytes = std::max(peak_bytes, bytes);
        return st;
    }

    // Fill until ready(), unbudgeted. This is exactly the loop app.cpp runs on
    // a cold start and after a teleport, so the test exercises the shipped
    // path rather than a description of it.
    int fill(glm::vec3 cam, int max_steps = 512) {
        int steps = 0;
        while (!streamer.ready(cam) && steps < max_steps) {
            tick(cam, StepMode::Fill);
            ++steps;
        }
        REQUIRE_MSG(streamer.ready(cam),
                    "the fill loop never reached ready(); a cold start would "
                    "resume into a hole",
                    "cold fill");
        return steps;
    }

    int settle(glm::vec3 cam, int max_steps = 20000) {
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

// A config small enough to settle quickly but with every ring represented.
StreamerConfig tiered_config() {
    StreamerConfig cfg;
    // lod_ring[0] is 2 and not 1 because 1 is not legal: see
    // kMinLevelZeroRingChunks. The streamer clamps it either way; writing the
    // legal value here keeps the ring counts below meaning what they say.
    cfg.lod_ring[0] = 2;
    cfg.lod_ring[1] = 3;
    cfg.lod_ring[2] = 5;
    cfg.load_radius = 7;
    cfg.evict_radius = 9;
    cfg.max_scatter_lod = 1;
    cfg.max_chunk_builds_per_step = 8;
    cfg.max_build_quads_per_step = 40000;
    cfg.max_instances_per_step = 4000;
    cfg.prime_radius = 2;
    return cfg;
}

// --- 1. the rings are where the config says --------------------------------

void every_chunk_is_resident_at_the_level_its_ring_asks_for() {
    Host h(0x5EEDull, tiered_config());
    h.settle(centre_of(ChunkCoord{0, 0}));

    std::size_t by_lod[kMaxChunkLod + 1];
    h.streamer.residency_by_lod(by_lod);

    const int r = h.streamer.config().load_radius;
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            if (dx * dx + dz * dz > r * r) continue;
            const ChunkCoord c{dx, dz};
            REQUIRE_MSG(h.streamer.resident(c),
                        "a chunk inside the load radius is missing", "rings");
            REQUIRE_MSG(h.streamer.resident_lod(c) == h.streamer.lod_for(c),
                        "a chunk settled at a level its ring did not ask for",
                        "rings");
        }
    }

    for (int l = 0; l <= kMaxChunkLod; ++l) {
        REQUIRE_MSG(by_lod[l] > 0u,
                    "a level ring is empty; this config was meant to exercise "
                    "all of them and now tests less than it claims",
                    "rings");
    }
    std::printf("    [rings] resident by level: %zu / %zu / %zu / %zu\n",
                by_lod[0], by_lod[1], by_lod[2], by_lod[3]);
}

// --- 2. THE COLLISION GUARANTEE ---------------------------------------------

void the_ground_the_car_can_touch_is_always_level_zero() {
    // Physics reconstructs the level 0 lattice analytically and does not
    // consult the streamer at all, so if the drawn chunk under the car is
    // coarser, the car rests on a surface it is not drawn on. That is the one
    // thing "collision derives from the geometry that draws" forbids outright,
    // and LOD is the first feature in this engine capable of breaking it.
    Host h(0x5EEDull, tiered_config());

    const glm::vec3 path[] = {
        centre_of(ChunkCoord{0, 0}),   centre_of(ChunkCoord{2, 1}),
        centre_of(ChunkCoord{5, 4}),   centre_of(ChunkCoord{-3, 6}),
        centre_of(ChunkCoord{-8, -2}), centre_of(ChunkCoord{4, -9}),
    };

    for (const glm::vec3 p : path) {
        h.settle(p);
        const ChunkCoord under = chunk_at(p.x, p.z);
        REQUIRE_MSG(h.streamer.resident(under),
                    "the chunk under the car is not resident", "collision lod");
        REQUIRE_MSG(h.streamer.resident_lod(under) == 0,
                    "the chunk under the car is coarser than level 0; the car "
                    "would rest on a chord it is not drawn on",
                    "collision lod");

        // ...and so are its neighbours, because a car is not a point and a
        // wheel can be in the next chunk while the body is in this one.
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                const ChunkCoord n{under.x + dx, under.z + dz};
                REQUIRE_MSG(h.streamer.resident_lod(n) == 0,
                            "a chunk adjacent to the car is coarser than level "
                            "0",
                            "collision lod");
            }
        }
    }
}

// --- 3. a level change must never open a hole -------------------------------

void a_chunk_changing_level_is_never_absent_from_the_scene() {
    Host h(0x77ull, tiered_config());
    h.settle(centre_of(ChunkCoord{0, 0}));

    // A chunk out in the level 2 ring, which the drive below will pull inward
    // through level 1 and into level 0. Every ring it crosses is a refit.
    const ChunkCoord watched{4, 0};
    REQUIRE(h.streamer.resident(watched));
    const int start_lod = h.streamer.resident_lod(watched);
    REQUIRE_MSG(start_lod >= 2, "the watched chunk did not start out coarse",
                "refit");

    const NodeId first_node = h.streamer.chunk_nodes(watched)[0];
    int refits = 0;
    int levels_seen = 0;
    int last_lod = start_lod;

    // Walk the camera onto it one chunk at a time, checking EVERY step rather
    // than only at the ends. A hole that exists for one step is exactly the
    // hole a player sees and a settled-state assertion never does.
    for (int x = 0; x <= 4; ++x) {
        for (int rep = 0; rep < 40; ++rep) {
            const StreamerStats st = h.tick(centre_of(ChunkCoord{x, 0}));
            refits += st.chunks_refitted;

            REQUIRE_MSG(h.streamer.resident(watched),
                        "the chunk stopped being resident while changing level",
                        "refit");
            const std::vector<NodeId> nodes = h.streamer.chunk_nodes(watched);
            REQUIRE_MSG(!nodes.empty() && h.scene.alive(nodes[0]),
                        "the chunk's terrain node died during a level change; "
                        "the player would see through the ground",
                        "refit");
            REQUIRE_MSG(nodes[0] == first_node,
                        "the terrain node was destroyed and recreated rather "
                        "than re-pointed, which is a hole by construction",
                        "refit");

            const int now = h.streamer.resident_lod(watched);
            if (now != last_lod) {
                ++levels_seen;
                last_lod = now;
            }
        }
    }

    REQUIRE_MSG(h.streamer.resident_lod(watched) == 0,
                "the chunk never reached level 0 with the camera on top of it",
                "refit");
    REQUIRE_MSG(refits > 0, "no refit happened at all; this test proved nothing",
                "refit");
    std::printf("    [refit] %d refits, %d level changes on the watched chunk, "
                "terrain node never died\n",
                refits, levels_seen);
}

// --- 4. THE LEAK TEST -------------------------------------------------------

void a_long_drive_frees_every_mesh_it_stops_using() {
    Host h(0xBEEFull, tiered_config());

    // A closed loop, driven twice. Closing the loop matters: it forces the
    // streamer to evict, re-request and re-evict the same coordinates, which is
    // the pattern that turns a small per-crossing leak into an unbounded one.
    const ChunkCoord loop[] = {{0, 0},  {4, 0},  {8, 0},  {8, 4},  {8, 8},
                               {4, 8},  {0, 8},  {-4, 8}, {-8, 4}, {-8, 0},
                               {-4, 0}, {0, 0}};

    for (int lap = 0; lap < 2; ++lap) {
        for (const ChunkCoord c : loop) {
            for (int rep = 0; rep < 30; ++rep) h.tick(centre_of(c));
        }
    }
    h.settle(centre_of(ChunkCoord{0, 0}));

    // THE INVARIANT: everything the host still believes it owns is exactly what
    // the streamer still references. Not "roughly" — every id.
    std::set<MeshId> referenced;
    const int r = h.streamer.config().evict_radius;
    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            const ChunkCoord c{dx, dz};
            if (!h.streamer.resident(c)) continue;
            const std::vector<NodeId> nodes = h.streamer.chunk_nodes(c);
            const SceneNode* n = h.scene.get(nodes[0]);
            REQUIRE_MSG(n != nullptr, "a resident chunk's terrain node is dead",
                        "mesh lifetime");
            referenced.insert(n->renderable.mesh);
        }
    }

    REQUIRE_MSG(h.uploaded.size() == referenced.size(),
                "the host still owns meshes nothing in the world references; "
                "that is a GPU leak that every node-count assertion passes over",
                "mesh lifetime");
    for (const MeshId m : referenced) {
        REQUIRE_MSG(h.uploaded.count(m) == 1u,
                    "the world references a mesh the host has already freed",
                    "mesh lifetime");
    }

    REQUIRE_MSG(h.total_frees > 0u, "nothing was ever freed; the drive was too "
                                    "short to prove anything",
                "mesh lifetime");
    std::printf("    [mesh lifetime] %zu uploads, %zu frees, %zu still live "
                "(%zu resident chunks)\n",
                h.total_uploads, h.total_frees, h.uploaded.size(),
                h.streamer.resident_count());
}

// --- 5. cold fill and teleport ----------------------------------------------

void a_cold_start_fills_before_it_is_ready() {
    Host h(0x1234ull, tiered_config());
    const glm::vec3 spawn = centre_of(ChunkCoord{0, 0});

    REQUIRE_MSG(!h.streamer.ready(spawn),
                "an untouched streamer claims to be ready; a cold start would "
                "resume into a world with nothing in it",
                "cold fill");

    const int steps = h.fill(spawn);

    // Fill mode plans only the prime ring, so the loop has to be SHORT. An
    // unbounded fill is not a fix for a hitch, it is a longer hitch.
    REQUIRE_MSG(steps <= 8,
                "the fill loop took more steps than the prime ring should need; "
                "it is planning more than prime_radius",
                "cold fill");

    const ChunkCoord under = chunk_at(spawn.x, spawn.z);
    REQUIRE(h.streamer.resident_lod(under) == 0);
    std::printf("    [cold fill] ready after %d unbudgeted steps, %zu chunks "
                "resident\n",
                steps, h.streamer.resident_count());
}

void a_teleport_across_the_island_fills_before_it_resumes() {
    Host h(0x1234ull, tiered_config());
    const glm::vec3 spawn = centre_of(ChunkCoord{0, 0});
    h.fill(spawn);
    h.settle(spawn);

    // The mission warp. Nothing around the destination is resident and the
    // player is about to be standing there.
    const glm::vec3 far_away = centre_of(ChunkCoord{60, -40});
    REQUIRE_MSG(!h.streamer.ready(far_away),
                "the streamer claims the far side of the island is already "
                "ready",
                "teleport");

    const int steps = h.fill(far_away);
    REQUIRE_MSG(steps <= 8, "the teleport fill was not bounded", "teleport");

    const ChunkCoord under = chunk_at(far_away.x, far_away.z);
    REQUIRE_MSG(h.streamer.resident_lod(under) == 0,
                "the teleport destination is not level 0 under the car",
                "teleport");

    // And the world it left behind is gone, not stranded.
    h.settle(far_away);
    REQUIRE_MSG(!h.streamer.resident(ChunkCoord{0, 0}),
                "the origin is still resident after a teleport across the "
                "island",
                "teleport");
    std::printf("    [teleport] ready after %d steps, %zu chunks resident, "
                "%zu meshes live\n",
                steps, h.streamer.resident_count(), h.uploaded.size());
}

void a_warp_inside_the_loaded_radius_still_refits_before_it_resumes() {
    // THE REGRESSION THIS EXISTS FOR, and it is the one a teleport test aimed
    // at the far side of the island cannot catch.
    //
    // Warp somewhere that is ALREADY RESIDENT but at the wrong level -- the
    // outer rings of the world you are standing in. Nothing has to be loaded;
    // everything has to be REFITTED, because what was level 3 background is
    // about to be the ground under the car.
    //
    // ready() originally measured levels about the last planned centre, which
    // between the jump and the next step still describes the world the player
    // left. Relative to that centre, every chunk at the destination was already
    // at the level it wanted, so ready() said yes, the fill loop ran zero
    // iterations, and the car resumed standing on an 8 m chord.
    //
    // The symptom was silent and shaped like success: "filled in 0 steps,
    // 0.0 ms". A fill that never runs looks exactly like a fill that is very
    // fast, which is why this asserts on the WORK DONE and not just on the
    // final state.
    Host h(0xFA11ull, tiered_config());
    const glm::vec3 spawn = centre_of(ChunkCoord{0, 0});
    h.fill(spawn);
    h.settle(spawn);

    // A chunk out in the coarse rings of the world we already have.
    const ChunkCoord dest{6, 0};
    REQUIRE_MSG(h.streamer.resident(dest),
                "the destination was not already resident, so this test is not "
                "exercising the case it was written for",
                "warp inside");
    REQUIRE_MSG(h.streamer.resident_lod(dest) > 0,
                "the destination was already level 0; pick a coarser one",
                "warp inside");

    const glm::vec3 to = centre_of(dest);
    REQUIRE_MSG(!h.streamer.ready(to),
                "ready() claims a coarse, already-resident destination is fine "
                "to resume on; it is measuring levels about the wrong centre",
                "warp inside");

    const int steps = h.fill(to);
    REQUIRE_MSG(steps > 0, "the fill loop did no work at all", "warp inside");
    REQUIRE_MSG(h.streamer.resident_lod(dest) == 0,
                "the destination is still coarse after a fill that claimed to "
                "have finished",
                "warp inside");
    std::printf("    [warp inside] refit %d coarse chunks to level 0 in %d "
                "fill steps\n",
                1, steps);
}

// --- 6. the scatter tier ----------------------------------------------------

void chunks_past_the_scatter_tier_carry_terrain_only() {
    Host h(0x31337ull, tiered_config());
    // Wooded ground, not the origin: the origin is Vellum Row, and a paved
    // district scatters nothing now that scatter reads PropParams::wild.
    // The guard below caught that correctly rather than passing on an
    // empty scene.
    h.settle(centre_of(ChunkCoord{5, 6}));

    const int max_scatter = h.streamer.config().max_scatter_lod;
    const int r = h.streamer.config().load_radius;
    std::size_t props_near = 0;

    for (int dz = -r; dz <= r; ++dz) {
        for (int dx = -r; dx <= r; ++dx) {
            const ChunkCoord c{dx, dz};
            if (!h.streamer.resident(c)) continue;
            const std::size_t nodes = h.streamer.chunk_nodes(c).size();

            if (h.streamer.resident_lod(c) > max_scatter) {
                REQUIRE_MSG(nodes == 1u,
                            "a chunk past the scatter tier owns prop nodes; the "
                            "resident instance count is what caps Scene::cull "
                            "and this is the knob that holds it",
                            "scatter tier");
            } else {
                REQUIRE_MSG(nodes == 1u + scatter_chunk(h.seed, c).size(),
                            "a chunk inside the scatter tier is missing props",
                            "scatter tier");
                props_near += nodes - 1u;
            }
        }
    }

    REQUIRE_MSG(props_near > 0u, "no props anywhere; the tier test is vacuous",
                "scatter tier");
    std::printf("    [scatter tier] %zu scene nodes total, %zu of them props\n",
                h.scene.size(), props_near);
}

// --- 7. determinism survives LOD --------------------------------------------

void two_streamers_crossing_rings_agree_exactly() {
    Host a(0xA11CEull, tiered_config());
    Host b(0xA11CEull, tiered_config());

    // A path that crosses every ring boundary in both directions.
    const ChunkCoord path[] = {{0, 0},  {2, 0},  {4, 1},  {6, 3}, {4, 5},
                               {1, 6},  {-2, 4}, {-5, 1}, {-2, -3}, {0, 0}};

    for (const ChunkCoord c : path) {
        for (int rep = 0; rep < 20; ++rep) {
            const glm::vec3 cam = centre_of(c);
            const StreamerStats sa = a.tick(cam);
            const StreamerStats sb = b.tick(cam);

            REQUIRE_MSG(sa.chunks_requested == sb.chunks_requested,
                        "load plans diverged", "determinism");
            REQUIRE_MSG(sa.quads_requested == sb.quads_requested,
                        "the quad budget spent differently", "determinism");
            REQUIRE_MSG(sa.chunks_refitted == sb.chunks_refitted,
                        "refits diverged", "determinism");
            REQUIRE_MSG(sa.chunks_evicted == sb.chunks_evicted,
                        "eviction diverged", "determinism");
            REQUIRE_MSG(a.scene.size() == b.scene.size(),
                        "scene sizes diverged", "determinism");
            REQUIRE_MSG(a.streamer.pending_loads() == b.streamer.pending_loads(),
                        "the load ORDER or the chosen LEVELS diverged",
                        "determinism");
            REQUIRE_MSG(a.total_frees == b.total_frees,
                        "mesh frees diverged", "determinism");
        }
    }
    REQUIRE(a.streamer.resident_count() == b.streamer.resident_count());
}

// --- 8. the production config, against the numbers it was sized to ----------

void the_shipping_rings_stay_under_the_cull_budget() {
    // The defaults, as the app runs them.
    Host h(0xA5EED0FFC0FFEE11ull, StreamerConfig{});

    const glm::vec3 spawn = centre_of(ChunkCoord{0, 0});
    h.fill(spawn);
    h.settle(spawn, 40000);

    std::size_t by_lod[kMaxChunkLod + 1];
    h.streamer.residency_by_lod(by_lod);

    std::printf("    [shipping] chunks %zu (%zu / %zu / %zu / %zu), "
                "scene nodes %zu, mesh bytes %.1f MB\n",
                h.streamer.resident_count(), by_lod[0], by_lod[1], by_lod[2],
                by_lod[3], h.scene.size(),
                static_cast<double>(h.bytes) / (1024.0 * 1024.0));

    // docs/design/pinatty.md measured Scene::cull() at 0.278 ms for 60k nodes
    // and 1.449 ms for 200k, and recommended capping resident static instances
    // near 60k with draw-distance tiers rather than building a BVH. This is the
    // assertion that keeps that recommendation true as the rings get tuned.
    REQUIRE_MSG(h.scene.size() < 60000u,
                "resident static instances went past the 60k the cull scan was "
                "sized for; retune the rings or max_scatter_lod before anyone "
                "reaches for a BVH",
                "shipping budget");

    // The whole point of the exercise: a 2.5 km ring at full detail was
    // measured at 1.34 GB. Anything close to that means LOD is not being
    // applied.
    REQUIRE_MSG(h.bytes < 256u * 1024u * 1024u,
                "resident terrain vertex data is over 256 MB; the LOD rings are "
                "not doing their job",
                "shipping budget");

    const ChunkCoord under = chunk_at(spawn.x, spawn.z);
    REQUIRE(h.streamer.resident_lod(under) == 0);
}

}  // namespace

int main() {
    std::printf("streamer_lod_tests\n");
    every_chunk_is_resident_at_the_level_its_ring_asks_for();
    apricot_test::pass("every chunk is resident at the level its ring asks for");
    the_ground_the_car_can_touch_is_always_level_zero();
    apricot_test::pass("the ground the car can touch is always level zero");
    a_chunk_changing_level_is_never_absent_from_the_scene();
    apricot_test::pass("a chunk changing level is never absent from the scene");
    a_long_drive_frees_every_mesh_it_stops_using();
    apricot_test::pass("a long drive frees every mesh it stops using");
    a_cold_start_fills_before_it_is_ready();
    apricot_test::pass("a cold start fills before it is ready");
    a_teleport_across_the_island_fills_before_it_resumes();
    apricot_test::pass("a teleport across the island fills before it resumes");
    a_warp_inside_the_loaded_radius_still_refits_before_it_resumes();
    apricot_test::pass("a warp inside the loaded radius still refits first");
    chunks_past_the_scatter_tier_carry_terrain_only();
    apricot_test::pass("chunks past the scatter tier carry terrain only");
    two_streamers_crossing_rings_agree_exactly();
    apricot_test::pass("two streamers crossing rings agree exactly");
    the_shipping_rings_stay_under_the_cull_budget();
    apricot_test::pass("the shipping rings stay under the cull budget");
    return apricot_test::done("streamer_lod_tests");
}
