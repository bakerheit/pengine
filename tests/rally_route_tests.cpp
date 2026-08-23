// Route generation: reproducible from the seed, and every gate on ground a car
// can actually stand on.
//
// The drivability assertions call the SAME predicate the generator used
// (is_gate_ground) rather than a copy of its thresholds. A test that keeps its
// own copy of a threshold is a test that goes on passing after the rule moves.

#include <cmath>
#include <cstdio>
#include <vector>

#include "core/rng.h"
#include "game/route.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr int kGates = 8;

// A spread of seeds that is itself reproducible, so a failure names a seed you
// can go and look at rather than "one of the random ones".
uint64_t nth_seed(int i) {
    return splitmix64_mix(0xA9C0FFEEull + static_cast<uint64_t>(i));
}

bool same_checkpoint(const Checkpoint& a, const Checkpoint& b) {
    return a.position == b.position && a.forward == b.forward &&
           a.right == b.right && a.half_width == b.half_width &&
           a.half_height == b.half_height;
}

void test_reproducible_from_seed() {
    for (int i = 0; i < 12; ++i) {
        const uint64_t seed = nth_seed(i);
        const TerrainCollider collider(seed);

        const Route a = build_route(seed, collider, kGates);
        const Route b = build_route(seed, collider, kGates);

        REQUIRE_MSG(route_ok(a, kGates), "route a is complete", "seed");
        REQUIRE_MSG(a.checkpoints.size() == b.checkpoints.size(),
                    "gate count matches", "seed");
        REQUIRE_MSG(a.seed == seed && b.seed == seed, "route carries its seed",
                    "seed");

        for (std::size_t g = 0; g < a.checkpoints.size(); ++g) {
            REQUIRE_MSG(same_checkpoint(a.checkpoints[g], b.checkpoints[g]),
                        "two generations are bit-identical", "gate");
        }

        // And built from a collider constructed separately — the route must be
        // a function of the seed, not of some state the collider accumulated.
        const TerrainCollider fresh(seed);
        const Route c = build_route(seed, fresh, kGates);
        for (std::size_t g = 0; g < a.checkpoints.size(); ++g) {
            REQUIRE_MSG(same_checkpoint(a.checkpoints[g], c.checkpoints[g]),
                        "a fresh collider gives the same route", "gate");
        }
    }
    apricot_test::pass("route is identical across two generations from one seed");
}

void test_every_gate_is_drivable() {
    int checked = 0;
    for (int i = 0; i < 24; ++i) {
        const uint64_t seed = nth_seed(i);
        const TerrainCollider collider(seed);
        const Route route = build_route(seed, collider, kGates);

        REQUIRE_MSG(route_ok(route, kGates), "every seed yields a full route",
                    "seed");

        const int count = static_cast<int>(route.checkpoints.size());
        for (int g = 0; g < count; ++g) {
            const Checkpoint& cp = route.checkpoints[static_cast<std::size_t>(g)];

            REQUIRE_MSG(is_gate_ground(collider, cp.position.x, cp.position.z),
                        "gate stands on drivable ground", "gate");

            // Not "close to" the surface — ON it. A renderer plants posts from
            // this without re-probing.
            REQUIRE(cp.position.y == collider.height(cp.position.x, cp.position.z));

            // Above the water line by the margin the rule asks for.
            REQUIRE(cp.position.y >= kGateMinHeight);

            REQUIRE(cp.forward.y == 0.0f);
            REQUIRE_NEAR(static_cast<double>(glm::length(cp.forward)), 1.0, 1e-5);
            REQUIRE(cp.right.y == 0.0f);
            REQUIRE_NEAR(static_cast<double>(glm::length(cp.right)), 1.0, 1e-5);
            REQUIRE_NEAR(static_cast<double>(glm::dot(cp.forward, cp.right)), 0.0,
                         1e-5);

            const Checkpoint& next =
                route.checkpoints[static_cast<std::size_t>((g + 1) % count)];
            const glm::vec3 d = next.position - cp.position;
            const float span = std::sqrt(d.x * d.x + d.z * d.z);
            REQUIRE_MSG(span >= kGateMinSpacing,
                        "consecutive gates are not the same corner twice",
                        "gate");
            ++checked;
        }
    }
    std::printf("      %d gates checked across 24 seeds\n", checked);
    apricot_test::pass("every gate sits on drivable ground");
}

void test_seeds_produce_different_routes() {
    // Not a strict requirement of anything, but a route that ignored its seed
    // would pass every other test in this file.
    const uint64_t a_seed = nth_seed(0);
    const uint64_t b_seed = nth_seed(1);
    const TerrainCollider ca(a_seed);
    const TerrainCollider cb(b_seed);

    const Route a = build_route(a_seed, ca, kGates);
    const Route b = build_route(b_seed, cb, kGates);
    REQUIRE(route_ok(a, kGates) && route_ok(b, kGates));

    bool any_different = false;
    for (std::size_t g = 0; g < a.checkpoints.size(); ++g) {
        if (!same_checkpoint(a.checkpoints[g], b.checkpoints[g])) {
            any_different = true;
            break;
        }
    }
    REQUIRE(any_different);
    apricot_test::pass("different seeds lay out different routes");
}

void test_degenerate_counts_refuse() {
    const uint64_t seed = nth_seed(3);
    const TerrainCollider collider(seed);

    for (int count : {-1, 0, 1, 2}) {
        const Route r = build_route(seed, collider, count);
        REQUIRE_MSG(r.checkpoints.empty(),
                    "a loop needs three gates; fewer is refused, not faked",
                    "count");
        REQUIRE(r.seed == seed);
    }
    apricot_test::pass("a route too small to be a loop is refused outright");
}

void test_gate_frame_points_along_the_route() {
    // forward should point roughly from the previous gate toward the next,
    // which is what makes "through the gate the right way" mean anything.
    const uint64_t seed = nth_seed(5);
    const TerrainCollider collider(seed);
    const Route route = build_route(seed, collider, kGates);
    REQUIRE(route_ok(route, kGates));

    const int count = static_cast<int>(route.checkpoints.size());
    for (int g = 0; g < count; ++g) {
        const Checkpoint& cp = route.checkpoints[static_cast<std::size_t>(g)];
        const Checkpoint& next =
            route.checkpoints[static_cast<std::size_t>((g + 1) % count)];

        glm::vec3 to_next = next.position - cp.position;
        to_next.y = 0.0f;
        to_next = glm::normalize(to_next);

        REQUIRE_MSG(glm::dot(cp.forward, to_next) > 0.3f,
                    "gate faces the way the route is going", "gate");

        // And the posts straddle the centre.
        const glm::vec3 l = gate_post_left(cp);
        const glm::vec3 r = gate_post_right(cp);
        REQUIRE_NEAR(static_cast<double>(glm::length(r - l)),
                     static_cast<double>(cp.half_width * 2.0f), 1e-3);
        REQUIRE(glm::dot(r - cp.position, cp.right) > 0.0f);
    }
    apricot_test::pass("gate frames are oriented along the route direction");
}

}  // namespace

int main() {
    test_reproducible_from_seed();
    test_every_gate_is_drivable();
    test_seeds_produce_different_routes();
    test_degenerate_counts_refuse();
    test_gate_frame_points_along_the_route();
    return apricot_test::done("rally_route_tests");
}
