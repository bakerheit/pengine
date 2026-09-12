// TerrainCollider::line_of_sight_blocked() against the raycast it replaced.
//
// This query exists because raycast() was the wrong tool for a visibility
// question: it marches the height field at a quarter of the terrain's own
// lattice spacing, which a wheel needs and a sight line does not. Police
// visibility was paying 0.208 ms a call for that, five calls a sim step.
//
// Swapping in a cheaper query is only safe if it gives the SAME ANSWER, so
// that is what is measured here — not against a formula rewritten from the
// implementation, but against the predicate the old call site actually used:
//
//     blocked  ==  hit && hit.distance < distance - slack
//
// The one difference the new query is allowed is a terrain notch narrower than
// the lattice can represent. That is the documented trade, so the agreement
// bound below is stated as a rate and the failures are printed rather than
// swallowed: if this ever starts disagreeing on open ground, the number moves
// and the test says by how much.

#include <cmath>
#include <cstdio>
#include <vector>

#include "core/aabb.h"
#include "core/rng.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr uint64_t kSeed = 0xC0FFEEu;
constexpr float kSlack = 0.08f;

// The predicate the police call site used before this query existed.
bool blocked_by_raycast(const TerrainCollider& c, glm::vec3 from, glm::vec3 to) {
    const glm::vec3 delta = to - from;
    const float distance = glm::length(delta);
    if (!(distance > 1e-6f)) return false;
    const auto hit = c.raycast(from, delta / distance, distance);
    return hit.hit && hit.distance < distance - kSlack;
}

// Eye height above the local ground, the way an officer and a target stand.
glm::vec3 standing_at(const TerrainCollider& c, float x, float z, float eye) {
    return {x, c.height(x, z) + eye, z};
}

void test_a_clear_sight_line_is_not_blocked() {
    const TerrainCollider collider(kSeed);
    // Two points a few metres apart on the same ground: nothing between them.
    const glm::vec3 a = standing_at(collider, 20.0f, 20.0f, 1.8f);
    const glm::vec3 b = standing_at(collider, 26.0f, 20.0f, 1.05f);
    REQUIRE(!collider.line_of_sight_blocked(a, b));
    REQUIRE_MSG(!blocked_by_raycast(collider, a, b),
                "the old predicate agrees", "clear");
    apricot_test::pass("a short sight line over open ground is clear both ways");
}

// The slack is what stops the ground under the target's own feet from blocking
// every view of it. Without it this query answers "blocked" for everything.
void test_the_ground_under_the_target_does_not_block_it() {
    const TerrainCollider collider(kSeed);
    const glm::vec3 eye = standing_at(collider, 40.0f, 40.0f, 1.8f);
    const glm::vec3 feet{44.0f, collider.height(44.0f, 40.0f), 44.0f};
    REQUIRE_MSG(!collider.line_of_sight_blocked(eye, feet, kSlack),
                "a target standing on the ground is still visible", "slack");
    apricot_test::pass("the ground the target stands on does not hide them");
}

// A building between two points blocks the view, and does so without the
// terrain march having to run at all.
void test_a_static_box_blocks_the_view() {
    TerrainCollider collider(kSeed);
    const float g = collider.height(100.0f, 0.0f);
    const glm::vec3 a = standing_at(collider, 80.0f, 0.0f, 1.8f);
    const glm::vec3 b = standing_at(collider, 120.0f, 0.0f, 1.05f);
    REQUIRE_MSG(!collider.line_of_sight_blocked(a, b),
                "clear before the wall goes up", "before");

    collider.add_static_box(AABB{{98.0f, g - 1.0f, -8.0f},
                                 {102.0f, g + 9.0f, 8.0f}});
    REQUIRE_MSG(collider.line_of_sight_blocked(a, b),
                "the wall blocks it", "after");
    REQUIRE_MSG(blocked_by_raycast(collider, a, b),
                "and the old predicate says the same", "agree");
    apricot_test::pass("a building between two points blocks the sight line");
}

