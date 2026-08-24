// Headless tests for the pedestrian crowd-separation steering.
//
// ped_separation() is the whole per-ped decision: the crowd system around it
// only gathers neighbours and smooths the result over time. So the load-bearing
// parts are all here — push away from close neighbours, project onto the lane
// right vector, clamp to the sidewalk strip, detect head-on blocking, and the
// keep-right tie-break — and they are pinned against the real function rather
// than a copy of it.
//
// What this CANNOT tell you: whether thirty peds on one pavement look like a
// crowd. That is a feel-check and it stays one.

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "city/pedestrian_separation.h"
#include "test_assert.h"

using namespace apricot;

namespace {

bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

PedSeparation eval(glm::vec2 self, glm::vec2 fwd,
                   const std::vector<glm::vec2>& neigh) {
    fwd = glm::normalize(fwd);
    return ped_separation(self, fwd, neigh.data(), neigh.size());
}

// The lane right vector ped_separation projects onto.
glm::vec2 right_of(glm::vec2 fwd) { fwd = glm::normalize(fwd); return {fwd.y, -fwd.x}; }

void test_no_neighbours() {
    PedSeparation s = eval({0, 0}, {1, 0}, {});
    REQUIRE(approx(s.lateral_target, 0.f));
    REQUIRE(!s.blocked);
    apricot_test::pass("empty neighbourhood is a no-op");
}

void test_neighbour_on_right_pushes_left() {
    glm::vec2 fwd{1, 0};
    glm::vec2 r = right_of(fwd);
    // Neighbour 0.5 m off to the ped's RIGHT -> ped should sidestep to the LEFT
    // (negative offset along the right axis), and not be "blocked" (it is
    // beside, not ahead).
    PedSeparation s = eval({0, 0}, fwd, {r * 0.5f});
    REQUIRE(s.lateral_target < -0.01f);
    REQUIRE(!s.blocked);
    apricot_test::pass("neighbour on the right pushes left");
}

void test_neighbour_on_left_pushes_right() {
    glm::vec2 fwd{1, 0};
    glm::vec2 r = right_of(fwd);
    PedSeparation s = eval({0, 0}, fwd, {-r * 0.5f});
    REQUIRE(s.lateral_target > 0.01f);
    REQUIRE(!s.blocked);
    apricot_test::pass("neighbour on the left pushes right");
}

void test_head_on_blocks_and_keeps_right() {
    glm::vec2 fwd{1, 0};
    // Neighbour 0.5 m directly AHEAD -> blocked, and with ~no lateral component
    // the tie-break nudges the ped to pass on the right (positive ~0.5 m).
    PedSeparation s = eval({0, 0}, fwd, {fwd * 0.5f});
    REQUIRE(s.blocked);
    REQUIRE(s.lateral_target > 0.4f);
    apricot_test::pass("head-on blocks, and both peds pass on the right");
}

void test_ahead_but_outside_block_dist_not_blocked() {
    glm::vec2 fwd{1, 0};
    // 1.1 m ahead: inside the 1.3 m search radius but beyond the 0.9 m block
    // distance -> contributes a (purely longitudinal) push but is NOT blocking,
    // so no tie-break and ~zero lateral target.
    PedSeparation s = eval({0, 0}, fwd, {fwd * 1.1f});
    REQUIRE(!s.blocked);
    REQUIRE(approx(s.lateral_target, 0.f, 0.05f));
    apricot_test::pass("inside the search radius is not the same as blocking");
}

void test_neighbour_beyond_radius_ignored() {
    glm::vec2 fwd{1, 0};
    glm::vec2 r = right_of(fwd);
    PedSeparation s = eval({0, 0}, fwd, {r * 2.0f});  // 2 m > 1.3 m radius
    REQUIRE(approx(s.lateral_target, 0.f));
    REQUIRE(!s.blocked);
    apricot_test::pass("beyond the radius contributes nothing");
}

void test_target_clamped_to_strip() {
    glm::vec2 fwd{1, 0};
    glm::vec2 r = right_of(fwd);
    // A tight knot of neighbours all on the right -> push saturates; the target
    // must clamp to the strip half-width, never shoving the ped off the kerb.
    std::vector<glm::vec2> knot;
    for (int k = 0; k < 6; ++k) knot.push_back(r * (0.15f + 0.05f * static_cast<float>(k)));
    PedSeparation s = eval({0, 0}, fwd, knot);
    REQUIRE(s.lateral_target <= -(PED_SEPARATION_MAX_OFFSET - 0.01f));
    REQUIRE(s.lateral_target >= -(PED_SEPARATION_MAX_OFFSET + 0.0001f));
    apricot_test::pass("a saturating push still clamps inside the strip");
}

void test_preferred_offset_when_uncrowded() {
    // With no neighbours a ped sits at its preferred line in the strip (not the
    // centreline) — this is what fans an uncrowded crowd across the pavement.
    PedSeparation s = ped_separation({0, 0}, {1, 0}, nullptr, 0,
                                     /*preferred*/ 0.4f, /*space*/ 1.f);
    REQUIRE(approx(s.lateral_target, 0.4f));
    REQUIRE(!s.blocked);
    apricot_test::pass("an uncrowded ped holds its preferred line");
}

void test_space_scale_widens_sidestep() {
    glm::vec2 fwd{1, 0};
    glm::vec2 r = right_of(fwd);
    std::vector<glm::vec2> one{r * 0.5f};  // a neighbour on the right
    PedSeparation timid = ped_separation({0, 0}, fwd, one.data(), one.size(),
                                         0.f, PED_SPACE_SCALE_MIN);
    PedSeparation pushy = ped_separation({0, 0}, fwd, one.data(), one.size(),
                                         0.f, PED_SPACE_SCALE_MAX);
    // Both sidestep left (negative); the space-demanding ped sidesteps harder.
    REQUIRE(timid.lateral_target < 0.f);
    REQUIRE(pushy.lateral_target < timid.lateral_target);
    apricot_test::pass("space_scale orders the two sidesteps");
}

void test_diagonal_forward_projects_correctly() {
    glm::vec2 fwd{1, 1};                 // non-axis-aligned lane
    glm::vec2 r = right_of(fwd);
    PedSeparation s = eval({3, -2}, fwd, {glm::vec2{3, -2} + r * 0.5f});
    REQUIRE(s.lateral_target < -0.01f);  // pushed left of the diagonal lane
    apricot_test::pass("the projection is in the lane frame, not world axes");
}

}  // namespace

int main() {
    test_no_neighbours();
    test_neighbour_on_right_pushes_left();
    test_neighbour_on_left_pushes_right();
    test_head_on_blocks_and_keeps_right();
    test_ahead_but_outside_block_dist_not_blocked();
    test_neighbour_beyond_radius_ignored();
    test_target_clamped_to_strip();
    test_preferred_offset_when_uncrowded();
    test_space_scale_widens_sidestep();
    test_diagonal_forward_projects_correctly();
    return apricot_test::done("ped_separation_tests");
}
