// Characterization tests for src/game/traffic.cpp.
//
// These pin down current observable behavior of TrafficSystem in advance of
// the PBD-007 split. They assert what TrafficSystem does today, not what it
// should do. After PBD-007, these tests should continue to pass unchanged —
// if they don't, the refactor isn't behavior-preserving and either the
// refactor or the tests need to be revisited (almost certainly the refactor).
//
// Why a separate target from traffic_ai_tests: that one is deliberately
// lightweight (links just glm + threads, tests pure helpers in traffic_ai.h).
// These tests require a real Scene, RoadGraph, WorldCollision, and the full
// asset/heightmap stack to instantiate TrafficSystem, so they link the full
// pengine_core static library.

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include <glm/glm.hpp>

#include "test_assert.h"

#include "core/log.h"
#include "game/traffic.h"
#include "game/traffic_ai.h"
#include "physics/world_collision.h"
#include "platform/window.h"
#include "scene/scene.h"
#include "world/heightmap.h"
#include "world/road_graph.h"
#include "world/world_defs.h"

using namespace pengine;

namespace {

// =============================================================================
// Fixture
// =============================================================================

// A minimal world: a 3×3 grid of loaded road cells centered on (0,0). Enough
// for spawn retries to land in the [spawn_min, spawn_max] band from a camera
// at the origin, and for routes to extend a few lanes ahead.
constexpr int CELL_GRID_HALF = 2;  // cells from -2..+2 on each axis (5x5)

// World size for the synthetic heightmap. Generous enough that all our test
// cells fit comfortably inside.
constexpr float TEST_WORLD_SIZE_M = 2048.f;

void load_test_cells(RoadGraph& graph) {
    for (int i = -CELL_GRID_HALF; i <= CELL_GRID_HALF; ++i) {
        for (int j = -CELL_GRID_HALF; j <= CELL_GRID_HALF; ++j) {
            graph.add_cell({i, j});
        }
    }
}

// Initialise the global Heightmap once per test process. The Heightmap is a
// process-wide static; if the PNG doesn't exist at the given path, init()
// generates a synthetic landscape and saves it. We point it at a tmpfile so
// tests don't touch the repo's assets/ tree.
void ensure_heightmap_initialised() {
    static bool initialised = false;
    if (initialised) return;
    auto tmp = std::filesystem::temp_directory_path()
             / "pengine_traffic_test_heightmap.png";
    bool ok = Heightmap::init(TEST_WORLD_SIZE_M, tmp);
    REQUIRE(ok);
    initialised = true;
}

// Stand up a process-wide OpenGL context so that TrafficSystem::init() can
// upload its wheel + body meshes. load_static_emesh() calls glBindBuffer /
// glBufferData directly; without a GL context glad's function pointers are
// NULL and the call site segfaults. The engine's Window class is the natural
// way to get a context — same SDL setup the game uses, just at a tiny
// resolution. A single static instance persists for the whole test process;
// it's cheap to create and tearing it down between tests caused
// SDL_Init/SDL_Quit churn we don't need.
Window& ensure_gl_context() {
    static Window window;
    static bool   initialised = false;
    if (!initialised) {
        WindowConfig cfg;
        cfg.title  = "traffic_system_tests";
        cfg.width  = 64;
        cfg.height = 64;
        cfg.vsync  = false;
        bool ok = window.init(cfg);
        REQUIRE(ok && "failed to create GL context for tests");
        initialised = true;
    }
    return window;
}

// A fully constructed TrafficSystem with empty world collision, a loaded
// road graph, and no traffic-light visuals (LightVisuals are visual-only
// scaffolding; passing nulls means lights skip their render path without
// affecting AI / spawn behavior).
struct Fixture {
    Scene           scene;
    RoadGraph       graph;
    WorldCollision  world;
    TrafficSystem   traffic;

    explicit Fixture(int target_ai_count = 0) {
        ensure_gl_context();
        ensure_heightmap_initialised();
        load_test_cells(graph);

        TrafficSystem::LightVisuals lights{};  // null mesh + null texture
        bool ok = traffic.init(scene, lights, graph, target_ai_count);
        REQUIRE(ok);
    }