// A box off to the side must not block anything: a box test that ignores the
// ray direction would make every officer blind the moment a wall existed.
void test_a_box_beside_the_line_does_not_block() {
    TerrainCollider collider(kSeed);
    const float g = collider.height(100.0f, 0.0f);
    collider.add_static_box(AABB{{98.0f, g - 1.0f, 40.0f},
                                 {102.0f, g + 9.0f, 56.0f}});
    const glm::vec3 a = standing_at(collider, 80.0f, 0.0f, 1.8f);
    const glm::vec3 b = standing_at(collider, 120.0f, 0.0f, 1.05f);
    REQUIRE(!collider.line_of_sight_blocked(a, b));
    apricot_test::pass("a wall to one side of the line leaves it clear");
}

// The one that matters: over real terrain, at real sight-line geometry, the
// cheap query must answer the way the expensive one did.
void test_it_agrees_with_the_raycast_it_replaced() {
    const TerrainCollider collider(kSeed);
    int compared = 0;
    int disagreed = 0;

    // Deterministic sample of eye-to-target lines across the island, at the
    // ranges police visibility actually uses.
    for (int i = 0; i < 400; ++i) {
        const uint64_t h = splitmix64_mix(0x5157u + static_cast<uint64_t>(i));
        const float ax = static_cast<float>(static_cast<int>(h % 4000u)) - 2000.0f;
        const float az = static_cast<float>(static_cast<int>((h >> 20) % 4000u)) - 2000.0f;
        const float angle = static_cast<float>((h >> 40) % 628u) * 0.01f;
        const float range = 12.0f + static_cast<float>((h >> 50) % 90u);

        const float bx = ax + std::cos(angle) * range;
        const float bz = az + std::sin(angle) * range;
        const glm::vec3 from = standing_at(collider, ax, az, 1.8f);
        const glm::vec3 to = standing_at(collider, bx, bz, 1.05f);

        ++compared;
        const bool fast = collider.line_of_sight_blocked(from, to, kSlack);
        const bool slow = blocked_by_raycast(collider, from, to);
        if (fast != slow) {
            ++disagreed;
            if (disagreed <= 5) {
                std::printf("      differ: (%.0f,%.0f)->(%.0f,%.0f) fast=%d slow=%d\n",
                            static_cast<double>(ax), static_cast<double>(az),
                            static_cast<double>(bx), static_cast<double>(bz),
                            fast ? 1 : 0, slow ? 1 : 0);
            }
        }
    }

    REQUIRE(compared == 400);
    // Stated as a rate on purpose. The trade this query makes is a terrain
    // notch under a metre wide; if it ever starts disagreeing broadly, this
    // number moves and the lines printed above say where.
    const double rate = 100.0 * disagreed / compared;
    std::printf("      agreement: %d/%d (%.2f%% differ)\n", compared - disagreed,
                compared, rate);
    REQUIRE_MSG(rate <= 2.0,
                "the cheap query must answer the way the expensive one did",
                "agreement");
    apricot_test::pass("it agrees with the raycast it replaced across the island");
}

// Degenerate inputs reach this from the sim, and a NaN answer to "can he see
// you" is an officer who behaves differently every frame.
void test_degenerate_segments_are_not_blocked() {
    const TerrainCollider collider(kSeed);
    const glm::vec3 p = standing_at(collider, 10.0f, 10.0f, 1.8f);
    REQUIRE_MSG(!collider.line_of_sight_blocked(p, p),
                "a zero-length segment blocks nothing", "zero");
    // A segment shorter than the slack is all slack and cannot block.
    const glm::vec3 q = p + glm::vec3{0.01f, 0.0f, 0.0f};
    REQUIRE(!collider.line_of_sight_blocked(p, q, kSlack));
    apricot_test::pass("zero-length and sub-slack segments block nothing");
}

}  // namespace

int main() {
    test_a_clear_sight_line_is_not_blocked();
    test_the_ground_under_the_target_does_not_block_it();
    test_a_static_box_blocks_the_view();
    test_a_box_beside_the_line_does_not_block();
    test_it_agrees_with_the_raycast_it_replaced();
    test_degenerate_segments_are_not_blocked();
    return apricot_test::done("line_of_sight_tests");
}
