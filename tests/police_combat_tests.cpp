#include <cmath>

#include "game/police_combat.h"
#include "test_assert.h"

using namespace apricot;

namespace {

void cadence_and_spread_are_stable_identity_data() {
    const int64_t first = police_first_shot_step(100, 0xAABBCCDDu, 7);
    REQUIRE(first >= 100 + kPoliceShotReactionSteps);
    REQUIRE(first < 100 + kPoliceShotReactionSteps + 19);
    REQUIRE(first == police_first_shot_step(100, 0xAABBCCDDu, 7));

    const glm::vec3 origin{0.0f, 1.4f, 0.0f};
    const glm::vec3 torso{0.0f, 1.05f, -24.0f};
    const auto a = make_police_shot(0xAABBCCDDu, 7, 4, first, origin, torso);
    const auto b = make_police_shot(0xAABBCCDDu, 7, 4, first, origin, torso);
    REQUIRE(a.end == b.end);
    REQUIRE(glm::distance(a.end, torso) <= 1.88f + 1e-5f);
    apricot_test::pass("police reaction timing and shot spread key off stable officer identity");
}

void bullet_hit_uses_the_real_segment_not_a_hit_roll() {
    PoliceShotEvent shot;
    shot.origin = {0.0f, 1.05f, 8.0f};
    shot.end = {0.0f, 1.05f, 0.0f};
    const glm::vec3 torso{0.0f, 1.05f, 0.0f};
    REQUIRE(police_shot_hits_player(shot, torso));
    shot.end.x = kPoliceBulletHitRadiusM + 0.01f;
    REQUIRE(!police_shot_hits_player(shot, torso));
    shot.end.x = kPoliceBulletHitRadiusM;
    REQUIRE(police_shot_hits_player(shot, torso));
    apricot_test::pass("police bullets hit the player capsule only when the traced segment reaches it");
}

}  // namespace

int main() {
    cadence_and_spread_are_stable_identity_data();
    bullet_hit_uses_the_real_segment_not_a_hit_roll();
    return apricot_test::done("police_combat_tests");
}