    // Drive the simulation for `frames` ticks at `dt` seconds each, with the
    // camera held at `camera_pos`. After each tick the scene transforms are
    // updated so subsequent frames see consistent world state.
    void tick(int frames, float dt, const glm::vec3& camera_pos) {
        double t = 0.0;
        for (int f = 0; f < frames; ++f) {
            traffic.update(dt, t, camera_pos, world);
            scene.update();
            t += dt;
        }
    }

    // Count cars by driver kind. Player and Parked cars stick around even
    // after the AI loses interest, so tests that care about "AI population"
    // need to filter.
    int count_by_driver(TrafficSystem::Driver want) const {
        int n = 0;
        for (const auto& cp : traffic.cars()) {
            if (cp->driver == want) ++n;
        }
        return n;
    }
};

// =============================================================================
// Tests
// =============================================================================

// (a) Spawn rate.
//
// TrafficSystem fills toward target_ai_count, spawning up to 2 cars per tick
// (the budget at traffic.cpp:1515). With a loaded road graph, a camera in the
// spawn band, and enough ticks to absorb retry failures, the AI population
// should converge near the target. We assert "approached the target" with a
// generous tolerance — the point is to catch "stopped spawning" or "spawning
// way too much," not to pin the exact convergence number.
void test_spawn_rate_converges_toward_target() {
    constexpr int target = 8;
    Fixture fx(target);

    const glm::vec3 camera{0.f, 0.f, 0.f};

    // 30 ticks at 1/30s = ~1 second simulated. Budget of 2/tick caps spawn
    // ramp at 16 over this window; retry failures will bring it lower.
    fx.tick(/*frames*/ 30, /*dt*/ 1.f / 30.f, camera);

    int ai = fx.count_by_driver(TrafficSystem::Driver::AI);

    // Lower bound: spawning is happening at all.
    REQUIRE(ai >= target / 2);
    // Upper bound: we never exceed target.
    REQUIRE(ai <= target);
}

// (b) Lane assignment.
//
// Every freshly-spawned AI car must hold a lane id that the TrafficLaneGraph
// considers loaded, and an ai_distance_along that's within the lane's length.
// This pins down the spawn-site assignment contract: a car the spawner
// produces is positioned coherently on the lane graph it was spawned into.
// Also checks that ai_next_lane is loaded (the route needs a valid hop
// after the current lane).
void test_spawned_cars_have_valid_lane_assignments() {
    constexpr int target = 6;
    Fixture fx(target);

    fx.tick(/*frames*/ 30, /*dt*/ 1.f / 30.f, glm::vec3{0.f, 0.f, 0.f});

    TrafficLaneGraph lanes(fx.graph);

    int checked = 0;
    for (const auto& cp : fx.traffic.cars()) {
        if (cp->driver != TrafficSystem::Driver::AI) continue;
        ++checked;

        // Current lane is loaded.
        REQUIRE(lanes.lane_loaded(cp->ai_lane));

        // distance_along sits within the lane's length (within a small float
        // tolerance — the value is a metres count, lane.length is also metres).
        TrafficLane current = lanes.lane(cp->ai_lane, /*lane_offset*/ 2.f);
        REQUIRE(cp->ai_distance_along >= -0.01f);
        REQUIRE(cp->ai_distance_along <= current.length + 0.01f);

        // Next-lane hop is also loaded — the spawner builds a route, so the
        // next lane must exist or the route is broken.
        REQUIRE(lanes.lane_loaded(cp->ai_next_lane));
    }

    // The test is meaningless if nothing spawned — confirm the inner loop
    // actually ran. (This also gives us a free-spawn sanity check separate
    // from the dedicated spawn-rate test above.)
    REQUIRE(checked > 0);
}

// (d) Driver patience.
//
// Pins down: when an AI car is blocked by something stationary ahead of it
// on its lane, its ai_blocked_timer accumulates each frame. This is the
// foundation the swerve / BlockedRecovery / honk paths build on
// (traffic.cpp:1022 increments the timer, line 1042 reads it to decide on
// lane changes, line 1079 transitions to BlockedRecovery once it exceeds
// patience_seconds + 6).
//
// Setup: force one AI car to be stationary by writing to its public
// ai_target_speed / ai_speed fields. This is allowed because Car's field
// surface is part of the public API (see TrafficSystem::cars() returning
// the full Car). If PBD-007 changes these fields, that's an API change
// and this test should be updated to match. We're not reaching into
// orchestration internals — we're using the same surface the engine
// exposes for reading, in the opposite direction for setup.
void test_blocked_driver_accumulates_patience_timer() {
    constexpr int target = 10;
    Fixture fx(target);

    // Spawn some traffic.
    fx.tick(/*frames*/ 60, /*dt*/ 1.f / 30.f, glm::vec3{0.f, 0.f, 0.f});

    // Pick two AI cars: one as the blocker, the other as the follower we'll
    // force onto the same lane just behind the blocker. The original version
    // of this test relied on spawn-rng happening to put a follower on the
    // blocker's lane — but with ~10 cars sprinkled over ~100 possible lanes
    // (5x5 cells * 4 directions) that almost never happens. PBD-046
    // instrumentation confirmed: across 600 frames, same-lane-behind = 0,
    // closest other car was always on a different lane. Production blocking
    // detection is fine; the test's setup assumption was the bug. We now
    // make the scenario deterministic by snapping a second AI car onto the
    // blocker's lane state and world position.
    TrafficSystem::Car* blocker  = nullptr;
    TrafficSystem::Car* follower = nullptr;
    for (const auto& cp : fx.traffic.cars()) {
        if (cp->driver != TrafficSystem::Driver::AI) continue;
        if (!blocker)       { blocker = cp.get();  continue; }
        if (!follower)      { follower = cp.get(); break;    }
    }
    REQUIRE(blocker != nullptr);
    REQUIRE(follower != nullptr);
    blocker->ai_target_speed = 0.f;
    blocker->ai_speed        = 0.f;

    // Snap the follower onto the blocker's lane ~6m behind (within STUCK_GAP
    // = 8m so the blocked condition triggers). Copy route state so the
    // follower's per-frame ai_advance / ai_extend_route stays self-consistent
    // (the route is what was already validated at spawn).
    follower->ai_lane              = blocker->ai_lane;
    follower->ai_next_lane         = blocker->ai_next_lane;
    follower->ai_prev_lane         = blocker->ai_prev_lane;
    follower->ai_route             = blocker->ai_route;
    follower->ai_route_index       = blocker->ai_route_index;
    follower->ai_distance_along    = std::max(0.f,
                                              blocker->ai_distance_along - 6.f);
    follower->ai_lateral_offset    = 0.f;
    follower->ai_lateral_rate      = 0.f;
    follower->ai_lane_change       = pengine::LaneChangeIntent::None;
    follower->ai_in_turn           = false;
    follower->ai_blocked_timer     = 0.f;
    follower->ai_speed             = 0.f;
    TrafficLaneGraph follower_lanes(fx.graph);
    glm::vec3 snap_xz = follower_lanes.lane_pose(follower->ai_lane,
                                                  follower->ai_distance_along,
                                                  /*lane_offset*/ 2.f, 0.f);
    glm::vec3 cur = follower->vehicle.position();
    follower->vehicle.set_kinematic_pose(glm::vec3{snap_xz.x, cur.y, snap_xz.z},
                                          follower->vehicle.orientation());

    // Run the simulation forward and track the highest blocked_timer the
    // follower reaches. We're not asserting exactly how long it takes —
    // just that the system notices it's blocked.
    float max_other_blocked_timer = 0.f;
    for (int f = 0; f < 600; ++f) {  // 20 simulated seconds at 30 Hz
        fx.tick(/*frames*/ 1, /*dt*/ 1.f / 30.f, glm::vec3{0.f, 0.f, 0.f});
        for (const auto& cp : fx.traffic.cars()) {
            if (cp.get() == blocker) continue;
            if (cp->driver != TrafficSystem::Driver::AI) continue;
            max_other_blocked_timer =
                std::max(max_other_blocked_timer, cp->ai_blocked_timer);
        }
        // Keep re-pinning the blocker — ai_update_speed would otherwise
        // ramp its target back up over time.
        blocker->ai_target_speed = 0.f;
        blocker->ai_speed        = 0.f;
    }

    // Some car must have run into the blocker.
    REQUIRE(max_other_blocked_timer > 0.f);
}

// (c) Parked-vehicle recovery to AI.
//
// Pins down: an AI car demoted to Parked with ai_recovery_pending=true is
// re-promoted to AI by try_ai_recover (traffic_drive.cpp::try_ai_recover) once
// it has settled (upright, slow, near its lane). This is the post-collision
// recovery path:
// when a moving car gets knocked out of AI control by a hit, the system
// gives the chassis a moment to bleed off the impact and then snaps it back
// onto the lane.
//
// We trigger the scenario by writing to public Car fields directly rather
// than orchestrating a real collision — the agreed-on Car-field-write-for-
// setup pattern (see "Setup:" comment block below).
void test_parked_car_recovers_to_ai_when_settled() {
    Fixture fx(5);

    // Spawn some cars so we have an AI car to work with.
    fx.tick(/*frames*/ 60, /*dt*/ 1.f / 30.f, glm::vec3{0.f, 0.f, 0.f});

    TrafficSystem::Car* target = nullptr;
    for (const auto& cp : fx.traffic.cars()) {
        if (cp->driver == TrafficSystem::Driver::AI) {
            target = cp.get();
            break;
        }
    }
    REQUIRE(target != nullptr);

    // Setup: force the scenario by writing to public Car fields directly.
    // This is allowed because Car's field surface is part of the public
    // API (see TrafficSystem::cars() returning the full Car). If PBD-007
    // changes these fields, that's an API change and this test should be
    // updated to match.
    target->driver               = TrafficSystem::Driver::Parked;
    target->ai_recovery_pending  = true;
    target->ai_recovery_timer    = 0.f;
    // Recovery checks vehicle.speed() <= 6.0 m/s; set_kinematic_pose zeroes
    // both linear and angular velocity so the speed gate is immediately
    // satisfied without waiting for drag.
    target->vehicle.set_kinematic_pose(target->vehicle.position(),
                                        target->vehicle.orientation());

    // try_ai_recover has a 1.5s delay before first attempt + 8.0s ceiling
    // before giving up. Run for up to 10 simulated seconds and bail on
    // success.
    bool recovered = false;
    for (int f = 0; f < 300 && !recovered; ++f) {
        fx.tick(/*frames*/ 1, /*dt*/ 1.f / 30.f, glm::vec3{0.f, 0.f, 0.f});
        if (target->driver == TrafficSystem::Driver::AI) recovered = true;
    }
    REQUIRE(recovered);
    // try_ai_recover (traffic_drive.cpp:481) DOES set ai_state = Cruise on
    // the promotion, but in the same update() loop that promotes the car,
    // the very next case branch runs update_ai_kinematic on it. That
    // recomputes ai_state every frame from the world (FollowLeader,
    // ApproachSignal, Queued, ...) so by the time this test observes
    // ai_state it is whatever the freshly-recovered car's lane situation
    // implies — typically ApproachSignal in this fixture (no leader, but a
    // red light up ahead at t=2s of sim time). That's correct production
    // behavior, so we don't assert Cruise here; we just confirm the
    // recovery bookkeeping was cleaned up.
    REQUIRE(target->ai_recovery_pending == false);
    REQUIRE(target->ai_recovery_timer == 0.f);
}

}  // namespace

int main() {
    // Quiet the per-mesh-load and per-model-init INFO chatter. We still want
    // to see WARN/ERROR if anything goes sideways in the asset load path.
    pengine::log::min_level() = pengine::log::Level::Warn;

    test_spawn_rate_converges_toward_target();
    test_spawned_cars_have_valid_lane_assignments();
    test_blocked_driver_accumulates_patience_timer();
    test_parked_car_recovers_to_ai_when_settled();
    std::printf("traffic_system_tests: OK\n");
    return 0;
}
